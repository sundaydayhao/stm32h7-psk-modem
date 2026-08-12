#include "main.h"

#include <stdio.h>
#include <string.h>

#include "audio_input.h"
#include "display.h"
#include "psk_modem.h"
#include "spectrum.h"
#include "touch_gt911.h"

UART_HandleTypeDef huart1;

static psk_modem_t g_modem;
static volatile uint8_t g_trace_ready;

void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void print_rx_text(const char *tag, const char *text, size_t len);

int main(void)
{
    MPU_Config();
    SCB_EnableICache();
    SCB_EnableDCache();

    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    printf("\r\n[boot] stm32h743iit6 psk modem\r\n");
    display_init();
    printf("[boot] psk mode table: %lu wav modes, bpsk non-fec decoder active\r\n",
           (unsigned long)psk_mode_count());

    psk_modem_config_t cfg = {
        .mode = PSK_MODE_BPSK31,
        .sample_rate_hz = AUDIO_INPUT_DECODE_RATE_HZ,
        .rx_tone_hz = 1500,
        .squelch = 5,
    };
    psk_modem_init(&g_modem, &cfg);

    if (audio_input_start() != HAL_OK) {
        printf("[warn] audio backend not started\r\n");
    }

    int16_t samples[AUDIO_INPUT_DECODE_BLOCK_SAMPLES];
    char decoded[96];
    char text[1024] = {0};
    spectrum_t spectrum = {0};
    audio_input_debug_t audio_debug;
    psk_modem_debug_t modem_debug;
    touch_gt911_state_t touch_state = {0};
    uint32_t last_diag_tick = 0;
    uint32_t last_render_tick = 0;
    uint32_t last_touch_tick = 0;
    uint32_t last_touch_retry_tick = 0;
    uint8_t touch_ready = 0U;
    uint8_t touch_was_pressed = 0U;

    if (touch_gt911_init() == HAL_OK) {
        touch_gt911_debug_snapshot(&touch_state);
        touch_ready = 1U;
        printf("[touch] gt911 addr=0x%02x id=%s\r\n", touch_state.address >> 1, touch_state.id);
    } else {
        touch_gt911_debug_snapshot(&touch_state);
        printf("[warn] gt911 touch not detected scan=0x%08lx err=0x%08lx\r\n",
               (unsigned long)touch_state.scan_mask,
               (unsigned long)touch_state.last_error);
    }

    while (1) {
        const size_t count = audio_input_read(samples, AUDIO_INPUT_DECODE_BLOCK_SAMPLES);
        const uint32_t now = HAL_GetTick();

        if (!touch_state.present && (uint32_t)(now - last_touch_retry_tick) >= 1000U) {
            last_touch_retry_tick = now;
            if (touch_gt911_init() == HAL_OK) {
                touch_gt911_debug_snapshot(&touch_state);
                touch_ready = 1U;
                printf("[touch] gt911 addr=0x%02x id=%s recovered\r\n",
                       touch_state.address >> 1,
                       touch_state.id);
            } else {
                touch_gt911_debug_snapshot(&touch_state);
            }
        }

        if ((uint32_t)(now - last_touch_tick) >= 30U) {
            last_touch_tick = now;
            if (touch_gt911_read(&touch_state) == HAL_OK) {
                touch_ready = 1U;
                if (touch_state.pressed && !touch_was_pressed) {
                    psk_mode_t selected;
                    if (display_mode_at(touch_state.x, touch_state.y, &selected)) {
                        psk_modem_set_mode(&g_modem, selected);
                        text[0] = '\0';
                        spectrum.peak_hz = 0;
                        printf("[mode] selected %s%s raw=%u,%u map=%u,%u\r\n",
                               psk_modem_mode_name(selected),
                               psk_modem_mode_supported(selected) ? "" : " (display only)",
                               touch_state.raw_x,
                               touch_state.raw_y,
                               touch_state.x,
                               touch_state.y);
                        display_render(&spectrum, psk_modem_mode_info(&g_modem),
                                       psk_modem_tone_hz(&g_modem), text);
                    } else {
                        printf("[touch] down raw=%u,%u map=%u,%u miss st=0x%02x\r\n",
                               touch_state.raw_x,
                               touch_state.raw_y,
                               touch_state.x,
                               touch_state.y,
                               touch_state.last_status);
                    }
                }
                touch_was_pressed = touch_state.pressed;
            } else if (touch_ready) {
                touch_ready = 0U;
                touch_was_pressed = 0U;
            }
        }

        if (count > 0U) {
            const size_t got = psk_modem_process(&g_modem, samples, count, decoded, sizeof(decoded));
            if (got > 0U) {
                print_rx_text(psk_modem_mode_name(psk_modem_mode(&g_modem)), decoded, got);
                const size_t old = strlen(text);
                if (old + got >= sizeof(text)) {
                    memmove(text, text + sizeof(text) / 2U, sizeof(text) / 2U);
                    text[sizeof(text) / 2U] = '\0';
                }
                strncat(text, decoded, sizeof(text) - strlen(text) - 1U);
            }

            if ((uint32_t)(now - last_render_tick) >= 500U) {
                last_render_tick = now;
                spectrum_analyze(samples, count, AUDIO_INPUT_DECODE_RATE_HZ, &spectrum);
                display_render(&spectrum, psk_modem_mode_info(&g_modem),
                               psk_modem_tone_hz(&g_modem), text);
            }
        }

        if ((uint32_t)(now - last_diag_tick) >= 2000U) {
            last_diag_tick = now;
            audio_input_debug_snapshot(&audio_debug);
            psk_modem_debug_snapshot(&g_modem, &modem_debug);
            printf("[diag] mode=%s tone=%dHz blocks=%lu empty=%lu cb=%lu/%lu in=%u lane=%u q=%lu peak=%dHz sq=%d/%d open=%d chars=%lu\r\n",
                   psk_modem_mode_name(psk_modem_mode(&g_modem)),
                   modem_debug.tone_hz,
                   audio_debug.read_blocks,
                   audio_debug.empty_reads,
                   audio_debug.half_callbacks,
                   audio_debug.full_callbacks,
                   audio_debug.last_abs_avg,
                   audio_debug.selected_lane,
                   audio_debug.queued_blocks,
                   spectrum.peak_hz,
                   modem_debug.sq_level,
                   modem_debug.sq_thresh,
                   modem_debug.sq_open,
                   modem_debug.chars);
        }

        display_task();
    }
}

static void print_rx_text(const char *tag, const char *text, size_t len)
{
    printf("[%s] ", tag ? tag : "rx");
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = (unsigned char)text[i];
        if (c >= 32U && c <= 126U) {
            putchar(c);
        } else if (c == '\r') {
            printf("\\r");
        } else if (c == '\n') {
            printf("\\n");
        } else {
            printf("\\x%02X", c);
        }
    }
    printf("\r\n");
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
    }

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 5;
    osc.PLL.PLLN = 160;
    osc.PLL.PLLP = 2;
    osc.PLL.PLLQ = 4;
    osc.PLL.PLLR = 2;
    osc.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
    osc.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    osc.PLL.PLLFRACN = 0;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        Error_Handler();
    }

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1 |
                    RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.SYSCLKDivider = RCC_SYSCLK_DIV1;
    clk.AHBCLKDivider = RCC_HCLK_DIV2;
    clk.APB3CLKDivider = RCC_APB3_DIV2;
    clk.APB1CLKDivider = RCC_APB1_DIV2;
    clk.APB2CLKDivider = RCC_APB2_DIV2;
    clk.APB4CLKDivider = RCC_APB4_DIV2;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
}

static void MX_USART1_UART_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) {
        Error_Handler();
    }
    if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK) {
        Error_Handler();
    }

    g_trace_ready = 1U;
}

static void MPU_Config(void)
{
    MPU_Region_InitTypeDef mpu = {0};

    HAL_MPU_Disable();
    mpu.Enable = MPU_REGION_ENABLE;
    mpu.Number = MPU_REGION_NUMBER0;
    mpu.BaseAddress = 0x24000000;
    mpu.Size = MPU_REGION_SIZE_512KB;
    mpu.AccessPermission = MPU_REGION_FULL_ACCESS;
    mpu.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    mpu.IsCacheable = MPU_ACCESS_CACHEABLE;
    mpu.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    mpu.TypeExtField = MPU_TEX_LEVEL0;
    mpu.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
    HAL_MPU_ConfigRegion(&mpu);

    mpu.Number = MPU_REGION_NUMBER1;
    mpu.BaseAddress = 0xD0000000;
    mpu.Size = MPU_REGION_SIZE_64MB;
    mpu.AccessPermission = MPU_REGION_FULL_ACCESS;
    mpu.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    mpu.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    mpu.IsShareable = MPU_ACCESS_SHAREABLE;
    mpu.TypeExtField = MPU_TEX_LEVEL0;
    mpu.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&mpu);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void Error_Handler(void)
{
    if (g_trace_ready) {
        printf("[fatal] Error_Handler\r\n");
    }
    __disable_irq();
    while (1) {
    }
}

int __io_putchar(int ch)
{
    const uint8_t c = (uint8_t)ch;
    if (!g_trace_ready) {
        return ch;
    }
    if (ch == '\n') {
        const uint8_t cr = '\r';
        HAL_UART_Transmit(&huart1, (uint8_t *)&cr, 1, 10);
    }
    HAL_UART_Transmit(&huart1, (uint8_t *)&c, 1, 10);
    return ch;
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    Error_Handler();
}
#endif
