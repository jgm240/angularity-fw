#include "security_log.h"

#include <furi_hal.h>
#include <storage/storage.h>
#include <toolbox/stream/stream.h>
#include <toolbox/stream/file_stream.h>

// Rotate (drop) the log once it exceeds this size, rather than growing forever
#define SECURITY_LOG_MAX_BYTES 4096

void security_log_add(const char* message) {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    FileInfo info;
    if(storage_common_stat(storage, SECURITY_LOG_PATH, &info) == FSE_OK &&
       info.size > SECURITY_LOG_MAX_BYTES) {
        storage_common_remove(storage, SECURITY_LOG_PATH);
    }

    Stream* stream = file_stream_alloc(storage);
    if(file_stream_open(stream, SECURITY_LOG_PATH, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        DateTime dt;
        furi_hal_rtc_get_datetime(&dt);
        FuriString* line = furi_string_alloc();
        furi_string_printf(
            line,
            "%02u:%02u:%02u %02u/%02u %s\n",
            dt.hour,
            dt.minute,
            dt.second,
            dt.day,
            dt.month,
            message);
        stream_write_string(stream, line);
        furi_string_free(line);
    }
    file_stream_close(stream);
    stream_free(stream);

    furi_record_close(RECORD_STORAGE);
}

void security_log_read(FuriString* out) {
    furi_string_reset(out);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    if(file_stream_open(stream, SECURITY_LOG_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FuriString* line = furi_string_alloc();
        while(stream_read_line(stream, line)) {
            furi_string_cat(out, line);
        }
        furi_string_free(line);
    }
    file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);

    if(furi_string_empty(out)) {
        furi_string_set(out, "No security events logged yet.");
    }
}
