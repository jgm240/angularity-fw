#include "../momentum_app.h"

enum VarItemListIndex {
    VarItemListIndexSound,
    VarItemListIndexLed,
    VarItemListIndexLock,
};

void momentum_app_scene_misc_power_result_var_item_list_callback(void* context, uint32_t index) {
    MomentumApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void momentum_app_scene_misc_power_result_sound_changed(VariableItem* item) {
    MomentumApp* app = variable_item_get_context(item);
    bool value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, value ? "ON" : "OFF");
    momentum_settings.theft_alarm_result_sound = value;
    app->save_settings = true;
}

static void momentum_app_scene_misc_power_result_led_changed(VariableItem* item) {
    MomentumApp* app = variable_item_get_context(item);
    bool value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, value ? "ON" : "OFF");
    momentum_settings.theft_alarm_result_led = value;
    app->save_settings = true;
}

static void momentum_app_scene_misc_power_result_lock_changed(VariableItem* item) {
    MomentumApp* app = variable_item_get_context(item);
    bool value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, value ? "ON" : "OFF");
    momentum_settings.theft_alarm_result_lock = value;
    app->save_settings = true;
}

void momentum_app_scene_misc_power_result_on_enter(void* context) {
    MomentumApp* app = context;
    VariableItemList* var_item_list = app->var_item_list;
    VariableItem* item;

    item = variable_item_list_add(
        var_item_list, "Sound", 2, momentum_app_scene_misc_power_result_sound_changed, app);
    variable_item_set_current_value_index(item, momentum_settings.theft_alarm_result_sound);
    variable_item_set_current_value_text(
        item, momentum_settings.theft_alarm_result_sound ? "ON" : "OFF");

    item = variable_item_list_add(
        var_item_list, "LED", 2, momentum_app_scene_misc_power_result_led_changed, app);
    variable_item_set_current_value_index(item, momentum_settings.theft_alarm_result_led);
    variable_item_set_current_value_text(
        item, momentum_settings.theft_alarm_result_led ? "ON" : "OFF");

    item = variable_item_list_add(
        var_item_list,
        "Lock if PIN set",
        2,
        momentum_app_scene_misc_power_result_lock_changed,
        app);
    variable_item_set_current_value_index(item, momentum_settings.theft_alarm_result_lock);
    variable_item_set_current_value_text(
        item, momentum_settings.theft_alarm_result_lock ? "ON" : "OFF");

    variable_item_list_set_enter_callback(
        var_item_list, momentum_app_scene_misc_power_result_var_item_list_callback, app);

    variable_item_list_set_selected_item(
        var_item_list,
        scene_manager_get_scene_state(app->scene_manager, MomentumAppSceneMiscPowerResult));

    view_dispatcher_switch_to_view(app->view_dispatcher, MomentumAppViewVarItemList);
}

bool momentum_app_scene_misc_power_result_on_event(void* context, SceneManagerEvent event) {
    MomentumApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(
            app->scene_manager, MomentumAppSceneMiscPowerResult, event.event);
        consumed = true;
    }

    return consumed;
}

void momentum_app_scene_misc_power_result_on_exit(void* context) {
    MomentumApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
