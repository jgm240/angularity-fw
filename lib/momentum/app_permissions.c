#include "app_permissions.h"

#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#define APP_PERMISSIONS_MAGIC       0x5A
#define APP_PERMISSIONS_VERSION     1
#define APP_PERMISSIONS_MAX_ENTRIES 48

typedef struct {
    uint32_t path_hash;
    uint32_t size;
    uint8_t requested;
    uint8_t allowed;
    uint8_t reserved[2];
} AppPermissionEntry;

typedef struct {
    bool enabled;
    uint8_t count;
    uint8_t next_evict; // simple ring: oldest answers are replaced when full
    uint8_t reserved;
    AppPermissionEntry entries[APP_PERMISSIONS_MAX_ENTRIES];
} AppPermissionsFile;

static void app_permissions_load(AppPermissionsFile* file) {
    memset(file, 0, sizeof(AppPermissionsFile));
    if(!saved_struct_load(
           APP_PERMISSIONS_PATH,
           file,
           sizeof(AppPermissionsFile),
           APP_PERMISSIONS_MAGIC,
           APP_PERMISSIONS_VERSION)) {
        memset(file, 0, sizeof(AppPermissionsFile));
    }
    if(file->count > APP_PERMISSIONS_MAX_ENTRIES) file->count = 0;
    if(file->next_evict >= APP_PERMISSIONS_MAX_ENTRIES) file->next_evict = 0;
}

static void app_permissions_save(const AppPermissionsFile* file) {
    saved_struct_save(
        APP_PERMISSIONS_PATH,
        file,
        sizeof(AppPermissionsFile),
        APP_PERMISSIONS_MAGIC,
        APP_PERMISSIONS_VERSION);
}

// FNV-1a over the app path
static uint32_t path_hash(const char* path) {
    uint32_t h = 2166136261u;
    for(; *path; path++) {
        h ^= (uint8_t)*path;
        h *= 16777619u;
    }
    return h;
}

bool app_permissions_is_enabled(void) {
    AppPermissionsFile file;
    app_permissions_load(&file);
    return file.enabled;
}

void app_permissions_set_enabled(bool enabled) {
    AppPermissionsFile file;
    app_permissions_load(&file);
    file.enabled = enabled;
    app_permissions_save(&file);
}

uint32_t app_permissions_count(void) {
    AppPermissionsFile file;
    app_permissions_load(&file);
    return file.count;
}

void app_permissions_forget_all(void) {
    AppPermissionsFile file;
    app_permissions_load(&file);
    file.count = 0;
    file.next_evict = 0;
    memset(file.entries, 0, sizeof(file.entries));
    app_permissions_save(&file);
}

static bool has_prefix(const char* s, const char* prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

uint32_t app_permissions_classify(const char* name) {
    static const char* const radio[] = {
        "furi_hal_subghz", "subghz",   "furi_hal_nfc", "nfc_",       "felica",     "mf_classic",
        "mf_ultralight",   "mf_desfire", "mf_plus",    "iso14443",   "iso15693",   "slix",
        "furi_hal_rfid",   "lfrfid",   "furi_hal_infrared", "infrared", "furi_hal_ibutton",
        "ibutton",         "onewire",  "furi_hal_nfc_",
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
    for(size_t i = 0; i < COUNT_OF(radio); i++)
        if(has_prefix(name, radio[i])) mask |= AppPermissionRadio;
    for(size_t i = 0; i < COUNT_OF(gpio); i++)
        if(has_prefix(name, gpio[i])) mask |= AppPermissionGpio;
    for(size_t i = 0; i < COUNT_OF(usb); i++)
        if(has_prefix(name, usb[i])) mask |= AppPermissionUsb;
    for(size_t i = 0; i < COUNT_OF(bt); i++)
        if(has_prefix(name, bt[i])) mask |= AppPermissionBluetooth;
    return mask;
}

bool app_permissions_lookup(
    const char* path,
    uint32_t file_size,
    uint32_t* requested,
    uint32_t* allowed) {
    AppPermissionsFile file;
    app_permissions_load(&file);
    uint32_t hash = path_hash(path);
    for(uint8_t i = 0; i < file.count; i++) {
        const AppPermissionEntry* e = &file.entries[i];
        if(e->path_hash == hash && e->size == file_size) {
            *requested = e->requested;
            *allowed = e->allowed;
            return true;
        }
    }
    return false;
}

void app_permissions_store(
    const char* path,
    uint32_t file_size,
    uint32_t requested,
    uint32_t allowed) {
    AppPermissionsFile file;
    app_permissions_load(&file);
    uint32_t hash = path_hash(path);

    AppPermissionEntry* slot = NULL;
    for(uint8_t i = 0; i < file.count; i++) {
        if(file.entries[i].path_hash == hash) {
            slot = &file.entries[i];
            break;
        }
    }
    if(!slot) {
        if(file.count < APP_PERMISSIONS_MAX_ENTRIES) {
            slot = &file.entries[file.count++];
        } else {
            slot = &file.entries[file.next_evict];
            file.next_evict = (file.next_evict + 1) % APP_PERMISSIONS_MAX_ENTRIES;
        }
    }
    slot->path_hash = hash;
    slot->size = file_size;
    slot->requested = requested;
    slot->allowed = allowed;
    app_permissions_save(&file);
}

void app_permissions_describe(uint32_t mask, FuriString* out) {
    furi_string_reset(out);
    const struct {
        uint32_t bit;
        const char* name;
    } names[] = {
        {AppPermissionRadio, "Radio"},
        {AppPermissionGpio, "GPIO"},
        {AppPermissionUsb, "USB"},
        {AppPermissionBluetooth, "Bluetooth"},
    };
    for(size_t i = 0; i < COUNT_OF(names); i++) {
        if(mask & names[i].bit) {
            if(!furi_string_empty(out)) furi_string_cat_str(out, ", ");
            furi_string_cat_str(out, names[i].name);
        }
    }
    if(furi_string_empty(out)) furi_string_set_str(out, "Nothing");
}
