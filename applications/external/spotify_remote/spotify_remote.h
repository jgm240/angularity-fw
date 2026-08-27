#pragma once
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_holder.h>
#include <gui/modules/text_input.h>
#include <storage/storage.h>

#include <flipper_http/flipper_http.h>

typedef enum {
    SpotifyStateSetupClientId,
    SpotifyStateSetupClientSecret,
    SpotifyStateSetupRefreshToken,
    SpotifyStateAuthorizing,
    SpotifyStateRemote,
    SpotifyStateError,
} SpotifyState;

typedef enum {
    SpotifyPendingNone,
    SpotifyPendingTokenRefresh,
    SpotifyPendingNowPlaying,
    SpotifyPendingPlay,
    SpotifyPendingPause,
    SpotifyPendingNext,
    SpotifyPendingPrev,
    SpotifyPendingVolume,
} SpotifyPendingAction;

#define SPOTIFY_CRED_MAX 200

typedef struct {
    Gui* gui;
    ViewHolder* view_holder;
    TextInput* text_input;
    View* remote_view;

    char client_id[SPOTIFY_CRED_MAX];
    char client_secret[SPOTIFY_CRED_MAX];
    char refresh_token[SPOTIFY_CRED_MAX];
    char access_token[SPOTIFY_CRED_MAX];

    FuriString* track_name;
    FuriString* artist_name;
    FuriString* status_text;
    bool is_playing;
    bool have_track;

    FlipperHTTP* fhttp;
    Storage* storage;

    SpotifyState state;
    SpotifyPendingAction pending_action;
    bool refresh_then_retry; // true when a token refresh was triggered mid-action
    SpotifyPendingAction action_after_refresh;
    int volume_percent;

    FuriMessageQueue* queue;
    FuriTimer* poll_timer;
    FuriTimer* redraw_timer;
} SpotifyRemoteApp;
