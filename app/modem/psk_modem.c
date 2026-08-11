#include "psk_modem.h"

#include <string.h>

static psk_rtty_mode_t to_backend_mode(psk_mode_t mode)
{
    const psk_mode_info_t *info = psk_mode_info(mode);
    return info->backend_mode;
}

static void on_char(char c, void *userdata)
{
    psk_modem_t *m = (psk_modem_t *)userdata;
    if (!m->out || m->out_len + 1 >= m->out_size) {
        return;
    }
    m->out[m->out_len++] = c;
    m->out[m->out_len] = '\0';
}

static void backend_init(psk_modem_t *m)
{
    if (!psk_mode_can_decode(m->config.mode)) {
        memset(&m->rx, 0, sizeof(m->rx));
        m->backend_active = 0U;
        return;
    }

    psk_rtty_config_t cfg = {
        .mode = to_backend_mode(m->config.mode),
        .sample_rate_hz = m->config.sample_rate_hz,
        .rx_tone_hz = m->config.rx_tone_hz,
        .char_cb = on_char,
        .userdata = m,
    };
    psk_rtty_rx_init(&m->rx, &cfg);
    m->rx.sq_thresh = m->config.squelch;
    m->backend_active = 1U;
}

void psk_modem_init(psk_modem_t *m, const psk_modem_config_t *config)
{
    memset(m, 0, sizeof(*m));
    m->config = *config;
    backend_init(m);
}

void psk_modem_set_mode(psk_modem_t *m, psk_mode_t mode)
{
    m->config.mode = mode;
    backend_init(m);
}

void psk_modem_set_tone(psk_modem_t *m, int tone_hz)
{
    if (tone_hz < 100) {
        tone_hz = 100;
    } else if (tone_hz > 3500) {
        tone_hz = 3500;
    }
    m->config.rx_tone_hz = tone_hz;
    backend_init(m);
}

psk_mode_t psk_modem_mode(const psk_modem_t *m)
{
    return m->config.mode;
}

int psk_modem_tone_hz(const psk_modem_t *m)
{
    return m->config.rx_tone_hz;
}

const char *psk_modem_mode_name(psk_mode_t mode)
{
    return psk_mode_name(mode);
}

const psk_mode_info_t *psk_modem_mode_info(const psk_modem_t *m)
{
    return psk_mode_info(m->config.mode);
}

int psk_modem_mode_supported(psk_mode_t mode)
{
    return psk_mode_can_decode(mode);
}

void psk_modem_debug_snapshot(const psk_modem_t *m, psk_modem_debug_t *debug)
{
    if (!debug) {
        return;
    }

    debug->symbols = m->rx.debug_symbols;
    debug->bits = m->rx.debug_bits;
    debug->ones = m->rx.debug_ones;
    debug->zeros = m->rx.debug_zeros;
    debug->delims = m->rx.debug_delims;
    debug->chars = m->rx.debug_chars;
    debug->unknown = m->rx.debug_unknown;
    debug->sq_open = m->rx.sq_open;
    debug->sq_level = m->rx.sq_level;
    debug->sq_thresh = m->rx.sq_thresh;
    debug->stable_symbols = m->rx.sq_stable_symbols;
    debug->warmup_symbols = m->rx.warmup_symbols;
    debug->tone_hz = m->backend_active ? m->rx.config.rx_tone_hz : m->config.rx_tone_hz;
}

size_t psk_modem_process(psk_modem_t *m, const int16_t *samples, size_t count,
                         char *out, size_t out_size)
{
    if (out_size == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!m->backend_active) {
        return 0;
    }
    m->out = out;
    m->out_size = out_size;
    m->out_len = 0;
    psk_rtty_rx_feed(&m->rx, samples, count);
    m->out = NULL;
    return m->out_len;
}
