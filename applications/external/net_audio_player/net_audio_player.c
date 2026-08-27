#define MINIMP3_IMPLEMENTATION
#include "net_audio_player.h"
#include "net_audio_hal.h"

#include <math.h>
#include <string.h>
#include <input/input.h>
#include <stm32wbxx_ll_dma.h>

#define NAP_TAG "NetAudioPlayer"
#define NAP_DOWNLOAD_DIR "/ext/apps_data/net_audio_player"
#define NAP_DOWNLOAD_PATH NAP_DOWNLOAD_DIR "/download.dat"

typedef enum {
    NapEventHalfTransfer,
    NapEventFullTransfer,
    NapEventCtrlVolUp,
    NapEventCtrlVolDn,
    NapEventCtrlBack,
} NapEventType;

typedef struct {
    NapEventType type;
} NapEvent;

// ---- sample conversion (shared by WAV and MP3 paths) ----

static uint8_t nap_convert_sample(NetAudioPlayerApp* app, int16_t sample) {
    float data = (float)sample / 256.0f;
    data /= 127.5f;
    data *= app->volume;
    data = tanhf(data);
    data *= 127.5f;
    data += 127.5f;
    if(data < 0.0f) data = 0.0f;
    if(data > 255.0f) data = 255.0f;
    return (uint8_t)data;
}

// ---- WAV path ----

static bool nap_parse_wav(NetAudioPlayerApp* app) {
    uint8_t hdr[12];
    if(stream_read(app->file_stream, hdr, 12) != 12) return false;
    if(memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return false;

    while(true) {
        uint8_t chunk_hdr[8];
        if(stream_read(app->file_stream, chunk_hdr, 8) != 8) return false;
        uint32_t chunk_size = chunk_hdr[4] | (chunk_hdr[5] << 8) | (chunk_hdr[6] << 16) |
                               (chunk_hdr[7] << 24);
        if(memcmp(chunk_hdr, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if(stream_read(app->file_stream, fmt, 16) != 16) return false;
            app->channels = fmt[2] | (fmt[3] << 8);
            app->sample_rate = fmt[4] | ((uint32_t)fmt[5] << 8) | ((uint32_t)fmt[6] << 16) |
                                ((uint32_t)fmt[7] << 24);
            app->bits_per_sample = fmt[14] | (fmt[15] << 8);
            if(chunk_size > 16) {
                stream_seek(app->file_stream, chunk_size - 16, StreamOffsetFromCurrent);
            }
        } else if(memcmp(chunk_hdr, "data", 4) == 0) {
            app->data_start = stream_tell(app->file_stream);
            app->data_end = app->data_start + chunk_size;
            return app->channels > 0 && app->sample_rate > 0 &&
                   (app->bits_per_sample == 8 || app->bits_per_sample == 16);
        } else {
            stream_seek(app->file_stream, chunk_size, StreamOffsetFromCurrent);
        }
    }
}

static void nap_fill_from_wav(NetAudioPlayerApp* app, uint8_t* out, size_t count) {
    for(size_t i = 0; i < count; i++) {
        int32_t sample = 0;
        if(app->bits_per_sample == 16) {
            uint8_t bytes[4] = {0};
            size_t need = app->channels == 2 ? 4 : 2;
            size_t got = stream_read(app->file_stream, bytes, need);
            if(got < need) {
                app->eof = true;
            } else {
                int16_t left = (int16_t)(bytes[0] | (bytes[1] << 8));
                if(app->channels == 2) {
                    int16_t right = (int16_t)(bytes[2] | (bytes[3] << 8));
                    sample = ((int32_t)left + (int32_t)right) / 2;
                } else {
                    sample = left;
                }
            }
        } else {
            uint8_t bytes[2] = {0};
            size_t need = app->channels == 2 ? 2 : 1;
            size_t got = stream_read(app->file_stream, bytes, need);
            if(got < need) {
                app->eof = true;
            } else {
                int32_t s8 = app->channels == 2 ? ((int32_t)bytes[0] + bytes[1]) / 2 : bytes[0];
                sample = (s8 - 128) * 256;
            }
        }
        out[i] = nap_convert_sample(app, (int16_t)sample);
    }
}

// ---- MP3 path ----

static void nap_mp3_refill_input(NetAudioPlayerApp* app) {
    size_t remaining = app->mp3_input_len - app->mp3_input_pos;
    if(app->mp3_input_pos > 0) {
        memmove(app->mp3_input_buf, app->mp3_input_buf + app->mp3_input_pos, remaining);
        app->mp3_input_len = remaining;
        app->mp3_input_pos = 0;
    }
    size_t space = sizeof(app->mp3_input_buf) - app->mp3_input_len;
    size_t to_read = space < NAP_MP3_INPUT_CHUNK ? space : NAP_MP3_INPUT_CHUNK;
    if(to_read > 0) {
        size_t got = stream_read(app->file_stream, app->mp3_input_buf + app->mp3_input_len, to_read);
        app->mp3_input_len += got;
        if(got < to_read) app->source_eof = true;
    }
}

// Decodes exactly one MP3 frame's worth of samples into the backlog.
// Returns false if no progress could be made (truly out of data).
static bool nap_mp3_decode_one(NetAudioPlayerApp* app) {
    if(app->mp3_input_len - app->mp3_input_pos < NAP_MP3_INPUT_CHUNK / 2 && !app->source_eof) {
        nap_mp3_refill_input(app);
    }
    if(app->mp3_input_len - app->mp3_input_pos == 0) return false;

    mp3dec_frame_info_t info;
    int16_t temp_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int samples = mp3dec_decode_frame(
        &app->mp3d,
        app->mp3_input_buf + app->mp3_input_pos,
        (int)(app->mp3_input_len - app->mp3_input_pos),
        temp_pcm,
        &info);

    if(info.frame_bytes <= 0) {
        // No sync found in what we have - pull more and let the caller retry.
        nap_mp3_refill_input(app);
        return app->mp3_input_len - app->mp3_input_pos > 0;
    }
    app->mp3_input_pos += (size_t)info.frame_bytes;

    if(samples > 0) {
        if(app->sample_rate == 0) app->sample_rate = (uint32_t)info.hz;
        for(int i = 0; i < samples && app->pcm_backlog_len < NAP_PCM_BACKLOG_CAPACITY; i++) {
            int16_t mono = info.channels == 2 ?
                               (int16_t)(((int32_t)temp_pcm[i * 2] + temp_pcm[i * 2 + 1]) / 2) :
                               temp_pcm[i];
            app->pcm_backlog[app->pcm_backlog_len++] = mono;
        }
    }
    return true;
}

static bool nap_mp3_preroll(NetAudioPlayerApp* app) {
    for(int attempts = 0; attempts < 64; attempts++) {
        if(!nap_mp3_decode_one(app)) return false;
        if(app->pcm_backlog_len > 0 && app->sample_rate > 0) return true;
        if(app->source_eof && app->mp3_input_len - app->mp3_input_pos == 0) return false;
    }
    return false;
}

static void nap_fill_from_mp3(NetAudioPlayerApp* app, uint8_t* out, size_t count) {
    size_t produced = 0;
    while(produced < count) {
        if(app->pcm_backlog_len == 0) {
            if(!nap_mp3_decode_one(app)) {
                app->eof = true;
                out[produced++] = nap_convert_sample(app, 0);
                continue;
            }
        } else {
            out[produced++] = nap_convert_sample(app, app->pcm_backlog[0]);
            memmove(
                app->pcm_backlog,
                app->pcm_backlog + 1,
                (app->pcm_backlog_len - 1) * sizeof(int16_t));
            app->pcm_backlog_len--;
        }
    }
}

static void nap_fill(NetAudioPlayerApp* app, size_t index) {
    uint8_t* out = &app->sample_buffer[index];
    if(app->format == NapFormatWav) {
        nap_fill_from_wav(app, out, app->samples_count_half);
    } else {
        nap_fill_from_mp3(app, out, app->samples_count_half);
    }
}

// ---- DMA ISR ----

static void nap_dma_isr(void* ctx) {
    FuriMessageQueue* queue = ctx;
    if(LL_DMA_IsActiveFlag_HT1(DMA1)) {
        LL_DMA_ClearFlag_HT1(DMA1);
        NapEvent event = {.type = NapEventHalfTransfer};
        furi_message_queue_put(queue, &event, 0);
    }
    if(LL_DMA_IsActiveFlag_TC1(DMA1)) {
        LL_DMA_ClearFlag_TC1(DMA1);
        NapEvent event = {.type = NapEventFullTransfer};
        furi_message_queue_put(queue, &event, 0);
    }
}

// ---- player view ----

static void nap_player_draw_callback(Canvas* canvas, void* ctx) {
    NetAudioPlayerApp* app = ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 12, AlignCenter, AlignCenter, "Net Audio Player");

    canvas_set_font(canvas, FontSecondary);
    if(app->state == NapStateDownloading) {
        canvas_draw_str_aligned(
            canvas, 64, 30, AlignCenter, AlignCenter, furi_string_get_cstr(app->status_text));
        canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignCenter, "Back to cancel");
    } else if(app->state == NapStateError) {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "Error");
        canvas_draw_str_aligned(
            canvas, 64, 38, AlignCenter, AlignCenter, furi_string_get_cstr(app->status_text));
        canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignCenter, "Back to exit");
    } else if(app->state == NapStatePlaying) {
        canvas_draw_str_aligned(
            canvas, 64, 26, AlignCenter, AlignCenter, app->eof ? "Finished" : "Playing...");
        char volume_line[32];
        snprintf(volume_line, sizeof(volume_line), "Volume: %d%%  Up/Down", (int)(app->volume * 100.0f));
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, volume_line);
        canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignCenter, "Back to exit");
    }
}

static void nap_start_download(NetAudioPlayerApp* app);

static bool nap_player_input_callback(InputEvent* input, void* ctx) {
    NetAudioPlayerApp* app = ctx;
    NapEvent event;

    if(app->state != NapStatePlaying) return false;
    if((input->type != InputTypeShort) && (input->type != InputTypeRepeat)) return false;

    if(input->key == InputKeyUp) {
        event.type = NapEventCtrlVolUp;
        furi_message_queue_put(app->queue, &event, FuriWaitForever);
        return true;
    } else if(input->key == InputKeyDown) {
        event.type = NapEventCtrlVolDn;
        furi_message_queue_put(app->queue, &event, FuriWaitForever);
        return true;
    }
    return false;
}

static void nap_back_callback(void* ctx) {
    FuriMessageQueue* queue = ctx;
    NapEvent event = {.type = NapEventCtrlBack};
    furi_message_queue_put(queue, &event, FuriWaitForever);
}

// ---- download + text input ----

static void nap_text_input_result(void* ctx) {
    NetAudioPlayerApp* app = ctx;
    if(strlen(app->url_buffer) == 0) return;
    nap_start_download(app);
}

static void nap_start_download(NetAudioPlayerApp* app) {
    if(!app->fhttp) {
        app->state = NapStateError;
        furi_string_set(app->status_text, "WiFi Devboard not detected");
        view_holder_set_view(app->view_holder, app->player_view);
        return;
    }
    app->state = NapStateDownloading;
    furi_string_set(app->status_text, "Connecting...");
    view_holder_set_view(app->view_holder, app->player_view);

    storage_common_mkdir(app->storage, NAP_DOWNLOAD_DIR);
    if(storage_file_exists(app->storage, NAP_DOWNLOAD_PATH)) {
        storage_simply_remove_recursive(app->storage, NAP_DOWNLOAD_PATH);
    }

    strncpy(app->fhttp->file_path, NAP_DOWNLOAD_PATH, sizeof(app->fhttp->file_path) - 1);
    if(!flipper_http_request(app->fhttp, BYTES, app->url_buffer, "{}", NULL)) {
        app->state = NapStateError;
        furi_string_set(app->status_text, "Request failed to start");
    }
}

static bool nap_open_and_detect_format(NetAudioPlayerApp* app) {
    app->file_stream = file_stream_alloc(app->storage);
    if(!file_stream_open(app->file_stream, NAP_DOWNLOAD_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        return false;
    }

    uint8_t magic[4];
    if(stream_read(app->file_stream, magic, 4) != 4) return false;
    stream_seek(app->file_stream, 0, StreamOffsetFromStart);

    if(memcmp(magic, "RIFF", 4) == 0) {
        app->format = NapFormatWav;
        return nap_parse_wav(app);
    }

    app->format = NapFormatMp3;
    mp3dec_init(&app->mp3d);
    return nap_mp3_preroll(app);
}

static void nap_begin_playback(NetAudioPlayerApp* app) {
    app->samples_count_half = 1024 * 4;
    app->samples_count = app->samples_count_half * 2;
    app->sample_buffer = malloc(app->samples_count);
    app->volume = 1.0f;
    app->eof = false;

    nap_fill(app, 0);
    nap_fill(app, app->samples_count_half);

    if(!furi_hal_speaker_acquire(1000)) {
        app->state = NapStateError;
        furi_string_set(app->status_text, "Speaker busy");
        free(app->sample_buffer);
        app->sample_buffer = NULL;
        return;
    }

    net_audio_hal_init(app->sample_rate);
    net_audio_hal_dma_init((uint32_t)app->sample_buffer, app->samples_count);
    net_audio_hal_set_dma_isr(nap_dma_isr, app->queue);
    net_audio_hal_dma_start();
    net_audio_hal_start();

    app->state = NapStatePlaying;
    app->playing = true;
}

// ---- app lifecycle ----

static NetAudioPlayerApp* nap_alloc(void) {
    NetAudioPlayerApp* app = malloc(sizeof(NetAudioPlayerApp));
    memset(app, 0, sizeof(NetAudioPlayerApp));

    app->storage = furi_record_open(RECORD_STORAGE);
    app->status_text = furi_string_alloc();
    app->queue = furi_message_queue_alloc(8, sizeof(NapEvent));
    app->volume = 1.0f;

    app->fhttp = flipper_http_alloc();

    app->gui = furi_record_open(RECORD_GUI);
    app->view_holder = view_holder_alloc();
    view_holder_attach_to_gui(app->view_holder, app->gui);
    view_holder_set_back_callback(app->view_holder, nap_back_callback, app->queue);

    app->text_input = text_input_alloc();
    text_input_set_header_text(app->text_input, "Enter audio URL (.wav/.mp3)");
    text_input_set_result_callback(
        app->text_input, nap_text_input_result, app, app->url_buffer, sizeof(app->url_buffer), true);

    app->player_view = view_alloc();
    view_set_context(app->player_view, app);
    view_set_draw_callback(app->player_view, nap_player_draw_callback);
    view_set_input_callback(app->player_view, nap_player_input_callback);

    if(!app->fhttp) {
        // flipper_http_alloc() returns NULL when the WiFi Devboard/UART
        // isn't ready. Every download path below dereferences app->fhttp,
        // so show an error instead of crashing the moment a URL is entered.
        app->state = NapStateError;
        furi_string_set(app->status_text, "WiFi Devboard not detected");
        view_holder_set_view(app->view_holder, app->player_view);
    } else {
        app->state = NapStateEnterUrl;
        view_holder_set_view(app->view_holder, text_input_get_view(app->text_input));
    }
    view_holder_send_to_front(app->view_holder);

    app->notification = furi_record_open(RECORD_NOTIFICATION);

    return app;
}

static void nap_free(NetAudioPlayerApp* app) {
    if(app->playing) {
        net_audio_hal_stop();
        net_audio_hal_dma_stop();
        net_audio_hal_set_dma_isr(NULL, NULL);
        net_audio_hal_deinit();
        furi_hal_speaker_release();
    }
    if(app->sample_buffer) free(app->sample_buffer);
    if(app->file_stream) stream_free(app->file_stream);

    furi_record_close(RECORD_NOTIFICATION);

    view_holder_set_view(app->view_holder, NULL);
    view_free(app->player_view);
    text_input_free(app->text_input);
    view_holder_free(app->view_holder);
    furi_record_close(RECORD_GUI);

    flipper_http_free(app->fhttp);

    furi_message_queue_free(app->queue);
    furi_string_free(app->status_text);
    furi_record_close(RECORD_STORAGE);
    free(app);
}

int32_t net_audio_player_app(void* p) {
    UNUSED(p);
    NetAudioPlayerApp* app = nap_alloc();

    bool running = true;
    while(running) {
        if(app->state == NapStateDownloading) {
            if(app->fhttp->state == IDLE) {
                if(nap_open_and_detect_format(app)) {
                    nap_begin_playback(app);
                } else {
                    app->state = NapStateError;
                    furi_string_set(app->status_text, "Unsupported/corrupt file");
                }
                view_holder_send_to_front(app->view_holder);
            } else if(app->fhttp->state == ISSUE) {
                app->state = NapStateError;
                furi_string_set(app->status_text, "Download failed");
                view_holder_send_to_front(app->view_holder);
            } else {
                furi_string_printf(
                    app->status_text, "Downloading... %zu B", app->fhttp->bytes_received);
            }
            view_holder_send_to_front(app->view_holder);
        }

        NapEvent event;
        FuriStatus status = furi_message_queue_get(app->queue, &event, furi_ms_to_ticks(100));
        if(status != FuriStatusOk) continue;

        switch(event.type) {
        case NapEventHalfTransfer:
            if(app->state == NapStatePlaying) nap_fill(app, 0);
            view_holder_send_to_front(app->view_holder);
            break;
        case NapEventFullTransfer:
            if(app->state == NapStatePlaying) nap_fill(app, app->samples_count_half);
            view_holder_send_to_front(app->view_holder);
            break;
        case NapEventCtrlVolUp:
            if(app->volume < 9.9f) app->volume += 0.2f;
            break;
        case NapEventCtrlVolDn:
            if(app->volume > 0.05f) app->volume -= 0.2f;
            break;
        case NapEventCtrlBack:
            running = false;
            break;
        }
    }

    nap_free(app);
    return 0;
}
