#include "profiles.h"
#include "settings.h"
#include "security_log.h"

#include <furi_hal.h>
#include <storage/storage.h>
#include <toolbox/saved_struct.h>
#include <notification/notification_app.h>
#include <bt/bt_service/bt_i.h>

#define PROFILES_MAGIC   0x5F
#define PROFILES_VERSION 1

typedef struct {
    bool valid;
    bool vibro_on;
    bool bt_enabled;
    bool dark_mode;
    bool theft_alarm;
    float display_brightness;
    float led_brightness;
    float speaker_volume;
    uint32_t display_off_delay_ms;
} Profile;

typedef struct {
    Profile slots[PROFILES_COUNT];
    uint8_t active;
} ProfilesFile;

static const char* const profile_names[PROFILES_COUNT] = {"Home", "Work", "Travel"};

static void profiles_load(ProfilesFile* file) {
    memset(file, 0, sizeof(ProfilesFile));
    file->active = PROFILE_NONE;
    if(!saved_struct_load(
           PROFILES_PATH, file, sizeof(ProfilesFile), PROFILES_MAGIC, PROFILES_VERSION)) {
        memset(file, 0, sizeof(ProfilesFile));
        file->active = PROFILE_NONE;
    }
    if(file->active >= PROFILES_COUNT) {
        file->active = PROFILE_NONE;
    }
}

static void profiles_store(const ProfilesFile* file) {
    saved_struct_save(
        PROFILES_PATH, file, sizeof(ProfilesFile), PROFILES_MAGIC, PROFILES_VERSION);
}

const char* profiles_name(uint8_t slot) {
    return slot < PROFILES_COUNT ? profile_names[slot] : "None";
}

bool profiles_is_saved(uint8_t slot) {
    if(slot >= PROFILES_COUNT) return false;
    ProfilesFile file;
    profiles_load(&file);
    return file.slots[slot].valid;
}

uint8_t profiles_get_active(void) {
    ProfilesFile file;
    profiles_load(&file);
    return file.active;
}

void profiles_save_current(uint8_t slot) {
    if(slot >= PROFILES_COUNT) return;

    ProfilesFile file;
    profiles_load(&file);

    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    Bt* bt = furi_record_open(RECORD_BT);

    Profile* p = &file.slots[slot];
    p->valid = true;
    p->display_brightness = notification->settings.display_brightness;
    p->led_brightness = notification->settings.led_brightness;
    p->speaker_volume = notification->settings.speaker_volume;
    p->display_off_delay_ms = notification->settings.display_off_delay_ms;
    p->vibro_on = notification->settings.vibro_on;
    p->bt_enabled = bt->bt_settings.enabled;
    p->dark_mode = momentum_settings.dark_mode;
    p->theft_alarm = momentum_settings.theft_alarm;
    file.active = slot;

    furi_record_close(RECORD_BT);
    furi_record_close(RECORD_NOTIFICATION);

    profiles_store(&file);

    FuriString* msg = furi_string_alloc_printf("Profile saved: %s", profiles_name(slot));
    security_log_add(furi_string_get_cstr(msg));
    furi_string_free(msg);
}

bool profiles_apply(uint8_t slot) {
    if(slot >= PROFILES_COUNT) return false;

    ProfilesFile file;
    profiles_load(&file);
    const Profile* p = &file.slots[slot];
    if(!p->valid) return false;

    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification->settings.display_brightness = p->display_brightness;
    notification->settings.led_brightness = p->led_brightness;
    notification->settings.speaker_volume = p->speaker_volume;
    notification->settings.display_off_delay_ms = p->display_off_delay_ms;
    notification->settings.vibro_on = p->vibro_on;
    notification_message_save_settings(notification);
    notification_message(notification, &sequence_display_backlight_on);
    furi_record_close(RECORD_NOTIFICATION);

    Bt* bt = furi_record_open(RECORD_BT);
    if(bt->bt_settings.enabled != p->bt_enabled) {
        bt->bt_settings.enabled = p->bt_enabled;
        if(p->bt_enabled) {
            furi_hal_bt_start_advertising();
        } else {
            furi_hal_bt_stop_advertising();
        }
        bt_settings_save(&bt->bt_settings);
    }
    furi_record_close(RECORD_BT);

    momentum_settings.dark_mode = p->dark_mode;
    momentum_settings.theft_alarm = p->theft_alarm;
    momentum_settings_save();

    file.active = slot;
    profiles_store(&file);

    FuriString* msg = furi_string_alloc_printf("Profile applied: %s", profiles_name(slot));
    security_log_add(furi_string_get_cstr(msg));
    furi_string_free(msg);
    return true;
}

uint8_t profiles_cycle(void) {
    ProfilesFile file;
    profiles_load(&file);

    uint8_t start = file.active < PROFILES_COUNT ? file.active : PROFILES_COUNT - 1;
    for(uint8_t i = 1; i <= PROFILES_COUNT; i++) {
        uint8_t slot = (start + i) % PROFILES_COUNT;
        if(file.slots[slot].valid) {
            return profiles_apply(slot) ? slot : PROFILE_NONE;
        }
    }
    return PROFILE_NONE;
}
