#pragma once

#include <stddef.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"

#define FIRE_LCD_WIDTH 800U
#define FIRE_LCD_HEIGHT 480U

#define FIRE_LCD_COLOR_BLACK 0xFF000000UL
#define FIRE_LCD_COLOR_WHITE 0xFFFFFFFFUL
#define FIRE_LCD_COLOR_RED 0xFFE53935UL
#define FIRE_LCD_COLOR_GREEN 0xFF43A047UL
#define FIRE_LCD_COLOR_BLUE 0xFF1E88E5UL
#define FIRE_LCD_COLOR_YELLOW 0xFFFFC107UL
#define FIRE_LCD_COLOR_CYAN 0xFF00ACC1UL
#define FIRE_LCD_COLOR_GRAY 0xFF455A64UL
#define FIRE_LCD_COLOR_DARK 0xFF101820UL

HAL_StatusTypeDef fire_lcd_init(void);
void fire_lcd_clear(uint32_t color);
void fire_lcd_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fire_lcd_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fire_lcd_draw_hline(uint32_t x, uint32_t y, uint32_t w, uint32_t color);
void fire_lcd_draw_text(uint32_t x, uint32_t y, const char *text, uint32_t color, uint32_t bg);
void fire_lcd_draw_text_scale(uint32_t x, uint32_t y, const char *text, uint32_t color, uint32_t bg, uint32_t scale);
