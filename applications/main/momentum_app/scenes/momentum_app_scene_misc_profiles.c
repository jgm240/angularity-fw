#include "../momentum_app.h"
#include <momentum/profiles.h>

enum VarItemListIndex {
    VarItemListIndexProfile,
    VarItemListIndexApply,
    VarItemListIndexSaveHere,
    VarItemListIndexActive,
};

static VariableItem* profile_item;
static VariableItem* active_item;
static uint8_t selected_profile = 0;

static void profile_item_update_text(void) {
    FuriString* text = furi_string_alloc_printf(
        "%s%s", profiles_name(selected_profile), profiles_is_saved(selected_profile) ? "" : " (empty)");
    variable_item_set_current_value_text(profile_item, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void active_item_update_text(void) {
    uint8_t active = profiles_get_active();
    variable_item_set_current_value_text(
        active_item, active == PROFILE_NONE ? "None" : profiles_name(active));
}

static void momentum_app_scene_misc_profiles_profile_changed(VariableItem* item) {
    selected_profile = variable_item_get_current_value_index(item);
    profile_item_update_text();
}

static void momentum_app_scene_misc_profiles_enter_callback(void* context, uint32_t index) {
    MomentumApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void momentum_app_scene_misc_profiles_on_enter(void* context) {
    MomentumApp* app = context;
    VariableItemList* var_item_list = app->var_item_list;

    profile_item = variable_item_list_add(
        var_item_list,
        "Profile",
        PROFILES_COUNT,
        momentum_app_scene_misc_profiles_profile_changed,
        app);
    variable_item_set_current_value_index(profile_item, selected_profile);
    profile_item_update_text();

    variable_item_list_add(var_item_list, "Apply", 0, NULL, app);
    variable_item_list_add(var_item_list, "Save current settings here", 0, NULL, app);

    active_item = variable_item_list_add(var_item_list, "Active", 0, NULL, app);
    active_item_update_text();

    variable_item_list_set_enter_callback(
        var_item_list, momentum_app_scene_misc_profiles_enter_callback, app);
    variable_item_list_set_selected_item(
        var_item_list, scene_manager_get_scene_state(app->scene_manager, MomentumAppSceneMiscProfiles));

    view_dispatcher_switch_to_view(app->view_dispatcher, MomentumAppViewVarItemList);
}

bool momentum_app_scene_misc_profiles_on_event(void* context, SceneManagerEvent event) {
    MomentumApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, MomentumAppSceneMiscProfiles, event.event);
        consumed = true;
        switch(event.event) {
        case VarItemListIndexApply:
            if(profiles_apply(selected_profile)) {
                notification_message(app->notification, &sequence_success);
            } else {
                notification_message(app->notification, &sequence_error);
            }
            active_item_update_text();
            break;
        case VarItemListIndexSaveHere:
            profiles_save_current(selected_profile);
            notification_message(app->notification, &sequence_success);
            profile_item_update_text();
            active_item_update_text();
            break;
        default:
            break;
        }
    }

    return consumed;
}

void momentum_app_scene_misc_profiles_on_exit(void* context) {
    MomentumApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
