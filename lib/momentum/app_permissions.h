#pragma once

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_PERMISSIONS_PATH INT_PATH(".app_permissions.bin")

/** What a third-party app can ask to use. Bit flags. */
typedef enum {
    AppPermissionRadio = (1 << 0), /**< Sub-GHz */
    AppPermissionGpio = (1 << 1), /**< GPIO pins, UART, I2C, SPI, PWM, ADC */
    AppPermissionUsb = (1 << 2), /**< USB / HID */
    AppPermissionBluetooth = (1 << 3), /**< Bluetooth LE */
    AppPermissionRfid = (1 << 4), /**< 125 kHz RFID, iButton */
    AppPermissionNfc = (1 << 5), /**< 13.56 MHz NFC */
    AppPermissionInfrared = (1 << 6), /**< Infrared */
} AppPermission;

#define APP_PERMISSIONS_ALL 0x7F

/** Whether launching apps asks for permission (default: off). */
bool app_permissions_is_enabled(void);
void app_permissions_set_enabled(bool enabled);

/** Which permission kinds the user wants to be asked about (bit mask, default all).
 * Kinds that are switched off are never prompted for. */
uint32_t app_permissions_get_watched(void);
void app_permissions_set_watched(uint32_t watched);

/** Number of apps with a remembered answer. */
uint32_t app_permissions_count(void);

/** Forget all remembered answers (the on/off switch is kept). */
void app_permissions_forget_all(void);

/** Map an imported symbol name to the permission it implies (0 if none). */
uint32_t app_permissions_classify(const char* symbol_name);

/** Look up a remembered answer for an app. Returns false if the app is unknown
 * or its file changed size since it was recorded. */
bool app_permissions_lookup(
    const char* path,
    uint32_t file_size,
    uint32_t* requested,
    uint32_t* allowed);

/** Remember what an app requests and which of it the user allowed. */
void app_permissions_store(
    const char* path,
    uint32_t file_size,
    uint32_t requested,
    uint32_t allowed);

/** Human readable list such as "Radio, GPIO". */
void app_permissions_describe(uint32_t mask, FuriString* out);

#ifdef __cplusplus
}
#endif
