#include "../momentum_app.h"
#include <momentum/app_permissions.h>

typedef struct {
    const char* label;
    uint32_t bit;
} PermissionKind;

static const PermissionKind kinds[] = {
    {"Ask: Radio (Sub-GHz)", AppPermissionRadio},
    {"Ask: NFC", AppPermissionNfc},
    {"Ask: RFID / iButton", AppPermissionRfid},
    {"Ask: IR", AppPermissionInfrared},
    {"Ask: GPIO", AppPermissionGpio},
    {"Ask: USB", AppPermissionUsb},
    {"Ask: Bluetooth", AppPermissionBluetooth},
};
#define KIND_COUNT COUNT_OF(kinds)

enum VarItemListIndex {
    VarItemListIndexEnabled,
    // kinds[] follow: indices 1..KIND_COUNT
    VarItemListIndexRemembered = 1 + KIND_COUNT,
    VarItemListIndexForget,
};

static VariableItem* remembered_item;

static void remembered_item_update(void) {
    FuriString* text = furi_string_alloc_printf("%lu", (unsigned long)app_permissions_count());
    variable_item_set_current_value_text(remembered_item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void momentum_app_scene_misc_permissions_enabled_changed(VariableItem* item) {
    bool value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, value ? "ON" : "OFF");
    app_permissions_set_enabled(value);
}

static void momentum_app_scene_misc_permissions_kind_changed(VariableItem* item) {
    const PermissionKind* kind = variable_item_get_context(item);
    bool value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, value ? "ON" : "OFF");
    uint32_t watched = app_permissions_get_watched();
    if(value) {
        watched |= kind->bit;
    } else {
        watched &= ~kind->bit;
    }
    app_permissions_set_watched(watched);
}

static void momentum_app_scene_misc_permissions_enter_callback(void* context, uint32_t index) {
    MomentumApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void momentum_app_scene_misc_permissions_on_enter(void* context) {
    MomentumApp* app = context;
    VariableItemList* var_item_list = app->var_item_list;
    VariableItem* item;

    item = variable_item_list_add(
        var_item_list,
        "Ask before apps run",
        2,
        momentum_app_scene_misc_permissions_enabled_changed,
        app);
    bool enabled = app_permissions_is_enabled();
    variable_item_set_current_value_index(item, enabled);
    variable_item_set_current_value_text(item, enabled ? "ON" : "OFF");

    uint32_t watched = app_permissions_get_watched();
    for(size_t i = 0; i < KIND_COUNT; i++) {
        item = variable_item_list_add(
            var_item_list,
            kinds[i].label,
            2,
            momentum_app_scene_misc_permissions_kind_changed,
            (void*)&kinds[i]);
        bool on = (watched & kinds[i].bit) != 0;
        variable_item_set_current_value_index(item, on);
        variable_item_set_current_value_text(item, on ? "ON" : "OFF");
    }

    remembered_item = variable_item_list_add(var_item_list, "Remembered apps", 0, NULL, app);
    remembered_item_update();

    variable_item_list_add(var_item_list, "Forget all answers", 0, NULL, app);

    variable_item_list_set_enter_callback(
        var_item_list, momentum_app_scene_misc_permissions_enter_callback, app);
    variable_item_list_set_selected_item(
        var_item_list,
        scene_manager_get_scene_state(app->scene_manager, MomentumAppSceneMiscPermissions));

    view_dispatcher_switch_to_view(app->view_dispatcher, MomentumAppViewVarItemList);
}

bool momentum_app_scene_misc_permissions_on_event(void* context, SceneManagerEvent event) {
    MomentumApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(
            app->scene_manager, MomentumAppSceneMiscPermissions, event.event);
        consumed = true;
        if(event.event == VarItemListIndexForget) {
            app_permissions_forget_all();
            notification_message(app->notification, &sequence_success);
            remembered_item_update();
        }
    }

    return consumed;
}

void momentum_app_scene_misc_permissions_on_exit(void* context) {
    MomentumApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
