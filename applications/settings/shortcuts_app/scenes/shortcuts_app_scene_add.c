#include "../shortcuts_app.h"
#include <furi_hal_resources.h>

#define GPIO_PIN_CHOICES_MAX 16

enum VarItemListIndex {
    VarItemListIndexTrigger,
    VarItemListIndexPin1,
    VarItemListIndexPin2,
    VarItemListIndexHour,
    VarItemListIndexMinute,
    VarItemListIndexAction,
    VarItemListIndexAppName,
    VarItemListIndexSave,
};

static const char* const shortcut_trigger_names[ShortcutTriggerCount] = {
    [ShortcutTriggerSdCardRemoved] = "SD Removed",
    [ShortcutTriggerGpioPull] = "GPIO Pull",
    [ShortcutTriggerPluggedIn] = "Plugged In",
    [ShortcutTriggerUnplugged] = "Unplugged",
    [ShortcutTriggerNfcScanned] = "NFC Scanned",
    [ShortcutTriggerScheduledTime] = "Scheduled Time",
};

static const char* const shortcut_action_names[ShortcutActionCount] = {
    [ShortcutActionTriggerAlarm] = "Trigger Alarm",
    [ShortcutActionBlinkLed] = "Blink LED",
    [ShortcutActionMakeSound] = "Make Sound",
    [ShortcutActionLaunchApp] = "Launch App",
    [ShortcutActionToggleDarkMode] = "Toggle Dark Mode",
};

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

static void shortcuts_app_scene_add_update_pin_locks(ShortcutsApp* app) {
    bool gpio_used = app->new_trigger == ShortcutTriggerGpioPull;
    variable_item_set_locked(
        variable_item_list_get(app->var_item_list, VarItemListIndexPin1),
        !gpio_used,
        "Needs GPIO Pull\ntrigger!");
    variable_item_set_locked(
        variable_item_list_get(app->var_item_list, VarItemListIndexPin2),
        !gpio_used,
        "Needs GPIO Pull\ntrigger!");

    bool time_used = app->new_trigger == ShortcutTriggerScheduledTime;
    variable_item_set_locked(
        variable_item_list_get(app->var_item_list, VarItemListIndexHour),
        !time_used,
        "Needs Scheduled\nTime trigger!");
    variable_item_set_locked(
        variable_item_list_get(app->var_item_list, VarItemListIndexMinute),
        !time_used,
        "Needs Scheduled\nTime trigger!");
}

static void shortcuts_app_scene_add_update_app_name_lock(ShortcutsApp* app) {
    variable_item_set_locked(
        variable_item_list_get(app->var_item_list, VarItemListIndexAppName),
        app->new_action != ShortcutActionLaunchApp,
        "Needs Launch App\naction!");
}

void shortcuts_app_scene_add_var_item_list_callback(void* context, uint32_t index) {
    ShortcutsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void shortcuts_app_scene_add_trigger_changed(VariableItem* item) {
    ShortcutsApp* app = variable_item_get_context(item);
    uint8_t value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, shortcut_trigger_names[value]);
    app->new_trigger = value;
    shortcuts_app_scene_add_update_pin_locks(app);
}

static void shortcuts_app_scene_add_pin1_changed(VariableItem* item) {
    ShortcutsApp* app = variable_item_get_context(item);
    uint8_t choice = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, gpio_pins[gpio_pin_choices[choice]].name);
    app->new_pin1 = gpio_pin_choices[choice];
}

static void shortcuts_app_scene_add_pin2_changed(VariableItem* item) {
    ShortcutsApp* app = variable_item_get_context(item);
    uint8_t choice = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, gpio_pins[gpio_pin_choices[choice]].name);
    app->new_pin2 = gpio_pin_choices[choice];
}

static void shortcuts_app_scene_add_hour_changed(VariableItem* item) {
    ShortcutsApp* app = variable_item_get_context(item);
    uint8_t value = variable_item_get_current_value_index(item);
    FuriString* text = furi_string_alloc_printf("%02u", value);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
    app->new_hour = value;
}

static void shortcuts_app_scene_add_minute_changed(VariableItem* item) {
    ShortcutsApp* app = variable_item_get_context(item);
    uint8_t value = variable_item_get_current_value_index(item);
    FuriString* text = furi_string_alloc_printf("%02u", value);
    variable_item_set_current_value_text(item, furi_string_get_cstr(text));
    furi_string_free(text);
    app->new_minute = value;
}

static void shortcuts_app_scene_add_action_changed(VariableItem* item) {
    ShortcutsApp* app = variable_item_get_context(item);
    uint8_t value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, shortcut_action_names[value]);
    app->new_action = value;
    shortcuts_app_scene_add_update_app_name_lock(app);
}

void shortcuts_app_scene_add_on_enter(void* context) {
    ShortcutsApp* app = context;
    VariableItemList* var_item_list = app->var_item_list;
    VariableItem* item;

    gpio_pin_choices_build();

    item = variable_item_list_add(
        var_item_list,
        "Trigger",
        ShortcutTriggerCount,
        shortcuts_app_scene_add_trigger_changed,
        app);
    variable_item_set_current_value_index(item, app->new_trigger);
    variable_item_set_current_value_text(item, shortcut_trigger_names[app->new_trigger]);

    item = variable_item_list_add(
        var_item_list,
        "GPIO Pin 1",
        gpio_pin_choices_count,
        shortcuts_app_scene_add_pin1_changed,
        app);
    uint8_t pin1_choice = gpio_pin_choice_index_for_raw(app->new_pin1);
    variable_item_set_current_value_index(item, pin1_choice);
    variable_item_set_current_value_text(item, gpio_pins[gpio_pin_choices[pin1_choice]].name);

    item = variable_item_list_add(
        var_item_list,
        "GPIO Pin 2",
        gpio_pin_choices_count,
        shortcuts_app_scene_add_pin2_changed,
        app);
    uint8_t pin2_choice = gpio_pin_choice_index_for_raw(app->new_pin2);
    variable_item_set_current_value_index(item, pin2_choice);
    variable_item_set_current_value_text(item, gpio_pins[gpio_pin_choices[pin2_choice]].name);

    item = variable_item_list_add(
        var_item_list, "Hour", 24, shortcuts_app_scene_add_hour_changed, app);
    variable_item_set_current_value_index(item, app->new_hour);
    FuriString* hour_text = furi_string_alloc_printf("%02u", app->new_hour);
    variable_item_set_current_value_text(item, furi_string_get_cstr(hour_text));
    furi_string_free(hour_text);

    item = variable_item_list_add(
        var_item_list, "Minute", 60, shortcuts_app_scene_add_minute_changed, app);
    variable_item_set_current_value_index(item, app->new_minute);
    FuriString* minute_text = furi_string_alloc_printf("%02u", app->new_minute);
    variable_item_set_current_value_text(item, furi_string_get_cstr(minute_text));
    furi_string_free(minute_text);

    item = variable_item_list_add(
        var_item_list, "Action", ShortcutActionCount, shortcuts_app_scene_add_action_changed, app);
    variable_item_set_current_value_index(item, app->new_action);
    variable_item_set_current_value_text(item, shortcut_action_names[app->new_action]);

    shortcuts_app_scene_add_update_pin_locks(app);

    item = variable_item_list_add(var_item_list, "App Name", 0, NULL, app);
    variable_item_set_current_value_text(
        item, app->new_app_name[0] ? app->new_app_name : "<none>");

    shortcuts_app_scene_add_update_app_name_lock(app);

    item = variable_item_list_add(var_item_list, "Save Shortcut", 0, NULL, app);
    variable_item_set_current_value_text(item, ">");

    variable_item_list_set_enter_callback(
        var_item_list, shortcuts_app_scene_add_var_item_list_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, ShortcutsAppViewVarItemList);
}

bool shortcuts_app_scene_add_on_event(void* context, SceneManagerEvent event) {
    ShortcutsApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(event.event == VarItemListIndexAppName) {
            if(app->new_action == ShortcutActionLaunchApp) {
                scene_manager_next_scene(app->scene_manager, ShortcutsAppSceneAppName);
            }
        } else if(event.event == VarItemListIndexSave) {
            bool scheduled = app->new_trigger == ShortcutTriggerScheduledTime;
            ShortcutRule rule = {
                .trigger = app->new_trigger,
                .gpio_pin1 = scheduled ? app->new_hour : app->new_pin1,
                .gpio_pin2 = scheduled ? app->new_minute : app->new_pin2,
                .action = app->new_action,
            };
            strlcpy(rule.app_name, app->new_app_name, SHORTCUT_APP_NAME_LEN);
            shortcuts_add(app->shortcuts, &rule);
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, ShortcutsAppSceneStart);
        }
    }

    return consumed;
}

void shortcuts_app_scene_add_on_exit(void* context) {
    ShortcutsApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
