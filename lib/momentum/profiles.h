#pragma once

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROFILES_COUNT   3
#define PROFILE_NONE     0xFF
#define PROFILES_PATH    INT_PATH(".profiles.bin")

/** Name of a profile slot ("Home", "Work", "Travel"). */
const char* profiles_name(uint8_t slot);

/** True once something has been saved into the slot. */
bool profiles_is_saved(uint8_t slot);

/** Snapshot the current screen/sound/LED/vibro/Bluetooth/dark-mode/theft-alarm
 * settings into the slot. */
void profiles_save_current(uint8_t slot);

/** Apply a saved slot to the running system and persist the affected settings.
 * Returns false if the slot is empty. */
bool profiles_apply(uint8_t slot);

/** Slot that was applied last, or PROFILE_NONE. */
uint8_t profiles_get_active(void);

/** Apply the next saved slot after the active one. Returns the applied slot,
 * or PROFILE_NONE if nothing is saved. */
uint8_t profiles_cycle(void);

#ifdef __cplusplus
}
#endif
