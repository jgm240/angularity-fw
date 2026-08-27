#pragma once
#include <stdint.h>
#include <stddef.h>

// Speaker PWM/DMA driver - same TIM16 (PWM duty=sample)/TIM2 (sample-rate
// clock)/DMA1-ch1 (circular, memory-to-peripheral) chain as
// applications/external/wav_player, parameterized by sample rate since the
// source could be a WAV file at any rate or an MP3 frame's own rate.

void net_audio_hal_init(uint32_t sample_rate);
void net_audio_hal_deinit(void);

void net_audio_hal_start(void);
void net_audio_hal_stop(void);

void net_audio_hal_dma_init(uint32_t buffer_address, size_t buffer_size);
void net_audio_hal_dma_start(void);
void net_audio_hal_dma_stop(void);

// Registers the ISR for DMA half/full transfer events (same signature as
// furi_hal_interrupt_set_isr for FuriHalInterruptIdDma1Ch1).
void net_audio_hal_set_dma_isr(void (*isr)(void* ctx), void* ctx);
