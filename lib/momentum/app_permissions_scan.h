#pragma once

// Firmware-internal (not exported to apps): used by the loader.

#include <furi.h>
#include <storage/storage.h>

/** Read an app's ELF import table from the SD card and compute which permissions it needs
 * (AppPermission bit flags). Returns false if the file could not be parsed. */
bool app_permissions_scan_file(Storage* storage, const char* path, uint32_t* mask);
