#include "dolphin_music.h"
#include "net_audio_hal.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <input/input.h>
#include <stm32wbxx_ll_dma.h>
#include <toolbox/stream/stream.h>
#include <toolbox/stream/file_stream.h>

#define DOLPHIN_DATA_DIR "/ext/apps_data/dolphin_music"
#define DOLPHIN_URL_PATH DOLPHIN_DATA_DIR "/server_url.txt"
#define DOLPHIN_CHUNK_PATH DOLPHIN_DATA_DIR "/chunk.bin"

typedef enum {
    DolphinEventHalfTransfer,
    DolphinEventFullTransfer,
    DolphinEventVolUp,
    DolphinEventVolDn,
    DolphinEventBack,
} DolphinEventType;

typedef struct {
    DolphinEventType type;
} DolphinEvent;

// ---- persistence ----

static bool dolphin_load_url(DolphinMusicApp* app) {
    if(!storage_file_exists(app->storage, DOLPHIN_URL_PATH)) return false;
    FuriString* contents = flipper_http_load_from_file(DOLPHIN_URL_PATH);
    if(!contents) return false;
    strncpy(app->server_url, furi_string_get_cstr(contents), sizeof(app->server_url) - 1);
    app->server_url[sizeof(app->server_url) - 1] = '\0';
    size_t len = strlen(app->server_url);
    while(len > 0 && (app->server_url[len - 1] == '\n' || app->server_url[len - 1] == '\r')) {
        app->server_url[--len] = '\0';
    }
    furi_string_free(contents);
    return strlen(app->server_url) > 0;
}

static void dolphin_save_url(DolphinMusicApp* app) {
    flipper_http_append_to_file(
        app->server_url, strlen(app->server_url), true, DOLPHIN_URL_PATH);
}

// ---- sample conversion ----

static uint8_t dolphin_convert_sample(DolphinMusicApp* app, uint8_t raw) {
    float data = (float)raw - 127.5f;
    data /= 127.5f;
    data *= app->volume;
    data = tanhf(data);
    data *= 127.5f;
    data += 127.5f;
    if(data < 0.0f) data = 0.0f;
    if(data > 255.0f) data = 255.0f;
    return (uint8_t)data;
}

static void dolphin_dma_isr(void* ctx);

// ---- network fetch ----
//
// FlipperHTTP requests are asynchronous: kick one off, then let the main
// loop notice fhttp->state going back to IDLE/ISSUE. Never block waiting
// for that here - this module's entry points (dolphin_begin_fetch) get
// called from the TextInput result callback and from queued DMA-interrupt
// events, both of which run on the thread that also has to keep servicing
// input/redraws. A blocking wait in either place freezes the whole app for
// as long as the fetch takes - which is exactly what happened before this
// was split into begin/complete halves.

static void dolphin_begin_fetch(DolphinMusicApp* app, DolphinFetchTarget target) {
    if(!app->fhttp) {
        app->state = DolphinStateError;
        furi_string_set(app->status_text, "WiFi Devboard not detected");
        view_holder_set_view(app->view_holder, app->status_view);
        return;
    }
    if(storage_file_exists(app->storage, DOLPHIN_CHUNK_PATH)) {
        storage_simply_remove_recursive(app->storage, DOLPHIN_CHUNK_PATH);
    }
    strncpy(app->fhttp->file_path, DOLPHIN_CHUNK_PATH, sizeof(app->fhttp->file_path) - 1);

    app->pending_fetch = target;
    if(!flipper_http_request(app->fhttp, BYTES, app->chunk_url, "{}", NULL)) {
        app->pending_fetch = DolphinFetchNone;
        app->state = DolphinStateError;
        furi_string_set(app->status_text, "Request failed to start");
    }
}

static void dolphin_finish_priming(DolphinMusicApp* app) {
    if(!furi_hal_speaker_acquire(1000)) {
        app->state = DolphinStateError;
        furi_string_set(app->status_text, "Speaker busy");
        free(app->sample_buffer);
        app->sample_buffer = NULL;
        return;
    }

    net_audio_hal_init(DOLPHIN_SAMPLE_RATE);
    net_audio_hal_dma_init((uint32_t)app->sample_buffer, app->samples_count);
    net_audio_hal_set_dma_isr(dolphin_dma_isr, app->queue);
    net_audio_hal_dma_start();
    net_audio_hal_start();

    app->state = DolphinStateStreaming;
    app->streaming = true;
}

// Called from the main loop once fhttp->state has left RECEIVING - reads
// whatever the fetch produced (successful or not) into the target half of
// the sample buffer, then advances the priming sequence or just leaves the
// buffer ready for the next HT/TC-triggered refill.
static void dolphin_complete_fetch(DolphinMusicApp* app) {
    DolphinFetchTarget target = app->pending_fetch;
    app->pending_fetch = DolphinFetchNone;

    bool first_half = (target == DolphinFetchPrimeA || target == DolphinFetchRefillA);
    uint8_t* out = &app->sample_buffer[first_half ? 0 : app->samples_count_half];
    size_t count = app->samples_count_half;

    size_t got = 0;
    if(app->fhttp->state == IDLE) {
        Stream* stream = file_stream_alloc(app->storage);
        if(file_stream_open(stream, DOLPHIN_CHUNK_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
            got = stream_read(stream, out, count);
        }
        stream_free(stream);
    }
    for(size_t i = 0; i < count; i++) {
        uint8_t raw = i < got ? out[i] : 0x80; // pad shortfalls with silence
        out[i] = dolphin_convert_sample(app, raw);
    }

    if(target == DolphinFetchPrimeA) {
        dolphin_begin_fetch(app, DolphinFetchPrimeB);
    } else if(target == DolphinFetchPrimeB) {
        dolphin_finish_priming(app);
    }
    // Refill targets need no follow-up - the buffer is ready in place for
    // whichever half the DMA laps into next.
}

// ---- DMA ISR ----

static void dolphin_dma_isr(void* ctx) {
    FuriMessageQueue* queue = ctx;
    if(LL_DMA_IsActiveFlag_HT1(DMA1)) {
        LL_DMA_ClearFlag_HT1(DMA1);
        DolphinEvent event = {.type = DolphinEventHalfTransfer};
        furi_message_queue_put(queue, &event, 0);
    }
    if(LL_DMA_IsActiveFlag_TC1(DMA1)) {
        LL_DMA_ClearFlag_TC1(DMA1);
        DolphinEvent event = {.type = DolphinEventFullTransfer};
        furi_message_queue_put(queue, &event, 0);
    }
}

// ---- status/streaming view ----

static void dolphin_draw_callback(Canvas* canvas, void* ctx) {
    DolphinMusicApp* app = ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 12, AlignCenter, AlignCenter, "DolphinMusic");

    canvas_set_font(canvas, FontSecondary);
    if(app->state == DolphinStateError) {
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, "Error");
        canvas_draw_str_aligned(
            canvas, 64, 40, AlignCenter, AlignCenter, furi_string_get_cstr(app->status_text));
    } else if(app->state == DolphinStatePriming) {
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "Connecting...");
    } else {
        canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, "Streaming...");
        char volume_line[32];
        snprintf(volume_line, sizeof(volume_line), "Volume: %d%%  Up/Down", (int)(app->volume * 100.0f));
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, volume_line);
    }
    canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignCenter, "Back to exit");
}

static bool dolphin_input_callback(InputEvent* input, void* ctx) {
    DolphinMusicApp* app = ctx;
    if(app->state != DolphinStateStreaming) return false;
    if((input->type != InputTypeShort) && (input->type != InputTypeRepeat)) return false;

    DolphinEvent event;
    if(input->key == InputKeyUp) {
        event.type = DolphinEventVolUp;
    } else if(input->key == InputKeyDown) {
        event.type = DolphinEventVolDn;
    } else {
        return false;
    }
    furi_message_queue_put(app->queue, &event, FuriWaitForever);
    return true;
}

static void dolphin_back_callback(void* ctx) {
    FuriMessageQueue* queue = ctx;
    DolphinEvent event = {.type = DolphinEventBack};
    furi_message_queue_put(queue, &event, FuriWaitForever);
}

// ---- setup + start ----

// The input screen's hint text reads "(http://ip:port)", which is easy to
// read as "type ip:port" rather than "include the http:// part" - accept
// both instead of failing on a URL with no scheme.
static bool dolphin_has_scheme(const char* url) {
    return strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0;
}

static void dolphin_start_streaming(DolphinMusicApp* app) {
    if(dolphin_has_scheme(app->server_url)) {
        snprintf(app->chunk_url, sizeof(app->chunk_url), "%s/chunk", app->server_url);
    } else {
        snprintf(app->chunk_url, sizeof(app->chunk_url), "http://%s/chunk", app->server_url);
    }

    app->samples_count_half = DOLPHIN_SAMPLE_RATE * 2; // ~2s per half, matches server chunk size
    app->samples_count = app->samples_count_half * 2;
    app->sample_buffer = malloc(app->samples_count);
    memset(app->sample_buffer, 0x80, app->samples_count);
    app->volume = 1.0f;

    app->state = DolphinStatePriming;
    furi_string_set(app->status_text, "Connecting...");
    view_holder_set_view(app->view_holder, app->status_view);

    // Speaker/DMA don't start until both halves are primed (see
    // dolphin_finish_priming, called once the second fetch completes in the
    // main loop) - this call only kicks off the first fetch and returns.
    dolphin_begin_fetch(app, DolphinFetchPrimeA);
}

static void dolphin_text_input_result(void* ctx) {
    DolphinMusicApp* app = ctx;
    if(strlen(app->server_url) == 0) return;
    dolphin_save_url(app);
    dolphin_start_streaming(app);
}

// ---- app lifecycle ----

static DolphinMusicApp* dolphin_alloc(void) {
    DolphinMusicApp* app = malloc(sizeof(DolphinMusicApp));
    memset(app, 0, sizeof(DolphinMusicApp));

    app->storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(app->storage, DOLPHIN_DATA_DIR);
    app->status_text = furi_string_alloc();
    app->queue = furi_message_queue_alloc(8, sizeof(DolphinEvent));
    app->volume = 1.0f;

    app->fhttp = flipper_http_alloc();

    app->gui = furi_record_open(RECORD_GUI);
    app->view_holder = view_holder_alloc();
    view_holder_attach_to_gui(app->view_holder, app->gui);
    view_holder_set_back_callback(app->view_holder, dolphin_back_callback, app->queue);

    app->text_input = text_input_alloc();
    text_input_set_header_text(app->text_input, "Server ip:port");
    text_input_set_result_callback(
        app->text_input,
        dolphin_text_input_result,
        app,
        app->server_url,
        sizeof(app->server_url),
        true);

    app->status_view = view_alloc();
    view_set_context(app->status_view, app);
    view_set_draw_callback(app->status_view, dolphin_draw_callback);
    view_set_input_callback(app->status_view, dolphin_input_callback);

    if(!app->fhttp) {
        // flipper_http_alloc() returns NULL when the WiFi Devboard/UART
        // isn't ready (not attached, still booting, etc). Every other path
        // below eventually dereferences app->fhttp, so bail out to the
        // error view now instead of crashing the moment a URL is typed or
        // loaded - this is what a saved server_url.txt from a previous run
        // triggered immediately, since dolphin_start_streaming() runs on
        // launch with no user interaction at all in that case.
        app->state = DolphinStateError;
        furi_string_set(app->status_text, "WiFi Devboard not detected");
        view_holder_set_view(app->view_holder, app->status_view);
    } else if(dolphin_load_url(app)) {
        dolphin_start_streaming(app);
    } else {
        app->state = DolphinStateUrlInput;
        view_holder_set_view(app->view_holder, text_input_get_view(app->text_input));
    }

    view_holder_send_to_front(app->view_holder);
    return app;
}

static void dolphin_free(DolphinMusicApp* app) {
    if(app->streaming) {
        net_audio_hal_stop();
        net_audio_hal_dma_stop();
        net_audio_hal_set_dma_isr(NULL, NULL);
        net_audio_hal_deinit();
        furi_hal_speaker_release();
    }
    if(app->sample_buffer) free(app->sample_buffer);

    view_holder_set_view(app->view_holder, NULL);
    view_free(app->status_view);
    text_input_free(app->text_input);
    view_holder_free(app->view_holder);
    furi_record_close(RECORD_GUI);

    flipper_http_free(app->fhttp);

    furi_message_queue_free(app->queue);
    furi_string_free(app->status_text);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t dolphin_music_app(void* p) {
    UNUSED(p);
    DolphinMusicApp* app = dolphin_alloc();

    bool running = true;
    while(running) {
        if(app->pending_fetch != DolphinFetchNone) {
            if(app->fhttp->state == IDLE) {
                dolphin_complete_fetch(app);
            } else if(app->fhttp->state == ISSUE) {
                app->pending_fetch = DolphinFetchNone;
                app->state = DolphinStateError;
                furi_string_set(app->status_text, "Network error");
            }
        }

        DolphinEvent event;
        FuriStatus status = furi_message_queue_get(app->queue, &event, furi_ms_to_ticks(50));
        if(status != FuriStatusOk) {
            if(app->state == DolphinStateStreaming || app->state == DolphinStatePriming) {
                view_holder_send_to_front(app->view_holder);
            }
            continue;
        }

        switch(event.type) {
        case DolphinEventHalfTransfer:
            // If a refill is already in flight, the network hasn't kept up
            // with playback - skip this cycle rather than stacking a second
            // request; that half just replays its stale contents once more.
            if(app->state == DolphinStateStreaming && app->pending_fetch == DolphinFetchNone) {
                dolphin_begin_fetch(app, DolphinFetchRefillA);
            }
            break;
        case DolphinEventFullTransfer:
            if(app->state == DolphinStateStreaming && app->pending_fetch == DolphinFetchNone) {
                dolphin_begin_fetch(app, DolphinFetchRefillB);
            }
            break;
        case DolphinEventVolUp:
            if(app->volume < 5.0f) app->volume += 0.2f;
            break;
        case DolphinEventVolDn:
            if(app->volume > 0.05f) app->volume -= 0.2f;
            break;
        case DolphinEventBack:
            running = false;
            break;
        }
        view_holder_send_to_front(app->view_holder);
    }

    dolphin_free(app);
    return 0;
}
