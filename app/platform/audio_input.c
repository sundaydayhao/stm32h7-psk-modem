#include "audio_input.h"

#include <stdlib.h>
#include <string.h>

#define WM8978_SLAVE_ADDRESS 0x34U

#define WM8978_LRC_PORT GPIOB
#define WM8978_LRC_PIN GPIO_PIN_12
#define WM8978_BCLK_PORT GPIOD
#define WM8978_BCLK_PIN GPIO_PIN_3
#define WM8978_ADCDAT_PORT GPIOC
#define WM8978_ADCDAT_PIN GPIO_PIN_2
#define WM8978_DACDAT_PORT GPIOI
#define WM8978_DACDAT_PIN GPIO_PIN_3
#define WM8978_MCLK_PORT GPIOC
#define WM8978_MCLK_PIN GPIO_PIN_6
#define WM8978_I2S_AF GPIO_AF5_SPI2

#define AUDIO_DMA_HALFWORDS (AUDIO_INPUT_DECODE_BLOCK_SAMPLES * AUDIO_INPUT_DECIMATION * 2U)
#define AUDIO_DMA_HALF_COUNT AUDIO_DMA_HALFWORDS
#define AUDIO_DMA_WORDS (AUDIO_DMA_HALFWORDS * 2U)
#define AUDIO_BLOCK_QUEUE_LEN 4U

typedef enum {
    IN_PATH_OFF = 0x00,
    MIC_LEFT_ON = 0x01,
    MIC_RIGHT_ON = 0x02,
    LINE_ON = 0x04,
    AUX_ON = 0x08,
    DAC_ON = 0x10,
    ADC_ON = 0x20,
} wm8978_input_path_t;

typedef enum {
    OUT_PATH_OFF = 0x00,
    EAR_LEFT_ON = 0x01,
    EAR_RIGHT_ON = 0x02,
    SPK_ON = 0x04,
    OUT3_4_ON = 0x08,
} wm8978_output_path_t;

static I2C_HandleTypeDef s_i2c;
static I2S_HandleTypeDef s_i2s;
static DMA_HandleTypeDef s_dma_rx;

static int16_t s_dma[AUDIO_DMA_WORDS] __attribute__((aligned(32)));
static int16_t s_blocks[AUDIO_BLOCK_QUEUE_LEN][AUDIO_INPUT_DECODE_BLOCK_SAMPLES];

static volatile uint32_t s_half_callbacks;
static volatile uint32_t s_full_callbacks;
static volatile uint32_t s_error_callbacks;
static volatile uint32_t s_read_blocks;
static volatile uint32_t s_empty_reads;
static volatile uint32_t s_overflow_blocks;
static volatile uint32_t s_queued_blocks;
static volatile uint32_t s_max_queued_blocks;
static volatile uint32_t s_head;
static volatile uint32_t s_tail;
static volatile uint8_t s_started;

static audio_input_debug_t s_debug;

static uint16_t s_wm_regs[] = {
    0x000, 0x000, 0x000, 0x000, 0x050, 0x000, 0x140, 0x000,
    0x000, 0x000, 0x000, 0x0FF, 0x0FF, 0x000, 0x100, 0x0FF,
    0x0FF, 0x000, 0x12C, 0x02C, 0x02C, 0x02C, 0x02C, 0x000,
    0x032, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x038, 0x00B, 0x032, 0x000, 0x008, 0x00C, 0x093, 0x0E9,
    0x000, 0x000, 0x000, 0x000, 0x003, 0x010, 0x010, 0x100,
    0x100, 0x002, 0x001, 0x001, 0x039, 0x039, 0x039, 0x039,
    0x001, 0x001
};

static HAL_StatusTypeDef configure_audio_clock(void);
static HAL_StatusTypeDef configure_i2c(void);
static HAL_StatusTypeDef configure_i2s(void);
static uint8_t wm8978_write(uint8_t reg, uint16_t value);
static uint16_t wm8978_read(uint8_t reg);
static uint8_t wm8978_reset(void);
static uint8_t wm8978_init(void);
static void wm8978_cfg_audio_if(void);
static void wm8978_cfg_audio_path(uint16_t in_path, uint16_t out_path);
static void wm8978_set_mic_gain(uint8_t gain);
static void wm8978_set_line_gain(uint8_t gain);
static void wm8978_gpio1(uint8_t value);
static void process_dma_half(uint32_t offset, uint8_t is_full);
static void invalidate_dma_cache(const void *addr, size_t bytes);

HAL_StatusTypeDef audio_input_start(void)
{
    if (s_started) {
        return HAL_OK;
    }

    memset((void *)&s_debug, 0, sizeof(s_debug));
    memset(s_dma, 0, sizeof(s_dma));

    if (configure_audio_clock() != HAL_OK) {
        return HAL_ERROR;
    }
    if (configure_i2c() != HAL_OK) {
        return HAL_ERROR;
    }
    if (!wm8978_init()) {
        s_debug.sai_error = 0x80000001UL;
        return HAL_ERROR;
    }

    wm8978_cfg_audio_if();
    wm8978_cfg_audio_path(LINE_ON | ADC_ON, OUT_PATH_OFF);
    wm8978_set_mic_gain(0U);
    wm8978_set_line_gain(6U);

    if (configure_i2s() != HAL_OK) {
        return HAL_ERROR;
    }
    if (HAL_I2S_Receive_DMA(&s_i2s, (uint16_t *)s_dma, AUDIO_DMA_WORDS) != HAL_OK) {
        s_debug.sai_error = s_i2s.ErrorCode;
        return HAL_ERROR;
    }

    s_started = 1U;
    return HAL_OK;
}

size_t audio_input_read(int16_t *out, size_t max_samples)
{
    if (!out || max_samples < AUDIO_INPUT_DECODE_BLOCK_SAMPLES) {
        ++s_empty_reads;
        return 0U;
    }

    __disable_irq();
    if (s_queued_blocks == 0U) {
        __enable_irq();
        ++s_empty_reads;
        return 0U;
    }

    memcpy(out, s_blocks[s_tail], sizeof(s_blocks[s_tail]));
    s_tail = (s_tail + 1U) % AUDIO_BLOCK_QUEUE_LEN;
    --s_queued_blocks;
    __enable_irq();

    ++s_read_blocks;
    return AUDIO_INPUT_DECODE_BLOCK_SAMPLES;
}

void audio_input_debug_snapshot(audio_input_debug_t *debug)
{
    if (!debug) {
        return;
    }

    __disable_irq();
    *debug = s_debug;
    debug->half_callbacks = s_half_callbacks;
    debug->full_callbacks = s_full_callbacks;
    debug->error_callbacks = s_error_callbacks;
    debug->read_blocks = s_read_blocks;
    debug->empty_reads = s_empty_reads;
    debug->overflow_blocks = s_overflow_blocks;
    debug->queued_blocks = s_queued_blocks;
    debug->max_queued_blocks = s_max_queued_blocks;
    debug->sai_state = (uint32_t)s_i2s.State;
    debug->dma_state = (uint32_t)s_dma_rx.State;
    debug->sai_error = s_i2s.ErrorCode;
    debug->dma_error = s_dma_rx.ErrorCode;
    if (s_dma_rx.Instance) {
        DMA_Stream_TypeDef *stream = (DMA_Stream_TypeDef *)s_dma_rx.Instance;
        debug->dma_cr = stream->CR;
        debug->dma_ndtr = stream->NDTR;
    } else {
        debug->dma_cr = 0U;
        debug->dma_ndtr = 0U;
    }
    __enable_irq();
}

void audio_input_dma_irq_handler(void)
{
    HAL_DMA_IRQHandler(&s_dma_rx);
}

void HAL_I2S_MspInit(I2S_HandleTypeDef *hi2s)
{
    GPIO_InitTypeDef gpio = {0};

    if (hi2s->Instance != SPI2) {
        return;
    }

    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = WM8978_I2S_AF;

    gpio.Pin = WM8978_LRC_PIN;
    HAL_GPIO_Init(WM8978_LRC_PORT, &gpio);
    gpio.Pin = WM8978_BCLK_PIN;
    HAL_GPIO_Init(WM8978_BCLK_PORT, &gpio);
    gpio.Pin = WM8978_ADCDAT_PIN;
    HAL_GPIO_Init(WM8978_ADCDAT_PORT, &gpio);
    gpio.Pin = WM8978_DACDAT_PIN;
    HAL_GPIO_Init(WM8978_DACDAT_PORT, &gpio);
    gpio.Pin = WM8978_MCLK_PIN;
    HAL_GPIO_Init(WM8978_MCLK_PORT, &gpio);

    s_dma_rx.Instance = DMA1_Stream0;
    s_dma_rx.Init.Request = DMA_REQUEST_SPI2_RX;
    s_dma_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    s_dma_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    s_dma_rx.Init.MemInc = DMA_MINC_ENABLE;
    s_dma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    s_dma_rx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    s_dma_rx.Init.Mode = DMA_CIRCULAR;
    s_dma_rx.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    s_dma_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;

    if (HAL_DMA_Init(&s_dma_rx) != HAL_OK) {
        s_debug.dma_error = 0x80000002UL;
        return;
    }

    __HAL_LINKDMA(hi2s, hdmarx, s_dma_rx);
    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
}

void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s == &s_i2s) {
        ++s_half_callbacks;
        process_dma_half(0U, 0U);
    }
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s == &s_i2s) {
        ++s_full_callbacks;
        process_dma_half(AUDIO_DMA_HALFWORDS, 1U);
    }
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s)
{
    if (hi2s == &s_i2s) {
        ++s_error_callbacks;
        s_debug.sai_error = hi2s->ErrorCode;
        s_debug.dma_error = s_dma_rx.ErrorCode;
    }
}

static HAL_StatusTypeDef configure_audio_clock(void)
{
    RCC_PeriphCLKInitTypeDef clk = {0};

    clk.PeriphClockSelection = RCC_PERIPHCLK_SPI2 | RCC_PERIPHCLK_I2C1;
    clk.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL;
    clk.I2c123ClockSelection = RCC_I2C123CLKSOURCE_D2PCLK1;

    return HAL_RCCEx_PeriphCLKConfig(&clk);
}

static HAL_StatusTypeDef configure_i2c(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_I2C1_FORCE_RESET();
    __HAL_RCC_I2C1_RELEASE_RESET();

    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);

    s_i2c.Instance = I2C1;
    s_i2c.Init.Timing = 0x40604E73U;
    s_i2c.Init.OwnAddress1 = 0U;
    s_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_i2c.Init.OwnAddress2 = 0U;
    s_i2c.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    s_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&s_i2c) != HAL_OK) {
        return HAL_ERROR;
    }
    if (HAL_I2CEx_ConfigAnalogFilter(&s_i2c, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
        return HAL_ERROR;
    }
    return HAL_OK;
}

static HAL_StatusTypeDef configure_i2s(void)
{
    __HAL_I2S_RESET_HANDLE_STATE(&s_i2s);
    s_i2s.Instance = SPI2;
    s_i2s.Init.Mode = I2S_MODE_MASTER_RX;
    s_i2s.Init.Standard = I2S_STANDARD_PHILIPS;
    s_i2s.Init.DataFormat = I2S_DATAFORMAT_16B;
    s_i2s.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
    s_i2s.Init.AudioFreq = AUDIO_INPUT_CODEC_RATE_HZ;
    s_i2s.Init.CPOL = I2S_CPOL_LOW;
    s_i2s.Init.FirstBit = I2S_FIRSTBIT_MSB;
    s_i2s.Init.WSInversion = I2S_WS_INVERSION_DISABLE;
    s_i2s.Init.Data24BitAlignment = I2S_DATA_24BIT_ALIGNMENT_RIGHT;
    s_i2s.Init.MasterKeepIOState = I2S_MASTER_KEEP_IO_STATE_DISABLE;

    return HAL_I2S_Init(&s_i2s);
}

static uint8_t wm8978_write(uint8_t reg, uint16_t value)
{
    uint8_t data[2];

    data[0] = (uint8_t)(((reg << 1U) & 0xFEU) | ((value >> 8U) & 0x01U));
    data[1] = (uint8_t)(value & 0xFFU);

    if (HAL_I2C_Master_Transmit(&s_i2c, WM8978_SLAVE_ADDRESS, data, sizeof(data), 100U) != HAL_OK) {
        return 0U;
    }
    if (reg < (sizeof(s_wm_regs) / sizeof(s_wm_regs[0]))) {
        s_wm_regs[reg] = value;
    }
    return 1U;
}

static uint16_t wm8978_read(uint8_t reg)
{
    if (reg >= (sizeof(s_wm_regs) / sizeof(s_wm_regs[0]))) {
        return 0U;
    }
    return s_wm_regs[reg];
}

static uint8_t wm8978_reset(void)
{
    static const uint16_t defaults[] = {
        0x000, 0x000, 0x000, 0x000, 0x050, 0x000, 0x140, 0x000,
        0x000, 0x000, 0x000, 0x0FF, 0x0FF, 0x000, 0x100, 0x0FF,
        0x0FF, 0x000, 0x12C, 0x02C, 0x02C, 0x02C, 0x02C, 0x000,
        0x032, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
        0x038, 0x00B, 0x032, 0x000, 0x008, 0x00C, 0x093, 0x0E9,
        0x000, 0x000, 0x000, 0x000, 0x003, 0x010, 0x010, 0x100,
        0x100, 0x002, 0x001, 0x001, 0x039, 0x039, 0x039, 0x039,
        0x001, 0x001
    };

    if (!wm8978_write(0U, 0U)) {
        return 0U;
    }
    memcpy(s_wm_regs, defaults, sizeof(defaults));
    HAL_Delay(10U);
    return 1U;
}

static uint8_t wm8978_init(void)
{
    if (!wm8978_reset()) {
        return 0U;
    }
    wm8978_gpio1(0U);
    return 1U;
}

static void wm8978_cfg_audio_if(void)
{
    wm8978_write(4U, (2U << 3U));
    wm8978_write(6U, 0x000U);
}

static void wm8978_cfg_audio_path(uint16_t in_path, uint16_t out_path)
{
    uint16_t reg;

    if (in_path == IN_PATH_OFF && out_path == OUT_PATH_OFF) {
        wm8978_reset();
        return;
    }

    reg = (1U << 3U) | (3U << 0U);
    if (out_path & OUT3_4_ON) {
        reg |= (1U << 7U) | (1U << 6U);
    }
    if (in_path & (MIC_LEFT_ON | MIC_RIGHT_ON)) {
        reg |= (1U << 4U);
    }
    wm8978_write(1U, reg);

    reg = 0U;
    if (out_path & EAR_LEFT_ON) {
        reg |= (1U << 7U);
    }
    if (out_path & EAR_RIGHT_ON) {
        reg |= (1U << 8U);
    }
    if (in_path & MIC_LEFT_ON) {
        reg |= (1U << 4U) | (1U << 2U);
    }
    if (in_path & MIC_RIGHT_ON) {
        reg |= (1U << 5U) | (1U << 3U);
    }
    if (in_path & LINE_ON) {
        reg |= (1U << 4U) | (1U << 5U);
    }
    if (in_path & ADC_ON) {
        reg |= (1U << 1U) | (1U << 0U);
    }
    wm8978_write(2U, reg);

    reg = 0U;
    if (out_path & OUT3_4_ON) {
        reg |= (1U << 8U) | (1U << 7U);
    }
    if (out_path & SPK_ON) {
        reg |= (1U << 6U) | (1U << 5U);
    }
    if (out_path != OUT_PATH_OFF) {
        reg |= (1U << 3U) | (1U << 2U);
    }
    if (in_path & DAC_ON) {
        reg |= (1U << 1U) | (1U << 0U);
    }
    wm8978_write(3U, reg);

    reg = 0U;
    if (in_path & LINE_ON) {
        reg |= (1U << 6U) | (1U << 2U);
    }
    if (in_path & MIC_RIGHT_ON) {
        reg |= (1U << 5U) | (1U << 4U);
    }
    if (in_path & MIC_LEFT_ON) {
        reg |= (1U << 1U) | (1U << 0U);
    }
    wm8978_write(44U, reg);

    wm8978_write(14U, (in_path & ADC_ON) ? ((1U << 3U) | (4U << 0U)) : 0U);
    wm8978_write(27U, 0U);
    wm8978_write(28U, 0U);
    wm8978_write(29U, 0U);
    wm8978_write(30U, 0U);
    wm8978_write(32U, 0U);
    wm8978_write(33U, 0U);
    wm8978_write(34U, 0U);
    wm8978_write(35U, (3U << 1U) | (7U << 0U));

    reg = 0U;
    if (in_path & (MIC_LEFT_ON | MIC_RIGHT_ON)) {
        reg |= (1U << 8U);
    }
    if (in_path & AUX_ON) {
        reg |= (3U << 0U);
    }
    if (in_path & LINE_ON) {
        reg |= (3U << 4U);
    }
    wm8978_write(47U, reg);
    wm8978_write(48U, reg);
    wm8978_write(15U, 0x0FFU);
    wm8978_write(16U, 0x1FFU);
}

static void wm8978_set_mic_gain(uint8_t gain)
{
    if (gain > 63U) {
        gain = 63U;
    }
    wm8978_write(45U, gain);
    wm8978_write(46U, gain | (1U << 8U));
}

static void wm8978_set_line_gain(uint8_t gain)
{
    uint16_t reg;

    if (gain > 7U) {
        gain = 7U;
    }

    reg = wm8978_read(47U);
    reg &= 0x08FU;
    reg |= (uint16_t)gain << 4U;
    wm8978_write(47U, reg);

    reg = wm8978_read(48U);
    reg &= 0x08FU;
    reg |= (uint16_t)gain << 4U;
    wm8978_write(48U, reg);
}

static void wm8978_gpio1(uint8_t value)
{
    wm8978_write(8U, value ? 7U : 6U);
}

static void process_dma_half(uint32_t offset, uint8_t is_full)
{
    int32_t sum_abs[2] = {0, 0};
    int16_t min_raw[2] = {INT16_MAX, INT16_MAX};
    int16_t max_raw[2] = {INT16_MIN, INT16_MIN};
    int16_t lane_block[2][AUDIO_INPUT_DECODE_BLOCK_SAMPLES];
    int16_t block[AUDIO_INPUT_DECODE_BLOCK_SAMPLES];

    invalidate_dma_cache(&s_dma[offset], AUDIO_DMA_HALF_COUNT * sizeof(s_dma[0]));

    for (size_t i = 0; i < AUDIO_INPUT_DECODE_BLOCK_SAMPLES; ++i) {
        const size_t base = offset + (i * AUDIO_INPUT_DECIMATION * 2U);
        int32_t acc[2] = {0, 0};

        for (size_t j = 0; j < AUDIO_INPUT_DECIMATION; ++j) {
            const int16_t l = s_dma[base + j * 2U];
            const int16_t r = s_dma[base + j * 2U + 1U];

            acc[0] += l;
            acc[1] += r;
            sum_abs[0] += abs((int)l);
            sum_abs[1] += abs((int)r);
            if (l < min_raw[0]) {
                min_raw[0] = l;
            }
            if (l > max_raw[0]) {
                max_raw[0] = l;
            }
            if (r < min_raw[1]) {
                min_raw[1] = r;
            }
            if (r > max_raw[1]) {
                max_raw[1] = r;
            }
        }

        lane_block[0][i] = (int16_t)(acc[0] / (int32_t)AUDIO_INPUT_DECIMATION);
        lane_block[1][i] = (int16_t)(acc[1] / (int32_t)AUDIO_INPUT_DECIMATION);
    }

    const uint8_t selected_lane = (sum_abs[1] > sum_abs[0]) ? 1U : 0U;
    memcpy(block, lane_block[selected_lane], sizeof(block));
    int16_t last_min = INT16_MAX;
    int16_t last_max = INT16_MIN;
    uint32_t last_abs = 0U;
    for (size_t i = 0; i < AUDIO_INPUT_DECODE_BLOCK_SAMPLES; ++i) {
        if (block[i] < last_min) {
            last_min = block[i];
        }
        if (block[i] > last_max) {
            last_max = block[i];
        }
        last_abs += (uint32_t)abs((int)block[i]);
    }

    if (s_queued_blocks >= AUDIO_BLOCK_QUEUE_LEN) {
        ++s_overflow_blocks;
    } else {
        memcpy(s_blocks[s_head], block, sizeof(block));
        s_head = (s_head + 1U) % AUDIO_BLOCK_QUEUE_LEN;
        ++s_queued_blocks;
        if (s_queued_blocks > s_max_queued_blocks) {
            s_max_queued_blocks = s_queued_blocks;
        }
    }

    s_debug.last_count = AUDIO_INPUT_DECODE_BLOCK_SAMPLES;
    s_debug.last_min = last_min;
    s_debug.last_max = last_max;
    s_debug.last_abs_avg = (uint16_t)(last_abs / AUDIO_INPUT_DECODE_BLOCK_SAMPLES);
    s_debug.selected_lane = selected_lane;
    s_debug.raw_min[0] = min_raw[0];
    s_debug.raw_min[1] = min_raw[1];
    s_debug.raw_max[0] = max_raw[0];
    s_debug.raw_max[1] = max_raw[1];
    s_debug.raw_abs_avg[0] = (uint16_t)(sum_abs[0] / (AUDIO_INPUT_DECODE_BLOCK_SAMPLES * AUDIO_INPUT_DECIMATION));
    s_debug.raw_abs_avg[1] = (uint16_t)(sum_abs[1] / (AUDIO_INPUT_DECODE_BLOCK_SAMPLES * AUDIO_INPUT_DECIMATION));
    s_debug.selected_lane = selected_lane;
    (void)is_full;
}

static void invalidate_dma_cache(const void *addr, size_t bytes)
{
    const uintptr_t start = (uintptr_t)addr & ~(uintptr_t)31U;
    const uintptr_t end = ((uintptr_t)addr + bytes + 31U) & ~(uintptr_t)31U;
    SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}
