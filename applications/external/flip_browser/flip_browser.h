#pragma once
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_holder.h>
#include <gui/modules/text_input.h>
#include <gui/modules/text_box.h>
#include <storage/storage.h>

#include <flipper_http/flipper_http.h>

typedef enum {
    BrowserStateUrlInput,
    BrowserStateLoading,
    BrowserStatePage,
    BrowserStateError,
} BrowserState;

#define BROWSER_URL_MAX  128
#define BROWSER_MAX_TEXT 4000 // TextBox display cap, not a hard page-size limit

typedef struct {
    Gui* gui;
    ViewHolder* view_holder;
    TextInput* text_input;
    TextBox* text_box;
    View* status_view;

    char url[BROWSER_URL_MAX];
    FuriString* status_text;
    FuriString* page_text;

    FlipperHTTP* fhttp;
    Storage* storage;

    BrowserState state;
    FuriMessageQueue* queue;
} FlipBrowserApp;
