#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** Values of BtSettings.mode, see BtMode in bt_service/bt.h */
#define BT_SETTINGS_MODE_PERIPHERAL 0
#define BT_SETTINGS_MODE_HOST       1

typedef struct {
    bool enabled;
    uint8_t mode;
} BtSettings;

void bt_settings_load(BtSettings* bt_settings);

void bt_settings_save(const BtSettings* bt_settings);

#ifdef __cplusplus
}
#endif
