#include "shortcuts_app.h"

static bool shortcuts_app_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    ShortcutsApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool shortcuts_app_back_event_callback(void* context) {
    furi_assert(context);
    ShortcutsApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static ShortcutsApp* shortcuts_app_alloc(void) {
    ShortcutsApp* app = malloc(sizeof(ShortcutsApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->shortcuts = furi_record_open(RECORD_SHORTCUTS);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&shortcuts_app_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, shortcuts_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, shortcuts_app_back_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        ShortcutsAppViewVarItemList,
        variable_item_list_get_view(app->var_item_list));

    app->dialog_ex = dialog_ex_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ShortcutsAppViewDialogEx, dialog_ex_get_view(app->dialog_ex));

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, ShortcutsAppViewTextInput, text_input_get_view(app->text_input));

    app->new_app_name[0] = '\0';

    scene_manager_next_scene(app->scene_manager, ShortcutsAppSceneStart);
    return app;
}

static void shortcuts_app_free(ShortcutsApp* app) {
    furi_assert(app);

    view_dispatcher_remove_view(app->view_dispatcher, ShortcutsAppViewVarItemList);
    variable_item_list_free(app->var_item_list);

    view_dispatcher_remove_view(app->view_dispatcher, ShortcutsAppViewDialogEx);
    dialog_ex_free(app->dialog_ex);

    view_dispatcher_remove_view(app->view_dispatcher, ShortcutsAppViewTextInput);
    text_input_free(app->text_input);

    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    furi_record_close(RECORD_SHORTCUTS);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t shortcuts_app(void* p) {
    UNUSED(p);
    ShortcutsApp* app = shortcuts_app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    shortcuts_app_free(app);
    return 0;
}
