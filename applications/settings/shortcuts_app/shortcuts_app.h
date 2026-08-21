#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/text_input.h>

#include <shortcuts/shortcuts.h>

#include "scenes/shortcuts_app_scene.h"

typedef struct {
    Shortcuts* shortcuts;
    Gui* gui;
    SceneManager* scene_manager;
    ViewDispatcher* view_dispatcher;
    VariableItemList* var_item_list;
    DialogEx* dialog_ex;
    TextInput* text_input;

    // Scratch state for the Add scene
    ShortcutTrigger new_trigger;
    uint8_t new_pin1;
    uint8_t new_pin2;
    ShortcutAction new_action;
    char new_app_name[SHORTCUT_APP_NAME_LEN];
    uint8_t new_hour;
    uint8_t new_minute;

    // Index of the shortcut targeted by the item action / delete confirmation
    size_t selected_index;
} ShortcutsApp;

typedef enum {
    ShortcutsAppViewVarItemList,
    ShortcutsAppViewDialogEx,
    ShortcutsAppViewTextInput,
} ShortcutsAppView;
