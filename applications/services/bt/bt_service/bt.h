#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <furi_ble/profile_interface.h>
#include <core/common_defines.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_BT "bt"

typedef struct Bt Bt;

typedef enum {
    BtStatusUnavailable,
    BtStatusOff,
    BtStatusAdvertising,
    BtStatusConnected,
    BtStatusHost, /**< Host mode: not advertising, free to scan and connect to other devices */
} BtStatus;

typedef enum {
    BtModePeripheral, /**< Advertise, phones and computers connect to the Flipper (default) */
    BtModeHost, /**< The Flipper scans and connects to other devices, see profiles/host_profile.h */
} BtMode;

typedef void (*BtStatusChangedCallback)(BtStatus status, void* context);

/** Change BLE Profile
 * @note Call of this function leads to 2nd core restart
 *
 * @param bt                 Bt instance
 * @param profile_template   Profile template to change to
 * @param params             Profile parameters. Can be NULL
 *
 * @return          true on success
 */
FURI_WARN_UNUSED FuriHalBleProfileBase* bt_profile_start(
    Bt* bt,
    const FuriHalBleProfileTemplate* profile_template,
    FuriHalBleProfileParams params);

/** Stop current BLE Profile and restore default profile
 * @note Call of this function leads to 2nd core restart
 *
 * @param bt        Bt instance
 *
 * @return          true on success
 */
bool bt_profile_restore_default(Bt* bt);

/** Disconnect from Central
 *
 * @param bt        Bt instance
 */
void bt_disconnect(Bt* bt);

/** Set callback for Bluetooth status change notification
 *
 * @param bt        Bt instance
 * @param callback  BtStatusChangedCallback instance
 * @param context   pointer to context
 */
void bt_set_status_changed_callback(Bt* bt, BtStatusChangedCallback callback, void* context);

/** Get the Bluetooth mode that is active after a restart and used by bt_profile_restore_default()
 *
 * @param bt        Bt instance
 */
BtMode bt_get_mode(Bt* bt);

/** Switch between Peripheral and Host mode. The choice is saved.
 * @note Call of this function leads to 2nd core restart. Host mode needs the full radio stack.
 *
 * @param bt        Bt instance
 * @param mode      new mode
 *
 * @return          true on success, false if the mode is not supported by the radio stack
 */
bool bt_set_mode(Bt* bt, BtMode mode);

/** Get the BLE Host profile instance, starting it if needed (see profiles/host_profile.h).
 * Pair with bt_host_stop(). In Host mode the profile is already running.
 *
 * @param bt        Bt instance
 *
 * @return          profile instance for ble_profile_host_*(), NULL if the radio stack
 *                  cannot do Host mode
 */
FuriHalBleProfileBase* bt_host_start(Bt* bt);

/** Finish using the Host profile: stops scanning and disconnects. Outside Host mode this also
 * restores the default (serial) profile. The instance from bt_host_start() is invalid afterwards.
 *
 * @param bt        Bt instance
 */
void bt_host_stop(Bt* bt);

/** Forget bonded devices
 * @note Leads to wipe ble key storage and deleting bt.keys
 *
 * @param bt        Bt instance
 */
void bt_forget_bonded_devices(Bt* bt);

/** Set keys storage file path
 *
 * @param bt                    Bt instance
 * @param keys_storage_path     Path to file with saved keys
 */
void bt_keys_storage_set_storage_path(Bt* bt, const char* keys_storage_path);

/** Set default keys storage file path
 *
 * @param bt                    Bt instance
 */
void bt_keys_storage_set_default_path(Bt* bt);

#ifdef __cplusplus
}
#endif
