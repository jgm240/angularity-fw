#pragma once
#include <stdint.h>
#include <stddef.h>

// Speaker PWM/DMA driver, adapted from applications/external/wav_player's
// TIM16 (PWM duty=sample)/TIM2 (sample-rate clock)/DMA1-ch1 (circular,
// memory-to-peripheral) chain. Unlike wav_player, the DMA source buffer
// here is a persistent ring buffer that the USB isochronous OUT endpoint
// writes live samples into, so the DMA never needs refilling by software -
// it just continuously replays whatever is currently in the ring, which is
// silence (0x80) until the host starts streaming.

#define USB_AUDIO_RING_SIZE 512 // 64ms of buffering at 8000 Hz

void usb_audio_hal_init(uint32_t sample_rate);
void usb_audio_hal_deinit(void);

void usb_audio_hal_start(void);
void usb_audio_hal_stop(void);

// Writes `len` bytes of unsigned 8-bit PCM into the ring buffer, ahead of
// the DMA's current playback position. Safe to call from USB ISR context
// (single-producer, lock-free against the DMA's hardware-driven consumer).
void usb_audio_hal_push(const uint8_t* data, size_t len);
