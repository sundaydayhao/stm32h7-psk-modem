#include "audio_decim.h"

size_t audio_decim_boxcar(const int16_t *in, size_t in_count, int16_t *out, uint32_t decimation)
{
    size_t out_count = 0;

    for (size_t base = 0; base + decimation <= in_count; base += decimation) {
        int32_t acc = 0;
        for (uint32_t j = 0; j < decimation; ++j) {
            acc += in[base + j];
        }
        out[out_count++] = (int16_t)(acc / (int32_t)decimation);
    }

    return out_count;
}
