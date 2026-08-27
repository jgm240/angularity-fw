#pragma once
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>

typedef struct {
    ViewPort* view_port;
    Gui* gui;
    FuriMessageQueue* events;
    FuriTimer* redraw_timer;
    FuriHalUsbInterface* usb_config_restore;
    volatile bool streaming;
    bool usb_locked;
    int32_t volume_percent;
} UsbAudioApp;
