#include "screenshot.h"
#include "screenshot_bmp.h"

#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>
#include <toolbox/saved_struct.h>
#include <datetime/datetime.h>

#define SCREENSHOT_MAGIC   0x5D
#define SCREENSHOT_VERSION 1
#define HOLD_MS            900

typedef struct {
    bool enabled;
    uint8_t combo;
} ScreenshotSettings;

typedef enum {
    FlagShot = (1 << 0),
} ScreenshotFlags;

static const struct {
    const char* name;
    uint8_t keys; // bit per InputKey
} combos[SCREENSHOT_COMBO_COUNT] = {
    {"Left + Right", (1 << InputKeyLeft) | (1 << InputKeyRight)},
    {"Up + Down", (1 << InputKeyUp) | (1 << InputKeyDown)},
    {"Left + OK", (1 << InputKeyLeft) | (1 << InputKeyOk)},
    {"Right + OK", (1 << InputKeyRight) | (1 << InputKeyOk)},
};

static struct {
    bool started;
    ScreenshotSettings settings;
    uint8_t frames[2][SHOT_W * SHOT_H / 8];
    volatile uint8_t active; // frame buffer the GUI finished writing last
    volatile bool have_frame;
    volatile CanvasOrientation orientation;
    volatile uint8_t keys_down;
    volatile bool armed; // false until the combo was released after a shot
    FuriTimer* hold_timer;
    FuriThread* thread;
} shot;

// ---------------------------------------------------------------- settings

static void settings_load(void) {
    memset(&shot.settings, 0, sizeof(shot.settings));
    if(!saved_struct_load(
           SCREENSHOT_SETTINGS_PATH,
           &shot.settings,
           sizeof(shot.settings),
           SCREENSHOT_MAGIC,
           SCREENSHOT_VERSION) ||
       shot.settings.combo >= SCREENSHOT_COMBO_COUNT) {
        memset(&shot.settings, 0, sizeof(shot.settings));
    }
}

static void settings_save(void) {
    saved_struct_save(
        SCREENSHOT_SETTINGS_PATH,
        &shot.settings,
        sizeof(shot.settings),
        SCREENSHOT_MAGIC,
        SCREENSHOT_VERSION);
}

bool screenshot_is_enabled(void) {
    if(!shot.started) settings_load();
    return shot.settings.enabled;
}

void screenshot_set_enabled(bool enabled) {
    if(!shot.started) settings_load();
    shot.settings.enabled = enabled;
    settings_save();
    // hooks are only installed once the feature is on; the settings were saved first because
    // screenshot_start() reloads them
    if(enabled) screenshot_start();
}

uint8_t screenshot_get_combo(void) {
    if(!shot.started) settings_load();
    return shot.settings.combo;
}

void screenshot_set_combo(uint8_t combo) {
    if(!shot.started) settings_load();
    if(combo >= SCREENSHOT_COMBO_COUNT) return;
    shot.settings.combo = combo;
    settings_save();
}

const char* screenshot_combo_name(uint8_t combo) {
    return combo < SCREENSHOT_COMBO_COUNT ? combos[combo].name : "?";
}

// ---------------------------------------------------------------- capture

static bool write_to_file(void* ctx, const void* data, size_t len) {
    return storage_file_write((File*)ctx, data, len) == len;
}

bool screenshot_take(void) {
    if(!shot.have_frame) return false;

    // snapshot the frame we are going to encode: the GUI may keep drawing meanwhile
    uint8_t* frame = malloc(sizeof(shot.frames[0]));
    memcpy(frame, shot.frames[shot.active], sizeof(shot.frames[0]));
    // Left-handed mode draws the screen rotated; turn it back so the picture is upright
    bool rotate = (shot.orientation == CanvasOrientationHorizontalFlip);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, SCREENSHOT_PATH_DIR);

    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    FuriString* path = furi_string_alloc_printf(
        "%s/screenshot_%04u%02u%02u_%02u%02u%02u.bmp",
        SCREENSHOT_PATH_DIR,
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);

    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = screenshot_bmp_write(frame, rotate, write_to_file, file);
    }
    storage_file_close(file);
    storage_file_free(file);
    if(!ok) storage_common_remove(storage, furi_string_get_cstr(path));

    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);
    free(frame);

    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, ok ? &sequence_blink_cyan_100 : &sequence_error);
    if(ok) notification_message(notification, &sequence_single_vibro);
    furi_record_close(RECORD_NOTIFICATION);
    return ok;
}

// ---------------------------------------------------------------- hooks

// Called by the GUI after every frame (GUI thread): keep a copy, nothing else.
static void framebuffer_callback(
    uint8_t* data,
    size_t size,
    CanvasOrientation orientation,
    void* context) {
    UNUSED(context);
    if(size != sizeof(shot.frames[0])) return;
    uint8_t next = shot.active ^ 1;
    memcpy(shot.frames[next], data, size);
    shot.orientation = orientation;
    shot.active = next;
    shot.have_frame = true;
}

static void hold_timer_callback(void* context) {
    UNUSED(context);
    if(shot.settings.enabled && shot.armed && shot.keys_down == combos[shot.settings.combo].keys) {
        shot.armed = false;
        furi_thread_flags_set(furi_thread_get_id(shot.thread), FlagShot);
    }
}

// Called for every key event (input thread): bookkeeping only.
static void input_callback(const void* value, void* context) {
    UNUSED(context);
    const InputEvent* event = value;
    uint8_t bit = 1 << event->key;
    if(event->type == InputTypePress) {
        shot.keys_down |= bit;
    } else if(event->type == InputTypeRelease) {
        shot.keys_down &= ~bit;
    } else {
        return;
    }

    if(shot.keys_down == 0) shot.armed = true;

    if(shot.settings.enabled && shot.keys_down == combos[shot.settings.combo].keys) {
        furi_timer_start(shot.hold_timer, furi_ms_to_ticks(HOLD_MS));
    } else {
        furi_timer_stop(shot.hold_timer);
    }
}

static int32_t screenshot_thread(void* context) {
    UNUSED(context);
    while(true) {
        uint32_t flags = furi_thread_flags_wait(FlagShot, FuriFlagWaitAny, FuriWaitForever);
        if(flags & FlagShot) screenshot_take();
    }
    return 0;
}

void screenshot_start(void) {
    if(shot.started) return;
    memset(&shot, 0, sizeof(shot));
    settings_load();
    shot.armed = true;

    shot.thread = furi_thread_alloc_ex("Screenshot", 4 * 1024, screenshot_thread, NULL);
    furi_thread_start(shot.thread);
    shot.hold_timer = furi_timer_alloc(hold_timer_callback, FuriTimerTypeOnce, NULL);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_framebuffer_callback(gui, framebuffer_callback, NULL);
    // the GUI record stays open for the lifetime of the service

    FuriPubSub* input_events = furi_record_open(RECORD_INPUT_EVENTS);
    furi_pubsub_subscribe(input_events, input_callback, NULL);

    shot.started = true;
}
