#pragma once

typedef struct PowerTheftAlarm PowerTheftAlarm;

#include <gui/view.h>

PowerTheftAlarm* power_theft_alarm_alloc(void);

void power_theft_alarm_free(PowerTheftAlarm* power_theft_alarm);

View* power_theft_alarm_get_view(PowerTheftAlarm* power_theft_alarm);
