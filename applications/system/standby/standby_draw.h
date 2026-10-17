#pragma once

// All Standby drawing. Depends only on the Canvas API and standby_data.h, so the same code is
// rendered on a desktop (with the real u8g2 fonts) to check layouts without a Flipper.

#include "standby_data.h"

typedef enum {
    PageClock,
    PageWeather,
    PageCalendar,
    PageJwlink,
    PageCount,
} Page;

typedef struct {
    DateTime now;
    uint32_t now_ts; // seconds since 1970 in local time
    uint8_t page;
    uint8_t battery;
    bool charging;
    bool dimmed;
    bool fahrenheit;
    bool h24;
    bool monday_first;
    char notice[44];
    StandbyWeather weather;
    StandbyJwlink jw;
} DisplayModel;

// ------------------------------------------------------------ drawing helpers

static void format_temp(char* out, size_t cap, int32_t temp10, bool fahrenheit) {
    int32_t v10 = temp10;
    if(fahrenheit) v10 = (temp10 * 9) / 5 + 320;
    // round to whole degrees
    int32_t whole = (v10 >= 0 ? v10 + 5 : v10 - 5) / 10;
    if(whole > 999) whole = 999;
    if(whole < -999) whole = -999;
    snprintf(out, cap, "%d", (int)whole);
}

static void draw_degree(Canvas* canvas, int x, int y) {
    canvas_draw_circle(canvas, x, y, 1);
}

static void draw_cloud(Canvas* canvas, int x, int y) {
    // roughly 20x12 starting at x,y (top-left)
    canvas_draw_disc(canvas, x + 5, y + 8, 4);
    canvas_draw_disc(canvas, x + 11, y + 5, 5);
    canvas_draw_disc(canvas, x + 16, y + 9, 3);
    canvas_draw_box(canvas, x + 5, y + 8, 12, 5);
}

static void draw_weather_icon(Canvas* canvas, StandbyIcon icon, int x, int y) {
    // 24x24 box with top-left at x,y
    switch(icon) {
    case StandbyIconSun:
        canvas_draw_disc(canvas, x + 12, y + 12, 5);
        for(int i = 0; i < 8; i++) {
            static const int8_t dx[8] = {0, 7, 10, 7, 0, -7, -10, -7};
            static const int8_t dy[8] = {-10, -7, 0, 7, 10, 7, 0, -7};
            int sx = x + 12 + dx[i] * 7 / 10, sy = y + 12 + dy[i] * 7 / 10;
            canvas_draw_line(canvas, sx, sy, x + 12 + dx[i], y + 12 + dy[i]);
        }
        break;
    case StandbyIconPartly:
        canvas_draw_circle(canvas, x + 7, y + 7, 5);
        draw_cloud(canvas, x + 3, y + 9);
        break;
    case StandbyIconCloud:
        draw_cloud(canvas, x + 2, y + 6);
        break;
    case StandbyIconFog:
        draw_cloud(canvas, x + 2, y + 2);
        canvas_draw_line(canvas, x + 2, y + 16, x + 22, y + 16);
        canvas_draw_line(canvas, x + 5, y + 20, x + 19, y + 20);
        break;
    case StandbyIconRain:
        draw_cloud(canvas, x + 2, y + 2);
        for(int i = 0; i < 3; i++) canvas_draw_line(canvas, x + 6 + i * 6, y + 16, x + 4 + i * 6, y + 22);
        break;
    case StandbyIconSnow:
        draw_cloud(canvas, x + 2, y + 2);
        for(int i = 0; i < 3; i++) {
            canvas_draw_dot(canvas, x + 6 + i * 6, y + 17);
            canvas_draw_dot(canvas, x + 4 + i * 6, y + 21);
            canvas_draw_dot(canvas, x + 8 + i * 6, y + 21);
        }
        break;
    case StandbyIconStorm:
        draw_cloud(canvas, x + 2, y + 2);
        canvas_draw_line(canvas, x + 13, y + 14, x + 9, y + 19);
        canvas_draw_line(canvas, x + 9, y + 19, x + 14, y + 19);
        canvas_draw_line(canvas, x + 14, y + 19, x + 10, y + 24);
        break;
    }
}

static const char* const weekday_short[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* const month_name[12] = {
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December"};
static const char* const month_short[12] =
    {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static void draw_battery_text(Canvas* canvas, const DisplayModel* m) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%u%%%s", m->battery, m->charging ? "+" : "");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 126, 1, AlignRight, AlignTop, buf);
}

static void draw_notice(Canvas* canvas, const DisplayModel* m) {
    if(m->notice[0]) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 60, AlignCenter, AlignBottom, m->notice);
    }
}

// ---- Clock

static void draw_clock(Canvas* canvas, const DisplayModel* m) {
    int hour = m->now.hour;
    const char* suffix = "";
    if(!m->h24) {
        suffix = hour >= 12 ? "PM" : "AM";
        hour = hour % 12;
        if(hour == 0) hour = 12;
    }
    char hh[4], mm[4], buf[40];
    snprintf(hh, sizeof(hh), m->h24 ? "%02d" : "%d", hour);
    snprintf(mm, sizeof(mm), "%02d", m->now.minute);

    canvas_set_font(canvas, FontBigNumbers);
    int gw = canvas_glyph_width(canvas, '0');
    int hh_w = (int)strlen(hh) * gw;
    int total = hh_w + 8 + 2 * gw;
    int x0 = (128 - total) / 2;
    canvas_draw_str_aligned(canvas, x0, 31, AlignLeft, AlignCenter, hh);
    canvas_draw_box(canvas, x0 + hh_w + 3, 25, 2, 2);
    canvas_draw_box(canvas, x0 + hh_w + 3, 34, 2, 2);
    canvas_draw_str_aligned(canvas, x0 + hh_w + 8, 31, AlignLeft, AlignCenter, mm);

    canvas_set_font(canvas, FontSecondary);
    if(suffix[0]) canvas_draw_str_aligned(canvas, x0 + total + 3, 31, AlignLeft, AlignCenter, suffix);

    // seconds as a thin progress line
    canvas_draw_line(canvas, 4, 47, 4 + (int)(m->now.second * 120 / 59), 47);
    canvas_draw_dot(canvas, 4, 46);
    canvas_draw_dot(canvas, 124, 46);

    int dow = standby_day_of_week(m->now.year, m->now.month, m->now.day);
    snprintf(
        buf,
        sizeof(buf),
        "%s %d %s %d",
        weekday_short[dow],
        m->now.day,
        month_short[(m->now.month - 1) % 12],
        m->now.year);
    canvas_draw_str_aligned(canvas, 2, 1, AlignLeft, AlignTop, buf);
    draw_battery_text(canvas, m);

    if(m->weather.valid && !m->notice[0]) {
        char t[8];
        format_temp(t, sizeof(t), m->weather.temp10, m->fahrenheit);
        snprintf(buf, sizeof(buf), "%s   %s", t, standby_wmo_text(m->weather.code));
        int x0 = 64 - canvas_string_width(canvas, buf) / 2;
        canvas_draw_str(canvas, x0, 60, buf);
        draw_degree(canvas, x0 + canvas_string_width(canvas, t) + 3, 54);
    }
}

// ---- Weather

static void draw_weather(Canvas* canvas, const DisplayModel* m) {
    const StandbyWeather* w = &m->weather;
    canvas_set_font(canvas, FontPrimary);
    if(!w->valid) {
        canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignCenter, "No weather yet");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignCenter, "OK > Refresh (needs");
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, "the WiFi Devboard)");
        draw_battery_text(canvas, m);
        draw_notice(canvas, m);
        return;
    }
    canvas_draw_str_aligned(canvas, 2, 1, AlignLeft, AlignTop, w->city[0] ? w->city : "Weather");

    char t[8], buf[48];
    format_temp(t, sizeof(t), w->temp10, m->fahrenheit);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 4, 27, AlignLeft, AlignCenter, t);
    int tw = canvas_string_width(canvas, t);
    draw_degree(canvas, 4 + tw + 4, 17);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 4 + tw + 8, 20, AlignLeft, AlignCenter, m->fahrenheit ? "F" : "C");

    draw_weather_icon(canvas, standby_wmo_icon(w->code), 98, 10);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 2, 35, AlignLeft, AlignTop, standby_wmo_text(w->code));
    int wind = w->wind10 / 10;
    if(m->fahrenheit) wind = (w->wind10 * 6214) / 100000; // km/h -> mph
    snprintf(
        buf,
        sizeof(buf),
        "%ld%%  %d %s",
        (long)(w->humidity < 0 ? 0 : w->humidity),
        wind,
        m->fahrenheit ? "mph" : "km/h");
    canvas_draw_str_aligned(canvas, 126, 35, AlignRight, AlignTop, buf);

    // next days: weekday over high/low, three columns
    int dow = standby_day_of_week(m->now.year, m->now.month, m->now.day);
    for(int i = 0; i < 3; i++) {
        int x = 3 + i * 42;
        if(w->day[i].hi10 == INT32_MIN) continue;
        char hi[8], lo[8];
        format_temp(hi, sizeof(hi), w->day[i].hi10, m->fahrenheit);
        format_temp(lo, sizeof(lo), w->day[i].lo10 == INT32_MIN ? 0 : w->day[i].lo10, m->fahrenheit);
        canvas_draw_str(canvas, x, 53, i == 0 ? "Today" : weekday_short[(dow + i) % 7]);
        snprintf(buf, sizeof(buf), "%s/%s", hi, lo);
        canvas_draw_str(canvas, x, 61, buf);
    }
}

// ---- Calendar

static void draw_calendar(Canvas* canvas, const DisplayModel* m) {
    char buf[40];
    int year = m->now.year, month = m->now.month;
    const int cell_w = 14, cell_h = 8, gx = 1, gy = 12;

    canvas_set_font(canvas, FontBatteryPercent);
    static const char* const hdr_mon[7] = {"M", "T", "W", "T", "F", "S", "S"};
    static const char* const hdr_sun[7] = {"S", "M", "T", "W", "T", "F", "S"};
    const char* const* hdr = m->monday_first ? hdr_mon : hdr_sun;
    for(int c = 0; c < 7; c++) {
        canvas_draw_str_aligned(canvas, gx + c * cell_w + cell_w - 4, 0, AlignRight, AlignTop, hdr[c]);
    }
    canvas_draw_line(canvas, gx, 9, gx + 7 * cell_w - 3, 9);

    int first_dow = standby_day_of_week(year, month, 1); // 0 = Sunday
    int offset = m->monday_first ? (first_dow + 6) % 7 : first_dow;
    int days = standby_days_in_month(year, month);
    for(int d = 1; d <= days; d++) {
        int idx = offset + d - 1;
        int col = idx % 7, row = idx / 7;
        int x = gx + col * cell_w, y = gy + row * cell_h;
        snprintf(buf, sizeof(buf), "%d", d);
        bool today = (d == m->now.day);
        if(today) {
            canvas_draw_box(canvas, x, y - 1, cell_w - 3, cell_h);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str_aligned(canvas, x + cell_w - 4, y + cell_h - 2, AlignRight, AlignBottom, buf);
        if(today) canvas_set_color(canvas, ColorBlack);

        // JWLink events in this month get a dot under the number
        for(uint8_t e = 0; e < m->jw.event_count; e++) {
            int ey, em, ed;
            if(sscanf(m->jw.events[e].date, "%d-%d-%d", &ey, &em, &ed) == 3 && ey == year &&
               em == month && ed == d && !today) {
                canvas_draw_dot(canvas, x + cell_w - 8, y + cell_h - 1);
            }
        }
    }

    // right-hand panel: month, big day, weekday
    const int px = 101, pw = 27;
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%s", month_short[(month - 1) % 12]);
    canvas_draw_str_aligned(canvas, px + pw / 2, 0, AlignCenter, AlignTop, buf);
    snprintf(buf, sizeof(buf), "%d", year);
    canvas_draw_str_aligned(canvas, px + pw / 2, 8, AlignCenter, AlignTop, buf);
    canvas_set_font(canvas, FontBigNumbers);
    snprintf(buf, sizeof(buf), "%d", m->now.day);
    canvas_draw_str_aligned(canvas, px + pw / 2, 33, AlignCenter, AlignCenter, buf);
    canvas_set_font(canvas, FontSecondary);
    int dow = standby_day_of_week(year, month, m->now.day);
    canvas_draw_str_aligned(canvas, px + pw / 2, 46, AlignCenter, AlignTop, weekday_short[dow]);
}

// ---- JWLink

static void draw_age(char* out, size_t cap, uint32_t now_ts, uint32_t then_ts) {
    if(then_ts == 0 || now_ts < then_ts) {
        snprintf(out, cap, "just now");
        return;
    }
    uint32_t s = now_ts - then_ts;
    if(s < 90) {
        snprintf(out, cap, "just now");
    } else if(s < 3600) {
        snprintf(out, cap, "%lum ago", (unsigned long)(s / 60));
    } else if(s < 86400) {
        snprintf(out, cap, "%luh ago", (unsigned long)(s / 3600));
    } else {
        snprintf(out, cap, "%lud ago", (unsigned long)(s / 86400));
    }
}

static void draw_jwlink(Canvas* canvas, const DisplayModel* m) {
    const StandbyJwlink* j = &m->jw;
    canvas_set_font(canvas, FontPrimary);
    if(!j->valid) {
        canvas_draw_str_aligned(canvas, 64, 14, AlignCenter, AlignCenter, "No JWLink data");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, "qAngularity on your phone");
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "sends it to this Flipper");
        canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignCenter, "(Settings > Standby sync)");
        draw_battery_text(canvas, m);
        return;
    }
    char buf[48];
    canvas_draw_str_aligned(canvas, 2, 1, AlignLeft, AlignTop, j->name[0] ? j->name : "Watch");
    canvas_set_font(canvas, FontSecondary);
    char age[16];
    draw_age(age, sizeof(age), m->now_ts, j->updated);
    canvas_draw_str_aligned(canvas, 126, 1, AlignRight, AlignTop, age);

    // battery
    if(j->battery >= 0) {
        snprintf(buf, sizeof(buf), "Battery %ld%%", (long)j->battery);
        canvas_draw_str(canvas, 2, 20, buf);
        canvas_draw_frame(canvas, 76, 13, 50, 8);
        canvas_draw_box(canvas, 77, 14, (int)(j->battery > 100 ? 48 : j->battery * 48 / 100), 6);
    }
    if(j->steps >= 0) {
        snprintf(buf, sizeof(buf), "Steps %ld", (long)j->steps);
        canvas_draw_str(canvas, 2, 30, buf);
    }
    if(j->heart >= 0) {
        snprintf(buf, sizeof(buf), "Heart %ld bpm", (long)j->heart);
        canvas_draw_str(canvas, 70, 30, buf);
    }
    // next events
    canvas_draw_line(canvas, 2, 33, 125, 33);
    int shown = 0;
    for(uint8_t i = 0; i < j->event_count && shown < 3; i++) {
        int ey, em, ed;
        if(sscanf(j->events[i].date, "%d-%d-%d", &ey, &em, &ed) != 3) continue;
        // skip events before today
        uint32_t ev_key = (uint32_t)ey * 10000 + (uint32_t)em * 100 + (uint32_t)ed;
        uint32_t today_key = (uint32_t)m->now.year * 10000 + (uint32_t)m->now.month * 100 + m->now.day;
        if(ev_key < today_key) continue;
        snprintf(
            buf,
            sizeof(buf),
            "%d %s %s %s",
            ed,
            month_short[(em - 1 + 12) % 12],
            j->events[i].time,
            j->events[i].title);
        canvas_draw_str(canvas, 2, 43 + shown * 9, buf);
        shown++;
    }
    if(shown == 0) canvas_draw_str(canvas, 2, 43, "No upcoming events");
}

static void display_draw(Canvas* canvas, void* model) {
    DisplayModel* m = model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    switch(m->page) {
    case PageWeather:
        draw_weather(canvas, m);
        break;
    case PageCalendar:
        draw_calendar(canvas, m);
        break;
    case PageJwlink:
        draw_jwlink(canvas, m);
        break;
    default:
        draw_clock(canvas, m);
        break;
    }
    if(m->page != PageWeather) {
        // page dots, bottom-right, so the page count is discoverable
        for(int i = 0; i < PageCount; i++) {
            if(i == m->page) {
                canvas_draw_box(canvas, 106 + i * 5, 63, 3, 1);
            } else {
                canvas_draw_dot(canvas, 107 + i * 5, 63);
            }
        }
    }
    if(m->page != PageWeather && m->page != PageJwlink) draw_notice(canvas, m);
}

