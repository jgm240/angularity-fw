#pragma once
#include "notification.h"
#include "notification_messages_notes.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********************************** Messages **********************************/

// Display
extern const NotificationMessage message_display_backlight_on;
extern const NotificationMessage message_display_backlight_off;
extern const NotificationMessage message_display_backlight_enforce_on;
extern const NotificationMessage message_display_backlight_enforce_auto;

// Led ON
extern const NotificationMessage message_red_255;
extern const NotificationMessage message_green_255;
extern const NotificationMessage message_blue_255;

// Led OFF
extern const NotificationMessage message_red_0;
extern const NotificationMessage message_green_0;
extern const NotificationMessage message_blue_0;

// Led hardware blink control
extern const NotificationMessage message_blink_start_10;
extern const NotificationMessage message_blink_start_100;
extern const NotificationMessage message_blink_stop;

extern const NotificationMessage message_blink_set_color_red;
extern const NotificationMessage message_blink_set_color_green;
extern const NotificationMessage message_blink_set_color_blue;
extern const NotificationMessage message_blink_set_color_cyan;
extern const NotificationMessage message_blink_set_color_magenta;
extern const NotificationMessage message_blink_set_color_yellow;
extern const NotificationMessage message_blink_set_color_white;

// Delay
extern const NotificationMessage message_delay_1;
extern const NotificationMessage message_delay_10;
extern const NotificationMessage message_delay_25;
extern const NotificationMessage message_delay_50;
extern const NotificationMessage message_delay_100;
extern const NotificationMessage message_delay_250;
extern const NotificationMessage message_delay_500;
extern const NotificationMessage message_delay_1000;

// Sound
extern const NotificationMessage message_sound_off;

// Vibro
extern const NotificationMessage message_vibro_on;
extern const NotificationMessage message_vibro_off;

// Reset
extern const NotificationMessage message_do_not_reset;

// Override user settings
extern const NotificationMessage message_force_speaker_volume_setting_1f;
extern const NotificationMessage message_force_vibro_setting_on;
extern const NotificationMessage message_force_vibro_setting_off;
extern const NotificationMessage message_force_display_brightness_setting_1f;

// LCD Messages
extern const NotificationMessage message_lcd_contrast_update;

/****************************** Message sequences ******************************/

// Reset
extern const NotificationSequence sequence_reset_red;
extern const NotificationSequence sequence_reset_green;
extern const NotificationSequence sequence_reset_blue;
extern const NotificationSequence sequence_reset_rgb;
extern const NotificationSequence sequence_reset_display;
extern const NotificationSequence sequence_reset_sound;
extern const NotificationSequence sequence_reset_vibro;

// Vibro
extern const NotificationSequence sequence_set_vibro_on;

// Display
/** Display: backlight wakeup */
extern const NotificationSequence sequence_display_backlight_on;
/** Display: backlight force off */
extern const NotificationSequence sequence_display_backlight_off;
/** Display: backlight force off after a delay of 1000ms */
extern const NotificationSequence sequence_display_backlight_off_delay_1000;

/** Display: backlight always on lock */
extern const NotificationSequence sequence_display_backlight_enforce_on;
/** Display: backlight always on unlock */
extern const NotificationSequence sequence_display_backlight_enforce_auto;

// Charging
extern const NotificationSequence sequence_charging;
extern const NotificationSequence sequence_charged;
extern const NotificationSequence sequence_not_charging;
extern const NotificationSequence sequence_charging_beep;
extern const NotificationSequence sequence_not_charging_beep;
extern const NotificationSequence sequence_theft_alarm_burst;
extern const NotificationSequence sequence_theft_alarm_led;
extern const NotificationSequence sequence_shortcut_beep;
extern const NotificationSequence sequence_shortcut_blink_led;
extern const NotificationSequence sequence_find_my_flipper;

// Sound packs: Lock/Unlock chimes
extern const NotificationSequence sequence_sound_pack_classic_unlock;
extern const NotificationSequence sequence_sound_pack_classic_lock;
extern const NotificationSequence sequence_sound_pack_chime_unlock;
extern const NotificationSequence sequence_sound_pack_chime_lock;
extern const NotificationSequence sequence_sound_pack_retro_unlock;
extern const NotificationSequence sequence_sound_pack_retro_lock;
extern const NotificationSequence sequence_sound_pack_classic_error;
extern const NotificationSequence sequence_sound_pack_chime_error;
extern const NotificationSequence sequence_sound_pack_retro_error;
extern const NotificationSequence sequence_sound_pack_minimal_unlock;
extern const NotificationSequence sequence_sound_pack_minimal_lock;
extern const NotificationSequence sequence_sound_pack_minimal_error;
extern const NotificationSequence sequence_sound_pack_arcade_unlock;
extern const NotificationSequence sequence_sound_pack_arcade_lock;
extern const NotificationSequence sequence_sound_pack_arcade_error;

// Sound packs: UI click
extern const NotificationSequence sequence_sound_pack_classic_click;
extern const NotificationSequence sequence_sound_pack_chime_click;
extern const NotificationSequence sequence_sound_pack_retro_click;
extern const NotificationSequence sequence_sound_pack_minimal_click;
extern const NotificationSequence sequence_sound_pack_arcade_click;

// Sound packs: Settings app open cue
extern const NotificationSequence sequence_sound_pack_classic_app_open;
extern const NotificationSequence sequence_sound_pack_chime_app_open;
extern const NotificationSequence sequence_sound_pack_retro_app_open;
extern const NotificationSequence sequence_sound_pack_minimal_app_open;
extern const NotificationSequence sequence_sound_pack_arcade_app_open;

// Sound packs: power on/off
extern const NotificationSequence sequence_sound_pack_classic_boot;
extern const NotificationSequence sequence_sound_pack_chime_boot;
extern const NotificationSequence sequence_sound_pack_retro_boot;
extern const NotificationSequence sequence_sound_pack_minimal_boot;
extern const NotificationSequence sequence_sound_pack_arcade_boot;
extern const NotificationSequence sequence_sound_pack_classic_shutdown;
extern const NotificationSequence sequence_sound_pack_chime_shutdown;
extern const NotificationSequence sequence_sound_pack_retro_shutdown;
extern const NotificationSequence sequence_sound_pack_minimal_shutdown;
extern const NotificationSequence sequence_sound_pack_arcade_shutdown;

// Sound packs: Sub-GHz app open cue
extern const NotificationSequence sequence_sound_pack_classic_app_subghz;
extern const NotificationSequence sequence_sound_pack_chime_app_subghz;
extern const NotificationSequence sequence_sound_pack_retro_app_subghz;
extern const NotificationSequence sequence_sound_pack_minimal_app_subghz;
extern const NotificationSequence sequence_sound_pack_arcade_app_subghz;

// Sound packs: Infrared app open cue
extern const NotificationSequence sequence_sound_pack_classic_app_infrared;
extern const NotificationSequence sequence_sound_pack_chime_app_infrared;
extern const NotificationSequence sequence_sound_pack_retro_app_infrared;
extern const NotificationSequence sequence_sound_pack_minimal_app_infrared;
extern const NotificationSequence sequence_sound_pack_arcade_app_infrared;

// Sound packs: iButton app open cue
extern const NotificationSequence sequence_sound_pack_classic_app_ibutton;
extern const NotificationSequence sequence_sound_pack_chime_app_ibutton;
extern const NotificationSequence sequence_sound_pack_retro_app_ibutton;
extern const NotificationSequence sequence_sound_pack_minimal_app_ibutton;
extern const NotificationSequence sequence_sound_pack_arcade_app_ibutton;

// Sound packs: U2F app open cue
extern const NotificationSequence sequence_sound_pack_classic_app_u2f;
extern const NotificationSequence sequence_sound_pack_chime_app_u2f;
extern const NotificationSequence sequence_sound_pack_retro_app_u2f;
extern const NotificationSequence sequence_sound_pack_minimal_app_u2f;
extern const NotificationSequence sequence_sound_pack_arcade_app_u2f;

// Sound packs: Bad KB app open cue
extern const NotificationSequence sequence_sound_pack_classic_app_badusb;
extern const NotificationSequence sequence_sound_pack_chime_app_badusb;
extern const NotificationSequence sequence_sound_pack_retro_app_badusb;
extern const NotificationSequence sequence_sound_pack_minimal_app_badusb;
extern const NotificationSequence sequence_sound_pack_arcade_app_badusb;

// Sound packs: GPIO app open cue
extern const NotificationSequence sequence_sound_pack_classic_app_gpio;
extern const NotificationSequence sequence_sound_pack_chime_app_gpio;
extern const NotificationSequence sequence_sound_pack_retro_app_gpio;
extern const NotificationSequence sequence_sound_pack_minimal_app_gpio;
extern const NotificationSequence sequence_sound_pack_arcade_app_gpio;

// Sound packs: Passport app open cue
extern const NotificationSequence sequence_sound_pack_classic_app_passport;
extern const NotificationSequence sequence_sound_pack_chime_app_passport;
extern const NotificationSequence sequence_sound_pack_retro_app_passport;
extern const NotificationSequence sequence_sound_pack_minimal_app_passport;
extern const NotificationSequence sequence_sound_pack_arcade_app_passport;

// Sound packs: Clock app open cue
extern const NotificationSequence sequence_sound_pack_classic_app_clock;
extern const NotificationSequence sequence_sound_pack_chime_app_clock;
extern const NotificationSequence sequence_sound_pack_retro_app_clock;
extern const NotificationSequence sequence_sound_pack_minimal_app_clock;
extern const NotificationSequence sequence_sound_pack_arcade_app_clock;

// Light up
extern const NotificationSequence sequence_set_only_red_255;
extern const NotificationSequence sequence_set_only_green_255;
extern const NotificationSequence sequence_set_only_blue_255;
extern const NotificationSequence sequence_set_red_255;
extern const NotificationSequence sequence_set_green_255;
extern const NotificationSequence sequence_set_blue_255;

// Solid colors
extern const NotificationSequence sequence_solid_yellow;

// Blink
extern const NotificationSequence sequence_blink_blue_10;
extern const NotificationSequence sequence_blink_red_10;
extern const NotificationSequence sequence_blink_green_10;
extern const NotificationSequence sequence_blink_yellow_10;
extern const NotificationSequence sequence_blink_cyan_10;
extern const NotificationSequence sequence_blink_magenta_10;

extern const NotificationSequence sequence_blink_red_100;
extern const NotificationSequence sequence_blink_green_100;
extern const NotificationSequence sequence_blink_blue_100;
extern const NotificationSequence sequence_blink_yellow_100;
extern const NotificationSequence sequence_blink_cyan_100;
extern const NotificationSequence sequence_blink_magenta_100;
extern const NotificationSequence sequence_blink_white_100;

// Hardware blink
extern const NotificationSequence sequence_blink_start_blue;
extern const NotificationSequence sequence_blink_start_red;
extern const NotificationSequence sequence_blink_start_green;
extern const NotificationSequence sequence_blink_start_yellow;
extern const NotificationSequence sequence_blink_start_cyan;
extern const NotificationSequence sequence_blink_start_magenta;
extern const NotificationSequence sequence_blink_stop;

// General
extern const NotificationSequence sequence_single_vibro;
extern const NotificationSequence sequence_double_vibro;
extern const NotificationSequence sequence_success;
extern const NotificationSequence sequence_semi_success;
extern const NotificationSequence sequence_error;
extern const NotificationSequence sequence_audiovisual_alert;

// LCD
extern const NotificationSequence sequence_lcd_contrast_update;

// Wait for notification queue become empty
extern const NotificationSequence sequence_empty;

// Once-a-year device birthday surprise
extern const NotificationSequence sequence_happy_birthday;

#ifdef __cplusplus
}
#endif
