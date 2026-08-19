#include "display.h"

#include <stdio.h>
#include <string.h>

#include "fire_lcd.h"
#include "lvgl.h"
#include "touch_gt911.h"

#define MODE_COLUMNS 3U
#define MODE_ROWS 9U
#define MODE_PANEL_X 520U
#define MODE_PANEL_Y 56U
#define MODE_BUTTON_X 8U
#define MODE_BUTTON_Y 46U
#define MODE_BUTTON_STEP_X 80U
#define MODE_BUTTON_STEP_Y 36U
#define MODE_BUTTON_W 76U
#define MODE_BUTTON_H 31U

#define WATERFALL_X 14U
#define WATERFALL_Y 76U
#define WATERFALL_WIDTH 456U
#define WATERFALL_HEIGHT 150U
#define WATERFALL_MAX_HZ 3000U
#define WATERFALL_SAMPLE_RATE_HZ 16000U
#define WATERFALL_LABEL_Y 229U
#define WATERFALL_FREQ_LABEL_WIDTH 48U
#define WATERFALL_SCREEN_X (20U + WATERFALL_X)
#define WATERFALL_SCREEN_Y (56U + WATERFALL_Y)

/* This region follows the 800x480 RGB888 LTDC frame buffer and LVGL heap. */
#define LVGL_DRAW_BUFFER_ADDR 0xD01A0000UL
#define LVGL_DRAW_BUFFER_LINES 32U
#define LVGL_DRAW_BUFFER_PIXELS (FIRE_LCD_WIDTH * LVGL_DRAW_BUFFER_LINES)

static uint8_t g_lvgl_ready;
static lv_disp_draw_buf_t g_draw_buf;
static lv_disp_drv_t g_disp_drv;
static lv_indev_drv_t g_touch_drv;
static lv_obj_t *g_mode_label;
static lv_obj_t *g_baud_label;
static lv_obj_t *g_tone_label;
static lv_obj_t *g_input_label;
static lv_obj_t *g_touch_label;
static lv_obj_t *g_rx_label;
static lv_obj_t *g_mode_buttons[PSK_MODE_COUNT];
static lv_color_t *const g_draw_buffer = (lv_color_t *)LVGL_DRAW_BUFFER_ADDR;
static lv_color_t g_waterfall_line[WATERFALL_WIDTH];
static display_mode_selected_cb_t g_mode_cb;
static void *g_mode_cb_user;

static void build_ui(void);
static lv_obj_t *make_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                            lv_coord_t w, lv_coord_t h);
static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y,
                            lv_coord_t w, const lv_font_t *font, lv_color_t color);
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);
static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);
static void mode_button_event_cb(lv_event_t *event);
static void update_mode_buttons(const psk_mode_info_t *active_mode);
static void update_touch_label(void);
static void format_baud(char *buf, size_t len, const psk_mode_info_t *mode);
static const char *tail_text(const char *text, size_t max_len);
static void waterfall_reset(void);
static void waterfall_push(const spectrum_t *spectrum);
static lv_color_t waterfall_color(uint8_t level);
static void make_waterfall_frequency_labels(lv_obj_t *parent);

void display_init(void)
{
    printf("[display] lvgl init begin\r\n");
    if (fire_lcd_init() != HAL_OK) {
        printf("[display] lcd init failed\r\n");
        return;
    }

    lv_init();
    lv_disp_draw_buf_init(&g_draw_buf, g_draw_buffer, NULL, LVGL_DRAW_BUFFER_PIXELS);
    lv_disp_drv_init(&g_disp_drv);
    g_disp_drv.hor_res = FIRE_LCD_WIDTH;
    g_disp_drv.ver_res = FIRE_LCD_HEIGHT;
    g_disp_drv.flush_cb = flush_cb;
    g_disp_drv.draw_buf = &g_draw_buf;
    (void)lv_disp_drv_register(&g_disp_drv);

    lv_indev_drv_init(&g_touch_drv);
    g_touch_drv.type = LV_INDEV_TYPE_POINTER;
    g_touch_drv.read_cb = touch_read_cb;
    (void)lv_indev_drv_register(&g_touch_drv);

    build_ui();
    g_lvgl_ready = 1U;

    /*
     * Build time invalidates the complete screen.  Flush it while no I2S DMA
     * producer is running yet: otherwise the first 800x480 RGB565-to-RGB888
     * conversion can briefly delay the audio consumer and overflow its queue.
    */
    lv_refr_now(NULL);
    waterfall_reset();
    printf("[display] lvgl backend ready\r\n");
}

void display_task(void)
{
    if (g_lvgl_ready) {
        (void)lv_timer_handler();
    }
}

void display_tick_inc(uint32_t elapsed_ms)
{
    if (g_lvgl_ready) {
        lv_tick_inc(elapsed_ms);
    }
}

void display_refresh_now(void)
{
    if (g_lvgl_ready) {
        lv_refr_now(NULL);
    }
}

void display_set_mode_selected_callback(display_mode_selected_cb_t cb, void *user)
{
    g_mode_cb = cb;
    g_mode_cb_user = user;
}

void display_status(const char *line1, const char *line2)
{
    if (!g_lvgl_ready) {
        printf("[status] %s | %s\r\n", line1 ? line1 : "", line2 ? line2 : "");
        return;
    }
    lv_label_set_text(g_mode_label, line1 ? line1 : "");
    lv_label_set_text(g_baud_label, line2 ? line2 : "");
}

void display_render(const spectrum_t *spectrum, const psk_mode_info_t *mode, int tone_hz,
                    const char *decoded, uint16_t input_abs_avg, uint8_t selected_lane,
                    uint32_t queued_blocks)
{
    char line[96];
    const int peak_hz = spectrum ? spectrum->peak_hz : 0;

    if (!g_lvgl_ready) {
        printf("[render] mode=%s tone=%dHz peak=%dHz text=%s\r\n",
               mode ? mode->name : "PSK", tone_hz, peak_hz, decoded ? decoded : "");
        return;
    }

    lv_label_set_text_fmt(g_mode_label, "STM32H743 %s", mode ? mode->name : "PSK");
    format_baud(line, sizeof(line), mode);
    lv_label_set_text_fmt(g_baud_label, "%s  %uC %uB%s", line,
                          mode ? mode->carriers : 0U, mode ? mode->bits_per_symbol : 0U,
                          mode && mode->fec ? " FEC" : (mode && !mode->decode_supported ? " VIEW" : ""));
    lv_label_set_text_fmt(g_tone_label, "Tone %d Hz  Peak %d Hz", tone_hz, peak_hz);
    lv_label_set_text_fmt(g_input_label, "Input %u  Lane %c  Queue %lu", input_abs_avg,
                          selected_lane == 1U ? 'R' : 'L', (unsigned long)queued_blocks);
    lv_label_set_text(g_rx_label, decoded && decoded[0] ? tail_text(decoded, 520U) :
                      "[NO DECODED PSK TEXT YET]");
    waterfall_push(spectrum);
    update_touch_label();
    update_mode_buttons(mode);
}

int display_mode_at(uint16_t x, uint16_t y, psk_mode_t *mode)
{
    const uint16_t matrix_x = (uint16_t)(MODE_PANEL_X + MODE_BUTTON_X);
    const uint16_t matrix_y = (uint16_t)(MODE_PANEL_Y + MODE_BUTTON_Y);
    const uint16_t matrix_w = (uint16_t)(MODE_BUTTON_STEP_X * MODE_COLUMNS);
    const uint16_t matrix_h = (uint16_t)(MODE_BUTTON_STEP_Y * MODE_ROWS);
    if (x < matrix_x || y < matrix_y || (uint32_t)(x - matrix_x) >= matrix_w ||
        (uint32_t)(y - matrix_y) >= matrix_h) {
        return 0;
    }

    const uint32_t local_x = (uint32_t)x - matrix_x;
    const uint32_t local_y = (uint32_t)y - matrix_y;
    const uint32_t col = local_x / MODE_BUTTON_STEP_X;
    const uint32_t row = local_y / MODE_BUTTON_STEP_Y;
    if (col >= MODE_COLUMNS || row >= MODE_ROWS ||
        (local_x % MODE_BUTTON_STEP_X) >= MODE_BUTTON_W ||
        (local_y % MODE_BUTTON_STEP_Y) >= MODE_BUTTON_H) {
        return 0;
    }

    const psk_mode_info_t *info = psk_mode_info_at((size_t)(col * MODE_ROWS + row));
    if (!info) {
        return 0;
    }
    if (mode) {
        *mode = info->id;
    }
    return 1;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    const uint32_t width = (uint32_t)(area->x2 - area->x1 + 1);
    const uint32_t height = (uint32_t)(area->y2 - area->y1 + 1);
    fire_lcd_blit_rgb565((uint32_t)area->x1, (uint32_t)area->y1, width, height,
                          (const uint16_t *)color_p, width);
    lv_disp_flush_ready(drv);
}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    touch_gt911_state_t state;
    (void)drv;
    if (touch_gt911_read(&state) == HAL_OK && state.pressed) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = (lv_coord_t)state.x;
        data->point.y = (lv_coord_t)state.y;
    } else {
        touch_gt911_debug_snapshot(&state);
        data->state = LV_INDEV_STATE_REL;
        data->point.x = (lv_coord_t)state.x;
        data->point.y = (lv_coord_t)state.y;
    }
}

static void build_ui(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), LV_PART_MAIN);
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, lv_color_hex(0xE8EEF2), LV_PART_MAIN);

    lv_obj_t *header = lv_obj_create(screen);
    lv_obj_remove_style_all(header);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, FIRE_LCD_WIDTH, 46);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1E3A5F), LV_PART_MAIN);
    make_label(header, "STM32H743 PSK MODEM", 22, 10, 420, &lv_font_montserrat_20,
               lv_color_hex(0xFFFFFF));
    g_touch_label = make_label(header, "TP --", 610, 15, 170, &lv_font_montserrat_14,
                                lv_color_hex(0xB0BEC5));

    lv_obj_t *left = make_panel(screen, 20, 56, 488, 406);
    g_mode_label = make_label(left, "STM32H743 BPSK31", 14, 10, 270, &lv_font_montserrat_20,
                               lv_color_hex(0xFFFFFF));
    g_baud_label = make_label(left, "31.25 BD", 284, 13, 180, &lv_font_montserrat_14,
                               lv_color_hex(0x4DD0E1));
    g_tone_label = make_label(left, "Tone 1500 Hz  Peak 0 Hz", 14, 42, 280,
                               &lv_font_montserrat_14, lv_color_hex(0xFFD54F));
    g_input_label = make_label(left, "Input 0  Lane L  Queue 0", 284, 42, 190,
                                &lv_font_montserrat_14, lv_color_hex(0x81C784));

    make_waterfall_frequency_labels(left);

    lv_obj_t *rx_panel = make_panel(left, 14, 248, 456, 138);
    make_label(rx_panel, "RX TEXT", 10, 8, 120, &lv_font_montserrat_14, lv_color_hex(0x81C784));
    g_rx_label = make_label(rx_panel, "[NO DECODED PSK TEXT YET]", 10, 32, 436,
                            &lv_font_montserrat_14, lv_color_hex(0xFFFFFF));
    lv_label_set_long_mode(g_rx_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(g_rx_label, 96);

    lv_obj_t *right = make_panel(screen, MODE_PANEL_X, MODE_PANEL_Y, 256, 406);
    make_label(right, "MODE SELECT", 12, 10, 220, &lv_font_montserrat_16, lv_color_hex(0x4DD0E1));
    for (size_t i = 0; i < psk_mode_count() && i < PSK_MODE_COUNT; ++i) {
        const psk_mode_info_t *mode = psk_mode_info_at(i);
        const uint32_t col = (uint32_t)(i / MODE_ROWS);
        const uint32_t row = (uint32_t)(i % MODE_ROWS);
        lv_obj_t *button = lv_btn_create(right);
        lv_obj_set_pos(button, (lv_coord_t)(MODE_BUTTON_X + col * MODE_BUTTON_STEP_X),
                       (lv_coord_t)(MODE_BUTTON_Y + row * MODE_BUTTON_STEP_Y));
        lv_obj_set_size(button, MODE_BUTTON_W, MODE_BUTTON_H);
        lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
        lv_obj_set_style_bg_color(button, lv_color_hex(mode && mode->decode_supported ? 0x173322 : 0x26313A),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_color(button, lv_color_hex(0xFFC107), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(button, lv_color_hex(0xE8EEF2), LV_PART_MAIN);
        lv_obj_set_style_text_color(button, lv_color_hex(0x101820), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_add_event_cb(button, mode_button_event_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(mode ? mode->id : 0));
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, mode ? mode->name : "?");
        lv_obj_center(label);
        g_mode_buttons[i] = button;
    }
}

static lv_obj_t *make_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                            lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x16212B), LV_PART_MAIN);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x455A64), LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    return panel;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y,
                            lv_coord_t w, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_label_set_text(label, text ? text : "");
    return label;
}

static void mode_button_event_cb(lv_event_t *event)
{
    const psk_mode_t mode = (psk_mode_t)(uintptr_t)lv_event_get_user_data(event);
    if (g_mode_cb) {
        g_mode_cb(mode, g_mode_cb_user);
    }
}

static void update_mode_buttons(const psk_mode_info_t *active_mode)
{
    for (size_t i = 0; i < psk_mode_count() && i < PSK_MODE_COUNT; ++i) {
        const psk_mode_info_t *mode = psk_mode_info_at(i);
        if (!mode || !g_mode_buttons[i]) {
            continue;
        }
        if (active_mode && active_mode->id == mode->id) {
            lv_obj_add_state(g_mode_buttons[i], LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(g_mode_buttons[i], LV_STATE_CHECKED);
        }
    }
}

static void update_touch_label(void)
{
    touch_gt911_state_t state;
    touch_gt911_debug_snapshot(&state);
    lv_label_set_text_fmt(g_touch_label, "TP %s %u,%u",
                          state.present ? (state.pressed ? "DOWN" : "OK") : "--", state.x, state.y);
    lv_obj_set_style_text_color(g_touch_label,
                                state.present ? lv_color_hex(0x4DD0E1) : lv_color_hex(0xEF5350),
                                LV_PART_MAIN);
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
        snprintf(buf, len, "%lu.%lu BD", (unsigned long)(baud_x100 / 100U),
                 (unsigned long)((baud_x100 / 10U) % 10U));
    } else {
        snprintf(buf, len, "%lu.%02lu BD", (unsigned long)(baud_x100 / 100U),
                 (unsigned long)(baud_x100 % 100U));
    }
}

static const char *tail_text(const char *text, size_t max_len)
{
    const size_t len = text ? strlen(text) : 0U;
    return (!text || len <= max_len) ? (text ? text : "") : text + len - max_len;
}

static void make_waterfall_frequency_labels(lv_obj_t *parent)
{
    static const struct { uint16_t hz; const char *text; } ticks[] = {
        {0U, "0 Hz"}, {500U, "500"}, {1000U, "1000"}, {1500U, "1500"},
        {2000U, "2000"}, {2500U, "2500"}, {3000U, "3000 Hz"},
    };
    for (size_t i = 0; i < sizeof(ticks) / sizeof(ticks[0]); ++i) {
        int32_t x = (int32_t)(WATERFALL_X + ((uint32_t)ticks[i].hz * WATERFALL_WIDTH) /
                              WATERFALL_MAX_HZ) - (int32_t)(WATERFALL_FREQ_LABEL_WIDTH / 2U);
        if (x < (int32_t)WATERFALL_X) {
            x = WATERFALL_X;
        }
        if (x + (int32_t)WATERFALL_FREQ_LABEL_WIDTH > 470) {
            x = 470 - (int32_t)WATERFALL_FREQ_LABEL_WIDTH;
        }
        lv_obj_t *label = make_label(parent, ticks[i].text, (lv_coord_t)x, WATERFALL_LABEL_Y,
                                     WATERFALL_FREQ_LABEL_WIDTH, &lv_font_montserrat_14,
                                     lv_color_hex(0x90A4AE));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
}

static void waterfall_reset(void)
{
    fire_lcd_fill_rect(WATERFALL_SCREEN_X, WATERFALL_SCREEN_Y,
                       WATERFALL_WIDTH, WATERFALL_HEIGHT, FIRE_LCD_COLOR_BLACK);
    fire_lcd_draw_rect(WATERFALL_SCREEN_X - 1U, WATERFALL_SCREEN_Y - 1U,
                       WATERFALL_WIDTH + 2U, WATERFALL_HEIGHT + 2U, FIRE_LCD_COLOR_GRAY);
}

static void waterfall_push(const spectrum_t *spectrum)
{
    if (!spectrum) {
        return;
    }
    for (uint32_t x = 0; x < WATERFALL_WIDTH; ++x) {
        const uint32_t scaled = x * ((WATERFALL_MAX_HZ * 2U * SPECTRUM_BINS) /
                                     WATERFALL_SAMPLE_RATE_HZ);
        const uint32_t bin = scaled / (WATERFALL_WIDTH - 1U);
        const uint32_t fraction = scaled % (WATERFALL_WIDTH - 1U);
        const uint32_t next_bin = (bin + 1U < SPECTRUM_BINS) ? bin + 1U : bin;
        const int level0 = spectrum->bins[bin];
        const int level1 = spectrum->bins[next_bin];
        const uint8_t level = (uint8_t)(level0 + (((level1 - level0) * (int)fraction +
            (int)(WATERFALL_WIDTH - 1U) / 2) / (int)(WATERFALL_WIDTH - 1U)));
        g_waterfall_line[x] = waterfall_color(level);
    }
    fire_lcd_scroll_rect_up(WATERFALL_SCREEN_X, WATERFALL_SCREEN_Y,
                            WATERFALL_WIDTH, WATERFALL_HEIGHT, 1U);
    fire_lcd_blit_rgb565(WATERFALL_SCREEN_X, WATERFALL_SCREEN_Y + WATERFALL_HEIGHT - 1U,
                          WATERFALL_WIDTH, 1U, (const uint16_t *)g_waterfall_line,
                          WATERFALL_WIDTH);
}

static lv_color_t waterfall_color(uint8_t level)
{
    uint8_t red = 0U;
    uint8_t green = 0U;
    uint8_t blue = 0U;
    if (level < 16U) {
        blue = (uint8_t)(level * 2U);
    } else if (level < 64U) {
        blue = (uint8_t)(32U + (level - 16U) * 4U);
    } else if (level < 128U) {
        green = (uint8_t)((level - 64U) * 4U);
        blue = (uint8_t)(255U - (level - 64U) * 3U);
    } else if (level < 192U) {
        red = (uint8_t)((level - 128U) * 4U);
        green = 255U;
    } else {
        red = 255U;
        green = 255U;
        blue = (uint8_t)((level - 192U) * 4U);
    }
    return lv_color_make(red, green, blue);
}
