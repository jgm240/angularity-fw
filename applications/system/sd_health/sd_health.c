#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/elements.h>
#include <storage/storage.h>

#define TAG "SdHealth"

#define BLOCK_SIZE       4096
#define TEST_FILE        EXT_PATH(".sd_health_test.bin")
#define SLOW_BLOCK_MS    200
#define SCAN_MAX_DEPTH   12
#define SCAN_NAME_LENGTH 128

typedef enum {
    SdViewMenu,
    SdViewProgress,
    SdViewResult,
} SdView;

typedef enum {
    JobInfo,
    JobSpeed,
    JobVerify16,
    JobVerify64,
    JobFolderScan,
    JobCleanup,
} Job;

typedef enum {
    SdEventJobDone = 1,
} SdEvent;

typedef struct {
    char title[28];
    char detail[40];
    uint8_t percent;
} ProgressModel;

typedef struct {
    Gui* gui;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    View* progress;
    TextBox* result;
    FuriString* text;
    Storage* storage;
    FuriThread* worker;
    volatile bool cancel;
    Job job;
} SdApp;

// ------------------------------------------------------------ helpers

static void progress_set(SdApp* app, const char* title, uint8_t percent, const char* detail) {
    with_view_model(
        app->progress,
        ProgressModel * m,
        {
            if(title) snprintf(m->title, sizeof(m->title), "%s", title);
            snprintf(m->detail, sizeof(m->detail), "%s", detail ? detail : "");
            m->percent = percent;
        },
        true);
}

static void progress_draw(Canvas* canvas, void* model) {
    ProgressModel* m = model;
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 14, m->title);
    elements_progress_bar(canvas, 2, 24, 124, (float)m->percent / 100.0f);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 46, m->detail);
    canvas_draw_str(canvas, 2, 62, "Back = cancel");
}

static bool progress_input(InputEvent* event, void* context) {
    SdApp* app = context;
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        app->cancel = true;
    }
    return true; // swallow everything: only the worker leaves this view
}

// xorshift32 block generator: cheap, deterministic, not all-zero/all-one so
// stuck bits and address aliasing both show up as mismatches
static void fill_block(uint8_t* buf, uint32_t block_index) {
    uint32_t x = block_index * 2654435761u + 0x9E3779B9u;
    if(x == 0) x = 1;
    uint32_t* words = (uint32_t*)buf;
    for(size_t i = 0; i < BLOCK_SIZE / 4; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        words[i] = x;
    }
}

static const char* fs_type_name(SDFsType t) {
    switch(t) {
    case FST_FAT12:
        return "FAT12";
    case FST_FAT16:
        return "FAT16";
    case FST_FAT32:
        return "FAT32";
    case FST_EXFAT:
        return "exFAT";
    default:
        return "unknown";
    }
}

static bool sd_ready(SdApp* app) {
    if(storage_sd_status(app->storage) != FSE_OK) {
        furi_string_cat_str(app->text, "No SD card, or it cannot be mounted.\n");
        return false;
    }
    return true;
}

static bool has_free_space(SdApp* app, uint64_t need_bytes) {
    uint64_t total = 0, free_space = 0;
    if(storage_common_fs_info(app->storage, EXT_PATH(""), &total, &free_space) != FSE_OK) {
        furi_string_cat_str(app->text, "Could not read free space.\n");
        return false;
    }
    if(free_space < need_bytes + (1024 * 1024)) {
        furi_string_cat_printf(
            app->text,
            "Not enough free space: need %lu MB, have %lu MB.\n",
            (unsigned long)(need_bytes / (1024 * 1024)),
            (unsigned long)(free_space / (1024 * 1024)));
        return false;
    }
    return true;
}

// ------------------------------------------------------------ jobs

static void job_info(SdApp* app) {
    if(!sd_ready(app)) return;
    SDInfo info;
    if(storage_sd_info(app->storage, &info) != FSE_OK) {
        furi_string_cat_str(app->text, "Could not read card information.\n");
        return;
    }
    uint32_t used_kb = info.kb_total - info.kb_free;
    uint32_t used_pct = info.kb_total ? (uint32_t)(((uint64_t)used_kb * 100) / info.kb_total) : 0;

    furi_string_cat_printf(
        app->text,
        "Filesystem: %s\nLabel: %s\n\nSize: %lu MB\nUsed: %lu MB (%lu%%)\nFree: %lu MB\n",
        fs_type_name(info.fs_type),
        info.label[0] ? info.label : "-",
        (unsigned long)(info.kb_total / 1024),
        (unsigned long)(used_kb / 1024),
        (unsigned long)used_pct,
        (unsigned long)(info.kb_free / 1024));
    furi_string_cat_printf(
        app->text,
        "Cluster: %u B  Sector: %u B\n\nMaker ID: 0x%02X\nOEM: %.2s\nProduct: %.5s rev %u.%u\nSerial: %08lX\nMade: %02u/%u\n",
        info.cluster_size,
        info.sector_size,
        info.manufacturer_id,
        info.oem_id,
        info.product_name,
        info.product_revision_major,
        info.product_revision_minor,
        (unsigned long)info.product_serial_number,
        info.manufacturing_month,
        info.manufacturing_year);

    if(info.fs_type == FST_EXFAT) {
        furi_string_cat_str(
            app->text, "\nNote: exFAT is not officially supported by Flipper; FAT32 is safest.\n");
    }
    if(used_pct >= 90) {
        furi_string_cat_str(app->text, "\nWarning: card is almost full.\n");
    }
}

static void job_speed(SdApp* app) {
    const uint32_t blocks = (8 * 1024 * 1024) / BLOCK_SIZE;
    if(!sd_ready(app) || !has_free_space(app, (uint64_t)blocks * BLOCK_SIZE)) return;

    uint8_t* buf = malloc(BLOCK_SIZE);
    File* file = storage_file_alloc(app->storage);
    bool ok = true;
    uint32_t write_ms = 0, read_ms = 0;

    do {
        if(!storage_file_open(file, TEST_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            furi_string_cat_str(app->text, "Cannot create test file.\n");
            ok = false;
            break;
        }
        uint32_t t0 = furi_get_tick();
        for(uint32_t i = 0; i < blocks && !app->cancel; i++) {
            fill_block(buf, i);
            if(storage_file_write(file, buf, BLOCK_SIZE) != BLOCK_SIZE) {
                furi_string_cat_printf(app->text, "Write error at block %lu.\n", (unsigned long)i);
                ok = false;
                break;
            }
            if(i % 32 == 0) progress_set(app, "Write test", (i * 50) / blocks, "Writing 8 MB...");
        }
        storage_file_sync(file);
        write_ms = furi_get_tick() - t0;
        storage_file_close(file);
        if(!ok || app->cancel) break;

        if(!storage_file_open(file, TEST_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
            furi_string_cat_str(app->text, "Cannot reopen test file.\n");
            ok = false;
            break;
        }
        t0 = furi_get_tick();
        for(uint32_t i = 0; i < blocks && !app->cancel; i++) {
            if(storage_file_read(file, buf, BLOCK_SIZE) != BLOCK_SIZE) {
                furi_string_cat_printf(app->text, "Read error at block %lu.\n", (unsigned long)i);
                ok = false;
                break;
            }
            if(i % 32 == 0) {
                progress_set(app, "Read test", 50 + (i * 50) / blocks, "Reading 8 MB...");
            }
        }
        read_ms = furi_get_tick() - t0;
        storage_file_close(file);
    } while(false);

    storage_file_free(file);
    free(buf);
    storage_common_remove(app->storage, TEST_FILE);

    if(app->cancel) {
        furi_string_cat_str(app->text, "Cancelled.\n");
    } else if(ok && write_ms && read_ms) {
        uint32_t w = (8 * 1024 * 1000) / write_ms;
        uint32_t r = (8 * 1024 * 1000) / read_ms;
        furi_string_cat_printf(
            app->text,
            "Write: %lu KB/s\nRead:  %lu KB/s\n\n%s\n",
            (unsigned long)w,
            (unsigned long)r,
            (w < 200 || r < 400) ? "Slow card: consider replacing it (class 10 / A1 recommended)." :
                                    "Speed looks normal.");
    }
}

static void job_verify(SdApp* app, uint32_t mb) {
    const uint32_t blocks = (mb * 1024 * 1024) / BLOCK_SIZE;
    if(!sd_ready(app) || !has_free_space(app, (uint64_t)blocks * BLOCK_SIZE)) return;

    uint8_t* buf = malloc(BLOCK_SIZE);
    uint8_t* cmp = malloc(BLOCK_SIZE);
    File* file = storage_file_alloc(app->storage);
    uint32_t write_errors = 0, read_errors = 0, bad_blocks = 0, slow_blocks = 0, worst_ms = 0;
    uint32_t checked = 0;
    char detail[40];

    do {
        if(!storage_file_open(file, TEST_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            furi_string_cat_str(app->text, "Cannot create test file.\n");
            break;
        }
        for(uint32_t i = 0; i < blocks && !app->cancel; i++) {
            fill_block(buf, i);
            if(storage_file_write(file, buf, BLOCK_SIZE) != BLOCK_SIZE) {
                write_errors++;
                break;
            }
            if(i % 64 == 0) {
                snprintf(detail, sizeof(detail), "%lu / %lu MB", (unsigned long)(i * BLOCK_SIZE / 1048576), (unsigned long)mb);
                progress_set(app, "Writing pattern", (i * 50) / blocks, detail);
            }
        }
        storage_file_sync(file);
        storage_file_close(file);
        if(write_errors || app->cancel) break;

        if(!storage_file_open(file, TEST_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
            furi_string_cat_str(app->text, "Cannot reopen test file.\n");
            break;
        }
        for(uint32_t i = 0; i < blocks && !app->cancel; i++) {
            fill_block(cmp, i);
            uint32_t t0 = furi_get_tick();
            size_t got = storage_file_read(file, buf, BLOCK_SIZE);
            uint32_t dt = furi_get_tick() - t0;
            if(dt > worst_ms) worst_ms = dt;
            if(dt > SLOW_BLOCK_MS) slow_blocks++;
            if(got != BLOCK_SIZE) {
                read_errors++;
                break;
            }
            if(memcmp(buf, cmp, BLOCK_SIZE) != 0) bad_blocks++;
            checked++;
            if(i % 64 == 0) {
                snprintf(
                    detail,
                    sizeof(detail),
                    "%lu bad, %lu slow",
                    (unsigned long)bad_blocks,
                    (unsigned long)slow_blocks);
                progress_set(app, "Verifying", 50 + (i * 50) / blocks, detail);
            }
        }
        storage_file_close(file);
    } while(false);

    storage_file_free(file);
    free(buf);
    free(cmp);
    storage_common_remove(app->storage, TEST_FILE);

    furi_string_cat_printf(
        app->text,
        "Tested: %lu of %lu MB\nBad blocks: %lu\nWrite errors: %lu\nRead errors: %lu\nSlow blocks (>%d ms): %lu\nWorst block: %lu ms\n\n",
        (unsigned long)(checked * BLOCK_SIZE / 1048576),
        (unsigned long)mb,
        (unsigned long)bad_blocks,
        (unsigned long)write_errors,
        (unsigned long)read_errors,
        SLOW_BLOCK_MS,
        (unsigned long)slow_blocks,
        (unsigned long)worst_ms);

    if(app->cancel) {
        furi_string_cat_str(app->text, "Cancelled (partial result).\n");
    } else if(bad_blocks || write_errors || read_errors) {
        furi_string_cat_str(
            app->text,
            "FAIL: this card returned wrong or unreadable data. Back up now and replace it. "
            "Only a tested region was checked; more errors may exist.\n");
    } else if(slow_blocks > blocks / 50) {
        furi_string_cat_str(app->text, "WARNING: many slow blocks; the card may be wearing out.\n");
    } else {
        furi_string_cat_str(app->text, "PASS: no errors in the tested region.\n");
    }
    furi_string_cat_str(
        app->text, "\nThis tests free space only; it cannot see or repair damaged existing files.\n");
}

typedef struct {
    uint32_t files, dirs, zero_len, open_errors, too_deep, long_names;
    uint64_t bytes;
} ScanStats;

static void scan_dir(SdApp* app, const char* path, uint8_t depth, ScanStats* st) {
    if(app->cancel) return;
    if(depth > SCAN_MAX_DEPTH) {
        st->too_deep++;
        return;
    }
    File* dir = storage_file_alloc(app->storage);
    if(!storage_dir_open(dir, path)) {
        st->open_errors++;
        storage_file_free(dir);
        return;
    }
    char* name = malloc(SCAN_NAME_LENGTH);
    FileInfo info;
    while(!app->cancel && storage_dir_read(dir, &info, name, SCAN_NAME_LENGTH)) {
        if(file_info_is_dir(&info)) {
            st->dirs++;
            FuriString* child = furi_string_alloc_printf("%s/%s", path, name);
            scan_dir(app, furi_string_get_cstr(child), depth + 1, st);
            furi_string_free(child);
        } else {
            st->files++;
            st->bytes += info.size;
            if(info.size == 0) st->zero_len++;
            if(strlen(name) >= SCAN_NAME_LENGTH - 2) st->long_names++;
        }
        if(((st->files + st->dirs) % 50) == 0) {
            char detail[40];
            snprintf(
                detail,
                sizeof(detail),
                "%lu files, %lu dirs",
                (unsigned long)st->files,
                (unsigned long)st->dirs);
            progress_set(app, "Folder scan", 50, detail);
        }
    }
    free(name);
    storage_dir_close(dir);
    storage_file_free(dir);
}

static void job_folder_scan(SdApp* app) {
    if(!sd_ready(app)) return;
    ScanStats st = {0};
    scan_dir(app, STORAGE_EXT_PATH_PREFIX, 0, &st);

    furi_string_cat_printf(
        app->text,
        "Files: %lu\nFolders: %lu\nTotal size: %lu MB\nEmpty files: %lu\nUnreadable folders: %lu\nToo deep (>%d): %lu\nVery long names: %lu\n\n",
        (unsigned long)st.files,
        (unsigned long)st.dirs,
        (unsigned long)(st.bytes / (1024 * 1024)),
        (unsigned long)st.zero_len,
        (unsigned long)st.open_errors,
        SCAN_MAX_DEPTH,
        (unsigned long)st.too_deep,
        (unsigned long)st.long_names);
    if(app->cancel) {
        furi_string_cat_str(app->text, "Cancelled (partial result).\n");
    } else if(st.open_errors) {
        furi_string_cat_str(
            app->text,
            "WARNING: some folders could not be opened. This usually means filesystem corruption: "
            "back up from a computer, then repair there (chkdsk / fsck.fat) or reformat.\n");
    } else {
        furi_string_cat_str(app->text, "Directory structure is readable.\n");
    }
    furi_string_cat_str(
        app->text, "\nMetadata only: file contents were not read.\n");
}

static void job_cleanup(SdApp* app) {
    if(!sd_ready(app)) return;
    FS_Error err = storage_common_remove(app->storage, TEST_FILE);
    if(err == FSE_OK) {
        furi_string_cat_str(app->text, "Removed leftover test file.\n");
    } else if(err == FSE_NOT_EXIST) {
        furi_string_cat_str(app->text, "Nothing to clean: no leftover test file.\n");
    } else {
        furi_string_cat_printf(app->text, "Cleanup failed: %s\n", filesystem_api_error_get_desc(err));
    }
}

// ------------------------------------------------------------ plumbing

static int32_t worker_thread(void* context) {
    SdApp* app = context;
    furi_string_reset(app->text);
    switch(app->job) {
    case JobInfo:
        job_info(app);
        break;
    case JobSpeed:
        job_speed(app);
        break;
    case JobVerify16:
        job_verify(app, 16);
        break;
    case JobVerify64:
        job_verify(app, 64);
        break;
    case JobFolderScan:
        job_folder_scan(app);
        break;
    case JobCleanup:
        job_cleanup(app);
        break;
    }
    view_dispatcher_send_custom_event(app->dispatcher, SdEventJobDone);
    return 0;
}

static void start_job(SdApp* app, Job job) {
    app->job = job;
    app->cancel = false;
    progress_set(app, "Working...", 0, "");
    view_dispatcher_switch_to_view(app->dispatcher, SdViewProgress);
    app->worker = furi_thread_alloc_ex("SdHealthWorker", 3 * 1024, worker_thread, app);
    furi_thread_start(app->worker);
}

static void menu_callback(void* context, uint32_t index) {
    start_job(context, (Job)index);
}

static bool custom_event_callback(void* context, uint32_t event) {
    SdApp* app = context;
    if(event == SdEventJobDone) {
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        app->worker = NULL;
        text_box_set_text(app->result, furi_string_get_cstr(app->text));
        text_box_set_focus(app->result, TextBoxFocusStart);
        view_dispatcher_switch_to_view(app->dispatcher, SdViewResult);
        return true;
    }
    return false;
}

static uint32_t result_previous(void* context) {
    UNUSED(context);
    return SdViewMenu;
}

static bool navigation_callback(void* context) {
    UNUSED(context);
    return false; // Back on the menu exits the app
}

int32_t sd_health_app(void* p) {
    UNUSED(p);
    SdApp* app = malloc(sizeof(SdApp));
    memset(app, 0, sizeof(SdApp));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->gui = furi_record_open(RECORD_GUI);
    app->text = furi_string_alloc();

    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, navigation_callback);

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Card info", JobInfo, menu_callback, app);
    submenu_add_item(app->menu, "Speed test (8 MB)", JobSpeed, menu_callback, app);
    submenu_add_item(app->menu, "Verify surface 16 MB", JobVerify16, menu_callback, app);
    submenu_add_item(app->menu, "Verify surface 64 MB", JobVerify64, menu_callback, app);
    submenu_add_item(app->menu, "Folder integrity scan", JobFolderScan, menu_callback, app);
    submenu_add_item(app->menu, "Remove test leftovers", JobCleanup, menu_callback, app);
    view_dispatcher_add_view(app->dispatcher, SdViewMenu, submenu_get_view(app->menu));

    app->progress = view_alloc();
    view_allocate_model(app->progress, ViewModelTypeLocking, sizeof(ProgressModel));
    view_set_context(app->progress, app);
    view_set_draw_callback(app->progress, progress_draw);
    view_set_input_callback(app->progress, progress_input);
    view_dispatcher_add_view(app->dispatcher, SdViewProgress, app->progress);

    app->result = text_box_alloc();
    text_box_set_font(app->result, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(app->result), result_previous);
    view_dispatcher_add_view(app->dispatcher, SdViewResult, text_box_get_view(app->result));

    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->dispatcher, SdViewMenu);
    view_dispatcher_run(app->dispatcher);

    if(app->worker) {
        app->cancel = true;
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
    }
    view_dispatcher_remove_view(app->dispatcher, SdViewMenu);
    view_dispatcher_remove_view(app->dispatcher, SdViewProgress);
    view_dispatcher_remove_view(app->dispatcher, SdViewResult);
    submenu_free(app->menu);
    view_free(app->progress);
    text_box_free(app->result);
    view_dispatcher_free(app->dispatcher);
    furi_string_free(app->text);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
