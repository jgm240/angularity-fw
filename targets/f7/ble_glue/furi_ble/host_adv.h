#pragma once

/**
 * @file host_adv.h
 * Parser for BLE advertising / scan response data (a sequence of length-type-value structures,
 * Bluetooth Core Spec Vol 3 Part C 11). Header-only and free of Furi / ST dependencies.
 * The data comes from other people's devices: everything is bounds-checked and the name is
 * reduced to printable ASCII so it is safe to draw on screen.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_HOST_ADV_NAME_MAX      32
#define BLE_HOST_ADV_UUID16_MAX    8
#define BLE_HOST_ADV_UUID128_MAX   2
#define BLE_HOST_ADV_MFG_DATA_MAX  27
#define BLE_HOST_ADV_DATA_MAX      31

#define BLE_HOST_AD_FLAGS          0x01
#define BLE_HOST_AD_UUID16_PART    0x02
#define BLE_HOST_AD_UUID16_FULL    0x03
#define BLE_HOST_AD_UUID128_PART   0x06
#define BLE_HOST_AD_UUID128_FULL   0x07
#define BLE_HOST_AD_NAME_SHORT     0x08
#define BLE_HOST_AD_NAME_FULL      0x09
#define BLE_HOST_AD_TX_POWER       0x0A
#define BLE_HOST_AD_APPEARANCE     0x19
#define BLE_HOST_AD_MFG_DATA       0xFF

typedef struct {
    char name[BLE_HOST_ADV_NAME_MAX + 1];
    bool name_complete;

    bool has_flags;
    uint8_t flags;

    bool has_tx_power;
    int8_t tx_power;

    bool has_appearance;
    uint16_t appearance;

    bool has_mfg;
    uint16_t mfg_company_id;
    uint8_t mfg_data[BLE_HOST_ADV_MFG_DATA_MAX];
    uint8_t mfg_data_len;

    uint8_t uuid16_count;
    uint16_t uuid16[BLE_HOST_ADV_UUID16_MAX];
    uint8_t uuid128_count;
    uint8_t uuid128[BLE_HOST_ADV_UUID128_MAX][16]; // little endian, as on the air
} BleHostAdv;

/** Parse advertising data into out. Stops at the first malformed structure, keeping what was
 * read before it.
 * @return false if data was malformed (out holds whatever parsed up to that point). */
static inline bool ble_host_adv_parse(const uint8_t* data, uint8_t length, BleHostAdv* out) {
    memset(out, 0, sizeof(*out));
    uint8_t pos = 0;
    while(pos < length) {
        uint8_t field_len = data[pos];
        if(field_len == 0) return true; // early terminator / padding
        if(pos + 1 + field_len > length) return false;
        uint8_t type = data[pos + 1];
        const uint8_t* value = &data[pos + 2];
        uint8_t value_len = field_len - 1;

        switch(type) {
        case BLE_HOST_AD_FLAGS:
            if(value_len >= 1) {
                out->has_flags = true;
                out->flags = value[0];
            }
            break;
        case BLE_HOST_AD_NAME_SHORT:
        case BLE_HOST_AD_NAME_FULL: {
            // a complete name wins over a shortened one
            if(type == BLE_HOST_AD_NAME_FULL || !out->name_complete) {
                uint8_t n = value_len > BLE_HOST_ADV_NAME_MAX ? BLE_HOST_ADV_NAME_MAX : value_len;
                for(uint8_t i = 0; i < n; i++) {
                    out->name[i] = (value[i] >= 0x20 && value[i] < 0x7F) ? (char)value[i] : '?';
                }
                out->name[n] = '\0';
                out->name_complete = (type == BLE_HOST_AD_NAME_FULL);
            }
            break;
        }
        case BLE_HOST_AD_TX_POWER:
            if(value_len >= 1) {
                out->has_tx_power = true;
                out->tx_power = (int8_t)value[0];
            }
            break;
        case BLE_HOST_AD_APPEARANCE:
            if(value_len >= 2) {
                out->has_appearance = true;
                out->appearance = (uint16_t)(value[0] | (value[1] << 8));
            }
            break;
        case BLE_HOST_AD_MFG_DATA:
            if(value_len >= 2 && !out->has_mfg) {
                out->has_mfg = true;
                out->mfg_company_id = (uint16_t)(value[0] | (value[1] << 8));
                uint8_t n = value_len - 2;
                if(n > BLE_HOST_ADV_MFG_DATA_MAX) n = BLE_HOST_ADV_MFG_DATA_MAX;
                memcpy(out->mfg_data, &value[2], n);
                out->mfg_data_len = n;
            }
            break;
        case BLE_HOST_AD_UUID16_PART:
        case BLE_HOST_AD_UUID16_FULL:
            for(uint8_t i = 0; i + 1 < value_len && out->uuid16_count < BLE_HOST_ADV_UUID16_MAX;
                i += 2) {
                out->uuid16[out->uuid16_count++] = (uint16_t)(value[i] | (value[i + 1] << 8));
            }
            break;
        case BLE_HOST_AD_UUID128_PART:
        case BLE_HOST_AD_UUID128_FULL:
            for(uint8_t i = 0; i + 15 < value_len && out->uuid128_count < BLE_HOST_ADV_UUID128_MAX;
                i += 16) {
                memcpy(out->uuid128[out->uuid128_count++], &value[i], 16);
            }
            break;
        default:
            break;
        }
        pos += 1 + field_len;
    }
    return true;
}

#ifdef __cplusplus
}
#endif
