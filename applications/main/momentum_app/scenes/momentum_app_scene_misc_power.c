#include "../momentum_app.h"
#include <furi_hal_resources.h>

#define GPIO_PIN_CHOICES_MAX 16

enum VarItemListIndex {
    VarItemListIndexChargingSound,
    VarItemListIndexTheftAlarm,
    VarItemListIndexTheftAlarmTrigger,
    VarItemListIndexTheftAlarmPin1,
    VarItemListIndexTheftAlarmPin2,
    VarItemListIndexTheftAlarmResult,
    VarItemListIndexGpioMirrorMode,
    VarItemListIndexGpioMirrorPin,
    VarItemListIndexSecurityLog,
};

static const char* const gpio_mirror_mode_names[GpioMirrorCount] = {
    [GpioMirrorOff] = "Off",
    [GpioMirrorCharging] = "Charging",
    [GpioMirrorLocked] = "Locked",
};

static const char* const theft_alarm_trigger_names[TheftAlarmTriggerCount] = {
    [TheftAlarmTriggerUnplug] = "Unplug",
    [TheftAlarmTriggerGpioPull] = "GPIO Pull",
    [TheftAlarmTriggerGpioTether] = "GPIO Tether",
};

// Raw gpio_pins[] indices for the non-debug (safe, user-wireable) pins,
// built once per scene entry
static uint8_t gpio_pin_choices[GPIO_PIN_CHOICES_MAX];
static uint8_t gpio_pin_choices_count = 0;

static void gpio_pin_choices_build(void) {
    gpio_pin_choices_count = 0;
    for(size_t i = 0; i < gpio_pins_count && gpio_pin_choices_count < GPIO_PIN_CHOICES_MAX; i++) {
        if(!gpio_pins[i].debug) {
            gpio_pin_choices[gpio_pin_choices_count++] = i;
        }
    }
}

static uint8_t gpio_pin_choice_index_for_raw(uint32_t raw_index) {
    for(uint8_t i = 0; i < gpio_pin_choices_count; i++) {
        if(gpio_pin_choices[i] == raw_index) {
            return i;
        }
    }
    return 0;
}

static void momentum_app_scene_misc_power_update_pin_locks(MomentumApp* app) {
    bool gpio_used = (momentum_settings.theft_alarm_trigger == TheftAlarmTriggerGpioPull) ||
                      (momentum_settings.theft_alarm_trigger == TheftAlarmTriggerGpioTether);
    bool pin2_used = (momentum_settings.theft_alarm_trigger == TheftAlarmTriggerGpioPull);

    variable_item_set_locked(
        variable_item_list_get(app->var_item_list, VarItemListIndexTheftAlarmPin1),
        !gpio_used,
        "Needs GPIO Pull\nor Tether trigger!");
    variable_item_set_locked(
        variable_item_list_get(app->var_item_list, VarItemListIndexTheftAlarmPin2),
        !pin2_used,
        "Needs GPIO Pull\ntrigger!");
}

static void momentum_app_scene_misc_power_update_mirror_pin_lock(MomentumApp* app) {
    variable_item_set_locked(
        variable_item_list_get(app->var_item_list, VarItemListIndexGpioMirrorPin),
        momentum_settings.gpio_mirror_mode == GpioMirrorOff,
        "Needs a Mirror\nMode set!");
}

void momentum_app_scene_misc_power_var_item_list_callback(void* context, uint32_t index) {
    MomentumApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void momentum_app_scene_misc_power_charging_sound_changed(VariableItem* item) {
    MomentumApp* app = variable_item_get_context(item);
    bool value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, value ? "ON" : "OFF");
    momentum_settings.charging_sound = value;
    app->save_settings = true;
}

static void momentum_app_scene_misc_power_theft_alarm_changed(VariableItem* item) {
    MomentumApp* app = variable_item_get_context(item);
    bool value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, value ? "ON" : "OFF");
    momentum_settings.theft_alarm = value;
    app->save_settings = true;
}

static void momentum_app_scene_misc_power_trigger_changed(VariableItem* item) {
    MomentumApp* app = variable_item_get_context(item);
    uint8_t value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, theft_alarm_trigger_names[value]);
    momentum_settings.theft_alarm_trigger = value;
    app->save_settings = true;
    momentum_app_scene_misc_power_update_pin_locks(app);
}

static void momentum_app_scene_misc_power_pin1_changed(VariableItem* item) {
    MomentumApp* app = variable_item_get_context(item);
    uint8_t choice = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, gpio_pins[gpio_pin_choices[choice]].name);
    momentum_settings.theft_alarm_gpio_pin1 = gpio_pin_choices[choice];
    app->save_settings = true;
}

static void momentum_app_scene_misc_power_pin2_changed(VariableItem* item) {
    MomentumApp* app = variable_item_get_context(item);
    uint8_t choice = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, gpio_pins[gpio_pin_choices[choice]].name);
    momentum_settings.theft_alarm_gpio_pin2 = gpio_pin_choices[choice];
    app->save_settings = true;
}

static void momentum_app_scene_misc_power_mirror_mode_changed(VariableItem* item) {
    MomentumApp* app = variable_item_get_context(item);
    uint8_t value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, gpio_mirror_mode_names[value]);
    momentum_settings.gpio_mirror_mode = value;
    app->save_settings = true;
    momentum_app_scene_misc_power_update_mirror_pin_lock(app);
}

static void momentum_app_scene_misc_power_mirror_pin_changed(VariableItem* item) {
    MomentumApp* app = variable_item_get_context(item);
    uint8_t choice = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, gpio_pins[gpio_pin_choices[choice]].name);
    momentum_settings.gpio_mirror_pin = gpio_pin_choices[choice];
    app->save_settings = true;
}

void momentum_app_scene_misc_power_on_enter(void* context) {
    MomentumApp* app = context;
    VariableItemList* var_item_list = app->var_item_list;
    VariableItem* item;

    gpio_pin_choices_build();

    item = variable_item_list_add(
        var_item_list,
        "Charging Sound",
        2,
        momentum_app_scene_misc_power_charging_sound_changed,
        app);
    variable_item_set_current_value_index(item, momentum_settings.charging_sound);
    variable_item_set_current_value_text(item, momentum_settings.charging_sound ? "ON" : "OFF");

    item = variable_item_list_add(
        var_item_list, "Theft Alarm", 2, momentum_app_scene_misc_power_theft_alarm_changed, app);
    variable_item_set_current_value_index(item, momentum_settings.theft_alarm);
    variable_item_set_current_value_text(item, momentum_settings.theft_alarm ? "ON" : "OFF");

    item = variable_item_list_add(
        var_item_list,
        "Trigger",
        TheftAlarmTriggerCount,
        momentum_app_scene_misc_power_trigger_changed,
        app);
    variable_item_set_current_value_index(item, momentum_settings.theft_alarm_trigger);
    variable_item_set_current_value_text(
        item, theft_alarm_trigger_names[momentum_settings.theft_alarm_trigger]);

    item = variable_item_list_add(
        var_item_list,
        "GPIO Pin 1",
        gpio_pin_choices_count,
        momentum_app_scene_misc_power_pin1_changed,
        app);
    uint8_t pin1_choice = gpio_pin_choice_index_for_raw(momentum_settings.theft_alarm_gpio_pin1);
    variable_item_set_current_value_index(item, pin1_choice);
    variable_item_set_current_value_text(item, gpio_pins[gpio_pin_choices[pin1_choice]].name);

    item = variable_item_list_add(
        var_item_list,
        "GPIO Pin 2",
        gpio_pin_choices_count,
        momentum_app_scene_misc_power_pin2_changed,
        app);
    uint8_t pin2_choice = gpio_pin_choice_index_for_raw(momentum_settings.theft_alarm_gpio_pin2);
    variable_item_set_current_value_index(item, pin2_choice);
    variable_item_set_current_value_text(item, gpio_pins[gpio_pin_choices[pin2_choice]].name);

    momentum_app_scene_misc_power_update_pin_locks(app);

    item = variable_item_list_add(var_item_list, "Result", 0, NULL, app);
    variable_item_set_current_value_text(item, ">");

    item = variable_item_list_add(
        var_item_list,
        "GPIO Mirror",
        GpioMirrorCount,
        momentum_app_scene_misc_power_mirror_mode_changed,
        app);
    variable_item_set_current_value_index(item, momentum_settings.gpio_mirror_mode);
    variable_item_set_current_value_text(
        item, gpio_mirror_mode_names[momentum_settings.gpio_mirror_mode]);

    item = variable_item_list_add(
        var_item_list,
        "Mirror Pin",
        gpio_pin_choices_count,
        momentum_app_scene_misc_power_mirror_pin_changed,
        app);
    uint8_t mirror_pin_choice =
        gpio_pin_choice_index_for_raw(momentum_settings.gpio_mirror_pin);
    variable_item_set_current_value_index(item, mirror_pin_choice);
    variable_item_set_current_value_text(
        item, gpio_pins[gpio_pin_choices[mirror_pin_choice]].name);

    momentum_app_scene_misc_power_update_mirror_pin_lock(app);

    item = variable_item_list_add(var_item_list, "Security Log", 0, NULL, app);
    variable_item_set_current_value_text(item, ">");

    variable_item_list_set_enter_callback(
        var_item_list, momentum_app_scene_misc_power_var_item_list_callback, app);

    variable_item_list_set_selected_item(
        var_item_list,
        scene_manager_get_scene_state(app->scene_manager, MomentumAppSceneMiscPower));

    view_dispatcher_switch_to_view(app->view_dispatcher, MomentumAppViewVarItemList);
}

bool momentum_app_scene_misc_power_on_event(void* context, SceneManagerEvent event) {
    MomentumApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, MomentumAppSceneMiscPower, event.event);
        consumed = true;
        switch(event.event) {
        case VarItemListIndexTheftAlarmResult:
            scene_manager_set_scene_state(app->scene_manager, MomentumAppSceneMiscPowerResult, 0);
            scene_manager_next_scene(app->scene_manager, MomentumAppSceneMiscPowerResult);
            break;
        case VarItemListIndexSecurityLog:
            scene_manager_next_scene(app->scene_manager, MomentumAppSceneMiscPowerSecurityLog);
            break;
        default:
            break;
        }
    }

    return consumed;
}

void momentum_app_scene_misc_power_on_exit(void* context) {
    MomentumApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
