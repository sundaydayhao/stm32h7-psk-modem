#pragma once

#include "spectrum.h"

void display_init(void);
void display_task(void);
void display_status(const char *line1, const char *line2);
void display_render(const spectrum_t *spectrum, const char *mode, int tone_hz, const char *decoded);
