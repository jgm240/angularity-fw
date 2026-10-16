#include "app_permissions.h"
#include "app_permissions_core.h"

#include <storage/storage.h>
#include <toolbox/saved_struct.h>

#define APP_PERMISSIONS_MAGIC       0x5A
#define APP_PERMISSIONS_VERSION     2 /* v2: split Radio into Radio/NFC/RFID/IR, added watched mask */
#define APP_PERMISSIONS_MAX_ENTRIES 48

// The values in app_permissions.h and the scanner core must agree
_Static_assert(AppPermissionRadio == APP_PERM_RADIO, "radio bit");
_Static_assert(AppPermissionGpio == APP_PERM_GPIO, "gpio bit");
_Static_assert(AppPermissionUsb == APP_PERM_USB, "usb bit");
_Static_assert(AppPermissionBluetooth == APP_PERM_BLUETOOTH, "bluetooth bit");
_Static_assert(AppPermissionRfid == APP_PERM_RFID, "rfid bit");
_Static_assert(AppPermissionNfc == APP_PERM_NFC, "nfc bit");
_Static_assert(AppPermissionInfrared == APP_PERM_IR, "ir bit");
_Static_assert(APP_PERMISSIONS_ALL == APP_PERM_ALL, "all bits");

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
    uint8_t watched; // permission kinds the user wants to be asked about
    AppPermissionEntry entries[APP_PERMISSIONS_MAX_ENTRIES];
} AppPermissionsFile;

// The loader and other service threads have small stacks, so the ~600 byte
// file image always lives on the heap.
static AppPermissionsFile* app_permissions_open(void) {
    AppPermissionsFile* file = malloc(sizeof(AppPermissionsFile));
    if(!saved_struct_load(
           APP_PERMISSIONS_PATH,
           file,
           sizeof(AppPermissionsFile),
           APP_PERMISSIONS_MAGIC,
           APP_PERMISSIONS_VERSION)) {
        memset(file, 0, sizeof(AppPermissionsFile));
        file->watched = APP_PERM_ALL;
    }
    if(file->count > APP_PERMISSIONS_MAX_ENTRIES) file->count = 0;
    if(file->next_evict >= APP_PERMISSIONS_MAX_ENTRIES) file->next_evict = 0;
    return file;
}

static void app_permissions_commit(AppPermissionsFile* file) {
    saved_struct_save(
        APP_PERMISSIONS_PATH,
        file,
        sizeof(AppPermissionsFile),
        APP_PERMISSIONS_MAGIC,
        APP_PERMISSIONS_VERSION);
    free(file);
}

static void app_permissions_discard(AppPermissionsFile* file) {
    free(file);
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
    AppPermissionsFile* file = app_permissions_open();
    bool enabled = file->enabled;
    app_permissions_discard(file);
    return enabled;
}

void app_permissions_set_enabled(bool enabled) {
    AppPermissionsFile* file = app_permissions_open();
    file->enabled = enabled;
    app_permissions_commit(file);
}

uint32_t app_permissions_get_watched(void) {
    AppPermissionsFile* file = app_permissions_open();
    uint32_t watched = file->watched;
    app_permissions_discard(file);
    return watched;
}

void app_permissions_set_watched(uint32_t watched) {
    AppPermissionsFile* file = app_permissions_open();
    file->watched = (uint8_t)(watched & APP_PERM_ALL);
    app_permissions_commit(file);
}

uint32_t app_permissions_count(void) {
    AppPermissionsFile* file = app_permissions_open();
    uint32_t count = file->count;
    app_permissions_discard(file);
    return count;
}

void app_permissions_forget_all(void) {
    AppPermissionsFile* file = app_permissions_open();
    file->count = 0;
    file->next_evict = 0;
    memset(file->entries, 0, sizeof(file->entries));
    app_permissions_commit(file);
}

uint32_t app_permissions_classify(const char* name) {
    return app_perm_classify_name(name);
}

bool app_permissions_lookup(
    const char* path,
    uint32_t file_size,
    uint32_t* requested,
    uint32_t* allowed) {
    AppPermissionsFile* file = app_permissions_open();
    uint32_t hash = path_hash(path);
    bool found = false;
    for(uint8_t i = 0; i < file->count; i++) {
        const AppPermissionEntry* e = &file->entries[i];
        if(e->path_hash == hash && e->size == file_size) {
            *requested = e->requested;
            *allowed = e->allowed;
            found = true;
            break;
        }
    }
    app_permissions_discard(file);
    return found;
}

void app_permissions_store(
    const char* path,
    uint32_t file_size,
    uint32_t requested,
    uint32_t allowed) {
    AppPermissionsFile* file = app_permissions_open();
    uint32_t hash = path_hash(path);

    AppPermissionEntry* slot = NULL;
    for(uint8_t i = 0; i < file->count; i++) {
        if(file->entries[i].path_hash == hash) {
            slot = &file->entries[i];
            break;
        }
    }
    if(!slot) {
        if(file->count < APP_PERMISSIONS_MAX_ENTRIES) {
            slot = &file->entries[file->count++];
        } else {
            slot = &file->entries[file->next_evict];
            file->next_evict = (file->next_evict + 1) % APP_PERMISSIONS_MAX_ENTRIES;
        }
    }
    slot->path_hash = hash;
    slot->size = file_size;
    slot->requested = requested;
    slot->allowed = allowed;
    app_permissions_commit(file);
}

void app_permissions_describe(uint32_t mask, FuriString* out) {
    furi_string_reset(out);
    const struct {
        uint32_t bit;
        const char* name;
    } names[] = {
        {AppPermissionRadio, "Radio"},
        {AppPermissionNfc, "NFC"},
        {AppPermissionRfid, "RFID"},
        {AppPermissionInfrared, "IR"},
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
