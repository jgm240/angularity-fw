/*
 * Computer Mode ESP32 firmware - EXPERIMENTAL, unverified against real
 * hardware. See ../README.md before flashing.
 *
 * Replaces FlipperHTTP on the WiFi Devboard's ESP32-S2 with a USB Host HID
 * driver: it enumerates an external USB keyboard/mouse plugged into the
 * devboard's USB-C port (via an OTG adapter - the port has to actually
 * support host mode for this to work at all, which is the single biggest
 * unknown here), and relays key/mouse events to the Flipper over the same
 * UART0 connection FlipperHTTP already uses for its own communication.
 *
 * USB Host HID handling here is adapted from Espressif's own official
 * example (esp-idf/examples/peripherals/usb/host/hid) - that part is
 * real, current, verified API usage. What's novel and untested is
 * everything downstream of a report arriving: the protocol sent over
 * UART, and pairing it with computer_mode.c on the Flipper side.
 *
 * Wire protocol (one line per event, LF-terminated):
 *   D<code>              key down, code = USB HID keyboard usage id (decimal)
 *   U<code>              key up
 *   M<dx>,<dy>,<buttons> mouse delta + button bitmask (bit0 = left, bit1 = right)
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "usb/hid_usage_mouse.h"

#define UART_PORT UART_NUM_0
#define UART_BAUDRATE 115200

static const char* TAG = "computer_mode";

typedef enum { APP_EVENT_HID_HOST } app_event_group_t;

typedef struct {
    app_event_group_t event_group;
    struct {
        hid_host_device_handle_t handle;
        hid_host_driver_event_t event;
        void* arg;
    } hid_host_device;
} app_event_queue_t;

static QueueHandle_t app_event_queue = NULL;

// ---- UART ----

static void uart_send_line(const char* line) {
    uart_write_bytes(UART_PORT, line, strlen(line));
    uart_write_bytes(UART_PORT, "\n", 1);
}

static void uart_init(void) {
    uart_config_t config = {
        .baud_rate = UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 1024, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &config));
    // Leave pins at whatever the board's default UART0 routing already is
    // (the same pins FlipperHTTP's Serial.begin() with no args used) -
    // deliberately not specifying GPIO numbers here, see the README for why.
    ESP_ERROR_CHECK(uart_set_pin(
        UART_PORT,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE));
}

// ---- HID report handling ----

static void hid_keyboard_report_callback(const uint8_t* const data, const int length) {
    hid_keyboard_input_report_boot_t* kb_report = (hid_keyboard_input_report_boot_t*)data;
    if(length < (int)sizeof(hid_keyboard_input_report_boot_t)) return;

    static uint8_t prev_keys[HID_KEYBOARD_KEY_MAX] = {0};
    char line[16];

    for(int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        uint8_t was = prev_keys[i];
        if(was > HID_KEY_ERROR_UNDEFINED) {
            bool still_down = false;
            for(int j = 0; j < HID_KEYBOARD_KEY_MAX; j++) {
                if(kb_report->key[j] == was) {
                    still_down = true;
                    break;
                }
            }
            if(!still_down) {
                snprintf(line, sizeof(line), "U%d", was);
                uart_send_line(line);
            }
        }

        uint8_t now = kb_report->key[i];
        if(now > HID_KEY_ERROR_UNDEFINED) {
            bool was_down = false;
            for(int j = 0; j < HID_KEYBOARD_KEY_MAX; j++) {
                if(prev_keys[j] == now) {
                    was_down = true;
                    break;
                }
            }
            if(!was_down) {
                snprintf(line, sizeof(line), "D%d", now);
                uart_send_line(line);
            }
        }
    }

    memcpy(prev_keys, kb_report->key, HID_KEYBOARD_KEY_MAX);
}

static void hid_mouse_report_callback(const uint8_t* const data, const int length) {
    hid_mouse_input_report_boot_t* mouse_report = (hid_mouse_input_report_boot_t*)data;
    if(length < (int)sizeof(hid_mouse_input_report_boot_t)) return;

    int buttons = (mouse_report->buttons.button1 ? 1 : 0) | (mouse_report->buttons.button2 ? 2 : 0);
    char line[32];
    snprintf(
        line,
        sizeof(line),
        "M%d,%d,%d",
        mouse_report->x_displacement,
        mouse_report->y_displacement,
        buttons);
    uart_send_line(line);
}

static void hid_host_interface_callback(
    hid_host_device_handle_t hid_device_handle,
    const hid_host_interface_event_t event,
    void* arg) {
    uint8_t data[64] = {0};
    size_t data_length = 0;
    hid_host_dev_params_t dev_params;
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

    switch(event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        ESP_ERROR_CHECK(
            hid_host_device_get_raw_input_report_data(hid_device_handle, data, 64, &data_length));
        if(dev_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
            if(dev_params.proto == HID_PROTOCOL_KEYBOARD) {
                hid_keyboard_report_callback(data, data_length);
            } else if(dev_params.proto == HID_PROTOCOL_MOUSE) {
                hid_mouse_report_callback(data, data_length);
            }
        }
        break;
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_ERROR_CHECK(hid_host_device_close(hid_device_handle));
        break;
    default:
        break;
    }
}

static void hid_host_device_event(
    hid_host_device_handle_t hid_device_handle,
    const hid_host_driver_event_t event,
    void* arg) {
    hid_host_dev_params_t dev_params;
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

    if(event == HID_HOST_DRIVER_EVENT_CONNECTED) {
        const hid_host_device_config_t dev_config = {
            .callback = hid_host_interface_callback,
            .callback_arg = NULL,
        };
        if(dev_params.proto != HID_PROTOCOL_NONE) {
            ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));
            if(dev_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
                ESP_ERROR_CHECK(
                    hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_BOOT));
                if(dev_params.proto == HID_PROTOCOL_KEYBOARD) {
                    ESP_ERROR_CHECK(hid_class_request_set_idle(hid_device_handle, 0, 0));
                }
            }
            ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));
        }
    }
}

static void hid_host_device_callback(
    hid_host_device_handle_t hid_device_handle,
    const hid_host_driver_event_t event,
    void* arg) {
    const app_event_queue_t evt_queue = {
        .event_group = APP_EVENT_HID_HOST,
        .hid_host_device = {.handle = hid_device_handle, .event = event, .arg = arg},
    };
    if(app_event_queue) {
        xQueueSend(app_event_queue, &evt_queue, 0);
    }
}

static void usb_lib_task(void* arg) {
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));
    xTaskNotifyGive(arg);

    while(true) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if(event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_ERROR_CHECK(usb_host_device_free_all());
        }
    }
}

void app_main(void) {
    esp_log_level_set("*", ESP_LOG_NONE); // see sdkconfig.defaults for why

    uart_init();

    BaseType_t task_created = xTaskCreatePinnedToCore(
        usb_lib_task, "usb_events", 4096, xTaskGetCurrentTaskHandle(), 2, NULL, 0);
    assert(task_created == pdTRUE);
    ulTaskNotifyTake(false, 1000);

    const hid_host_driver_config_t hid_host_driver_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_host_device_callback,
        .callback_arg = NULL,
    };
    ESP_ERROR_CHECK(hid_host_install(&hid_host_driver_config));

    app_event_queue = xQueueCreate(10, sizeof(app_event_queue_t));

    app_event_queue_t evt_queue;
    while(1) {
        if(xQueueReceive(app_event_queue, &evt_queue, portMAX_DELAY)) {
            if(evt_queue.event_group == APP_EVENT_HID_HOST) {
                hid_host_device_event(
                    evt_queue.hid_host_device.handle,
                    evt_queue.hid_host_device.event,
                    evt_queue.hid_host_device.arg);
            }
        }
    }
}
