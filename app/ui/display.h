#pragma once

#include "psk_modes.h"
#include "spectrum.h"

void display_init(void);
void display_task(void);
void display_status(const char *line1, const char *line2);
void display_render(const spectrum_t *spectrum, const psk_mode_info_t *mode, int tone_hz, const char *decoded);
int display_mode_at(uint16_t x, uint16_t y, psk_mode_t *mode);
