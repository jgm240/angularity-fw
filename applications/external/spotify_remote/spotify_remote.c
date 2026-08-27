#include "spotify_remote.h"

#include <input/input.h>
#include <string.h>
#include <stdio.h>
#include <jsmn/jsmn_furi.h>

#define SPOTIFY_DATA_DIR "/ext/apps_data/spotify_remote"
#define SPOTIFY_CLIENT_ID_PATH SPOTIFY_DATA_DIR "/client_id.txt"
#define SPOTIFY_CLIENT_SECRET_PATH SPOTIFY_DATA_DIR "/client_secret.txt"
#define SPOTIFY_REFRESH_TOKEN_PATH SPOTIFY_DATA_DIR "/refresh_token.txt"

#define SPOTIFY_TOKEN_URL "https://accounts.spotify.com/api/token"
#define SPOTIFY_API_BASE "https://api.spotify.com/v1/me/player"

#define SPOTIFY_POLL_INTERVAL_MS 5000

typedef enum {
    SpotifyEventTextInputDone,
    SpotifyEventBack,
    SpotifyEventUp,
    SpotifyEventDown,
    SpotifyEventLeft,
    SpotifyEventRight,
    SpotifyEventOk,
    SpotifyEventPoll,
} SpotifyEventType;

typedef struct {
    SpotifyEventType type;
} SpotifyEvent;

// ---- persistence ----

static bool spotify_load_file(const char* path, char* out, size_t out_size) {
    FuriString* contents = flipper_http_load_from_file((char*)path);
    if(!contents) return false;
    strncpy(out, furi_string_get_cstr(contents), out_size - 1);
    out[out_size - 1] = '\0';
    size_t len = strlen(out);
    while(len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) out[--len] = '\0';
    furi_string_free(contents);
    return strlen(out) > 0;
}

static bool spotify_load_credentials(SpotifyRemoteApp* app) {
    if(!storage_file_exists(app->storage, SPOTIFY_CLIENT_ID_PATH)) return false;
    if(!spotify_load_file(SPOTIFY_CLIENT_ID_PATH, app->client_id, sizeof(app->client_id)))
        return false;
    if(!spotify_load_file(
           SPOTIFY_CLIENT_SECRET_PATH, app->client_secret, sizeof(app->client_secret)))
        return false;
    if(!spotify_load_file(
           SPOTIFY_REFRESH_TOKEN_PATH, app->refresh_token, sizeof(app->refresh_token)))
        return false;
    return true;
}

static void spotify_save_file(const char* path, const char* value) {
    flipper_http_append_to_file(value, strlen(value), true, (char*)path);
}

// ---- setup flow ----

static void spotify_begin_authorize(SpotifyRemoteApp* app);

static void spotify_text_input_result(void* ctx);

// text_input_reset() zeroes the widget's callback/buffer pointers, so it
// must run before (re-)registering them, not after - doing this the other
// way around leaves the widget with a null callback/buffer and crashes the
// app the moment the user confirms their input.
static void spotify_show_setup_step(SpotifyRemoteApp* app) {
    text_input_reset(app->text_input);
    char* buffer;
    size_t buffer_size;
    const char* header;
    if(app->state == SpotifyStateSetupClientId) {
        header = "Spotify Client ID";
        buffer = app->client_id;
        buffer_size = sizeof(app->client_id);
    } else if(app->state == SpotifyStateSetupClientSecret) {
        header = "Spotify Client Secret";
        buffer = app->client_secret;
        buffer_size = sizeof(app->client_secret);
    } else {
        header = "Refresh Token";
        buffer = app->refresh_token;
        buffer_size = sizeof(app->refresh_token);
    }
    text_input_set_header_text(app->text_input, header);
    text_input_set_result_callback(
        app->text_input, spotify_text_input_result, app, buffer, buffer_size, true);
    view_holder_set_view(app->view_holder, text_input_get_view(app->text_input));
}

static void spotify_text_input_result(void* ctx) {
    SpotifyRemoteApp* app = ctx;
    if(app->state == SpotifyStateSetupClientId) {
        if(strlen(app->client_id) == 0) return;
        spotify_save_file(SPOTIFY_CLIENT_ID_PATH, app->client_id);
        app->state = SpotifyStateSetupClientSecret;
        spotify_show_setup_step(app);
    } else if(app->state == SpotifyStateSetupClientSecret) {
        if(strlen(app->client_secret) == 0) return;
        spotify_save_file(SPOTIFY_CLIENT_SECRET_PATH, app->client_secret);
        app->state = SpotifyStateSetupRefreshToken;
        spotify_show_setup_step(app);
    } else if(app->state == SpotifyStateSetupRefreshToken) {
        if(strlen(app->refresh_token) == 0) return;
        spotify_save_file(SPOTIFY_REFRESH_TOKEN_PATH, app->refresh_token);
        spotify_begin_authorize(app);
    }
}

// ---- remote view ----

static void spotify_remote_draw_callback(Canvas* canvas, void* ctx) {
    SpotifyRemoteApp* app = ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignCenter, "Spotify Remote");

    canvas_set_font(canvas, FontSecondary);
    if(app->state == SpotifyStateError) {
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, "Error");
        canvas_draw_str_aligned(
            canvas, 64, 40, AlignCenter, AlignCenter, furi_string_get_cstr(app->status_text));
    } else if(app->state == SpotifyStateAuthorizing) {
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "Connecting...");
    } else if(!app->have_track) {
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, "Nothing playing");
    } else {
        canvas_draw_str_aligned(
            canvas, 64, 22, AlignCenter, AlignCenter, furi_string_get_cstr(app->track_name));
        canvas_draw_str_aligned(
            canvas, 64, 34, AlignCenter, AlignCenter, furi_string_get_cstr(app->artist_name));
        canvas_draw_str_aligned(
            canvas, 64, 46, AlignCenter, AlignCenter, app->is_playing ? "Playing" : "Paused");
    }
    canvas_draw_str_aligned(
        canvas, 64, 58, AlignCenter, AlignCenter, "<</OK/>>  Up/Dn: Vol  Back: Exit");
}

static bool spotify_remote_input_callback(InputEvent* input, void* ctx) {
    SpotifyRemoteApp* app = ctx;
    if(app->state != SpotifyStateRemote) return false;
    if(input->type != InputTypeShort) return false;

    SpotifyEvent event;
    switch(input->key) {
    case InputKeyUp:
        event.type = SpotifyEventUp;
        break;
    case InputKeyDown:
        event.type = SpotifyEventDown;
        break;
    case InputKeyLeft:
        event.type = SpotifyEventLeft;
        break;
    case InputKeyRight:
        event.type = SpotifyEventRight;
        break;
    case InputKeyOk:
        event.type = SpotifyEventOk;
        break;
    default:
        return false;
    }
    furi_message_queue_put(app->queue, &event, FuriWaitForever);
    return true;
}

static void spotify_back_callback(void* ctx) {
    FuriMessageQueue* queue = ctx;
    SpotifyEvent event = {.type = SpotifyEventBack};
    furi_message_queue_put(queue, &event, FuriWaitForever);
}

static void spotify_poll_timer_callback(void* ctx) {
    FuriMessageQueue* queue = ctx;
    SpotifyEvent event = {.type = SpotifyEventPoll};
    furi_message_queue_put(queue, &event, 0);
}

static void spotify_redraw_timer_callback(void* ctx) {
    SpotifyRemoteApp* app = ctx;
    view_holder_send_to_front(app->view_holder);
}

// ---- requests ----

static void spotify_refresh_access_token(SpotifyRemoteApp* app, SpotifyPendingAction then_action) {
    if(!app->fhttp) {
        app->state = SpotifyStateError;
        furi_string_set(app->status_text, "WiFi Devboard not detected");
        view_holder_set_view(app->view_holder, app->remote_view);
        return;
    }
    app->pending_action = SpotifyPendingTokenRefresh;
    app->action_after_refresh = then_action;

    char form_body[SPOTIFY_CRED_MAX * 3 + 64];
    snprintf(
        form_body,
        sizeof(form_body),
        "grant_type=refresh_token&refresh_token=%s&client_id=%s&client_secret=%s",
        app->refresh_token,
        app->client_id,
        app->client_secret);

    char payload[sizeof(form_body) + 4];
    snprintf(payload, sizeof(payload), "\"%s\"", form_body);

    const char* headers = "{\"Content-Type\":\"application/x-www-form-urlencoded\"}";
    flipper_http_request(app->fhttp, POST, SPOTIFY_TOKEN_URL, headers, payload);
}

static void spotify_begin_authorize(SpotifyRemoteApp* app) {
    app->state = SpotifyStateAuthorizing;
    view_holder_set_view(app->view_holder, app->remote_view);
    spotify_refresh_access_token(app, SpotifyPendingNowPlaying);
}

static void spotify_auth_headers(SpotifyRemoteApp* app, char* out, size_t out_size) {
    snprintf(out, out_size, "{\"Authorization\":\"Bearer %s\"}", app->access_token);
}

static void spotify_request_now_playing(SpotifyRemoteApp* app) {
    app->pending_action = SpotifyPendingNowPlaying;
    char headers[SPOTIFY_CRED_MAX + 32];
    spotify_auth_headers(app, headers, sizeof(headers));
    flipper_http_request(
        app->fhttp, GET, SPOTIFY_API_BASE "/currently-playing", headers, NULL);
}

static void spotify_do_action(SpotifyRemoteApp* app, SpotifyPendingAction action) {
    if(strlen(app->access_token) == 0) {
        spotify_refresh_access_token(app, action);
        return;
    }
    app->pending_action = action;
    char headers[SPOTIFY_CRED_MAX + 32];
    spotify_auth_headers(app, headers, sizeof(headers));

    switch(action) {
    case SpotifyPendingPlay:
        flipper_http_request(app->fhttp, PUT, SPOTIFY_API_BASE "/play", headers, "{}");
        break;
    case SpotifyPendingPause:
        flipper_http_request(app->fhttp, PUT, SPOTIFY_API_BASE "/pause", headers, "{}");
        break;
    case SpotifyPendingNext:
        flipper_http_request(app->fhttp, POST, SPOTIFY_API_BASE "/next", headers, "{}");
        break;
    case SpotifyPendingPrev:
        flipper_http_request(app->fhttp, POST, SPOTIFY_API_BASE "/previous", headers, "{}");
        break;
    case SpotifyPendingVolume: {
        char url[128];
        snprintf(
            url,
            sizeof(url),
            SPOTIFY_API_BASE "/volume?volume_percent=%d",
            app->volume_percent);
        flipper_http_request(app->fhttp, PUT, url, headers, "{}");
        break;
    }
    default:
        break;
    }
}

// ---- response handling ----

static FuriString* spotify_get_nested(const FuriString* json, const char* key) {
    return get_json_value_furi(key, json);
}

static void spotify_handle_now_playing_response(SpotifyRemoteApp* app) {
    if(!app->fhttp->last_response || strlen(app->fhttp->last_response) == 0) {
        app->have_track = false;
        return;
    }
    FuriString* json = furi_string_alloc_set_str(app->fhttp->last_response);

    FuriString* is_playing_str = spotify_get_nested(json, "is_playing");
    app->is_playing = is_playing_str && furi_string_cmp_str(is_playing_str, "true") == 0;
    if(is_playing_str) furi_string_free(is_playing_str);

    FuriString* item = spotify_get_nested(json, "item");
    if(item) {
        FuriString* name = spotify_get_nested(item, "name");
        FuriString* first_artist = get_json_array_value_furi("artists", 0, item);
        FuriString* artist = first_artist ? spotify_get_nested(first_artist, "name") : NULL;

        if(name) {
            furi_string_set(app->track_name, name);
            furi_string_free(name);
        }
        if(artist) {
            furi_string_set(app->artist_name, artist);
            furi_string_free(artist);
        }
        app->have_track = true;
        if(first_artist) furi_string_free(first_artist);
        furi_string_free(item);
    } else {
        app->have_track = false;
    }
    furi_string_free(json);
}

static void spotify_handle_response(SpotifyRemoteApp* app) {
    SpotifyPendingAction completed = app->pending_action;
    app->pending_action = SpotifyPendingNone;

    if(completed == SpotifyPendingTokenRefresh) {
        FuriString* json = furi_string_alloc_set_str(
            app->fhttp->last_response ? app->fhttp->last_response : "");
        FuriString* token = get_json_value_furi("access_token", json);
        if(token) {
            strncpy(app->access_token, furi_string_get_cstr(token), sizeof(app->access_token) - 1);
            app->access_token[sizeof(app->access_token) - 1] = '\0';
            furi_string_free(token);
            app->state = SpotifyStateRemote;
            SpotifyPendingAction next = app->action_after_refresh;
            app->action_after_refresh = SpotifyPendingNone;
            if(next == SpotifyPendingNowPlaying) {
                spotify_request_now_playing(app);
            } else if(next != SpotifyPendingNone) {
                spotify_do_action(app, next);
            }
        } else {
            app->state = SpotifyStateError;
            furi_string_set(app->status_text, "Auth failed - check credentials");
        }
        furi_string_free(json);
        return;
    }

    if(completed == SpotifyPendingNowPlaying) {
        spotify_handle_now_playing_response(app);
        return;
    }

    // Play/Pause/Next/Prev/Volume: Spotify returns empty body on success.
    // Optimistically update local state, then re-poll shortly for the truth.
    if(completed == SpotifyPendingPlay) app->is_playing = true;
    if(completed == SpotifyPendingPause) app->is_playing = false;
    spotify_request_now_playing(app);
}

// ---- app lifecycle ----

static SpotifyRemoteApp* spotify_alloc(void) {
    SpotifyRemoteApp* app = malloc(sizeof(SpotifyRemoteApp));
    memset(app, 0, sizeof(SpotifyRemoteApp));

    app->storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(app->storage, SPOTIFY_DATA_DIR);
    app->track_name = furi_string_alloc();
    app->artist_name = furi_string_alloc();
    app->status_text = furi_string_alloc();
    app->queue = furi_message_queue_alloc(8, sizeof(SpotifyEvent));
    app->volume_percent = 50;

    app->fhttp = flipper_http_alloc();

    app->gui = furi_record_open(RECORD_GUI);
    app->view_holder = view_holder_alloc();
    view_holder_attach_to_gui(app->view_holder, app->gui);
    view_holder_set_back_callback(app->view_holder, spotify_back_callback, app->queue);

    app->text_input = text_input_alloc();
    app->remote_view = view_alloc();
    view_set_context(app->remote_view, app);
    view_set_draw_callback(app->remote_view, spotify_remote_draw_callback);
    view_set_input_callback(app->remote_view, spotify_remote_input_callback);

    app->poll_timer = furi_timer_alloc(spotify_poll_timer_callback, FuriTimerTypePeriodic, app->queue);
    app->redraw_timer = furi_timer_alloc(spotify_redraw_timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->redraw_timer, furi_ms_to_ticks(500));

    if(!app->fhttp) {
        // flipper_http_alloc() returns NULL when the WiFi Devboard/UART
        // isn't ready. If credentials were already saved from a previous
        // run, the normal path below calls spotify_begin_authorize()
        // immediately on launch with no user interaction at all - show an
        // error instead of crashing right there.
        app->state = SpotifyStateError;
        furi_string_set(app->status_text, "WiFi Devboard not detected");
        view_holder_set_view(app->view_holder, app->remote_view);
    } else if(spotify_load_credentials(app)) {
        spotify_begin_authorize(app);
    } else {
        app->state = SpotifyStateSetupClientId;
        spotify_show_setup_step(app);
    }

    view_holder_send_to_front(app->view_holder);
    return app;
}

static void spotify_free(SpotifyRemoteApp* app) {
    furi_timer_stop(app->poll_timer);
    furi_timer_free(app->poll_timer);
    furi_timer_stop(app->redraw_timer);
    furi_timer_free(app->redraw_timer);

    view_holder_set_view(app->view_holder, NULL);
    view_free(app->remote_view);
    text_input_free(app->text_input);
    view_holder_free(app->view_holder);
    furi_record_close(RECORD_GUI);

    flipper_http_free(app->fhttp);

    furi_message_queue_free(app->queue);
    furi_string_free(app->track_name);
    furi_string_free(app->artist_name);
    furi_string_free(app->status_text);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t spotify_remote_app(void* p) {
    UNUSED(p);
    SpotifyRemoteApp* app = spotify_alloc();

    bool running = true;
    bool poll_started = false;
    while(running) {
        if(app->state == SpotifyStateRemote && !poll_started) {
            furi_timer_start(app->poll_timer, furi_ms_to_ticks(SPOTIFY_POLL_INTERVAL_MS));
            poll_started = true;
        }

        if(app->pending_action != SpotifyPendingNone) {
            if(app->fhttp->state == IDLE) {
                spotify_handle_response(app);
            } else if(app->fhttp->state == ISSUE) {
                app->pending_action = SpotifyPendingNone;
                app->state = SpotifyStateError;
                furi_string_set(app->status_text, "Network error");
            }
        }

        SpotifyEvent event;
        FuriStatus status = furi_message_queue_get(app->queue, &event, furi_ms_to_ticks(100));
        if(status != FuriStatusOk) continue;

        if(app->pending_action != SpotifyPendingNone && event.type != SpotifyEventBack) {
            continue; // ignore input/poll while a request is in flight
        }

        switch(event.type) {
        case SpotifyEventBack:
            running = false;
            break;
        case SpotifyEventPoll:
            if(app->state == SpotifyStateRemote) spotify_request_now_playing(app);
            break;
        case SpotifyEventOk:
            spotify_do_action(app, app->is_playing ? SpotifyPendingPause : SpotifyPendingPlay);
            break;
        case SpotifyEventLeft:
            spotify_do_action(app, SpotifyPendingPrev);
            break;
        case SpotifyEventRight:
            spotify_do_action(app, SpotifyPendingNext);
            break;
        case SpotifyEventUp:
            app->volume_percent = app->volume_percent + 10 > 100 ? 100 : app->volume_percent + 10;
            spotify_do_action(app, SpotifyPendingVolume);
            break;
        case SpotifyEventDown:
            app->volume_percent = app->volume_percent - 10 < 0 ? 0 : app->volume_percent - 10;
            spotify_do_action(app, SpotifyPendingVolume);
            break;
        default:
            break;
        }
    }

    spotify_free(app);
    return 0;
}
