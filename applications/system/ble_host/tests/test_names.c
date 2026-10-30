/* Unit tests for the Scanner's name tables and formatting helpers. Run with tests/run.sh */
#include <stdio.h>
#include <stdlib.h>
#include "ble_names.h"

static int fails;
#define CHECK(c) \
    do { \
        if(!(c)) { \
            printf("FAIL line %d: %s\n", __LINE__, #c); \
            fails++; \
        } \
    } while(0)

int main(void) {
    char b[64];
    const uint8_t u16[2] = {0x0F, 0x18};
    ble_format_uuid(u16, 2, b, sizeof(b));
    CHECK(strcmp(b, "180F") == 0);
    const uint8_t nus[16] = {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                             0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E};
    ble_format_uuid(nus, 16, b, sizeof(b));
    CHECK(strcmp(b, "6E400001-B5A3-F393-E0A9-E50E24DCCA9E") == 0);
    char tiny[5];
    ble_format_uuid(nus, 16, tiny, sizeof(tiny));
    CHECK(strlen(tiny) == 4); /* truncates safely */
    ble_format_uuid(nus, 5, b, sizeof(b));
    CHECK(b[0] == 0);

    const uint8_t addr[6] = {0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
    ble_format_address(addr, b, sizeof(b));
    CHECK(strcmp(b, "11:22:33:44:55:66") == 0);

    ble_format_properties(0x12, b, sizeof(b));
    CHECK(strcmp(b, "RN") == 0);
    ble_format_properties(0x1A, b, sizeof(b));
    CHECK(strcmp(b, "RWN") == 0);
    ble_format_properties(0x00, b, sizeof(b));
    CHECK(b[0] == 0);
    ble_format_properties(0xFF, b, sizeof(b));
    CHECK(strlen(b) == 7);
    ble_format_properties(0xFF, b, 4);
    CHECK(strlen(b) == 3);

    const uint8_t hi[2] = {'H', 'i'};
    ble_format_value(hi, 2, b, sizeof(b));
    CHECK(strcmp(b, "48 69 | Hi") == 0);
    const uint8_t bin[3] = {0x01, 0x02, 0xFF};
    ble_format_value(bin, 3, b, sizeof(b));
    CHECK(strcmp(b, "01 02 FF") == 0);
    ble_format_value(bin, 0, b, sizeof(b));
    CHECK(b[0] == 0);
    uint8_t big[100];
    for(int i = 0; i < 100; i++) big[i] = (uint8_t)i;
    for(size_t size = 1; size < 40; size++) {
        char o[40];
        memset(o, 'X', sizeof(o));
        ble_format_value(big, 100, o, size);
        CHECK(strlen(o) < size);
        CHECK(o[size] == 'X' || size >= sizeof(o)); /* nothing written past the buffer */
    }

    CHECK(strcmp(ble_service_name(0x180F), "Battery") == 0);
    CHECK(ble_service_name(0x1234) == NULL);
    CHECK(strcmp(ble_characteristic_name(0x2A19), "Battery Level") == 0);
    CHECK(strcmp(ble_company_name(0x004C), "Apple") == 0);

    printf(fails ? "FAILED\n" : "name/format tests passed\n");
    return fails != 0;
}
