#pragma once
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_holder.h>
#include <gui/modules/text_input.h>
#include <storage/storage.h>

#include <flipper_http/flipper_http.h>

typedef enum {
    DolphinStateUrlInput,
    DolphinStatePriming,
    DolphinStateStreaming,
    DolphinStateError,
} DolphinState;

typedef enum {
    DolphinFetchNone,
    DolphinFetchPrimeA,
    DolphinFetchPrimeB,
    DolphinFetchRefillA,
    DolphinFetchRefillB,
} DolphinFetchTarget;

#define DOLPHIN_SAMPLE_RATE 8000
#define DOLPHIN_URL_MAX     128

typedef struct {
    Gui* gui;
    ViewHolder* view_holder;
    TextInput* text_input;
    View* status_view;

    char server_url[DOLPHIN_URL_MAX];
    char chunk_url[DOLPHIN_URL_MAX + 16];
    FuriString* status_text;

    FlipperHTTP* fhttp;
    Storage* storage;

    uint8_t* sample_buffer;
    size_t samples_count_half;
    size_t samples_count;
    float volume;
    bool streaming;

    DolphinState state;
    DolphinFetchTarget pending_fetch;
    FuriMessageQueue* queue;
} DolphinMusicApp;
