#include "power_theft_alarm.h"
#include <furi.h>
#include <gui/elements.h>

struct PowerTheftAlarm {
    View* view;
};

static void power_theft_alarm_draw_callback(Canvas* canvas, void* _model) {
    UNUSED(_model);

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 0, 0, 128, 64);

    canvas_set_font(canvas, FontPrimary);
    elements_multiline_text_aligned(
        canvas, 64, 32, AlignCenter, AlignCenter, "Theft alarm triggered,\nlocking device");
}

PowerTheftAlarm* power_theft_alarm_alloc(void) {
    PowerTheftAlarm* power_theft_alarm = malloc(sizeof(PowerTheftAlarm));

    power_theft_alarm->view = view_alloc();
    view_set_context(power_theft_alarm->view, power_theft_alarm);
    view_set_draw_callback(power_theft_alarm->view, power_theft_alarm_draw_callback);
    view_set_input_callback(power_theft_alarm->view, NULL);

    return power_theft_alarm;
}

void power_theft_alarm_free(PowerTheftAlarm* power_theft_alarm) {
    furi_assert(power_theft_alarm);
    view_free(power_theft_alarm->view);
    free(power_theft_alarm);
}

View* power_theft_alarm_get_view(PowerTheftAlarm* power_theft_alarm) {
    furi_assert(power_theft_alarm);
    return power_theft_alarm->view;
}
