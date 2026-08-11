#pragma once

#include <stdint.h>

#include "stm32h7xx_hal.h"

typedef struct {
    uint8_t present;
    uint8_t pressed;
    uint8_t points;
    uint16_t x;
    uint16_t y;
    uint32_t reads;
    uint32_t errors;
    uint32_t scan_mask;
    uint32_t last_error;
    uint8_t last_status;
    uint8_t address;
    uint16_t raw_x;
    uint16_t raw_y;
    char id[5];
} touch_gt911_state_t;

HAL_StatusTypeDef touch_gt911_init(void);
HAL_StatusTypeDef touch_gt911_read(touch_gt911_state_t *state);
void touch_gt911_debug_snapshot(touch_gt911_state_t *state);
