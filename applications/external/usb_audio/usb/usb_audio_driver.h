#pragma once
#include <furi_hal_usb.h>

extern FuriHalUsbInterface usb_audio_interface;

// Fixed stream format: mono, 16-bit signed PCM over USB (downconverted to
// the speaker HAL's native 8-bit unsigned format by the driver on receipt).
#define USB_AUDIO_SAMPLE_RATE 8000

typedef void (*UsbAudioStreamStateCallback)(bool streaming, void* context);

// Called by usb_audio_hal when the host starts/stops the AS interface
// (alternate setting 1/0), so the app can drive the speaker DMA/PWM chain.
void usb_audio_set_stream_state_callback(UsbAudioStreamStateCallback callback, void* context);

// Software gain applied to each sample before the 16-bit -> 8-bit
// downconvert, as a percentage (100 = unity, source samples passed through
// as-is). Hard-clipped to the 8-bit range rather than soft-limited, to keep
// the per-sample cost in the USB ISR minimal.
void usb_audio_set_volume_percent(int32_t percent);
