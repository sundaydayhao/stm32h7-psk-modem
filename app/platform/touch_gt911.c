#include "touch_gt911.h"

#include <string.h>

#include "fire_lcd.h"

#define GT911_ADDR_5D (0x5DU << 1)
#define GT911_ADDR_14 (0x14U << 1)

#define GT911_REG_ID 0x8140U
#define GT911_REG_STATUS 0x814EU
#define GT911_REG_POINT1 0x814FU

#ifndef TOUCH_GT911_I2C_TIMING
#define TOUCH_GT911_I2C_TIMING 0x40604E73U
#endif

#ifndef TOUCH_GT911_SWAP_XY
#define TOUCH_GT911_SWAP_XY 0
#endif

#ifndef TOUCH_GT911_INVERT_X
#define TOUCH_GT911_INVERT_X 0
#endif

#ifndef TOUCH_GT911_INVERT_Y
#define TOUCH_GT911_INVERT_Y 0
#endif

#ifndef TOUCH_GT911_X_OFFSET
#define TOUCH_GT911_X_OFFSET 520U
#endif

static I2C_HandleTypeDef s_i2c;
static touch_gt911_state_t s_state;

static HAL_StatusTypeDef configure_i2c2(void);
static HAL_StatusTypeDef read_reg(uint16_t reg, uint8_t *data, uint16_t len);
static HAL_StatusTypeDef write_reg(uint16_t reg, const uint8_t *data, uint16_t len);
static void map_point(uint16_t raw_x, uint16_t raw_y, uint16_t *x_out, uint16_t *y_out);
static void scan_i2c_bus(void);
static HAL_StatusTypeDef probe_address(uint8_t address, uint8_t *id);
static void reset_gt91x(void);

HAL_StatusTypeDef touch_gt911_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    if (configure_i2c2() != HAL_OK) {
        ++s_state.errors;
        s_state.last_error = HAL_I2C_GetError(&s_i2c);
        return HAL_ERROR;
    }
    reset_gt91x();
    scan_i2c_bus();

    uint8_t id[4] = {0};
    if (probe_address(GT911_ADDR_5D, id) != HAL_OK &&
        probe_address(GT911_ADDR_14, id) != HAL_OK) {
        ++s_state.errors;
        s_state.present = 0U;
        s_state.address = 0U;
        return HAL_ERROR;
    }

    for (uint32_t i = 0; i < sizeof(id); ++i) {
        s_state.id[i] = (char)id[i];
    }
    s_state.id[4] = '\0';
    s_state.present = 1U;

    const uint8_t clear = 0U;
    (void)write_reg(GT911_REG_STATUS, &clear, 1U);
    return HAL_OK;
}

HAL_StatusTypeDef touch_gt911_read(touch_gt911_state_t *state)
{
    if (!s_state.present) {
        if (state) {
            *state = s_state;
        }
        return HAL_ERROR;
    }

    uint8_t status = 0;
    if (read_reg(GT911_REG_STATUS, &status, 1U) != HAL_OK) {
        ++s_state.errors;
        s_state.last_error = HAL_I2C_GetError(&s_i2c);
        if (state) {
            *state = s_state;
        }
        return HAL_ERROR;
    }
    ++s_state.reads;
    s_state.last_status = status;

    if ((status & 0x80U) == 0U) {
        s_state.pressed = 0U;
        s_state.points = 0U;
        if (state) {
            *state = s_state;
        }
        return HAL_OK;
    }

    const uint8_t points = status & 0x0FU;
    if (points > 0U) {
        uint8_t point[8] = {0};
        if (read_reg(GT911_REG_POINT1, point, sizeof(point)) == HAL_OK) {
            const uint16_t panel_x = (uint16_t)point[1] | ((uint16_t)point[2] << 8);
            const uint16_t panel_y = (uint16_t)point[3] | ((uint16_t)point[4] << 8);
            uint16_t raw_x = panel_y;
            uint16_t raw_y = panel_x;
            s_state.raw_x = raw_x;
            s_state.raw_y = raw_y;
            map_point(raw_x, raw_y, &s_state.x, &s_state.y);
            s_state.points = points;
            s_state.pressed = 1U;
        } else {
            ++s_state.errors;
            s_state.last_error = HAL_I2C_GetError(&s_i2c);
            s_state.pressed = 0U;
            s_state.points = 0U;
        }
    } else {
        s_state.pressed = 0U;
        s_state.points = 0U;
    }

    const uint8_t clear = 0U;
    (void)write_reg(GT911_REG_STATUS, &clear, 1U);
    if (state) {
        *state = s_state;
    }
    return HAL_OK;
}

void touch_gt911_debug_snapshot(touch_gt911_state_t *state)
{
    if (state) {
        *state = s_state;
    }
}

static HAL_StatusTypeDef configure_i2c2(void)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef clk = {0};

    clk.PeriphClockSelection = RCC_PERIPHCLK_I2C2;
    clk.I2c123ClockSelection = RCC_I2C123CLKSOURCE_D2PCLK1;
    (void)HAL_RCCEx_PeriphCLKConfig(&clk);

    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_I2C2_CLK_ENABLE();
    __HAL_RCC_I2C2_FORCE_RESET();
    __HAL_RCC_I2C2_RELEASE_RESET();

    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(GPIOH, &gpio);

    __HAL_I2C_RESET_HANDLE_STATE(&s_i2c);
    s_i2c.Instance = I2C2;
    s_i2c.Init.Timing = TOUCH_GT911_I2C_TIMING;
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
    return HAL_I2CEx_ConfigAnalogFilter(&s_i2c, I2C_ANALOGFILTER_ENABLE);
}

static HAL_StatusTypeDef read_reg(uint16_t reg, uint8_t *data, uint16_t len)
{
    return HAL_I2C_Mem_Read(&s_i2c, s_state.address, reg, I2C_MEMADD_SIZE_16BIT, data, len, 20U);
}

static HAL_StatusTypeDef write_reg(uint16_t reg, const uint8_t *data, uint16_t len)
{
    return HAL_I2C_Mem_Write(&s_i2c, s_state.address, reg, I2C_MEMADD_SIZE_16BIT, (uint8_t *)data, len, 20U);
}

static void scan_i2c_bus(void)
{
    s_state.scan_mask = 0U;
    const uint8_t addresses[] = {0x14U, 0x1AU, 0x5DU};
    for (uint32_t i = 0; i < sizeof(addresses); ++i) {
        if (HAL_I2C_IsDeviceReady(&s_i2c, (uint16_t)(addresses[i] << 1), 1U, 5U) == HAL_OK) {
            s_state.scan_mask |= (1UL << i);
        }
    }
    s_state.last_error = HAL_I2C_GetError(&s_i2c);
}

static void reset_gt91x(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOD, &gpio);

    gpio.Pin = GPIO_PIN_8;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOI, &gpio);

    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOI, GPIO_PIN_8, GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(GPIOI, GPIO_PIN_8, GPIO_PIN_SET);
    HAL_Delay(60U);

    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOD, &gpio);
    HAL_Delay(50U);
}

static HAL_StatusTypeDef probe_address(uint8_t address, uint8_t *id)
{
    s_state.address = address;
    HAL_StatusTypeDef st = read_reg(GT911_REG_ID, id, 4U);
    if (st != HAL_OK) {
        s_state.last_error = HAL_I2C_GetError(&s_i2c);
    }
    return st;
}

static void map_point(uint16_t raw_x, uint16_t raw_y, uint16_t *x_out, uint16_t *y_out)
{
    uint32_t x = raw_y;
    uint32_t y = raw_x;

#if TOUCH_GT911_SWAP_XY
    const uint32_t t = x;
    x = y;
    y = t;
#endif
#if TOUCH_GT911_INVERT_X
    if (x < FIRE_LCD_WIDTH) {
        x = FIRE_LCD_WIDTH - 1U - x;
    }
#endif
    if (x >= FIRE_LCD_WIDTH) {
        x = FIRE_LCD_WIDTH - 1U;
    }
#if TOUCH_GT911_INVERT_Y
    if (y < FIRE_LCD_HEIGHT) {
        y = FIRE_LCD_HEIGHT - 1U - y;
    }
#endif
    if (y >= FIRE_LCD_HEIGHT) {
        y = FIRE_LCD_HEIGHT - 1U;
    }
    *x_out = (uint16_t)x;
    *y_out = (uint16_t)y;
}
