#include "../momentum_app.h"

enum VarItemListIndex {
    VarItemListIndexSoundPack,
};

static const char* const sound_pack_names[SoundPackCount] = {
    [SoundPackOff] = "Off",
    [SoundPackClassic] = "Classic",
    [SoundPackChime] = "Chime",
    [SoundPackRetro] = "Retro",
    [SoundPackMinimal] = "Minimal",
    [SoundPackArcade] = "Arcade",
};

void momentum_app_scene_misc_sound_var_item_list_callback(void* context, uint32_t index) {
    MomentumApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void momentum_app_scene_misc_sound_pack_changed(VariableItem* item) {
    MomentumApp* app = variable_item_get_context(item);
    uint8_t value = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, sound_pack_names[value]);
    momentum_settings.sound_pack = value;
    app->save_settings = true;
}

void momentum_app_scene_misc_sound_on_enter(void* context) {
    MomentumApp* app = context;
    VariableItemList* var_item_list = app->var_item_list;
    VariableItem* item;

    item = variable_item_list_add(
        var_item_list,
        "Sound Pack",
        SoundPackCount,
        momentum_app_scene_misc_sound_pack_changed,
        app);
    variable_item_set_current_value_index(item, momentum_settings.sound_pack);
    variable_item_set_current_value_text(item, sound_pack_names[momentum_settings.sound_pack]);

    variable_item_list_set_enter_callback(
        var_item_list, momentum_app_scene_misc_sound_var_item_list_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, MomentumAppViewVarItemList);
}

bool momentum_app_scene_misc_sound_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void momentum_app_scene_misc_sound_on_exit(void* context) {
    MomentumApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
