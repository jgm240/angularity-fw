#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/elements.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>
#include <loader/loader.h>
#include <expansion/expansion.h>
#include <toolbox/tar/tar_archive.h>
#include <toolbox/version.h>
#include <momentum/settings.h>

#include "sha256.h"
#include "ota_json.h"

#define TAG "OtaUpdate"

// Small JSON describing the newest release (written by tools/make_ota_directory.py)
#define OTA_DEFAULT_URL "https://raw.githubusercontent.com/jgm240/angularity-fw/main/ota/latest.json"
#define OTA_URL_OVERRIDE_FILE EXT_PATH("apps_data/ota_update/url.txt")

#define OTA_DIR      EXT_PATH("update")
#define OTA_PART     EXT_PATH("update/.ota_download.part")
#define OTA_PART_ID  EXT_PATH("update/.ota_download.id")
#define OTA_TGZ      EXT_PATH("update/.ota_package.tgz")

#define UART_BAUD         115200
#define RX_STREAM_SIZE    8192
#define IO_BUFFER_SIZE    4096
#define JSON_BUFFER_SIZE  3072
#define CHUNK_SIZE        (128 * 1024)
#define CHUNK_RETRIES     4
#define IDLE_TIMEOUT_MS   20000
#define MIN_BATTERY_PCT   30

typedef enum {
    OtaViewMenu,
    OtaViewStatus,
    OtaViewResult,
} OtaView;

typedef enum {
    JobUpdate,
    JobCheck,
    JobTest,
} Job;

typedef enum {
    OtaEventJobDone = 1,
    OtaEventExit,
} OtaEvent;

typedef struct {
    char title[28];
    char line1[44];
    char line2[44];
    int16_t percent; // -1 = no bar
} StatusModel;

typedef struct {
    uint32_t status;
    uint32_t length;
    char error[64];
} HttpHead;

typedef struct {
    char version[64];
    char changelog[96];
    char url[200];
    char sha256[72];
    uint32_t size;
} Release;

typedef struct {
    Gui* gui;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    View* status;
    TextBox* result;
    FuriString* text;
    Storage* storage;
    DialogsApp* dialogs;
    Expansion* expansion;
    FuriThread* worker;
    volatile bool cancel;
    Job job;

    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx;
    volatile uint32_t rx_overflow;
    uint8_t* io;
} OtaApp;

// ------------------------------------------------------------ status UI

static void status_set(
    OtaApp* app,
    const char* title,
    const char* line1,
    const char* line2,
    int percent) {
    with_view_model(
        app->status,
        StatusModel * m,
        {
            if(title) snprintf(m->title, sizeof(m->title), "%s", title);
            snprintf(m->line1, sizeof(m->line1), "%s", line1 ? line1 : "");
            snprintf(m->line2, sizeof(m->line2), "%s", line2 ? line2 : "");
            m->percent = (int16_t)percent;
        },
        true);
}

static void status_draw(Canvas* canvas, void* model) {
    StatusModel* m = model;
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, m->title);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 28, m->line1);
    canvas_draw_str(canvas, 2, 39, m->line2);
    if(m->percent >= 0) {
        elements_progress_bar(canvas, 2, 44, 124, (float)m->percent / 100.0f);
    }
    canvas_draw_str(canvas, 2, 62, "Back = cancel");
}

static bool status_input(InputEvent* event, void* context) {
    OtaApp* app = context;
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        app->cancel = true;
    }
    return true;
}

// ------------------------------------------------------------ UART link

static void rx_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    OtaApp* app = context;
    if(event == FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        if(furi_stream_buffer_send(app->rx, &byte, 1, 0) != 1) {
            app->rx_overflow++;
        }
    }
}

static bool link_open(OtaApp* app) {
    FuriHalSerialId channel = momentum_settings.uart_esp_channel;
    app->expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(app->expansion);

    if(furi_hal_serial_control_is_busy(channel)) {
        expansion_enable(app->expansion);
        furi_record_close(RECORD_EXPANSION);
        app->expansion = NULL;
        return false;
    }
    app->rx = furi_stream_buffer_alloc(RX_STREAM_SIZE, 1);
    app->serial = furi_hal_serial_control_acquire(channel);
    if(!app->serial) {
        furi_stream_buffer_free(app->rx);
        app->rx = NULL;
        expansion_enable(app->expansion);
        furi_record_close(RECORD_EXPANSION);
        app->expansion = NULL;
        return false;
    }
    furi_hal_serial_init(app->serial, UART_BAUD);
    furi_hal_serial_enable_direction(app->serial, FuriHalSerialDirectionRx);
    furi_hal_serial_async_rx_start(app->serial, rx_callback, app, false);
    furi_hal_serial_tx_wait_complete(app->serial);
    return true;
}

static void link_close(OtaApp* app) {
    if(app->serial) {
        furi_hal_serial_async_rx_stop(app->serial);
        furi_hal_serial_disable_direction(app->serial, FuriHalSerialDirectionRx);
        furi_hal_serial_control_release(app->serial);
        furi_hal_serial_deinit(app->serial);
        app->serial = NULL;
    }
    if(app->rx) {
        furi_stream_buffer_free(app->rx);
        app->rx = NULL;
    }
    if(app->expansion) {
        expansion_enable(app->expansion);
        furi_record_close(RECORD_EXPANSION);
        app->expansion = NULL;
    }
}

static void link_flush(OtaApp* app) {
    uint8_t tmp[64];
    while(furi_stream_buffer_receive(app->rx, tmp, sizeof(tmp), 0) > 0) {
    }
}

static void link_send(OtaApp* app, const char* command) {
    size_t len = strlen(command);
    furi_hal_serial_tx(app->serial, (const uint8_t*)command, len);
    furi_hal_serial_tx(app->serial, (const uint8_t*)"\n", 1);
    furi_hal_serial_tx_wait_complete(app->serial);
}

static bool link_read_line(OtaApp* app, char* out, size_t cap, uint32_t timeout_ms) {
    size_t n = 0;
    uint32_t start = furi_get_tick();
    while(true) {
        uint32_t elapsed = furi_get_tick() - start;
        if(elapsed >= timeout_ms || app->cancel) return false;
        uint8_t byte;
        if(furi_stream_buffer_receive(app->rx, &byte, 1, furi_ms_to_ticks(timeout_ms - elapsed)) ==
           1) {
            if(byte == '\n') {
                while(n > 0 && out[n - 1] == '\r')
                    n--;
                out[n] = '\0';
                return true;
            }
            if(n + 1 < cap) out[n++] = byte;
        }
    }
}

static bool link_ping(OtaApp* app) {
    char line[128];
    for(int attempt = 0; attempt < 3 && !app->cancel; attempt++) {
        link_flush(app);
        link_send(app, "[PING]");
        uint32_t start = furi_get_tick();
        while(furi_get_tick() - start < 2000) {
            if(link_read_line(app, line, sizeof(line), 500) && strstr(line, "[PONG]")) {
                return true;
            }
        }
    }
    return false;
}

// ------------------------------------------------------------ HTTP over the devboard

// Sends [GET/BYTES] and reads the "[GET/SUCCESS]{...}" header. The body follows.
static bool http_begin(
    OtaApp* app,
    const char* url,
    bool ranged,
    uint32_t from,
    uint32_t to,
    HttpHead* head) {
    memset(head, 0, sizeof(HttpHead));
    char cmd[480];
    int n;
    if(ranged) {
        n = snprintf(
            cmd,
            sizeof(cmd),
            "[GET/BYTES]{\"url\":\"%s\",\"headers\":{\"Content-Type\":\"application/octet-stream\",\"Range\":\"bytes=%lu-%lu\"}}",
            url,
            (unsigned long)from,
            (unsigned long)to);
    } else {
        n = snprintf(
            cmd,
            sizeof(cmd),
            "[GET/BYTES]{\"url\":\"%s\",\"headers\":{\"Content-Type\":\"application/octet-stream\"}}",
            url);
    }
    if(n < 0 || n >= (int)sizeof(cmd)) {
        snprintf(head->error, sizeof(head->error), "URL too long");
        return false;
    }

    link_flush(app);
    app->rx_overflow = 0;
    link_send(app, cmd);

    char line[256];
    uint32_t start = furi_get_tick();
    while(furi_get_tick() - start < 45000 && !app->cancel) {
        if(!link_read_line(app, line, sizeof(line), 1000)) continue;
        if(strstr(line, "[GET/SUCCESS]")) {
            const char* sc = strstr(line, "\"Status-Code\":");
            const char* cl = strstr(line, "\"Content-Length\":");
            head->status = sc ? (uint32_t)atoi(sc + strlen("\"Status-Code\":")) : 0;
            head->length = cl ? (uint32_t)atoi(cl + strlen("\"Content-Length\":")) : 0;
            return true;
        }
        if(strstr(line, "ERROR") || strstr(line, "[GET/FAILED]") || strstr(line, "Failed")) {
            snprintf(head->error, sizeof(head->error), "%.60s", line);
            return false;
        }
    }
    snprintf(head->error, sizeof(head->error), app->cancel ? "Cancelled" : "No answer from devboard");
    return false;
}

// Body trailer: "[GET/END]" arrives after the payload
static void http_finish(OtaApp* app) {
    char line[64];
    for(int i = 0; i < 5; i++) {
        if(!link_read_line(app, line, sizeof(line), 1500)) return;
        if(strstr(line, "[GET/END]")) return;
    }
}

static bool http_get_text(OtaApp* app, const char* url, char* out, size_t cap, HttpHead* head) {
    if(!http_begin(app, url, false, 0, 0, head)) return false;
    if(head->status != 200) {
        snprintf(head->error, sizeof(head->error), "Server answered %lu", (unsigned long)head->status);
        return false;
    }
    if(head->length == 0 || head->length >= cap) {
        snprintf(head->error, sizeof(head->error), "Unexpected size (%lu)", (unsigned long)head->length);
        return false;
    }
    size_t got = 0;
    uint32_t last = furi_get_tick();
    while(got < head->length && !app->cancel) {
        size_t n = furi_stream_buffer_receive(
            app->rx, (uint8_t*)out + got, head->length - got, furi_ms_to_ticks(500));
        if(n > 0) {
            got += n;
            last = furi_get_tick();
        } else if(furi_get_tick() - last > IDLE_TIMEOUT_MS) {
            snprintf(head->error, sizeof(head->error), "Timed out reading answer");
            return false;
        }
    }
    out[got] = '\0';
    if(got != head->length) return false;
    http_finish(app);
    return true;
}

typedef struct {
    OtaApp* app;
    File* file;
    uint32_t base; // bytes already in the file before this transfer
    uint32_t total; // final file size
    uint32_t t_start;
    uint32_t t_bytes_start;
} Transfer;

static void transfer_progress(Transfer* t, uint32_t have) {
    char l1[44], l2[44];
    uint32_t elapsed = furi_get_tick() - t->t_start;
    uint32_t speed = elapsed ? (uint32_t)(((uint64_t)(have - t->t_bytes_start) * 1000) / elapsed) : 0;
    snprintf(
        l1,
        sizeof(l1),
        "%lu / %lu KB",
        (unsigned long)(have / 1024),
        (unsigned long)(t->total / 1024));
    if(speed > 0) {
        uint32_t eta = (t->total - have) / speed;
        snprintf(
            l2,
            sizeof(l2),
            "%lu KB/s  ~%lum %lus left",
            (unsigned long)(speed / 1024),
            (unsigned long)(eta / 60),
            (unsigned long)(eta % 60));
    } else {
        snprintf(l2, sizeof(l2), "starting...");
    }
    status_set(t->app, "Downloading", l1, l2, (int)(((uint64_t)have * 100) / t->total));
}

// Read exactly `length` body bytes to the file. Returns bytes written.
static uint32_t transfer_body(Transfer* t, uint32_t length, uint32_t file_pos) {
    OtaApp* app = t->app;
    uint32_t got = 0;
    uint32_t last_data = furi_get_tick();
    uint32_t last_ui = 0;
    while(got < length && !app->cancel) {
        uint32_t want = length - got;
        if(want > IO_BUFFER_SIZE) want = IO_BUFFER_SIZE;
        size_t n = furi_stream_buffer_receive(app->rx, app->io, want, furi_ms_to_ticks(500));
        if(n == 0) {
            if(furi_get_tick() - last_data > IDLE_TIMEOUT_MS) break;
            continue;
        }
        last_data = furi_get_tick();
        if(storage_file_write(t->file, app->io, n) != n) break;
        got += n;
        if(furi_get_tick() - last_ui > 700) {
            last_ui = furi_get_tick();
            transfer_progress(t, file_pos + got);
        }
    }
    return got;
}

typedef enum {
    DownloadOk,
    DownloadCancelled,
    DownloadFailed,
} DownloadResult;

// Resumable, chunked download of `total` bytes into OTA_PART.
static DownloadResult download_file(OtaApp* app, const Release* rel, const char* url) {
    Storage* storage = app->storage;
    storage_common_mkdir(storage, OTA_DIR);

    // Only resume a partial file if it belongs to this exact package
    bool same_package = false;
    {
        File* idf = storage_file_alloc(storage);
        char old_id[72] = {0};
        if(storage_file_open(idf, OTA_PART_ID, FSAM_READ, FSOM_OPEN_EXISTING)) {
            storage_file_read(idf, old_id, sizeof(old_id) - 1);
            same_package = strncmp(old_id, rel->sha256, 64) == 0;
        }
        storage_file_close(idf);
        storage_file_free(idf);
    }
    if(!same_package) {
        storage_common_remove(storage, OTA_PART);
        File* idf = storage_file_alloc(storage);
        if(storage_file_open(idf, OTA_PART_ID, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            storage_file_write(idf, rel->sha256, 64);
        }
        storage_file_close(idf);
        storage_file_free(idf);
    }

    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, OTA_PART, FSAM_READ_WRITE, FSOM_OPEN_ALWAYS)) {
        storage_file_free(file);
        furi_string_cat_str(app->text, "Cannot create the download file on the SD card.\n");
        return DownloadFailed;
    }
    uint32_t have = (uint32_t)storage_file_size(file);
    if(have > rel->size) {
        storage_file_seek(file, 0, true);
        storage_file_truncate(file);
        have = 0;
    }
    storage_file_seek(file, have, true);

    Transfer t = {
        .app = app,
        .file = file,
        .total = rel->size,
        .t_start = furi_get_tick(),
        .t_bytes_start = have,
    };

    DownloadResult result = DownloadOk;
    int failures = 0;
    while(have < rel->size) {
        if(app->cancel) {
            result = DownloadCancelled;
            break;
        }
        uint32_t chunk = rel->size - have;
        if(chunk > CHUNK_SIZE) chunk = CHUNK_SIZE;

        HttpHead head;
        bool ok = http_begin(app, url, true, have, have + chunk - 1, &head);
        uint32_t got = 0;
        bool whole_file_instead = false;

        if(ok) {
            if(head.status == 206 && head.length == chunk) {
                got = transfer_body(&t, chunk, have);
                ok = (got == chunk) && (app->rx_overflow == 0);
            } else if(head.status == 200 && head.length == rel->size) {
                // The server (or devboard) ignored the Range header: take the whole file
                // from the start in one go
                storage_file_seek(file, 0, true);
                storage_file_truncate(file);
                have = 0;
                t.t_bytes_start = 0;
                got = transfer_body(&t, rel->size, 0);
                ok = (got == rel->size) && (app->rx_overflow == 0);
                whole_file_instead = true;
            } else {
                snprintf(
                    head.error,
                    sizeof(head.error),
                    "Unexpected reply: status %lu, %lu bytes",
                    (unsigned long)head.status,
                    (unsigned long)head.length);
                ok = false;
            }
        }

        if(ok) {
            http_finish(app);
            have += got;
            failures = 0;
            (void)whole_file_instead;
            transfer_progress(&t, have);
        } else {
            if(app->cancel) {
                result = DownloadCancelled;
                break;
            }
            // Drop whatever partial data arrived and retry this chunk
            storage_file_seek(file, have, true);
            storage_file_truncate(file);
            failures++;
            FURI_LOG_W(TAG, "Chunk at %lu failed: %s", (unsigned long)have, head.error);
            if(failures >= CHUNK_RETRIES) {
                furi_string_cat_printf(
                    app->text,
                    "Download stopped at %lu of %lu KB.\n%s\nRun again to resume from here.\n",
                    (unsigned long)(have / 1024),
                    (unsigned long)(rel->size / 1024),
                    head.error[0] ? head.error : "Transfer interrupted or data lost");
                result = DownloadFailed;
                break;
            }
            status_set(app, "Retrying", "Connection hiccup...", "", -1);
            furi_delay_ms(1500);
        }
    }

    storage_file_sync(file);
    storage_file_close(file);
    storage_file_free(file);
    return result;
}

// ------------------------------------------------------------ verify / unpack

static bool verify_sha256(OtaApp* app, const char* path, const char* expected_hex) {
    File* file = storage_file_alloc(app->storage);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        return false;
    }
    uint32_t total = (uint32_t)storage_file_size(file);
    OtaSha256 sha;
    ota_sha256_init(&sha);
    uint32_t done = 0, last_ui = 0;
    while(!app->cancel) {
        size_t n = storage_file_read(file, app->io, IO_BUFFER_SIZE);
        if(n == 0) break;
        ota_sha256_update(&sha, app->io, n);
        done += n;
        if(furi_get_tick() - last_ui > 500) {
            last_ui = furi_get_tick();
            status_set(app, "Verifying", "Checking SHA-256...", "", (int)(((uint64_t)done * 100) / (total ? total : 1)));
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    if(app->cancel) return false;

    char hex[65];
    ota_sha256_final_hex(&sha, hex);
    return strncasecmp(hex, expected_hex, 64) == 0;
}

static bool find_update_manifest(OtaApp* app, const char* version, FuriString* manifest) {
    File* dir = storage_file_alloc(app->storage);
    bool found = false;
    if(storage_dir_open(dir, OTA_DIR)) {
        char name[128];
        FileInfo info;
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            if(!file_info_is_dir(&info)) continue;
            if(!strstr(name, version)) continue;
            furi_string_printf(manifest, "%s/%s/update.fuf", OTA_DIR, name);
            if(storage_file_exists(app->storage, furi_string_get_cstr(manifest))) {
                found = true;
                break;
            }
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
    return found;
}

// ------------------------------------------------------------ job helpers

static bool confirm(OtaApp* app, const char* header, const char* text, const char* yes) {
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, header, 64, 0, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 64, 32, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, "Cancel", NULL, yes);
    DialogMessageButton res = dialog_message_show(app->dialogs, message);
    dialog_message_free(message);
    return res == DialogMessageButtonRight;
}

static void load_update_url(char* url, size_t cap) {
    snprintf(url, cap, "%s", OTA_DEFAULT_URL);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, OTA_URL_OVERRIDE_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[200] = {0};
        size_t n = storage_file_read(file, buf, sizeof(buf) - 1);
        buf[n] = '\0';
        char* end = buf;
        while(*end && *end != '\r' && *end != '\n' && *end != ' ' && *end != '\t')
            end++;
        *end = '\0';
        if(strncmp(buf, "https://", 8) == 0) snprintf(url, cap, "%s", buf);
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static bool connect_devboard(OtaApp* app) {
    status_set(app, "WiFi Devboard", "Connecting...", "", -1);
    if(!link_open(app)) {
        furi_string_cat_str(
            app->text,
            "Cannot open the serial port. Another app may be using the expansion header (or the UART channel in Angularity > Protocols is wrong).\n");
        return false;
    }
    if(!link_ping(app)) {
        furi_string_cat_str(
            app->text,
            "No answer from the WiFi Devboard.\n\n- Seat it firmly on the GPIO header\n- It must run FlipperHTTP firmware (not Computer Mode)\n- WiFi must be set up once with the FlipperHTTP / WiFi apps\n");
        return false;
    }
    return true;
}

static bool fetch_release(OtaApp* app, Release* rel) {
    char url[200];
    load_update_url(url, sizeof(url));
    status_set(app, "Checking", "Reading update info...", "", -1);

    char* json = malloc(JSON_BUFFER_SIZE);
    HttpHead head;
    bool ok = http_get_text(app, url, json, JSON_BUFFER_SIZE, &head);
    if(!ok) {
        furi_string_cat_printf(
            app->text,
            "Could not read the update info.\n%s\n\nIs the Devboard connected to WiFi?\n",
            head.error[0] ? head.error : "No data");
        free(json);
        return false;
    }

    memset(rel, 0, sizeof(Release));
    char direct[200] = {0};
    bool good = ota_json_string(json, "version", rel->version, sizeof(rel->version)) &&
                ota_json_string(json, "sha256", rel->sha256, sizeof(rel->sha256)) &&
                ota_json_u32(json, "size", &rel->size);
    ota_json_string(json, "changelog", rel->changelog, sizeof(rel->changelog));
    // wifi_url is a direct (non-redirecting) link; url may redirect, which the devboard can't follow
    if(!ota_json_string(json, "wifi_url", direct, sizeof(direct))) {
        ota_json_string(json, "url", direct, sizeof(direct));
    }
    snprintf(rel->url, sizeof(rel->url), "%s", direct);
    free(json);

    if(!good || rel->url[0] == '\0' || strlen(rel->sha256) != 64 || rel->size < 100000) {
        furi_string_cat_str(
            app->text,
            "The update info is incomplete (needs version, size, sha256 and a download link).\n");
        return false;
    }
    return true;
}

static bool is_installed_version(const Release* rel) {
    const char* current = version_get_version(version_get());
    return current && (strstr(current, rel->version) || strstr(rel->version, current));
}

static void job_test(OtaApp* app) {
    if(connect_devboard(app)) {
        furi_string_cat_str(app->text, "WiFi Devboard answered.\n");
        Release rel;
        if(fetch_release(app, &rel)) {
            furi_string_cat_printf(
                app->text,
                "\nWiFi works.\nLatest: %s\nSize: %lu KB\n",
                rel.version,
                (unsigned long)(rel.size / 1024));
        }
    }
}

static void job_check(OtaApp* app) {
    if(!connect_devboard(app)) return;
    Release rel;
    if(!fetch_release(app, &rel)) return;
    furi_string_cat_printf(
        app->text,
        "Latest: %s\nSize: %lu MB\nInstalled: %s\n\n%s\n\n%s\n",
        rel.version,
        (unsigned long)(rel.size / (1024 * 1024)),
        version_get_version(version_get()),
        rel.changelog,
        is_installed_version(&rel) ? "You are up to date." : "A newer version is available.");
}

static void job_update(OtaApp* app) {
    if(!connect_devboard(app)) return;
    Release rel;
    if(!fetch_release(app, &rel)) return;

    bool same = is_installed_version(&rel);
    FuriString* ask = furi_string_alloc_printf(
        "%s\n%lu MB, ~%lu min\n%s",
        rel.version,
        (unsigned long)(rel.size / (1024 * 1024)),
        (unsigned long)(rel.size / 11000 / 60 + 1),
        same ? "Already installed" : "New version");
    bool go = confirm(app, same ? "Reinstall?" : "Download update?", furi_string_get_cstr(ask), same ? "Reinstall" : "Download");
    furi_string_free(ask);
    if(!go) {
        furi_string_cat_str(app->text, "Nothing was changed.\n");
        return;
    }

    if(!furi_hal_power_is_charging() && furi_hal_power_get_pct() < MIN_BATTERY_PCT) {
        furi_string_cat_printf(
            app->text,
            "Battery is %u%%. Charge to at least %d%% (or plug in USB) before updating.\n",
            furi_hal_power_get_pct(),
            MIN_BATTERY_PCT);
        return;
    }

    uint64_t total_space = 0, free_space = 0;
    storage_common_fs_info(app->storage, EXT_PATH(""), &total_space, &free_space);
    // package + unpacked copy + headroom
    if(free_space < (uint64_t)rel.size * 22 / 10 + (4 * 1024 * 1024)) {
        furi_string_cat_printf(
            app->text,
            "Not enough free SD space: need about %lu MB, have %lu MB.\n",
            (unsigned long)(((uint64_t)rel.size * 22 / 10) / (1024 * 1024) + 4),
            (unsigned long)(free_space / (1024 * 1024)));
        return;
    }

    DownloadResult dl = download_file(app, &rel, rel.url);
    if(dl == DownloadCancelled) {
        furi_string_cat_str(app->text, "Cancelled. Progress is kept: run again to resume.\n");
        return;
    }
    if(dl != DownloadOk) return;

    storage_common_remove(app->storage, OTA_TGZ);
    storage_common_rename(app->storage, OTA_PART, OTA_TGZ);
    storage_common_remove(app->storage, OTA_PART_ID);

    if(!verify_sha256(app, OTA_TGZ, rel.sha256)) {
        storage_common_remove(app->storage, OTA_TGZ);
        furi_string_cat_str(
            app->text,
            app->cancel ? "Cancelled.\n" :
                          "The downloaded file is corrupt (SHA-256 mismatch) and was deleted. Try again.\n");
        return;
    }

    status_set(app, "Unpacking", "Takes a few minutes...", "Do not remove the SD card", -1);
    bool unpacked = false;
    TarArchive* archive = tar_archive_alloc(app->storage);
    if(tar_archive_open(archive, OTA_TGZ, TarOpenModeReadGzip)) {
        unpacked = tar_archive_unpack_to(archive, OTA_DIR, NULL);
    }
    tar_archive_free(archive);
    if(!unpacked) {
        furi_string_cat_str(app->text, "Unpacking failed. The package is kept; check the SD card and retry.\n");
        return;
    }
    storage_common_remove(app->storage, OTA_TGZ);

    FuriString* manifest = furi_string_alloc();
    if(!find_update_manifest(app, rel.version, manifest)) {
        furi_string_cat_str(
            app->text,
            "Unpacked, but no update folder matching this version was found in /ext/update.\n");
        furi_string_free(manifest);
        return;
    }

    furi_string_cat_printf(
        app->text, "Update %s is ready in /ext/update.\n", rel.version);
    if(confirm(app, "Install now?", "The Flipper will restart\nand update itself.", "Install")) {
        Loader* loader = furi_record_open(RECORD_LOADER);
        loader_start_detached_with_gui_error(loader, "UpdaterApp", furi_string_get_cstr(manifest));
        furi_record_close(RECORD_LOADER);
        furi_string_free(manifest);
        view_dispatcher_send_custom_event(app->dispatcher, OtaEventExit);
        return;
    }
    furi_string_cat_str(
        app->text, "You can install it later from Archive: /ext/update > update.fuf > Run.\n");
    furi_string_free(manifest);
}

// ------------------------------------------------------------ plumbing

static int32_t worker_thread(void* context) {
    OtaApp* app = context;
    furi_string_reset(app->text);
    switch(app->job) {
    case JobUpdate:
        job_update(app);
        break;
    case JobCheck:
        job_check(app);
        break;
    case JobTest:
        job_test(app);
        break;
    }
    link_close(app);
    view_dispatcher_send_custom_event(app->dispatcher, OtaEventJobDone);
    return 0;
}

static void start_job(OtaApp* app, Job job) {
    app->job = job;
    app->cancel = false;
    status_set(app, "Working...", "", "", -1);
    view_dispatcher_switch_to_view(app->dispatcher, OtaViewStatus);
    app->worker = furi_thread_alloc_ex("OtaWorker", 4 * 1024, worker_thread, app);
    furi_thread_start(app->worker);
}

static void menu_callback(void* context, uint32_t index) {
    start_job(context, (Job)index);
}

static bool custom_event_callback(void* context, uint32_t event) {
    OtaApp* app = context;
    if(event == OtaEventJobDone) {
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        app->worker = NULL;
        text_box_set_text(app->result, furi_string_get_cstr(app->text));
        text_box_set_focus(app->result, TextBoxFocusStart);
        view_dispatcher_switch_to_view(app->dispatcher, OtaViewResult);
        return true;
    }
    if(event == OtaEventExit) {
        view_dispatcher_stop(app->dispatcher);
        return true;
    }
    return false;
}

static uint32_t result_previous(void* context) {
    UNUSED(context);
    return OtaViewMenu;
}

static bool navigation_callback(void* context) {
    UNUSED(context);
    return false;
}

int32_t ota_update_app(void* p) {
    UNUSED(p);
    OtaApp* app = malloc(sizeof(OtaApp));
    memset(app, 0, sizeof(OtaApp));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->gui = furi_record_open(RECORD_GUI);
    app->text = furi_string_alloc();
    app->io = malloc(IO_BUFFER_SIZE);

    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, navigation_callback);

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Check and install update", JobUpdate, menu_callback, app);
    submenu_add_item(app->menu, "Check for update only", JobCheck, menu_callback, app);
    submenu_add_item(app->menu, "Test WiFi Devboard", JobTest, menu_callback, app);
    view_dispatcher_add_view(app->dispatcher, OtaViewMenu, submenu_get_view(app->menu));

    app->status = view_alloc();
    view_allocate_model(app->status, ViewModelTypeLocking, sizeof(StatusModel));
    view_set_context(app->status, app);
    view_set_draw_callback(app->status, status_draw);
    view_set_input_callback(app->status, status_input);
    view_dispatcher_add_view(app->dispatcher, OtaViewStatus, app->status);

    app->result = text_box_alloc();
    text_box_set_font(app->result, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(app->result), result_previous);
    view_dispatcher_add_view(app->dispatcher, OtaViewResult, text_box_get_view(app->result));

    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->dispatcher, OtaViewMenu);
    view_dispatcher_run(app->dispatcher);

    if(app->worker) {
        app->cancel = true;
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
    }
    link_close(app);

    view_dispatcher_remove_view(app->dispatcher, OtaViewMenu);
    view_dispatcher_remove_view(app->dispatcher, OtaViewStatus);
    view_dispatcher_remove_view(app->dispatcher, OtaViewResult);
    submenu_free(app->menu);
    view_free(app->status);
    text_box_free(app->result);
    view_dispatcher_free(app->dispatcher);
    furi_string_free(app->text);
    free(app->io);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
