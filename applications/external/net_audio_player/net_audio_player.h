#pragma once
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_holder.h>
#include <gui/modules/text_input.h>
#include <toolbox/stream/stream.h>
#include <toolbox/stream/file_stream.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>

#include <flipper_http/flipper_http.h>

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_STDIO
#include "minimp3.h"

typedef enum {
    NapStateEnterUrl,
    NapStateDownloading,
    NapStatePlaying,
    NapStateError,
} NapState;

typedef enum {
    NapFormatUnknown,
    NapFormatWav,
    NapFormatMp3,
} NapFormat;

#define NAP_PCM_BACKLOG_CAPACITY (MINIMP3_MAX_SAMPLES_PER_FRAME * 4)
#define NAP_MP3_INPUT_CHUNK      4096

typedef struct {
    Gui* gui;
    ViewHolder* view_holder;
    TextInput* text_input;
    View* player_view;
    char url_buffer[256];

    FlipperHTTP* fhttp;
    Storage* storage;
    Stream* file_stream;

    NapState state;
    FuriString* status_text;

    NapFormat format;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t data_start;
    uint32_t data_end;

    mp3dec_t mp3d;
    uint8_t mp3_input_buf[NAP_MP3_INPUT_CHUNK * 2];
    size_t mp3_input_len;
    size_t mp3_input_pos;
    int16_t pcm_backlog[NAP_PCM_BACKLOG_CAPACITY];
    size_t pcm_backlog_len;

    uint8_t* sample_buffer;
    size_t samples_count_half;
    size_t samples_count;

    float volume;
    bool eof;
    bool playing;
    bool source_eof; // no more bytes to read from file/network

    FuriMessageQueue* queue;
    NotificationApp* notification;
} NetAudioPlayerApp;
