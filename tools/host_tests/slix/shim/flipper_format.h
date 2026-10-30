#pragma once
#include "furi.h"
typedef struct FlipperFormat FlipperFormat;
bool flipper_format_rewind(FlipperFormat* ff);
bool flipper_format_key_exist(FlipperFormat* ff, const char* key);
bool flipper_format_get_value_count(FlipperFormat* ff, const char* key, uint32_t* count);
bool flipper_format_read_string(FlipperFormat* ff, const char* key, FuriString* data);
bool flipper_format_write_string_cstr(FlipperFormat* ff, const char* key, const char* data);
bool flipper_format_read_uint32(FlipperFormat* ff, const char* key, uint32_t* data, const uint16_t n);
bool flipper_format_write_uint32(FlipperFormat* ff, const char* key, const uint32_t* data, const uint16_t n);
bool flipper_format_read_bool(FlipperFormat* ff, const char* key, bool* data, const uint16_t n);
bool flipper_format_write_bool(FlipperFormat* ff, const char* key, const bool* data, const uint16_t n);
bool flipper_format_read_hex(FlipperFormat* ff, const char* key, uint8_t* data, const uint16_t n);
bool flipper_format_write_hex(FlipperFormat* ff, const char* key, const uint8_t* data, const uint16_t n);
bool flipper_format_write_comment_cstr(FlipperFormat* ff, const char* data);
