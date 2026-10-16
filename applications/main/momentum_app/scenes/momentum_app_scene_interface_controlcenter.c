#include "../momentum_app.h"
#include <momentum/cc_layout.h>

// Tiles are laid out column-major: slots 0/1 are the first column, 2/3 the second, ...
static const char* const slot_labels[CC_LAYOUT_SLOTS] = {
    "Col 1 top",
    "Col 1 bottom",
    "Col 2 top",
    "Col 2 bottom",
    "Col 3 top",
    "Col 3 bottom",
    "Col 4 top",
    "Col 4 bottom",
};

static const uint8_t slot_ids[CC_LAYOUT_SLOTS] = {0, 1, 2, 3, 4, 5, 6, 7};
static VariableItem* slot_items[CC_LAYOUT_SLOTS];

#define VarItemListIndexReset CC_LAYOUT_SLOTS

static void momentum_app_scene_interface_controlcenter_slot_changed(VariableItem* item) {
    const uint8_t* slot = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    cc_layout_set(*slot, index);
    variable_item_set_current_value_text(item, cc_action_name(index));
}

static void momentum_app_scene_interface_controlcenter_enter_callback(void* context, uint32_t index) {
    MomentumApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void momentum_app_scene_interface_controlcenter_on_enter(void* context) {
    MomentumApp* app = context;
    VariableItemList* var_item_list = app->var_item_list;

    cc_layout_reload();

    for(uint8_t i = 0; i < CC_LAYOUT_SLOTS; i++) {
        VariableItem* item = variable_item_list_add(
            var_item_list,
            slot_labels[i],
            CcActionCount,
            momentum_app_scene_interface_controlcenter_slot_changed,
            (void*)&slot_ids[i]);
        variable_item_set_current_value_index(item, cc_layout_get(i));
        variable_item_set_current_value_text(item, cc_action_name(cc_layout_get(i)));
        slot_items[i] = item;
    }

    variable_item_list_add(var_item_list, "Reset to default", 0, NULL, app);

    variable_item_list_set_enter_callback(
        var_item_list, momentum_app_scene_interface_controlcenter_enter_callback, app);

    variable_item_list_set_selected_item(
        var_item_list,
        scene_manager_get_scene_state(app->scene_manager, MomentumAppSceneInterfaceControlCenter));

    view_dispatcher_switch_to_view(app->view_dispatcher, MomentumAppViewVarItemList);
}

bool momentum_app_scene_interface_controlcenter_on_event(void* context, SceneManagerEvent event) {
    MomentumApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(
            app->scene_manager, MomentumAppSceneInterfaceControlCenter, event.event);
        consumed = true;
        if(event.event == VarItemListIndexReset) {
            cc_layout_reset();
            for(uint8_t i = 0; i < CC_LAYOUT_SLOTS; i++) {
                variable_item_set_current_value_index(slot_items[i], cc_layout_get(i));
                variable_item_set_current_value_text(
                    slot_items[i], cc_action_name(cc_layout_get(i)));
            }
        }
    }

    return consumed;
}

void momentum_app_scene_interface_controlcenter_on_exit(void* context) {
    MomentumApp* app = context;
    cc_layout_save();
    variable_item_list_reset(app->var_item_list);
}
