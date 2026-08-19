#include "../momentum_app.h"

#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>

#define NFC_ENROLL_SCAN_TIMEOUT_MS 5000
#define NFC_ENROLL_SCAN_FLAG_COMPLETE (1 << 0)
#define NFC_ENROLL_UID_MAX_LEN 10

enum VarItemListIndex {
    VarItemListIndexEnabled,
    VarItemListIndexScan,
};

typedef struct {
    FuriThreadId thread_id;
    NfcPoller* poller;
    bool detected;
    Iso14443_3aData data;
} NfcEnrollScanContext;

static NfcCommand momentum_app_nfc_enroll_scan_callback(NfcGenericEvent event, void* context) {
    NfcEnrollScanContext* ctx = context;
    NfcCommand command = NfcCommandContinue;

    if(event.protocol == NfcProtocolIso14443_3a) {
        const Iso14443_3aPollerEvent* poller_event = event.event_data;
        if(poller_event->type == Iso14443_3aPollerEventTypeReady) {
            const NfcDeviceData* device_data = nfc_poller_get_data(ctx->poller);
            if(device_data) {
                iso14443_3a_copy(&ctx->data, (const Iso14443_3aData*)device_data);
                ctx->detected = true;
            }
            furi_thread_flags_set(ctx->thread_id, NFC_ENROLL_SCAN_FLAG_COMPLETE);
            command = NfcCommandStop;
        } else if(poller_event->type == Iso14443_3aPollerEventTypeError) {
            furi_thread_flags_set(ctx->thread_id, NFC_ENROLL_SCAN_FLAG_COMPLETE);
            command = NfcCommandStop;
        }
    }

    return command;
}

// Blocks the calling thread for up to NFC_ENROLL_SCAN_TIMEOUT_MS. Only
// ever called from an explicit "Scan Tag to Enroll" menu selection.
static bool momentum_app_nfc_enroll_scan(uint8_t* uid_out, size_t* uid_len_out) {
    Nfc* nfc = nfc_alloc();
    if(!nfc) {
        return false;
    }

    NfcEnrollScanContext ctx = {0};
    ctx.thread_id = furi_thread_get_current_id();

    NfcPoller* poller = nfc_poller_alloc(nfc, NfcProtocolIso14443_3a);
    ctx.poller = poller;
    nfc_poller_start(poller, momentum_app_nfc_enroll_scan_callback, &ctx);

    furi_thread_flags_wait(
        NFC_ENROLL_SCAN_FLAG_COMPLETE, FuriFlagWaitAny, NFC_ENROLL_SCAN_TIMEOUT_MS);
    furi_thread_flags_clear(NFC_ENROLL_SCAN_FLAG_COMPLETE);

    nfc_poller_stop(poller);
    nfc_poller_free(poller);
    nfc_free(nfc);

    if(!ctx.detected) {
        return false;
    }

    size_t uid_len = 0;
    const uint8_t* uid = iso14443_3a_get_uid(&ctx.data, &uid_len);
    if(!uid || uid_len == 0 || uid_len > NFC_ENROLL_UID_MAX_LEN) {
        return false;
    }

    memcpy(uid_out, uid, uid_len);
    *uid_len_out = uid_len;
    return true;
}

void momentum_app_scene_interface_lockscreen_nfc_var_item_list_callback(
    void* context,
    uint32_t index) {
    MomentumApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void momentum_app_scene_interface_lockscreen_nfc_enabled_changed(VariableItem* item) {
    MomentumApp* app = variable_item_get_context(item);
    bool value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, value ? "ON" : "OFF");
    momentum_settings.nfc_unlock_enabled = value;
    app->save_settings = true;
}

static void momentum_app_scene_interface_lockscreen_nfc_populate(MomentumApp* app) {
    VariableItemList* var_item_list = app->var_item_list;
    VariableItem* item;

    item = variable_item_list_add(
        var_item_list,
        "NFC Unlock",
        2,
        momentum_app_scene_interface_lockscreen_nfc_enabled_changed,
        app);
    variable_item_set_current_value_index(item, momentum_settings.nfc_unlock_enabled);
    variable_item_set_current_value_text(
        item, momentum_settings.nfc_unlock_enabled ? "ON" : "OFF");

    item = variable_item_list_add(var_item_list, "Scan Tag to Enroll", 0, NULL, app);
    variable_item_set_current_value_text(
        item, strlen(momentum_settings.nfc_unlock_uid) ? "Set" : "Empty");

    variable_item_list_set_enter_callback(
        var_item_list, momentum_app_scene_interface_lockscreen_nfc_var_item_list_callback, app);
}

void momentum_app_scene_interface_lockscreen_nfc_on_enter(void* context) {
    MomentumApp* app = context;
    momentum_app_scene_interface_lockscreen_nfc_populate(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, MomentumAppViewVarItemList);
}

bool momentum_app_scene_interface_lockscreen_nfc_on_event(void* context, SceneManagerEvent event) {
    MomentumApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == VarItemListIndexScan) {
            popup_set_header(app->popup, "Scanning...", 64, 20, AlignCenter, AlignCenter);
            popup_set_text(
                app->popup, "Hold tag near Flipper", 64, 36, AlignCenter, AlignCenter);
            popup_set_callback(app->popup, NULL);
            popup_set_context(app->popup, NULL);
            popup_disable_timeout(app->popup);
            view_dispatcher_switch_to_view(app->view_dispatcher, MomentumAppViewPopup);

            uint8_t uid[NFC_ENROLL_UID_MAX_LEN];
            size_t uid_len = 0;
            bool scanned = momentum_app_nfc_enroll_scan(uid, &uid_len);

            if(scanned) {
                for(size_t i = 0; i < uid_len; i++) {
                    snprintf(
                        momentum_settings.nfc_unlock_uid + i * 2, 3, "%02X", uid[i]);
                }
                app->save_settings = true;
                popup_set_header(app->popup, "Tag Saved", 64, 26, AlignCenter, AlignCenter);
                popup_set_text(app->popup, "", 64, 40, AlignCenter, AlignCenter);
            } else {
                popup_set_header(app->popup, "No Tag Found", 64, 26, AlignCenter, AlignCenter);
                popup_set_text(app->popup, "Try again", 64, 40, AlignCenter, AlignCenter);
            }
            furi_delay_ms(1200);

            popup_reset(app->popup);
            variable_item_list_reset(app->var_item_list);
            momentum_app_scene_interface_lockscreen_nfc_populate(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, MomentumAppViewVarItemList);
        }
    }

    return consumed;
}

void momentum_app_scene_interface_lockscreen_nfc_on_exit(void* context) {
    MomentumApp* app = context;
    variable_item_list_reset(app->var_item_list);
    popup_reset(app->popup);
}
