#include "../shortcuts_app.h"

enum TextInputIndex {
    TextInputResultOk,
};

static void shortcuts_app_scene_app_name_callback(void* context) {
    ShortcutsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, TextInputResultOk);
}

void shortcuts_app_scene_app_name_on_enter(void* context) {
    ShortcutsApp* app = context;
    TextInput* text_input = app->text_input;

    text_input_set_header_text(text_input, "App name (exact)");

    text_input_set_result_callback(
        text_input,
        shortcuts_app_scene_app_name_callback,
        app,
        app->new_app_name,
        SHORTCUT_APP_NAME_LEN,
        false);

    view_dispatcher_switch_to_view(app->view_dispatcher, ShortcutsAppViewTextInput);
}

bool shortcuts_app_scene_app_name_on_event(void* context, SceneManagerEvent event) {
    ShortcutsApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        switch(event.event) {
        case TextInputResultOk:
            scene_manager_previous_scene(app->scene_manager);
            break;
        default:
            break;
        }
    }

    return consumed;
}

void shortcuts_app_scene_app_name_on_exit(void* context) {
    ShortcutsApp* app = context;
    text_input_reset(app->text_input);
}
