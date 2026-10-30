#pragma once

/* Names and formatting for what a BLE scan shows. Pure functions, no Furi dependencies. */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t id;
    const char* name;
} BleName16;

/* Assigned numbers (Bluetooth SIG) for the services and characteristics most devices expose */
static const BleName16 ble_service_names[] = {
    {0x1800, "Generic Access"},
    {0x1801, "Generic Attribute"},
    {0x1802, "Immediate Alert"},
    {0x1803, "Link Loss"},
    {0x1804, "Tx Power"},
    {0x1805, "Current Time"},
    {0x180A, "Device Information"},
    {0x180D, "Heart Rate"},
    {0x180F, "Battery"},
    {0x1810, "Blood Pressure"},
    {0x1812, "Human Interface Device"},
    {0x1816, "Cycling Speed/Cadence"},
    {0x1818, "Cycling Power"},
    {0x181A, "Environmental Sensing"},
    {0x181C, "User Data"},
    {0x181D, "Weight Scale"},
    {0x1826, "Fitness Machine"},
    {0xFE2C, "Google Fast Pair"},
    {0xFD6F, "Exposure Notification"},
};

static const BleName16 ble_characteristic_names[] = {
    {0x2A00, "Device Name"},
    {0x2A01, "Appearance"},
    {0x2A04, "Preferred Conn Params"},
    {0x2A05, "Service Changed"},
    {0x2A19, "Battery Level"},
    {0x2A24, "Model Number"},
    {0x2A25, "Serial Number"},
    {0x2A26, "Firmware Revision"},
    {0x2A27, "Hardware Revision"},
    {0x2A28, "Software Revision"},
    {0x2A29, "Manufacturer Name"},
    {0x2A37, "Heart Rate"},
    {0x2A38, "Body Sensor Location"},
    {0x2A39, "Heart Rate Control"},
    {0x2A4A, "HID Information"},
    {0x2A4B, "Report Map"},
    {0x2A4C, "HID Control Point"},
    {0x2A4D, "Report"},
    {0x2A50, "PnP ID"},
    {0x2A6E, "Temperature"},
    {0x2A6F, "Humidity"},
    {0x2A2B, "Current Time"},
    {0x2902, "Client Config"},
};

/* A few Bluetooth SIG company identifiers (manufacturer data) */
static const BleName16 ble_company_names[] = {
    {0x0002, "Intel"},
    {0x0006, "Microsoft"},
    {0x000D, "Texas Instruments"},
    {0x000F, "Broadcom"},
    {0x004C, "Apple"},
    {0x0059, "Nordic"},
    {0x0075, "Samsung"},
    {0x0087, "Garmin"},
    {0x009E, "Bose"},
    {0x00E0, "Google"},
    {0x012D, "Sony"},
    {0x0171, "Amazon"},
    {0x02E5, "Espressif"},
    {0x038F, "Xiaomi"},
    {0x0499, "Ruuvi"},
};

#define BLE_NAME_COUNT(table) (sizeof(table) / sizeof((table)[0]))

static inline const char* ble_name_lookup(const BleName16* table, size_t count, uint16_t id) {
    for(size_t i = 0; i < count; i++) {
        if(table[i].id == id) return table[i].name;
    }
    return NULL;
}

#define ble_service_name(id)        ble_name_lookup(ble_service_names, BLE_NAME_COUNT(ble_service_names), id)
#define ble_characteristic_name(id) ble_name_lookup(ble_characteristic_names, BLE_NAME_COUNT(ble_characteristic_names), id)
#define ble_company_name(id)        ble_name_lookup(ble_company_names, BLE_NAME_COUNT(ble_company_names), id)

/* "180F" or "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" from the on-air (little endian) bytes.
 * size is 2 or 16. out needs 37 bytes. */
static inline void ble_format_uuid(const uint8_t* bytes, uint8_t size, char* out, size_t out_size) {
    if(out_size == 0) return;
    if(size == 2) {
        snprintf(out, out_size, "%02X%02X", bytes[1], bytes[0]);
    } else if(size == 16) {
        snprintf(
            out,
            out_size,
            "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
            bytes[15], bytes[14], bytes[13], bytes[12], bytes[11], bytes[10], bytes[9],
            bytes[8], bytes[7], bytes[6], bytes[5], bytes[4], bytes[3], bytes[2], bytes[1],
            bytes[0]);
    } else {
        out[0] = '\0';
    }
}

/* "AA:BB:CC:DD:EE:FF" from the on-air (little endian) address. out needs 18 bytes. */
static inline void ble_format_address(const uint8_t address[6], char* out, size_t out_size) {
    snprintf(
        out,
        out_size,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        address[5], address[4], address[3], address[2], address[1], address[0]);
}

/* "R W N" style summary of characteristic properties. out needs 12 bytes. */
static inline void ble_format_properties(uint8_t properties, char* out, size_t out_size) {
    size_t n = 0;
    const struct {
        uint8_t bit;
        char letter;
    } flags[] = {
        {0x02, 'R'}, {0x08, 'W'}, {0x04, 'w'}, {0x10, 'N'}, {0x20, 'I'}, {0x40, 'S'}, {0x01, 'B'},
    };
    for(size_t i = 0; i < sizeof(flags) / sizeof(flags[0]) && n + 1 < out_size; i++) {
        if(properties & flags[i].bit) out[n++] = flags[i].letter;
    }
    if(out_size) out[n] = '\0';
}

/* Hex bytes with an ASCII column for printable text: "48 69 | Hi". Truncates with "..". */
static inline void ble_format_value(const uint8_t* data, uint8_t length, char* out, size_t out_size) {
    if(out_size == 0) return;
    size_t n = 0;
    out[0] = '\0';
    bool printable = length > 0;
    for(uint8_t i = 0; i < length; i++) {
        if(data[i] < 0x20 || data[i] >= 0x7F) printable = false;
    }
    for(uint8_t i = 0; i < length; i++) {
        if(n + 4 >= out_size) {
            if(n + 3 < out_size) {
                out[n++] = '.';
                out[n++] = '.';
                out[n] = '\0';
            }
            return;
        }
        n += (size_t)snprintf(out + n, out_size - n, i ? " %02X" : "%02X", data[i]);
    }
    if(printable && n + 5 < out_size) {
        n += (size_t)snprintf(out + n, out_size - n, " | ");
        for(uint8_t i = 0; i < length && n + 1 < out_size; i++) out[n++] = (char)data[i];
        out[n] = '\0';
    }
}
