#include "app_permissions_scan.h"
#include "app_permissions_core.h"

static bool scan_read(void* ctx, uint32_t offset, void* buf, uint32_t len) {
    File* file = ctx;
    return storage_file_seek(file, offset, true) && storage_file_read(file, buf, len) == len;
}

bool app_permissions_scan_file(Storage* storage, const char* path, uint32_t* mask) {
    File* file = storage_file_alloc(storage);
    uint8_t* scratch = malloc(APP_PERM_SYM_CHUNK * APP_PERM_SYM_SIZE);
    bool ok = false;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        ok = app_perm_scan_elf(scan_read, file, scratch, mask);
    }
    storage_file_close(file);
    storage_file_free(file);
    free(scratch);
    return ok;
}
