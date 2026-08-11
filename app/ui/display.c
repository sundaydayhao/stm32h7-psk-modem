#include "display.h"

#include <stdio.h>
#include <string.h>

#include "audio_input.h"
#include "fire_lcd.h"
#include "touch_gt911.h"

static uint8_t g_lcd_ready;
static char g_status1[64];
static char g_status2[96];
static uint32_t g_waterfall_row;

#define LEFT_X 24U
#define LEFT_W 484U
#define MODE_MATRIX_X 520U
#define MODE_MATRIX_Y 56U
#define MODE_MATRIX_W 256U
#define MODE_MATRIX_H 406U
#define MODE_MATRIX_PAD_X 8U
#define MODE_MATRIX_HEADER_H 32U
#define MODE_MATRIX_COL_W 80U
#define MODE_MATRIX_ROW_H 36U
#define MODE_MATRIX_ROWS 9U

static void draw_static_frame(void);
static void draw_spectrum_and_waterfall(const spectrum_t *spectrum);
static void draw_mode_matrix(const psk_mode_info_t *active_mode);
static void draw_touch_status(void);
static void format_baud(char *buf, size_t len, const psk_mode_info_t *mode);
static uint32_t heat_color(uint8_t value);
static void draw_wrapped_text_tail(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char *text);

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

    fire_lcd_fill_rect(LEFT_X, 56U, LEFT_W, 42U, FIRE_LCD_COLOR_DARK);
    fire_lcd_draw_text_scale(36U, 60U, g_status1, FIRE_LCD_COLOR_WHITE, FIRE_LCD_COLOR_DARK, 2U);
    fire_lcd_draw_text_scale(36U, 84U, g_status2, FIRE_LCD_COLOR_CYAN, FIRE_LCD_COLOR_DARK, 1U);
}

void display_render(const spectrum_t *spectrum, const psk_mode_info_t *mode, int tone_hz, const char *decoded)
{
    char line[96];
    const int peak_hz = spectrum ? spectrum->peak_hz : 0;
    audio_input_debug_t audio_debug;
    audio_input_debug_snapshot(&audio_debug);

    if (!g_lcd_ready) {
        printf("[render] mode=%s tone=%dHz peak=%dHz text=%s\r\n",
               mode ? mode->name : "PSK",
               tone_hz,
               peak_hz,
               decoded && decoded[0] ? decoded : "");
        return;
    }

    fire_lcd_fill_rect(LEFT_X, 56U, LEFT_W, 42U, 0xFF16212BUL);
    snprintf(line, sizeof(line), "%s %s", mode ? psk_mode_family_name(mode->family) : "PSK",
             mode ? mode->name : "");
    fire_lcd_draw_text_scale(36U, 62U, line, FIRE_LCD_COLOR_WHITE, 0xFF16212BUL, 2U);
    format_baud(line, sizeof(line), mode);
    fire_lcd_draw_text_scale(288U, 62U, line, FIRE_LCD_COLOR_CYAN, 0xFF16212BUL, 1U);
    snprintf(line, sizeof(line), "%uC %uB%s",
             mode ? mode->carriers : 0U,
             mode ? mode->bits_per_symbol : 0U,
             mode && mode->fec ? " FEC" : (mode && !mode->decode_supported ? " VIEW" : ""));
    fire_lcd_draw_text_scale(288U, 76U, line, FIRE_LCD_COLOR_CYAN, 0xFF16212BUL, 1U);
    snprintf(line, sizeof(line), "TONE %d  PEAK %d", tone_hz, peak_hz);
    fire_lcd_draw_text_scale(36U, 86U, line, FIRE_LCD_COLOR_YELLOW, 0xFF16212BUL, 1U);
    snprintf(line, sizeof(line), "IN %u %c Q%lu",
             audio_debug.last_abs_avg,
             audio_debug.selected_lane == 1U ? 'R' : 'L',
             audio_debug.queued_blocks);
    fire_lcd_draw_text_scale(324U, 86U, line, FIRE_LCD_COLOR_GREEN, 0xFF16212BUL, 1U);

    draw_spectrum_and_waterfall(spectrum);

    fire_lcd_fill_rect(LEFT_X, 334U, LEFT_W, 128U, 0xFF101820UL);
    fire_lcd_draw_rect(LEFT_X, 334U, LEFT_W, 128U, FIRE_LCD_COLOR_GRAY);
    fire_lcd_draw_text_scale(36U, 344U, "RX TEXT", FIRE_LCD_COLOR_GREEN, 0xFF101820UL, 1U);
    draw_wrapped_text_tail(36U, 366U, LEFT_W - 24U, 82U, decoded && decoded[0] ? decoded : "[NO DECODED PSK TEXT YET]");
    draw_mode_matrix(mode);
}

int display_mode_at(uint16_t x, uint16_t y, psk_mode_t *mode)
{
    if (x < (MODE_MATRIX_X + MODE_MATRIX_PAD_X) ||
        y < (MODE_MATRIX_Y + MODE_MATRIX_HEADER_H)) {
        return 0;
    }
    const uint32_t local_x = (uint32_t)x - MODE_MATRIX_X - MODE_MATRIX_PAD_X;
    const uint32_t local_y = (uint32_t)y - MODE_MATRIX_Y - MODE_MATRIX_HEADER_H;
    if (local_x >= MODE_MATRIX_COL_W * 3U || local_y >= MODE_MATRIX_ROW_H * MODE_MATRIX_ROWS) {
        return 0;
    }
    const uint32_t col = local_x / MODE_MATRIX_COL_W;
    const uint32_t row = local_y / MODE_MATRIX_ROW_H;
    if (col >= 3U || row >= MODE_MATRIX_ROWS) {
        return 0;
    }

    const size_t index = (size_t)(col * MODE_MATRIX_ROWS + row);
    const psk_mode_info_t *info = psk_mode_info_at(index);
    if (!info) {
        return 0;
    }
    if (mode) {
        *mode = info->id;
    }
    return 1;
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
}

static void draw_spectrum_and_waterfall(const spectrum_t *spectrum)
{
    const uint32_t x = LEFT_X;
    const uint32_t spec_y = 108U;
    const uint32_t spec_h = 58U;
    const uint32_t wf_y = 174U;
    const uint32_t wf_h = 144U;
    const uint32_t w = LEFT_W;

    fire_lcd_fill_rect(x, spec_y, w, spec_h, 0xFF0A1118UL);
    fire_lcd_draw_rect(x, spec_y, w, spec_h + 8U + wf_h, FIRE_LCD_COLOR_GRAY);
    fire_lcd_draw_text_scale(x + 8U, spec_y + 4U, "SPECTRUM / WATERFALL 0-4096 HZ",
                             FIRE_LCD_COLOR_GRAY, 0xFF0A1118UL, 1U);

    if (!spectrum) {
        return;
    }

    for (uint32_t bin = 0; bin < SPECTRUM_BINS; ++bin) {
        const uint32_t level = ((uint32_t)spectrum->bins[bin] * (spec_h - 14U)) / 255U;
        const uint32_t bx = x + (bin * w) / SPECTRUM_BINS;
        const uint32_t next_x = x + ((bin + 1U) * w) / SPECTRUM_BINS;
        const uint32_t bin_w = next_x > bx ? next_x - bx : 1U;
        const uint32_t color = heat_color(spectrum->bins[bin]);
        if (level > 0U) {
            fire_lcd_fill_rect(bx, spec_y + spec_h - level, bin_w, level, color);
        }
        fire_lcd_fill_rect(bx, wf_y + g_waterfall_row, bin_w, 1U, color);
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

static void draw_mode_matrix(const psk_mode_info_t *active_mode)
{
    const uint32_t x = MODE_MATRIX_X;
    const uint32_t y = MODE_MATRIX_Y;
    const uint32_t w = MODE_MATRIX_W;
    const uint32_t h = MODE_MATRIX_H;
    char line[24];

    fire_lcd_fill_rect(x, y, w, h, 0xFF0D141BUL);
    fire_lcd_draw_rect(x, y, w, h, FIRE_LCD_COLOR_GRAY);
    fire_lcd_draw_text_scale(x + 12U, y + 10U, "MODE SELECT", FIRE_LCD_COLOR_CYAN, 0xFF0D141BUL, 2U);

    for (size_t i = 0; i < psk_mode_count(); ++i) {
        const psk_mode_info_t *mode = psk_mode_info_at(i);
        const uint32_t col = (uint32_t)(i / MODE_MATRIX_ROWS);
        const uint32_t row = (uint32_t)(i % MODE_MATRIX_ROWS);
        const uint32_t cell_x = x + MODE_MATRIX_PAD_X + col * MODE_MATRIX_COL_W;
        const uint32_t cell_y = y + MODE_MATRIX_HEADER_H + row * MODE_MATRIX_ROW_H;
        const uint32_t cell_w = MODE_MATRIX_COL_W - 4U;
        const uint32_t cell_h = MODE_MATRIX_ROW_H - 5U;
        uint32_t fg = mode->decode_supported ? FIRE_LCD_COLOR_GREEN : 0xFFB0BEC5UL;
        uint32_t bg = mode->decode_supported ? 0xFF13251DUL : 0xFF182027UL;

        if (active_mode && active_mode->id == mode->id) {
            fg = FIRE_LCD_COLOR_BLACK;
            bg = FIRE_LCD_COLOR_YELLOW;
        }

        fire_lcd_fill_rect(cell_x, cell_y, cell_w, cell_h, bg);
        fire_lcd_draw_rect(cell_x, cell_y, cell_w, cell_h, active_mode && active_mode->id == mode->id ? FIRE_LCD_COLOR_WHITE : FIRE_LCD_COLOR_GRAY);
        snprintf(line, sizeof(line), "%s", mode->name);
        fire_lcd_draw_text_scale(cell_x + 6U, cell_y + 8U, line, fg, bg, 1U);
        fire_lcd_draw_text_scale(cell_x + 6U, cell_y + 20U, mode->decode_supported ? "RX" : "VIEW",
                                 mode->decode_supported ? FIRE_LCD_COLOR_CYAN : FIRE_LCD_COLOR_GRAY, bg, 1U);
    }
    draw_touch_status();
}

static void draw_touch_status(void)
{
    touch_gt911_state_t ts;
    char line[40];
    touch_gt911_debug_snapshot(&ts);

    const uint32_t x = MODE_MATRIX_X + 8U;
    const uint32_t y = MODE_MATRIX_Y + MODE_MATRIX_HEADER_H + MODE_MATRIX_ROW_H * MODE_MATRIX_ROWS + 2U;
    const uint32_t bg = 0xFF0D141BUL;

    fire_lcd_fill_rect(x, y, MODE_MATRIX_W - 16U, 42U, bg);
    snprintf(line, sizeof(line), "TP %s A%02X S%lu E%lu",
             ts.present ? (ts.pressed ? "DOWN" : "OK") : "--",
             ts.address >> 1,
             (unsigned long)ts.scan_mask,
             (unsigned long)ts.errors);
    fire_lcd_draw_text_scale(x, y + 2U, line, ts.present ? FIRE_LCD_COLOR_CYAN : FIRE_LCD_COLOR_RED, bg, 1U);
    snprintf(line, sizeof(line), "XY %u,%u ST%02X",
             ts.x,
             ts.y,
             ts.last_status);
    fire_lcd_draw_text_scale(x, y + 18U, line, FIRE_LCD_COLOR_GRAY, bg, 1U);
    snprintf(line, sizeof(line), "RAW %u,%u",
             ts.raw_x,
             ts.raw_y);
    fire_lcd_draw_text_scale(x + 112U, y + 18U, line, FIRE_LCD_COLOR_GRAY, bg, 1U);
}

static void format_baud(char *buf, size_t len, const psk_mode_info_t *mode)
{
    if (!buf || len == 0U) {
        return;
    }
    if (!mode) {
        snprintf(buf, len, "0 BD");
        return;
    }

    const uint32_t baud_x100 = (uint32_t)(mode->baud * 100.0f + 0.5f);
    if ((baud_x100 % 100U) == 0U) {
        snprintf(buf, len, "%lu BD", (unsigned long)(baud_x100 / 100U));
    } else if ((baud_x100 % 10U) == 0U) {
        snprintf(buf, len, "%lu.%lu BD",
                 (unsigned long)(baud_x100 / 100U),
                 (unsigned long)((baud_x100 / 10U) % 10U));
    } else {
        snprintf(buf, len, "%lu.%02lu BD",
                 (unsigned long)(baud_x100 / 100U),
                 (unsigned long)(baud_x100 % 100U));
    }
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

static void draw_wrapped_text_tail(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char *text)
{
    const uint32_t char_w = 12U;
    const uint32_t char_h = 16U;
    const uint32_t max_cols = w / char_w;
    const uint32_t max_rows = h / char_h;
    size_t row_start_ring[16] = {0};
    const size_t ring_len = sizeof(row_start_ring) / sizeof(row_start_ring[0]);
    size_t line = 0;
    size_t start = 0;
    size_t index = 0;
    uint32_t col = 0;
    uint32_t row = 0;
    char one[2] = {0};

    if (!text || max_cols == 0U || max_rows == 0U) {
        return;
    }

    row_start_ring[0] = 0;
    while (text[index]) {
        const char c = text[index];
        if (c == '\r') {
            ++index;
            continue;
        }
        if (c == '\n') {
            ++line;
            row_start_ring[line % ring_len] = index + 1U;
            col = 0;
            ++index;
            continue;
        }
        if (col >= max_cols) {
            ++line;
            row_start_ring[line % ring_len] = index;
            col = 0;
        }
        ++col;
        ++index;
    }

    const size_t total_lines = line + 1U;
    if (total_lines > max_rows) {
        const size_t start_line = total_lines - max_rows;
        start = row_start_ring[start_line % ring_len];
    }

    text += start;
    col = 0;
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
