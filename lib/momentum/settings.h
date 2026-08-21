#pragma once

#include <furi_hal_serial_types.h>
#include <furi_hal_version.h>
#include <stdint.h>
#include <toolbox/colors.h>

#define MOMENTUM_SETTINGS_PATH INT_PATH(".momentum_settings.txt")

#define ASSET_PACKS_NAME_LEN 32

// Uppercase hex-encoded UID, up to a 10-byte UID (20 hex chars) + null
#define NFC_UNLOCK_UID_LEN 24

typedef enum {
    BatteryIconOff,
    BatteryIconBar,
    BatteryIconPercent,
    BatteryIconInvertedPercent,
    BatteryIconRetro3,
    BatteryIconRetro5,
    BatteryIconBarPercent,
    BatteryIconCount,
} BatteryIcon;

typedef enum {
    TheftAlarmTriggerUnplug,
    TheftAlarmTriggerGpioPull,
    TheftAlarmTriggerGpioTether,
    TheftAlarmTriggerCount,
} TheftAlarmTrigger;

typedef enum {
    GpioMirrorOff,
    GpioMirrorCharging,
    GpioMirrorLocked,
    GpioMirrorCount,
} GpioMirrorMode;

typedef enum {
    MenuStyleList,
    MenuStyleWii,
    MenuStyleDsi,
    MenuStylePs4,
    MenuStyleVertical,
    MenuStyleC64,
    MenuStyleCompact,
    MenuStyleMNTM,
    MenuStyleCoverFlow,
    MenuStyleCount,
} MenuStyle;

typedef enum {
    SoundPackOff,
    SoundPackClassic,
    SoundPackChime,
    SoundPackRetro,
    SoundPackCount,
} SoundPack;

typedef enum {
    SpiDefault, // CS on pa4
    SpiExtra, // CS on pc3
    SpiCount,
} SpiHandle;

typedef enum {
    ScreenColorModeDefault,
    ScreenColorModeCustom,
    ScreenColorModeRainbow,
    ScreenColorModeRgbBacklight,
    ScreenColorModeCount,
} ScreenColorMode;

typedef union __attribute__((packed)) {
    struct {
        ScreenColorMode mode;
        RgbColor rgb;
    };
    uint32_t value;
} ScreenFrameColor;

typedef enum {
    BrowserPathOff,
    BrowserPathCurrent,
    BrowserPathBrief,
    BrowserPathFull,
    BrowserPathModeCount,
} BrowserPathMode;

typedef struct {
    char asset_pack[ASSET_PACKS_NAME_LEN];
    uint32_t anim_speed;
    int32_t cycle_anims;
    bool unlock_anims;
    MenuStyle menu_style;
    bool lock_on_boot;
    bool bad_pins_format;
    bool allow_locked_rpc_usb;
    bool allow_locked_rpc_ble;
    bool lockscreen_poweroff;
    bool lockscreen_time;
    bool lockscreen_seconds;
    bool lockscreen_date;
    bool lockscreen_statusbar;
    bool lockscreen_prompt;
    bool lockscreen_transparent;
    bool lockscreen_skip_animation;
    bool nfc_unlock_enabled;
    char nfc_unlock_uid[NFC_UNLOCK_UID_LEN];
    GpioMirrorMode gpio_mirror_mode;
    uint32_t gpio_mirror_pin;
    BatteryIcon battery_icon;
    bool status_icons;
    bool bar_borders;
    bool bar_background;
    bool sort_dirs_first;
    bool show_hidden_files;
    bool show_internal_tab;
    BrowserPathMode browser_path_mode;
    uint32_t favorite_timeout;
    bool scroll_marquee;
    bool dark_mode;
    bool rgb_backlight;
    bool charging_sound;
    SoundPack sound_pack;
    bool theft_alarm;
    TheftAlarmTrigger theft_alarm_trigger;
    uint32_t theft_alarm_gpio_pin1;
    uint32_t theft_alarm_gpio_pin2;
    bool theft_alarm_result_sound;
    bool theft_alarm_result_led;
    bool theft_alarm_result_lock;
    uint32_t butthurt_timer;
    bool midnight_format_00;
    bool popup_overlay;
    SpiHandle spi_cc1101_handle;
    SpiHandle spi_nrf24_handle;
    FuriHalSerialId uart_esp_channel;
    FuriHalSerialId uart_nmea_channel;
    bool file_naming_prefix_after;
    FuriHalVersionColor spoof_color;
    ScreenFrameColor rpc_color_fg;
    ScreenFrameColor rpc_color_bg;
} MomentumSettings;

void momentum_settings_save(void);
extern MomentumSettings momentum_settings;
