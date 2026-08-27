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
    ClaudeStateApiKeyInput,
    ClaudeStatePromptInput,
    ClaudeStateWaiting,
    ClaudeStateResponse,
    ClaudeStateError,
} ClaudeState;

#define CLAUDE_API_KEY_MAX 200
#define CLAUDE_PROMPT_MAX  256

typedef struct {
    Gui* gui;
    ViewHolder* view_holder;
    TextInput* text_input;
    TextBox* text_box;
    View* status_view;

    char api_key[CLAUDE_API_KEY_MAX];
    char prompt_buffer[CLAUDE_PROMPT_MAX];
    FuriString* status_text;
    FuriString* response_text;

    FlipperHTTP* fhttp;
    Storage* storage;

    ClaudeState state;
    FuriMessageQueue* queue;
} ClaudeChatApp;
