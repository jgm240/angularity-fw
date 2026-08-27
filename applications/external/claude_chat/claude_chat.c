#include "claude_chat.h"

#include <input/input.h>
#include <string.h>
#include <stdio.h>
#include <jsmn/jsmn_furi.h>

#define CLAUDE_TAG "ClaudeChat"
#define CLAUDE_DATA_DIR "/ext/apps_data/claude_chat"
#define CLAUDE_API_KEY_PATH CLAUDE_DATA_DIR "/api_key.txt"
#define CLAUDE_API_URL "https://api.anthropic.com/v1/messages"
#define CLAUDE_MODEL "claude-haiku-4-5-20251001"
#define CLAUDE_MAX_TOKENS 300

typedef enum {
    ClaudeEventTextInputDone,
    ClaudeEventBack,
} ClaudeEventType;

typedef struct {
    ClaudeEventType type;
} ClaudeEvent;

// ---- persistence ----

static bool claude_load_api_key(ClaudeChatApp* app) {
    if(!storage_file_exists(app->storage, CLAUDE_API_KEY_PATH)) return false;
    FuriString* contents = flipper_http_load_from_file(CLAUDE_API_KEY_PATH);
    if(!contents) return false;
    strncpy(app->api_key, furi_string_get_cstr(contents), sizeof(app->api_key) - 1);
    app->api_key[sizeof(app->api_key) - 1] = '\0';
    // Trim any trailing newline the loader may have preserved.
    size_t len = strlen(app->api_key);
    while(len > 0 && (app->api_key[len - 1] == '\n' || app->api_key[len - 1] == '\r')) {
        app->api_key[--len] = '\0';
    }
    furi_string_free(contents);
    return strlen(app->api_key) > 0;
}

static void claude_save_api_key(ClaudeChatApp* app) {
    storage_common_mkdir(app->storage, CLAUDE_DATA_DIR);
    flipper_http_append_to_file(app->api_key, strlen(app->api_key), true, CLAUDE_API_KEY_PATH);
}

// ---- JSON helpers ----

static void claude_json_escape(const char* in, char* out, size_t out_size) {
    size_t o = 0;
    for(size_t i = 0; in[i] != '\0' && o + 2 < out_size; i++) {
        char c = in[i];
        if(c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = c;
        } else if(c == '\n') {
            out[o++] = '\\';
            out[o++] = 'n';
        } else if((unsigned char)c >= 0x20) {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

// ---- status view ----

static void claude_status_draw_callback(Canvas* canvas, void* ctx) {
    ClaudeChatApp* app = ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 12, AlignCenter, AlignCenter, "Claude Chat");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas, 64, 32, AlignCenter, AlignCenter, furi_string_get_cstr(app->status_text));
    canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignCenter, "Back to cancel");
}

static bool claude_status_input_callback(InputEvent* input, void* ctx) {
    UNUSED(input);
    UNUSED(ctx);
    return false;
}

static void claude_back_callback(void* ctx) {
    FuriMessageQueue* queue = ctx;
    ClaudeEvent event = {.type = ClaudeEventBack};
    furi_message_queue_put(queue, &event, FuriWaitForever);
}

// ---- request ----

static void claude_send_prompt(ClaudeChatApp* app) {
    if(!app->fhttp) {
        app->state = ClaudeStateError;
        furi_string_set(app->status_text, "WiFi Devboard not detected");
        view_holder_set_view(app->view_holder, app->status_view);
        return;
    }
    app->state = ClaudeStateWaiting;
    furi_string_set(app->status_text, "Asking Claude...");
    view_holder_set_view(app->view_holder, app->status_view);

    char escaped_prompt[CLAUDE_PROMPT_MAX * 2];
    claude_json_escape(app->prompt_buffer, escaped_prompt, sizeof(escaped_prompt));

    char headers[CLAUDE_API_KEY_MAX + 128];
    snprintf(
        headers,
        sizeof(headers),
        "{\"content-type\":\"application/json\",\"x-api-key\":\"%s\",\"anthropic-version\":\"2023-06-01\"}",
        app->api_key);

    char payload[CLAUDE_PROMPT_MAX * 2 + 128];
    snprintf(
        payload,
        sizeof(payload),
        "{\"model\":\"%s\",\"max_tokens\":%d,\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
        CLAUDE_MODEL,
        CLAUDE_MAX_TOKENS,
        escaped_prompt);

    if(!flipper_http_request(app->fhttp, POST, CLAUDE_API_URL, headers, payload)) {
        app->state = ClaudeStateError;
        furi_string_set(app->status_text, "Request failed to start");
    }
}

static void claude_handle_response(ClaudeChatApp* app) {
    if(!app->fhttp->last_response) {
        app->state = ClaudeStateError;
        furi_string_set(app->status_text, "No response");
        view_holder_set_view(app->view_holder, app->status_view);
        return;
    }

    FuriString* json = furi_string_alloc_set_str(app->fhttp->last_response);

    FuriString* type = get_json_value_furi("type", json);
    if(type && furi_string_cmp_str(type, "error") == 0) {
        FuriString* error_obj = get_json_value_furi("error", json);
        FuriString* message = error_obj ? get_json_value_furi("message", error_obj) : NULL;
        furi_string_set(
            app->status_text, message ? furi_string_get_cstr(message) : "API error");
        app->state = ClaudeStateError;
        view_holder_set_view(app->view_holder, app->status_view);
        if(error_obj) furi_string_free(error_obj);
        if(message) furi_string_free(message);
        furi_string_free(type);
        furi_string_free(json);
        return;
    }
    if(type) furi_string_free(type);

    FuriString* first_block = get_json_array_value_furi("content", 0, json);
    FuriString* text = first_block ? get_json_value_furi("text", first_block) : NULL;

    if(text) {
        furi_string_set(app->response_text, text);
        text_box_set_text(app->text_box, furi_string_get_cstr(app->response_text));
        app->state = ClaudeStateResponse;
        view_holder_set_view(app->view_holder, text_box_get_view(app->text_box));
        furi_string_free(text);
    } else {
        furi_string_set(app->status_text, "Couldn't parse response");
        app->state = ClaudeStateError;
        view_holder_set_view(app->view_holder, app->status_view);
    }
    if(first_block) furi_string_free(first_block);
    furi_string_free(json);
}

// ---- text input flow ----

static void claude_text_input_result(void* ctx);

static void claude_show_prompt_input(ClaudeChatApp* app) {
    app->state = ClaudeStatePromptInput;
    app->prompt_buffer[0] = '\0';
    // text_input_reset() zeroes the widget's callback/buffer pointers, so it
    // must run before (re-)registering them, not after - doing this the other
    // way around leaves the widget with a null callback/buffer and crashes
    // the app the moment the user confirms their input.
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Ask Claude");
    text_input_set_result_callback(
        app->text_input,
        claude_text_input_result,
        app,
        app->prompt_buffer,
        sizeof(app->prompt_buffer),
        true);
    view_holder_set_view(app->view_holder, text_input_get_view(app->text_input));
}

static void claude_text_input_result(void* ctx) {
    ClaudeChatApp* app = ctx;
    if(app->state == ClaudeStateApiKeyInput) {
        if(strlen(app->api_key) == 0) return;
        claude_save_api_key(app);
        claude_show_prompt_input(app);
    } else if(app->state == ClaudeStatePromptInput) {
        if(strlen(app->prompt_buffer) == 0) return;
        claude_send_prompt(app);
    }
}

// ---- app lifecycle ----

static ClaudeChatApp* claude_alloc(void) {
    ClaudeChatApp* app = malloc(sizeof(ClaudeChatApp));
    memset(app, 0, sizeof(ClaudeChatApp));

    app->storage = furi_record_open(RECORD_STORAGE);
    app->status_text = furi_string_alloc();
    app->response_text = furi_string_alloc();
    app->queue = furi_message_queue_alloc(8, sizeof(ClaudeEvent));

    app->fhttp = flipper_http_alloc();

    app->gui = furi_record_open(RECORD_GUI);
    app->view_holder = view_holder_alloc();
    view_holder_attach_to_gui(app->view_holder, app->gui);
    view_holder_set_back_callback(app->view_holder, claude_back_callback, app->queue);

    app->text_input = text_input_alloc();
    app->text_box = text_box_alloc();

    app->status_view = view_alloc();
    view_set_context(app->status_view, app);
    view_set_draw_callback(app->status_view, claude_status_draw_callback);
    view_set_input_callback(app->status_view, claude_status_input_callback);

    if(!app->fhttp) {
        // flipper_http_alloc() returns NULL when the WiFi Devboard/UART
        // isn't ready. Every request path below dereferences app->fhttp,
        // so show an error instead of crashing the moment a prompt is sent.
        app->state = ClaudeStateError;
        furi_string_set(app->status_text, "WiFi Devboard not detected");
        view_holder_set_view(app->view_holder, app->status_view);
    } else if(claude_load_api_key(app)) {
        claude_show_prompt_input(app);
    } else {
        app->state = ClaudeStateApiKeyInput;
        text_input_set_header_text(app->text_input, "Enter Anthropic API key");
        text_input_set_result_callback(
            app->text_input,
            claude_text_input_result,
            app,
            app->api_key,
            sizeof(app->api_key),
            true);
        view_holder_set_view(app->view_holder, text_input_get_view(app->text_input));
    }

    view_holder_send_to_front(app->view_holder);
    return app;
}

static void claude_free(ClaudeChatApp* app) {
    view_holder_set_view(app->view_holder, NULL);
    view_free(app->status_view);
    text_box_free(app->text_box);
    text_input_free(app->text_input);
    view_holder_free(app->view_holder);
    furi_record_close(RECORD_GUI);

    flipper_http_free(app->fhttp);

    furi_message_queue_free(app->queue);
    furi_string_free(app->status_text);
    furi_string_free(app->response_text);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t claude_chat_app(void* p) {
    UNUSED(p);
    ClaudeChatApp* app = claude_alloc();

    bool running = true;
    while(running) {
        if(app->state == ClaudeStateWaiting) {
            if(app->fhttp->state == IDLE) {
                claude_handle_response(app);
                view_holder_send_to_front(app->view_holder);
            } else if(app->fhttp->state == ISSUE) {
                app->state = ClaudeStateError;
                furi_string_set(app->status_text, "Network error");
                view_holder_send_to_front(app->view_holder);
            }
        }

        ClaudeEvent event;
        FuriStatus status = furi_message_queue_get(app->queue, &event, furi_ms_to_ticks(150));
        if(status != FuriStatusOk) continue;

        if(event.type == ClaudeEventBack) {
            if(app->state == ClaudeStateResponse || app->state == ClaudeStateError) {
                claude_show_prompt_input(app);
                view_holder_send_to_front(app->view_holder);
            } else {
                running = false;
            }
        }
    }

    claude_free(app);
    return 0;
}
