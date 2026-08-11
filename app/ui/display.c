#include "display.h"

#include <stdio.h>
#include <string.h>

#include "audio_input.h"
#include "fire_lcd.h"

static uint8_t g_lcd_ready;
static char g_status1[64];
static char g_status2[96];
static uint32_t g_waterfall_row;

static void draw_static_frame(void);
static void draw_spectrum_and_waterfall(const spectrum_t *spectrum);
static uint32_t heat_color(uint8_t value);
static void draw_wrapped_text(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char *text);

void display_init(void)
{
    if (fire_lcd_init() == HAL_OK) {
        g_lcd_ready = 1U;
        draw_static_frame();
        printf("[display] embedfire h743 ltdc lcd backend\r\n");
    } else {
        g_lcd_ready = 0U;
        printf("[display] lcd init failed; uart fallback active\r\n");
    }
}

void display_task(void)
{
}

void display_status(const char *line1, const char *line2)
{
    snprintf(g_status1, sizeof(g_status1), "%s", line1 ? line1 : "");
    snprintf(g_status2, sizeof(g_status2), "%s", line2 ? line2 : "");

    if (!g_lcd_ready) {
        printf("[status] %s | %s\r\n", g_status1, g_status2);
        return;
    }

    fire_lcd_fill_rect(24U, 56U, 752U, 44U, FIRE_LCD_COLOR_DARK);
    fire_lcd_draw_text_scale(32U, 60U, g_status1, FIRE_LCD_COLOR_WHITE, FIRE_LCD_COLOR_DARK, 3U);
    fire_lcd_draw_text_scale(32U, 88U, g_status2, FIRE_LCD_COLOR_CYAN, FIRE_LCD_COLOR_DARK, 1U);
}

void display_render(const spectrum_t *spectrum, const char *mode, int tone_hz, const char *decoded)
{
    char line[96];
    const int peak_hz = spectrum ? spectrum->peak_hz : 0;
    audio_input_debug_t audio_debug;
    audio_input_debug_snapshot(&audio_debug);

    if (!g_lcd_ready) {
        printf("[render] mode=%s tone=%dHz peak=%dHz text=%s\r\n",
               mode ? mode : "PSK",
               tone_hz,
               peak_hz,
               decoded && decoded[0] ? decoded : "");
        return;
    }

    fire_lcd_fill_rect(24U, 56U, 752U, 40U, 0xFF16212BUL);
    snprintf(line, sizeof(line), "MODE %s", mode ? mode : "PSK");
    fire_lcd_draw_text_scale(40U, 66U, line, FIRE_LCD_COLOR_WHITE, 0xFF16212BUL, 2U);
    snprintf(line, sizeof(line), "TONE %d HZ", tone_hz);
    fire_lcd_draw_text_scale(244U, 66U, line, FIRE_LCD_COLOR_CYAN, 0xFF16212BUL, 2U);
    snprintf(line, sizeof(line), "PEAK %d HZ", peak_hz);
    fire_lcd_draw_text_scale(496U, 66U, line, FIRE_LCD_COLOR_YELLOW, 0xFF16212BUL, 2U);
    snprintf(line, sizeof(line), "IN %u %c Q%lu",
             audio_debug.last_abs_avg,
             audio_debug.selected_lane == 1U ? 'R' : 'L',
             audio_debug.queued_blocks);
    fire_lcd_draw_text_scale(610U, 88U, line, FIRE_LCD_COLOR_GREEN, 0xFF16212BUL, 1U);

    draw_spectrum_and_waterfall(spectrum);

    fire_lcd_fill_rect(24U, 342U, 752U, 120U, 0xFF101820UL);
    fire_lcd_draw_rect(24U, 342U, 752U, 120U, FIRE_LCD_COLOR_GRAY);
    fire_lcd_draw_text_scale(36U, 352U, "RX TEXT", FIRE_LCD_COLOR_GREEN, 0xFF101820UL, 1U);
    draw_wrapped_text(36U, 374U, 728U, 76U, decoded && decoded[0] ? decoded : "[NO DECODED PSK TEXT YET]");
}

static void draw_static_frame(void)
{
    fire_lcd_clear(FIRE_LCD_COLOR_WHITE);
    fire_lcd_fill_rect(0U, 36U, FIRE_LCD_WIDTH / 4U, 14U, FIRE_LCD_COLOR_RED);
    fire_lcd_fill_rect(FIRE_LCD_WIDTH / 4U, 36U, FIRE_LCD_WIDTH / 4U, 14U, FIRE_LCD_COLOR_GREEN);
    fire_lcd_fill_rect(FIRE_LCD_WIDTH / 2U, 36U, FIRE_LCD_WIDTH / 4U, 14U, FIRE_LCD_COLOR_BLUE);
    fire_lcd_fill_rect((FIRE_LCD_WIDTH * 3U) / 4U, 36U, FIRE_LCD_WIDTH / 4U, 14U, FIRE_LCD_COLOR_YELLOW);
    fire_lcd_fill_rect(0U, 0U, FIRE_LCD_WIDTH, 36U, 0xFF1E3A5FUL);
    fire_lcd_draw_text_scale(24U, 10U, "STM32H743 PSK MODEM", FIRE_LCD_COLOR_WHITE, 0xFF1E3A5FUL, 2U);
    fire_lcd_draw_rect(18U, 50U, 764U, 416U, FIRE_LCD_COLOR_GRAY);
}

static void draw_spectrum_and_waterfall(const spectrum_t *spectrum)
{
    const uint32_t x = 32U;
    const uint32_t spec_y = 108U;
    const uint32_t spec_h = 54U;
    const uint32_t wf_y = 170U;
    const uint32_t wf_h = 156U;
    const uint32_t w = 736U;
    const uint32_t bin_w = w / SPECTRUM_BINS;

    fire_lcd_fill_rect(x, spec_y, w, spec_h, 0xFF0A1118UL);
    fire_lcd_draw_rect(x, spec_y, w, spec_h + 8U + wf_h, FIRE_LCD_COLOR_GRAY);
    fire_lcd_draw_text_scale(x + 8U, spec_y + 4U, "SPECTRUM / WATERFALL 0-4096 HZ",
                             FIRE_LCD_COLOR_GRAY, 0xFF0A1118UL, 1U);

    if (!spectrum) {
        return;
    }

    for (uint32_t bin = 0; bin < SPECTRUM_BINS; ++bin) {
        const uint32_t level = ((uint32_t)spectrum->bins[bin] * (spec_h - 14U)) / 255U;
        const uint32_t bx = x + bin * bin_w;
        const uint32_t color = heat_color(spectrum->bins[bin]);
        if (level > 0U) {
            fire_lcd_fill_rect(bx, spec_y + spec_h - level, bin_w ? bin_w : 1U, level, color);
        }
        fire_lcd_fill_rect(bx, wf_y + g_waterfall_row, bin_w ? bin_w : 1U, 1U, color);
    }

    if (spectrum->peak_hz > 0) {
        uint32_t marker = (uint32_t)((spectrum->peak_hz * w) / (AUDIO_INPUT_DECODE_RATE_HZ / 2));
        if (marker >= w) {
            marker = w - 1U;
        }
        fire_lcd_fill_rect(x + marker, spec_y, 2U, spec_h, FIRE_LCD_COLOR_YELLOW);
    }

    g_waterfall_row = (g_waterfall_row + 1U) % wf_h;
    fire_lcd_fill_rect(x, wf_y + g_waterfall_row, w, 1U, FIRE_LCD_COLOR_BLACK);
}

static uint32_t heat_color(uint8_t value)
{
    const uint32_t v = value;
    uint32_t r = 0U;
    uint32_t g = 0U;
    uint32_t b = 20U;

    if (v < 85U) {
        b = 40U + v * 2U;
    } else if (v < 170U) {
        g = (v - 85U) * 3U;
        b = 255U - (v - 85U);
    } else {
        r = (v - 170U) * 3U;
        g = 255U;
        b = 60U;
    }

    if (r > 255U) {
        r = 255U;
    }
    if (g > 255U) {
        g = 255U;
    }
    if (b > 255U) {
        b = 255U;
    }
    return 0xFF000000UL | (r << 16) | (g << 8) | b;
}

static void draw_wrapped_text(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char *text)
{
    const uint32_t char_w = 12U;
    const uint32_t char_h = 16U;
    const uint32_t max_cols = w / char_w;
    const uint32_t max_rows = h / char_h;
    uint32_t col = 0;
    uint32_t row = 0;
    char one[2] = {0};

    while (text && *text && row < max_rows) {
        char c = *text++;
        if (c == '\r') {
            continue;
        }
        if (c == '\n' || col >= max_cols) {
            col = 0;
            ++row;
            if (c == '\n') {
                continue;
            }
        }
        if (row >= max_rows) {
            break;
        }
        one[0] = c;
        fire_lcd_draw_text_scale(x + col * char_w, y + row * char_h, one,
                                 FIRE_LCD_COLOR_WHITE, 0xFF101820UL, 2U);
        ++col;
    }
}
