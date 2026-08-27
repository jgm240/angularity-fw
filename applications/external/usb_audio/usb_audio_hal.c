#include "usb_audio_hal.h"

#include <stm32wbxx_ll_tim.h>
#include <stm32wbxx_ll_dma.h>
#include <stm32wbxx_ll_gpio.h>
#include <furi_hal.h>
#include <furi_hal_gpio.h>
#include <furi_hal_resources.h>
#include <string.h>

#define FURI_HAL_SPEAKER_TIMER  TIM16
#define SAMPLE_RATE_TIMER       TIM2
#define FURI_HAL_SPEAKER_CHANNEL LL_TIM_CHANNEL_CH1
#define DMA_INSTANCE            DMA1, LL_DMA_CHANNEL_1

// 0x80 = electrical center of unsigned 8-bit PCM = silence. DMA replays
// this ring forever; the USB ISR overwrites it with live samples ahead of
// the DMA's current read position.
static uint8_t usb_audio_ring[USB_AUDIO_RING_SIZE];
static volatile uint32_t usb_audio_write_pos = 0;

void usb_audio_hal_init(uint32_t sample_rate) {
    memset(usb_audio_ring, 0x80, sizeof(usb_audio_ring));
    usb_audio_write_pos = 0;

    furi_hal_bus_enable(FuriHalBusTIM2);

    LL_TIM_InitTypeDef TIM_InitStruct = {0};
    TIM_InitStruct.Prescaler = 1;
    TIM_InitStruct.Autoreload = 255;
    LL_TIM_Init(FURI_HAL_SPEAKER_TIMER, &TIM_InitStruct);

    LL_TIM_OC_InitTypeDef TIM_OC_InitStruct = {0};
    TIM_OC_InitStruct.OCMode = LL_TIM_OCMODE_PWM1;
    TIM_OC_InitStruct.OCState = LL_TIM_OCSTATE_ENABLE;
    TIM_OC_InitStruct.CompareValue = 128;
    LL_TIM_OC_Init(FURI_HAL_SPEAKER_TIMER, FURI_HAL_SPEAKER_CHANNEL, &TIM_OC_InitStruct);

    TIM_InitStruct.Prescaler = 0;
    TIM_InitStruct.Autoreload = SystemCoreClock / sample_rate - 1;
    LL_TIM_Init(SAMPLE_RATE_TIMER, &TIM_InitStruct);

    TIM_OC_InitStruct.OCMode = LL_TIM_OCMODE_PWM1;
    TIM_OC_InitStruct.OCState = LL_TIM_OCSTATE_ENABLE;
    TIM_OC_InitStruct.CompareValue = 128;
    LL_TIM_OC_Init(SAMPLE_RATE_TIMER, FURI_HAL_SPEAKER_CHANNEL, &TIM_OC_InitStruct);

    furi_hal_gpio_init_ex(
        &gpio_ext_pa6,
        GpioModeAltFunctionPushPull,
        GpioPullNo,
        GpioSpeedVeryHigh,
        GpioAltFn14TIM16);

    uint32_t dma_dst = (uint32_t) & (FURI_HAL_SPEAKER_TIMER->CCR1);
    LL_DMA_ConfigAddresses(
        DMA_INSTANCE, (uint32_t)usb_audio_ring, dma_dst, LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
    LL_DMA_SetDataLength(DMA_INSTANCE, USB_AUDIO_RING_SIZE);
    LL_DMA_SetPeriphRequest(DMA_INSTANCE, LL_DMAMUX_REQ_TIM2_UP);
    LL_DMA_SetDataTransferDirection(DMA_INSTANCE, LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
    LL_DMA_SetChannelPriorityLevel(DMA_INSTANCE, LL_DMA_PRIORITY_VERYHIGH);
    LL_DMA_SetMode(DMA_INSTANCE, LL_DMA_MODE_CIRCULAR);
    LL_DMA_SetPeriphIncMode(DMA_INSTANCE, LL_DMA_PERIPH_NOINCREMENT);
    LL_DMA_SetMemoryIncMode(DMA_INSTANCE, LL_DMA_MEMORY_INCREMENT);
    LL_DMA_SetPeriphSize(DMA_INSTANCE, LL_DMA_PDATAALIGN_HALFWORD);
    LL_DMA_SetMemorySize(DMA_INSTANCE, LL_DMA_MDATAALIGN_BYTE);
}

void usb_audio_hal_deinit(void) {
    furi_hal_gpio_init(&gpio_ext_pa6, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    furi_hal_bus_disable(FuriHalBusTIM2);
}

void usb_audio_hal_start(void) {
    LL_DMA_EnableChannel(DMA_INSTANCE);
    LL_TIM_EnableDMAReq_UPDATE(SAMPLE_RATE_TIMER);

    LL_TIM_EnableAllOutputs(FURI_HAL_SPEAKER_TIMER);
    LL_TIM_EnableCounter(FURI_HAL_SPEAKER_TIMER);
    LL_TIM_EnableAllOutputs(SAMPLE_RATE_TIMER);
    LL_TIM_EnableCounter(SAMPLE_RATE_TIMER);
}

void usb_audio_hal_stop(void) {
    LL_TIM_DisableAllOutputs(FURI_HAL_SPEAKER_TIMER);
    LL_TIM_DisableCounter(FURI_HAL_SPEAKER_TIMER);
    LL_TIM_DisableAllOutputs(SAMPLE_RATE_TIMER);
    LL_TIM_DisableCounter(SAMPLE_RATE_TIMER);

    LL_DMA_DisableChannel(DMA_INSTANCE);
}

void usb_audio_hal_push(const uint8_t* data, size_t len) {
    // Current DMA playback position: NDTR counts down from RING_SIZE to 0
    // then auto-reloads in circular mode.
    uint32_t remaining = LL_DMA_GetDataLength(DMA_INSTANCE);
    uint32_t read_pos = USB_AUDIO_RING_SIZE - remaining;

    for(size_t i = 0; i < len; i++) {
        uint32_t next_pos = (usb_audio_write_pos + 1) % USB_AUDIO_RING_SIZE;
        // Guard against the write cursor lapping the DMA's read cursor
        // (host sending faster than we consume) - drop the overrun rather
        // than corrupt not-yet-played samples.
        if(next_pos == read_pos) break;
        usb_audio_ring[usb_audio_write_pos] = data[i];
        usb_audio_write_pos = next_pos;
    }
}
