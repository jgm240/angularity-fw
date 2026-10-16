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
