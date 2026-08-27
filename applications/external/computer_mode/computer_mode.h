#pragma once
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_serial.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <storage/storage.h>
#include <momentum/settings.h>

// Talks to a custom (non-FlipperHTTP) ESP32 firmware that acts as a USB
// host for an external keyboard/mouse and relays HID events over this same
// UART channel used elsewhere for the WiFi Devboard. See
// tools/computer_mode_esp32/ for that firmware - EXPERIMENTAL, unverified
// against real hardware (see its own README for why).
#define COMPUTER_MODE_UART_CH (momentum_settings.uart_esp_channel)
#define COMPUTER_MODE_BAUDRATE 115200
#define COMPUTER_MODE_RX_LINE_MAX 64
#define COMPUTER_MODE_TEXT_MAX 2048

typedef struct {
    ViewPort* view_port;
    Gui* gui;
    Storage* storage;

    FuriHalSerialHandle* serial_handle;
    FuriStreamBuffer* rx_stream;
    FuriThread* rx_thread;
    volatile bool rx_thread_running;

    FuriMessageQueue* queue;

    FuriString* text;
    bool shift_held;

    int16_t mouse_x;
    int16_t mouse_y;
    bool mouse_button;
} ComputerModeApp;
