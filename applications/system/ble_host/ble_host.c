/*
 * BLE Scanner: scan for Bluetooth LE devices, connect to one, browse its services and read,
 * write or subscribe to its characteristics. Uses the firmware's BLE Host mode
 * (profiles/host_profile.h), which needs the full radio stack.
 *
 * Threading: the BLE callback runs on the firmware's tiny BLE event thread, so it only records
 * what happened into App (under app->mutex). A 100 ms tick on the GUI thread reacts to it.
 */

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/elements.h>
#include <gui/modules/byte_input.h>
#include <bt/bt_service/bt.h>
#include <profiles/host_profile.h>

#include "ble_names.h"

#define TAG "BleScanner"

#define MAX_DEVICES      48
#define MAX_SERVICES     BLE_HOST_MAX_SERVICES
#define MAX_CHARS        96
#define MAX_ROWS         (MAX_SERVICES + MAX_CHARS)
#define MAX_WRITE_BYTES  20
#define TICK_MS          100
#define SORT_PERIOD_MS   1000
#define CONNECT_TIMEOUT  15000
#define NOTICE_MS        2500
#define VISIBLE_ROWS     3
#define ROW_HEIGHT       13
#define LIST_TOP         11

typedef enum {
    ScreenStarting,
    ScreenUnsupported,
    ScreenScan,
    ScreenDevice,
    ScreenConnecting,
    ScreenBrowsing,
    ScreenTree,
    ScreenChar,
    ScreenWriteLength,
} Screen;

typedef enum {
    ViewMain,
    ViewByteInput,
} ViewId;

typedef enum {
    EventTick = 1,
    EventWriteDone,
} CustomEvent;

typedef enum {
    PairNone,
    PairPasskey, // show the code, the other device types it
    PairConfirm, // does the code match the other device's screen?
    PairResult,
} PairState;

typedef struct {
    uint8_t address[BLE_HOST_ADDR_SIZE];
    uint8_t address_type;
    int8_t rssi;
    uint8_t adv_type;
    uint8_t adv[BLE_HOST_ADV_DATA_MAX];
    uint8_t adv_length;
    uint8_t response[BLE_HOST_ADV_DATA_MAX];
    uint8_t response_length;
    uint32_t last_seen;
    bool stale; // raw data changed since it was last parsed
    char name[BLE_HOST_ADV_NAME_MAX + 1];
    bool has_company;
    uint16_t company;
    bool connectable;
} Device;

typedef struct {
    BleHostCharacteristic characteristic;
    uint8_t service_index;
    uint16_t cccd_handle; // 0 when the characteristic has none
} ChrEntry;

typedef struct {
    bool is_service;
    uint8_t index;
} Row;

typedef struct App App;

typedef struct {
    App* app;
} ViewModel;

struct App {
    Gui* gui;
    ViewDispatcher* dispatcher;
    View* view;
    ByteInput* byte_input;
    FuriTimer* timer;
    FuriMutex* mutex;
    Bt* bt;
    FuriHalBleProfileBase* host;

    Screen screen;
    uint32_t screen_since;
    char notice[40];
    uint32_t notice_until;

    // scanning
    Device devices[MAX_DEVICES];
    uint8_t device_count;
    uint8_t order[MAX_DEVICES];
    uint8_t order_count; // entries of order[] that are valid
    int16_t selected_device; // index into devices, -1 none
    uint8_t scroll;
    bool sort_by_signal;
    bool active_scan;
    uint32_t last_sort;
    uint8_t detail_scroll;

    // events recorded by the BLE callback, consumed on the GUI thread
    bool ev_connected;
    uint8_t connect_status;
    bool ev_disconnected;
    bool ev_discovery_done;
    uint8_t discovery_status;
    bool ev_read;
    uint8_t read_status;
    bool ev_write;
    uint8_t write_status;
    PairState pair_state;
    uint32_t pair_value;
    uint8_t pair_status;
    bool connected;

    // the connected device
    Device peer;
    BleHostService services[MAX_SERVICES];
    uint8_t service_count;
    ChrEntry chars[MAX_CHARS];
    uint8_t char_count;
    Row rows[MAX_ROWS];
    uint8_t row_count;
    uint8_t selected_row;
    uint8_t row_scroll;
    bool discovery_truncated;

    // characteristic screen
    int16_t current_char; // index into chars, -1 none
    uint8_t value[BLE_HOST_VALUE_MAX];
    uint8_t value_length;
    bool value_valid;
    bool value_from_notification;
    uint32_t notifications;
    bool subscribed;
    bool subscribe_pending; // the last write was a subscribe request
    uint8_t write_length;
    uint8_t write_buffer[MAX_WRITE_BYTES];
};

/* ------------------------------------------------------------------ helpers */

static const char* att_error_text(uint8_t error) {
    switch(error) {
    case 0x01:
        return "invalid handle";
    case 0x02:
        return "read not allowed";
    case 0x03:
        return "write not allowed";
    case 0x05:
        return "pair first";
    case 0x07:
        return "bad offset";
    case 0x08:
        return "not authorized";
    case 0x0A:
        return "not found";
    case 0x0D:
        return "bad length";
    case 0x0F:
        return "pair first";
    default:
        return NULL;
    }
}

static void app_lock(App* app) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
}

static void app_unlock(App* app) {
    furi_check(furi_mutex_release(app->mutex) == FuriStatusOk);
}

static void set_notice(App* app, const char* text) {
    strlcpy(app->notice, text, sizeof(app->notice));
    app->notice_until = furi_get_tick() + NOTICE_MS;
}

static void set_status_notice(App* app, const char* what, uint8_t status) {
    const char* text = att_error_text(status);
    char buffer[sizeof(app->notice)];
    if(text) {
        snprintf(buffer, sizeof(buffer), "%s: %s", what, text);
    } else {
        snprintf(buffer, sizeof(buffer), "%s failed (0x%02X)", what, status);
    }
    set_notice(app, buffer);
}

static void set_screen(App* app, Screen screen) {
    app->screen = screen;
    app->screen_since = furi_get_tick();
}

static bool addresses_equal(const Device* d, const BleHostScanResult* scan) {
    return d->address_type == scan->address_type &&
           memcmp(d->address, scan->address, BLE_HOST_ADDR_SIZE) == 0;
}

/* (Re)read the name and manufacturer out of the raw advertising data. GUI thread. */
static void device_parse(Device* d) {
    BleHostAdv adv;
    d->name[0] = '\0';
    d->has_company = false;
    // the scan response often carries the name, the advertisement the manufacturer
    ble_host_adv_parse(d->adv, d->adv_length, &adv);
    if(adv.name[0]) strlcpy(d->name, adv.name, sizeof(d->name));
    if(adv.has_mfg) {
        d->has_company = true;
        d->company = adv.mfg_company_id;
    }
    ble_host_adv_parse(d->response, d->response_length, &adv);
    if(adv.name[0] && (!d->name[0] || adv.name_complete)) {
        strlcpy(d->name, adv.name, sizeof(d->name));
    }
    if(adv.has_mfg && !d->has_company) {
        d->has_company = true;
        d->company = adv.mfg_company_id;
    }
    d->stale = false;
}

static void device_label(const Device* d, char* out, size_t size) {
    if(d->name[0]) {
        strlcpy(out, d->name, size);
    } else if(d->has_company) {
        const char* company = ble_company_name(d->company);
        if(company) {
            snprintf(out, size, "%s device", company);
        } else {
            snprintf(out, size, "Mfr %04X", d->company);
        }
    } else {
        snprintf(out, size, "%02X:%02X:%02X", d->address[2], d->address[1], d->address[0]);
    }
}

/* ------------------------------------------------------------------ BLE callback (event thread) */

static Device* device_for_scan(App* app, const BleHostScanResult* scan) {
    for(uint8_t i = 0; i < app->device_count; i++) {
        if(addresses_equal(&app->devices[i], scan)) return &app->devices[i];
    }
    if(app->device_count < MAX_DEVICES) {
        Device* d = &app->devices[app->device_count];
        memset(d, 0, sizeof(*d));
        memcpy(d->address, scan->address, BLE_HOST_ADDR_SIZE);
        d->address_type = scan->address_type;
        app->device_count++;
        return d;
    }
    // full: replace the device not heard from for the longest, except the one on screen
    int16_t oldest = -1;
    for(uint8_t i = 0; i < app->device_count; i++) {
        if(i == app->selected_device) continue;
        if(oldest < 0 || app->devices[i].last_seen < app->devices[oldest].last_seen) oldest = i;
    }
    if(oldest < 0) return NULL;
    Device* d = &app->devices[oldest];
    memset(d, 0, sizeof(*d));
    memcpy(d->address, scan->address, BLE_HOST_ADDR_SIZE);
    d->address_type = scan->address_type;
    return d;
}

static void record_scan_result(App* app, const BleHostScanResult* scan) {
    Device* d = device_for_scan(app, scan);
    if(!d) return;
    d->rssi = scan->rssi;
    d->last_seen = furi_get_tick();
    if(scan->adv_type == 4) { // scan response
        memcpy(d->response, scan->data, scan->data_length);
        d->response_length = scan->data_length;
    } else {
        memcpy(d->adv, scan->data, scan->data_length);
        d->adv_length = scan->data_length;
        d->adv_type = scan->adv_type;
        d->connectable = scan->adv_type == 0 || scan->adv_type == 1;
    }
    d->stale = true;
}

static void ble_event_callback(const BleHostEvent* event, void* context) {
    App* app = context;
    app_lock(app);
    switch(event->type) {
    case BleHostEventScanResult:
        record_scan_result(app, &event->data.scan);
        break;
    case BleHostEventConnected:
        app->ev_connected = true;
        app->connect_status = event->data.connected.status;
        break;
    case BleHostEventDisconnected:
        app->ev_disconnected = true;
        break;
    case BleHostEventService: {
        uint8_t index = event->data.service.index;
        if(index < MAX_SERVICES) {
            app->services[index] = event->data.service.service;
            if(index >= app->service_count) app->service_count = index + 1;
        }
        break;
    }
    case BleHostEventCharacteristic:
        if(app->char_count < MAX_CHARS) {
            ChrEntry* c = &app->chars[app->char_count++];
            c->characteristic = event->data.characteristic.characteristic;
            c->service_index = event->data.characteristic.service_index;
            c->cccd_handle = 0;
        } else {
            app->discovery_truncated = true;
        }
        break;
    case BleHostEventDescriptor: {
        const BleHostDescriptor* d = &event->data.descriptor.descriptor;
        if(ble_host_uuid16(&d->uuid) == 0x2902) {
            for(uint8_t i = 0; i < app->char_count; i++) {
                if(app->chars[i].characteristic.value_handle ==
                   event->data.descriptor.characteristic_value_handle) {
                    app->chars[i].cccd_handle = d->handle;
                }
            }
        }
        break;
    }
    case BleHostEventDiscoveryDone:
        app->ev_discovery_done = true;
        app->discovery_status = event->data.done.status;
        break;
    case BleHostEventReadDone:
        app->ev_read = true;
        app->read_status = event->data.value.status;
        if(event->data.value.status == 0) {
            memcpy(app->value, event->data.value.data, event->data.value.length);
            app->value_length = event->data.value.length;
        }
        break;
    case BleHostEventWriteDone:
        app->ev_write = true;
        app->write_status = event->data.done.status;
        break;
    case BleHostEventNotification:
        if(app->current_char >= 0 &&
           app->chars[app->current_char].characteristic.value_handle ==
               event->data.value.handle) {
            memcpy(app->value, event->data.value.data, event->data.value.length);
            app->value_length = event->data.value.length;
            app->value_valid = true;
            app->value_from_notification = true;
        }
        app->notifications++;
        break;
    case BleHostEventPairingPasskey:
        app->pair_state = PairPasskey;
        app->pair_value = event->data.passkey.value;
        break;
    case BleHostEventPairingConfirm:
        app->pair_state = PairConfirm;
        app->pair_value = event->data.passkey.value;
        break;
    case BleHostEventPairingComplete:
        app->pair_state = PairResult;
        app->pair_status = event->data.done.status;
        break;
    default:
        break;
    }
    app_unlock(app);
}

/* ------------------------------------------------------------------ list building */

static void rebuild_order(App* app) {
    app->order_count = app->device_count;
    for(uint8_t i = 0; i < app->device_count; i++) app->order[i] = i;
    if(!app->sort_by_signal) return;
    // insertion sort, strongest first; stable for equal signal
    for(uint8_t i = 1; i < app->device_count; i++) {
        uint8_t v = app->order[i];
        int16_t j = i - 1;
        while(j >= 0 && app->devices[app->order[j]].rssi < app->devices[v].rssi) {
            app->order[j + 1] = app->order[j];
            j--;
        }
        app->order[j + 1] = v;
    }
}

static int16_t order_position(const App* app, int16_t device) {
    for(uint8_t i = 0; i < app->order_count; i++) {
        if(app->order[i] == device) return i;
    }
    return -1;
}

static void build_tree_rows(App* app) {
    app->row_count = 0;
    for(uint8_t s = 0; s < app->service_count && app->row_count < MAX_ROWS; s++) {
        app->rows[app->row_count].is_service = true;
        app->rows[app->row_count].index = s;
        app->row_count++;
        for(uint8_t c = 0; c < app->char_count && app->row_count < MAX_ROWS; c++) {
            if(app->chars[c].service_index != s) continue;
            app->rows[app->row_count].is_service = false;
            app->rows[app->row_count].index = c;
            app->row_count++;
        }
    }
    app->selected_row = 0;
    app->row_scroll = 0;
}

static void uuid_label(const BleHostUuid* uuid, const BleName16* names, size_t count, char* out, size_t size) {
    uint16_t id = ble_host_uuid16(uuid);
    const char* name = id ? ble_name_lookup(names, count, id) : NULL;
    if(name) {
        strlcpy(out, name, size);
    } else if(uuid->size == 2) {
        snprintf(out, size, "%04X", id);
    } else {
        // the first group is enough to tell them apart on this screen
        char full[37];
        ble_format_uuid(uuid->bytes, uuid->size, full, sizeof(full));
        snprintf(out, size, "%.8s..", full);
    }
}

/* ------------------------------------------------------------------ drawing */

static void draw_header(Canvas* canvas, const char* left, const char* right) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, left);
    if(right && right[0]) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, right);
    }
    canvas_draw_line(canvas, 0, 10, 127, 10);
}

static void draw_message(Canvas* canvas, const char* title, const char* line1, const char* line2) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 14, AlignCenter, AlignCenter, title);
    canvas_set_font(canvas, FontSecondary);
    if(line1) canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, line1);
    if(line2) canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, line2);
}

/* One list row: label left, detail right, inverted when selected. */
static void
    draw_row(Canvas* canvas, uint8_t slot, bool selected, uint8_t indent, const char* label, const char* detail) {
    uint8_t y = LIST_TOP + slot * ROW_HEIGHT;
    if(selected) {
        canvas_draw_box(canvas, 0, y, 124, ROW_HEIGHT - 1);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_set_font(canvas, FontSecondary);
    uint8_t detail_width = detail ? canvas_string_width(canvas, detail) + 4 : 0;
    // clip the label to the space the detail leaves
    char clipped[40];
    strlcpy(clipped, label, sizeof(clipped));
    while(clipped[0] && canvas_string_width(canvas, clipped) + 3 + indent > 124 - detail_width) {
        clipped[strlen(clipped) - 1] = '\0';
    }
    canvas_draw_str(canvas, 3 + indent, y + 9, clipped);
    if(detail) canvas_draw_str_aligned(canvas, 122, y + 9, AlignRight, AlignBottom, detail);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_scrollbar(Canvas* canvas, uint8_t position, uint8_t total) {
    if(total > VISIBLE_ROWS) elements_scrollbar_pos(canvas, 127, LIST_TOP, VISIBLE_ROWS * ROW_HEIGHT, position, total);
}

static void draw_notice(Canvas* canvas, App* app) {
    if(!app->notice[0] || furi_get_tick() >= app->notice_until) return;
    canvas_set_font(canvas, FontSecondary);
    uint8_t width = canvas_string_width(canvas, app->notice) + 8;
    if(width > 124) width = 124;
    uint8_t x = (128 - width) / 2;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x, 27, width, 14);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, x, 27, width, 14);
    canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignCenter, app->notice);
}

static void draw_scan(Canvas* canvas, App* app) {
    char right[24];
    snprintf(right, sizeof(right), "%u found", app->device_count);
    draw_header(canvas, app->active_scan ? "Active scan" : "BLE Scanner", right);

    if(app->device_count == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, "Looking for devices...");
    }
    int16_t position = order_position(app, app->selected_device);
    if(position < 0) position = 0;
    for(uint8_t slot = 0; slot < VISIBLE_ROWS; slot++) {
        uint8_t i = app->scroll + slot;
        if(i >= app->order_count) break;
        const Device* d = &app->devices[app->order[i]];
        char label[40], rssi[8];
        device_label(d, label, sizeof(label));
        snprintf(rssi, sizeof(rssi), "%d", d->rssi);
        draw_row(canvas, slot, i == position, 0, label, rssi);
    }
    draw_scrollbar(canvas, app->scroll, app->order_count);
    elements_button_left(canvas, app->sort_by_signal ? "Signal" : "Order");
    elements_button_center(canvas, "Open");
    elements_button_right(canvas, app->active_scan ? "Active" : "Passive");
}

/* Lines of detail text about the device on screen, one per call to emit */
typedef void (*LineEmit)(void* context, const char* text);

static void device_lines(const Device* d, LineEmit emit, void* context) {
    char line[48], tmp[37];
    BleHostAdv adv;
    ble_host_adv_parse(d->adv, d->adv_length, &adv);
    BleHostAdv response;
    ble_host_adv_parse(d->response, d->response_length, &response);

    device_label(d, line, sizeof(line));
    emit(context, line);
    ble_format_address(d->address, tmp, sizeof(tmp));
    snprintf(line, sizeof(line), "%s %s", tmp, d->address_type ? "rnd" : "pub");
    emit(context, line);
    snprintf(line, sizeof(line), "Signal %d dBm  %s", d->rssi, d->connectable ? "connectable" : "adv only");
    emit(context, line);
    if(d->has_company) {
        const char* company = ble_company_name(d->company);
        snprintf(line, sizeof(line), "Maker %s (%04X)", company ? company : "unknown", d->company);
        emit(context, line);
    }
    if(adv.has_tx_power) {
        snprintf(line, sizeof(line), "Tx power %d dBm", adv.tx_power);
        emit(context, line);
    }
    if(adv.has_appearance) {
        snprintf(line, sizeof(line), "Appearance %04X", adv.appearance);
        emit(context, line);
    }
    if(adv.has_flags) {
        snprintf(line, sizeof(line), "Flags %02X", adv.flags);
        emit(context, line);
    }
    for(uint8_t i = 0; i < adv.uuid16_count; i++) {
        const char* name = ble_service_name(adv.uuid16[i]);
        snprintf(line, sizeof(line), "Svc %04X %s", adv.uuid16[i], name ? name : "");
        emit(context, line);
    }
    for(uint8_t i = 0; i < response.uuid16_count; i++) {
        const char* name = ble_service_name(response.uuid16[i]);
        snprintf(line, sizeof(line), "Svc %04X %s", response.uuid16[i], name ? name : "");
        emit(context, line);
    }
    for(uint8_t i = 0; i < adv.uuid128_count; i++) {
        ble_format_uuid(adv.uuid128[i], 16, tmp, sizeof(tmp));
        snprintf(line, sizeof(line), "%.18s", tmp);
        emit(context, line);
    }
    if(adv.has_mfg && adv.mfg_data_len) {
        ble_format_value(adv.mfg_data, adv.mfg_data_len, tmp, sizeof(tmp));
        snprintf(line, sizeof(line), "Data %s", tmp);
        emit(context, line);
    }
}

typedef struct {
    Canvas* canvas;
    uint8_t skip;
    uint8_t shown;
    uint8_t total;
} LineDraw;

static void draw_line_cb(void* context, const char* text) {
    LineDraw* draw = context;
    if(draw->total >= draw->skip && draw->shown < VISIBLE_ROWS + 1) {
        canvas_draw_str(draw->canvas, 3, LIST_TOP + 9 + draw->shown * 10, text);
        draw->shown++;
    }
    draw->total++;
}

static void draw_device(Canvas* canvas, App* app) {
    draw_header(canvas, "Device", NULL);
    const Device* d = &app->devices[app->selected_device >= 0 ? app->selected_device : 0];
    canvas_set_font(canvas, FontSecondary);
    LineDraw draw = {.canvas = canvas, .skip = app->detail_scroll, .shown = 0, .total = 0};
    device_lines(d, draw_line_cb, &draw);
    if(draw.total > draw.shown + app->detail_scroll) {
        elements_scrollbar_pos(canvas, 127, LIST_TOP, 40, app->detail_scroll, draw.total);
    }
    if(d->connectable) elements_button_center(canvas, "Connect");
}

static void draw_tree(Canvas* canvas, App* app) {
    char right[24], label[32], detail[16];
    snprintf(right, sizeof(right), "%u svc", app->service_count);
    draw_header(canvas, "Services", right);
    for(uint8_t slot = 0; slot < VISIBLE_ROWS; slot++) {
        uint8_t i = app->row_scroll + slot;
        if(i >= app->row_count) break;
        const Row* row = &app->rows[i];
        if(row->is_service) {
            uuid_label(&app->services[row->index].uuid, ble_service_names, BLE_NAME_COUNT(ble_service_names), label, sizeof(label));
            draw_row(canvas, slot, i == app->selected_row, 0, label, NULL);
        } else {
            const ChrEntry* c = &app->chars[row->index];
            uuid_label(&c->characteristic.uuid, ble_characteristic_names, BLE_NAME_COUNT(ble_characteristic_names), label, sizeof(label));
            ble_format_properties(c->characteristic.properties, detail, sizeof(detail));
            draw_row(canvas, slot, i == app->selected_row, 6, label, detail);
        }
    }
    draw_scrollbar(canvas, app->row_scroll, app->row_count);
    elements_button_left(canvas, "Disc");
    elements_button_center(canvas, "Open");
    elements_button_right(canvas, "Pair");
}

static void draw_char(Canvas* canvas, App* app) {
    const ChrEntry* c = &app->chars[app->current_char];
    char name[32], uuid[37], props[12], line[48], value[64];
    uuid_label(&c->characteristic.uuid, ble_characteristic_names, BLE_NAME_COUNT(ble_characteristic_names), name, sizeof(name));
    ble_format_properties(c->characteristic.properties, props, sizeof(props));
    draw_header(canvas, name, props);

    canvas_set_font(canvas, FontSecondary);
    ble_format_uuid(c->characteristic.uuid.bytes, c->characteristic.uuid.size, uuid, sizeof(uuid));
    snprintf(line, sizeof(line), "%.20s", uuid);
    canvas_draw_str(canvas, 3, LIST_TOP + 9, line);
    if(app->value_valid) {
        ble_format_value(app->value, app->value_length, value, sizeof(value));
        snprintf(line, sizeof(line), "%.20s", value);
        canvas_draw_str(canvas, 3, LIST_TOP + 20, line);
        if(strlen(value) > 20) {
            snprintf(line, sizeof(line), "%.20s", value + 20);
            canvas_draw_str(canvas, 3, LIST_TOP + 30, line);
        }
        if(app->value_from_notification) {
            snprintf(line, sizeof(line), "%lu notifications", (unsigned long)app->notifications);
            canvas_draw_str(canvas, 3, LIST_TOP + 40, line);
        }
    } else {
        canvas_draw_str(canvas, 3, LIST_TOP + 20, "No value yet");
    }
    uint8_t p = c->characteristic.properties;
    if(p & BLE_HOST_PROP_READ) elements_button_left(canvas, "Read");
    if((p & (BLE_HOST_PROP_NOTIFY | BLE_HOST_PROP_INDICATE)) && c->cccd_handle) {
        elements_button_center(canvas, app->subscribed ? "Stop" : "Listen");
    }
    if(p & (BLE_HOST_PROP_WRITE | BLE_HOST_PROP_WRITE_NO_RESPONSE)) elements_button_right(canvas, "Write");
}

static void draw_write_length(Canvas* canvas, App* app) {
    draw_header(canvas, "Write value", NULL);
    char line[24];
    snprintf(line, sizeof(line), "%u byte%s", app->write_length, app->write_length == 1 ? "" : "s");
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, line);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, "Up/Down: length");
    elements_button_center(canvas, "Edit");
}

static void draw_pairing(Canvas* canvas, App* app) {
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 4, 8, 120, 48);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 4, 8, 120, 48, 3);
    char code[16], text[32];
    canvas_set_font(canvas, FontPrimary);
    if(app->pair_state == PairPasskey || app->pair_state == PairConfirm) {
        snprintf(code, sizeof(code), "%06lu", (unsigned long)app->pair_value);
        canvas_draw_str_aligned(canvas, 64, 14, AlignCenter, AlignTop, app->pair_state == PairPasskey ? "Pairing code" : "Confirm code");
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignTop, code);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 54, AlignCenter, AlignBottom, app->pair_state == PairPasskey ? "Type it on the other device" : "OK = same, Back = different");
    } else {
        if(app->pair_status == 0) {
            snprintf(text, sizeof(text), "Paired");
        } else {
            snprintf(text, sizeof(text), "Pairing failed (%u)", app->pair_status);
        }
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, text);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignBottom, "OK to close");
    }
}

static void view_draw(Canvas* canvas, void* model) {
    App* app = ((ViewModel*)model)->app;
    app_lock(app);
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    switch(app->screen) {
    case ScreenStarting:
        draw_message(canvas, "BLE Scanner", "Starting Bluetooth...", NULL);
        break;
    case ScreenUnsupported:
        draw_message(canvas, "Not available", "Host mode needs the full", "Bluetooth radio stack");
        break;
    case ScreenScan:
        draw_scan(canvas, app);
        break;
    case ScreenDevice:
        draw_device(canvas, app);
        break;
    case ScreenConnecting: {
        char label[40];
        device_label(&app->peer, label, sizeof(label));
        draw_message(canvas, "Connecting...", label, "Back to cancel");
        break;
    }
    case ScreenBrowsing: {
        char line[28];
        snprintf(line, sizeof(line), "%u services, %u chars", app->service_count, app->char_count);
        draw_message(canvas, "Reading services", line, NULL);
        break;
    }
    case ScreenTree:
        draw_tree(canvas, app);
        break;
    case ScreenChar:
        draw_char(canvas, app);
        break;
    case ScreenWriteLength:
        draw_write_length(canvas, app);
        break;
    }
    if(app->pair_state != PairNone) {
        draw_pairing(canvas, app);
    } else {
        draw_notice(canvas, app);
    }
    app_unlock(app);
}

/* ------------------------------------------------------------------ actions (GUI thread) */

static void start_scan(App* app) {
    ble_profile_host_scan_stop(app->host);
    if(!ble_profile_host_scan_start(app->host, app->active_scan, false)) {
        set_notice(app, "Scan failed");
    }
}

static void go_to_scan(App* app) {
    app->ev_connected = app->ev_disconnected = app->ev_discovery_done = false;
    app->connected = false;
    app->service_count = app->char_count = app->row_count = 0;
    app->current_char = -1;
    app->subscribed = false;
    app->pair_state = PairNone;
    set_screen(app, ScreenScan);
    start_scan(app);
}

static void leave_char(App* app) {
    if(app->subscribed && app->current_char >= 0) {
        const ChrEntry* c = &app->chars[app->current_char];
        ble_profile_host_subscribe(app->host, c->cccd_handle, BleHostSubscribeOff);
        app->subscribed = false;
        app->subscribe_pending = false;
    }
    app->current_char = -1;
}

static void begin_connect(App* app) {
    if(app->selected_device < 0) return;
    app->peer = app->devices[app->selected_device];
    app->ev_connected = app->ev_disconnected = false;
    app->service_count = app->char_count = app->row_count = 0;
    app->discovery_truncated = false;
    if(ble_profile_host_connect(app->host, app->peer.address, app->peer.address_type)) {
        set_screen(app, ScreenConnecting);
    } else {
        set_notice(app, "Could not connect");
    }
}

static void do_read(App* app) {
    if(!ble_profile_host_read(app->host, app->chars[app->current_char].characteristic.value_handle)) {
        set_notice(app, "Busy, try again");
    }
}

static void do_listen(App* app) {
    const ChrEntry* c = &app->chars[app->current_char];
    bool turn_on = !app->subscribed;
    BleHostSubscribeMode mode = BleHostSubscribeOff;
    if(turn_on) {
        mode = (c->characteristic.properties & BLE_HOST_PROP_NOTIFY) ? BleHostSubscribeNotify :
                                                                       BleHostSubscribeIndicate;
    }
    if(ble_profile_host_subscribe(app->host, c->cccd_handle, mode)) {
        app->subscribed = turn_on;
        app->subscribe_pending = true;
        app->notifications = 0;
    } else {
        set_notice(app, "Busy, try again");
    }
}

static void byte_input_done(void* context) {
    App* app = context;
    view_dispatcher_send_custom_event(app->dispatcher, EventWriteDone);
}

static void begin_write_edit(App* app) {
    memset(app->write_buffer, 0, sizeof(app->write_buffer));
    // start from the last value read, if there is one
    if(app->value_valid) {
        memcpy(app->write_buffer, app->value, MIN(app->write_length, app->value_length));
    }
    byte_input_set_header_text(app->byte_input, "Value to write (hex)");
    byte_input_set_result_callback(app->byte_input, byte_input_done, NULL, app, app->write_buffer, app->write_length);
    view_dispatcher_switch_to_view(app->dispatcher, ViewByteInput);
}

static void finish_write(App* app) {
    view_dispatcher_switch_to_view(app->dispatcher, ViewMain);
    const ChrEntry* c = &app->chars[app->current_char];
    uint8_t p = c->characteristic.properties;
    bool with_response = (p & BLE_HOST_PROP_WRITE) != 0;
    if(!ble_profile_host_write(app->host, c->characteristic.value_handle, app->write_buffer, app->write_length, with_response)) {
        set_notice(app, "Write failed (too long or busy)");
    } else if(!with_response) {
        set_notice(app, "Sent");
    }
    set_screen(app, ScreenChar);
}

/* Reacts to what the BLE callback recorded. GUI thread, tick. */
static void process_events(App* app) {
    uint32_t now = furi_get_tick();

    // parse raw advertising data that changed
    for(uint8_t i = 0; i < app->device_count; i++) {
        if(app->devices[i].stale) device_parse(&app->devices[i]);
    }
    if(app->screen == ScreenScan && now - app->last_sort >= SORT_PERIOD_MS) {
        rebuild_order(app);
        app->last_sort = now;
    } else {
        // devices heard since the last sort join at the end of the list
        while(app->order_count < app->device_count) {
            app->order[app->order_count] = app->order_count;
            app->order_count++;
        }
    }
    if(app->screen == ScreenScan) {
        if(app->selected_device < 0 && app->order_count) app->selected_device = app->order[0];
        int16_t position = order_position(app, app->selected_device);
        if(position >= 0) {
            if(position < app->scroll) app->scroll = position;
            if(position >= app->scroll + VISIBLE_ROWS) app->scroll = position - VISIBLE_ROWS + 1;
        }
    }

    if(app->ev_disconnected) {
        app->ev_disconnected = false;
        if(app->screen == ScreenConnecting || app->screen == ScreenBrowsing ||
           app->screen == ScreenTree || app->screen == ScreenChar ||
           app->screen == ScreenWriteLength) {
            set_notice(app, "Disconnected");
            go_to_scan(app);
        }
    }

    if(app->ev_connected) {
        app->ev_connected = false;
        if(app->screen == ScreenConnecting) {
            if(app->connect_status != 0) {
                set_status_notice(app, "Connect", app->connect_status);
                set_screen(app, ScreenDevice);
            } else {
                app->connected = true;
                set_screen(app, ScreenBrowsing);
                app->service_count = app->char_count = 0;
                if(!ble_profile_host_discover(app->host)) {
                    set_notice(app, "Could not read services");
                    ble_profile_host_disconnect(app->host);
                }
            }
        }
    }
    if(app->screen == ScreenConnecting && now - app->screen_since > CONNECT_TIMEOUT) {
        ble_profile_host_connect_cancel(app->host);
        set_notice(app, "No answer");
        set_screen(app, ScreenDevice);
    }

    if(app->ev_discovery_done) {
        app->ev_discovery_done = false;
        if(app->screen == ScreenBrowsing) {
            if(app->discovery_status != 0 && app->service_count == 0) {
                set_status_notice(app, "Reading services", app->discovery_status);
                ble_profile_host_disconnect(app->host);
            } else {
                build_tree_rows(app);
                if(app->discovery_truncated) set_notice(app, "List was cut short");
                set_screen(app, ScreenTree);
            }
        }
    }

    if(app->ev_read) {
        app->ev_read = false;
        if(app->read_status == 0) {
            app->value_valid = true;
            app->value_from_notification = false;
        } else {
            set_status_notice(app, "Read", app->read_status);
        }
    }
    if(app->ev_write) {
        app->ev_write = false;
        if(app->subscribe_pending) {
            app->subscribe_pending = false;
            if(app->write_status != 0) {
                app->subscribed = !app->subscribed; // it did not take effect
                set_status_notice(app, "Listen", app->write_status);
            }
        } else if(app->write_status == 0) {
            set_notice(app, "Written");
        } else {
            set_status_notice(app, "Write", app->write_status);
        }
    }
}

/* ------------------------------------------------------------------ input */

static void move_selection(App* app, int8_t delta) {
    if(app->screen == ScreenScan) {
        if(!app->order_count) return;
        int16_t position = order_position(app, app->selected_device);
        if(position < 0) position = 0;
        position += delta;
        if(position < 0) position = 0;
        if(position >= app->order_count) position = app->order_count - 1;
        app->selected_device = app->order[position];
    } else if(app->screen == ScreenTree) {
        if(!app->row_count) return;
        int16_t row = app->selected_row + delta;
        if(row < 0) row = 0;
        if(row >= app->row_count) row = app->row_count - 1;
        app->selected_row = row;
        if(row < app->row_scroll) app->row_scroll = row;
        if(row >= app->row_scroll + VISIBLE_ROWS) app->row_scroll = row - VISIBLE_ROWS + 1;
    } else if(app->screen == ScreenDevice) {
        int16_t scroll = app->detail_scroll + delta;
        if(scroll < 0) scroll = 0;
        if(scroll > 40) scroll = 40;
        app->detail_scroll = scroll;
    } else if(app->screen == ScreenWriteLength) {
        int16_t length = app->write_length + delta;
        if(length < 1) length = 1;
        if(length > MAX_WRITE_BYTES) length = MAX_WRITE_BYTES;
        app->write_length = length;
    }
}

static bool handle_pairing_input(App* app, InputKey key) {
    if(app->pair_state == PairConfirm) {
        if(key == InputKeyOk || key == InputKeyBack) {
            ble_profile_host_pairing_confirm(app->host, key == InputKeyOk);
            app->pair_state = PairNone;
        }
    } else if(app->pair_state == PairPasskey) {
        if(key == InputKeyBack) app->pair_state = PairNone;
    } else if(key == InputKeyOk || key == InputKeyBack) {
        app->pair_state = PairNone;
    }
    return true;
}

static bool view_input(InputEvent* event, void* context) {
    App* app = context;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return true;
    bool keep_running = true;
    InputKey key = event->key;

    app_lock(app);
    if(app->pair_state != PairNone) {
        handle_pairing_input(app, key);
        app_unlock(app);
        return true;
    }
    bool short_press = event->type == InputTypeShort;

    switch(app->screen) {
    case ScreenStarting:
        break;
    case ScreenUnsupported:
        if(key == InputKeyBack || key == InputKeyOk) keep_running = false;
        break;
    case ScreenScan:
        if(key == InputKeyUp) {
            move_selection(app, -1);
        } else if(key == InputKeyDown) {
            move_selection(app, 1);
        } else if(key == InputKeyLeft && short_press) {
            app->sort_by_signal = !app->sort_by_signal;
            rebuild_order(app);
        } else if(key == InputKeyRight && short_press) {
            app->active_scan = !app->active_scan;
            start_scan(app);
        } else if(key == InputKeyOk && short_press && app->selected_device >= 0) {
            app->detail_scroll = 0;
            set_screen(app, ScreenDevice);
        } else if(key == InputKeyBack && short_press) {
            keep_running = false;
        }
        break;
    case ScreenDevice:
        if(key == InputKeyUp) {
            move_selection(app, -1);
        } else if(key == InputKeyDown) {
            move_selection(app, 1);
        } else if(key == InputKeyOk && short_press) {
            if(app->selected_device >= 0 && app->devices[app->selected_device].connectable) begin_connect(app);
        } else if(key == InputKeyBack && short_press) {
            go_to_scan(app);
        }
        break;
    case ScreenConnecting:
        if(key == InputKeyBack && short_press) {
            ble_profile_host_connect_cancel(app->host);
            set_screen(app, ScreenDevice);
        }
        break;
    case ScreenBrowsing:
        if(key == InputKeyBack && short_press) ble_profile_host_disconnect(app->host);
        break;
    case ScreenTree:
        if(key == InputKeyUp) {
            move_selection(app, -1);
        } else if(key == InputKeyDown) {
            move_selection(app, 1);
        } else if(key == InputKeyOk && short_press && app->row_count) {
            const Row* row = &app->rows[app->selected_row];
            if(!row->is_service) {
                app->current_char = row->index;
                app->value_valid = false;
                app->subscribed = false;
                app->notifications = 0;
                set_screen(app, ScreenChar);
            }
        } else if(key == InputKeyRight && short_press) {
            if(!ble_profile_host_pair(app->host)) set_notice(app, "Could not start pairing");
        } else if((key == InputKeyLeft || key == InputKeyBack) && short_press) {
            ble_profile_host_disconnect(app->host);
        }
        break;
    case ScreenChar: {
        const ChrEntry* c = &app->chars[app->current_char];
        uint8_t p = c->characteristic.properties;
        if(!short_press) break;
        if(key == InputKeyLeft && (p & BLE_HOST_PROP_READ)) {
            do_read(app);
        } else if(key == InputKeyOk && (p & (BLE_HOST_PROP_NOTIFY | BLE_HOST_PROP_INDICATE)) && c->cccd_handle) {
            do_listen(app);
        } else if(key == InputKeyRight && (p & (BLE_HOST_PROP_WRITE | BLE_HOST_PROP_WRITE_NO_RESPONSE))) {
            if(app->write_length == 0) app->write_length = 1;
            set_screen(app, ScreenWriteLength);
        } else if(key == InputKeyBack) {
            leave_char(app);
            set_screen(app, ScreenTree);
        }
        break;
    }
    case ScreenWriteLength:
        if(key == InputKeyUp) {
            move_selection(app, 1);
        } else if(key == InputKeyDown) {
            move_selection(app, -1);
        } else if(key == InputKeyOk && short_press) {
            begin_write_edit(app);
        } else if(key == InputKeyBack && short_press) {
            set_screen(app, ScreenChar);
        }
        break;
    }
    app_unlock(app);

    if(!keep_running) view_dispatcher_stop(app->dispatcher);
    return true;
}

/* ------------------------------------------------------------------ lifecycle */

static void start_host(App* app) {
    app->host = bt_host_start(app->bt);
    if(!app->host) {
        set_screen(app, ScreenUnsupported);
        return;
    }
    ble_profile_host_set_event_callback(app->host, ble_event_callback, app);
    go_to_scan(app);
}

static bool custom_event(void* context, uint32_t event) {
    App* app = context;
    if(event == EventTick) {
        if(app->screen == ScreenStarting && furi_get_tick() - app->screen_since > TICK_MS) {
            // blocks for a moment while the radio restarts, the splash is already on screen
            start_host(app);
        } else {
            app_lock(app);
            process_events(app);
            app_unlock(app);
        }
        with_view_model(app->view, ViewModel * model, { UNUSED(model); }, true);
    } else if(event == EventWriteDone) {
        app_lock(app);
        finish_write(app);
        app_unlock(app);
    }
    return true;
}

static void tick_callback(void* context) {
    App* app = context;
    view_dispatcher_send_custom_event(app->dispatcher, EventTick);
}

static uint32_t navigation_back(void* context) {
    UNUSED(context);
    return VIEW_IGNORE; // Back is handled per screen in view_input; it never closes the app itself
}

static uint32_t byte_input_previous(void* context) {
    UNUSED(context);
    return ViewMain;
}

int32_t ble_host_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->selected_device = -1;
    app->current_char = -1;
    app->write_length = 1;
    app->sort_by_signal = true;
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->bt = furi_record_open(RECORD_BT);

    app->view = view_alloc();
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(ViewModel));
    with_view_model(app->view, ViewModel * model, { model->app = app; }, false);
    view_set_context(app->view, app);
    view_set_draw_callback(app->view, view_draw);
    view_set_input_callback(app->view, view_input);
    view_set_previous_callback(app->view, navigation_back);

    app->byte_input = byte_input_alloc();
    view_set_previous_callback(byte_input_get_view(app->byte_input), byte_input_previous);

    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, custom_event);
    view_dispatcher_add_view(app->dispatcher, ViewMain, app->view);
    view_dispatcher_add_view(app->dispatcher, ViewByteInput, byte_input_get_view(app->byte_input));
    app->gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    set_screen(app, ScreenStarting);
    view_dispatcher_switch_to_view(app->dispatcher, ViewMain);
    app->timer = furi_timer_alloc(tick_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->timer, TICK_MS);

    view_dispatcher_run(app->dispatcher);

    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);
    if(app->host) {
        // stop first so no callback runs into freed state
        ble_profile_host_set_event_callback(app->host, NULL, NULL);
        bt_host_stop(app->bt);
    }
    view_dispatcher_remove_view(app->dispatcher, ViewMain);
    view_dispatcher_remove_view(app->dispatcher, ViewByteInput);
    view_dispatcher_free(app->dispatcher);
    view_free(app->view);
    byte_input_free(app->byte_input);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_BT);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
