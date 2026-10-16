#pragma once

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC_LAYOUT_SLOTS 8
#define CC_LAYOUT_PATH  INT_PATH(".cc_layout.bin")

/** Everything a Control Center tile can do. Order is stable: it is stored on flash. */
typedef enum {
    CcActionLefthanded,
    CcActionSettings,
    CcActionDarkMode,
    CcActionLock,
    CcActionBluetooth,
    CcActionAngularity,
    CcActionDummyMode,
    CcActionComputerMode,
    CcActionPseudoOff,
    CcActionStealth,
    CcActionDiagnostics,
    CcActionSdHealth,
    CcActionProfile,
    CcActionCount,
} CcAction;

const char* cc_action_name(CcAction action);

/** Action assigned to a tile slot (0-7, column-major: slot 0/1 are the first column). */
CcAction cc_layout_get(uint8_t slot);

void cc_layout_set(uint8_t slot, CcAction action);

/** Restore the default tile layout (saved immediately). */
void cc_layout_reset(void);

/** Persist the current layout. */
void cc_layout_save(void);

/** Re-read the layout from flash (call after settings may have changed it). */
void cc_layout_reload(void);

#ifdef __cplusplus
}
#endif
