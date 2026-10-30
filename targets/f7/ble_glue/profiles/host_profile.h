#pragma once

/**
 * @file host_profile.h
 * BLE Host mode: the Flipper acts as a GAP central / GATT client. It can scan for devices,
 * connect to one, browse its services, read, write and subscribe to characteristics, and pair.
 *
 * Needs the full radio stack (the light stack is peripheral only); check with
 * ble_profile_host_is_supported(). Typical use from an application:
 *
 *     Bt* bt = furi_record_open(RECORD_BT);
 *     FuriHalBleProfileBase* host = bt_profile_start(bt, ble_profile_host, NULL);
 *     if(host) {
 *         ble_profile_host_set_event_callback(host, my_callback, my_context);
 *         ble_profile_host_scan_start(host, true, false);
 *         ...
 *     }
 *     bt_profile_restore_default(bt); // when done; `host` is invalid after this
 *
 * Threading: the event callback runs on the BLE event thread, which has a small stack and
 * delays all Bluetooth traffic while it runs. Copy what you need out of the event and return;
 * do the real work on your own thread. The event is only valid during the callback.
 * The other functions may be called from any thread. Only one GATT operation (discover, read,
 * write, subscribe) can be in flight at a time; a new one is refused (returns false) until the
 * previous one reported its Done/Read/Write event.
 */

#include <stdint.h>
#include <stdbool.h>

#include <furi_ble/profile_interface.h>
#include <furi_ble/host_adv.h>
#include <furi_ble/host_gatt.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_HOST_ADDR_SIZE 6
/** Largest value carried by a read result or notification */
#define BLE_HOST_VALUE_MAX 244

/** Profile template for bt_profile_start() */
extern const FuriHalBleProfileTemplate* const ble_profile_host;

typedef enum {
    BleHostAddrPublic = 0,
    BleHostAddrRandom = 1,
} BleHostAddrType;

typedef enum {
    BleHostEventScanResult, /**< An advertisement or scan response was received */
    BleHostEventScanStopped, /**< Scanning ended */
    BleHostEventConnected, /**< connected.status == 0: connected and ready; else it failed */
    BleHostEventDisconnected,
    BleHostEventService, /**< Discovery: a primary service */
    BleHostEventCharacteristic, /**< Discovery: a characteristic of the last service */
    BleHostEventDescriptor, /**< Discovery: a descriptor of a characteristic */
    BleHostEventDiscoveryDone, /**< done.status == 0: everything was listed */
    BleHostEventReadDone, /**< value.status == 0: value.data holds the data */
    BleHostEventWriteDone, /**< done.status == 0: the peer accepted the write / subscription */
    BleHostEventNotification, /**< A notification or indication (value.indication) arrived */
    BleHostEventPairingPasskey, /**< Show passkey.value to the user; the peer types it in */
    BleHostEventPairingConfirm, /**< Ask whether passkey.value matches the peer's screen, then call
                                     ble_profile_host_pairing_confirm() */
    BleHostEventPairingComplete, /**< done.status == 0: paired (and bonded) */
} BleHostEventType;

typedef struct {
    uint8_t address[BLE_HOST_ADDR_SIZE]; /**< Little endian, as on the air (reverse to print) */
    uint8_t address_type; /**< BleHostAddrType */
    int8_t rssi; /**< dBm, 127 = not available */
    uint8_t adv_type; /**< 0 connectable, 1 directed, 2 scannable, 3 non-connectable, 4 scan response */
    uint8_t data_length;
    uint8_t data[BLE_HOST_ADV_DATA_MAX]; /**< Raw advertising data, see ble_host_adv_parse() */
} BleHostScanResult;

/** For failed operations status is an ATT error code (0x01 invalid handle, 0x02 read not
 * permitted, 0x03 write not permitted, 0x05 insufficient authentication, 0x0F insufficient
 * encryption, ...) or a Bluetooth error code. 0 always means success. */
typedef struct {
    BleHostEventType type;
    union {
        BleHostScanResult scan;
        struct {
            uint8_t status;
            uint8_t address[BLE_HOST_ADDR_SIZE];
            uint8_t address_type;
        } connected;
        struct {
            uint8_t reason;
        } disconnected;
        struct {
            uint8_t index; /**< Position in the list of services of this discovery */
            BleHostService service;
        } service;
        struct {
            uint8_t service_index; /**< Index of the service it belongs to */
            BleHostCharacteristic characteristic;
        } characteristic;
        struct {
            uint16_t characteristic_value_handle; /**< The characteristic it belongs to */
            BleHostDescriptor descriptor;
        } descriptor;
        struct {
            uint8_t status;
        } done;
        struct {
            uint8_t status;
            uint16_t handle;
            bool indication;
            uint8_t length;
            uint8_t data[BLE_HOST_VALUE_MAX];
        } value;
        struct {
            uint32_t value;
        } passkey;
    } data;
} BleHostEvent;

typedef void (*BleHostEventCallback)(const BleHostEvent* event, void* context);

/** Whether the running radio stack can do Host mode (needs the full stack).
 * Safe to call at any time. */
bool ble_profile_host_is_supported(void);

/** Set the function that receives all events. Pass NULL to stop receiving them. */
void ble_profile_host_set_event_callback(
    FuriHalBleProfileBase* profile,
    BleHostEventCallback callback,
    void* context);

/** Start scanning. Results arrive as BleHostEventScanResult.
 * @param active              also request scan responses (more data, more radio use)
 * @param filter_duplicates   report each device once. Leave off to follow signal strength.
 * @return false if scanning could not be started (busy connecting, unsupported stack) */
bool ble_profile_host_scan_start(
    FuriHalBleProfileBase* profile,
    bool active,
    bool filter_duplicates);

void ble_profile_host_scan_stop(FuriHalBleProfileBase* profile);

bool ble_profile_host_is_scanning(FuriHalBleProfileBase* profile);

/** Connect to a device (stops scanning first). Result arrives as BleHostEventConnected.
 * @param address       6 bytes, little endian, as received in a scan result
 * @param address_type  BleHostAddrType, as received in a scan result */
bool ble_profile_host_connect(
    FuriHalBleProfileBase* profile,
    const uint8_t address[BLE_HOST_ADDR_SIZE],
    uint8_t address_type);

/** Give up on a connection attempt that has not completed yet */
void ble_profile_host_connect_cancel(FuriHalBleProfileBase* profile);

/** Disconnect. BleHostEventDisconnected follows. */
void ble_profile_host_disconnect(FuriHalBleProfileBase* profile);

bool ble_profile_host_is_connected(FuriHalBleProfileBase* profile);

/** The negotiated ATT MTU (23 until the exchange finishes) */
uint16_t ble_profile_host_get_mtu(FuriHalBleProfileBase* profile);

/** List every service, characteristic and descriptor of the connected device. Reports
 * Service / Characteristic / Descriptor events as they are found, then DiscoveryDone. */
bool ble_profile_host_discover(FuriHalBleProfileBase* profile);

/** Read a characteristic value or descriptor. Reports BleHostEventReadDone. */
bool ble_profile_host_read(FuriHalBleProfileBase* profile, uint16_t handle);

/** Write a value. With a response (default for most characteristics) BleHostEventWriteDone
 * reports the result; without one the call returns as soon as the packet is queued and no
 * event follows. false: not connected, busy, too long for the MTU, or queue full. */
bool ble_profile_host_write(
    FuriHalBleProfileBase* profile,
    uint16_t handle,
    const uint8_t* data,
    uint8_t length,
    bool with_response);

typedef enum {
    BleHostSubscribeOff = 0,
    BleHostSubscribeNotify = 1,
    BleHostSubscribeIndicate = 2,
} BleHostSubscribeMode;

/** Turn notifications / indications on or off by writing the characteristic's client
 * characteristic configuration descriptor (UUID 0x2902). Reports BleHostEventWriteDone;
 * values then arrive as BleHostEventNotification. */
bool ble_profile_host_subscribe(
    FuriHalBleProfileBase* profile,
    uint16_t cccd_handle,
    BleHostSubscribeMode mode);

/** Start pairing / bonding with the connected device (also encrypts the link). Reports
 * PairingPasskey / PairingConfirm if needed, then PairingComplete. */
bool ble_profile_host_pair(FuriHalBleProfileBase* profile);

/** Answer a BleHostEventPairingConfirm */
void ble_profile_host_pairing_confirm(FuriHalBleProfileBase* profile, bool matches);

#ifdef __cplusplus
}
#endif
