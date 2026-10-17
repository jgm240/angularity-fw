#pragma once

// Tiny JSON field readers for the flat latest.json written by
// tools/make_ota_directory.py. Header-only so it can be tested on a desktop.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* ota_json_find_value(const char* json, const char* key) {
    char pattern[40];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if(!p) return NULL;
    p += strlen(pattern);
    while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if(*p != ':') return NULL;
    p++;
    while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

static bool ota_json_string(const char* json, const char* key, char* out, size_t cap) {
    const char* p = ota_json_find_value(json, key);
    if(!p || *p != '"' || cap == 0) return false;
    p++;
    size_t n = 0;
    while(*p && *p != '"') {
        char c = *p++;
        if(c == '\\' && *p) {
            c = *p++;
            if(c == 'n' || c == 't' || c == 'r') c = ' ';
        }
        if(n + 1 < cap) out[n++] = c;
    }
    out[n] = '\0';
    return *p == '"';
}

static bool ota_json_u32(const char* json, const char* key, uint32_t* out) {
    const char* p = ota_json_find_value(json, key);
    if(!p || *p < '0' || *p > '9') return false;
    *out = (uint32_t)strtoul(p, NULL, 10);
    return true;
}

/** Read a JSON array of strings, e.g. "networks":["a","b"]. Items are copied into `out`
 * (max_items slots of item_cap bytes each). Truncated or unterminated trailing items are
 * dropped. Returns the number of items read. */
static size_t ota_json_string_array(
    const char* json,
    const char* key,
    char* out,
    size_t item_cap,
    size_t max_items) {
    const char* p = ota_json_find_value(json, key);
    if(!p || *p != '[' || item_cap < 2) return 0;
    p++;
    size_t count = 0;
    while(*p && *p != ']' && count < max_items) {
        while(*p && *p != '"' && *p != ']')
            p++;
        if(*p != '"') break;
        p++;
        char* dst = out + count * item_cap;
        size_t n = 0;
        while(*p && *p != '"') {
            char c = *p++;
            if(c == '\\' && *p) {
                c = *p++;
                if(c == 'n' || c == 't' || c == 'r') c = ' ';
            }
            if(n + 1 < item_cap) dst[n++] = c;
        }
        if(*p != '"') break; // unterminated: the line was cut off
        p++;
        dst[n] = '\0';
        if(n > 0) count++;
    }
    return count;
}

/** True once the array for `key` has its closing bracket, i.e. the whole list has arrived. */
static bool ota_json_array_complete(const char* json, const char* key) {
    const char* p = ota_json_find_value(json, key);
    if(!p || *p != '[') return false;
    bool in_string = false;
    for(p++; *p; p++) {
        if(in_string) {
            if(*p == '\\' && p[1]) {
                p++;
            } else if(*p == '"') {
                in_string = false;
            }
        } else if(*p == '"') {
            in_string = true;
        } else if(*p == ']') {
            return true;
        }
    }
    return false;
}

/** Escape a string for use inside a JSON string literal. Returns false if it did not fit. */
static bool ota_json_escape(const char* in, char* out, size_t cap) {
    size_t n = 0;
    for(; *in; in++) {
        unsigned char c = (unsigned char)*in;
        if(c == '"' || c == '\\') {
            if(n + 2 >= cap) return false;
            out[n++] = '\\';
            out[n++] = (char)c;
        } else if(c < 0x20) {
            continue; // control characters have no place in an SSID or password
        } else {
            if(n + 1 >= cap) return false;
            out[n++] = (char)c;
        }
    }
    out[n] = '\0';
    return true;
}

/** Finds a dotted-quad IPv4 address in a line of text. */
static bool ota_find_ipv4(const char* text, char* out, size_t cap) {
    for(const char* p = text; *p; p++) {
        if(*p < '0' || *p > '9') continue;
        if(p > text && ((p[-1] >= '0' && p[-1] <= '9') || p[-1] == '.')) continue;
        int dots = 0, digits = 0;
        const char* q = p;
        while((*q >= '0' && *q <= '9') || *q == '.') {
            if(*q == '.') {
                dots++;
                digits = 0;
            } else if(++digits > 3) {
                break;
            }
            q++;
        }
        if(dots == 3 && q > p + 6 && q[-1] != '.' && (size_t)(q - p) < cap) {
            memcpy(out, p, (size_t)(q - p));
            out[q - p] = '\0';
            return true;
        }
    }
    return false;
}
