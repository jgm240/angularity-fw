#include "bt_settings.h"
#include "bt_settings_filename.h"

#include <furi.h>

#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#define TAG "BtSettings"

#define BT_SETTINGS_VERSION    (1)
#define BT_SETTINGS_VERSION_V0 (0)
#define BT_SETTINGS_MAGIC      (0x19)

// Layout of the settings file before the mode was added
typedef struct {
    bool enabled;
} BtSettingsV0;

void bt_settings_load(BtSettings* bt_settings) {
    furi_assert(bt_settings);

    bool load_success = saved_struct_load(
        BT_SETTINGS_PATH, bt_settings, sizeof(BtSettings), BT_SETTINGS_MAGIC, BT_SETTINGS_VERSION);

    if(!load_success) {
        // Keep the user's Bluetooth on/off choice from before the mode setting existed
        BtSettingsV0 old;
        if(saved_struct_load(
               BT_SETTINGS_PATH,
               &old,
               sizeof(BtSettingsV0),
               BT_SETTINGS_MAGIC,
               BT_SETTINGS_VERSION_V0)) {
            bt_settings->enabled = old.enabled;
            bt_settings->mode = BT_SETTINGS_MODE_PERIPHERAL;
            load_success = true;
        }
    }

    if(!load_success) {
        FURI_LOG_W(TAG, "Failed to load settings, using defaults");

        bt_settings->enabled = false;
        bt_settings->mode = BT_SETTINGS_MODE_PERIPHERAL;
        // bt_settings_save(bt_settings);
    }

    if(bt_settings->mode > BT_SETTINGS_MODE_HOST) {
        bt_settings->mode = BT_SETTINGS_MODE_PERIPHERAL;
    }
}

void bt_settings_save(const BtSettings* bt_settings) {
    furi_assert(bt_settings);

    const bool success = saved_struct_save(
        BT_SETTINGS_PATH, bt_settings, sizeof(BtSettings), BT_SETTINGS_MAGIC, BT_SETTINGS_VERSION);

    if(!success) {
        FURI_LOG_E(TAG, "Failed to save settings");
    }
}
