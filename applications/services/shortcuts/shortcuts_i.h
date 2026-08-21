#pragma once

#include "shortcuts.h"
#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <notification/notification.h>

#define SHORTCUTS_FILE_PATH INT_PATH(".shortcuts.txt")
#define SHORTCUTS_GPIO_POLL_MS 1000

struct Shortcuts {
    FuriMutex* mutex;

    ShortcutRule rules[SHORTCUTS_MAX];
    // Edge-detect state for GpioPull rules, indexed the same as rules[]
    bool gpio_rule_broken[SHORTCUTS_MAX];
    size_t count;

    // Minute-of-day (0-1439) last checked for ScheduledTime rules, so each
    // minute only fires once even though the poll timer ticks every second
    uint16_t last_scheduled_minute;

    FuriPubSubSubscription* storage_subscription;
    FuriPubSubSubscription* power_subscription;
    FuriTimer* gpio_poll_timer;
};

void shortcuts_load(Shortcuts* instance);
void shortcuts_save(Shortcuts* instance);
void shortcuts_execute_action(const ShortcutRule* rule);
