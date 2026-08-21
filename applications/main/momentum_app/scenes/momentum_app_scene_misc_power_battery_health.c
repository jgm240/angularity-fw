#include "../momentum_app.h"
#include <furi_hal_power.h>

void momentum_app_scene_misc_power_battery_health_on_enter(void* context) {
    MomentumApp* app = context;

    furi_string_printf(
        app->text_box_scratch,
        "Battery Health: %u%%\n"
        "Charge: %u%%\n\n"
        "Full capacity: %lu mAh\n"
        "Design capacity: %lu mAh\n\n"
        "Charge cycles: %lu\n",
        furi_hal_power_get_bat_health_pct(),
        furi_hal_power_get_pct(),
        (unsigned long)furi_hal_power_get_battery_full_capacity(),
        (unsigned long)furi_hal_power_get_battery_design_capacity(),
        (unsigned long)momentum_settings.charge_cycles);

    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_scratch));

    view_dispatcher_switch_to_view(app->view_dispatcher, MomentumAppViewTextBox);
}

bool momentum_app_scene_misc_power_battery_health_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void momentum_app_scene_misc_power_battery_health_on_exit(void* context) {
    MomentumApp* app = context;
    text_box_reset(app->text_box);
}
