#include "../momentum_app.h"
#include <momentum/security_log.h>

void momentum_app_scene_misc_power_security_log_on_enter(void* context) {
    MomentumApp* app = context;

    security_log_read(app->text_box_scratch);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_focus(app->text_box, TextBoxFocusEnd);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_scratch));

    view_dispatcher_switch_to_view(app->view_dispatcher, MomentumAppViewTextBox);
}

bool momentum_app_scene_misc_power_security_log_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void momentum_app_scene_misc_power_security_log_on_exit(void* context) {
    MomentumApp* app = context;
    text_box_reset(app->text_box);
}
