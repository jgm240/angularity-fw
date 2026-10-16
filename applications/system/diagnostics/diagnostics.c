#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/elements.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <toolbox/version.h>

typedef enum {
    DiagViewMenu,
    DiagViewBattery,
    DiagViewButtons,
    DiagViewOutputs,
    DiagViewSystem,
    DiagViewCrash,
} DiagView;

typedef struct {
    Gui* gui;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    View* battery;
    View* buttons;
    View* outputs;
    View* system;
    View* crash;
    NotificationApp* notification;
    FuriTimer* timer;
    DiagView current;
} DiagApp;

// ---------------------------------------------------------------- battery

typedef struct {
    uint8_t pct;
    uint8_t health;
    float voltage;
    float current_ma;
    float temp;
    bool charging;
    bool done;
    uint32_t full_mah;
    uint32_t design_mah;
} BatteryModel;

static void battery_read(BatteryModel* m) {
    m->pct = furi_hal_power_get_pct();
    m->health = furi_hal_power_get_bat_health_pct();
    m->voltage = furi_hal_power_get_battery_voltage(FuriHalPowerICFuelGauge);
    m->current_ma = furi_hal_power_get_battery_current(FuriHalPowerICFuelGauge) * 1000.0f;
    m->temp = furi_hal_power_get_battery_temperature(FuriHalPowerICFuelGauge);
    m->charging = furi_hal_power_is_charging();
    m->done = furi_hal_power_is_charging_done();
    m->full_mah = furi_hal_power_get_battery_full_capacity();
    m->design_mah = furi_hal_power_get_battery_design_capacity();
}

static void battery_draw(Canvas* canvas, void* model) {
    BatteryModel* m = model;
    char buf[48];
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Battery");
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "Charge %u%%   Health %u%%", m->pct, m->health);
    canvas_draw_str(canvas, 2, 22, buf);
    snprintf(
        buf,
        sizeof(buf),
        "%.2f V  %+.0f mA  %.0f C",
        (double)m->voltage,
        (double)m->current_ma,
        (double)m->temp);
    canvas_draw_str(canvas, 2, 33, buf);
    snprintf(
        buf, sizeof(buf), "Full %lu / design %lu mAh", (unsigned long)m->full_mah, (unsigned long)m->design_mah);
    canvas_draw_str(canvas, 2, 44, buf);
    canvas_draw_str(
        canvas, 2, 55, m->done ? "Charged" : (m->charging ? "Charging" : "On battery"));
}

// ---------------------------------------------------------------- buttons

typedef struct {
    uint8_t mask;
    InputKey last_key;
    InputType last_type;
    bool any;
    uint16_t count[6];
} ButtonsModel;

static const char* const key_names[] = {"Up", "Down", "Right", "Left", "OK", "Back"};
static const char* const type_names[] = {"Press", "Release", "Short", "Long", "Repeat"};

static void draw_key(Canvas* canvas, uint8_t mask, InputKey key, int x, int y, const char* label) {
    if(mask & (1 << key)) {
        canvas_draw_box(canvas, x, y, 14, 12);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(canvas, x + 7, y + 7, AlignCenter, AlignCenter, label);
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_draw_frame(canvas, x, y, 14, 12);
        canvas_draw_str_aligned(canvas, x + 7, y + 7, AlignCenter, AlignCenter, label);
    }
}

static void buttons_draw(Canvas* canvas, void* model) {
    ButtonsModel* m = model;
    canvas_set_font(canvas, FontSecondary);
    draw_key(canvas, m->mask, InputKeyUp, 22, 4, "^");
    draw_key(canvas, m->mask, InputKeyLeft, 6, 18, "<");
    draw_key(canvas, m->mask, InputKeyOk, 22, 18, "o");
    draw_key(canvas, m->mask, InputKeyRight, 38, 18, ">");
    draw_key(canvas, m->mask, InputKeyDown, 22, 32, "v");
    draw_key(canvas, m->mask, InputKeyBack, 6, 48, "B");

    char buf[32];
    if(m->any) {
        snprintf(buf, sizeof(buf), "%s", key_names[m->last_key]);
        canvas_draw_str(canvas, 66, 14, buf);
        snprintf(buf, sizeof(buf), "%s", type_names[m->last_type]);
        canvas_draw_str(canvas, 66, 26, buf);
        snprintf(buf, sizeof(buf), "x%u", m->count[m->last_key]);
        canvas_draw_str(canvas, 66, 38, buf);
    } else {
        canvas_draw_str(canvas, 66, 14, "Press any");
        canvas_draw_str(canvas, 66, 26, "button");
    }
    canvas_draw_str(canvas, 28, 60, "Hold Back = exit");
}

static bool buttons_input(InputEvent* event, void* context) {
    DiagApp* app = context;
    if(event->key == InputKeyBack && event->type == InputTypeLong) {
        app->current = DiagViewMenu;
        view_dispatcher_switch_to_view(app->dispatcher, DiagViewMenu);
        return true;
    }
    with_view_model(
        app->buttons,
        ButtonsModel * m,
        {
            if(event->type == InputTypePress) {
                m->mask |= 1 << event->key;
            } else if(event->type == InputTypeRelease) {
                m->mask &= ~(1 << event->key);
            }
            if(event->type == InputTypeShort) {
                m->count[event->key]++;
            }
            m->last_key = event->key;
            m->last_type = event->type;
            m->any = true;
        },
        true);
    return true;
}

// ---------------------------------------------------------------- outputs

typedef struct {
    uint8_t led;
    bool backlight_off;
} OutputsModel;

static const NotificationSequence sequence_diag_beep = {
    &message_note_c5,
    &message_delay_250,
    &message_sound_off,
    NULL,
};

static const char* const led_names[] = {"Off", "Red", "Green", "Blue", "White"};

static void outputs_draw(Canvas* canvas, void* model) {
    OutputsModel* m = model;
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Outputs");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 24, "OK    beep");
    canvas_draw_str(canvas, 2, 35, "Up    vibrate");
    char buf[32];
    snprintf(buf, sizeof(buf), "Down  LED: %s", led_names[m->led]);
    canvas_draw_str(canvas, 2, 46, buf);
    canvas_draw_str(canvas, 2, 57, "Left  blink backlight");
}

static void outputs_set_led(DiagApp* app, uint8_t led) {
    notification_message(app->notification, &sequence_reset_rgb);
    switch(led) {
    case 1:
        notification_message(app->notification, &sequence_set_only_red_255);
        break;
    case 2:
        notification_message(app->notification, &sequence_set_only_green_255);
        break;
    case 3:
        notification_message(app->notification, &sequence_set_only_blue_255);
        break;
    case 4:
        notification_message(app->notification, &sequence_set_red_255);
        notification_message(app->notification, &sequence_set_green_255);
        notification_message(app->notification, &sequence_set_blue_255);
        break;
    default:
        break;
    }
}

static bool outputs_input(InputEvent* event, void* context) {
    DiagApp* app = context;
    if(event->type != InputTypeShort) return false;

    switch(event->key) {
    case InputKeyOk:
        notification_message(app->notification, &sequence_diag_beep);
        return true;
    case InputKeyUp:
        notification_message(app->notification, &sequence_single_vibro);
        return true;
    case InputKeyDown:
        with_view_model(
            app->outputs,
            OutputsModel * m,
            {
                m->led = (m->led + 1) % 5;
                outputs_set_led(app, m->led);
            },
            true);
        return true;
    case InputKeyLeft:
        notification_message(app->notification, &sequence_display_backlight_off);
        furi_delay_ms(300);
        notification_message(app->notification, &sequence_display_backlight_on);
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------- system

static void system_draw(Canvas* canvas, void* model) {
    UNUSED(model);
    char buf[48];
    const Version* v = version_get();

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "System");
    canvas_set_font(canvas, FontSecondary);

    snprintf(buf, sizeof(buf), "FW %s %s", version_get_version(v), version_get_githash(v));
    canvas_draw_str(canvas, 2, 22, buf);

    snprintf(
        buf,
        sizeof(buf),
        "Heap %uK free / %uK, min %uK",
        (unsigned)(memmgr_get_free_heap() / 1024),
        (unsigned)(memmgr_get_total_heap() / 1024),
        (unsigned)(memmgr_get_minimum_free_heap() / 1024));
    canvas_draw_str(canvas, 2, 33, buf);

    uint32_t up = furi_get_tick() / 1000;
    snprintf(
        buf,
        sizeof(buf),
        "Uptime %luh %lum %lus",
        (unsigned long)(up / 3600),
        (unsigned long)((up / 60) % 60),
        (unsigned long)(up % 60));
    canvas_draw_str(canvas, 2, 44, buf);

    snprintf(
        buf,
        sizeof(buf),
        "HW rev %u  %s",
        furi_hal_version_get_hw_version(),
        furi_hal_version_get_model_name());
    canvas_draw_str(canvas, 2, 55, buf);
}

// ---------------------------------------------------------------- last crash

typedef struct {
    char message[64];
    bool have;
} CrashModel;

static void crash_load(CrashModel* m) {
    m->have = false;
    m->message[0] = '\0';
    uint32_t ptr = furi_hal_rtc_get_fault_data();
    // The crash handler stores a pointer to a message string in flash
    if(ptr >= FLASH_BASE && ptr < (FLASH_BASE + FLASH_SIZE)) {
        const char* src = (const char*)ptr;
        size_t i = 0;
        for(; i < sizeof(m->message) - 1 && src[i]; i++) {
            if(src[i] < 32 || src[i] > 126) break; // not text: stale pointer after an update
            m->message[i] = src[i];
        }
        m->message[i] = '\0';
        m->have = (i > 0);
    }
}

static void crash_draw(Canvas* canvas, void* model) {
    CrashModel* m = model;
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Last crash");
    canvas_set_font(canvas, FontSecondary);
    if(m->have) {
        elements_multiline_text_aligned(canvas, 2, 20, AlignLeft, AlignTop, m->message);
        canvas_draw_str(canvas, 2, 52, "May be stale after updates");
        canvas_draw_str(canvas, 2, 62, "OK = clear");
    } else {
        canvas_draw_str(canvas, 2, 24, "No crash recorded");
    }
}

static bool crash_input(InputEvent* event, void* context) {
    DiagApp* app = context;
    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        furi_hal_rtc_set_fault_data(0);
        with_view_model(app->crash, CrashModel * m, { crash_load(m); }, true);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- plumbing

static uint32_t previous_to_menu(void* context) {
    DiagApp* app = context;
    app->current = DiagViewMenu;
    notification_message(app->notification, &sequence_reset_rgb);
    return DiagViewMenu;
}

static void timer_callback(void* context) {
    DiagApp* app = context;
    if(app->current == DiagViewBattery) {
        with_view_model(app->battery, BatteryModel * m, { battery_read(m); }, true);
    } else if(app->current == DiagViewSystem) {
        with_view_model(app->system, void* m, { UNUSED(m); }, true);
    }
}

static void menu_callback(void* context, uint32_t index) {
    DiagApp* app = context;
    switch(index) {
    case DiagViewBattery:
        with_view_model(app->battery, BatteryModel * m, { battery_read(m); }, true);
        break;
    case DiagViewCrash:
        with_view_model(app->crash, CrashModel * m, { crash_load(m); }, true);
        break;
    default:
        break;
    }
    app->current = index;
    view_dispatcher_switch_to_view(app->dispatcher, index);
}

static bool navigation_callback(void* context) {
    UNUSED(context);
    return false; // stop the dispatcher: Back from the menu exits the app
}

static View* make_view(
    DiagApp* app,
    size_t model_size,
    ViewDrawCallback draw,
    ViewInputCallback input,
    DiagView id) {
    View* view = view_alloc();
    if(model_size) {
        view_allocate_model(view, ViewModelTypeLocking, model_size);
    }
    view_set_context(view, app);
    view_set_draw_callback(view, draw);
    if(input) view_set_input_callback(view, input);
    view_set_previous_callback(view, previous_to_menu);
    view_dispatcher_add_view(app->dispatcher, id, view);
    return view;
}

int32_t diagnostics_app(void* p) {
    UNUSED(p);
    DiagApp* app = malloc(sizeof(DiagApp));
    memset(app, 0, sizeof(DiagApp));
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, navigation_callback);

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Battery", DiagViewBattery, menu_callback, app);
    submenu_add_item(app->menu, "Button test", DiagViewButtons, menu_callback, app);
    submenu_add_item(app->menu, "Speaker / vibro / LED", DiagViewOutputs, menu_callback, app);
    submenu_add_item(app->menu, "System info", DiagViewSystem, menu_callback, app);
    submenu_add_item(app->menu, "Last crash", DiagViewCrash, menu_callback, app);
    view_dispatcher_add_view(app->dispatcher, DiagViewMenu, submenu_get_view(app->menu));

    app->battery = make_view(app, sizeof(BatteryModel), battery_draw, NULL, DiagViewBattery);
    app->buttons = make_view(app, sizeof(ButtonsModel), buttons_draw, buttons_input, DiagViewButtons);
    // The button test consumes Back itself (long press leaves), so no previous callback
    view_set_previous_callback(app->buttons, NULL);
    app->outputs = make_view(app, sizeof(OutputsModel), outputs_draw, outputs_input, DiagViewOutputs);
    app->system = make_view(app, 0, system_draw, NULL, DiagViewSystem);
    app->crash = make_view(app, sizeof(CrashModel), crash_draw, crash_input, DiagViewCrash);

    app->current = DiagViewMenu;
    app->timer = furi_timer_alloc(timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->timer, furi_ms_to_ticks(500));

    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->dispatcher, DiagViewMenu);
    view_dispatcher_run(app->dispatcher);

    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);
    notification_message(app->notification, &sequence_reset_rgb);

    view_dispatcher_remove_view(app->dispatcher, DiagViewMenu);
    view_dispatcher_remove_view(app->dispatcher, DiagViewBattery);
    view_dispatcher_remove_view(app->dispatcher, DiagViewButtons);
    view_dispatcher_remove_view(app->dispatcher, DiagViewOutputs);
    view_dispatcher_remove_view(app->dispatcher, DiagViewSystem);
    view_dispatcher_remove_view(app->dispatcher, DiagViewCrash);
    submenu_free(app->menu);
    view_free(app->battery);
    view_free(app->buttons);
    view_free(app->outputs);
    view_free(app->system);
    view_free(app->crash);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    free(app);
    return 0;
}
