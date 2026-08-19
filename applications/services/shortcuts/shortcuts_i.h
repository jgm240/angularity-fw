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

    FuriPubSubSubscription* storage_subscription;
    FuriPubSubSubscription* power_subscription;
    FuriTimer* gpio_poll_timer;
};

void shortcuts_load(Shortcuts* instance);
void shortcuts_save(Shortcuts* instance);
void shortcuts_execute_action(ShortcutAction action);
