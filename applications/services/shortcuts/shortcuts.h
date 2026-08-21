#pragma once

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_SHORTCUTS "shortcuts"
#define SHORTCUTS_MAX 16
#define SHORTCUT_APP_NAME_LEN 32

typedef struct Shortcuts Shortcuts;

typedef enum {
    ShortcutTriggerSdCardRemoved,
    ShortcutTriggerGpioPull,
    ShortcutTriggerPluggedIn,
    ShortcutTriggerUnplugged,
    ShortcutTriggerNfcScanned,
    ShortcutTriggerScheduledTime,
    ShortcutTriggerCount,
} ShortcutTrigger;

typedef enum {
    ShortcutActionTriggerAlarm,
    ShortcutActionBlinkLed,
    ShortcutActionMakeSound,
    ShortcutActionLaunchApp,
    ShortcutActionToggleDarkMode,
    ShortcutActionCount,
} ShortcutAction;

typedef struct {
    ShortcutTrigger trigger;
    // For GpioPull/GpioTether: gpio_pins[] indices.
    // For ScheduledTime: gpio_pin1 = hour (0-23), gpio_pin2 = minute (0-59).
    uint8_t gpio_pin1;
    uint8_t gpio_pin2;
    ShortcutAction action;
    // Only used when action == ShortcutActionLaunchApp; matches an app's
    // FlipperAppType `name` field (e.g. "NFC", "125 kHz RFID") exactly.
    char app_name[SHORTCUT_APP_NAME_LEN];
} ShortcutRule;

/** Number of shortcuts currently configured */
size_t shortcuts_get_count(Shortcuts* instance);

/** Get a copy of the shortcut at index. Returns false if index is out of range. */
bool shortcuts_get(Shortcuts* instance, size_t index, ShortcutRule* out_rule);

/** Append a new shortcut. Returns false if the list is full. Persists to storage. */
bool shortcuts_add(Shortcuts* instance, const ShortcutRule* rule);

/** Remove the shortcut at index. Persists to storage. */
void shortcuts_remove(Shortcuts* instance, size_t index);

/** Run all shortcuts configured for the NFC Scanned trigger. */
void shortcuts_notify_nfc_scanned(Shortcuts* instance);

/** Manually execute the action of the shortcut at index, ignoring its trigger. */
void shortcuts_run(Shortcuts* instance, size_t index);

#ifdef __cplusplus
}
#endif
