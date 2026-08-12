#pragma once

#include <stddef.h>
#include <stdint.h>

/* Box-car decimator: averages consecutive groups of `decimation` mono samples.
 * Shared by app/platform/audio_input.c (real ADC path) and the host test
 * harness (tools/host_psk_rtty_wav.c), so host regression tests exercise the
 * exact decimation arithmetic the firmware runs, not a reimplementation. */
size_t audio_decim_boxcar(const int16_t *in, size_t in_count, int16_t *out, uint32_t decimation);
