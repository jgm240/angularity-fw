#include "cc_layout.h"

#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#define CC_LAYOUT_MAGIC   0x5C
#define CC_LAYOUT_VERSION 1

static const uint8_t cc_default_layout[CC_LAYOUT_SLOTS] = {
    CcActionLefthanded,
    CcActionSettings,
    CcActionDarkMode,
    CcActionLock,
    CcActionBluetooth,
    CcActionAngularity,
    CcActionDummyMode,
    CcActionComputerMode,
};

static const char* const cc_action_names[CcActionCount] = {
    [CcActionLefthanded] = "Left-handed",
    [CcActionSettings] = "Settings",
    [CcActionDarkMode] = "Dark mode",
    [CcActionLock] = "Lock",
    [CcActionBluetooth] = "Bluetooth",
    [CcActionAngularity] = "Angularity",
    [CcActionDummyMode] = "Dummy mode",
    [CcActionComputerMode] = "Computer Mode",
    [CcActionPseudoOff] = "Pseudo-Off",
    [CcActionStealth] = "Stealth",
    [CcActionDiagnostics] = "Diagnostics",
    [CcActionSdHealth] = "SD Health",
    [CcActionProfile] = "Next profile",
};

static uint8_t cc_layout[CC_LAYOUT_SLOTS];
static bool cc_layout_loaded = false;

const char* cc_action_name(CcAction action) {
    return action < CcActionCount ? cc_action_names[action] : "?";
}

void cc_layout_reload(void) {
    uint8_t loaded[CC_LAYOUT_SLOTS];
    bool ok = saved_struct_load(
        CC_LAYOUT_PATH, loaded, sizeof(loaded), CC_LAYOUT_MAGIC, CC_LAYOUT_VERSION);
    for(size_t i = 0; ok && i < CC_LAYOUT_SLOTS; i++) {
        if(loaded[i] >= CcActionCount) ok = false;
    }
    memcpy(cc_layout, ok ? loaded : cc_default_layout, sizeof(cc_layout));
    cc_layout_loaded = true;
}

CcAction cc_layout_get(uint8_t slot) {
    if(!cc_layout_loaded) cc_layout_reload();
    return slot < CC_LAYOUT_SLOTS ? (CcAction)cc_layout[slot] : CcActionSettings;
}

void cc_layout_set(uint8_t slot, CcAction action) {
    if(!cc_layout_loaded) cc_layout_reload();
    if(slot < CC_LAYOUT_SLOTS && action < CcActionCount) {
        cc_layout[slot] = action;
    }
}

void cc_layout_save(void) {
    if(!cc_layout_loaded) cc_layout_reload();
    saved_struct_save(
        CC_LAYOUT_PATH, cc_layout, sizeof(cc_layout), CC_LAYOUT_MAGIC, CC_LAYOUT_VERSION);
}

void cc_layout_reset(void) {
    memcpy(cc_layout, cc_default_layout, sizeof(cc_layout));
    cc_layout_loaded = true;
    cc_layout_save();
}
