#include "usb_audio.h"
#include "usb/usb_audio_driver.h"
#include "usb_audio_hal.h"

#include <input/input.h>

typedef enum {
    UsbAudioEventExit,
} UsbAudioEventType;

typedef struct {
    UsbAudioEventType type;
} UsbAudioEvent;

#define VOLUME_STEP 50
#define VOLUME_MIN 0
#define VOLUME_MAX 1500

static void draw_callback(Canvas* canvas, void* ctx) {
    furi_assert(ctx);
    UsbAudioApp* app = ctx;

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 14, AlignCenter, AlignCenter, "USB Audio");

    canvas_set_font(canvas, FontSecondary);
    if(app->usb_locked) {
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, "USB is busy");
        canvas_draw_str_aligned(
            canvas, 64, 40, AlignCenter, AlignCenter, "Close qFlipper / other USB tool");
        canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignCenter, "Hold Back to exit");
        return;
    }

    canvas_draw_str_aligned(
        canvas,
        64,
        30,
        AlignCenter,
        AlignCenter,
        app->streaming ? "Streaming..." : "Connect via USB");

    char volume_line[32];
    snprintf(volume_line, sizeof(volume_line), "Volume: %d%%  Up/Down", (int)app->volume_percent);
    canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignCenter, volume_line);
    canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignCenter, "Hold Back to exit");
}

static void input_callback(InputEvent* input, void* ctx) {
    furi_assert(ctx);
    UsbAudioApp* app = ctx;

    if(input->key == InputKeyBack && input->type == InputTypeLong) {
        UsbAudioEvent event = {.type = UsbAudioEventExit};
        furi_message_queue_put(app->events, &event, FuriWaitForever);
        return;
    }

    if((input->type != InputTypeShort) && (input->type != InputTypeRepeat)) return;

    if(input->key == InputKeyUp) {
        app->volume_percent = MIN(app->volume_percent + VOLUME_STEP, VOLUME_MAX);
        usb_audio_set_volume_percent(app->volume_percent);
    } else if(input->key == InputKeyDown) {
        app->volume_percent = MAX(app->volume_percent - VOLUME_STEP, VOLUME_MIN);
        usb_audio_set_volume_percent(app->volume_percent);
    }
}

static void redraw_timer_callback(void* ctx) {
    furi_assert(ctx);
    UsbAudioApp* app = ctx;
    view_port_update(app->view_port);
}

static void stream_state_callback(bool streaming, void* ctx) {
    UsbAudioApp* app = ctx;
    app->streaming = streaming;
}

static UsbAudioApp* usb_audio_app_alloc() {
    UsbAudioApp* app = malloc(sizeof(UsbAudioApp));
    app->events = furi_message_queue_alloc(4, sizeof(UsbAudioEvent));
    app->streaming = false;
    app->usb_locked = false;
    app->volume_percent = 100;

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->redraw_timer =
        furi_timer_alloc(redraw_timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->redraw_timer, furi_ms_to_ticks(250));

    return app;
}

static void usb_audio_app_free(UsbAudioApp* app) {
    furi_timer_stop(app->redraw_timer);
    furi_timer_free(app->redraw_timer);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app->events);
    free(app);
}

int32_t usb_audio_app(void* p) {
    UNUSED(p);
    UsbAudioApp* app = usb_audio_app_alloc();

    app->usb_config_restore = furi_hal_usb_get_config();
    app->usb_locked = !furi_hal_usb_set_config(&usb_audio_interface, NULL);

    if(!app->usb_locked) {
        if(!furi_hal_speaker_acquire(1000)) {
            furi_hal_usb_set_config(app->usb_config_restore, NULL);
            usb_audio_app_free(app);
            return -1;
        }

        usb_audio_hal_init(USB_AUDIO_SAMPLE_RATE);
        usb_audio_hal_start();

        usb_audio_set_stream_state_callback(stream_state_callback, app);
        usb_audio_set_volume_percent(app->volume_percent);
    }

    UsbAudioEvent event;
    while(furi_message_queue_get(app->events, &event, FuriWaitForever) == FuriStatusOk) {
        if(event.type == UsbAudioEventExit) break;
    }

    if(!app->usb_locked) {
        furi_hal_usb_set_config(app->usb_config_restore, NULL);
        usb_audio_set_stream_state_callback(NULL, NULL);

        usb_audio_hal_stop();
        usb_audio_hal_deinit();
        furi_hal_speaker_release();
    }

    usb_audio_app_free(app);
    return 0;
}
