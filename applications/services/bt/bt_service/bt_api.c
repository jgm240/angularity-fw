#include "bt_i.h"
#include <profiles/serial_profile.h>
#include <profiles/host_profile.h>

FuriHalBleProfileBase* bt_profile_start(
    Bt* bt,
    const FuriHalBleProfileTemplate* profile_template,
    FuriHalBleProfileParams params) {
    furi_check(bt);

    // Send message
    FuriHalBleProfileBase* profile_instance = NULL;

    BtMessage message = {
        .lock = api_lock_alloc_locked(),
        .type = BtMessageTypeSetProfile,
        .profile_instance = &profile_instance,
        .data.profile.params = params,
        .data.profile.template = profile_template,
    };
    furi_check(
        furi_message_queue_put(bt->message_queue, &message, FuriWaitForever) == FuriStatusOk);
    // Wait for unlock
    api_lock_wait_unlock_and_free(message.lock);

    bt->current_profile = profile_instance;
    return profile_instance;
}

bool bt_profile_restore_default(Bt* bt) {
    bt->current_profile = bt_profile_start(bt, bt_default_profile(bt), NULL);
    return bt->current_profile != NULL;
}

BtMode bt_get_mode(Bt* bt) {
    furi_check(bt);
    return bt->bt_settings.mode == BT_SETTINGS_MODE_HOST ? BtModeHost : BtModePeripheral;
}

bool bt_set_mode(Bt* bt, BtMode mode) {
    furi_check(bt);
    if(mode == BtModeHost && !ble_profile_host_is_supported()) return false;

    bool result = false;
    BtMessage message = {
        .lock = api_lock_alloc_locked(),
        .type = BtMessageTypeSetMode,
        .result = &result,
        .data.mode = mode,
    };
    furi_check(
        furi_message_queue_put(bt->message_queue, &message, FuriWaitForever) == FuriStatusOk);
    api_lock_wait_unlock_and_free(message.lock);
    return result;
}

FuriHalBleProfileBase* bt_host_start(Bt* bt) {
    furi_check(bt);
    if(!ble_profile_host_is_supported()) return NULL;
    if(furi_hal_bt_check_profile_type(bt->current_profile, ble_profile_host)) {
        return bt->current_profile;
    }
    return bt_profile_start(bt, ble_profile_host, NULL);
}

void bt_host_stop(Bt* bt) {
    furi_check(bt);
    if(bt_get_mode(bt) == BtModeHost &&
       furi_hal_bt_check_profile_type(bt->current_profile, ble_profile_host)) {
        // Host mode is the default: no need to restart the stack, just let go of the link
        ble_profile_host_scan_stop(bt->current_profile);
        ble_profile_host_connect_cancel(bt->current_profile);
        ble_profile_host_disconnect(bt->current_profile);
        ble_profile_host_set_event_callback(bt->current_profile, NULL, NULL);
    } else {
        bt_profile_restore_default(bt);
    }
}

void bt_disconnect(Bt* bt) {
    furi_check(bt);

    // Send message
    BtMessage message = {.lock = api_lock_alloc_locked(), .type = BtMessageTypeDisconnect};
    furi_check(
        furi_message_queue_put(bt->message_queue, &message, FuriWaitForever) == FuriStatusOk);
    // Wait for unlock
    api_lock_wait_unlock_and_free(message.lock);
}

void bt_set_status_changed_callback(Bt* bt, BtStatusChangedCallback callback, void* context) {
    furi_check(bt);

    bt->status_changed_cb = callback;
    bt->status_changed_ctx = context;
}

void bt_forget_bonded_devices(Bt* bt) {
    furi_check(bt);
    BtMessage message = {.type = BtMessageTypeForgetBondedDevices};
    furi_check(
        furi_message_queue_put(bt->message_queue, &message, FuriWaitForever) == FuriStatusOk);
}

void bt_keys_storage_set_storage_path(Bt* bt, const char* keys_storage_path) {
    furi_check(bt);
    furi_check(bt->keys_storage);
    furi_check(keys_storage_path);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* path = furi_string_alloc_set(keys_storage_path);
    storage_common_resolve_path_and_ensure_app_directory(storage, path);

    bt_keys_storage_set_file_path(bt->keys_storage, furi_string_get_cstr(path));

    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);
}

void bt_keys_storage_set_default_path(Bt* bt) {
    furi_check(bt);
    furi_check(bt->keys_storage);

    bt_keys_storage_set_file_path(bt->keys_storage, BT_KEYS_STORAGE_PATH);
}
