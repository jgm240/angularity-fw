#include <furi.h>
#include <usb.h>
#include <usb_std.h>
#include <math.h>

#include "usb_audio_driver.h"
#include "usb_audio_defs.h"
#include "cm3_usb_audio.h"
#include "../usb_audio_hal.h"

// Universal Serial Bus Device Class Definition for Audio Devices, Rev 1.0.
// Topology: Input Terminal (USB streaming) -> Output Terminal (speaker),
// no Feature Unit (no volume/mute control - keeps the class-specific
// control surface at zero, minimizing host-compatibility risk since we
// can't test against real hardware/host stacks here).

#define USB_VID 0x6666
#define USB_PID 0x511A

#define USB_EP0_SIZE 8

#define USB_AUDIO_EP_OUT       0x01
#define USB_AUDIO_EP_MAX_PACKET 32 // nominal 16 bytes/ms @ 8000 Hz mono 16-bit, + jitter margin

// The USB-facing format is 16-bit signed PCM - the format essentially every
// host audio stack tests against, unlike 8-bit which is rarely exercised
// in the wild and appeared to trip up the host driver (reported as "static").
// The speaker HAL still only understands 8-bit unsigned PCM (matching the
// PWM hardware), so audio_iso_rx() downconverts each sample on the way in,
// using the same scaling wav_player.c uses for its 16-bit-source case.
#define USB_AUDIO_SUBFRAME_SIZE 2
#define USB_AUDIO_BIT_RESOLUTION 16

#define AC_INTERFACE_NUM 0
#define AS_INTERFACE_NUM 1

#define EP_CFG_DECONFIGURE 0
#define EP_CFG_CONFIGURE   1

enum {
    USB_STR_ZERO,
    USB_STR_MANUFACTURER,
    USB_STR_PRODUCT,
    USB_STR_SERIAL_NUMBER,
};

static const struct usb_device_descriptor device_descriptor = {
    .bLength = sizeof(struct usb_device_descriptor),
    .bDescriptorType = USB_DTYPE_DEVICE,
    .bcdUSB = VERSION_BCD(2, 0, 0),
    .bDeviceClass = USB_CLASS_PER_INTERFACE,
    .bDeviceSubClass = USB_SUBCLASS_NONE,
    .bDeviceProtocol = USB_PROTO_NONE,
    .bMaxPacketSize0 = USB_EP0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = VERSION_BCD(1, 0, 0),
    .iManufacturer = USB_STR_MANUFACTURER,
    .iProduct = USB_STR_PRODUCT,
    .iSerialNumber = USB_STR_SERIAL_NUMBER,
    .bNumConfigurations = 1,
};

struct AudioControlBlock {
    struct usb_audio_header_descriptor_head header_head;
    struct usb_audio_header_descriptor_body header_body;
    struct usb_audio_input_terminal_descriptor input_terminal;
    struct usb_audio_output_terminal_descriptor output_terminal;
} __attribute__((packed));

struct AudioConfigDescriptor {
    struct usb_config_descriptor config;

    // AudioControl interface (topology declaration only, no endpoints)
    struct usb_interface_descriptor audio_control_iface;
    struct AudioControlBlock audio_control_block;

    // AudioStreaming interface, alt 0: zero-bandwidth (idle)
    struct usb_interface_descriptor audio_stream_iface_alt0;

    // AudioStreaming interface, alt 1: active, carries the iso OUT endpoint
    struct usb_interface_descriptor audio_stream_iface_alt1;
    struct usb_audio_stream_interface_descriptor audio_stream_general;
    struct usb_audio_format_type1_descriptor_1freq audio_stream_format;
    struct usb_endpoint_descriptor iso_out_ep;
    struct usb_audio_stream_audio_endpoint_descriptor iso_out_ep_audio;
} __attribute__((packed));

_Static_assert(
    sizeof(struct usb_audio_format_type1_descriptor_1freq) == 11,
    "USB Audio Format Type I descriptor must pack its 24-bit sample rate field to 3 bytes");

static const struct AudioConfigDescriptor config_descriptor = {
    .config =
        {
            .bLength = sizeof(struct usb_config_descriptor),
            .bDescriptorType = USB_DTYPE_CONFIGURATION,
            .wTotalLength = sizeof(struct AudioConfigDescriptor),
            .bNumInterfaces = 2,
            .bConfigurationValue = 1,
            .iConfiguration = 0,
            .bmAttributes = USB_CFG_ATTR_RESERVED,
            .bMaxPower = USB_CFG_POWER_MA(100),
        },
    .audio_control_iface =
        {
            .bLength = sizeof(struct usb_interface_descriptor),
            .bDescriptorType = USB_DTYPE_INTERFACE,
            .bInterfaceNumber = AC_INTERFACE_NUM,
            .bAlternateSetting = 0,
            .bNumEndpoints = 0,
            .bInterfaceClass = USB_CLASS_AUDIO,
            .bInterfaceSubClass = USB_AUDIO_SUBCLASS_CONTROL,
            .bInterfaceProtocol = USB_PROTO_NONE,
            .iInterface = 0,
        },
    .audio_control_block =
        {
            .header_head =
                {
                    .bLength = sizeof(struct usb_audio_header_descriptor_head) +
                               sizeof(struct usb_audio_header_descriptor_body),
                    .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
                    .bDescriptorSubtype = USB_AUDIO_TYPE_HEADER,
                    .bcdADC = VERSION_BCD(1, 0, 0),
                    .wTotalLength = sizeof(struct AudioControlBlock),
                    .bInCollection = 1,
                },
            .header_body =
                {
                    .baInterfaceNr = AS_INTERFACE_NUM,
                },
            .input_terminal =
                {
                    .bLength = sizeof(struct usb_audio_input_terminal_descriptor),
                    .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
                    .bDescriptorSubtype = USB_AUDIO_TYPE_INPUT_TERMINAL,
                    .bTerminalID = 1,
                    .wTerminalType = USB_AUDIO_TERMINAL_USB_STREAMING,
                    .bAssocTerminal = 0,
                    .bNrChannels = 1,
                    .wChannelConfig = 0,
                    .iChannelNames = 0,
                    .iTerminal = 0,
                },
            .output_terminal =
                {
                    .bLength = sizeof(struct usb_audio_output_terminal_descriptor),
                    .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
                    .bDescriptorSubtype = USB_AUDIO_TYPE_OUTPUT_TERMINAL,
                    .bTerminalID = 2,
                    .wTerminalType = USB_AUDIO_TERMINAL_SPEAKER,
                    .bAssocTerminal = 0,
                    .bSourceID = 1,
                    .iTerminal = 0,
                },
        },
    .audio_stream_iface_alt0 =
        {
            .bLength = sizeof(struct usb_interface_descriptor),
            .bDescriptorType = USB_DTYPE_INTERFACE,
            .bInterfaceNumber = AS_INTERFACE_NUM,
            .bAlternateSetting = 0,
            .bNumEndpoints = 0,
            .bInterfaceClass = USB_CLASS_AUDIO,
            .bInterfaceSubClass = USB_AUDIO_SUBCLASS_AUDIOSTREAMING,
            .bInterfaceProtocol = USB_PROTO_NONE,
            .iInterface = 0,
        },
    .audio_stream_iface_alt1 =
        {
            .bLength = sizeof(struct usb_interface_descriptor),
            .bDescriptorType = USB_DTYPE_INTERFACE,
            .bInterfaceNumber = AS_INTERFACE_NUM,
            .bAlternateSetting = 1,
            .bNumEndpoints = 1,
            .bInterfaceClass = USB_CLASS_AUDIO,
            .bInterfaceSubClass = USB_AUDIO_SUBCLASS_AUDIOSTREAMING,
            .bInterfaceProtocol = USB_PROTO_NONE,
            .iInterface = 0,
        },
    .audio_stream_general =
        {
            .bLength = sizeof(struct usb_audio_stream_interface_descriptor),
            .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
            .bDescriptorSubtype = USB_AUDIO_AS_GENERAL,
            .bTerminalLink = 1,
            .bDelay = 1,
            .wFormatTag = USB_AUDIO_FORMAT_TAG_PCM,
        },
    .audio_stream_format =
        {
            .head =
                {
                    .bLength = sizeof(struct usb_audio_format_type1_descriptor_1freq),
                    .bDescriptorType = USB_AUDIO_DT_CS_INTERFACE,
                    .bDescriptorSubtype = USB_AUDIO_AS_FORMAT_TYPE,
                    .bFormatType = USB_AUDIO_FORMAT_TYPE_I,
                    .bNrChannels = 1,
                    .bSubFrameSize = USB_AUDIO_SUBFRAME_SIZE,
                    .bBitResolution = USB_AUDIO_BIT_RESOLUTION,
                    .bSamFreqType = 1,
                },
            .freqs =
                {
                    {.tSamFreq = USB_AUDIO_SAMPLE_RATE},
                },
        },
    .iso_out_ep =
        {
            .bLength = sizeof(struct usb_endpoint_descriptor),
            .bDescriptorType = USB_DTYPE_ENDPOINT,
            .bEndpointAddress = USB_AUDIO_EP_OUT,
            .bmAttributes = USB_EPTYPE_ISOCHRONUS | (0x02 << 2), // Adaptive sync, Data endpoint
            .wMaxPacketSize = USB_AUDIO_EP_MAX_PACKET,
            .bInterval = 1,
        },
    .iso_out_ep_audio =
        {
            .bLength = sizeof(struct usb_audio_stream_audio_endpoint_descriptor),
            .bDescriptorType = USB_AUDIO_DT_CS_ENDPOINT,
            .bDescriptorSubtype = USB_AUDIO_EP_GENERAL,
            .bmAttributes = 0,
            .bLockDelayUnits = 0,
            .wLockDelay = 0,
        },
};

static const struct usb_string_descriptor dev_manufacturer_string =
    USB_STRING_DESC("Flipper Devices Inc.");

static const struct usb_string_descriptor dev_product_string =
    USB_STRING_DESC("Angularity USB Audio");

static const struct usb_string_descriptor dev_serial_number_string =
    USB_STRING_DESC("Serial Number");

static void audio_init(usbd_device* dev, FuriHalUsbInterface* intf, void* ctx);
static void audio_deinit(usbd_device* dev);
static void audio_on_wakeup(usbd_device* dev);
static void audio_on_suspend(usbd_device* dev);
static usbd_respond audio_ep_config(usbd_device* dev, uint8_t cfg);
static usbd_respond
    audio_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback);

FuriHalUsbInterface usb_audio_interface = {
    .init = audio_init,
    .deinit = audio_deinit,
    .wakeup = audio_on_wakeup,
    .suspend = audio_on_suspend,
    .dev_descr = (struct usb_device_descriptor*)&device_descriptor,
    .cfg_descr = (void*)&config_descriptor,
};

typedef struct {
    usbd_device* dev;
    bool connected;
    bool streaming;
    UsbAudioStreamStateCallback stream_callback;
    void* stream_callback_context;
} UsbAudioUsb;

static UsbAudioUsb usb_audio_usb;
static volatile int32_t audio_volume_percent = 100;

// Soft-clip lookup table: maps a gained, pre-bias sample (already divided
// down to roughly +-8-bit scale) through tanhf() to 0..255. tanhf() is
// unity-slope near zero, so normal-level audio passes through essentially
// unchanged, while anything gain has pushed past the natural 8-bit range
// saturates smoothly instead of hard-clipping into a harsh square wave.
// Built once (in a real thread, not the USB ISR) since tanhf() is too
// expensive to call per-sample inside the 1kHz isochronous RX handler.
#define SOFT_CLIP_LUT_RANGE 512 // +-4x the unclipped 8-bit half-range; tanh is
                                // already >99.9% saturated well before this
#define SOFT_CLIP_LUT_SIZE (SOFT_CLIP_LUT_RANGE * 2 + 1)
static uint8_t soft_clip_lut[SOFT_CLIP_LUT_SIZE];

static void build_soft_clip_lut(void) {
    for(int32_t i = 0; i < SOFT_CLIP_LUT_SIZE; i++) {
        float x = (float)(i - SOFT_CLIP_LUT_RANGE) / 127.5f;
        float y = 127.5f + 127.5f * tanhf(x);
        if(y < 0.0f) y = 0.0f;
        if(y > 255.0f) y = 255.0f;
        soft_clip_lut[i] = (uint8_t)(y + 0.5f);
    }
}

static uint8_t soft_clip(int32_t value) {
    if(value < -SOFT_CLIP_LUT_RANGE) value = -SOFT_CLIP_LUT_RANGE;
    if(value > SOFT_CLIP_LUT_RANGE) value = SOFT_CLIP_LUT_RANGE;
    return soft_clip_lut[value + SOFT_CLIP_LUT_RANGE];
}

void usb_audio_set_stream_state_callback(UsbAudioStreamStateCallback callback, void* context) {
    usb_audio_usb.stream_callback = callback;
    usb_audio_usb.stream_callback_context = context;
}

void usb_audio_set_volume_percent(int32_t percent) {
    audio_volume_percent = percent;
}

static void audio_init(usbd_device* dev, FuriHalUsbInterface* intf, void* ctx) {
    UNUSED(intf);
    UNUSED(ctx);

    usb_audio_interface.str_manuf_descr = (void*)&dev_manufacturer_string;
    usb_audio_interface.str_prod_descr = (void*)&dev_product_string;
    usb_audio_interface.str_serial_descr = (void*)&dev_serial_number_string;
    usb_audio_interface.dev_descr->idVendor = USB_VID;
    usb_audio_interface.dev_descr->idProduct = USB_PID;

    usb_audio_usb.dev = dev;
    usb_audio_usb.streaming = false;

    build_soft_clip_lut();

    usbd_reg_config(dev, audio_ep_config);
    usbd_reg_control(dev, audio_control);

    usbd_connect(dev, true);
}

static void audio_deinit(usbd_device* dev) {
    usb_audio_usb.connected = false;
    usb_audio_usb.streaming = false;
    usb_audio_usb.dev = NULL;

    usbd_reg_config(dev, NULL);
    usbd_reg_control(dev, NULL);
}

static void audio_on_wakeup(usbd_device* dev) {
    UNUSED(dev);
    usb_audio_usb.connected = true;
}

static void audio_on_suspend(usbd_device* dev) {
    UNUSED(dev);
    usb_audio_usb.connected = false;
    usb_audio_usb.streaming = false;
    if(usb_audio_usb.stream_callback) {
        usb_audio_usb.stream_callback(false, usb_audio_usb.stream_callback_context);
    }
}

static void audio_iso_rx(usbd_device* dev, uint8_t event, uint8_t ep) {
    UNUSED(ep);
    if(event != usbd_evt_eprx) return;

    uint8_t buffer[USB_AUDIO_EP_MAX_PACKET];
    int32_t len = usbd_ep_read(dev, USB_AUDIO_EP_OUT, buffer, sizeof(buffer));
    if(len <= 0) return;

    // 16-bit LE signed PCM -> 8-bit unsigned PCM, same scaling wav_player.c
    // uses for its 16-bit-source case, so the ring buffer/PWM/DMA chain
    // below never has to know the USB-facing format differs from its own.
    uint8_t converted[USB_AUDIO_EP_MAX_PACKET / 2];
    size_t sample_count = (size_t)len / 2;
    for(size_t i = 0; i < sample_count; i++) {
        int16_t sample = (int16_t)((buffer[i * 2 + 1] << 8) | buffer[i * 2]);
        int32_t scaled = (int32_t)sample * audio_volume_percent / 100;
        converted[i] = soft_clip(scaled / 256);
    }
    usb_audio_hal_push(converted, sample_count);
}

static void audio_stream_enable(usbd_device* dev, bool enable) {
    if(enable) {
        usbd_ep_config(dev, USB_AUDIO_EP_OUT, USB_EPTYPE_ISOCHRONUS, USB_AUDIO_EP_MAX_PACKET);
        usbd_reg_endpoint(dev, USB_AUDIO_EP_OUT, audio_iso_rx);
    } else {
        usbd_ep_deconfig(dev, USB_AUDIO_EP_OUT);
        usbd_reg_endpoint(dev, USB_AUDIO_EP_OUT, NULL);
    }
    usb_audio_usb.streaming = enable;
    if(usb_audio_usb.stream_callback) {
        usb_audio_usb.stream_callback(enable, usb_audio_usb.stream_callback_context);
    }
}

static usbd_respond audio_ep_config(usbd_device* dev, uint8_t cfg) {
    switch(cfg) {
    case EP_CFG_DECONFIGURE:
        if(usb_audio_usb.streaming) {
            audio_stream_enable(dev, false);
        }
        return usbd_ack;
    case EP_CFG_CONFIGURE:
        // AS interface starts at alt setting 0 (zero-bandwidth); the host
        // must SET_INTERFACE(alt=1) before any endpoint is active.
        return usbd_ack;
    default:
        return usbd_fail;
    }
}

static usbd_respond
    audio_control(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback) {
    UNUSED(callback);

    if(((USB_REQ_RECIPIENT | USB_REQ_TYPE) & req->bmRequestType) ==
           (USB_REQ_INTERFACE | USB_REQ_STANDARD) &&
       req->bRequest == USB_STD_SET_INTERFACE && req->wIndex == AS_INTERFACE_NUM) {
        if(req->wValue == 1) {
            audio_stream_enable(dev, true);
            return usbd_ack;
        } else if(req->wValue == 0) {
            audio_stream_enable(dev, false);
            return usbd_ack;
        }
        return usbd_fail;
    }

    if(((USB_REQ_RECIPIENT | USB_REQ_TYPE) & req->bmRequestType) ==
           (USB_REQ_INTERFACE | USB_REQ_STANDARD) &&
       req->bRequest == USB_STD_GET_INTERFACE && req->wIndex == AS_INTERFACE_NUM) {
        static uint8_t alt_setting;
        alt_setting = usb_audio_usb.streaming ? 1 : 0;
        dev->status.data_ptr = &alt_setting;
        dev->status.data_count = sizeof(alt_setting);
        return usbd_ack;
    }

    return usbd_fail;
}
