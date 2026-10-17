#pragma once

// Tiny, allocation-free JSON readers for the handful of API replies Angularity apps use
// (Open-Meteo, timeapi.io, ipwhois, the JWLink snapshot). Pure C so it is unit-tested on a
// desktop compiler. These are lookups, not a validating parser: keys are matched by their
// first occurrence, so scope with mj_object() / mj_array_object() when a key name repeats.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static inline bool mj_is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/** Pointer to the value following "key": or NULL. */
static inline const char* mj_value(const char* json, const char* key) {
    char pattern[48];
    size_t klen = strlen(key);
    if(klen + 3 > sizeof(pattern)) return NULL;
    pattern[0] = '"';
    memcpy(pattern + 1, key, klen);
    pattern[klen + 1] = '"';
    pattern[klen + 2] = '\0';
    const char* p = json;
    while((p = strstr(p, pattern)) != NULL) {
        const char* q = p + klen + 2;
        while(mj_is_ws(*q))
            q++;
        if(*q == ':') {
            q++;
            while(mj_is_ws(*q))
                q++;
            return q;
        }
        p += klen + 2; // matched a string value that equals the key; keep looking
    }
    return NULL;
}

/** Read the string whose opening quote is at p. */
static inline bool mj_string_value(const char* p, char* out, size_t cap) {
    if(!p || *p != '"' || cap == 0) return false;
    p++;
    size_t n = 0;
    while(*p && *p != '"') {
        char c = *p++;
        if(c == '\\' && *p) {
            c = *p++;
            if(c == 'n' || c == 't' || c == 'r') c = ' ';
            // \uXXXX is not decoded: keep a placeholder rather than garbage
            if(c == 'u') {
                c = '?';
                for(int i = 0; i < 4 && *p; i++)
                    p++;
            }
        }
        if(n + 1 < cap) out[n++] = c;
    }
    out[n] = '\0';
    return *p == '"';
}

static inline bool mj_string(const char* json, const char* key, char* out, size_t cap) {
    return mj_string_value(mj_value(json, key), out, cap);
}

/** Parse a JSON number at p into value * 10^decimals (rounded half away from zero). */
static inline bool mj_parse_scaled(const char* p, int decimals, int32_t* out) {
    bool neg = false;
    if(*p == '-') {
        neg = true;
        p++;
    }
    if(*p < '0' || *p > '9') return false;
    int64_t whole = 0;
    while(*p >= '0' && *p <= '9') {
        whole = whole * 10 + (*p - '0');
        if(whole > 2147483647) return false; // would not fit even before scaling
        p++;
    }
    int64_t scale = 1;
    for(int i = 0; i < decimals; i++)
        scale *= 10;
    int64_t value = whole * scale;
    if(*p == '.') {
        p++;
        int64_t place = scale;
        int round_digit = 0;
        for(int i = 0; *p >= '0' && *p <= '9'; p++, i++) {
            if(i < decimals) {
                place /= 10;
                value += (*p - '0') * place;
            } else if(i == decimals) {
                round_digit = *p - '0';
            }
        }
        if(round_digit >= 5) value += 1;
    }
    if(value > 2147483647) return false;
    *out = (int32_t)(neg ? -value : value);
    return true;
}

/** Unsigned whole number up to 4,294,967,295 (timestamps). */
static inline bool mj_u32(const char* json, const char* key, uint32_t* out) {
    const char* p = mj_value(json, key);
    if(!p || *p < '0' || *p > '9') return false;
    uint64_t v = 0;
    while(*p >= '0' && *p <= '9') {
        v = v * 10 + (uint64_t)(*p - '0');
        if(v > 4294967295ull) return false;
        p++;
    }
    *out = (uint32_t)v;
    return true;
}

static inline bool mj_scaled(const char* json, const char* key, int decimals, int32_t* out) {
    const char* p = mj_value(json, key);
    return p && mj_parse_scaled(p, decimals, out);
}

static inline bool mj_int(const char* json, const char* key, int32_t* out) {
    return mj_scaled(json, key, 0, out);
}

static inline bool mj_bool(const char* json, const char* key, bool* out) {
    const char* p = mj_value(json, key);
    if(!p) return false;
    if(strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if(strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

/** Copy the balanced { ... } or [ ... ] starting at p (string-aware) into out. */
static inline bool mj_copy_balanced(const char* p, char* out, size_t cap) {
    char open = *p;
    char close = open == '{' ? '}' : (open == '[' ? ']' : 0);
    if(!close || cap < 3) return false;
    int depth = 0;
    bool in_string = false;
    size_t n = 0;
    for(; *p; p++) {
        char c = *p;
        if(n + 1 >= cap) return false; // does not fit
        out[n++] = c;
        if(in_string) {
            if(c == '\\' && p[1]) {
                if(n + 1 >= cap) return false;
                out[n++] = *++p;
            } else if(c == '"') {
                in_string = false;
            }
        } else if(c == '"') {
            in_string = true;
        } else if(c == open) {
            depth++;
        } else if(c == close) {
            if(--depth == 0) {
                out[n] = '\0';
                return true;
            }
        }
    }
    return false; // truncated reply
}

/** Copy the object value of "key" ({...}) into out so lookups can be scoped to it. */
static inline bool mj_object(const char* json, const char* key, char* out, size_t cap) {
    const char* p = mj_value(json, key);
    return p && *p == '{' && mj_copy_balanced(p, out, cap);
}

/** Copy element `index` of the array "key", when its elements are objects, into out. */
static inline bool
    mj_array_object(const char* json, const char* key, uint32_t index, char* out, size_t cap) {
    const char* p = mj_value(json, key);
    if(!p || *p != '[') return false;
    p++;
    for(uint32_t i = 0;; i++) {
        while(mj_is_ws(*p) || *p == ',')
            p++;
        if(*p != '{') return false;
        if(i == index) return mj_copy_balanced(p, out, cap);
        // skip this object
        int depth = 0;
        bool in_string = false;
        for(; *p; p++) {
            if(in_string) {
                if(*p == '\\' && p[1]) {
                    p++;
                } else if(*p == '"') {
                    in_string = false;
                }
            } else if(*p == '"') {
                in_string = true;
            } else if(*p == '{') {
                depth++;
            } else if(*p == '}' && --depth == 0) {
                p++;
                break;
            }
        }
        if(depth != 0) return false;
    }
}

/** Element `index` of a numeric array, scaled like mj_scaled. */
static inline bool
    mj_array_scaled(const char* json, const char* key, uint32_t index, int decimals, int32_t* out) {
    const char* p = mj_value(json, key);
    if(!p || *p != '[') return false;
    p++;
    for(uint32_t i = 0;; i++) {
        while(mj_is_ws(*p) || *p == ',')
            p++;
        if(*p == ']' || *p == '\0') return false;
        if(i == index) return mj_parse_scaled(p, decimals, out);
        while(*p && *p != ',' && *p != ']')
            p++;
    }
}

/** End (one past the closing brace) of the balanced {..} or [..] starting at p, without copying.
 * Use this for large objects such as geocoding results. */
static inline const char* mj_span_end(const char* p) {
    char open = *p;
    char close = open == '{' ? '}' : (open == '[' ? ']' : 0);
    if(!close) return NULL;
    int depth = 0;
    bool in_string = false;
    for(; *p; p++) {
        char c = *p;
        if(in_string) {
            if(c == '\\' && p[1]) {
                p++;
            } else if(c == '"') {
                in_string = false;
            }
        } else if(c == '"') {
            in_string = true;
        } else if(c == open) {
            depth++;
        } else if(c == close && --depth == 0) {
            return p + 1;
        }
    }
    return NULL;
}

/** Start and end of element 0 of the object-array "key". */
static inline bool mj_first_object(const char* json, const char* key, const char** start, const char** end) {
    const char* p = mj_value(json, key);
    if(!p || *p != '[') return false;
    p++;
    while(mj_is_ws(*p))
        p++;
    if(*p != '{') return false;
    const char* e = mj_span_end(p);
    if(!e) return false;
    *start = p;
    *end = e;
    return true;
}

/** Like mj_value, but only accepts a match inside [start, end). */
static inline const char* mj_value_in(const char* start, const char* end, const char* key) {
    const char* v = mj_value(start, key);
    return (v && v < end) ? v : NULL;
}
