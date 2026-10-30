#include "host_profile.h"

#include "app_common.h"
#include <ble/ble.h>
#include <furi_ble/event_dispatcher.h>

#include <furi.h>
#include <furi_hal.h>

#define TAG "BleHost"

#define NO_CONNECTION  0xFFFF
#define DEFAULT_MTU    23
#define ATT_HEADER     3
#define ATT_ERROR_NONE 0

// Time = value * 0.625 ms
#define SCAN_INTERVAL 0x0080
#define SCAN_WINDOW   0x0080
#define INIT_INTERVAL 0x0060
#define INIT_WINDOW   0x0060
// Time = value * 1.25 ms
#define CONN_INTERVAL_MIN 0x0018
#define CONN_INTERVAL_MAX 0x0028
// Time = value * 10 ms
#define CONN_TIMEOUT 0x00C8

typedef enum {
    HostProcNone,
    HostProcMtu,
    HostProcDiscover,
    HostProcRead,
    HostProcWrite,
} HostProc;

typedef struct {
    FuriHalBleProfileBase base;
    GapSvcEventHandler* handler;
    FuriMutex* mutex; // protects everything below except the event-thread-only fields
    BleHostEventCallback callback;
    void* callback_context;

    bool scanning;
    bool connecting;
    uint16_t conn;
    uint16_t mtu;
    uint8_t peer_address[BLE_HOST_ADDR_SIZE];
    uint8_t peer_address_type;
    HostProc proc;
    uint8_t att_error; // last ATT error response while a read/write was in flight

    // event thread only
    HostDisc* disc;
    BleHostEvent* event; // scratch event, kept off the small event thread stack
    bool read_ready;
    uint8_t read_length;
    uint8_t read_data[BLE_HOST_VALUE_MAX];
} BleProfileHost;
_Static_assert(offsetof(BleProfileHost, base) == 0, "Wrong layout");

/* ------------------------------------------------------------------ helpers */

static BleProfileHost* host_get(FuriHalBleProfileBase* profile) {
    furi_check(profile && profile->config == ble_profile_host);
    return (BleProfileHost*)profile;
}

bool ble_profile_host_is_supported(void) {
    return furi_hal_bt_get_radio_stack() == FuriHalBtStackFull;
}

static void host_lock(BleProfileHost* host) {
    furi_check(furi_mutex_acquire(host->mutex, FuriWaitForever) == FuriStatusOk);
}

static void host_unlock(BleProfileHost* host) {
    furi_check(furi_mutex_release(host->mutex) == FuriStatusOk);
}

static bool host_is_our_connection(BleProfileHost* host, uint16_t handle) {
    host_lock(host);
    bool ours = host->conn != NO_CONNECTION && host->conn == handle;
    host_unlock(host);
    return ours;
}

static uint16_t host_le16(const uint8_t* p) {
    return ble_host_le16(p);
}

/* Hand the filled-in scratch event to the application. Event thread only. */
static void host_emit(BleProfileHost* host) {
    host_lock(host);
    BleHostEventCallback callback = host->callback;
    void* context = host->callback_context;
    host_unlock(host);
    if(callback) callback(host->event, context);
}

static BleHostEvent* host_new_event(BleProfileHost* host, BleHostEventType type) {
    memset(host->event, 0, sizeof(BleHostEvent));
    host->event->type = type;
    return host->event;
}

static void host_emit_done(BleProfileHost* host, BleHostEventType type, uint8_t status) {
    host_new_event(host, type)->data.done.status = status;
    host_emit(host);
}

/* ------------------------------------------------------------------ discovery */

static void host_disc_item_cb(void* context, const HostDiscItem* item) {
    BleProfileHost* host = context;
    BleHostEvent* event;
    switch(item->kind) {
    case HostDiscItemService:
        event = host_new_event(host, BleHostEventService);
        event->data.service.index = item->service_index;
        event->data.service.service = item->service;
        break;
    case HostDiscItemCharacteristic:
        event = host_new_event(host, BleHostEventCharacteristic);
        event->data.characteristic.service_index = item->service_index;
        event->data.characteristic.characteristic = item->characteristic;
        break;
    default:
        event = host_new_event(host, BleHostEventDescriptor);
        event->data.descriptor.characteristic_value_handle = item->char_value_handle;
        event->data.descriptor.descriptor = item->descriptor;
        break;
    }
    host_emit(host);
}

static void host_finish_discovery(BleProfileHost* host, uint8_t status) {
    host_lock(host);
    host->proc = HostProcNone;
    host_unlock(host);
    host_emit_done(host, BleHostEventDiscoveryDone, status);
}

/* Send the request the walker asks for. A request the stack refuses is treated like a failed
 * procedure, so the walker moves on (or gives up). */
static void host_run_step(BleProfileHost* host, HostStep step) {
    while(true) {
        tBleStatus status;
        switch(step.kind) {
        case HostStepServices:
            status = aci_gatt_disc_all_primary_services(host->conn);
            break;
        case HostStepChars:
            status = aci_gatt_disc_all_char_of_service(host->conn, step.start, step.end);
            break;
        case HostStepDescs:
            status = aci_gatt_disc_all_char_desc(host->conn, step.start, step.end);
            break;
        case HostStepDone:
            host_finish_discovery(host, 0);
            return;
        case HostStepFailed:
            host_finish_discovery(host, step.status);
            return;
        default:
            return;
        }
        if(status == BLE_STATUS_SUCCESS) return;
        FURI_LOG_W(TAG, "Discovery request refused: 0x%02X", status);
        step = host_disc_on_complete(host->disc, status);
    }
}

/* ------------------------------------------------------------------ procedure results */

static void host_proc_complete(BleProfileHost* host, uint8_t error) {
    host_lock(host);
    HostProc proc = host->proc;
    uint8_t att_error = host->att_error;
    if(proc != HostProcDiscover) host->proc = HostProcNone;
    host->att_error = ATT_ERROR_NONE;
    host_unlock(host);

    switch(proc) {
    case HostProcMtu: {
        BleHostEvent* event = host_new_event(host, BleHostEventConnected);
        host_lock(host);
        memcpy(event->data.connected.address, host->peer_address, BLE_HOST_ADDR_SIZE);
        event->data.connected.address_type = host->peer_address_type;
        host_unlock(host);
        host_emit(host);
        break;
    }
    case HostProcDiscover:
        host_run_step(host, host_disc_on_complete(host->disc, error));
        break;
    case HostProcRead: {
        BleHostEvent* event = host_new_event(host, BleHostEventReadDone);
        if(host->read_ready) {
            event->data.value.length = host->read_length;
            memcpy(event->data.value.data, host->read_data, host->read_length);
        } else {
            event->data.value.status = att_error ? att_error : (error ? error : 0x0E);
        }
        host->read_ready = false;
        host_emit(host);
        break;
    }
    case HostProcWrite:
        host_emit_done(host, BleHostEventWriteDone, att_error ? att_error : error);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ connection events */

static void host_handle_advertising_reports(BleProfileHost* host, const uint8_t* p, uint8_t len) {
    if(len < 1) return;
    uint8_t reports = p[0];
    uint16_t pos = 1;
    for(uint8_t i = 0; i < reports; i++) {
        // event type, address type, address (6), data length, data, rssi
        if(pos + 9 > len) return;
        uint8_t data_length = p[pos + 8];
        if(data_length > BLE_HOST_ADV_DATA_MAX || pos + 9 + data_length + 1 > len) return;

        BleHostEvent* event = host_new_event(host, BleHostEventScanResult);
        BleHostScanResult* scan = &event->data.scan;
        scan->adv_type = p[pos];
        scan->address_type = p[pos + 1];
        memcpy(scan->address, &p[pos + 2], BLE_HOST_ADDR_SIZE);
        scan->data_length = data_length;
        memcpy(scan->data, &p[pos + 9], data_length);
        scan->rssi = (int8_t)p[pos + 9 + data_length];
        host_emit(host);

        pos += 10 + data_length;
    }
}

static void host_handle_connection_complete(
    BleProfileHost* host,
    const hci_le_connection_complete_event_rp0* connection) {
    host_lock(host);
    bool was_connecting = host->connecting;
    host->connecting = false;
    if(connection->Status == 0) {
        host->conn = connection->Connection_Handle;
        memcpy(host->peer_address, connection->Peer_Address, BLE_HOST_ADDR_SIZE);
        host->peer_address_type = connection->Peer_Address_Type;
        host->mtu = DEFAULT_MTU;
        host->proc = HostProcMtu;
        host->att_error = ATT_ERROR_NONE;
    }
    host_unlock(host);

    if(connection->Status != 0) {
        // a cancelled attempt has already been reported
        if(was_connecting) {
            BleHostEvent* event = host_new_event(host, BleHostEventConnected);
            event->data.connected.status = connection->Status;
            host_emit(host);
        }
        return;
    }

    FURI_LOG_I(TAG, "Connected, handle 0x%04X", connection->Connection_Handle);
    // Negotiate the MTU first; Connected is reported once that has finished
    if(aci_gatt_exchange_config(connection->Connection_Handle) != BLE_STATUS_SUCCESS) {
        host_proc_complete(host, 0);
    }
}

/* ------------------------------------------------------------------ the event handler */

static BleEventAckStatus host_handle_vendor(BleProfileHost* host, const hci_event_pckt* pckt) {
    if(pckt->plen < 2) return BleEventNotAck;
    const evt_blecore_aci* ev = (const evt_blecore_aci*)pckt->data;
    const uint8_t* d = ev->data;
    uint8_t len = pckt->plen - 2;

    // Events about the GAP procedures themselves carry no connection handle
    if(ev->ecode == ACI_GAP_PROC_COMPLETE_VSEVT_CODE) {
        if(len < 2) return BleEventNotAck;
        uint8_t procedure = d[0], status = d[1];
        if(procedure == GAP_OBSERVATION_PROC) {
            host_lock(host);
            host->scanning = false;
            host_unlock(host);
            host_emit_done(host, BleHostEventScanStopped, status);
            return BleEventAckFlowEnable;
        } else if(procedure == GAP_DIRECT_CONNECTION_ESTABLISHMENT_PROC) {
            host_lock(host);
            bool failed = host->connecting && status != 0;
            if(failed) host->connecting = false;
            host_unlock(host);
            if(failed) {
                BleHostEvent* event = host_new_event(host, BleHostEventConnected);
                event->data.connected.status = status;
                host_emit(host);
            }
            return BleEventAckFlowEnable;
        }
        return BleEventNotAck;
    }

    // Everything else we care about starts with the connection handle
    if(len < 2 || !host_is_our_connection(host, host_le16(d))) return BleEventNotAck;

    switch(ev->ecode) {
    case ACI_ATT_EXCHANGE_MTU_RESP_VSEVT_CODE:
        if(len >= 4) {
            uint16_t mtu = host_le16(&d[2]);
            host_lock(host);
            host->mtu = mtu < DEFAULT_MTU ? DEFAULT_MTU : mtu;
            bool exchanging = host->proc == HostProcMtu;
            host_unlock(host);
            // Don't depend on a separate "procedure complete" for this one
            if(exchanging) host_proc_complete(host, 0);
        }
        break;

    case ACI_ATT_READ_BY_GROUP_TYPE_RESP_VSEVT_CODE:
    case ACI_ATT_READ_BY_TYPE_RESP_VSEVT_CODE:
    case ACI_ATT_FIND_INFO_RESP_VSEVT_CODE: {
        // handle(2), item length or format(1), data length(1), data
        if(len < 4 || !host_disc_active(host->disc)) break;
        uint8_t data_length = d[3];
        if(data_length > len - 4) data_length = len - 4;
        if(ev->ecode == ACI_ATT_READ_BY_GROUP_TYPE_RESP_VSEVT_CODE) {
            host_disc_on_services(host->disc, &d[4], data_length, d[2]);
        } else if(ev->ecode == ACI_ATT_READ_BY_TYPE_RESP_VSEVT_CODE) {
            host_disc_on_chars(host->disc, &d[4], data_length, d[2]);
        } else {
            host_disc_on_descs(host->disc, &d[4], data_length, d[2]);
        }
        break;
    }

    case ACI_ATT_READ_RESP_VSEVT_CODE: {
        // handle(2), length(1), value
        if(len < 3) break;
        uint8_t value_length = d[2];
        if(value_length > len - 3) value_length = len - 3;
        if(value_length > BLE_HOST_VALUE_MAX) value_length = BLE_HOST_VALUE_MAX;
        memcpy(host->read_data, &d[3], value_length);
        host->read_length = value_length;
        host->read_ready = true;
        break;
    }

    case ACI_GATT_ERROR_RESP_VSEVT_CODE:
        // handle(2), request opcode(1), attribute handle(2), error(1). Discovery ends with a
        // "not found" error by design, only reads and writes care.
        if(len >= 6) {
            host_lock(host);
            if(host->proc == HostProcRead || host->proc == HostProcWrite) host->att_error = d[5];
            host_unlock(host);
        }
        break;

    case ACI_GATT_NOTIFICATION_VSEVT_CODE:
    case ACI_GATT_INDICATION_VSEVT_CODE: {
        // handle(2), attribute handle(2), length(1), value
        if(len < 5) break;
        uint8_t value_length = d[4];
        if(value_length > len - 5) value_length = len - 5;
        if(value_length > BLE_HOST_VALUE_MAX) value_length = BLE_HOST_VALUE_MAX;
        bool indication = ev->ecode == ACI_GATT_INDICATION_VSEVT_CODE;
        BleHostEvent* event = host_new_event(host, BleHostEventNotification);
        event->data.value.handle = host_le16(&d[2]);
        event->data.value.indication = indication;
        event->data.value.length = value_length;
        memcpy(event->data.value.data, &d[5], value_length);
        host_emit(host);
        if(indication) aci_gatt_confirm_indication(host_le16(d));
        break;
    }

    case ACI_GATT_PROC_COMPLETE_VSEVT_CODE:
        if(len >= 3) host_proc_complete(host, d[2]);
        break;

    case ACI_GATT_TX_POOL_AVAILABLE_VSEVT_CODE:
        break;

    case ACI_GAP_PASS_KEY_REQ_VSEVT_CODE: {
        // We have no keyboard: pick the passkey, show it, and the peer types it in
        uint32_t passkey = furi_hal_random_get() % 1000000;
        aci_gap_pass_key_resp(host_le16(d), passkey);
        host_new_event(host, BleHostEventPairingPasskey)->data.passkey.value = passkey;
        host_emit(host);
        break;
    }

    case ACI_GAP_NUMERIC_COMPARISON_VALUE_VSEVT_CODE:
        if(len >= 6) {
            uint32_t value = d[2] | (d[3] << 8) | (d[4] << 16) | ((uint32_t)d[5] << 24);
            host_new_event(host, BleHostEventPairingConfirm)->data.passkey.value = value;
            host_emit(host);
        }
        break;

    case ACI_GAP_PAIRING_COMPLETE_VSEVT_CODE:
        if(len >= 3) host_emit_done(host, BleHostEventPairingComplete, d[2]);
        break;

    case ACI_L2CAP_CONNECTION_UPDATE_REQ_VSEVT_CODE: {
        // The device asks us (the central) to change the connection parameters. Accept.
        if(len < 13) break;
        aci_l2cap_connection_parameter_update_resp(
            host_le16(d),
            host_le16(&d[5]),
            host_le16(&d[7]),
            host_le16(&d[9]),
            host_le16(&d[11]),
            0,
            0,
            d[2],
            1);
        break;
    }

    default:
        return BleEventNotAck;
    }
    return BleEventAckFlowEnable;
}

static BleEventAckStatus host_event_handler(void* event, void* context) {
    BleProfileHost* host = context;
    const hci_event_pckt* pckt = (const hci_event_pckt*)(((hci_uart_pckt*)event)->data);

    switch(pckt->evt) {
    case HCI_DISCONNECTION_COMPLETE_EVT_CODE: {
        const hci_disconnection_complete_event_rp0* e =
            (const hci_disconnection_complete_event_rp0*)pckt->data;
        if(!host_is_our_connection(host, e->Connection_Handle)) return BleEventNotAck;
        host_lock(host);
        host->conn = NO_CONNECTION;
        host->proc = HostProcNone;
        host->mtu = DEFAULT_MTU;
        host_unlock(host);
        host_disc_init(host->disc, host_disc_item_cb, host);
        host->read_ready = false;
        FURI_LOG_I(TAG, "Disconnected, reason 0x%02X", e->Reason);
        host_new_event(host, BleHostEventDisconnected)->data.disconnected.reason = e->Reason;
        host_emit(host);
        return BleEventAckFlowEnable;
    }

    case HCI_LE_META_EVT_CODE: {
        const evt_le_meta_event* meta = (const evt_le_meta_event*)pckt->data;
        switch(meta->subevent) {
        case HCI_LE_ADVERTISING_REPORT_SUBEVT_CODE:
            host_handle_advertising_reports(host, meta->data, pckt->plen - 1);
            return BleEventAckFlowEnable;
        case HCI_LE_CONNECTION_COMPLETE_SUBEVT_CODE: {
            const hci_le_connection_complete_event_rp0* e =
                (const hci_le_connection_complete_event_rp0*)meta->data;
            if(e->Role != 0) return BleEventNotAck; // somebody connected to us, not our business
            host_handle_connection_complete(host, e);
            return BleEventAckFlowEnable;
        }
        case HCI_LE_CONNECTION_UPDATE_COMPLETE_SUBEVT_CODE:
        case HCI_LE_PHY_UPDATE_COMPLETE_SUBEVT_CODE: {
            // both: status(1), connection handle(2), ...
            const uint8_t* meta_data = meta->data;
            return host_is_our_connection(host, host_le16(&meta_data[1])) ? BleEventAckFlowEnable :
                                                                           BleEventNotAck;
        }
        default:
            return BleEventNotAck;
        }
    }

    case HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE:
        return host_handle_vendor(host, pckt);

    default:
        return BleEventNotAck;
    }
}

/* ------------------------------------------------------------------ profile */

static FuriHalBleProfileBase* ble_profile_host_start(FuriHalBleProfileParams profile_params) {
    UNUSED(profile_params);

    BleProfileHost* host = malloc(sizeof(BleProfileHost));
    memset(host, 0, sizeof(BleProfileHost));
    host->base.config = ble_profile_host;
    host->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    host->conn = NO_CONNECTION;
    host->mtu = DEFAULT_MTU;
    host->disc = malloc(sizeof(HostDisc));
    host_disc_init(host->disc, host_disc_item_cb, host);
    host->event = malloc(sizeof(BleHostEvent));
    host->handler = ble_event_dispatcher_register_svc_handler(host_event_handler, host);
    return &host->base;
}

static void ble_profile_host_stop(FuriHalBleProfileBase* profile) {
    BleProfileHost* host = host_get(profile);

    // The stack is reset right after this, which also drops any connection
    if(host->scanning) aci_gap_terminate_gap_proc(GAP_OBSERVATION_PROC);
    if(host->connecting) aci_gap_terminate_gap_proc(GAP_DIRECT_CONNECTION_ESTABLISHMENT_PROC);

    ble_event_dispatcher_unregister_svc_handler(host->handler);
    furi_mutex_free(host->mutex);
    free(host->disc);
    free(host->event);
    free(host);
}

static void
    ble_profile_host_get_gap_config(GapConfig* config, FuriHalBleProfileParams profile_params) {
    UNUSED(profile_params);
    furi_check(config);

    memset(config, 0, sizeof(GapConfig));
    // never advertised, but gap_init() insists on a valid service UUID
    config->adv_service.UUID_Type = UUID_TYPE_16;
    config->adv_service.Service_UUID_16 = 0x3080;
    config->appearance_char = 0x8600;
    config->bonding_mode = true;
    config->pairing_method = GapPairingPinCodeVerifyYesNo;
    config->conn_param.conn_int_min = CONN_INTERVAL_MIN;
    config->conn_param.conn_int_max = CONN_INTERVAL_MAX;
    config->conn_param.slave_latency = 0;
    config->conn_param.supervisor_timeout = 0;
    config->role_central = true;
    config->no_advertising = true;
    memcpy(config->mac_address, furi_hal_version_get_ble_mac(), sizeof(config->mac_address));
    strlcpy(
        config->adv_name,
        furi_hal_version_get_ble_local_device_name_ptr(),
        FURI_HAL_VERSION_DEVICE_NAME_LENGTH);
}

static const FuriHalBleProfileTemplate profile_callbacks = {
    .start = ble_profile_host_start,
    .stop = ble_profile_host_stop,
    .get_gap_config = ble_profile_host_get_gap_config,
};

const FuriHalBleProfileTemplate* const ble_profile_host = &profile_callbacks;

/* ------------------------------------------------------------------ public API */

void ble_profile_host_set_event_callback(
    FuriHalBleProfileBase* profile,
    BleHostEventCallback callback,
    void* context) {
    BleProfileHost* host = host_get(profile);
    host_lock(host);
    host->callback = callback;
    host->callback_context = context;
    host_unlock(host);
}

bool ble_profile_host_scan_start(
    FuriHalBleProfileBase* profile,
    bool active,
    bool filter_duplicates) {
    BleProfileHost* host = host_get(profile);
    if(!ble_profile_host_is_supported()) return false;

    host_lock(host);
    bool already = host->scanning;
    bool busy = host->connecting;
    if(!already && !busy) host->scanning = true;
    host_unlock(host);
    if(already) return true;
    if(busy) return false;

    tBleStatus status = aci_gap_start_observation_proc(
        SCAN_INTERVAL,
        SCAN_WINDOW,
        active ? 0x01 : 0x00,
        GAP_PUBLIC_ADDR,
        filter_duplicates ? 0x01 : 0x00,
        0x00);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "Scan start failed: 0x%02X", status);
        host_lock(host);
        host->scanning = false;
        host_unlock(host);
        return false;
    }
    return true;
}

void ble_profile_host_scan_stop(FuriHalBleProfileBase* profile) {
    BleProfileHost* host = host_get(profile);
    host_lock(host);
    bool was_scanning = host->scanning;
    host->scanning = false;
    host_unlock(host);
    if(was_scanning) aci_gap_terminate_gap_proc(GAP_OBSERVATION_PROC);
}

bool ble_profile_host_is_scanning(FuriHalBleProfileBase* profile) {
    BleProfileHost* host = host_get(profile);
    host_lock(host);
    bool scanning = host->scanning;
    host_unlock(host);
    return scanning;
}

bool ble_profile_host_connect(
    FuriHalBleProfileBase* profile,
    const uint8_t address[BLE_HOST_ADDR_SIZE],
    uint8_t address_type) {
    BleProfileHost* host = host_get(profile);
    furi_check(address);
    if(!ble_profile_host_is_supported() || address_type > BleHostAddrRandom) return false;

    host_lock(host);
    bool busy = host->conn != NO_CONNECTION || host->connecting;
    bool was_scanning = host->scanning;
    if(!busy) {
        host->scanning = false;
        host->connecting = true;
    }
    host_unlock(host);
    if(busy) return false;

    // a scan has to stop before a connection can be initiated
    if(was_scanning) aci_gap_terminate_gap_proc(GAP_OBSERVATION_PROC);

    tBleStatus status = aci_gap_create_connection(
        INIT_INTERVAL,
        INIT_WINDOW,
        address_type,
        address,
        GAP_PUBLIC_ADDR,
        CONN_INTERVAL_MIN,
        CONN_INTERVAL_MAX,
        0,
        CONN_TIMEOUT,
        0,
        0);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "Connect failed: 0x%02X", status);
        host_lock(host);
        host->connecting = false;
        host_unlock(host);
        return false;
    }
    return true;
}

void ble_profile_host_connect_cancel(FuriHalBleProfileBase* profile) {
    BleProfileHost* host = host_get(profile);
    host_lock(host);
    bool connecting = host->connecting;
    host_unlock(host);
    if(connecting) aci_gap_terminate_gap_proc(GAP_DIRECT_CONNECTION_ESTABLISHMENT_PROC);
}

void ble_profile_host_disconnect(FuriHalBleProfileBase* profile) {
    BleProfileHost* host = host_get(profile);
    host_lock(host);
    uint16_t conn = host->conn;
    host_unlock(host);
    if(conn != NO_CONNECTION) aci_gap_terminate(conn, 0x13);
}

bool ble_profile_host_is_connected(FuriHalBleProfileBase* profile) {
    BleProfileHost* host = host_get(profile);
    host_lock(host);
    bool connected = host->conn != NO_CONNECTION && host->proc != HostProcMtu;
    host_unlock(host);
    return connected;
}

uint16_t ble_profile_host_get_mtu(FuriHalBleProfileBase* profile) {
    BleProfileHost* host = host_get(profile);
    host_lock(host);
    uint16_t mtu = host->mtu;
    host_unlock(host);
    return mtu;
}

/* Claim the single GATT operation slot. Returns the connection handle or NO_CONNECTION. */
static uint16_t host_begin(BleProfileHost* host, HostProc proc) {
    host_lock(host);
    uint16_t conn = NO_CONNECTION;
    if(host->conn != NO_CONNECTION && host->proc == HostProcNone) {
        conn = host->conn;
        host->proc = proc;
        host->att_error = ATT_ERROR_NONE;
    }
    host_unlock(host);
    return conn;
}

static void host_abort(BleProfileHost* host) {
    host_lock(host);
    host->proc = HostProcNone;
    host_unlock(host);
}

bool ble_profile_host_discover(FuriHalBleProfileBase* profile) {
    BleProfileHost* host = host_get(profile);
    uint16_t conn = host_begin(host, HostProcDiscover);
    if(conn == NO_CONNECTION) return false;
    // Nothing reaches the event thread before the first request goes out, so the walker can be
    // reset here. A refusal is reported by the return value, not as an event.
    host_disc_start(host->disc);
    if(aci_gatt_disc_all_primary_services(conn) != BLE_STATUS_SUCCESS) {
        host_abort(host);
        return false;
    }
    return true;
}

bool ble_profile_host_read(FuriHalBleProfileBase* profile, uint16_t handle) {
    BleProfileHost* host = host_get(profile);
    uint16_t conn = host_begin(host, HostProcRead);
    if(conn == NO_CONNECTION) return false;
    host->read_ready = false;
    if(aci_gatt_read_char_value(conn, handle) != BLE_STATUS_SUCCESS) {
        host_abort(host);
        return false;
    }
    return true;
}

bool ble_profile_host_write(
    FuriHalBleProfileBase* profile,
    uint16_t handle,
    const uint8_t* data,
    uint8_t length,
    bool with_response) {
    BleProfileHost* host = host_get(profile);
    furi_check(data || length == 0);

    host_lock(host);
    uint16_t mtu = host->mtu;
    host_unlock(host);
    if(length > BLE_HOST_VALUE_MAX || length + ATT_HEADER > mtu) return false;

    if(!with_response) {
        host_lock(host);
        uint16_t conn = host->conn;
        host_unlock(host);
        if(conn == NO_CONNECTION) return false;
        return aci_gatt_write_without_resp(conn, handle, length, data) == BLE_STATUS_SUCCESS;
    }

    uint16_t conn = host_begin(host, HostProcWrite);
    if(conn == NO_CONNECTION) return false;
    if(aci_gatt_write_char_value(conn, handle, length, data) != BLE_STATUS_SUCCESS) {
        host_abort(host);
        return false;
    }
    return true;
}

bool ble_profile_host_subscribe(
    FuriHalBleProfileBase* profile,
    uint16_t cccd_handle,
    BleHostSubscribeMode mode) {
    if(mode > BleHostSubscribeIndicate) return false;
    uint8_t value[2] = {mode, 0};
    return ble_profile_host_write(profile, cccd_handle, value, sizeof(value), true);
}

bool ble_profile_host_pair(FuriHalBleProfileBase* profile) {
    BleProfileHost* host = host_get(profile);
    host_lock(host);
    uint16_t conn = host->conn;
    host_unlock(host);
    if(conn == NO_CONNECTION) return false;
    return aci_gap_send_pairing_req(conn, 0) == BLE_STATUS_SUCCESS;
}

void ble_profile_host_pairing_confirm(FuriHalBleProfileBase* profile, bool matches) {
    BleProfileHost* host = host_get(profile);
    host_lock(host);
    uint16_t conn = host->conn;
    host_unlock(host);
    if(conn != NO_CONNECTION) aci_gap_numeric_comparison_value_confirm_yesno(conn, matches);
}
