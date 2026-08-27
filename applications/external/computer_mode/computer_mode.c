#include "computer_mode.h"

#include <gui/elements.h>
#include <input/input.h>
#include <string.h>
#include <stdlib.h>
#include <toolbox/stream/stream.h>
#include <toolbox/stream/file_stream.h>

#define TAG "ComputerMode"
#define COMPUTER_MODE_SAVE_PATH "/ext/apps_data/computer_mode/notepad.txt"

// USB HID keyboard usage IDs -> US QWERTY ASCII. Unmapped/non-printable
// keys (arrows, function keys, etc.) are left as 0 and simply ignored -
// this is a notepad, not a full terminal.
static char hid_to_ascii(uint8_t code, bool shift) {
    static const char lower[] = "abcdefghijklmnopqrstuvwxyz1234567890";
    static const char upper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()";
    if(code >= 4 && code <= 39) {
        return shift ? upper[code - 4] : lower[code - 4];
    }
    switch(code) {
    case 44:
        return ' ';
    case 45:
        return shift ? '_' : '-';
    case 46:
        return shift ? '+' : '=';
    case 47:
        return shift ? '{' : '[';
    case 48:
        return shift ? '}' : ']';
    case 49:
        return shift ? '|' : '\\';
    case 51:
        return shift ? ':' : ';';
    case 52:
        return shift ? '"' : '\'';
    case 53:
        return shift ? '~' : '`';
    case 54:
        return shift ? '<' : ',';
    case 55:
        return shift ? '>' : '.';
    case 56:
        return shift ? '?' : '/';
    default:
        return 0;
    }
}

typedef enum {
    ComputerModeEventKeyDown,
    ComputerModeEventKeyUp,
    ComputerModeEventMouse,
    ComputerModeEventExit,
} ComputerModeEventType;

typedef struct {
    ComputerModeEventType type;
    int code; // key code, or packed mouse buttons for Mouse events
    int a; // mouse dx
    int b; // mouse dy
} ComputerModeEvent;

// ---- UART RX ----

static void computer_mode_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    ComputerModeApp* app = context;
    if(event == FuriHalSerialRxEventData) {
        uint8_t data = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(app->rx_stream, &data, 1, 0);
    }
}

// Parses one line of the wire protocol:
//   D<code>       key down (USB HID usage id, decimal)
//   U<code>       key up
//   M<dx>,<dy>,<buttons>   mouse delta + button bitmask (bit0 = left)
static void computer_mode_parse_line(ComputerModeApp* app, char* line) {
    ComputerModeEvent event = {0};
    if(line[0] == 'D') {
        event.type = ComputerModeEventKeyDown;
        event.code = atoi(&line[1]);
    } else if(line[0] == 'U') {
        event.type = ComputerModeEventKeyUp;
        event.code = atoi(&line[1]);
    } else if(line[0] == 'M') {
        char* comma1 = strchr(&line[1], ',');
        if(!comma1) return;
        char* comma2 = strchr(comma1 + 1, ',');
        if(!comma2) return;
        *comma1 = '\0';
        *comma2 = '\0';
        event.type = ComputerModeEventMouse;
        event.a = atoi(&line[1]);
        event.b = atoi(comma1 + 1);
        event.code = atoi(comma2 + 1);
    } else {
        return;
    }
    furi_message_queue_put(app->queue, &event, 0);
}

static int32_t computer_mode_rx_thread(void* ctx) {
    ComputerModeApp* app = ctx;
    char line[COMPUTER_MODE_RX_LINE_MAX];
    size_t line_len = 0;

    while(app->rx_thread_running) {
        uint8_t byte;
        size_t got = furi_stream_buffer_receive(app->rx_stream, &byte, 1, 100);
        if(got == 0) continue;

        if(byte == '\n' || byte == '\r') {
            if(line_len > 0) {
                line[line_len] = '\0';
                computer_mode_parse_line(app, line);
                line_len = 0;
            }
        } else if(line_len < COMPUTER_MODE_RX_LINE_MAX - 1) {
            line[line_len++] = (char)byte;
        } else {
            line_len = 0; // overflow - drop the line
        }
    }
    return 0;
}

// ---- rendering ----

static void computer_mode_draw_callback(Canvas* canvas, void* ctx) {
    ComputerModeApp* app = ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    // Show only the tail of the buffer - no line-count tracking needed for
    // a simple notepad, just however many characters fit on screen.
    const char* full = furi_string_get_cstr(app->text);
    size_t len = strlen(full);
    size_t show_from = len > 180 ? len - 180 : 0;
    elements_multiline_text(canvas, 2, 10, full + show_from);

    int16_t cx = app->mouse_x < 0 ? 0 : (app->mouse_x > 127 ? 127 : app->mouse_x);
    int16_t cy = app->mouse_y < 0 ? 0 : (app->mouse_y > 63 ? 63 : app->mouse_y);
    canvas_draw_line(canvas, cx, cy, cx + 4, cy + 4);
    canvas_draw_line(canvas, cx, cy, cx, cy + 6);
    canvas_draw_line(canvas, cx, cy, cx + 6, cy);
    if(app->mouse_button) {
        canvas_draw_disc(canvas, cx, cy, 2);
    }
}

static void computer_mode_input_callback(InputEvent* input, void* ctx) {
    ComputerModeApp* app = ctx;
    if(input->key == InputKeyBack && input->type == InputTypeLong) {
        ComputerModeEvent event = {.type = ComputerModeEventExit};
        furi_message_queue_put(app->queue, &event, FuriWaitForever);
    }
}

// ---- lifecycle ----

static ComputerModeApp* computer_mode_alloc(void) {
    ComputerModeApp* app = malloc(sizeof(ComputerModeApp));
    memset(app, 0, sizeof(ComputerModeApp));

    app->storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(app->storage, "/ext/apps_data/computer_mode");
    app->text = furi_string_alloc();
    app->queue = furi_message_queue_alloc(16, sizeof(ComputerModeEvent));
    app->rx_stream = furi_stream_buffer_alloc(512, 1);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, computer_mode_draw_callback, app);
    view_port_input_callback_set(app->view_port, computer_mode_input_callback, app);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->serial_handle = furi_hal_serial_control_acquire(COMPUTER_MODE_UART_CH);
    if(app->serial_handle) {
        furi_hal_serial_init(app->serial_handle, COMPUTER_MODE_BAUDRATE);
        furi_hal_serial_enable_direction(app->serial_handle, FuriHalSerialDirectionRx);
        furi_hal_serial_async_rx_start(
            app->serial_handle, computer_mode_rx_callback, app, false);

        app->rx_thread_running = true;
        app->rx_thread =
            furi_thread_alloc_ex("ComputerModeRx", 1024, computer_mode_rx_thread, app);
        furi_thread_start(app->rx_thread);
    } else {
        furi_string_set(app->text, "WiFi Devboard not detected");
    }

    return app;
}

static void computer_mode_free(ComputerModeApp* app) {
    if(app->serial_handle) {
        app->rx_thread_running = false;
        furi_thread_join(app->rx_thread);
        furi_thread_free(app->rx_thread);

        furi_hal_serial_async_rx_stop(app->serial_handle);
        furi_hal_serial_disable_direction(app->serial_handle, FuriHalSerialDirectionRx);
        furi_hal_serial_control_release(app->serial_handle);
        furi_hal_serial_deinit(app->serial_handle);
    }

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);

    furi_stream_buffer_free(app->rx_stream);
    furi_message_queue_free(app->queue);
    furi_string_free(app->text);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

static void computer_mode_save(ComputerModeApp* app) {
    Stream* stream = file_stream_alloc(app->storage);
    if(file_stream_open(stream, COMPUTER_MODE_SAVE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        stream_write_string(stream, app->text);
    }
    stream_free(stream);
}

int32_t computer_mode_app(void* p) {
    UNUSED(p);
    ComputerModeApp* app = computer_mode_alloc();

    bool running = true;
    while(running) {
        ComputerModeEvent event;
        FuriStatus status = furi_message_queue_get(app->queue, &event, furi_ms_to_ticks(200));
        if(status != FuriStatusOk) {
            view_port_update(app->view_port);
            continue;
        }

        switch(event.type) {
        case ComputerModeEventKeyDown:
            if(event.code == 225 || event.code == 229) {
                app->shift_held = true;
            } else if(event.code == 42) { // Backspace
                size_t len = furi_string_size(app->text);
                if(len > 0) furi_string_left(app->text, len - 1);
            } else if(event.code == 40 || event.code == 88) { // Enter
                furi_string_cat_str(app->text, "\n");
            } else if(event.code == 43) { // Tab
                furi_string_cat_str(app->text, "\t");
            } else {
                char c = hid_to_ascii((uint8_t)event.code, app->shift_held);
                if(c) furi_string_push_back(app->text, c);
            }
            if(furi_string_size(app->text) > COMPUTER_MODE_TEXT_MAX) {
                furi_string_right(
                    app->text, furi_string_size(app->text) - COMPUTER_MODE_TEXT_MAX);
            }
            break;
        case ComputerModeEventKeyUp:
            if(event.code == 225 || event.code == 229) {
                app->shift_held = false;
            }
            break;
        case ComputerModeEventMouse:
            app->mouse_x += event.a;
            app->mouse_y += event.b;
            app->mouse_button = (event.code & 1) != 0;
            break;
        case ComputerModeEventExit:
            running = false;
            break;
        }
        view_port_update(app->view_port);
    }

    if(app->serial_handle) computer_mode_save(app);
    computer_mode_free(app);
    return 0;
}
