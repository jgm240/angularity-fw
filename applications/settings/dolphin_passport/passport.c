#include <furi.h>
#include <furi_hal_version.h>

#include <gui/gui.h>
#include <dolphin/dolphin.h>
#include <dolphin/helpers/dolphin_state.h>

#include <assets_icons.h>

#include <momentum/momentum.h>
#include <momentum/game_stats.h>

#include <nfc/nfc.h>
#include <nfc/nfc_listener.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>

// "Tap two Flippers together" stat compare. Reuses the exact NFC Unlock
// scan pattern (poller + callback + bounded thread_flags_wait) for
// reading, and the plain nfc_listener emulation used by NFC test tooling
// for broadcasting - no NDEF writer support exists in this codebase, so
// the two stats are packed directly into a 7-byte ISO14443-3A UID instead.
#define PASSPORT_NFC_MAGIC              0xA9
#define PASSPORT_NFC_UID_LEN            7
#define PASSPORT_NFC_SCAN_TIMEOUT_MS    5000
#define PASSPORT_NFC_SCAN_FLAG_COMPLETE (1 << 0)

typedef enum {
    PassportNfcModeNone,
    PassportNfcModeBroadcasting,
    PassportNfcModeReading,
    PassportNfcModeResult,
    PassportNfcModeNotFound,
} PassportNfcMode;

typedef struct {
    FuriSemaphore* semaphore;
    DolphinStats* stats;
    ViewPort* view_port;
    bool progress_total;

    PassportNfcMode nfc_mode;
    FuriThread* nfc_thread;
    volatile bool nfc_cancel;
    uint32_t their_streak;
    uint32_t their_games;
} PassportContext;

static void
    passport_encode_uid(uint8_t* uid, uint32_t streak_days, uint32_t games_played) {
    uid[0] = PASSPORT_NFC_MAGIC;
    uid[1] = (uint8_t)((streak_days >> 8) & 0xFF);
    uid[2] = (uint8_t)(streak_days & 0xFF);
    uid[3] = (uint8_t)games_played;
    uid[4] = 0;
    uid[5] = 0;
    uid[6] = 0;
}

static bool passport_decode_uid(
    const uint8_t* uid,
    size_t uid_len,
    uint32_t* streak_days,
    uint32_t* games_played) {
    if(uid_len != PASSPORT_NFC_UID_LEN || uid[0] != PASSPORT_NFC_MAGIC) {
        return false;
    }
    *streak_days = ((uint32_t)uid[1] << 8) | uid[2];
    *games_played = uid[3];
    return true;
}

static int32_t passport_nfc_broadcast_thread(void* context) {
    PassportContext* ctx = context;

    Nfc* nfc = nfc_alloc();
    if(!nfc) {
        return 0;
    }

    Iso14443_3aData data = {0};
    data.uid_len = PASSPORT_NFC_UID_LEN;
    passport_encode_uid(
        data.uid, momentum_settings.security_streak_days, game_stats_get_distinct_played());
    data.atqa[0] = 0x44;
    data.atqa[1] = 0x00;
    data.sak = 0x00;

    NfcListener* listener = nfc_listener_alloc(nfc, NfcProtocolIso14443_3a, &data);
    nfc_listener_start(listener, NULL, NULL);

    while(!ctx->nfc_cancel) {
        furi_delay_ms(100);
    }

    nfc_listener_stop(listener);
    nfc_listener_free(listener);
    nfc_free(nfc);
    return 0;
}

typedef struct {
    NfcPoller* poller;
    FuriThreadId thread_id;
    bool detected;
    Iso14443_3aData data;
} PassportNfcReadContext;

static NfcCommand passport_nfc_read_callback(NfcGenericEvent event, void* context) {
    PassportNfcReadContext* read_ctx = context;
    NfcCommand command = NfcCommandContinue;

    if(event.protocol == NfcProtocolIso14443_3a) {
        const Iso14443_3aPollerEvent* poller_event = event.event_data;
        if(poller_event->type == Iso14443_3aPollerEventTypeReady) {
            const NfcDeviceData* device_data = nfc_poller_get_data(read_ctx->poller);
            if(device_data) {
                iso14443_3a_copy(&read_ctx->data, (const Iso14443_3aData*)device_data);
                read_ctx->detected = true;
            }
            furi_thread_flags_set(read_ctx->thread_id, PASSPORT_NFC_SCAN_FLAG_COMPLETE);
            command = NfcCommandStop;
        } else if(poller_event->type == Iso14443_3aPollerEventTypeError) {
            furi_thread_flags_set(read_ctx->thread_id, PASSPORT_NFC_SCAN_FLAG_COMPLETE);
            command = NfcCommandStop;
        }
    }

    return command;
}

static int32_t passport_nfc_read_thread(void* context) {
    PassportContext* ctx = context;

    Nfc* nfc = nfc_alloc();
    if(!nfc) {
        ctx->nfc_mode = PassportNfcModeNotFound;
        view_port_update(ctx->view_port);
        return 0;
    }

    PassportNfcReadContext read_ctx = {0};
    read_ctx.thread_id = furi_thread_get_current_id();

    NfcPoller* poller = nfc_poller_alloc(nfc, NfcProtocolIso14443_3a);
    read_ctx.poller = poller;
    nfc_poller_start(poller, passport_nfc_read_callback, &read_ctx);

    furi_thread_flags_wait(
        PASSPORT_NFC_SCAN_FLAG_COMPLETE, FuriFlagWaitAny, PASSPORT_NFC_SCAN_TIMEOUT_MS);
    furi_thread_flags_clear(PASSPORT_NFC_SCAN_FLAG_COMPLETE);

    nfc_poller_stop(poller);
    nfc_poller_free(poller);
    nfc_free(nfc);

    bool ok = false;
    if(read_ctx.detected) {
        size_t uid_len = 0;
        const uint8_t* uid = iso14443_3a_get_uid(&read_ctx.data, &uid_len);
        if(uid) {
            ok = passport_decode_uid(uid, uid_len, &ctx->their_streak, &ctx->their_games);
        }
    }

    ctx->nfc_mode = ok ? PassportNfcModeResult : PassportNfcModeNotFound;
    view_port_update(ctx->view_port);
    return 0;
}

static void passport_nfc_join_and_clear(PassportContext* ctx) {
    if(!ctx->nfc_thread) return;
    furi_thread_join(ctx->nfc_thread);
    furi_thread_free(ctx->nfc_thread);
    ctx->nfc_thread = NULL;
}

static void input_callback(InputEvent* input, void* _ctx) {
    PassportContext* ctx = _ctx;

    if(ctx->nfc_mode == PassportNfcModeBroadcasting) {
        // Bounded (~100ms) block while the worker notices the cancel flag
        if(input->type == InputTypeShort && input->key == InputKeyBack) {
            ctx->nfc_cancel = true;
            passport_nfc_join_and_clear(ctx);
            ctx->nfc_mode = PassportNfcModeNone;
            view_port_update(ctx->view_port);
        }
        return;
    }

    if(ctx->nfc_mode == PassportNfcModeReading) {
        // Worker has its own bounded timeout; let it resolve on its own
        return;
    }

    if(ctx->nfc_mode == PassportNfcModeResult || ctx->nfc_mode == PassportNfcModeNotFound) {
        if(input->type == InputTypeShort) {
            passport_nfc_join_and_clear(ctx);
            ctx->nfc_mode = PassportNfcModeNone;
            view_port_update(ctx->view_port);
        }
        return;
    }

    if((input->type == InputTypeShort) && (input->key == InputKeyOk)) {
        ctx->progress_total = !ctx->progress_total;
        view_port_update(ctx->view_port);
    }

    if((input->type == InputTypeLong) && (input->key == InputKeyLeft)) {
        ctx->nfc_cancel = false;
        ctx->nfc_mode = PassportNfcModeBroadcasting;
        ctx->nfc_thread = furi_thread_alloc_ex(
            "PassportNfcBroadcast", 2048, passport_nfc_broadcast_thread, ctx);
        furi_thread_start(ctx->nfc_thread);
        view_port_update(ctx->view_port);
    }

    if((input->type == InputTypeLong) && (input->key == InputKeyRight)) {
        ctx->nfc_mode = PassportNfcModeReading;
        ctx->nfc_thread =
            furi_thread_alloc_ex("PassportNfcRead", 2048, passport_nfc_read_thread, ctx);
        furi_thread_start(ctx->nfc_thread);
        view_port_update(ctx->view_port);
    }

    if((input->type == InputTypeShort) && (input->key == InputKeyBack)) {
        furi_semaphore_release(ctx->semaphore);
    }
}

static void render_callback(Canvas* canvas, void* _ctx) {
    PassportContext* ctx = _ctx;

    if(ctx->nfc_mode == PassportNfcModeBroadcasting) {
        canvas_draw_str(canvas, 4, 12, "Broadcasting stats...");
        canvas_draw_str(canvas, 4, 26, "Hold near another");
        canvas_draw_str(canvas, 4, 38, "Flipper's Passport");
        canvas_draw_str(canvas, 4, 60, "[Back] Cancel");
        return;
    }

    if(ctx->nfc_mode == PassportNfcModeReading) {
        canvas_draw_str(canvas, 4, 12, "Reading...");
        canvas_draw_str(canvas, 4, 26, "Hold near a Flipper");
        canvas_draw_str(canvas, 4, 38, "showing its stats");
        return;
    }

    if(ctx->nfc_mode == PassportNfcModeNotFound) {
        canvas_draw_str(canvas, 4, 24, "No Flipper found.");
        canvas_draw_str(canvas, 4, 36, "Try again.");
        canvas_draw_str(canvas, 4, 56, "Press any key");
        return;
    }

    if(ctx->nfc_mode == PassportNfcModeResult) {
        char line[32];
        canvas_draw_str(canvas, 4, 8, "Stats Compare");
        snprintf(
            line, sizeof(line), "You:  %lu day streak", momentum_settings.security_streak_days);
        canvas_draw_str(canvas, 4, 22, line);
        snprintf(line, sizeof(line), "Them: %lu day streak", ctx->their_streak);
        canvas_draw_str(canvas, 4, 32, line);
        snprintf(
            line,
            sizeof(line),
            "You:  %u/%u games",
            (unsigned)game_stats_get_distinct_played(),
            (unsigned)GAME_STATS_COUNT);
        canvas_draw_str(canvas, 4, 46, line);
        snprintf(
            line, sizeof(line), "Them: %lu/%u games", ctx->their_games, (unsigned)GAME_STATS_COUNT);
        canvas_draw_str(canvas, 4, 56, line);
        return;
    }

    DolphinStats* stats = ctx->stats;

    char level_str[12];
    char xp_str[12];
    const char* mood_str = NULL;
    const Icon* portrait = NULL;

    if(stats->butthurt <= 4) {
        portrait = &I_passport_happy_46x49;
        mood_str = "Mood: Happy";
    } else if(stats->butthurt <= 9) {
        portrait = &I_passport_okay_46x49;
        mood_str = "Mood: Okay";
    } else {
        portrait = &I_passport_bad_46x49;
        mood_str = "Mood: Angry";
    }

    uint32_t xp_progress = 0;
    uint32_t xp_to_levelup = dolphin_state_xp_to_levelup(stats->icounter);
    uint32_t xp_above_last_levelup = dolphin_state_xp_above_last_levelup(stats->icounter);

    uint32_t xp_have = 0;
    uint32_t xp_target = 0;
    if(ctx->progress_total) {
        xp_have = stats->icounter;
        xp_target = DOLPHIN_LEVELS[DOLPHIN_LEVEL_COUNT - 1];
    } else {
        xp_have = xp_above_last_levelup;
        xp_target = xp_to_levelup + xp_above_last_levelup;
    }

    if(stats->level == DOLPHIN_LEVEL_COUNT + 1) {
        xp_progress = 0;
    } else {
        xp_progress = (xp_target - xp_have) * 64 / xp_target;
    }

    // multipass
    canvas_draw_icon(canvas, 0, 0, &I_passport_128x64);

    // portrait
    furi_assert((stats->level > 0) && (stats->level <= DOLPHIN_LEVEL_COUNT + 1));
    canvas_draw_icon(canvas, 11, 2, portrait);

    const char* my_name = furi_hal_version_get_name_ptr();
    snprintf(level_str, sizeof(level_str), "Level: %hu", stats->level);
    canvas_draw_str(canvas, 59, 10, my_name ? my_name : "Unknown");
    canvas_draw_str(canvas, 59, 22, mood_str);
    canvas_draw_str(canvas, 59, 34, level_str);

    if(stats->level == DOLPHIN_LEVEL_COUNT + 1) {
        snprintf(xp_str, sizeof(xp_str), "Max Level!");
    } else {
        snprintf(xp_str, sizeof(xp_str), "%lu/%lu", xp_have, xp_target);
    }
    canvas_set_font(canvas, FontBatteryPercent);
    canvas_draw_str(canvas, 59, 42, xp_str);
    canvas_set_font(canvas, FontSecondary);

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 123 - xp_progress, 45, xp_progress + (xp_progress > 0), 5);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontKeyboard);

    if(momentum_settings.security_streak_days > 0) {
        char streak_str[40];
        snprintf(
            streak_str,
            sizeof(streak_str),
            "Secured: %lu day%s",
            momentum_settings.security_streak_days,
            momentum_settings.security_streak_days == 1 ? "" : "s");
        canvas_draw_str(canvas, 59, 51, streak_str);
    }

    size_t games_played = game_stats_get_distinct_played();
    if(games_played > 0) {
        char games_str[40];
        if(games_played == GAME_STATS_COUNT) {
            snprintf(games_str, sizeof(games_str), "All Games Played!");
        } else {
            snprintf(
                games_str,
                sizeof(games_str),
                "Games: %u/%u",
                (unsigned)games_played,
                (unsigned)GAME_STATS_COUNT);
        }
        canvas_draw_str(canvas, 59, 58, games_str);
    }

    if(momentum_settings.clean_unlock_streak > 0) {
        char clean_str[40];
        snprintf(
            clean_str, sizeof(clean_str), "Clean: %lu", momentum_settings.clean_unlock_streak);
        canvas_draw_str(canvas, 59, 64, clean_str);
    }

    canvas_set_font(canvas, FontSecondary);
}

int32_t passport_app(void* p) {
    UNUSED(p);
    FuriSemaphore* semaphore = furi_semaphore_alloc(1, 0);
    ViewPort* view_port = view_port_alloc();

    Dolphin* dolphin = furi_record_open(RECORD_DOLPHIN);
    DolphinStats stats = dolphin_stats(dolphin);
    PassportContext* ctx = malloc(sizeof(PassportContext));
    ctx->stats = &stats;
    ctx->view_port = view_port;
    ctx->semaphore = semaphore;
    ctx->progress_total = false;
    ctx->nfc_mode = PassportNfcModeNone;
    ctx->nfc_thread = NULL;
    ctx->nfc_cancel = false;
    ctx->their_streak = 0;
    ctx->their_games = 0;
    furi_record_close(RECORD_DOLPHIN);
    view_port_draw_callback_set(view_port, render_callback, ctx);
    view_port_input_callback_set(view_port, input_callback, ctx);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    view_port_update(view_port);

    furi_check(furi_semaphore_acquire(semaphore, FuriWaitForever) == FuriStatusOk);

    if(ctx->nfc_thread) {
        ctx->nfc_cancel = true;
        passport_nfc_join_and_clear(ctx);
    }

    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_semaphore_free(semaphore);
    free(ctx);

    return 0;
}
