#include "shortcuts_i.h"
#include <toolbox/stream/stream.h>
#include <toolbox/stream/file_stream.h>
#include <notification/notification_messages.h>
#include <power/power_service/power.h>
#include <desktop/desktop.h>

#define TAG "Shortcuts"

void shortcuts_execute_action(ShortcutAction action) {
    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);

    switch(action) {
    case ShortcutActionTriggerAlarm:
        notification_message(notification, &sequence_theft_alarm_burst);
        notification_message(notification, &sequence_theft_alarm_led);
        break;
    case ShortcutActionBlinkLed:
        notification_message(notification, &sequence_shortcut_blink_led);
        break;
    case ShortcutActionMakeSound:
        notification_message(notification, &sequence_shortcut_beep);
        break;
    default:
        break;
    }

    furi_record_close(RECORD_NOTIFICATION);

    if(action == ShortcutActionTriggerAlarm) {
        Desktop* desktop = furi_record_open(RECORD_DESKTOP);
        desktop_api_lock(desktop);
        furi_record_close(RECORD_DESKTOP);
    }
}

void shortcuts_load(Shortcuts* instance) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    instance->count = 0;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    FuriString* line = furi_string_alloc();

    if(file_stream_open(stream, SHORTCUTS_FILE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint32_t version;
        if(stream_read_line(stream, line) &&
           sscanf(furi_string_get_cstr(line), "Shortcuts Version %lu", &version) == 1 &&
           version == 1) {
            while(instance->count < SHORTCUTS_MAX && stream_read_line(stream, line)) {
                unsigned trigger, pin1, pin2, action;
                if(sscanf(
                       furi_string_get_cstr(line),
                       "%u %u %u %u",
                       &trigger,
                       &pin1,
                       &pin2,
                       &action) == 4 &&
                   trigger < ShortcutTriggerCount && action < ShortcutActionCount) {
                    ShortcutRule* rule = &instance->rules[instance->count];
                    rule->trigger = (ShortcutTrigger)trigger;
                    rule->gpio_pin1 = (uint8_t)pin1;
                    rule->gpio_pin2 = (uint8_t)pin2;
                    rule->action = (ShortcutAction)action;
                    instance->gpio_rule_broken[instance->count] = false;
                    instance->count++;
                }
            }
        }
    }

    furi_string_free(line);
    file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    furi_mutex_release(instance->mutex);
}

void shortcuts_save(Shortcuts* instance) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);

    if(file_stream_open(stream, SHORTCUTS_FILE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FuriString* line = furi_string_alloc();
        furi_string_printf(line, "Shortcuts Version 1\n");
        stream_write_string(stream, line);
        for(size_t i = 0; i < instance->count; i++) {
            const ShortcutRule* rule = &instance->rules[i];
            furi_string_printf(
                line,
                "%u %u %u %u\n",
                (unsigned)rule->trigger,
                (unsigned)rule->gpio_pin1,
                (unsigned)rule->gpio_pin2,
                (unsigned)rule->action);
            stream_write_string(stream, line);
        }
        furi_string_free(line);
    }

    file_stream_close(stream);
    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    furi_mutex_release(instance->mutex);
}

size_t shortcuts_get_count(Shortcuts* instance) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    size_t count = instance->count;
    furi_mutex_release(instance->mutex);
    return count;
}

bool shortcuts_get(Shortcuts* instance, size_t index, ShortcutRule* out_rule) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    bool ok = index < instance->count;
    if(ok) {
        *out_rule = instance->rules[index];
    }
    furi_mutex_release(instance->mutex);
    return ok;
}

bool shortcuts_add(Shortcuts* instance, const ShortcutRule* rule) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    bool ok = instance->count < SHORTCUTS_MAX;
    if(ok) {
        instance->rules[instance->count] = *rule;
        instance->gpio_rule_broken[instance->count] = false;
        instance->count++;
    }
    furi_mutex_release(instance->mutex);
    if(ok) {
        shortcuts_save(instance);
    }
    return ok;
}

void shortcuts_remove(Shortcuts* instance, size_t index) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    if(index < instance->count) {
        for(size_t i = index; i + 1 < instance->count; i++) {
            instance->rules[i] = instance->rules[i + 1];
            instance->gpio_rule_broken[i] = instance->gpio_rule_broken[i + 1];
        }
        instance->count--;
    }
    furi_mutex_release(instance->mutex);
    shortcuts_save(instance);
}

static void shortcuts_run_triggers(Shortcuts* instance, ShortcutTrigger trigger) {
    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    ShortcutAction actions_to_run[SHORTCUTS_MAX];
    size_t actions_to_run_count = 0;
    for(size_t i = 0; i < instance->count; i++) {
        if(instance->rules[i].trigger == trigger) {
            actions_to_run[actions_to_run_count++] = instance->rules[i].action;
        }
    }
    furi_mutex_release(instance->mutex);

    for(size_t i = 0; i < actions_to_run_count; i++) {
        shortcuts_execute_action(actions_to_run[i]);
    }
}

static void shortcuts_storage_callback(const void* message, void* context) {
    Shortcuts* instance = context;
    const StorageEvent* event = message;
    if(event->type == StorageEventTypeCardUnmount) {
        shortcuts_run_triggers(instance, ShortcutTriggerSdCardRemoved);
    }
}

static void shortcuts_power_callback(const void* message, void* context) {
    Shortcuts* instance = context;
    const PowerEvent* event = message;
    if(event->type == PowerEventTypeStartCharging) {
        shortcuts_run_triggers(instance, ShortcutTriggerPluggedIn);
    } else if(event->type == PowerEventTypeStopCharging) {
        shortcuts_run_triggers(instance, ShortcutTriggerUnplugged);
    }
}

static void shortcuts_gpio_poll_callback(void* context) {
    Shortcuts* instance = context;

    furi_mutex_acquire(instance->mutex, FuriWaitForever);
    for(size_t i = 0; i < instance->count; i++) {
        if(instance->rules[i].trigger != ShortcutTriggerGpioPull) {
            continue;
        }

        uint8_t pin1_idx = instance->rules[i].gpio_pin1;
        uint8_t pin2_idx = instance->rules[i].gpio_pin2;
        if(pin1_idx >= gpio_pins_count) pin1_idx = 0;
        if(pin2_idx >= gpio_pins_count) pin2_idx = 0;

        const GpioPin* pin1 = gpio_pins[pin1_idx].pin;
        const GpioPin* pin2 = gpio_pins[pin2_idx].pin;

        furi_hal_gpio_init(pin1, GpioModeOutputPushPull, GpioPullNo, GpioSpeedLow);
        furi_hal_gpio_write(pin1, true);
        furi_hal_gpio_init(pin2, GpioModeInput, GpioPullDown, GpioSpeedLow);
        bool loop_intact = furi_hal_gpio_read(pin2);

        if(!loop_intact && !instance->gpio_rule_broken[i]) {
            instance->gpio_rule_broken[i] = true;
            shortcuts_execute_action(instance->rules[i].action);
        } else if(loop_intact) {
            instance->gpio_rule_broken[i] = false;
        }
    }
    furi_mutex_release(instance->mutex);
}

static Shortcuts* shortcuts_alloc(void) {
    Shortcuts* instance = malloc(sizeof(Shortcuts));
    instance->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    instance->count = 0;
    return instance;
}

int32_t shortcuts_srv(void* p) {
    UNUSED(p);

    if(!furi_hal_is_normal_boot()) {
        FURI_LOG_W(TAG, "Skipping start in special boot mode");
        furi_thread_suspend(furi_thread_get_current_id());
        return 0;
    }

    Shortcuts* instance = shortcuts_alloc();
    shortcuts_load(instance);

    furi_record_create(RECORD_SHORTCUTS, instance);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    instance->storage_subscription =
        furi_pubsub_subscribe(storage_get_pubsub(storage), shortcuts_storage_callback, instance);
    furi_record_close(RECORD_STORAGE);

    Power* power = furi_record_open(RECORD_POWER);
    instance->power_subscription =
        furi_pubsub_subscribe(power_get_pubsub(power), shortcuts_power_callback, instance);
    furi_record_close(RECORD_POWER);

    instance->gpio_poll_timer =
        furi_timer_alloc(shortcuts_gpio_poll_callback, FuriTimerTypePeriodic, instance);
    furi_timer_start(instance->gpio_poll_timer, furi_ms_to_ticks(SHORTCUTS_GPIO_POLL_MS));

    furi_thread_suspend(furi_thread_get_current_id());
    return 0;
}
