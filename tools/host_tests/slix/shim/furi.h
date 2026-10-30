#pragma once
/* Just enough of Furi to compile the NFC protocol sources on a computer. furi_check() reports
 * and longjmps instead of crashing the device, so the harness can list every failing request.
 * furi_assert() is compiled out like in release firmware builds (define FURI_HOST_ASSERTS to
 * make it fatal too). */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>

/* Furi's malloc() returns zeroed memory */
#define malloc(size) calloc(1, (size))

extern jmp_buf furi_host_jmp;
extern int furi_host_armed;
extern char furi_host_msg[256];
void furi_host_fail(const char* expr, const char* file, int line);

#define furi_check(x) \
    do { \
        if(!(x)) furi_host_fail(#x, __FILE__, __LINE__); \
    } while(0)
#ifdef FURI_HOST_ASSERTS
#define furi_assert(x) furi_check(x)
#else
#define furi_assert(x) ((void)0)
#endif
#define furi_crash(msg) furi_host_fail(msg, __FILE__, __LINE__)

#define UNUSED(x) ((void)(x))
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#define COUNT_OF(x) (sizeof(x) / sizeof((x)[0]))
#define REVERSE_BYTES_U32(x) \
    ((((x) & 0x000000FFU) << 24) | (((x) & 0x0000FF00U) << 8) | (((x) & 0x00FF0000U) >> 8) | \
     (((x) & 0xFF000000U) >> 24))
#define FURI_BIT(x, n) (((x) >> (n)) & 1)
#define FURI_BIT_SET(x, n) ((x) |= (1 << (n)))
#define FURI_BIT_CLEAR(x, n) ((x) &= ~(1 << (n)))
#define FURI_WARN_UNUSED
#define FURI_LOG_E(tag, ...) ((void)0)
#define FURI_LOG_W(tag, ...) ((void)0)
#define FURI_LOG_I(tag, ...) ((void)0)
#define FURI_LOG_D(tag, ...) ((void)0)
#define FURI_LOG_T(tag, ...) ((void)0)

typedef struct FuriString FuriString;
FuriString* furi_string_alloc(void);
void furi_string_free(FuriString* string);
const char* furi_string_get_cstr(const FuriString* string);
void furi_string_set_str(FuriString* string, const char source[]);
void furi_string_cat(FuriString* string_1, const FuriString* string_2);
void furi_string_cat_str(FuriString* string_1, const char cstring_2[]);
int furi_string_cmp_str(const FuriString* string_1, const char cstring_2[]);
bool furi_string_equal(const FuriString* string_1, const FuriString* string_2);
