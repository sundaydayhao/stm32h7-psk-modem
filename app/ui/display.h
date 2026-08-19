#pragma once

#include "psk_modes.h"
#include "spectrum.h"

typedef void (*display_mode_selected_cb_t)(psk_mode_t mode, void *user);

void display_init(void);
void display_task(void);
void display_tick_inc(uint32_t elapsed_ms);
void display_refresh_now(void);
void display_status(const char *line1, const char *line2);
void display_render(const spectrum_t *spectrum, const psk_mode_info_t *mode, int tone_hz,
                    const char *decoded, uint16_t input_abs_avg, uint8_t selected_lane,
                    uint32_t queued_blocks);
int display_mode_at(uint16_t x, uint16_t y, psk_mode_t *mode);
void display_set_mode_selected_callback(display_mode_selected_cb_t cb, void *user);
