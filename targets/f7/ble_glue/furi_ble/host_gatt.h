#pragma once

/**
 * @file host_gatt.h
 * GATT client helpers for BLE Host mode: decoders for ATT discovery responses and the state
 * machine that walks a peer's services -> characteristics -> descriptors. Header-only and free
 * of Furi / ST dependencies, so it can be unit tested on a computer.
 *
 * The walker does not talk to the radio. The caller feeds it responses and "procedure complete"
 * notifications, and it answers with the next discovery request to send (a HostStep).
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_HOST_MAX_SERVICES 32
#define BLE_HOST_MAX_CHARS    48

#define BLE_HOST_PROP_BROADCAST         0x01
#define BLE_HOST_PROP_READ              0x02
#define BLE_HOST_PROP_WRITE_NO_RESPONSE 0x04
#define BLE_HOST_PROP_WRITE             0x08
#define BLE_HOST_PROP_NOTIFY            0x10
#define BLE_HOST_PROP_INDICATE          0x20
#define BLE_HOST_PROP_SIGNED_WRITE      0x40
#define BLE_HOST_PROP_EXTENDED          0x80

/** A 16-bit or 128-bit UUID. 16-bit UUIDs use bytes[0..1] (little endian, as on the air). */
typedef struct {
    uint8_t size; // 2 or 16
    uint8_t bytes[16];
} BleHostUuid;

typedef struct {
    uint16_t start_handle;
    uint16_t end_handle;
    BleHostUuid uuid;
} BleHostService;

typedef struct {
    uint16_t declaration_handle;
    uint16_t value_handle;
    uint8_t properties; // BLE_HOST_PROP_*
    BleHostUuid uuid;
} BleHostCharacteristic;

typedef struct {
    uint16_t handle;
    BleHostUuid uuid;
} BleHostDescriptor;

static inline uint16_t ble_host_le16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

/** The 16-bit value of a UUID, or 0 when it is a 128-bit UUID. */
static inline uint16_t ble_host_uuid16(const BleHostUuid* uuid) {
    return uuid->size == 2 ? ble_host_le16(uuid->bytes) : 0;
}

static inline void ble_host_uuid_set(BleHostUuid* uuid, const uint8_t* bytes, uint8_t size) {
    memset(uuid, 0, sizeof(*uuid));
    uuid->size = size;
    memcpy(uuid->bytes, bytes, size);
}

/* -------------------------------------------------------------------- response decoders */

/* One entry of an ATT Read By Group Type response (primary service): start handle, end
 * handle, UUID. item_len is 6 (16-bit UUID) or 20 (128-bit UUID). */
static inline bool ble_host_decode_service(const uint8_t* p, uint8_t item_len, BleHostService* out) {
    if(item_len != 6 && item_len != 20) return false;
    out->start_handle = ble_host_le16(&p[0]);
    out->end_handle = ble_host_le16(&p[2]);
    if(out->start_handle == 0 || out->end_handle < out->start_handle) return false;
    ble_host_uuid_set(&out->uuid, &p[4], item_len - 4);
    return true;
}

/* One entry of an ATT Read By Type response (characteristic declaration): declaration handle,
 * properties, value handle, UUID. item_len is 7 (16-bit UUID) or 21 (128-bit UUID). */
static inline bool
    ble_host_decode_char(const uint8_t* p, uint8_t item_len, BleHostCharacteristic* out) {
    if(item_len != 7 && item_len != 21) return false;
    out->declaration_handle = ble_host_le16(&p[0]);
    out->properties = p[2];
    out->value_handle = ble_host_le16(&p[3]);
    if(out->declaration_handle == 0 || out->value_handle <= out->declaration_handle) return false;
    ble_host_uuid_set(&out->uuid, &p[5], item_len - 5);
    return true;
}

/* One entry of an ATT Find Information response: handle, UUID. item_len is 4 (16-bit UUID) or
 * 18 (128-bit UUID). */
static inline bool ble_host_decode_desc(const uint8_t* p, uint8_t item_len, BleHostDescriptor* out) {
    if(item_len != 4 && item_len != 18) return false;
    out->handle = ble_host_le16(&p[0]);
    if(out->handle == 0) return false;
    ble_host_uuid_set(&out->uuid, &p[2], item_len - 2);
    return true;
}

/** Decode a whole ATT Read By Group Type response into out (at most max entries). */
static inline uint8_t ble_host_parse_services(
    const uint8_t* data,
    uint8_t data_len,
    uint8_t item_len,
    BleHostService* out,
    uint8_t max) {
    if(item_len != 6 && item_len != 20) return 0;
    uint8_t count = 0;
    for(uint8_t pos = 0; pos + item_len <= data_len && count < max; pos += item_len) {
        if(ble_host_decode_service(&data[pos], item_len, &out[count])) count++;
    }
    return count;
}

/** Decode a whole ATT Read By Type response (characteristic declarations). */
static inline uint8_t ble_host_parse_chars(
    const uint8_t* data,
    uint8_t data_len,
    uint8_t item_len,
    BleHostCharacteristic* out,
    uint8_t max) {
    if(item_len != 7 && item_len != 21) return 0;
    uint8_t count = 0;
    for(uint8_t pos = 0; pos + item_len <= data_len && count < max; pos += item_len) {
        if(ble_host_decode_char(&data[pos], item_len, &out[count])) count++;
    }
    return count;
}

/** Decode a whole ATT Find Information response. format 1 = 16-bit UUIDs (4 byte entries),
 * 2 = 128-bit UUIDs (18 byte entries). */
static inline uint8_t ble_host_parse_descs(
    const uint8_t* data,
    uint8_t data_len,
    uint8_t format,
    BleHostDescriptor* out,
    uint8_t max) {
    if(format != 1 && format != 2) return 0;
    uint8_t item_len = format == 1 ? 4 : 18;
    uint8_t count = 0;
    for(uint8_t pos = 0; pos + item_len <= data_len && count < max; pos += item_len) {
        if(ble_host_decode_desc(&data[pos], item_len, &out[count])) count++;
    }
    return count;
}

/* -------------------------------------------------------------------- discovery walker */

typedef enum {
    HostDiscPhaseIdle,
    HostDiscPhaseServices,
    HostDiscPhaseChars,
    HostDiscPhaseDescs,
} HostDiscPhase;

typedef enum {
    HostStepNone, // nothing to send (waiting for more responses)
    HostStepServices, // send "discover all primary services"
    HostStepChars, // send "discover all characteristics" in [start, end]
    HostStepDescs, // send "discover all descriptors" in [start, end]
    HostStepDone, // discovery finished
    HostStepFailed, // discovery failed, status holds the stack error code
} HostStepKind;

typedef struct {
    HostStepKind kind;
    uint16_t start;
    uint16_t end;
    uint8_t status;
} HostStep;

typedef enum {
    HostDiscItemService,
    HostDiscItemCharacteristic,
    HostDiscItemDescriptor,
} HostDiscItemKind;

typedef struct {
    HostDiscItemKind kind;
    uint8_t service_index; // service (Characteristic) or owning service (Descriptor)
    uint16_t char_value_handle; // owning characteristic (Descriptor)
    BleHostService service;
    BleHostCharacteristic characteristic;
    BleHostDescriptor descriptor;
} HostDiscItem;

typedef void (*HostDiscEmit)(void* context, const HostDiscItem* item);

typedef struct {
    HostDiscPhase phase;
    BleHostService services[BLE_HOST_MAX_SERVICES];
    uint8_t service_count;
    uint8_t service_index; // service being walked in the Chars/Descs phases
    BleHostCharacteristic chars[BLE_HOST_MAX_CHARS]; // characteristics of that service
    uint8_t char_count;
    uint8_t char_index; // characteristic whose descriptors are being walked
    bool truncated; // more services/characteristics than we keep
    HostDiscEmit emit;
    void* emit_context;
} HostDisc;

static inline HostStep host_step(HostStepKind kind, uint16_t start, uint16_t end, uint8_t status) {
    HostStep step;
    step.kind = kind;
    step.start = start;
    step.end = end;
    step.status = status;
    return step;
}

static inline void host_disc_item_init(HostDiscItem* item, HostDiscItemKind kind, uint8_t service_index) {
    memset(item, 0, sizeof(*item));
    item->kind = kind;
    item->service_index = service_index;
}

static inline void host_disc_init(HostDisc* d, HostDiscEmit emit, void* emit_context) {
    memset(d, 0, sizeof(*d));
    d->emit = emit;
    d->emit_context = emit_context;
}

static inline bool host_disc_active(const HostDisc* d) {
    return d->phase != HostDiscPhaseIdle;
}

/** Begin a discovery. Returns the first request to send. */
static inline HostStep host_disc_start(HostDisc* d) {
    HostDiscEmit emit = d->emit;
    void* ctx = d->emit_context;
    host_disc_init(d, emit, ctx);
    d->phase = HostDiscPhaseServices;
    return host_step(HostStepServices, 0x0001, 0xFFFF, 0);
}

static inline void host_disc_emit(HostDisc* d, const HostDiscItem* item) {
    if(d->emit) d->emit(d->emit_context, item);
}

/** Feed an ATT Read By Group Type response (any number of entries). */
static inline void
    host_disc_on_services(HostDisc* d, const uint8_t* data, uint8_t data_len, uint8_t item_len) {
    if(d->phase != HostDiscPhaseServices || (item_len != 6 && item_len != 20)) return;
    for(uint8_t pos = 0; pos + item_len <= data_len; pos += item_len) {
        BleHostService service;
        if(!ble_host_decode_service(&data[pos], item_len, &service)) continue;
        if(d->service_count >= BLE_HOST_MAX_SERVICES) {
            d->truncated = true;
            break;
        }
        d->services[d->service_count] = service;
        HostDiscItem item;
        host_disc_item_init(&item, HostDiscItemService, d->service_count);
        item.service = service;
        d->service_count++;
        host_disc_emit(d, &item);
    }
}

/** Feed an ATT Read By Type response (characteristic declarations of the current service). */
static inline void
    host_disc_on_chars(HostDisc* d, const uint8_t* data, uint8_t data_len, uint8_t item_len) {
    if(d->phase != HostDiscPhaseChars || (item_len != 7 && item_len != 21)) return;
    for(uint8_t pos = 0; pos + item_len <= data_len; pos += item_len) {
        BleHostCharacteristic characteristic;
        if(!ble_host_decode_char(&data[pos], item_len, &characteristic)) continue;
        if(d->char_count >= BLE_HOST_MAX_CHARS) {
            d->truncated = true;
            break;
        }
        d->chars[d->char_count++] = characteristic;
        HostDiscItem item;
        host_disc_item_init(&item, HostDiscItemCharacteristic, d->service_index);
        item.characteristic = characteristic;
        host_disc_emit(d, &item);
    }
}

/** Feed an ATT Find Information response (descriptors of the current characteristic). */
static inline void
    host_disc_on_descs(HostDisc* d, const uint8_t* data, uint8_t data_len, uint8_t format) {
    if(d->phase != HostDiscPhaseDescs || d->char_index >= d->char_count) return;
    if(format != 1 && format != 2) return;
    uint8_t item_len = format == 1 ? 4 : 18;
    const BleHostCharacteristic* owner = &d->chars[d->char_index];
    for(uint8_t pos = 0; pos + item_len <= data_len; pos += item_len) {
        HostDiscItem item;
        host_disc_item_init(&item, HostDiscItemDescriptor, d->service_index);
        item.char_value_handle = owner->value_handle;
        if(!ble_host_decode_desc(&data[pos], item_len, &item.descriptor)) continue;
        // the response can also list the value attribute itself and the next declarations
        if(item.descriptor.handle <= owner->value_handle) continue;
        uint16_t u = ble_host_uuid16(&item.descriptor.uuid);
        if(u >= 0x2800 && u <= 0x2803) continue; // service / include / characteristic declaration
        host_disc_emit(d, &item);
    }
}

/* Find the next characteristic of the current service that has handles left for descriptors,
 * otherwise move on to the next service. */
static inline HostStep host_disc_next(HostDisc* d) {
    HostStep step = host_step(HostStepDone, 0, 0, 0);
    while(true) {
        if(d->phase == HostDiscPhaseDescs) {
            const BleHostService* svc = &d->services[d->service_index];
            while(d->char_index < d->char_count) {
                const BleHostCharacteristic* c = &d->chars[d->char_index];
                uint16_t first = c->value_handle + 1;
                uint16_t last = (d->char_index + 1 < d->char_count) ?
                                    d->chars[d->char_index + 1].declaration_handle - 1 :
                                    svc->end_handle;
                if(first <= last) {
                    step.kind = HostStepDescs;
                    step.start = first;
                    step.end = last;
                    return step;
                }
                d->char_index++;
            }
            d->service_index++;
        } else {
            // Services just finished, or a service has been completed: set up the next one
        }
        if(d->service_index >= d->service_count) {
            d->phase = HostDiscPhaseIdle;
            step.kind = HostStepDone;
            return step;
        }
        const BleHostService* next = &d->services[d->service_index];
        d->phase = HostDiscPhaseChars;
        d->char_count = 0;
        d->char_index = 0;
        step.kind = HostStepChars;
        step.start = next->start_handle;
        step.end = next->end_handle;
        return step;
    }
}

/** Feed an ACI "GATT procedure complete" with its error code (0 = success).
 * Returns the next request to send, or Done / Failed. */
static inline HostStep host_disc_on_complete(HostDisc* d, uint8_t error_code) {
    HostStep step = host_step(HostStepNone, 0, 0, 0);
    switch(d->phase) {
    case HostDiscPhaseServices:
        if(error_code != 0) {
            d->phase = HostDiscPhaseIdle;
            step.kind = HostStepFailed;
            step.status = error_code;
            return step;
        }
        d->service_index = 0;
        return host_disc_next(d); // enters Chars for service 0, or Done when there are none
    case HostDiscPhaseChars:
        if(error_code != 0) {
            d->phase = HostDiscPhaseIdle;
            step.kind = HostStepFailed;
            step.status = error_code;
            return step;
        }
        d->phase = HostDiscPhaseDescs;
        d->char_index = 0;
        return host_disc_next(d);
    case HostDiscPhaseDescs:
        // A descriptor request that errors out only costs us that characteristic's descriptors
        d->char_index++;
        return host_disc_next(d);
    default:
        return step;
    }
}

#ifdef __cplusplus
}
#endif
