#include "psk_rtty.h"

#include <math.h>
#include <string.h>

#include "PSKTables.h"
#include "pskcore_rx_tables.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TWO_PI 6.28318530717958647692f
#define PSKCORE_TS 0.032f
#define PSKCORE_AFC_OFF 0
#define PSKCORE_AFC_ON 1
#define PSKCORE_AFC_TIMELIMIT 10
#define PSKCORE_NLP_K 100.0f
#define PHZ_180_BMIN 0.0f
#define PHZ_180_BMAX ((float)M_PI)
#define PHZ_0_BMIN ((float)M_PI)
#define PHZ_0_BMAX TWO_PI
#define PHZ_ERR_SIGN_LIMIT 5
#define SQ_EMIT_STABLE_SYMBOLS 10
#define PSK_FAMILY_BPSK 0U
#define PSK_FAMILY_QPSK 1U
#define PSK_FAMILY_8PSK 2U
#define MAX_PSK_CARRIERS 12U
#define FEC_PUNCTURE 128U
#define VIT_MAX_TRACEBACK 64U
#define VIT_CHUNKSIZE 8U

static unsigned char s_varicode_decode[2048];
static unsigned char s_mfsk_decode[4096];
static int s_tables_ready;

static const unsigned char s_rtty_table[0x20][2] = {
    {0, 0}, {'E', '3'}, {'\n', '\n'}, {'A', '-'}, {' ', ' '}, {'S', '\''}, {'I', '8'}, {'U', '7'},
    {'\r', '\r'}, {'D', '$'}, {'R', '4'}, {'J', 7}, {'N', ','}, {'F', '!'}, {'C', ':'}, {'K', '('},
    {'T', '5'}, {'Z', '"'}, {'L', ')'}, {'W', '2'}, {'H', '#'}, {'Y', '6'}, {'P', '0'}, {'Q', '1'},
    {'O', '9'}, {'B', '?'}, {'G', '&'}, {0, 0}, {'M', '.'}, {'X', '/'}, {0, 0}, {0, 0},
};

static const uint8_t s_gray_8psk_softbits[8][3] = {
    {25, 0, 25}, {0, 25, 230}, {0, 230, 230}, {25, 255, 25},
    {230, 255, 25}, {255, 230, 230}, {255, 25, 230}, {230, 0, 25},
};

static const unsigned short s_mfsk_varidecode[256] = {
    0x75C,0x760,0x768,0x76C,0x770,0x774,0x778,0x77C,0x0A8,0x780,0x7A0,0x7A8,0x7AC,0x0AC,0x7B0,0x7B4,
    0x7B8,0x7BC,0x7C0,0x7D0,0x7D4,0x7D8,0x7DC,0x7E0,0x7E8,0x7EC,0x7F0,0x7F4,0x7F8,0x7FC,0x800,0xA00,
    0x004,0x1C0,0x1FC,0x2D8,0x2A8,0x2A0,0x200,0x1BC,0x1F4,0x1F0,0x2B4,0x1E0,0x0A0,0x1D8,0x1D4,0x1E8,
    0x0E0,0x0F0,0x140,0x154,0x174,0x160,0x16C,0x1A0,0x180,0x1AC,0x1EC,0x1F8,0x2C0,0x1DC,0x2BC,0x1D0,
    0x280,0x0BC,0x100,0x0D4,0x0DC,0x0B8,0x0F8,0x150,0x158,0x0C0,0x1B4,0x17C,0x0F4,0x0E8,0x0FC,0x0D0,
    0x0EC,0x1B0,0x0D8,0x0B4,0x0B0,0x15C,0x1A8,0x168,0x170,0x178,0x1B8,0x2E8,0x2D0,0x2EC,0x2D4,0x2B0,
    0x2AC,0x014,0x060,0x038,0x034,0x008,0x050,0x058,0x030,0x018,0x080,0x070,0x02C,0x040,0x01C,0x010,
    0x054,0x078,0x020,0x028,0x00C,0x03C,0x06C,0x068,0x074,0x05C,0x07C,0x2DC,0x2B8,0x2E0,0x2F0,0xA80,
    0xAA0,0xAA8,0xAAC,0xAB0,0xAB4,0xAB8,0xABC,0xAC0,0xAD0,0xAD4,0xAD8,0xADC,0xAE0,0xAE8,0xAEC,0xAF0,
    0xAF4,0xAF8,0xAFC,0xB00,0xB40,0xB50,0xB54,0xB58,0xB5C,0xB60,0xB68,0xB6C,0xB70,0xB74,0xB78,0xB7C,
    0x2F4,0x2F8,0x2FC,0x300,0x340,0x350,0x354,0x358,0x35C,0x360,0x368,0x36C,0x370,0x374,0x378,0x37C,
    0x380,0x3A0,0x3A8,0x3AC,0x3B0,0x3B4,0x3B8,0x3BC,0x3C0,0x3D0,0x3D4,0x3D8,0x3DC,0x3E0,0x3E8,0x3EC,
    0x3F0,0x3F4,0x3F8,0x3FC,0x400,0x500,0x540,0x550,0x554,0x558,0x55C,0x560,0x568,0x56C,0x570,0x574,
    0x578,0x57C,0x580,0x5A0,0x5A8,0x5AC,0x5B0,0x5B4,0x5B8,0x5BC,0x5C0,0x5D0,0x5D4,0x5D8,0x5DC,0x5E0,
    0x5E8,0x5EC,0x5F0,0x5F4,0x5F8,0x5FC,0x600,0x680,0x6A0,0x6A8,0x6AC,0x6B0,0x6B4,0x6B8,0x6BC,0x6C0,
    0x6D0,0x6D4,0x6D8,0x6DC,0x6E0,0x6E8,0x6EC,0x6F0,0x6F4,0x6F8,0x6FC,0x700,0x740,0x750,0x754,0x758
};

typedef struct {
    uint8_t family;
    uint8_t bits_per_symbol;
    uint8_t carriers;
    uint8_t fec;
    uint8_t punctured;
    uint8_t mfsk_varicode;
    uint8_t fec_k;
    uint16_t interleave_depth;
    uint32_t fec_poly1;
    uint32_t fec_poly2;
    float baud;
} psk_mode_params_t;

static void init_tables(void)
{
    if (s_tables_ready) {
        return;
    }
    memset(s_varicode_decode, 0, sizeof(s_varicode_decode));
    memset(s_mfsk_decode, 0, sizeof(s_mfsk_decode));
    for (int ch = 0; ch < 256; ++ch) {
        const unsigned int code = VARICODE_TABLE[ch];
        if (code < sizeof(s_varicode_decode)) {
            s_varicode_decode[code] = (unsigned char)ch;
        }
        if (s_mfsk_varidecode[ch] < sizeof(s_mfsk_decode)) {
            s_mfsk_decode[s_mfsk_varidecode[ch]] = (unsigned char)ch;
        }
    }
    s_tables_ready = 1;
}

static psk_mode_params_t mode_params(psk_rtty_mode_t mode)
{
    psk_mode_params_t p = {
        .family = PSK_FAMILY_BPSK,
        .bits_per_symbol = 1,
        .carriers = 1,
        .fec = 0,
        .punctured = 0,
        .mfsk_varicode = 0,
        .fec_k = 0,
        .interleave_depth = 0,
        .fec_poly1 = 0,
        .fec_poly2 = 0,
        .baud = 31.25f,
    };
    switch (mode) {
    case PSK_RTTY_MODE_PSK63:
        p.baud = 62.5f;
        break;
    case PSK_RTTY_MODE_BPSK63F:
        p.baud = 62.5f;
        p.fec = 1;
        p.mfsk_varicode = 1;
        p.fec_k = 7;
        p.fec_poly1 = 0x6dU;
        p.fec_poly2 = 0x4fU;
        break;
    case PSK_RTTY_MODE_BPSK125:
        p.baud = 125.0f;
        break;
    case PSK_RTTY_MODE_BPSK250:
        p.baud = 250.0f;
        break;
    case PSK_RTTY_MODE_BPSK500:
        p.baud = 500.0f;
        break;
    case PSK_RTTY_MODE_BPSK1000:
        p.baud = 1000.0f;
        break;
    case PSK_RTTY_MODE_12XPSK125:
        p.baud = 125.0f;
        p.carriers = 12;
        break;
    case PSK_RTTY_MODE_6XPSK250:
        p.baud = 250.0f;
        p.carriers = 6;
        break;
    case PSK_RTTY_MODE_2XPSK500:
        p.baud = 500.0f;
        p.carriers = 2;
        break;
    case PSK_RTTY_MODE_4XPSK500:
        p.baud = 500.0f;
        p.carriers = 4;
        break;
    case PSK_RTTY_MODE_2XPSK800:
        p.baud = 800.0f;
        p.carriers = 2;
        break;
    case PSK_RTTY_MODE_2XPSK1000:
        p.baud = 1000.0f;
        p.carriers = 2;
        break;
    case PSK_RTTY_MODE_QPSK31_USB:
    case PSK_RTTY_MODE_QPSK31_LSB:
    case PSK_RTTY_MODE_QPSK63:
    case PSK_RTTY_MODE_QPSK125:
    case PSK_RTTY_MODE_QPSK250:
    case PSK_RTTY_MODE_QPSK500:
        p.family = PSK_FAMILY_QPSK;
        p.bits_per_symbol = 2;
        p.fec = 1;
        p.fec_k = 5;
        p.fec_poly1 = 0x17U;
        p.fec_poly2 = 0x19U;
        if (mode == PSK_RTTY_MODE_QPSK63) {
            p.baud = 62.5f;
        } else if (mode == PSK_RTTY_MODE_QPSK125) {
            p.baud = 125.0f;
        } else if (mode == PSK_RTTY_MODE_QPSK250) {
            p.baud = 250.0f;
        } else if (mode == PSK_RTTY_MODE_QPSK500) {
            p.baud = 500.0f;
        }
        break;
    case PSK_RTTY_MODE_8PSK125:
    case PSK_RTTY_MODE_8PSK250:
    case PSK_RTTY_MODE_8PSK500:
    case PSK_RTTY_MODE_8PSK1000:
    case PSK_RTTY_MODE_8PSK125F:
    case PSK_RTTY_MODE_8PSK250F:
    case PSK_RTTY_MODE_8PSK500F:
    case PSK_RTTY_MODE_8PSK1000F:
    case PSK_RTTY_MODE_8PSK1200F:
        p.family = PSK_FAMILY_8PSK;
        p.bits_per_symbol = 3;
        p.mfsk_varicode = 1;
        if (mode == PSK_RTTY_MODE_8PSK250 || mode == PSK_RTTY_MODE_8PSK250F) {
            p.baud = 250.0f;
        } else if (mode == PSK_RTTY_MODE_8PSK500 || mode == PSK_RTTY_MODE_8PSK500F) {
            p.baud = 500.0f;
        } else if (mode == PSK_RTTY_MODE_8PSK1000 || mode == PSK_RTTY_MODE_8PSK1000F) {
            p.baud = 1000.0f;
        } else if (mode == PSK_RTTY_MODE_8PSK1200F) {
            p.baud = 16000.0f / 13.0f;
        } else {
            p.baud = 125.0f;
        }
        if (mode >= PSK_RTTY_MODE_8PSK125F) {
            p.fec = 1;
            if (mode == PSK_RTTY_MODE_8PSK125F) {
                p.fec_k = 16;
                p.fec_poly1 = 0152711U;
                p.fec_poly2 = 0126723U;
                p.interleave_depth = 384;
            } else if (mode == PSK_RTTY_MODE_8PSK250F) {
                p.fec_k = 16;
                p.fec_poly1 = 0152711U;
                p.fec_poly2 = 0126723U;
                p.interleave_depth = 512;
            } else if (mode == PSK_RTTY_MODE_8PSK500F) {
                p.fec_k = 13;
                p.fec_poly1 = 016461U;
                p.fec_poly2 = 012767U;
                p.interleave_depth = 640;
                p.punctured = 1;
            } else {
                p.fec_k = 7;
                p.fec_poly1 = 0x6dU;
                p.fec_poly2 = 0x4fU;
                p.interleave_depth = 512;
                p.punctured = 1;
            }
        }
        break;
    case PSK_RTTY_MODE_RTTY:
        p.baud = 45.45f;
        break;
    case PSK_RTTY_MODE_BPSK31:
    default:
        break;
    }
    return p;
}

static float symbol_rate_for_mode(psk_rtty_mode_t mode)
{
    return mode_params(mode).baud;
}

static void reset_runtime(psk_rtty_rx_t *rx)
{
    const psk_mode_params_t params = mode_params(rx->config.mode);
    rx->nco_phase = 0.0f;
    rx->nco_phase_inc = TWO_PI * (float)rx->config.rx_tone_hz / (float)rx->config.sample_rate_hz;
    rx->nco_cos = 1.0f;
    rx->nco_sin = 0.0f;
    rx->freq_error = 0.0f;
    rx->afc_limit = 50.0f * TWO_PI / (float)rx->config.sample_rate_hz;
    rx->afc_min = rx->nco_phase_inc - rx->afc_limit;
    rx->afc_max = rx->nco_phase_inc + rx->afc_limit;
    if (rx->afc_min < 0.0f) {
        rx->afc_min = 0.0f;
    }
    rx->fferr_ave = 0.0f;
    rx->fperr_ave = 0.0f;
    rx->q_freq_error = 0.0f;
    rx->nlp_k = PSKCORE_NLP_K;
    rx->agc_ave = 0.0f;
    rx->dev_ave = 90.0f;
    rx->bit_phase_inc = 16.0f / (float)rx->config.sample_rate_hz;
    rx->bit_phase_pos = 0.0f;
    rx->prev_symbol_phase = 0.0f;
    rx->symbol_clock = 0.0f;
    rx->symbol_period_samples = (float)rx->config.sample_rate_hz / params.baud;
    rx->i_acc = 0.0f;
    rx->q_acc = 0.0f;
    rx->samples_in_symbol = 0;
    rx->sample_count = 0;
    rx->dec1_state = PSKCORE_DEC4_LPFIR_LENGTH - 1;
    rx->dec2_state = PSKCORE_DEC4_LPFIR_LENGTH - 1;
    rx->bit_fir_state = PSKCORE_BITFIR_LENGTH - 1;
    rx->pk_pos = 0;
    rx->new_pk_pos = 5;
    rx->raw_pk_pos = 5;
    rx->bit_pos = 0;
    rx->warmup_symbols = (int)symbol_rate_for_mode(rx->config.mode);
    if (rx->warmup_symbols < 32) {
        rx->warmup_symbols = 32;
    }
    rx->last_pk_pos = 0;
    rx->sq_open = 0;
    rx->sq_was_open = 0;
    rx->sq_stable_symbols = 0;
    rx->sq_level = 10;
    rx->sq_thresh = 35;
    rx->squelch_speed = 75;
    rx->on_count = 0;
    rx->off_count = 0;
    rx->p_count = 0;
    rx->n_count = 0;
    rx->bit_acc = 0;
    rx->last_bit_zero = 0;
    rx->rtty_figures = 0;
    rx->rtty_bit_index = 0;
    rx->rtty_shift_reg = 0;
    rx->i0 = 0.0f;
    rx->i1 = 0.0f;
    rx->q0 = 0.0f;
    rx->q1 = 0.0f;
    rx->carriers = params.carriers;
    rx->bits_per_symbol = params.bits_per_symbol;
    rx->psk_family = params.family;
    rx->fec_enabled = params.fec;
    rx->fec_punctured = params.punctured;
    rx->use_mfsk_varicode = params.mfsk_varicode;
    rx->fec_pair_count = 0;
    rx->fec_rxbitstate = 0;
    rx->fec_bit_phase = 0;
    rx->fec_pair_len = 0;
    rx->fec_k = params.fec_k;
    rx->fec_state = 0;
    rx->fec_mask = params.fec_k ? ((1UL << params.fec_k) - 1UL) : 0UL;
    rx->fec_poly1 = params.fec_poly1;
    rx->fec_poly2 = params.fec_poly2;
    rx->vit_traceback = params.fec_k ? (uint8_t)(params.fec_k * 12U) : 0U;
    if (rx->vit_traceback > VIT_MAX_TRACEBACK) {
        rx->vit_traceback = VIT_MAX_TRACEBACK;
    }
    rx->vit_chunksize = VIT_CHUNKSIZE;
    rx->vit_step = 0;
    rx->inlv_depth = params.interleave_depth;
    rx->inlv_len = (uint16_t)(params.interleave_depth * 4U);
    if (rx->inlv_len > sizeof(rx->inlv_table)) {
        rx->inlv_len = sizeof(rx->inlv_table);
    }
    memset(rx->fec_pending_bits, 0, sizeof(rx->fec_pending_bits));
    memset(rx->fec_pair_buf, 0, sizeof(rx->fec_pair_buf));
    memset(rx->inlv_table, FEC_PUNCTURE, sizeof(rx->inlv_table));
    memset(rx->vit_decisions, 0, sizeof(rx->vit_decisions));
    if (params.fec_k) {
        const uint16_t nstates = (uint16_t)(1UL << (params.fec_k - 1U));
        for (uint16_t i = 0; i < nstates; ++i) {
            rx->vit_metrics_a[i] = 0;
            rx->vit_metrics_b[i] = 0;
        }
    }
    memset(rx->carrier_i_acc, 0, sizeof(rx->carrier_i_acc));
    memset(rx->carrier_q_acc, 0, sizeof(rx->carrier_q_acc));
    memset(rx->carrier_i_prev, 0, sizeof(rx->carrier_i_prev));
    memset(rx->carrier_q_prev, 0, sizeof(rx->carrier_q_prev));
    memset(rx->carrier_has_prev, 0, sizeof(rx->carrier_has_prev));
    const float spacing = 1.4f * params.baud;
    const float first = (float)rx->config.rx_tone_hz + (((-1.0f * (float)params.carriers) + 1.0f) * spacing * 0.5f);
    for (uint8_t car = 0; car < MAX_PSK_CARRIERS; ++car) {
        const float freq = first + (float)car * spacing;
        rx->carrier_phase_inc[car] = TWO_PI * freq / (float)rx->config.sample_rate_hz;
        rx->carrier_rot_cos[car] = cosf(rx->carrier_phase_inc[car]);
        rx->carrier_rot_sin[car] = sinf(rx->carrier_phase_inc[car]);
        rx->carrier_cos[car] = 1.0f;
        rx->carrier_sin[car] = 0.0f;
    }
    memset(rx->sync_ave, 0, sizeof(rx->sync_ave));
    memset(rx->dec1_queue, 0, sizeof(rx->dec1_queue));
    memset(rx->dec2_queue, 0, sizeof(rx->dec2_queue));
    memset(rx->bit_queue, 0, sizeof(rx->bit_queue));
    memset(&rx->freq_signal, 0, sizeof(rx->freq_signal));
    memset(&rx->bit_signal, 0, sizeof(rx->bit_signal));
    memset(&rx->z1, 0, sizeof(rx->z1));
    memset(&rx->z2, 0, sizeof(rx->z2));
}

void psk_rtty_rx_init(psk_rtty_rx_t *rx, const psk_rtty_config_t *config)
{
    init_tables();
    memset(rx, 0, sizeof(*rx));
    rx->config = *config;
    reset_runtime(rx);
}

void psk_rtty_rx_set_mode(psk_rtty_rx_t *rx, psk_rtty_mode_t mode)
{
    if (mode >= PSK_RTTY_MODE_COUNT) {
        return;
    }
    rx->config.mode = mode;
    reset_runtime(rx);
}

void psk_rtty_rx_set_tone(psk_rtty_rx_t *rx, int tone_hz)
{
    if (tone_hz < 100) {
        tone_hz = 100;
    } else if (tone_hz > 3500) {
        tone_hz = 3500;
    }
    rx->config.rx_tone_hz = tone_hz;
    reset_runtime(rx);
}

static void emit(psk_rtty_rx_t *rx, char c)
{
    if (c && rx->config.char_cb) {
        rx->config.char_cb(c, rx->config.userdata);
    }
}

static void push_varicode_bit(psk_rtty_rx_t *rx, int bit)
{
    if (bit == 0) {
        if (rx->last_bit_zero) {
            rx->debug_delims++;
            if (rx->sq_open && rx->sq_stable_symbols >= SQ_EMIT_STABLE_SYMBOLS && rx->bit_acc < 2048) {
                const unsigned char ch = s_varicode_decode[rx->bit_acc];
                if (ch != 0) {
                    rx->debug_chars++;
                    emit(rx, (char)ch);
                } else if (rx->bit_acc != 0) {
                    rx->debug_unknown++;
                }
            }
            rx->bit_acc = 0;
            rx->last_bit_zero = 0;
        } else {
            rx->last_bit_zero = 1;
        }
        return;
    }

    if (rx->last_bit_zero) {
        rx->bit_acc <<= 1;
        rx->last_bit_zero = 0;
    }
    rx->bit_acc = (uint16_t)(((rx->bit_acc << 1) | 1U) & 0x07ffU);
}

static void push_mfsk_varicode_bit(psk_rtty_rx_t *rx, int bit)
{
    rx->bit_acc = (uint16_t)(((rx->bit_acc << 1) | (bit ? 1U : 0U)) & 0x0fffU);
    if ((rx->bit_acc & 0x07U) == 1U) {
        const uint16_t code = rx->bit_acc >> 1;
        const unsigned char ch = code < sizeof(s_mfsk_decode) ? s_mfsk_decode[code] : 0U;
        rx->debug_delims++;
        if (rx->sq_open && rx->sq_stable_symbols >= SQ_EMIT_STABLE_SYMBOLS) {
            if (ch != 0U) {
                rx->debug_chars++;
                emit(rx, (char)ch);
            } else if (code != 0U) {
                rx->debug_unknown++;
            }
        }
        rx->bit_acc = 1U;
    }
}

static void push_payload_bit(psk_rtty_rx_t *rx, int bit)
{
    if (rx->use_mfsk_varicode) {
        push_mfsk_varicode_bit(rx, bit);
    } else {
        push_varicode_bit(rx, bit);
    }
}

static uint8_t parity32(uint32_t value)
{
    value ^= value >> 16;
    value ^= value >> 8;
    value ^= value >> 4;
    value &= 0x0fU;
    return (uint8_t)((0x6996U >> value) & 1U);
}

static void fec_deinterleave_pair(psk_rtty_rx_t *rx, uint8_t pair[2])
{
    if (rx->inlv_depth == 0U || rx->inlv_len < 4U) {
        return;
    }

    for (uint16_t k = 0; k < rx->inlv_depth; ++k) {
        const uint16_t base = (uint16_t)(k * 4U);
        rx->inlv_table[base + 0U] = rx->inlv_table[base + 1U];
        rx->inlv_table[base + 1U] = pair[0];
        rx->inlv_table[base + 2U] = rx->inlv_table[base + 3U];
        rx->inlv_table[base + 3U] = pair[1];
        pair[0] = rx->inlv_table[base + 0U];
        pair[1] = rx->inlv_table[base + 3U];
    }
}

static int branch_metric(uint8_t expected, uint8_t b0, uint8_t b1)
{
    int metric = 0;
    const uint8_t e0 = expected & 1U;
    const uint8_t e1 = (expected >> 1U) & 1U;
    if (b0 != FEC_PUNCTURE) {
        metric += e0 ? ((int)b0 - 128) : (128 - (int)b0);
    }
    if (b1 != FEC_PUNCTURE) {
        metric += e1 ? ((int)b1 - 128) : (128 - (int)b1);
    }
    return metric;
}

static int vit_get_decision(const psk_rtty_rx_t *rx, uint8_t step, uint16_t state)
{
    return (rx->vit_decisions[step][state >> 3U] >> (state & 7U)) & 1U;
}

static void vit_set_decision(psk_rtty_rx_t *rx, uint8_t step, uint16_t state, int decision)
{
    uint8_t *slot = &rx->vit_decisions[step][state >> 3U];
    const uint8_t mask = (uint8_t)(1U << (state & 7U));
    if (decision) {
        *slot |= mask;
    } else {
        *slot &= (uint8_t)~mask;
    }
}

static uint8_t vit_traceback(psk_rtty_rx_t *rx, uint16_t nstates, const int16_t *metrics)
{
    int16_t best_metric = metrics[0];
    uint16_t state = 0;
    for (uint16_t i = 1; i < nstates; ++i) {
        if (metrics[i] > best_metric) {
            best_metric = metrics[i];
            state = i;
        }
    }

    uint8_t step = (uint8_t)((rx->vit_step + VIT_MAX_TRACEBACK - 1U) % VIT_MAX_TRACEBACK);
    rx->vit_sequence[rx->vit_traceback] = state;
    for (int i = (int)rx->vit_traceback - 1; i >= 0; --i) {
        const int decision = vit_get_decision(rx, step, state);
        state = (uint16_t)(((uint32_t)state + (decision ? (uint32_t)nstates : 0UL)) >> 1U);
        rx->vit_sequence[i] = state;
        step = (uint8_t)((step + VIT_MAX_TRACEBACK - 1U) % VIT_MAX_TRACEBACK);
    }

    uint8_t out = 0;
    for (uint8_t i = 0; i < rx->vit_chunksize; ++i) {
        out = (uint8_t)((out << 1U) | (rx->vit_sequence[i] & 1U));
    }
    return out;
}

static int fec_decode_pair(psk_rtty_rx_t *rx, uint8_t b0, uint8_t b1, uint8_t *decoded)
{
    if (!rx->fec_k || rx->fec_k > 16U) {
        return 0;
    }

    const uint16_t nstates = (uint16_t)(1UL << (rx->fec_k - 1U));
    int16_t *prev = (rx->vit_step & 1U) ? rx->vit_metrics_b : rx->vit_metrics_a;
    int16_t *curr = (rx->vit_step & 1U) ? rx->vit_metrics_a : rx->vit_metrics_b;
    int16_t max_metric = -32768;

    memset(rx->vit_decisions[rx->vit_step], 0, (size_t)nstates >> 3U);
    for (uint16_t state = 0; state < nstates; ++state) {
        const uint32_t s0 = state;
        const uint32_t s1 = (uint32_t)state + nstates;
        const uint16_t p0 = (uint16_t)(s0 >> 1U);
        const uint16_t p1 = (uint16_t)(s1 >> 1U);
        const uint8_t o0 = (uint8_t)(parity32(s0 & rx->fec_poly1) |
                                     (parity32(s0 & rx->fec_poly2) << 1U));
        const uint8_t o1 = (uint8_t)(parity32(s1 & rx->fec_poly1) |
                                     (parity32(s1 & rx->fec_poly2) << 1U));
        const int16_t m0 = (int16_t)(prev[p0] + branch_metric(o0, b0, b1));
        const int16_t m1 = (int16_t)(prev[p1] + branch_metric(o1, b0, b1));
        if (m0 > m1) {
            curr[state] = m0;
            vit_set_decision(rx, rx->vit_step, state, 0);
        } else {
            curr[state] = m1;
            vit_set_decision(rx, rx->vit_step, state, 1);
        }
        if (curr[state] > max_metric) {
            max_metric = curr[state];
        }
    }

    if (max_metric > 12000) {
        for (uint16_t i = 0; i < nstates; ++i) {
            curr[i] = (int16_t)(curr[i] - max_metric);
        }
    }

    rx->vit_step = (uint8_t)((rx->vit_step + 1U) % VIT_MAX_TRACEBACK);
    rx->fec_pair_count++;
    if ((rx->fec_pair_count % rx->vit_chunksize) == 0U &&
        rx->fec_pair_count > rx->vit_traceback) {
        *decoded = vit_traceback(rx, nstates, curr);
        return 1;
    }
    return 0;
}

static void push_fec_encoded_bit(psk_rtty_rx_t *rx, uint8_t bit)
{
    rx->fec_pair_buf[rx->fec_pair_len++] = bit;
    if (rx->fec_pair_len < 2U) {
        return;
    }

    uint8_t pair[2] = {rx->fec_pair_buf[0], rx->fec_pair_buf[1]};
    rx->fec_pair_len = 0;
    if (rx->inlv_depth != 0U) {
        const uint8_t tmp_in = pair[0];
        pair[0] = pair[1];
        pair[1] = tmp_in;
        fec_deinterleave_pair(rx, pair);
        const uint8_t tmp_out = pair[0];
        pair[0] = pair[1];
        pair[1] = tmp_out;
    }
    uint8_t decoded = 0;
    if (fec_decode_pair(rx, pair[0], pair[1], &decoded)) {
        for (int bit = 7; bit >= 0; --bit) {
            push_payload_bit(rx, (decoded >> bit) & 1U);
        }
    }
}

static void push_rtty_bit(psk_rtty_rx_t *rx, int bit)
{
    if (rx->rtty_bit_index == 0) {
        if (bit == 0) {
            rx->rtty_shift_reg = 0;
            rx->rtty_bit_index = 1;
        }
        return;
    }
    if (rx->rtty_bit_index <= 5) {
        rx->rtty_shift_reg >>= 1;
        if (bit) {
            rx->rtty_shift_reg |= 0x10;
        }
        rx->rtty_bit_index++;
        return;
    }

    if (bit) {
        const uint8_t code = rx->rtty_shift_reg & 0x1f;
        if (code == 0x1b) {
            rx->rtty_figures = 1;
        } else if (code == 0x1f) {
            rx->rtty_figures = 0;
        } else {
            emit(rx, (char)s_rtty_table[code][rx->rtty_figures]);
        }
    }
    rx->rtty_bit_index = 0;
}

static void finish_symbol(psk_rtty_rx_t *rx)
{
    if (rx->samples_in_symbol == 0) {
        return;
    }

    const float phase = atan2f(rx->q_acc, rx->i_acc);
    float diff = phase - rx->prev_symbol_phase;
    while (diff > (float)M_PI) {
        diff -= TWO_PI;
    }
    while (diff < -(float)M_PI) {
        diff += TWO_PI;
    }
    rx->prev_symbol_phase = phase;

    if (rx->config.mode == PSK_RTTY_MODE_RTTY) {
        push_rtty_bit(rx, rx->i_acc > 0.0f ? 1 : 0);
    } else {
        const int bit = fabsf(diff) > ((float)M_PI * 0.5f) ? 0 : 1;
        push_varicode_bit(rx, bit);
    }

    rx->i_acc = 0.0f;
    rx->q_acc = 0.0f;
    rx->samples_in_symbol = 0;
}

static void finish_simple_bpsk_symbol(psk_rtty_rx_t *rx)
{
    if (rx->samples_in_symbol == 0) {
        return;
    }

    const float i = rx->i_acc;
    const float q = rx->q_acc;
    const float mag = sqrtf(i * i + q * q) / (float)rx->samples_in_symbol;

    if (mag > rx->agc_ave) {
        rx->agc_ave = (1.0f - 1.0f / 50.0f) * rx->agc_ave + (1.0f / 50.0f) * mag;
    } else {
        rx->agc_ave = (1.0f - 1.0f / 200.0f) * rx->agc_ave + (1.0f / 200.0f) * mag;
    }
    rx->sq_level = rx->agc_ave > 50.0f ? 99 : 0;
    rx->sq_open = rx->sq_level >= rx->sq_thresh;
    if (rx->sq_open && rx->sq_stable_symbols < SQ_EMIT_STABLE_SYMBOLS) {
        rx->sq_stable_symbols++;
    }

    if (rx->debug_symbols > 0) {
        const float dot = rx->i0 * i + rx->q0 * q;
        const int bit = dot >= 0.0f;

        if (rx->warmup_symbols > 0) {
            rx->warmup_symbols--;
        } else {
            rx->debug_bits++;
            if (bit) {
                rx->debug_ones++;
            } else {
                rx->debug_zeros++;
            }
            push_varicode_bit(rx, bit);
        }
    }

    rx->i0 = i;
    rx->q0 = q;
    rx->debug_symbols++;
    rx->i_acc = 0.0f;
    rx->q_acc = 0.0f;
    rx->samples_in_symbol = 0;
}

static psk_rtty_complex_t fir_complex(const psk_rtty_complex_t *queue, int newest, int length, const float *coef)
{
    psk_rtty_complex_t acc = {0.0f, 0.0f};
    for (int i = 0; i < length; ++i) {
        const int coef_idx = (length - newest + i) % length;
        acc.x += queue[i].x * coef[coef_idx];
        acc.y += queue[i].y * coef[coef_idx];
    }
    return acc;
}

static void calc_bit_filter(psk_rtty_rx_t *rx, psk_rtty_complex_t sample)
{
    rx->bit_queue[rx->bit_fir_state] = sample;
    rx->freq_signal = fir_complex(rx->bit_queue, rx->bit_fir_state, PSKCORE_BITFIR_LENGTH, s_freq_fir);
    rx->bit_signal = fir_complex(rx->bit_queue, rx->bit_fir_state, PSKCORE_BITFIR_LENGTH, s_bit_fir);
    if (--rx->bit_fir_state < 0) {
        rx->bit_fir_state = PSKCORE_BITFIR_LENGTH - 1;
    }
}

static void calc_agc(psk_rtty_rx_t *rx)
{
    const float mag = sqrtf(rx->freq_signal.x * rx->freq_signal.x + rx->freq_signal.y * rx->freq_signal.y);
    if (mag > rx->agc_ave) {
        rx->agc_ave = (1.0f - 1.0f / 200.0f) * rx->agc_ave + (1.0f / 200.0f) * mag;
    } else {
        rx->agc_ave = (1.0f - 1.0f / 500.0f) * rx->agc_ave + (1.0f / 500.0f) * mag;
    }

    if (rx->agc_ave >= 1.0f) {
        rx->bit_signal.x /= rx->agc_ave;
        rx->bit_signal.y /= rx->agc_ave;
        rx->freq_signal.x /= rx->agc_ave;
        rx->freq_signal.y /= rx->agc_ave;
    }
}

static void calc_freq_error(psk_rtty_rx_t *rx)
{
    const float p_gn = 0.001f;
    const float i_gn = 1.5e-6f;
    const float p_cgn = 0.0004f;
    const float i_cgn = 3.0e-6f;
    const float wide_gn = 1.0f / 0.02f;
    const float wlp_k = 200.0f;

    float ferr = (rx->freq_signal.x - rx->z2.x) * rx->z1.y -
                 (rx->freq_signal.y - rx->z2.y) * rx->z1.x;
    rx->z2 = rx->z1;
    rx->z1 = rx->freq_signal;

    if (ferr > 0.30f) {
        ferr = 0.30f;
    } else if (ferr < -0.30f) {
        ferr = -0.30f;
    }

    rx->fferr_ave = (1.0f - 1.0f / wlp_k) * rx->fferr_ave + (wide_gn / wlp_k) * ferr;
    if (rx->afc_timer > 0) {
        ferr = rx->fferr_ave;
        if (ferr > 0.3f || ferr < -0.3f) {
            rx->nco_phase_inc += ferr * i_cgn;
        }
        rx->freq_error = ferr * p_cgn;
    } else {
        ferr = (rx->fferr_ave * rx->fperr_ave > 0.0f) ? rx->fperr_ave : 0.0f;
        if (ferr > 0.3f || ferr < -0.3f) {
            rx->nco_phase_inc += ferr * i_gn;
        }
        rx->freq_error = ferr * p_gn;
    }

    if (rx->nco_phase_inc + rx->freq_error > rx->afc_max) {
        rx->nco_phase_inc = rx->afc_max;
        rx->freq_error = 0.0f;
    } else if (rx->nco_phase_inc + rx->freq_error < rx->afc_min) {
        rx->nco_phase_inc = rx->afc_min;
        rx->freq_error = 0.0f;
    }
}

static int symbol_sync(psk_rtty_rx_t *rx, psk_rtty_complex_t sample)
{
    int trigger = 0;
    int bit_pos = rx->bit_pos;

    if (bit_pos < 16) {
        float energy = sample.x * sample.x + sample.y * sample.y;
        if (energy > 4.0f) {
            energy = 1.0f;
        }
        rx->sync_ave[bit_pos] = (1.0f - 1.0f / 82.0f) * rx->sync_ave[bit_pos] + (1.0f / 82.0f) * energy;
        trigger = (bit_pos == rx->pk_pos);

        if (bit_pos == s_half_tbl[rx->new_pk_pos]) {
            rx->pk_pos = rx->new_pk_pos;
        }
        bit_pos++;
    }

    rx->bit_phase_pos += rx->bit_phase_inc;
    if (rx->bit_phase_pos >= PSKCORE_TS) {
        rx->bit_phase_pos = fmodf(rx->bit_phase_pos, PSKCORE_TS);
        if (bit_pos == 15 && rx->pk_pos == 15) {
            trigger = 1;
        }
        bit_pos = 0;

        float max_energy = -1.0e10f;
        int max_pos = rx->new_pk_pos;
        for (int i = 0; i < 16; ++i) {
            if (rx->sync_ave[i] > max_energy) {
                max_pos = i;
                max_energy = rx->sync_ave[i];
            }
        }
        rx->raw_pk_pos = max_pos;

        if (rx->sq_open) {
            int delta = max_pos - rx->pk_pos;
            if (delta > 8) {
                delta -= 16;
            } else if (delta < -8) {
                delta += 16;
            }
            if (delta > 1) {
                rx->new_pk_pos = (rx->pk_pos + 1) & 0x0f;
            } else if (delta < -1) {
                rx->new_pk_pos = (rx->pk_pos + 15) & 0x0f;
            } else {
                rx->new_pk_pos = max_pos;
            }
        } else {
            rx->new_pk_pos = max_pos;
        }
        rx->last_pk_pos = rx->pk_pos;
    }

    rx->bit_pos = bit_pos;
    return trigger;
}

static void calc_quality_bpsk(psk_rtty_rx_t *rx, float angle)
{
    const float phzderived_gn = 1.0f / 0.2f;
    float temp;
    const int was_open = rx->sq_open;

    if (angle >= PHZ_180_BMIN && angle <= PHZ_180_BMAX) {
        temp = angle - ((float)M_PI / 2.0f);
        rx->q_freq_error = temp;
        temp = 150.0f * fabsf(temp);
        if (temp < rx->dev_ave) {
            rx->dev_ave = (1.0f - 1.0f / rx->squelch_speed) * rx->dev_ave + (1.0f / rx->squelch_speed) * temp;
        } else {
            rx->dev_ave = (1.0f - 1.0f / (rx->squelch_speed * 2.0f)) * rx->dev_ave +
                          (1.0f / (rx->squelch_speed * 2.0f)) * temp;
        }
        if (rx->on_count > 20) {
            rx->dev_ave = 25.0f;
        } else {
            rx->on_count++;
        }
        rx->off_count = 0;
        if (rx->q_freq_error >= 0.0f) {
            rx->p_count++;
            rx->n_count = 0;
        } else {
            rx->n_count++;
            rx->p_count = 0;
        }
        if (rx->p_count < PHZ_ERR_SIGN_LIMIT && rx->n_count < PHZ_ERR_SIGN_LIMIT) {
            rx->q_freq_error = 0.0f;
        }
    } else if (angle >= PHZ_0_BMIN && angle <= PHZ_0_BMAX) {
        temp = angle - (3.0f * (float)M_PI / 2.0f);
        rx->q_freq_error = temp;
        temp = 150.0f * fabsf(temp);
        if (temp < rx->dev_ave) {
            rx->dev_ave = (1.0f - 1.0f / rx->squelch_speed) * rx->dev_ave + (1.0f / rx->squelch_speed) * temp;
        } else {
            rx->dev_ave = (1.0f - 1.0f / (rx->squelch_speed * 2.0f)) * rx->dev_ave +
                          (1.0f / (rx->squelch_speed * 2.0f)) * temp;
        }
        if (rx->off_count > 20) {
            rx->dev_ave = 100.0f;
        } else {
            rx->off_count++;
        }
        rx->on_count = 0;
        if (rx->q_freq_error >= 0.0f) {
            rx->p_count++;
            rx->n_count = 0;
        } else {
            rx->n_count++;
            rx->p_count = 0;
        }
        if (rx->p_count < PHZ_ERR_SIGN_LIMIT && rx->n_count < PHZ_ERR_SIGN_LIMIT) {
            rx->q_freq_error = 0.0f;
        }
    }

    if (rx->agc_ave > 10.0f) {
        rx->sq_level = 100 - (int)rx->dev_ave;
        rx->sq_open = rx->sq_level >= rx->sq_thresh;
    } else {
        rx->sq_level = 0;
        rx->sq_open = 0;
    }
    if (rx->sq_open) {
        if (!was_open) {
            rx->bit_acc = 0;
            rx->last_bit_zero = 0;
            rx->sq_stable_symbols = 0;
        } else if (rx->sq_stable_symbols < SQ_EMIT_STABLE_SYMBOLS) {
            rx->sq_stable_symbols++;
        }
    } else {
        rx->bit_acc = 0;
        rx->last_bit_zero = 0;
        rx->sq_stable_symbols = 0;
    }
    rx->sq_was_open = rx->sq_open;

    if (rx->q_freq_error > 1.0f) {
        rx->q_freq_error = 1.0f;
    } else if (rx->q_freq_error < -1.0f) {
        rx->q_freq_error = -1.0f;
    }
    rx->fperr_ave = (1.0f - 1.0f / rx->nlp_k) * rx->fperr_ave +
                    (phzderived_gn / rx->nlp_k) * rx->q_freq_error;
}

static void decode_bpsk_symbol(psk_rtty_rx_t *rx, psk_rtty_complex_t sample)
{
    rx->i1 = rx->i0;
    rx->q1 = rx->q0;
    rx->i0 = sample.x;
    rx->q0 = sample.y;

    psk_rtty_complex_t vect = {
        .x = rx->i1 * rx->q0 - rx->i0 * rx->q1,
        .y = rx->i1 * rx->i0 + rx->q1 * rx->q0,
    };
    float angle = (float)M_PI + atan2f(vect.y, vect.x);
    if (angle < 0.0f) {
        angle += TWO_PI;
    } else if (angle >= TWO_PI) {
        angle -= TWO_PI;
    }
    calc_quality_bpsk(rx, angle);

    const int bit = vect.y > 0.0f;
    rx->debug_symbols++;
    if (rx->warmup_symbols > 0) {
        rx->warmup_symbols--;
        return;
    }
    rx->debug_bits++;
    if (bit) {
        rx->debug_ones++;
    } else {
        rx->debug_zeros++;
    }
    push_varicode_bit(rx, bit);
}

static void pskcore_rx_feed(psk_rtty_rx_t *rx, const int16_t *samples, size_t count)
{
    if (rx->afc_timer > 0 && --rx->afc_timer <= 0) {
        rx->afc_timer = 0;
        rx->afc_max = rx->nco_phase_inc + rx->afc_limit;
        rx->afc_min = rx->nco_phase_inc - rx->afc_limit;
        if (rx->afc_min < 0.0f) {
            rx->afc_min = 0.0f;
        }
    }

    const int second_decim = rx->config.mode == PSK_RTTY_MODE_PSK63 ? 8 : 16;
    float rot_inc = rx->nco_phase_inc + rx->freq_error;
    float rot_cos = cosf(rot_inc);
    float rot_sin = sinf(rot_inc);
    for (size_t n = 0; n < count; ++n) {
        const float sample = (float)samples[n];
        rx->dec1_queue[rx->dec1_state].x = sample * rx->nco_cos;
        rx->dec1_queue[rx->dec1_state].y = sample * rx->nco_sin;

        const float next_cos = rx->nco_cos * rot_cos - rx->nco_sin * rot_sin;
        const float next_sin = rx->nco_sin * rot_cos + rx->nco_cos * rot_sin;
        rx->nco_cos = next_cos;
        rx->nco_sin = next_sin;
        if ((rx->sample_count & 0xff) == 0) {
            const float mag = sqrtf(rx->nco_cos * rx->nco_cos + rx->nco_sin * rx->nco_sin);
            if (mag > 0.0f) {
                rx->nco_cos /= mag;
                rx->nco_sin /= mag;
            }
        }

        if ((++rx->sample_count % 4) == 0) {
            rx->dec2_queue[rx->dec2_state] = fir_complex(rx->dec1_queue, rx->dec1_state,
                                                         PSKCORE_DEC4_LPFIR_LENGTH, s_dec4_lpfir);

            if ((rx->sample_count % second_decim) == 0) {
                psk_rtty_complex_t filtered = fir_complex(rx->dec2_queue, rx->dec2_state,
                                                        PSKCORE_DEC4_LPFIR_LENGTH, s_dec4_lpfir);
                calc_bit_filter(rx, filtered);
                calc_agc(rx);
                calc_freq_error(rx);
                rot_inc = rx->nco_phase_inc + rx->freq_error;
                rot_cos = cosf(rot_inc);
                rot_sin = sinf(rot_inc);
                if (symbol_sync(rx, rx->bit_signal)) {
                    decode_bpsk_symbol(rx, rx->bit_signal);
                }
            }

            if (--rx->dec2_state < 0) {
                rx->dec2_state = PSKCORE_DEC4_LPFIR_LENGTH - 1;
            }
        }

        if (--rx->dec1_state < 0) {
            rx->dec1_state = PSKCORE_DEC4_LPFIR_LENGTH - 1;
        }
    }
    rx->sample_count %= 16;
}

static void simple_bpsk_rx_feed(psk_rtty_rx_t *rx, const int16_t *samples, size_t count)
{
    const float phase_inc = TWO_PI * (float)rx->config.rx_tone_hz / (float)rx->config.sample_rate_hz;
    const int initial_skip = (int)(rx->symbol_period_samples * 5.0f);
    const int window_start = (int)(rx->symbol_period_samples * 0.13f);
    const int window_end = (int)(rx->symbol_period_samples * 0.87f);

    for (size_t n = 0; n < count; ++n) {
        const float s = (float)samples[n];
        const float c = cosf(rx->nco_phase);
        const float si = sinf(rx->nco_phase);

        rx->nco_phase += phase_inc;
        if (rx->nco_phase >= TWO_PI) {
            rx->nco_phase -= TWO_PI;
        }

        if (rx->sample_count < initial_skip) {
            rx->sample_count++;
            continue;
        }

        if (rx->bit_pos >= window_start && rx->bit_pos < window_end) {
            rx->i_acc += s * c;
            rx->q_acc -= s * si;
            rx->samples_in_symbol++;
        }
        rx->bit_pos++;

        rx->symbol_clock += 1.0f;
        if (rx->symbol_clock >= rx->symbol_period_samples) {
            rx->symbol_clock -= rx->symbol_period_samples;
            finish_simple_bpsk_symbol(rx);
            rx->bit_pos = 0;
        }
    }
}

static uint8_t phase_index(float angle, uint8_t count)
{
    if (angle < 0.0f) {
        angle += TWO_PI;
    }
    const float step = TWO_PI / (float)count;
    return (uint8_t)(((int)(angle / step + 0.5f)) & (count - 1U));
}

static uint8_t inverse_psk_symbol(const psk_rtty_rx_t *rx, float angle, float dot)
{
    if (rx->psk_family == PSK_FAMILY_BPSK) {
        return dot >= 0.0f ? 2U : 0U;
    }
    if (rx->psk_family == PSK_FAMILY_QPSK) {
        static const uint8_t map[4] = {2U, 3U, 0U, 1U};
        return map[phase_index(angle, 4U)];
    }
    if (rx->fec_enabled) {
        static const uint8_t map_fec[8] = {0U, 4U, 5U, 7U, 6U, 2U, 3U, 1U};
        return map_fec[phase_index(angle, 8U)];
    }
    static const uint8_t map[8] = {4U, 3U, 2U, 1U, 0U, 7U, 6U, 5U};
    return map[phase_index(angle, 8U)];
}

static void process_raw_symbol(psk_rtty_rx_t *rx, uint8_t symbol, uint8_t phase_idx)
{
    rx->debug_symbols++;
    if (rx->warmup_symbols > 0) {
        rx->warmup_symbols--;
        return;
    }

    if (rx->psk_family == PSK_FAMILY_BPSK && !rx->fec_enabled) {
        const int bit = symbol == 2U;
        rx->debug_bits++;
        bit ? rx->debug_ones++ : rx->debug_zeros++;
        push_varicode_bit(rx, bit);
        return;
    }

    if (rx->psk_family == PSK_FAMILY_BPSK) {
        const uint8_t bit = symbol == 2U ? 1U : 0U;
        rx->debug_bits++;
        bit ? rx->debug_ones++ : rx->debug_zeros++;
        push_fec_encoded_bit(rx, bit ? 255U : 0U);
        return;
    }

    if (rx->psk_family == PSK_FAMILY_QPSK) {
        const uint8_t b0 = symbol & 1U;
        const uint8_t b1 = (symbol >> 1U) & 1U;
        rx->debug_bits += 2U;
        rx->debug_ones += b0 + b1;
        rx->debug_zeros += 2U - b0 - b1;
        push_fec_encoded_bit(rx, b0 ? 255U : 0U);
        push_fec_encoded_bit(rx, b1 ? 255U : 0U);
        return;
    }

    const uint8_t b0 = symbol & 1U;
    const uint8_t b1 = (symbol >> 1U) & 1U;
    const uint8_t b2 = (symbol >> 2U) & 1U;
    rx->debug_bits += 3U;
    rx->debug_ones += b0 + b1 + b2;
    rx->debug_zeros += 3U - b0 - b1 - b2;
    if (!rx->fec_enabled) {
        push_mfsk_varicode_bit(rx, b0);
        push_mfsk_varicode_bit(rx, b1);
        push_mfsk_varicode_bit(rx, b2);
        return;
    }
    const uint8_t soft_idx = (uint8_t)((8U - phase_idx) & 7U);
    push_fec_encoded_bit(rx, s_gray_8psk_softbits[soft_idx][2]);
    push_fec_encoded_bit(rx, s_gray_8psk_softbits[soft_idx][1]);
    push_fec_encoded_bit(rx, s_gray_8psk_softbits[soft_idx][0]);
    if (rx->fec_punctured) {
        push_fec_encoded_bit(rx, FEC_PUNCTURE);
    }
}

static void finish_generic_psk_symbol(psk_rtty_rx_t *rx)
{
    int any_signal = 0;
    for (uint8_t car = 0; car < rx->carriers && car < MAX_PSK_CARRIERS; ++car) {
        const float i = rx->carrier_i_acc[car];
        const float q = rx->carrier_q_acc[car];
        const float mag = sqrtf(i * i + q * q) / (float)(rx->samples_in_symbol ? rx->samples_in_symbol : 1);
        if (mag > rx->agc_ave) {
            rx->agc_ave = 0.98f * rx->agc_ave + 0.02f * mag;
        } else {
            rx->agc_ave = 0.995f * rx->agc_ave + 0.005f * mag;
        }
        if (mag > 5.0f) {
            any_signal = 1;
        }

        if (rx->carrier_has_prev[car]) {
            const float pi = rx->carrier_i_prev[car];
            const float pq = rx->carrier_q_prev[car];
            const float dot = pi * i + pq * q;
            const float cross = pi * q - pq * i;
            float angle = atan2f(cross, dot);
            if (angle < 0.0f) {
                angle += TWO_PI;
            }
            const uint8_t phase_idx = phase_index(angle, rx->psk_family == PSK_FAMILY_QPSK ? 4U :
                                                         (rx->psk_family == PSK_FAMILY_8PSK ? 8U : 2U));
            process_raw_symbol(rx, inverse_psk_symbol(rx, angle, dot), phase_idx);
        }
        rx->carrier_i_prev[car] = i;
        rx->carrier_q_prev[car] = q;
        rx->carrier_has_prev[car] = 1U;
        rx->carrier_i_acc[car] = 0.0f;
        rx->carrier_q_acc[car] = 0.0f;
    }

    rx->sq_level = (any_signal && rx->agc_ave > 10.0f) ? 99 : 0;
    rx->sq_open = rx->sq_level >= rx->sq_thresh;
    if (rx->sq_open && rx->sq_stable_symbols < SQ_EMIT_STABLE_SYMBOLS) {
        rx->sq_stable_symbols++;
    }
    rx->samples_in_symbol = 0;
}

static void generic_psk_rx_feed(psk_rtty_rx_t *rx, const int16_t *samples, size_t count)
{
    const int window_start = (int)(rx->symbol_period_samples * (rx->carriers > 1U ? 0.45f :
                                                               (rx->config.mode == PSK_RTTY_MODE_QPSK31_USB ? 0.25f : 0.62f)));
    const int window_end = (int)(rx->symbol_period_samples * (rx->carriers > 1U ? 0.95f :
                                                             (rx->config.mode == PSK_RTTY_MODE_QPSK31_USB ? 0.75f : 0.96f)));

    for (size_t n = 0; n < count; ++n) {
        const float s = (float)samples[n];
        const int pos = rx->bit_pos;
        for (uint8_t car = 0; car < rx->carriers && car < MAX_PSK_CARRIERS; ++car) {
            if (pos >= window_start && pos < window_end) {
                rx->carrier_i_acc[car] += s * rx->carrier_cos[car];
                rx->carrier_q_acc[car] -= s * rx->carrier_sin[car];
            }
            const float rot_cos = rx->carrier_rot_cos[car];
            const float rot_sin = rx->carrier_rot_sin[car];
            const float next_cos = rx->carrier_cos[car] * rot_cos - rx->carrier_sin[car] * rot_sin;
            const float next_sin = rx->carrier_sin[car] * rot_cos + rx->carrier_cos[car] * rot_sin;
            rx->carrier_cos[car] = next_cos;
            rx->carrier_sin[car] = next_sin;
        }
        rx->bit_pos++;
        if (pos >= window_start && pos < window_end) {
            rx->samples_in_symbol++;
        }
        rx->symbol_clock += 1.0f;
        if (rx->symbol_clock >= rx->symbol_period_samples) {
            rx->symbol_clock -= rx->symbol_period_samples;
            finish_generic_psk_symbol(rx);
            rx->bit_pos = 0;
        }
        if ((rx->sample_count++ & 0xff) == 0) {
            for (uint8_t car = 0; car < rx->carriers && car < MAX_PSK_CARRIERS; ++car) {
                const float mag = sqrtf(rx->carrier_cos[car] * rx->carrier_cos[car] +
                                        rx->carrier_sin[car] * rx->carrier_sin[car]);
                if (mag > 0.0f) {
                    rx->carrier_cos[car] /= mag;
                    rx->carrier_sin[car] /= mag;
                }
            }
        }
    }
}

void psk_rtty_rx_feed(psk_rtty_rx_t *rx, const int16_t *samples, size_t count)
{
    if (rx->config.mode == PSK_RTTY_MODE_QPSK31_USB ||
        rx->config.mode == PSK_RTTY_MODE_QPSK31_LSB ||
        rx->config.mode >= PSK_RTTY_MODE_BPSK63F) {
        generic_psk_rx_feed(rx, samples, count);
        return;
    }

    if (rx->config.mode == PSK_RTTY_MODE_BPSK31 || rx->config.mode == PSK_RTTY_MODE_PSK63) {
        pskcore_rx_feed(rx, samples, count);
        return;
    }

    if (rx->config.mode == PSK_RTTY_MODE_BPSK125 || rx->config.mode == PSK_RTTY_MODE_BPSK250 ||
        rx->config.mode == PSK_RTTY_MODE_BPSK500 || rx->config.mode == PSK_RTTY_MODE_BPSK1000) {
        simple_bpsk_rx_feed(rx, samples, count);
        return;
    }

    const float phase_inc = TWO_PI * (float)rx->config.rx_tone_hz / (float)rx->config.sample_rate_hz;
    for (size_t n = 0; n < count; ++n) {
        const float s = (float)samples[n] / 32768.0f;
        rx->i_acc += s * cosf(rx->nco_phase);
        rx->q_acc += s * sinf(rx->nco_phase);
        rx->samples_in_symbol++;
        rx->nco_phase += phase_inc;
        if (rx->nco_phase >= TWO_PI) {
            rx->nco_phase -= TWO_PI;
        }

        rx->symbol_clock += 1.0f;
        if (rx->symbol_clock >= rx->symbol_period_samples) {
            rx->symbol_clock -= rx->symbol_period_samples;
            finish_symbol(rx);
        }
    }
}
