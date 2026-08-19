#pragma once

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_SHORTCUTS "shortcuts"
#define SHORTCUTS_MAX 16

typedef struct Shortcuts Shortcuts;

typedef enum {
    ShortcutTriggerSdCardRemoved,
    ShortcutTriggerGpioPull,
    ShortcutTriggerPluggedIn,
    ShortcutTriggerUnplugged,
    ShortcutTriggerCount,
} ShortcutTrigger;

typedef enum {
    ShortcutActionTriggerAlarm,
    ShortcutActionBlinkLed,
    ShortcutActionMakeSound,
    ShortcutActionCount,
} ShortcutAction;

typedef struct {
    ShortcutTrigger trigger;
    uint8_t gpio_pin1;
    uint8_t gpio_pin2;
    ShortcutAction action;
} ShortcutRule;

/** Number of shortcuts currently configured */
size_t shortcuts_get_count(Shortcuts* instance);

/** Get a copy of the shortcut at index. Returns false if index is out of range. */
bool shortcuts_get(Shortcuts* instance, size_t index, ShortcutRule* out_rule);

/** Append a new shortcut. Returns false if the list is full. Persists to storage. */
bool shortcuts_add(Shortcuts* instance, const ShortcutRule* rule);

/** Remove the shortcut at index. Persists to storage. */
void shortcuts_remove(Shortcuts* instance, size_t index);

#ifdef __cplusplus
}
#endif
