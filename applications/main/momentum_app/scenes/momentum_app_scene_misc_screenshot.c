#include "../momentum_app.h"
#include <momentum/screenshot.h>

static void momentum_app_scene_misc_screenshot_enabled_changed(VariableItem* item) {
    bool value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, value ? "ON" : "OFF");
    screenshot_set_enabled(value);
}

static void momentum_app_scene_misc_screenshot_combo_changed(VariableItem* item) {
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, screenshot_combo_name(index));
    screenshot_set_combo(index);
}

void momentum_app_scene_misc_screenshot_on_enter(void* context) {
    MomentumApp* app = context;
    VariableItemList* var_item_list = app->var_item_list;
    VariableItem* item;

    item = variable_item_list_add(
        var_item_list,
        "Screenshot shortcut",
        2,
        momentum_app_scene_misc_screenshot_enabled_changed,
        app);
    bool enabled = screenshot_is_enabled();
    variable_item_set_current_value_index(item, enabled);
    variable_item_set_current_value_text(item, enabled ? "ON" : "OFF");

    item = variable_item_list_add(
        var_item_list,
        "Hold keys (1 s)",
        SCREENSHOT_COMBO_COUNT,
        momentum_app_scene_misc_screenshot_combo_changed,
        app);
    uint8_t combo = screenshot_get_combo();
    variable_item_set_current_value_index(item, combo);
    variable_item_set_current_value_text(item, screenshot_combo_name(combo));

    item = variable_item_list_add(var_item_list, "Saved to", 0, NULL, app);
    variable_item_set_current_value_text(item, "/screenshots");

    view_dispatcher_switch_to_view(app->view_dispatcher, MomentumAppViewVarItemList);
}

bool momentum_app_scene_misc_screenshot_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void momentum_app_scene_misc_screenshot_on_exit(void* context) {
    MomentumApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
