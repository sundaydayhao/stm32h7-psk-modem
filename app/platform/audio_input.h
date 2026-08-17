#pragma once

#include <stddef.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"

#define AUDIO_INPUT_CODEC_RATE_HZ 48000
#define AUDIO_INPUT_DECODE_RATE_HZ 16000
#define AUDIO_INPUT_DECIMATION (AUDIO_INPUT_CODEC_RATE_HZ / AUDIO_INPUT_DECODE_RATE_HZ)
#define AUDIO_INPUT_DECODE_BLOCK_SAMPLES 128

/*
 * SPI2/I2S is sourced from PLL1Q = 200 MHz. With MCLK enabled the STM32H7 I2S
 * prescaler uses an integer divider, so a 48 kHz request is realized as
 * 200 MHz / (256 * 16) = 48828.125 Hz. Keep the 48k request for HAL mode
 * selection, but tell the PSK decoder the measured post-decimation rate.
 */
#define AUDIO_INPUT_ACTUAL_CODEC_RATE_HZ 48828
#define AUDIO_INPUT_ACTUAL_DECODE_RATE_HZ (AUDIO_INPUT_ACTUAL_CODEC_RATE_HZ / AUDIO_INPUT_DECIMATION)

#if (AUDIO_INPUT_CODEC_RATE_HZ % AUDIO_INPUT_DECODE_RATE_HZ) != 0
#error "AUDIO_INPUT_CODEC_RATE_HZ must be an integer multiple of AUDIO_INPUT_DECODE_RATE_HZ"
#endif

#if AUDIO_INPUT_DECIMATION != 3
#error "The shared audio frontend is currently designed and validated for decimation by 3"
#endif

typedef struct {
    uint32_t half_callbacks;
    uint32_t full_callbacks;
    uint32_t error_callbacks;
    uint32_t read_blocks;
    uint32_t empty_reads;
    uint32_t overflow_blocks;
    uint32_t queued_blocks;
    uint32_t max_queued_blocks;
    uint32_t last_count;
    int16_t last_min;
    int16_t last_max;
    uint16_t last_abs_avg;
    uint8_t selected_lane;
    int16_t raw_min[2];
    int16_t raw_max[2];
    uint16_t raw_abs_avg[2];
    uint32_t sai_state;
    uint32_t dma_state;
    uint32_t sai_error;
    uint32_t dma_error;
    uint32_t dma_cr;
    uint32_t dma_ndtr;
    uint8_t codec_input;
    int8_t forced_lane;
    uint8_t codec_adc_volume;
    uint8_t codec_ana_volume;
    uint8_t auto_gain_enabled;
    uint32_t clip_events;
} audio_input_debug_t;

HAL_StatusTypeDef audio_input_start(void);
size_t audio_input_read(int16_t *out, size_t max_samples);
void audio_input_debug_snapshot(audio_input_debug_t *debug);
HAL_StatusTypeDef audio_input_set_codec_input(uint8_t input);
HAL_StatusTypeDef audio_input_set_lane(int8_t lane);
HAL_StatusTypeDef audio_input_set_codec_gain(uint8_t adc_volume, uint8_t ana_volume);
HAL_StatusTypeDef audio_input_set_auto_gain(uint8_t enabled);
void audio_input_dma_irq_handler(void);
