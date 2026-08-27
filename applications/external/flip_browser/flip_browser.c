#include "flip_browser.h"

#include <input/input.h>
#include <string.h>
#include <ctype.h>
#include <toolbox/stream/stream.h>
#include <toolbox/stream/file_stream.h>

#define BROWSER_DATA_DIR "/ext/apps_data/flip_browser"
#define BROWSER_PAGE_PATH BROWSER_DATA_DIR "/page.html"
#define BROWSER_MAX_HTML_READ (32 * 1024) // cap how much of a page we process

typedef enum {
    BrowserEventGoUrl,
    BrowserEventBack,
} BrowserEventType;

typedef struct {
    BrowserEventType type;
} BrowserEvent;

// ---- HTML -> readable text ----

static bool tag_is(const char* name, size_t name_len, const char* target) {
    size_t target_len = strlen(target);
    if(name_len != target_len) return false;
    for(size_t i = 0; i < target_len; i++) {
        if(tolower((unsigned char)name[i]) != target[i]) return false;
    }
    return true;
}

// Strips tags, drops <script>/<style> contents entirely (raw JS/CSS is
// useless as "readable text"), decodes a handful of common named entities,
// collapses whitespace runs, and inserts a newline at common block-level
// boundaries so the result reads as paragraphs rather than one long run.
static void browser_strip_html(const char* html, size_t html_len, FuriString* out) {
    bool in_tag = false;
    bool skip_content = false;
    char tag_name[8];
    size_t tag_name_len = 0;
    bool last_was_space = true;

    for(size_t i = 0; i < html_len; i++) {
        char c = html[i];

        if(c == '<') {
            in_tag = true;
            tag_name_len = 0;
            continue;
        }
        if(in_tag) {
            if(c == '>') {
                in_tag = false;
                bool closing = tag_name_len > 0 && tag_name[0] == '/';
                const char* name = closing ? tag_name + 1 : tag_name;
                size_t name_len = closing ? tag_name_len - 1 : tag_name_len;

                if(tag_is(name, name_len, "script") || tag_is(name, name_len, "style")) {
                    skip_content = !closing;
                } else if(
                    !closing &&
                    (tag_is(name, name_len, "br") || tag_is(name, name_len, "p") ||
                     tag_is(name, name_len, "div") || tag_is(name, name_len, "li"))) {
                    if(!last_was_space) {
                        furi_string_push_back(out, '\n');
                        last_was_space = true;
                    }
                }
                continue;
            }
            if(tag_name_len < sizeof(tag_name) - 1) {
                tag_name[tag_name_len++] = c;
            }
            continue;
        }
        if(skip_content) continue;

        if(c == '&') {
            size_t remaining = html_len - i;
            if(remaining >= 5 && strncmp(&html[i], "&amp;", 5) == 0) {
                furi_string_push_back(out, '&');
                i += 4;
                last_was_space = false;
                continue;
            }
            if(remaining >= 4 && strncmp(&html[i], "&lt;", 4) == 0) {
                furi_string_push_back(out, '<');
                i += 3;
                last_was_space = false;
                continue;
            }
            if(remaining >= 4 && strncmp(&html[i], "&gt;", 4) == 0) {
                furi_string_push_back(out, '>');
                i += 3;
                last_was_space = false;
                continue;
            }
            if(remaining >= 6 && strncmp(&html[i], "&quot;", 6) == 0) {
                furi_string_push_back(out, '"');
                i += 5;
                last_was_space = false;
                continue;
            }
            if(remaining >= 5 && strncmp(&html[i], "&#39;", 5) == 0) {
                furi_string_push_back(out, '\'');
                i += 4;
                last_was_space = false;
                continue;
            }
            if(remaining >= 6 && strncmp(&html[i], "&nbsp;", 6) == 0) {
                i += 5;
                c = ' '; // fall through to whitespace handling
            }
        }

        if(c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if(!last_was_space) {
                furi_string_push_back(out, ' ');
                last_was_space = true;
            }
            continue;
        }

        furi_string_push_back(out, c);
        last_was_space = false;

        if(furi_string_size(out) >= BROWSER_MAX_TEXT) break;
    }
}

// ---- URL normalization ----

static bool browser_has_scheme(const char* url) {
    return strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0;
}

// ---- views ----

static void browser_status_draw_callback(Canvas* canvas, void* ctx) {
    FlipBrowserApp* app = ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 12, AlignCenter, AlignCenter, "Flip Browser");

    canvas_set_font(canvas, FontSecondary);
    if(app->state == BrowserStateError) {
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, "Error");
        canvas_draw_str_aligned(
            canvas, 64, 40, AlignCenter, AlignCenter, furi_string_get_cstr(app->status_text));
        canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignCenter, "Back to try again");
    } else {
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "Loading...");
    }
}

static bool browser_status_input_callback(InputEvent* input, void* ctx) {
    UNUSED(input);
    UNUSED(ctx);
    return false;
}

static void browser_back_callback(void* ctx) {
    FuriMessageQueue* queue = ctx;
    BrowserEvent event = {.type = BrowserEventBack};
    furi_message_queue_put(queue, &event, FuriWaitForever);
}

// ---- fetch + render ----

static void browser_text_input_result(void* ctx);

// text_input_reset() zeroes the widget's callback/buffer pointers, so it
// must run before (re-)registering them, not after.
static void browser_show_url_input(FlipBrowserApp* app) {
    app->state = BrowserStateUrlInput;
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Enter URL");
    text_input_set_result_callback(
        app->text_input, browser_text_input_result, app, app->url, sizeof(app->url), false);
    view_holder_set_view(app->view_holder, text_input_get_view(app->text_input));
}

static void browser_go(FlipBrowserApp* app) {
    if(strlen(app->url) == 0) return;

    if(!app->fhttp) {
        app->state = BrowserStateError;
        furi_string_set(app->status_text, "WiFi Devboard not detected");
        view_holder_set_view(app->view_holder, app->status_view);
        return;
    }

    app->state = BrowserStateLoading;
    view_holder_set_view(app->view_holder, app->status_view);

    if(storage_file_exists(app->storage, BROWSER_PAGE_PATH)) {
        storage_simply_remove_recursive(app->storage, BROWSER_PAGE_PATH);
    }
    strncpy(app->fhttp->file_path, BROWSER_PAGE_PATH, sizeof(app->fhttp->file_path) - 1);

    char full_url[BROWSER_URL_MAX + 8];
    if(browser_has_scheme(app->url)) {
        strncpy(full_url, app->url, sizeof(full_url) - 1);
    } else {
        snprintf(full_url, sizeof(full_url), "http://%s", app->url);
    }
    full_url[sizeof(full_url) - 1] = '\0';

    if(!flipper_http_request(app->fhttp, BYTES, full_url, "{}", NULL)) {
        app->state = BrowserStateError;
        furi_string_set(app->status_text, "Request failed to start");
        view_holder_set_view(app->view_holder, app->status_view);
    }
}

static void browser_render_page(FlipBrowserApp* app) {
    Stream* stream = file_stream_alloc(app->storage);
    if(!file_stream_open(stream, BROWSER_PAGE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        stream_free(stream);
        app->state = BrowserStateError;
        furi_string_set(app->status_text, "Could not read page");
        view_holder_set_view(app->view_holder, app->status_view);
        return;
    }

    char* raw = malloc(BROWSER_MAX_HTML_READ);
    size_t got = stream_read(stream, (uint8_t*)raw, BROWSER_MAX_HTML_READ);
    stream_free(stream);

    furi_string_reset(app->page_text);
    browser_strip_html(raw, got, app->page_text);
    free(raw);

    if(furi_string_size(app->page_text) == 0) {
        furi_string_set(app->page_text, "(no readable text on this page)");
    }

    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->page_text));

    app->state = BrowserStatePage;
    view_holder_set_view(app->view_holder, text_box_get_view(app->text_box));
}

static void browser_text_input_result(void* ctx) {
    FlipBrowserApp* app = ctx;
    browser_go(app);
}

// ---- app lifecycle ----

static FlipBrowserApp* browser_alloc(void) {
    FlipBrowserApp* app = malloc(sizeof(FlipBrowserApp));
    memset(app, 0, sizeof(FlipBrowserApp));

    app->storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(app->storage, BROWSER_DATA_DIR);
    app->status_text = furi_string_alloc();
    app->page_text = furi_string_alloc();
    app->queue = furi_message_queue_alloc(8, sizeof(BrowserEvent));

    app->fhttp = flipper_http_alloc();

    app->gui = furi_record_open(RECORD_GUI);
    app->view_holder = view_holder_alloc();
    view_holder_attach_to_gui(app->view_holder, app->gui);
    view_holder_set_back_callback(app->view_holder, browser_back_callback, app->queue);

    app->text_input = text_input_alloc();
    app->text_box = text_box_alloc();

    app->status_view = view_alloc();
    view_set_context(app->status_view, app);
    view_set_draw_callback(app->status_view, browser_status_draw_callback);
    view_set_input_callback(app->status_view, browser_status_input_callback);

    browser_show_url_input(app);
    view_holder_send_to_front(app->view_holder);

    return app;
}

static void browser_free(FlipBrowserApp* app) {
    view_holder_set_view(app->view_holder, NULL);
    view_free(app->status_view);
    text_box_free(app->text_box);
    text_input_free(app->text_input);
    view_holder_free(app->view_holder);
    furi_record_close(RECORD_GUI);

    flipper_http_free(app->fhttp);

    furi_message_queue_free(app->queue);
    furi_string_free(app->status_text);
    furi_string_free(app->page_text);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t flip_browser_app(void* p) {
    UNUSED(p);
    FlipBrowserApp* app = browser_alloc();

    bool running = true;
    while(running) {
        if(app->state == BrowserStateLoading) {
            if(app->fhttp->state == IDLE) {
                browser_render_page(app);
            } else if(app->fhttp->state == ISSUE) {
                app->state = BrowserStateError;
                furi_string_set(app->status_text, "Network error");
                view_holder_set_view(app->view_holder, app->status_view);
            }
        }

        BrowserEvent event;
        FuriStatus status = furi_message_queue_get(app->queue, &event, furi_ms_to_ticks(100));
        if(status != FuriStatusOk) continue;

        if(event.type == BrowserEventBack) {
            if(app->state == BrowserStatePage || app->state == BrowserStateError) {
                browser_show_url_input(app);
            } else {
                running = false;
            }
        }
    }

    browser_free(app);
    return 0;
}
