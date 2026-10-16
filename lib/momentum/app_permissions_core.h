#pragma once

// Pure-C core of the app permission scanner: classifies imported symbol names
// and walks an ELF32 symbol table through a caller-supplied read function.
// No firmware dependencies, so it can be unit-tested on a desktop compiler.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define APP_PERM_RADIO     (1u << 0)
#define APP_PERM_GPIO      (1u << 1)
#define APP_PERM_USB       (1u << 2)
#define APP_PERM_BLUETOOTH (1u << 3)

static inline bool app_perm_has_prefix(const char* s, const char* prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static inline uint32_t app_perm_classify_name(const char* name) {
    static const char* const radio[] = {
        "furi_hal_subghz", "subghz",        "furi_hal_nfc",  "nfc_",       "felica",
        "mf_classic",      "mf_ultralight", "mf_desfire",    "mf_plus",    "iso14443",
        "iso15693",        "slix",          "furi_hal_rfid", "lfrfid",     "furi_hal_infrared",
        "infrared",        "furi_hal_ibutton", "ibutton",    "onewire",
    };
    static const char* const gpio[] = {
        "furi_hal_gpio",
        "furi_hal_serial",
        "furi_hal_i2c",
        "furi_hal_spi",
        "furi_hal_pwm",
        "furi_hal_adc",
        "gpio_ext_",
        "gpio_pins",
        "expansion_",
    };
    static const char* const usb[] = {"furi_hal_usb", "furi_hal_hid", "furi_hal_cdc"};
    static const char* const bt[] = {"furi_hal_bt", "bt_", "ble_profile"};

    uint32_t mask = 0;
    for(unsigned i = 0; i < sizeof(radio) / sizeof(radio[0]); i++)
        if(app_perm_has_prefix(name, radio[i])) mask |= APP_PERM_RADIO;
    for(unsigned i = 0; i < sizeof(gpio) / sizeof(gpio[0]); i++)
        if(app_perm_has_prefix(name, gpio[i])) mask |= APP_PERM_GPIO;
    for(unsigned i = 0; i < sizeof(usb) / sizeof(usb[0]); i++)
        if(app_perm_has_prefix(name, usb[i])) mask |= APP_PERM_USB;
    for(unsigned i = 0; i < sizeof(bt) / sizeof(bt[0]); i++)
        if(app_perm_has_prefix(name, bt[i])) mask |= APP_PERM_BLUETOOTH;
    return mask;
}

/** Reads `len` bytes at absolute file `offset` into `buf`. Returns false on any error/short read. */
typedef bool (*AppPermReadFn)(void* ctx, uint32_t offset, void* buf, uint32_t len);

static inline uint16_t app_perm_u16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint32_t app_perm_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#define APP_PERM_SYM_SIZE   16u
#define APP_PERM_SYM_CHUNK  32u // symbols read per call
#define APP_PERM_NAME_BYTES 32u // enough for every known prefix
#define APP_PERM_MAX_SYMS   20000u

/** Walk the undefined symbols of an ARM ELF32 file and OR together their permission bits.
 *  `scratch` must hold APP_PERM_SYM_CHUNK * APP_PERM_SYM_SIZE bytes (512).
 *  Returns false if the file is not a readable ELF32. */
static inline bool app_perm_scan_elf(AppPermReadFn read, void* ctx, uint8_t* scratch, uint32_t* mask_out) {
    uint8_t hdr[52];
    if(!read(ctx, 0, hdr, sizeof(hdr))) return false;
    if(hdr[0] != 0x7f || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F' || hdr[4] != 1) return false;

    uint32_t shoff = app_perm_u32(&hdr[32]);
    uint16_t shentsize = app_perm_u16(&hdr[46]);
    uint16_t shnum = app_perm_u16(&hdr[48]);
    if(shentsize < 40 || shnum == 0 || shnum > 256) return false;

    uint32_t sym_off = 0, sym_size = 0, str_idx = 0;
    bool have_symtab = false;
    uint8_t sh[40];
    for(uint16_t i = 0; i < shnum; i++) {
        if(!read(ctx, shoff + (uint32_t)i * shentsize, sh, sizeof(sh))) return false;
        if(app_perm_u32(&sh[4]) == 2) { // SHT_SYMTAB
            sym_off = app_perm_u32(&sh[16]);
            sym_size = app_perm_u32(&sh[20]);
            str_idx = app_perm_u32(&sh[24]);
            have_symtab = true;
            break;
        }
    }
    if(!have_symtab || str_idx >= shnum) return false;
    if(!read(ctx, shoff + str_idx * shentsize, sh, sizeof(sh))) return false;
    uint32_t str_off = app_perm_u32(&sh[16]);
    uint32_t str_size = app_perm_u32(&sh[20]);

    uint32_t count = sym_size / APP_PERM_SYM_SIZE;
    if(count > APP_PERM_MAX_SYMS) count = APP_PERM_MAX_SYMS;

    uint32_t mask = 0;
    for(uint32_t base = 0; base < count; base += APP_PERM_SYM_CHUNK) {
        uint32_t n = count - base;
        if(n > APP_PERM_SYM_CHUNK) n = APP_PERM_SYM_CHUNK;
        if(!read(ctx, sym_off + base * APP_PERM_SYM_SIZE, scratch, n * APP_PERM_SYM_SIZE)) return false;
        for(uint32_t i = 0; i < n; i++) {
            const uint8_t* s = &scratch[i * APP_PERM_SYM_SIZE];
            uint32_t st_name = app_perm_u32(&s[0]);
            uint16_t st_shndx = app_perm_u16(&s[14]);
            if(st_shndx != 0 || st_name == 0 || st_name >= str_size) continue; // not an import

            char name[APP_PERM_NAME_BYTES + 1];
            uint32_t want = APP_PERM_NAME_BYTES;
            if(st_name + want > str_size) want = str_size - st_name;
            if(!read(ctx, str_off + st_name, name, want)) continue;
            name[want] = '\0';
            mask |= app_perm_classify_name(name);
        }
    }
    *mask_out = mask;
    return true;
}
