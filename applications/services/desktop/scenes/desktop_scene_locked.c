#include <furi.h>
#include <furi_hal.h>
#include <gui/scene_manager.h>
#include <gui/view_stack.h>
#include <stdint.h>
#include <toolbox/run_parallel.h>

#include "../desktop.h"
#include "../desktop_i.h"
#include "../helpers/pin_code.h"
#include "../animations/animation_manager.h"
#include "../views/desktop_events.h"
#include "../views/desktop_view_locked.h"
#include "desktop_scene.h"
#include "desktop_scene_locked.h"

#include <momentum/settings.h>
#include <momentum/security_log.h>
#include <shortcuts/shortcuts.h>

#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>

#define WRONG_PIN_HEADER_TIMEOUT 3000
#define INPUT_PIN_VIEW_TIMEOUT   15000

#define NFC_UNLOCK_SCAN_TIMEOUT_MS 5000
#define NFC_UNLOCK_SCAN_FLAG_COMPLETE (1 << 0)
#define NFC_UNLOCK_UID_MAX_LEN 10

typedef struct {
    FuriThreadId thread_id;
    NfcPoller* poller;
    bool detected;
    Iso14443_3aData data;
} NfcUnlockScanContext;

static NfcCommand desktop_nfc_unlock_scan_callback(NfcGenericEvent event, void* context) {
    NfcUnlockScanContext* ctx = context;
    NfcCommand command = NfcCommandContinue;

    if(event.protocol == NfcProtocolIso14443_3a) {
        const Iso14443_3aPollerEvent* poller_event = event.event_data;
        if(poller_event->type == Iso14443_3aPollerEventTypeReady) {
            const NfcDeviceData* device_data = nfc_poller_get_data(ctx->poller);
            if(device_data) {
                iso14443_3a_copy(&ctx->data, (const Iso14443_3aData*)device_data);
                ctx->detected = true;
            }
            furi_thread_flags_set(ctx->thread_id, NFC_UNLOCK_SCAN_FLAG_COMPLETE);
            command = NfcCommandStop;
        } else if(poller_event->type == Iso14443_3aPollerEventTypeError) {
            furi_thread_flags_set(ctx->thread_id, NFC_UNLOCK_SCAN_FLAG_COMPLETE);
            command = NfcCommandStop;
        }
    }

    return command;
}

// Blocks the calling thread for up to NFC_UNLOCK_SCAN_TIMEOUT_MS waiting
// for an ISO14443-3A tag. Returns true and fills uid_out/uid_len_out on
// success. Only ever called from an explicit, user-initiated lock-screen
// action (OK press with NFC unlock enabled) - never runs unattended.
static bool desktop_nfc_unlock_scan(uint8_t* uid_out, size_t* uid_len_out) {
    Nfc* nfc = nfc_alloc();
    if(!nfc) {
        return false;
    }

    NfcUnlockScanContext ctx = {0};
    ctx.thread_id = furi_thread_get_current_id();

    NfcPoller* poller = nfc_poller_alloc(nfc, NfcProtocolIso14443_3a);
    ctx.poller = poller;
    nfc_poller_start(poller, desktop_nfc_unlock_scan_callback, &ctx);

    furi_thread_flags_wait(
        NFC_UNLOCK_SCAN_FLAG_COMPLETE, FuriFlagWaitAny, NFC_UNLOCK_SCAN_TIMEOUT_MS);
    furi_thread_flags_clear(NFC_UNLOCK_SCAN_FLAG_COMPLETE);

    nfc_poller_stop(poller);
    nfc_poller_free(poller);
    nfc_free(nfc);

    if(!ctx.detected) {
        return false;
    }

    size_t uid_len = 0;
    const uint8_t* uid = iso14443_3a_get_uid(&ctx.data, &uid_len);
    if(!uid || uid_len == 0 || uid_len > NFC_UNLOCK_UID_MAX_LEN) {
        return false;
    }

    memcpy(uid_out, uid, uid_len);
    *uid_len_out = uid_len;
    return true;
}

static bool desktop_nfc_unlock_uid_matches_setting(const uint8_t* uid, size_t uid_len) {
    // Stored setting is an uppercase hex string, e.g. "04A1B2C3"
    if(uid_len == 0 || strlen(momentum_settings.nfc_unlock_uid) != uid_len * 2) {
        return false;
    }

    char uid_hex[NFC_UNLOCK_UID_MAX_LEN * 2 + 1];
    for(size_t i = 0; i < uid_len; i++) {
        snprintf(uid_hex + i * 2, 3, "%02X", uid[i]);
    }

    return strcmp(uid_hex, momentum_settings.nfc_unlock_uid) == 0;
}

static void desktop_scene_locked_callback(DesktopEvent event, void* context) {
    Desktop* desktop = (Desktop*)context;
    view_dispatcher_send_custom_event(desktop->view_dispatcher, event);
}

static void desktop_scene_locked_new_idle_animation_callback(void* context) {
    furi_assert(context);
    Desktop* desktop = context;
    view_dispatcher_send_custom_event(
        desktop->view_dispatcher, DesktopAnimationEventNewIdleAnimation);
}

void desktop_scene_locked_on_enter(void* context) {
    Desktop* desktop = (Desktop*)context;

    // callbacks for 1-st layer
    animation_manager_set_new_idle_callback(
        desktop->animation_manager, desktop_scene_locked_new_idle_animation_callback);
    animation_manager_set_check_callback(desktop->animation_manager, NULL);
    animation_manager_set_interact_callback(desktop->animation_manager, NULL);

    // callbacks for 2-nd layer
    desktop_view_locked_set_callback(desktop->locked_view, desktop_scene_locked_callback, desktop);

    bool switch_to_timeout_scene = false;
    uint32_t state = scene_manager_get_scene_state(desktop->scene_manager, DesktopSceneLocked);
    if(state == DesktopSceneLockedStateFirstEnter) {
        view_port_enabled_set(desktop->lock_icon_viewport, true);
        Gui* gui = furi_record_open(RECORD_GUI);
        gui_set_lockdown(gui, true);
        furi_record_close(RECORD_GUI);

        if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagLock)) {
            desktop_view_locked_lock(desktop->locked_view, true);
            uint32_t pin_timeout = desktop_pin_lock_get_fail_timeout();
            if(pin_timeout > 0) {
                scene_manager_set_scene_state(
                    desktop->scene_manager, DesktopScenePinTimeout, pin_timeout);
                switch_to_timeout_scene = true;
            } else {
                desktop_view_locked_close_cover(desktop->locked_view);
            }
        } else {
            desktop_view_locked_lock(desktop->locked_view, false);
            desktop_view_locked_close_cover(desktop->locked_view);
        }
        scene_manager_set_scene_state(
            desktop->scene_manager, DesktopSceneLocked, DesktopSceneLockedStateRepeatEnter);
    }

    if(switch_to_timeout_scene) {
        scene_manager_next_scene(desktop->scene_manager, DesktopScenePinTimeout);
    } else {
        view_dispatcher_switch_to_view(desktop->view_dispatcher, DesktopViewIdLocked);
    }
}

bool desktop_scene_locked_on_event(void* context, SceneManagerEvent event) {
    Desktop* desktop = (Desktop*)context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case DesktopLockedEventOpenPowerOff: {
            if(momentum_settings.lockscreen_poweroff) {
                // Workaround for shutdown when app can't be opened
                run_parallel(desktop_shutdown, desktop, 512);
            }
            consumed = true;
            break;
        }
        case DesktopLockedEventUnlocked:
        case DesktopGlobalApiUnlock:
            desktop_unlock(desktop);
            consumed = true;
            break;
        case DesktopLockedEventCoversClosed:
            notification_message(desktop->notification, &sequence_display_backlight_off);
            consumed = true;
            break;
        case DesktopLockedEventUpdate:
            if(desktop_view_locked_is_locked_hint_visible(desktop->locked_view)) {
                notification_message(desktop->notification, &sequence_display_backlight_off);
            }
            desktop_view_locked_update(desktop->locked_view);
            consumed = true;
            break;
        case DesktopLockedEventShowPinInput:
            scene_manager_next_scene(desktop->scene_manager, DesktopScenePinInput);
            consumed = true;
            break;
        case DesktopLockedEventNfcScan: {
            uint8_t uid[NFC_UNLOCK_UID_MAX_LEN];
            size_t uid_len = 0;
            if(desktop_nfc_unlock_scan(uid, &uid_len)) {
                Shortcuts* shortcuts = furi_record_open(RECORD_SHORTCUTS);
                shortcuts_notify_nfc_scanned(shortcuts);
                furi_record_close(RECORD_SHORTCUTS);

                if(desktop_nfc_unlock_uid_matches_setting(uid, uid_len)) {
                    desktop_unlock(desktop);
                } else {
                    security_log_add("NFC unlock: wrong tag scanned");
                    desktop_play_error_sound();
                }
            } else {
                desktop_play_error_sound();
            }
            consumed = true;
            break;
        }
        case DesktopLockedEventFindMyFlipper:
            security_log_add("Find My Flipper activated");
            notification_message(desktop->notification, &sequence_find_my_flipper);
            consumed = true;
            break;
        case DesktopAnimationEventNewIdleAnimation:
            animation_manager_new_idle_process(desktop->animation_manager);
            consumed = true;
            break;
        }
    }

    return consumed;
}

void desktop_scene_locked_on_exit(void* context) {
    UNUSED(context);
}
