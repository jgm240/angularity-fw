#pragma once

// Data model and parsers for the Standby app. Pure C (no firmware headers) so the parsers can be
// unit-tested on a desktop compiler against real API replies.

#include "mini_json.h"
#include <stdio.h>

// ---------------------------------------------------------------- weather

typedef struct {
    int32_t hi10; // tenths of a degree C
    int32_t lo10;
    int32_t code; // WMO weather code
} StandbyDay;

typedef struct {
    bool valid;
    uint32_t fetched; // seconds since 1970 in local time (same scale as the Flipper RTC)
    char city[32];
    int32_t temp10; // tenths of a degree C
    int32_t humidity; // %
    int32_t code; // WMO weather code
    int32_t wind10; // tenths of km/h
    bool is_day;
    StandbyDay day[3];
} StandbyWeather;

typedef enum {
    StandbyIconSun,
    StandbyIconPartly,
    StandbyIconCloud,
    StandbyIconFog,
    StandbyIconRain,
    StandbyIconSnow,
    StandbyIconStorm,
} StandbyIcon;

static inline const char* standby_wmo_text(int32_t code) {
    switch(code) {
    case 0:
        return "Clear";
    case 1:
        return "Mostly clear";
    case 2:
        return "Partly cloudy";
    case 3:
        return "Overcast";
    case 45:
    case 48:
        return "Fog";
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
        return "Drizzle";
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
        return "Rain";
    case 71:
    case 73:
    case 75:
    case 77:
        return "Snow";
    case 80:
    case 81:
    case 82:
        return "Showers";
    case 85:
    case 86:
        return "Snow showers";
    case 95:
        return "Thunderstorm";
    case 96:
    case 99:
        return "Hail storm";
    default:
        return "Unknown";
    }
}

static inline StandbyIcon standby_wmo_icon(int32_t code) {
    if(code == 0 || code == 1) return StandbyIconSun;
    if(code == 2) return StandbyIconPartly;
    if(code == 3) return StandbyIconCloud;
    if(code == 45 || code == 48) return StandbyIconFog;
    if((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return StandbyIconRain;
    if((code >= 71 && code <= 77) || code == 85 || code == 86) return StandbyIconSnow;
    if(code >= 95) return StandbyIconStorm;
    return StandbyIconCloud;
}

/** Fill w from an Open-Meteo forecast reply (current + 3 daily entries). city/fetched are left alone. */
static inline bool standby_parse_forecast(const char* json, StandbyWeather* w) {
    char current[320], daily[512];
    if(!mj_object(json, "current", current, sizeof(current))) return false;
    int32_t v;
    if(!mj_scaled(current, "temperature_2m", 1, &w->temp10)) return false;
    w->humidity = mj_int(current, "relative_humidity_2m", &v) ? v : -1;
    w->code = mj_int(current, "weather_code", &v) ? v : -1;
    w->wind10 = mj_scaled(current, "wind_speed_10m", 1, &v) ? v : 0;
    w->is_day = mj_int(current, "is_day", &v) ? (v != 0) : true;

    for(int i = 0; i < 3; i++) {
        w->day[i].hi10 = INT32_MIN;
        w->day[i].lo10 = INT32_MIN;
        w->day[i].code = -1;
    }
    if(mj_object(json, "daily", daily, sizeof(daily))) {
        for(uint32_t i = 0; i < 3; i++) {
            mj_array_scaled(daily, "temperature_2m_max", i, 1, &w->day[i].hi10);
            mj_array_scaled(daily, "temperature_2m_min", i, 1, &w->day[i].lo10);
            if(mj_array_scaled(daily, "weather_code", i, 0, &v)) w->day[i].code = v;
        }
    }
    w->valid = true;
    return true;
}

/** Compact text form used for the on-SD cache. */
static inline bool standby_weather_serialize(const StandbyWeather* w, char* out, size_t cap) {
    int n = snprintf(
        out,
        cap,
        "{\"f\":%lu,\"city\":\"%s\",\"t\":%ld,\"h\":%ld,\"c\":%ld,\"w\":%ld,\"d\":%d,"
        "\"d0\":[%ld,%ld,%ld],\"d1\":[%ld,%ld,%ld],\"d2\":[%ld,%ld,%ld]}",
        (unsigned long)w->fetched,
        w->city,
        (long)w->temp10,
        (long)w->humidity,
        (long)w->code,
        (long)w->wind10,
        w->is_day ? 1 : 0,
        (long)w->day[0].hi10,
        (long)w->day[0].lo10,
        (long)w->day[0].code,
        (long)w->day[1].hi10,
        (long)w->day[1].lo10,
        (long)w->day[1].code,
        (long)w->day[2].hi10,
        (long)w->day[2].lo10,
        (long)w->day[2].code);
    return n > 0 && (size_t)n < cap;
}

static inline bool standby_weather_deserialize(const char* json, StandbyWeather* w) {
    memset(w, 0, sizeof(*w));
    int32_t v;
    if(!mj_int(json, "t", &w->temp10)) return false;
    // the leading "t" matched the first key named exactly "t"
    w->fetched = 0;
    mj_u32(json, "f", &w->fetched);
    mj_string(json, "city", w->city, sizeof(w->city));
    w->humidity = mj_int(json, "h", &v) ? v : -1;
    w->code = mj_int(json, "c", &v) ? v : -1;
    w->wind10 = mj_int(json, "w", &v) ? v : 0;
    w->is_day = mj_int(json, "d", &v) ? (v != 0) : true;
    static const char* const keys[3] = {"d0", "d1", "d2"};
    for(int i = 0; i < 3; i++) {
        w->day[i].hi10 = INT32_MIN;
        w->day[i].lo10 = INT32_MIN;
        w->day[i].code = -1;
        mj_array_scaled(json, keys[i], 0, 0, &w->day[i].hi10);
        mj_array_scaled(json, keys[i], 1, 0, &w->day[i].lo10);
        mj_array_scaled(json, keys[i], 2, 0, &w->day[i].code);
    }
    w->valid = true;
    return true;
}

// ---------------------------------------------------------------- place and time

typedef struct {
    char name[32];
    char tz[40];
    int32_t lat; // degrees * 10000
    int32_t lon;
} StandbyPlace;

/** Open-Meteo geocoding: first result of "results" (large objects, so scoped in place). */
static inline bool standby_parse_geocode(const char* json, StandbyPlace* p) {
    const char *start, *end;
    if(!mj_first_object(json, "results", &start, &end)) return false;
    memset(p, 0, sizeof(*p));
    if(!mj_string_value(mj_value_in(start, end, "name"), p->name, sizeof(p->name))) return false;
    const char* lat = mj_value_in(start, end, "latitude");
    const char* lon = mj_value_in(start, end, "longitude");
    if(!lat || !lon || !mj_parse_scaled(lat, 4, &p->lat) || !mj_parse_scaled(lon, 4, &p->lon)) {
        return false;
    }
    mj_string_value(mj_value_in(start, end, "timezone"), p->tz, sizeof(p->tz));
    return true;
}

/** ipwhois.app: where this internet connection appears to be. */
static inline bool standby_parse_ipwhois(const char* json, StandbyPlace* p) {
    bool ok = false;
    if(!mj_bool(json, "success", &ok) || !ok) return false;
    memset(p, 0, sizeof(*p));
    mj_string(json, "city", p->name, sizeof(p->name));
    if(!mj_scaled(json, "latitude", 4, &p->lat) || !mj_scaled(json, "longitude", 4, &p->lon)) {
        return false;
    }
    mj_string(json, "timezone", p->tz, sizeof(p->tz));
    return true;
}

typedef struct {
    int year, month, day, hour, minute, second;
} StandbyClock;

/** timeapi.io current-time reply (local time of the requested zone). */
static inline bool standby_parse_timeapi(const char* json, StandbyClock* c) {
    int32_t y, mo, d, h, mi, s;
    if(!mj_int(json, "year", &y) || !mj_int(json, "month", &mo) || !mj_int(json, "day", &d) ||
       !mj_int(json, "hour", &h) || !mj_int(json, "minute", &mi) || !mj_int(json, "seconds", &s)) {
        return false;
    }
    if(y < 2024 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 ||
       mi < 0 || mi > 59 || s < 0 || s > 60) {
        return false;
    }
    c->year = y;
    c->month = mo;
    c->day = d;
    c->hour = h;
    c->minute = mi;
    c->second = s > 59 ? 59 : s;
    return true;
}

static inline void standby_url_encode(const char* in, char* out, size_t cap) {
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for(; *in && n + 4 < cap; in++) {
        unsigned char c = (unsigned char)*in;
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                     c == '-' || c == '_' || c == '.' || c == '~' || c == '/';
        if(plain) {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        }
    }
    out[n] = '\0';
}

/** Print a degrees*10000 coordinate as e.g. -33.8700. */
static inline void standby_fmt_coord(int32_t v, char* out, size_t cap) {
    int64_t a = v < 0 ? -(int64_t)v : v;
    snprintf(out, cap, "%s%ld.%04ld", v < 0 ? "-" : "", (long)(a / 10000), (long)(a % 10000));
}

// ---------------------------------------------------------------- calendar math

static inline int standby_days_in_month(int year, int month) {
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if(month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
    return days[(month - 1) % 12];
}

/** 0 = Sunday ... 6 = Saturday. */
static inline int standby_day_of_week(int y, int m, int d) {
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if(m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// ---------------------------------------------------------------- JWLink snapshot

#define STANDBY_MAX_EVENTS 6

typedef struct {
    char date[11]; // YYYY-MM-DD
    char time[6]; // HH:MM, empty for all-day
    char title[28];
} StandbyEvent;

typedef struct {
    bool valid;
    uint32_t updated; // seconds since 1970 in the phone's local time
    char name[20];
    int32_t battery; // %, -1 unknown
    int32_t steps; // -1 unknown
    int32_t heart; // bpm, -1 unknown
    StandbyEvent events[STANDBY_MAX_EVENTS];
    uint8_t event_count;
} StandbyJwlink;

/** The snapshot written to /ext/apps_data/standby/jwlink.json by qAngularity on the phone. */
static inline bool standby_parse_jwlink(const char* json, StandbyJwlink* j) {
    memset(j, 0, sizeof(*j));
    j->battery = j->steps = j->heart = -1;
    int32_t v;
    if(!mj_u32(json, "updated", &j->updated)) return false;

    char watch[160];
    if(mj_object(json, "watch", watch, sizeof(watch))) {
        mj_string(watch, "name", j->name, sizeof(j->name));
        if(mj_int(watch, "battery", &v)) j->battery = v;
        if(mj_int(watch, "steps", &v)) j->steps = v;
        if(mj_int(watch, "heart", &v)) j->heart = v;
    }
    char ev[160];
    for(uint32_t i = 0; i < STANDBY_MAX_EVENTS; i++) {
        if(!mj_array_object(json, "events", i, ev, sizeof(ev))) break;
        StandbyEvent* e = &j->events[j->event_count];
        if(!mj_string(ev, "d", e->date, sizeof(e->date))) continue;
        mj_string(ev, "t", e->time, sizeof(e->time));
        mj_string(ev, "title", e->title, sizeof(e->title));
        j->event_count++;
    }
    j->valid = true;
    return true;
}
