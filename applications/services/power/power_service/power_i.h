#pragma once

#include "power.h"

#include <gui/gui.h>
#include <gui/view_holder.h>

#include <toolbox/api_lock.h>
#include <assets_icons.h>

#include "views/power_off.h"
#include "views/power_unplug_usb.h"
#include "views/power_theft_alarm.h"

#include <power/power_service/power_settings.h>

typedef enum {
    PowerStateNotCharging,
    PowerStateCharging,
    PowerStateCharged,
} PowerState;

struct Power {
    ViewHolder* view_holder;
    FuriPubSub* event_pubsub;
    FuriEventLoop* event_loop;
    FuriMessageQueue* message_queue;

    ViewPort* battery_view_port;
    PowerOff* view_power_off;
    PowerUnplugUsb* view_power_unplug_usb;
    PowerTheftAlarm* view_power_theft_alarm;

    PowerEvent event;
    PowerState state;
    PowerInfo info;

    bool battery_low;
    bool show_battery_low_warning;
    bool is_otg_requested;
    uint8_t battery_level;
    uint8_t power_off_timeout;

    PowerSettings settings;
    FuriTimer* auto_poweroff_timer;
    bool app_running;
    bool charge_is_supressed;
    bool theft_alarm_active;
    // Pseudo-Off: screen/LEDs/radios dark, still charging, OK-hold wakes
    bool pseudo_off;
    bool pseudo_off_bt_was_enabled;
    bool pseudo_off_otg_was_requested;
    FuriPubSubSubscription* pseudo_off_input_subscription;
    // Set once the alarm's forced lock has actually been observed, so a
    // later unlock can be told apart from "was never locked"
    bool theft_alarm_saw_lock;
    // GPIO Pull/Tether triggers only fire on a healthy -> broken transition,
    // so this must have been seen healthy first
    bool gpio_theft_seen_ok;
    FuriPubSub* input_events_pubsub;
    FuriPubSub* ascii_events_pubsub;
    FuriPubSubSubscription* input_events_subscription;
    FuriPubSubSubscription* ascii_events_subscription;
};

typedef enum {
    PowerViewOff,
    PowerViewUnplugUsb,
} PowerView;

typedef enum {
    PowerMessageTypeShutdown,
    PowerMessageTypeReboot,
    PowerMessageTypeGetInfo,
    PowerMessageTypeIsBatteryHealthy,
    PowerMessageTypeShowBatteryLowWarning,
    PowerMessageTypeSwitchOTG,
    PowerMessageTypePseudoOff,
    PowerMessageTypePseudoWake,

    PowerMessageTypeGetSettings,
    PowerMessageTypeSetSettings,
    PowerMessageTypeReloadSettings,
} PowerMessageType;

typedef struct {
    PowerMessageType type;
    union {
        PowerBootMode boot_mode;
        PowerInfo* power_info;
        bool* bool_param;

        PowerSettings* settings;
        const PowerSettings* csettings;
    };
    FuriApiLock lock;
} PowerMessage;
