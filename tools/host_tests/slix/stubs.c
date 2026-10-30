/* Replacements for the parts of Furi / the NFC stack that the code under test calls but that
 * are irrelevant on a computer. */
#include "furi.h"
#include <nfc/nfc.h>
#include <toolbox/bit_buffer.h>

jmp_buf furi_host_jmp;
int furi_host_armed = 0;
char furi_host_msg[256];

void furi_host_fail(const char* expr, const char* file, int line) {
    const char* base = strrchr(file, '/');
    snprintf(furi_host_msg, sizeof(furi_host_msg), "furi_check(%s) at %s:%d", expr, base ? base + 1 : file, line);
    if(!furi_host_armed) {
        fprintf(stderr, "%s outside a test case\n", furi_host_msg);
        abort();
    }
    furi_host_armed = 0;
    longjmp(furi_host_jmp, 1);
}

uint32_t furi_hal_random_get(void) {
    return (uint32_t)rand();
}

/* ---- what the radio would send, captured for the harness */
BitBuffer* host_last_tx = NULL;
int host_tx_count = 0;

void nfc_config(Nfc* instance, NfcMode mode, NfcTech tech) {
    (void)instance; (void)mode; (void)tech;
}
void nfc_set_fdt_listen_fc(Nfc* instance, uint32_t fdt) {
    (void)instance; (void)fdt;
}
NfcError nfc_listener_tx(Nfc* instance, const BitBuffer* tx_buffer) {
    (void)instance;
    if(!host_last_tx) host_last_tx = bit_buffer_alloc(512);
    bit_buffer_copy(host_last_tx, tx_buffer);
    host_tx_count++;
    return NfcErrorNone;
}

/* ---- FlipperFormat / FuriString are only used by load/save, which the harness never calls */
struct FuriString { char text[8]; };
FuriString* furi_string_alloc(void) { return calloc(1, sizeof(FuriString)); }
void furi_string_free(FuriString* s) { free(s); }
const char* furi_string_get_cstr(const FuriString* s) { return s->text; }
void furi_string_set_str(FuriString* s, const char src[]) { (void)s; (void)src; }
void furi_string_cat(FuriString* a, const FuriString* b) { (void)a; (void)b; }
void furi_string_cat_str(FuriString* a, const char b[]) { (void)a; (void)b; }
int furi_string_cmp_str(const FuriString* a, const char b[]) { (void)a; (void)b; return 1; }
bool furi_string_equal(const FuriString* a, const FuriString* b) { (void)a; (void)b; return false; }

typedef struct FlipperFormat FlipperFormat;
bool flipper_format_rewind(FlipperFormat* f) { (void)f; return false; }
bool flipper_format_key_exist(FlipperFormat* f, const char* k) { (void)f; (void)k; return false; }
bool flipper_format_get_value_count(FlipperFormat* f, const char* k, uint32_t* c) { (void)f; (void)k; (void)c; return false; }
bool flipper_format_read_string(FlipperFormat* f, const char* k, FuriString* d) { (void)f; (void)k; (void)d; return false; }
bool flipper_format_write_string_cstr(FlipperFormat* f, const char* k, const char* d) { (void)f; (void)k; (void)d; return false; }
bool flipper_format_read_uint32(FlipperFormat* f, const char* k, uint32_t* d, const uint16_t n) { (void)f; (void)k; (void)d; (void)n; return false; }
bool flipper_format_write_uint32(FlipperFormat* f, const char* k, const uint32_t* d, const uint16_t n) { (void)f; (void)k; (void)d; (void)n; return false; }
bool flipper_format_read_bool(FlipperFormat* f, const char* k, bool* d, const uint16_t n) { (void)f; (void)k; (void)d; (void)n; return false; }
bool flipper_format_write_bool(FlipperFormat* f, const char* k, const bool* d, const uint16_t n) { (void)f; (void)k; (void)d; (void)n; return false; }
bool flipper_format_read_hex(FlipperFormat* f, const char* k, uint8_t* d, const uint16_t n) { (void)f; (void)k; (void)d; (void)n; return false; }
bool flipper_format_write_hex(FlipperFormat* f, const char* k, const uint8_t* d, const uint16_t n) { (void)f; (void)k; (void)d; (void)n; return false; }
bool flipper_format_write_comment_cstr(FlipperFormat* f, const char* d) { (void)f; (void)d; return false; }
