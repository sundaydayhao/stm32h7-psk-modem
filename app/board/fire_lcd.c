#include "fire_lcd.h"

#include <string.h>

#define SDRAM_BANK_ADDR 0xD0000000UL
#define LCD_FB_ADDR 0xD0000000UL
#define SDRAM_TIMEOUT 0xFFFFUL

#define LCD_HBP 46U
#define LCD_VBP 23U
#define LCD_HSW 1U
#define LCD_VSW 1U
#define LCD_HFP 210U
#define LCD_VFP 22U

static SDRAM_HandleTypeDef hsdram1;
static LTDC_HandleTypeDef hltdc;
volatile uint32_t g_fire_lcd_debug_stage;
volatile uint32_t g_fire_lcd_debug_error;

static uint8_t *const fb = (uint8_t *)LCD_FB_ADDR;

static void gpio_init_af(GPIO_TypeDef *port, uint32_t pins, uint32_t af);
static void gpio_init_output(GPIO_TypeDef *port, uint32_t pins, GPIO_PinState state);
static void sdram_gpio_init(void);
static HAL_StatusTypeDef sdram_clock_init(void);
static HAL_StatusTypeDef sdram_init(void);
static HAL_StatusTypeDef sdram_command_sequence(void);
static void lcd_backlight_init(GPIO_PinState state);
static void ltdc_gpio_init(void);
static HAL_StatusTypeDef ltdc_clock_init(void);
static HAL_StatusTypeDef ltdc_init(void);
static void lcd_startup_delay(void);
static void write_pixel(uint32_t index, uint32_t color);
static void draw_char(uint32_t x, uint32_t y, char ch, uint32_t color, uint32_t bg, uint32_t scale);
static uint8_t glyph_row(char ch, uint32_t row);

HAL_StatusTypeDef fire_lcd_init(void)
{
    g_fire_lcd_debug_stage = 1U;
    g_fire_lcd_debug_error = 0U;
    lcd_backlight_init(GPIO_PIN_SET);
    lcd_startup_delay();
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_RESET);
    lcd_startup_delay();
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_SET);
    lcd_startup_delay();

    g_fire_lcd_debug_stage = 2U;
    if (sdram_init() != HAL_OK) {
        g_fire_lcd_debug_error = 1U;
        return HAL_ERROR;
    }
    g_fire_lcd_debug_stage = 3U;
    if (ltdc_init() != HAL_OK) {
        g_fire_lcd_debug_error = 2U;
        return HAL_ERROR;
    }

    g_fire_lcd_debug_stage = 4U;
    fire_lcd_clear(FIRE_LCD_COLOR_BLACK);
    __HAL_LTDC_ENABLE(&hltdc);
    g_fire_lcd_debug_stage = 5U;
    return HAL_OK;
}

void fire_lcd_clear(uint32_t color)
{
    for (uint32_t i = 0; i < FIRE_LCD_WIDTH * FIRE_LCD_HEIGHT; ++i) {
        write_pixel(i, color);
    }
}

void fire_lcd_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (x >= FIRE_LCD_WIDTH || y >= FIRE_LCD_HEIGHT) {
        return;
    }
    if (x + w > FIRE_LCD_WIDTH) {
        w = FIRE_LCD_WIDTH - x;
    }
    if (y + h > FIRE_LCD_HEIGHT) {
        h = FIRE_LCD_HEIGHT - y;
    }
    for (uint32_t row = 0; row < h; ++row) {
        const uint32_t base = (y + row) * FIRE_LCD_WIDTH + x;
        for (uint32_t col = 0; col < w; ++col) {
            write_pixel(base + col, color);
        }
    }
}

void fire_lcd_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (w == 0U || h == 0U) {
        return;
    }
    fire_lcd_draw_hline(x, y, w, color);
    fire_lcd_draw_hline(x, y + h - 1U, w, color);
    fire_lcd_fill_rect(x, y, 1U, h, color);
    fire_lcd_fill_rect(x + w - 1U, y, 1U, h, color);
}

void fire_lcd_draw_hline(uint32_t x, uint32_t y, uint32_t w, uint32_t color)
{
    fire_lcd_fill_rect(x, y, w, 1U, color);
}

void fire_lcd_draw_text(uint32_t x, uint32_t y, const char *text, uint32_t color, uint32_t bg)
{
    fire_lcd_draw_text_scale(x, y, text, color, bg, 2U);
}

void fire_lcd_draw_text_scale(uint32_t x, uint32_t y, const char *text, uint32_t color, uint32_t bg, uint32_t scale)
{
    if (scale == 0U) {
        scale = 1U;
    }
    while (text && *text) {
        if (*text == '\n') {
            y += 8U * scale;
            x = 0U;
        } else {
            draw_char(x, y, *text, color, bg, scale);
            x += 6U * scale;
        }
        ++text;
        if (x + 5U * scale >= FIRE_LCD_WIDTH) {
            break;
        }
    }
}

static void gpio_init_af(GPIO_TypeDef *port, uint32_t pins, uint32_t af)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = pins;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = af;
    HAL_GPIO_Init(port, &gpio);
}

static void gpio_init_output(GPIO_TypeDef *port, uint32_t pins, GPIO_PinState state)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = pins;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(port, &gpio);
    HAL_GPIO_WritePin(port, pins, state);
}

static void sdram_gpio_init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    gpio_init_af(GPIOF, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                        GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 |
                        GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15, GPIO_AF12_FMC);
    gpio_init_af(GPIOG, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 |
                        GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15, GPIO_AF12_FMC);
    gpio_init_af(GPIOD, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 |
                        GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15, GPIO_AF12_FMC);
    gpio_init_af(GPIOE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 |
                        GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
                        GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15, GPIO_AF12_FMC);
    gpio_init_af(GPIOH, GPIO_PIN_6 | GPIO_PIN_7, GPIO_AF12_FMC);
    gpio_init_af(GPIOC, GPIO_PIN_0, GPIO_AF12_FMC);
}

static HAL_StatusTypeDef sdram_clock_init(void)
{
    RCC_PeriphCLKInitTypeDef clk = {0};
    clk.PeriphClockSelection = RCC_PERIPHCLK_FMC;
    clk.PLL2.PLL2M = 25;
    clk.PLL2.PLL2N = 240;
    clk.PLL2.PLL2P = 2;
    clk.PLL2.PLL2Q = 2;
    clk.PLL2.PLL2R = 1;
    clk.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_2;
    clk.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
    clk.PLL2.PLL2FRACN = 0;
    clk.FmcClockSelection = RCC_FMCCLKSOURCE_PLL2;
    return HAL_RCCEx_PeriphCLKConfig(&clk);
}

static HAL_StatusTypeDef sdram_init(void)
{
    FMC_SDRAM_TimingTypeDef timing = {0};

    sdram_gpio_init();
    if (sdram_clock_init() != HAL_OK) {
        return HAL_ERROR;
    }
    __HAL_RCC_FMC_CLK_ENABLE();

    hsdram1.Instance = FMC_SDRAM_DEVICE;
    hsdram1.Init.SDBank = FMC_SDRAM_BANK2;
    hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_9;
    hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
    hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
    hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
    hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
    hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
    hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1;

    timing.LoadToActiveDelay = 2;
    timing.ExitSelfRefreshDelay = 8;
    timing.SelfRefreshTime = 5;
    timing.RowCycleDelay = 7;
    timing.WriteRecoveryTime = 2;
    timing.RPDelay = 2;
    timing.RCDDelay = 2;

    if (HAL_SDRAM_Init(&hsdram1, &timing) != HAL_OK) {
        return HAL_ERROR;
    }
    return sdram_command_sequence();
}

static HAL_StatusTypeDef sdram_command_sequence(void)
{
    FMC_SDRAM_CommandTypeDef cmd = {0};
    uint32_t mode;

    cmd.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    cmd.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
    cmd.AutoRefreshNumber = 1;
    if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, SDRAM_TIMEOUT) != HAL_OK) {
        return HAL_ERROR;
    }
    HAL_Delay(1);

    cmd.CommandMode = FMC_SDRAM_CMD_PALL;
    if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, SDRAM_TIMEOUT) != HAL_OK) {
        return HAL_ERROR;
    }

    cmd.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    cmd.AutoRefreshNumber = 8;
    if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, SDRAM_TIMEOUT) != HAL_OK) {
        return HAL_ERROR;
    }

    mode = 0x0000U | 0x0030U | 0x0200U;
    cmd.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    cmd.AutoRefreshNumber = 1;
    cmd.ModeRegisterDefinition = mode;
    if (HAL_SDRAM_SendCommand(&hsdram1, &cmd, SDRAM_TIMEOUT) != HAL_OK) {
        return HAL_ERROR;
    }

    return HAL_SDRAM_ProgramRefreshRate(&hsdram1, 1855U);
}

static void lcd_backlight_init(GPIO_PinState state)
{
    __HAL_RCC_GPIOD_CLK_ENABLE();

    gpio_init_output(GPIOD, GPIO_PIN_4, GPIO_PIN_SET);
    gpio_init_output(GPIOD, GPIO_PIN_7, state);
}

static void ltdc_gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    gpio_init_af(GPIOH, GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_8 | GPIO_PIN_13 |
                        GPIO_PIN_15, GPIO_AF14_LTDC);
    gpio_init_af(GPIOB, GPIO_PIN_0 | GPIO_PIN_1, GPIO_AF9_LTDC);
    gpio_init_af(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_AF14_LTDC);
    gpio_init_af(GPIOA, GPIO_PIN_3 | GPIO_PIN_11 | GPIO_PIN_12, GPIO_AF14_LTDC);
    gpio_init_af(GPIOG, GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_12, GPIO_AF14_LTDC);
    gpio_init_af(GPIOG, GPIO_PIN_10, GPIO_AF9_LTDC);
    gpio_init_af(GPIOG, GPIO_PIN_11, GPIO_AF14_LTDC);
    gpio_init_af(GPIOE, GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6, GPIO_AF14_LTDC);
    gpio_init_af(GPIOI, GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_9 |
                        GPIO_PIN_10, GPIO_AF14_LTDC);
    gpio_init_af(GPIOC, GPIO_PIN_7, GPIO_AF14_LTDC);
    gpio_init_af(GPIOD, GPIO_PIN_6, GPIO_AF14_LTDC);
    gpio_init_af(GPIOF, GPIO_PIN_10, GPIO_AF14_LTDC);

    gpio_init_output(GPIOD, GPIO_PIN_4 | GPIO_PIN_7, GPIO_PIN_SET);
}

static HAL_StatusTypeDef ltdc_clock_init(void)
{
    RCC_PeriphCLKInitTypeDef clk = {0};
    clk.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    clk.PLL3.PLL3M = 25;
    clk.PLL3.PLL3N = 330;
    clk.PLL3.PLL3P = 2;
    clk.PLL3.PLL3Q = 2;
    clk.PLL3.PLL3R = 10;
    clk.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_0;
    clk.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
    clk.PLL3.PLL3FRACN = 0;
    return HAL_RCCEx_PeriphCLKConfig(&clk);
}

static HAL_StatusTypeDef ltdc_init(void)
{
    LTDC_LayerCfgTypeDef layer = {0};

    __HAL_RCC_LTDC_CLK_ENABLE();
    if (ltdc_clock_init() != HAL_OK) {
        return HAL_ERROR;
    }
    ltdc_gpio_init();

    hltdc.Instance = LTDC;
    hltdc.Init.HorizontalSync = LCD_HSW - 1U;
    hltdc.Init.VerticalSync = LCD_VSW - 1U;
    hltdc.Init.AccumulatedHBP = LCD_HSW + LCD_HBP - 1U;
    hltdc.Init.AccumulatedVBP = LCD_VSW + LCD_VBP - 1U;
    hltdc.Init.AccumulatedActiveW = LCD_HSW + LCD_HBP + FIRE_LCD_WIDTH - 1U;
    hltdc.Init.AccumulatedActiveH = LCD_VSW + LCD_VBP + FIRE_LCD_HEIGHT - 1U;
    hltdc.Init.TotalWidth = LCD_HSW + LCD_HBP + FIRE_LCD_WIDTH + LCD_HFP - 1U;
    hltdc.Init.TotalHeigh = LCD_VSW + LCD_VBP + FIRE_LCD_HEIGHT + LCD_VFP - 1U;
    hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
    hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
    hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
    hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
    hltdc.Init.Backcolor.Red = 0;
    hltdc.Init.Backcolor.Green = 0;
    hltdc.Init.Backcolor.Blue = 0;

    if (HAL_LTDC_Init(&hltdc) != HAL_OK) {
        return HAL_ERROR;
    }

    layer.WindowX0 = 0;
    layer.WindowX1 = FIRE_LCD_WIDTH;
    layer.WindowY0 = 0;
    layer.WindowY1 = FIRE_LCD_HEIGHT;
    layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB888;
    layer.FBStartAdress = LCD_FB_ADDR;
    layer.Alpha = 255;
    layer.Alpha0 = 0;
    layer.Backcolor.Red = 0;
    layer.Backcolor.Green = 0;
    layer.Backcolor.Blue = 0;
    layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
    layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
    layer.ImageWidth = FIRE_LCD_WIDTH;
    layer.ImageHeight = FIRE_LCD_HEIGHT;

    if (HAL_LTDC_ConfigLayer(&hltdc, &layer, 0) != HAL_OK) {
        return HAL_ERROR;
    }
    __HAL_LTDC_RELOAD_CONFIG(&hltdc);
    return HAL_OK;
}

static void lcd_startup_delay(void)
{
    for (volatile uint32_t i = 0; i < 200000U; ++i) {
        __NOP();
    }
}

static void write_pixel(uint32_t index, uint32_t color)
{
    uint8_t *dst = &fb[index * 3U];
    dst[0] = (uint8_t)(color & 0xFFU);
    dst[1] = (uint8_t)((color >> 8) & 0xFFU);
    dst[2] = (uint8_t)((color >> 16) & 0xFFU);
}

static void draw_char(uint32_t x, uint32_t y, char ch, uint32_t color, uint32_t bg, uint32_t scale)
{
    for (uint32_t row = 0; row < 7U; ++row) {
        uint8_t bits = glyph_row(ch, row);
        for (uint32_t col = 0; col < 5U; ++col) {
            uint32_t px = (bits & (1U << (4U - col))) ? color : bg;
            fire_lcd_fill_rect(x + col * scale, y + row * scale, scale, scale, px);
        }
    }
}

static uint8_t glyph_row(char ch, uint32_t row)
{
    static const uint8_t digits[10][7] = {
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
        {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
        {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
        {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
        {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
        {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
        {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    };
    static const uint8_t letters[26][7] = {
        {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
        {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
        {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
        {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
        {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
        {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
        {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
        {0x07,0x02,0x02,0x02,0x12,0x12,0x0C},
        {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
        {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
        {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
        {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
        {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
        {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
        {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
        {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
        {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
        {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
        {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
        {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},
        {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
        {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
        {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
        {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    };
    static const uint8_t lower[26][7] = {
        {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
        {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E},
        {0x00,0x00,0x0E,0x10,0x10,0x11,0x0E},
        {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F},
        {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
        {0x06,0x09,0x08,0x1C,0x08,0x08,0x08},
        {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E},
        {0x10,0x10,0x1E,0x11,0x11,0x11,0x11},
        {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
        {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
        {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
        {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
        {0x00,0x00,0x1A,0x15,0x15,0x15,0x15},
        {0x00,0x00,0x1E,0x11,0x11,0x11,0x11},
        {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
        {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
        {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01},
        {0x00,0x00,0x16,0x19,0x10,0x10,0x10},
        {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},
        {0x08,0x08,0x1C,0x08,0x08,0x09,0x06},
        {0x00,0x00,0x11,0x11,0x11,0x13,0x0D},
        {0x00,0x00,0x11,0x11,0x0A,0x0A,0x04},
        {0x00,0x00,0x11,0x15,0x15,0x15,0x0A},
        {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
        {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
        {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},
    };
    uint8_t c = (uint8_t)ch;
    if (c >= 'a' && c <= 'z') {
        return lower[c - 'a'][row];
    }
    if (c >= 'A' && c <= 'Z') {
        return letters[c - 'A'][row];
    }
    if (c >= '0' && c <= '9') {
        return digits[c - '0'][row];
    }
    switch (c) {
    case ' ': return 0x00;
    case ':': return row == 2U || row == 4U ? 0x04 : 0x00;
    case '.': return row == 6U ? 0x04 : 0x00;
    case '-': return row == 3U ? 0x1F : 0x00;
    case '/': return (uint8_t)(0x01U << (6U - row > 4U ? 4U : 6U - row));
    case '_': return row == 6U ? 0x1F : 0x00;
    case '[': return row == 0U || row == 6U ? 0x0E : 0x08;
    case ']': return row == 0U || row == 6U ? 0x0E : 0x02;
    default: return row == 0U || row == 6U ? 0x1F : 0x11;
    }
}
