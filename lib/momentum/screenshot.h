#pragma once

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCREENSHOT_PATH_DIR     EXT_PATH("screenshots")
#define SCREENSHOT_SETTINGS_PATH INT_PATH(".screenshot.bin")
#define SCREENSHOT_COMBO_COUNT  4

/** Start the screenshot service (called once by the desktop service). Safe to call again. */
void screenshot_start(void);

bool screenshot_is_enabled(void);
void screenshot_set_enabled(bool enabled);

/** Which keys, held together for about a second, take a screenshot. */
uint8_t screenshot_get_combo(void);
void screenshot_set_combo(uint8_t combo);
const char* screenshot_combo_name(uint8_t combo);

/** Save the current screen now (used by the hold-keys shortcut). Returns false on failure. */
bool screenshot_take(void);

#ifdef __cplusplus
}
#endif
