#include "../shortcuts_app.h"

static const char* const shortcut_trigger_names[ShortcutTriggerCount] = {
    [ShortcutTriggerSdCardRemoved] = "SD Removed",
    [ShortcutTriggerGpioPull] = "GPIO Pull",
    [ShortcutTriggerPluggedIn] = "Plugged In",
    [ShortcutTriggerUnplugged] = "Unplugged",
};

static const char* const shortcut_action_names[ShortcutActionCount] = {
    [ShortcutActionTriggerAlarm] = "Trigger Alarm",
    [ShortcutActionBlinkLed] = "Blink LED",
    [ShortcutActionMakeSound] = "Make Sound",
};

void shortcuts_app_scene_start_var_item_list_callback(void* context, uint32_t index) {
    ShortcutsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void shortcuts_app_scene_start_on_enter(void* context) {
    ShortcutsApp* app = context;
    VariableItemList* var_item_list = app->var_item_list;
    VariableItem* item;

    size_t count = shortcuts_get_count(app->shortcuts);
    FuriString* label = furi_string_alloc();
    for(size_t i = 0; i < count; i++) {
        ShortcutRule rule;
        shortcuts_get(app->shortcuts, i, &rule);
        furi_string_printf(
            label,
            "%s->%s",
            shortcut_trigger_names[rule.trigger],
            shortcut_action_names[rule.action]);
        item = variable_item_list_add(var_item_list, furi_string_get_cstr(label), 0, NULL, app);
        variable_item_set_current_value_text(item, ">");
    }
    furi_string_free(label);

    item = variable_item_list_add(var_item_list, "+ Add Shortcut", 0, NULL, app);
    variable_item_set_current_value_text(item, ">");

    variable_item_list_set_enter_callback(
        var_item_list, shortcuts_app_scene_start_var_item_list_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, ShortcutsAppViewVarItemList);
}

bool shortcuts_app_scene_start_on_event(void* context, SceneManagerEvent event) {
    ShortcutsApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        size_t count = shortcuts_get_count(app->shortcuts);
        if(event.event == count) {
            // "+ Add Shortcut" tapped
            app->new_trigger = ShortcutTriggerUnplugged;
            app->new_pin1 = 0;
            app->new_pin2 = 1;
            app->new_action = ShortcutActionMakeSound;
            scene_manager_next_scene(app->scene_manager, ShortcutsAppSceneAdd);
        } else if(event.event < count) {
            app->delete_index = event.event;
            scene_manager_next_scene(app->scene_manager, ShortcutsAppSceneConfirmDelete);
        }
        consumed = true;
    }

    return consumed;
}

void shortcuts_app_scene_start_on_exit(void* context) {
    ShortcutsApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
