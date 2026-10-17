#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/elements.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/text_input.h>
#include <notification/notification_messages.h>
#include <notification/notification_app.h>
#include <storage/storage.h>
#include <datetime/datetime.h>

#include "standby_draw.h"
#include "devboard.h"

#define TAG "Standby"

#define SETTINGS_PATH EXT_PATH("apps_data/standby/settings.bin")
#define WEATHER_PATH  EXT_PATH("apps_data/standby/weather.json")
#define JWLINK_PATH   EXT_PATH("apps_data/standby/jwlink.json")

#define SETTINGS_MAGIC 0x53425931u // "SBY1"
#define JSON_BUFFER    4096
#define JWLINK_BUFFER  2048
#define NOTICE_MS      6000

typedef struct {
    uint32_t magic;
    char city[32];
    char tz[40];
    int32_t lat; // degrees * 10000
    int32_t lon;
    bool has_place;
    bool fahrenheit;
    bool h24;
    bool monday_first;
    bool sync_time_on_start;
    uint8_t dim_pct;
    uint8_t dim_after_s; // 0 = never dim
    uint8_t refresh_min; // 0 = manual only
} StandbySettings;

typedef enum {
    ViewDisplay,
    ViewMenu,
    ViewSettings,
    ViewCity,
} AppView;

typedef enum {
    EventTick = 1,
    EventWorkerDone,
    MenuRefresh = 10,
    MenuSync,
    MenuSettings,
    MenuClose,
} AppEvent;

typedef enum {
    JobRefresh,
    JobSyncTime,
} Job;

typedef struct {
    Gui* gui;
    ViewDispatcher* dispatcher;
    View* display;
    Submenu* menu;
    VariableItemList* settings_list;
    TextInput* city_input;
    Storage* storage;
    NotificationApp* notification;
    FuriTimer* timer;
    FuriThread* worker;
    volatile bool cancel;
    Job job;

    StandbySettings settings;
    bool settings_dirty;
    StandbyWeather weather;
    StandbyJwlink jw;
    char city_buf[32];

    uint8_t page;
    float original_brightness;
    bool dimmed;
    bool swallow_press;
    uint32_t last_input;
    uint32_t last_jwlink_load;
    uint32_t last_auto_try;
    bool auto_tried;
    bool refresh_after_settings;
} StandbyApp;

// ------------------------------------------------------------ settings / files

static void settings_defaults(StandbySettings* s) {
    memset(s, 0, sizeof(*s));
    s->magic = SETTINGS_MAGIC;
    s->h24 = true;
    s->monday_first = true;
    s->dim_pct = 8;
    s->dim_after_s = 20;
    s->refresh_min = 30;
}

static void ensure_data_dir(Storage* storage) {
    storage_common_mkdir(storage, EXT_PATH("apps_data"));
    storage_common_mkdir(storage, EXT_PATH("apps_data/standby"));
}

static void settings_load(StandbyApp* app) {
    settings_defaults(&app->settings);
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        StandbySettings loaded;
        if(storage_file_read(f, &loaded, sizeof(loaded)) == sizeof(loaded) &&
           loaded.magic == SETTINGS_MAGIC && loaded.dim_pct >= 1 && loaded.dim_pct <= 100) {
            loaded.city[sizeof(loaded.city) - 1] = '\0';
            loaded.tz[sizeof(loaded.tz) - 1] = '\0';
            app->settings = loaded;
        }
    }
    storage_file_close(f);
    storage_file_free(f);
}

static void settings_save(StandbyApp* app) {
    ensure_data_dir(app->storage);
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, &app->settings, sizeof(app->settings));
    }
    storage_file_close(f);
    storage_file_free(f);
    app->settings_dirty = false;
}

// Reads a whole (small) file into a heap buffer, NUL terminated. Caller frees.
static char* read_text_file(Storage* storage, const char* path, size_t max) {
    File* f = storage_file_alloc(storage);
    char* buf = NULL;
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        buf = malloc(max + 1);
        size_t n = storage_file_read(f, buf, max);
        buf[n] = '\0';
    }
    storage_file_close(f);
    storage_file_free(f);
    return buf;
}

static void weather_load(StandbyApp* app) {
    char* text = read_text_file(app->storage, WEATHER_PATH, 512);
    if(text) {
        if(!standby_weather_deserialize(text, &app->weather)) memset(&app->weather, 0, sizeof(app->weather));
        free(text);
    }
}

static void weather_store(StandbyApp* app) {
    char text[400];
    if(!standby_weather_serialize(&app->weather, text, sizeof(text))) return;
    ensure_data_dir(app->storage);
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, WEATHER_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, text, strlen(text));
    }
    storage_file_close(f);
    storage_file_free(f);
}

static void jwlink_load(StandbyApp* app) {
    char* text = read_text_file(app->storage, JWLINK_PATH, JWLINK_BUFFER);
    if(text) {
        StandbyJwlink parsed;
        if(standby_parse_jwlink(text, &parsed)) {
            app->jw = parsed;
        }
        free(text);
    }
}

static uint32_t local_timestamp(void) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    return datetime_datetime_to_timestamp(&dt);
}

// ------------------------------------------------------------ display model

static void model_push(StandbyApp* app, const char* notice) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    uint32_t now_ts = datetime_datetime_to_timestamp(&now);
    uint8_t pct = furi_hal_power_get_pct();
    bool charging = furi_hal_power_is_charging();
    with_view_model(
        app->display,
        DisplayModel * m,
        {
            m->now = now;
            m->now_ts = now_ts;
            m->page = app->page;
            m->battery = pct;
            m->charging = charging;
            m->dimmed = app->dimmed;
            m->fahrenheit = app->settings.fahrenheit;
            m->h24 = app->settings.h24;
            m->monday_first = app->settings.monday_first;
            m->weather = app->weather;
            m->jw = app->jw;
            if(notice) snprintf(m->notice, sizeof(m->notice), "%s", notice);
        },
        true);
}

static void notice_set(StandbyApp* app, const char* text) {
    with_view_model(
        app->display, DisplayModel * m, { snprintf(m->notice, sizeof(m->notice), "%s", text); }, true);
}

// ------------------------------------------------------------ backlight

static void apply_dim(StandbyApp* app, bool dim) {
    if(app->dimmed == dim) return;
    app->dimmed = dim;
    if(dim) {
        app->notification->settings.display_brightness = app->settings.dim_pct / 100.0f;
    } else {
        app->notification->settings.display_brightness = app->original_brightness;
    }
    notification_message(app->notification, &sequence_display_backlight_on);
}

// ------------------------------------------------------------ worker: weather, place, time

static bool fetch_json(DevBoard* d, const char* url, char* buf, size_t cap) {
    return devboard_get_text(d, url, buf, cap);
}

static bool ensure_place(StandbyApp* app, DevBoard* d, char* buf) {
    StandbySettings* s = &app->settings;
    if(s->has_place) return true;
    StandbyPlace place;
    char url[200];
    if(s->city[0]) {
        char enc[96];
        standby_url_encode(s->city, enc, sizeof(enc));
        snprintf(
            url,
            sizeof(url),
            "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=en&format=json",
            enc);
        if(!fetch_json(d, url, buf, JSON_BUFFER)) return false;
        if(!standby_parse_geocode(buf, &place)) {
            devboard_set_error(d, "City not found");
            return false;
        }
    } else {
        if(!fetch_json(d, "https://ipwhois.app/json/", buf, JSON_BUFFER)) return false;
        if(!standby_parse_ipwhois(buf, &place)) {
            devboard_set_error(d, "Location lookup failed");
            return false;
        }
        snprintf(s->city, sizeof(s->city), "%s", place.name);
    }
    s->lat = place.lat;
    s->lon = place.lon;
    snprintf(s->tz, sizeof(s->tz), "%s", place.tz);
    s->has_place = true;
    app->settings_dirty = true;
    return true;
}

static bool do_refresh(StandbyApp* app, DevBoard* d, char* buf) {
    if(!ensure_place(app, d, buf)) return false;
    StandbySettings* s = &app->settings;
    char lat[16], lon[16], url[330];
    standby_fmt_coord(s->lat, lat, sizeof(lat));
    standby_fmt_coord(s->lon, lon, sizeof(lon));
    snprintf(
        url,
        sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,is_day&daily=temperature_2m_max,temperature_2m_min,weather_code&timezone=auto&forecast_days=3",
        lat,
        lon);
    if(!fetch_json(d, url, buf, JSON_BUFFER)) return false;
    StandbyWeather w;
    memset(&w, 0, sizeof(w));
    if(!standby_parse_forecast(buf, &w)) {
        devboard_set_error(d, "Unreadable weather reply");
        return false;
    }
    snprintf(w.city, sizeof(w.city), "%s", s->city);
    w.fetched = local_timestamp();
    app->weather = w;
    weather_store(app);
    return true;
}

static bool do_sync_time(StandbyApp* app, DevBoard* d, char* buf) {
    if(!ensure_place(app, d, buf)) return false;
    char enc[96], url[200];
    standby_url_encode(app->settings.tz, enc, sizeof(enc));
    snprintf(url, sizeof(url), "https://timeapi.io/api/time/current/zone?timeZone=%s", enc);
    if(!fetch_json(d, url, buf, JSON_BUFFER)) return false;
    StandbyClock c;
    if(!standby_parse_timeapi(buf, &c)) {
        devboard_set_error(d, "Unreadable time reply");
        return false;
    }
    DateTime dt = {
        .hour = c.hour,
        .minute = c.minute,
        .second = c.second,
        .day = c.day,
        .month = c.month,
        .year = c.year,
    };
    int dow = standby_day_of_week(c.year, c.month, c.day);
    dt.weekday = dow == 0 ? 7 : dow;
    furi_hal_rtc_set_datetime(&dt);
    return true;
}

static int32_t worker_thread(void* context) {
    StandbyApp* app = context;
    char* buf = malloc(JSON_BUFFER);
    DevBoard d;
    char msg[48];

    notice_set(app, app->job == JobSyncTime ? "Syncing clock..." : "Updating weather...");
    bool ok = false;
    if(devboard_open(&d, &app->cancel)) {
        if(devboard_ping(&d)) {
            ok = (app->job == JobSyncTime) ? do_sync_time(app, &d, buf) : do_refresh(app, &d, buf);
        }
        devboard_close(&d);
    }
    if(ok) {
        DateTime now;
        furi_hal_rtc_get_datetime(&now);
        snprintf(
            msg,
            sizeof(msg),
            "%s %02d:%02d",
            app->job == JobSyncTime ? "Clock set" : "Weather updated",
            now.hour,
            now.minute);
    } else {
        snprintf(msg, sizeof(msg), "%.43s", d.error[0] ? d.error : "Failed");
    }
    notice_set(app, msg);
    free(buf);
    view_dispatcher_send_custom_event(app->dispatcher, EventWorkerDone);
    return 0;
}

static void start_job(StandbyApp* app, Job job) {
    if(app->worker) return; // one at a time
    app->job = job;
    app->cancel = false;
    app->worker = furi_thread_alloc_ex("StandbyWorker", 4 * 1024, worker_thread, app);
    furi_thread_start(app->worker);
}

// ------------------------------------------------------------ settings screens

static const uint8_t dim_levels[] = {1, 2, 3, 5, 8, 10, 15, 20, 30, 50};
static const uint8_t dim_after_values[] = {0, 5, 10, 20, 30, 60, 120};
static const uint8_t refresh_values[] = {0, 15, 30, 60, 120};

static uint8_t index_of(const uint8_t* values, size_t count, uint8_t value) {
    for(size_t i = 0; i < count; i++)
        if(values[i] == value) return i;
    return 0;
}

static void on_units(VariableItem* item) {
    StandbyApp* app = variable_item_get_context(item);
    app->settings.fahrenheit = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, app->settings.fahrenheit ? "F / mph" : "C / km/h");
    app->settings_dirty = true;
}
static void on_clock(VariableItem* item) {
    StandbyApp* app = variable_item_get_context(item);
    app->settings.h24 = !variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, app->settings.h24 ? "24 hour" : "12 hour");
    app->settings_dirty = true;
}
static void on_week(VariableItem* item) {
    StandbyApp* app = variable_item_get_context(item);
    app->settings.monday_first = !variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, app->settings.monday_first ? "Monday" : "Sunday");
    app->settings_dirty = true;
}
static void on_dim_level(VariableItem* item) {
    StandbyApp* app = variable_item_get_context(item);
    app->settings.dim_pct = dim_levels[variable_item_get_current_value_index(item)];
    char text[8];
    snprintf(text, sizeof(text), "%u%%", app->settings.dim_pct);
    variable_item_set_current_value_text(item, text);
    app->settings_dirty = true;
}
static void on_dim_after(VariableItem* item) {
    StandbyApp* app = variable_item_get_context(item);
    app->settings.dim_after_s = dim_after_values[variable_item_get_current_value_index(item)];
    char text[12];
    if(app->settings.dim_after_s == 0) {
        snprintf(text, sizeof(text), "Never");
    } else {
        snprintf(text, sizeof(text), "%u s", app->settings.dim_after_s);
    }
    variable_item_set_current_value_text(item, text);
    app->settings_dirty = true;
}
static void on_refresh(VariableItem* item) {
    StandbyApp* app = variable_item_get_context(item);
    app->settings.refresh_min = refresh_values[variable_item_get_current_value_index(item)];
    char text[12];
    if(app->settings.refresh_min == 0) {
        snprintf(text, sizeof(text), "Manual");
    } else {
        snprintf(text, sizeof(text), "%u min", app->settings.refresh_min);
    }
    variable_item_set_current_value_text(item, text);
    app->settings_dirty = true;
}
static void on_sync_start(VariableItem* item) {
    StandbyApp* app = variable_item_get_context(item);
    app->settings.sync_time_on_start = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, app->settings.sync_time_on_start ? "ON" : "OFF");
    app->settings_dirty = true;
}

enum {
    SettingsIndexCity,
    SettingsIndexUnits,
    SettingsIndexClock,
    SettingsIndexWeek,
    SettingsIndexDimLevel,
    SettingsIndexDimAfter,
    SettingsIndexRefresh,
    SettingsIndexSyncStart,
    SettingsIndexAutoLocation,
};

static void settings_enter(void* context, uint32_t index) {
    StandbyApp* app = context;
    view_dispatcher_send_custom_event(app->dispatcher, 100 + index);
}

static void settings_populate(StandbyApp* app) {
    VariableItemList* l = app->settings_list;
    variable_item_list_reset(l);
    StandbySettings* s = &app->settings;
    VariableItem* item;

    item = variable_item_list_add(l, "City", 0, NULL, app);
    variable_item_set_current_value_text(item, s->city[0] ? s->city : "(auto)");

    item = variable_item_list_add(l, "Units", 2, on_units, app);
    variable_item_set_current_value_index(item, s->fahrenheit);
    variable_item_set_current_value_text(item, s->fahrenheit ? "F / mph" : "C / km/h");

    item = variable_item_list_add(l, "Clock", 2, on_clock, app);
    variable_item_set_current_value_index(item, !s->h24);
    variable_item_set_current_value_text(item, s->h24 ? "24 hour" : "12 hour");

    item = variable_item_list_add(l, "Week starts", 2, on_week, app);
    variable_item_set_current_value_index(item, !s->monday_first);
    variable_item_set_current_value_text(item, s->monday_first ? "Monday" : "Sunday");

    item = variable_item_list_add(l, "Dim level", COUNT_OF(dim_levels), on_dim_level, app);
    uint8_t i = index_of(dim_levels, COUNT_OF(dim_levels), s->dim_pct);
    variable_item_set_current_value_index(item, i);
    char text[12];
    snprintf(text, sizeof(text), "%u%%", dim_levels[i]);
    variable_item_set_current_value_text(item, text);

    item = variable_item_list_add(l, "Dim after", COUNT_OF(dim_after_values), on_dim_after, app);
    i = index_of(dim_after_values, COUNT_OF(dim_after_values), s->dim_after_s);
    variable_item_set_current_value_index(item, i);
    if(dim_after_values[i] == 0) {
        snprintf(text, sizeof(text), "Never");
    } else {
        snprintf(text, sizeof(text), "%u s", dim_after_values[i]);
    }
    variable_item_set_current_value_text(item, text);

    item = variable_item_list_add(l, "Auto refresh", COUNT_OF(refresh_values), on_refresh, app);
    i = index_of(refresh_values, COUNT_OF(refresh_values), s->refresh_min);
    variable_item_set_current_value_index(item, i);
    if(refresh_values[i] == 0) {
        snprintf(text, sizeof(text), "Manual");
    } else {
        snprintf(text, sizeof(text), "%u min", refresh_values[i]);
    }
    variable_item_set_current_value_text(item, text);

    item = variable_item_list_add(l, "Sync clock on start", 2, on_sync_start, app);
    variable_item_set_current_value_index(item, s->sync_time_on_start);
    variable_item_set_current_value_text(item, s->sync_time_on_start ? "ON" : "OFF");

    variable_item_list_add(l, "Use internet location", 0, NULL, app);

    variable_item_list_set_enter_callback(l, settings_enter, app);
}

static void city_entered(void* context) {
    StandbyApp* app = context;
    snprintf(app->settings.city, sizeof(app->settings.city), "%s", app->city_buf);
    app->settings.has_place = false; // look the new city up on the next refresh
    app->settings_dirty = true;
    app->refresh_after_settings = true;
    settings_populate(app);
    view_dispatcher_switch_to_view(app->dispatcher, ViewSettings);
}

static uint32_t previous_to_display(void* context) {
    UNUSED(context);
    return ViewDisplay;
}

static uint32_t previous_to_settings(void* context) {
    UNUSED(context);
    return ViewSettings;
}

// ------------------------------------------------------------ events

static void menu_callback(void* context, uint32_t index) {
    StandbyApp* app = context;
    view_dispatcher_send_custom_event(app->dispatcher, index);
}

static void tick(StandbyApp* app) {
    uint32_t now_tick = furi_get_tick();

    // idle dimming
    if(!app->dimmed && app->settings.dim_after_s > 0 &&
       now_tick - app->last_input > (uint32_t)app->settings.dim_after_s * 1000) {
        apply_dim(app, true);
    }

    // pick up a fresh JWLink snapshot now and then
    if(now_tick - app->last_jwlink_load > 20000 || app->last_jwlink_load == 0) {
        app->last_jwlink_load = now_tick ? now_tick : 1;
        jwlink_load(app);
    }

    // automatic refresh: once at start, then on the chosen interval; failures back off for 10 minutes
    if(!app->worker) {
        bool due = false;
        uint32_t since_try = now_tick - app->last_auto_try;
        if(!app->auto_tried) {
            due = true;
        } else if(
            app->settings.refresh_min > 0 && since_try > 600000u) {
            uint32_t age = app->weather.valid ? local_timestamp() - app->weather.fetched : 0xFFFFFFFFu;
            due = age > (uint32_t)app->settings.refresh_min * 60u;
        }
        if(due) {
            app->auto_tried = true;
            app->last_auto_try = now_tick ? now_tick : 1;
            start_job(app, JobRefresh);
        }
    }
    model_push(app, NULL);
}

static bool custom_event_callback(void* context, uint32_t event) {
    StandbyApp* app = context;
    switch(event) {
    case EventTick:
        tick(app);
        return true;
    case EventWorkerDone:
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        app->worker = NULL;
        if(app->settings_dirty) settings_save(app);
        model_push(app, NULL);
        return true;
    case MenuRefresh:
        view_dispatcher_switch_to_view(app->dispatcher, ViewDisplay);
        start_job(app, JobRefresh);
        return true;
    case MenuSync:
        view_dispatcher_switch_to_view(app->dispatcher, ViewDisplay);
        start_job(app, JobSyncTime);
        return true;
    case MenuSettings:
        settings_populate(app);
        view_dispatcher_switch_to_view(app->dispatcher, ViewSettings);
        return true;
    case MenuClose:
        view_dispatcher_switch_to_view(app->dispatcher, ViewDisplay);
        return true;
    case 100 + SettingsIndexCity:
        text_input_reset(app->city_input);
        text_input_set_header_text(app->city_input, "City (blank = auto)");
        text_input_set_minimum_length(app->city_input, 0);
        snprintf(app->city_buf, sizeof(app->city_buf), "%s", app->settings.city);
        text_input_set_result_callback(
            app->city_input, city_entered, app, app->city_buf, sizeof(app->city_buf), false);
        view_dispatcher_switch_to_view(app->dispatcher, ViewCity);
        return true;
    case 100 + SettingsIndexAutoLocation:
        app->settings.city[0] = '\0';
        app->settings.has_place = false;
        app->settings_dirty = true;
        app->refresh_after_settings = true;
        settings_populate(app);
        return true;
    default:
        return false;
    }
}

static void timer_callback(void* context) {
    StandbyApp* app = context;
    view_dispatcher_send_custom_event(app->dispatcher, EventTick);
}

static bool display_input(InputEvent* event, void* context) {
    StandbyApp* app = context;

    if(event->type == InputTypePress) {
        app->last_input = furi_get_tick();
        app->swallow_press = app->dimmed;
        if(app->dimmed) apply_dim(app, false);
        return true;
    }
    if(event->type == InputTypeRelease) {
        return true;
    }
    if(app->swallow_press) {
        // this press only woke the screen
        if(event->type == InputTypeShort || event->type == InputTypeLong) app->swallow_press = false;
        return true;
    }
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return true;

    switch(event->key) {
    case InputKeyLeft:
        app->page = (app->page + PageCount - 1) % PageCount;
        model_push(app, "");
        return true;
    case InputKeyRight:
        app->page = (app->page + 1) % PageCount;
        model_push(app, "");
        return true;
    case InputKeyUp:
        if(app->settings.dim_pct < 50) app->settings.dim_pct += app->settings.dim_pct < 10 ? 1 : 5;
        app->settings_dirty = true;
        break;
    case InputKeyDown:
        if(app->settings.dim_pct > 1) app->settings.dim_pct -= app->settings.dim_pct <= 10 ? 1 : 5;
        app->settings_dirty = true;
        break;
    case InputKeyOk:
        if(event->type == InputTypeShort) {
            view_dispatcher_switch_to_view(app->dispatcher, ViewMenu);
        }
        return true;
    case InputKeyBack:
        return false; // leave the app
    default:
        break;
    }
    if(event->key == InputKeyUp || event->key == InputKeyDown) {
        char text[32];
        snprintf(text, sizeof(text), "Idle brightness %u%%", app->settings.dim_pct);
        model_push(app, text);
    }
    return true;
}

static bool navigation_callback(void* context) {
    UNUSED(context);
    return false;
}

int32_t standby_app(void* p) {
    UNUSED(p);
    StandbyApp* app = malloc(sizeof(StandbyApp));
    memset(app, 0, sizeof(StandbyApp));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->gui = furi_record_open(RECORD_GUI);

    ensure_data_dir(app->storage);
    settings_load(app);
    weather_load(app);
    app->jw.battery = app->jw.steps = app->jw.heart = -1;

    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, navigation_callback);

    app->display = view_alloc();
    view_allocate_model(app->display, ViewModelTypeLocking, sizeof(DisplayModel));
    view_set_context(app->display, app);
    view_set_draw_callback(app->display, display_draw);
    view_set_input_callback(app->display, display_input);
    view_dispatcher_add_view(app->dispatcher, ViewDisplay, app->display);

    app->menu = submenu_alloc();
    submenu_set_header(app->menu, "Standby");
    submenu_add_item(app->menu, "Refresh weather", MenuRefresh, menu_callback, app);
    submenu_add_item(app->menu, "Sync clock (internet)", MenuSync, menu_callback, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, menu_callback, app);
    submenu_add_item(app->menu, "Back to display", MenuClose, menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->menu), previous_to_display);
    view_dispatcher_add_view(app->dispatcher, ViewMenu, submenu_get_view(app->menu));

    app->settings_list = variable_item_list_alloc();
    view_set_previous_callback(variable_item_list_get_view(app->settings_list), previous_to_display);
    view_dispatcher_add_view(
        app->dispatcher, ViewSettings, variable_item_list_get_view(app->settings_list));

    app->city_input = text_input_alloc();
    view_set_previous_callback(text_input_get_view(app->city_input), previous_to_settings);
    view_dispatcher_add_view(app->dispatcher, ViewCity, text_input_get_view(app->city_input));

    // keep the backlight on, remember the user's brightness so it can be restored
    app->original_brightness = app->notification->settings.display_brightness;
    notification_message(app->notification, &sequence_display_backlight_enforce_on);
    app->last_input = furi_get_tick();

    if(app->settings.sync_time_on_start) start_job(app, JobSyncTime);

    app->timer = furi_timer_alloc(timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->timer, furi_ms_to_ticks(1000));

    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    model_push(app, NULL);
    view_dispatcher_switch_to_view(app->dispatcher, ViewDisplay);
    view_dispatcher_run(app->dispatcher);

    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);
    if(app->worker) {
        app->cancel = true;
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
    }
    if(app->settings_dirty) settings_save(app);

    // restore the screen exactly as the user had it
    app->notification->settings.display_brightness = app->original_brightness;
    notification_message(app->notification, &sequence_display_backlight_enforce_auto);
    notification_message(app->notification, &sequence_display_backlight_on);

    view_dispatcher_remove_view(app->dispatcher, ViewDisplay);
    view_dispatcher_remove_view(app->dispatcher, ViewMenu);
    view_dispatcher_remove_view(app->dispatcher, ViewSettings);
    view_dispatcher_remove_view(app->dispatcher, ViewCity);
    view_free(app->display);
    submenu_free(app->menu);
    variable_item_list_free(app->settings_list);
    text_input_free(app->city_input);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
