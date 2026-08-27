#pragma once

// Additional USB Audio Class 1.0 constants not covered by cm3_usb_audio.h
// (Class-specific AS Interface Descriptor Subtypes, Table 4-19 / Format
// Type Codes, "Audio Data Formats" spec, and Terminal Types, Terminal
// Types spec appendices).

#define USB_AUDIO_AS_GENERAL      0x01
#define USB_AUDIO_AS_FORMAT_TYPE  0x02

#define USB_AUDIO_FORMAT_TYPE_I 0x01

#define USB_AUDIO_FORMAT_TAG_PCM 0x0001

// Class-specific AS Isochronous Audio Data Endpoint Descriptor Subtype
#define USB_AUDIO_EP_GENERAL 0x01

// Terminal Types (USB Terminal Types spec)
#define USB_AUDIO_TERMINAL_USB_STREAMING 0x0101
#define USB_AUDIO_TERMINAL_SPEAKER       0x0301
