#include "../shortcuts_app.h"

void shortcuts_app_scene_confirm_delete_dialog_callback(DialogExResult result, void* context) {
    furi_assert(context);
    ShortcutsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void shortcuts_app_scene_confirm_delete_on_enter(void* context) {
    ShortcutsApp* app = context;
    DialogEx* dialog = app->dialog_ex;

    dialog_ex_set_header(dialog, "Delete shortcut?", 64, 0, AlignCenter, AlignTop);
    dialog_ex_set_text(dialog, "This can't be undone", 64, 32, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(dialog, "Cancel");
    dialog_ex_set_right_button_text(dialog, "Delete");
    dialog_ex_set_result_callback(dialog, shortcuts_app_scene_confirm_delete_dialog_callback);
    dialog_ex_set_context(dialog, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, ShortcutsAppViewDialogEx);
}

bool shortcuts_app_scene_confirm_delete_on_event(void* context, SceneManagerEvent event) {
    ShortcutsApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == DialogExResultRight) {
            shortcuts_remove(app->shortcuts, app->selected_index);
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, ShortcutsAppSceneStart);
        } else if(event.event == DialogExResultLeft) {
            scene_manager_previous_scene(app->scene_manager);
        }
        consumed = true;
    }

    return consumed;
}

void shortcuts_app_scene_confirm_delete_on_exit(void* context) {
    ShortcutsApp* app = context;
    dialog_ex_reset(app->dialog_ex);
}
