/* sqrtcm: RTCM 3 transport + MSM observation decode (see sqrtcm.h). Original. */
#include "sqrtcm.h"

#include <math.h>
#include <string.h>

/* ---- CRC-24Q (RTCM 10403.x) ---- */

uint32_t sqrtcm_crc24q(const unsigned char *data, size_t n) {
    uint32_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= ((uint32_t)data[i]) << 16;
        for (int b = 0; b < 8; b++) {
            crc <<= 1;
            if (crc & 0x1000000u) crc ^= 0x1864CFBu;
        }
    }
    return crc & 0xFFFFFFu;
}

/* ---- bit helpers (MSB-first in the RTCM bit stream) ---- */

static unsigned get_bits(const unsigned char *p, size_t bit_off, unsigned nbits) {
    unsigned v = 0;
    for (unsigned i = 0; i < nbits; i++) {
        size_t bit = bit_off + i;
        unsigned byte = p[bit / 8];
        unsigned shift = 7u - (unsigned)(bit % 8);
        v = (v << 1) | ((byte >> shift) & 1u);
    }
    return v;
}

static int get_bits_signed(const unsigned char *p, size_t bit_off, unsigned nbits) {
    unsigned v = get_bits(p, bit_off, nbits);
    unsigned sign = 1u << (nbits - 1u);
    if (v & sign) return (int)v - (int)(1u << nbits);
    return (int)v;
}

static void put_bits(unsigned char *p, size_t bit_off, unsigned nbits, unsigned value) {
    for (unsigned i = 0; i < nbits; i++) {
        size_t bit = bit_off + i;
        unsigned shift = 7u - (unsigned)(bit % 8);
        unsigned mask = 1u << shift;
        unsigned bitv = (value >> (nbits - 1u - i)) & 1u;
        if (bitv)
            p[bit / 8] |= (unsigned char)mask;
        else
            p[bit / 8] &= (unsigned char)~mask;
    }
}

static void put_bits_signed(unsigned char *p, size_t bit_off, unsigned nbits, int value) {
    unsigned mask = (nbits >= 32u) ? 0xFFFFFFFFu : ((1u << nbits) - 1u);
    put_bits(p, bit_off, nbits, (unsigned)value & mask);
}

static size_t bits_to_bytes(size_t nbits) { return (nbits + 7u) / 8u; }

int sqrtcm_msg_category(unsigned msg_type) {
    if ((msg_type >= 1001 && msg_type <= 1004) || (msg_type >= 1009 && msg_type <= 1012))
        return SQRTCM_CAT_OBS;
    if (msg_type == 1005 || msg_type == 1006) return SQRTCM_CAT_STATION;
    if (msg_type >= 1071 && msg_type <= 1127) return SQRTCM_CAT_MSM;
    if (msg_type >= 1001 && msg_type <= 1230) return SQRTCM_CAT_OTHER;
    return SQRTCM_CAT_UNKNOWN;
}

int sqrtcm_msm_type(unsigned msg_type) {
    if (sqrtcm_msg_category(msg_type) != SQRTCM_CAT_MSM) return 0;
    int t = (int)(msg_type % 10u);
    if (t < 1 || t > 7) return 0;
    return t;
}

size_t sqrtcm_frame(unsigned char *out, size_t cap, const unsigned char *payload, size_t plen) {
    if (!out || (!payload && plen) || plen > SQRTCM_MAX_PAYLOAD) return 0;
    size_t total = 3 + plen + 3;
    if (cap < total) return 0;
    out[0] = SQRTCM_PREAMBLE;
    out[1] = (unsigned char)((plen >> 8) & 0x03); /* reserved 6 bits = 0 */
    out[2] = (unsigned char)(plen & 0xFF);
    if (plen) memcpy(out + 3, payload, plen);
    uint32_t crc = sqrtcm_crc24q(out, 3 + plen);
    out[3 + plen] = (unsigned char)((crc >> 16) & 0xFF);
    out[3 + plen + 1] = (unsigned char)((crc >> 8) & 0xFF);
    out[3 + plen + 2] = (unsigned char)(crc & 0xFF);
    return total;
}

size_t sqrtcm_pack_msm_header(unsigned char *out, size_t cap, unsigned msg_type,
                              unsigned station_id, unsigned epoch_ms, int multi) {
    if (!out || cap < SQRTCM_MSM_HEADER_BYTES || msg_type > 0xFFFu || station_id > 0xFFFu)
        return 0;
    if (sqrtcm_msg_category(msg_type) != SQRTCM_CAT_MSM) return 0;
    memset(out, 0, SQRTCM_MSM_HEADER_BYTES);
    put_bits(out, 0, 12, msg_type);
    put_bits(out, 12, 12, station_id);
    /* GPS-style 30-bit ms-of-week for most MSM; GLONASS 1081–1087 is day+ms
     * — callers must pack that layout when GLONASS decode is added. */
    put_bits(out, 24, 30, epoch_ms & 0x3FFFFFFFu);
    put_bits(out, 54, 1, multi ? 1u : 0u);
    /* DF409 IODS (3), DF001 reserved (7), DF411 (2), DF412 (2), DF417 (1),
     * DF418 (3) left zero — bits 55..72. */
    return SQRTCM_MSM_HEADER_BYTES;
}

size_t sqrtcm_pack_obs_header(unsigned char *out, size_t cap, unsigned msg_type,
                              unsigned station_id) {
    if (!out || cap < 3 || msg_type > 0xFFFu || station_id > 0xFFFu) return 0;
    memset(out, 0, 3);
    put_bits(out, 0, 12, msg_type);
    put_bits(out, 12, 12, station_id);
    return 3;
}

int sqrtcm_parse_header(const unsigned char *payload, size_t len, SqrtcmMsg *out) {
    if (!payload || !out || len < 2) return -1;
    memset(out, 0, sizeof *out);
    out->payload = payload;
    out->payload_len = len;
    out->payload_owned = 0;
    out->msg_type = get_bits(payload, 0, 12);
    out->category = sqrtcm_msg_category(out->msg_type);
    out->multi = -1;
    if (len >= 3) {
        out->station_id = get_bits(payload, 12, 12);
        out->have_station = 1;
    }
    if (out->category == SQRTCM_CAT_MSM && len >= 8) {
        out->epoch_ms = get_bits(payload, 24, 30);
        out->have_epoch = 1;
        out->multi = (int)get_bits(payload, 54, 1);
    }
    return 0;
}

/* ---- MSM observation pack / parse (RTCM 10403.x §3.5.12–3.5.13) ---- */

static void msm_set_field_flags(SqrtcmMsm *m) {
    int t = m->msm_type;
    m->have_rough_range = (t == 1 || t >= 3) ? 1 : 0;
    m->have_rough_rate = (t == 5 || t == 7) ? 1 : 0;
    m->have_fine_pr = (t == 1 || t == 3 || t >= 4) ? 1 : 0;
    m->have_fine_phase = (t == 2 || t >= 3) ? 1 : 0;
    m->have_lock = (t == 2 || t >= 3) ? 1 : 0;
    m->have_half_cycle = (t == 2 || t >= 3) ? 1 : 0;
    m->have_cnr = (t >= 4) ? 1 : 0;
    m->have_fine_phaserate = (t == 5 || t == 7) ? 1 : 0;
    m->ext_res = (t == 6 || t == 7) ? 1 : 0;
}

static int msm_cell_bits_ok(const SqrtcmMsm *m) {
    if (m->n_sat < 0 || m->n_sat > SQRTCM_MSM_MAX_SAT) return 0;
    if (m->n_sig < 0 || m->n_sig > SQRTCM_MSM_MAX_SIG) return 0;
    if (m->n_cell < 0 || m->n_cell > SQRTCM_MSM_MAX_CELL) return 0;
    if (m->n_sat > 0 && m->n_sig > 0) {
        int max_cells = m->n_sat * m->n_sig;
        if (max_cells > SQRTCM_MSM_MAX_CELL) max_cells = SQRTCM_MSM_MAX_CELL;
        if (m->n_cell > max_cells) return 0;
    } else if (m->n_cell != 0) {
        return 0;
    }
    for (int i = 0; i < m->n_cell; i++) {
        if (m->cell_sat[i] >= (unsigned)m->n_sat) return 0;
        if (m->cell_sig[i] >= (unsigned)m->n_sig) return 0;
    }
    return 1;
}

size_t sqrtcm_pack_msm(unsigned char *out, size_t cap, const SqrtcmMsm *msm) {
    if (!out || !msm) return 0;
    int t = msm->msm_type ? msm->msm_type : sqrtcm_msm_type(msm->msg_type);
    if (t < 1 || t > 7) return 0;
    if (!msm_cell_bits_ok(msm)) return 0;
    if (msm->msg_type > 0xFFFu || msm->station_id > 0xFFFu) return 0;

    /* Estimate bit length then refuse if over payload cap. */
    size_t nbits = SQRTCM_MSM_HEADER_BITS + 64u + 32u;
    nbits += (size_t)msm->n_sat * (size_t)msm->n_sig; /* DF396 */
    if (t == 1 || t >= 3) nbits += (size_t)msm->n_sat * (8u + 10u);
    if (t == 5 || t == 7) nbits += (size_t)msm->n_sat * 14u;
    {
        unsigned cell_bits = 0;
        if (t == 1) cell_bits = 15;
        else if (t == 2) cell_bits = 22 + 4 + 1;
        else if (t == 3) cell_bits = 15 + 22 + 4 + 1;
        else if (t == 4) cell_bits = 15 + 22 + 4 + 1 + 6;
        else if (t == 5) cell_bits = 15 + 22 + 4 + 1 + 6 + 15;
        else if (t == 6) cell_bits = 20 + 24 + 10 + 1 + 10;
        else /* 7 */ cell_bits = 20 + 24 + 10 + 1 + 10 + 15;
        nbits += (size_t)msm->n_cell * cell_bits;
    }
    size_t nbytes = bits_to_bytes(nbits);
    if (nbytes > SQRTCM_MAX_PAYLOAD || cap < nbytes) return 0;

    memset(out, 0, nbytes);
    put_bits(out, 0, 12, msm->msg_type);
    put_bits(out, 12, 12, msm->station_id);
    put_bits(out, 24, 30, msm->epoch_ms & 0x3FFFFFFFu);
    put_bits(out, 54, 1, msm->multi ? 1u : 0u);
    put_bits(out, 55, 3, msm->iods & 7u);
    put_bits(out, 58, 7, 0); /* reserved */
    put_bits(out, 65, 2, msm->clock_steering & 3u);
    put_bits(out, 67, 2, msm->ext_clock & 3u);
    put_bits(out, 69, 1, msm->divergence_free ? 1u : 0u);
    put_bits(out, 70, 3, msm->smooth_interval & 7u);

    size_t bit = SQRTCM_MSM_HEADER_BITS;

    /* DF394 satellite mask: MSB = sat ID 1. */
    {
        uint64_t mask = 0;
        for (int i = 0; i < msm->n_sat; i++) {
            unsigned id = msm->sat_id[i];
            if (id < 1 || id > 64) return 0;
            mask |= (uint64_t)1 << (64u - id);
        }
        put_bits(out, bit, 32, (unsigned)(mask >> 32));
        put_bits(out, bit + 32, 32, (unsigned)(mask & 0xFFFFFFFFu));
        bit += 64;
    }

    /* DF395 signal mask: MSB = signal ID 1. */
    {
        unsigned mask = 0;
        for (int i = 0; i < msm->n_sig; i++) {
            unsigned id = msm->sig_id[i];
            if (id < 1 || id > 32) return 0;
            mask |= 1u << (32u - id);
        }
        put_bits(out, bit, 32, mask);
        bit += 32;
    }

    /* DF396 cell mask: sat-major, sig-minor; at most 64 bits of content. */
    {
        int mask_bits = msm->n_sat * msm->n_sig;
        if (mask_bits > SQRTCM_MSM_MAX_CELL) mask_bits = SQRTCM_MSM_MAX_CELL;
        for (int i = 0; i < mask_bits; i++) put_bits(out, bit + (size_t)i, 1, 0);
        for (int c = 0; c < msm->n_cell; c++) {
            int idx = (int)msm->cell_sat[c] * msm->n_sig + (int)msm->cell_sig[c];
            if (idx < 0 || idx >= mask_bits) return 0;
            put_bits(out, bit + (size_t)idx, 1, 1);
        }
        bit += (size_t)mask_bits;
    }

    if (t == 1 || t >= 3) {
        for (int i = 0; i < msm->n_sat; i++)
            put_bits(out, bit + (size_t)i * 8u, 8, msm->rough_ms[i] & 0xFFu);
        bit += (size_t)msm->n_sat * 8u;
        for (int i = 0; i < msm->n_sat; i++)
            put_bits(out, bit + (size_t)i * 10u, 10, msm->rough_mod[i] & 0x3FFu);
        bit += (size_t)msm->n_sat * 10u;
    }
    if (t == 5 || t == 7) {
        for (int i = 0; i < msm->n_sat; i++)
            put_bits_signed(out, bit + (size_t)i * 14u, 14, msm->rough_rate[i]);
        bit += (size_t)msm->n_sat * 14u;
    }

    for (int c = 0; c < msm->n_cell; c++) {
        if (t == 1) {
            put_bits_signed(out, bit, 15, msm->fine_pr[c]);
            bit += 15;
        } else if (t == 2) {
            put_bits_signed(out, bit, 22, msm->fine_phase[c]);
            bit += 22;
            put_bits(out, bit, 4, msm->lock[c] & 0xFu);
            bit += 4;
            put_bits(out, bit, 1, msm->half_cycle[c] & 1u);
            bit += 1;
        } else if (t == 3) {
            put_bits_signed(out, bit, 15, msm->fine_pr[c]);
            bit += 15;
            put_bits_signed(out, bit, 22, msm->fine_phase[c]);
            bit += 22;
            put_bits(out, bit, 4, msm->lock[c] & 0xFu);
            bit += 4;
            put_bits(out, bit, 1, msm->half_cycle[c] & 1u);
            bit += 1;
        } else if (t == 4 || t == 5) {
            put_bits_signed(out, bit, 15, msm->fine_pr[c]);
            bit += 15;
            put_bits_signed(out, bit, 22, msm->fine_phase[c]);
            bit += 22;
            put_bits(out, bit, 4, msm->lock[c] & 0xFu);
            bit += 4;
            put_bits(out, bit, 1, msm->half_cycle[c] & 1u);
            bit += 1;
            put_bits(out, bit, 6, msm->cnr[c] & 0x3Fu);
            bit += 6;
            if (t == 5) {
                put_bits_signed(out, bit, 15, msm->fine_phaserate[c]);
                bit += 15;
            }
        } else { /* MSM6 / MSM7 */
            put_bits_signed(out, bit, 20, msm->fine_pr[c]);
            bit += 20;
            put_bits_signed(out, bit, 24, msm->fine_phase[c]);
            bit += 24;
            put_bits(out, bit, 10, msm->lock[c] & 0x3FFu);
            bit += 10;
            put_bits(out, bit, 1, msm->half_cycle[c] & 1u);
            bit += 1;
            put_bits(out, bit, 10, msm->cnr[c] & 0x3FFu);
            bit += 10;
            if (t == 7) {
                put_bits_signed(out, bit, 15, msm->fine_phaserate[c]);
                bit += 15;
            }
        }
    }

    (void)bit;
    return nbytes;
}

int sqrtcm_parse_msm(const unsigned char *payload, size_t len, SqrtcmMsm *out) {
    if (!payload || !out || len < SQRTCM_MSM_HEADER_BYTES) return -1;
    memset(out, 0, sizeof *out);

    out->msg_type = get_bits(payload, 0, 12);
    out->msm_type = sqrtcm_msm_type(out->msg_type);
    if (out->msm_type == 0) return -1;
    out->station_id = get_bits(payload, 12, 12);
    out->epoch_ms = get_bits(payload, 24, 30);
    out->multi = (int)get_bits(payload, 54, 1);
    out->iods = get_bits(payload, 55, 3);
    out->clock_steering = get_bits(payload, 65, 2);
    out->ext_clock = get_bits(payload, 67, 2);
    out->divergence_free = (int)get_bits(payload, 69, 1);
    out->smooth_interval = get_bits(payload, 70, 3);
    msm_set_field_flags(out);

    size_t bit = SQRTCM_MSM_HEADER_BITS;
    size_t len_bits = len * 8u;

    if (bit + 64 + 32 > len_bits) return -1;

    /* DF394 */
    {
        uint64_t hi = get_bits(payload, bit, 32);
        uint64_t lo = get_bits(payload, bit + 32, 32);
        uint64_t mask = (hi << 32) | lo;
        bit += 64;
        out->n_sat = 0;
        for (unsigned id = 1; id <= 64; id++) {
            if (mask & ((uint64_t)1 << (64u - id))) {
                if (out->n_sat >= SQRTCM_MSM_MAX_SAT) return -1;
                out->sat_id[out->n_sat++] = id;
            }
        }
    }

    /* DF395 */
    {
        unsigned mask = get_bits(payload, bit, 32);
        bit += 32;
        out->n_sig = 0;
        for (unsigned id = 1; id <= 32; id++) {
            if (mask & (1u << (32u - id))) {
                if (out->n_sig >= SQRTCM_MSM_MAX_SIG) return -1;
                out->sig_id[out->n_sig++] = id;
            }
        }
    }

    int mask_bits = out->n_sat * out->n_sig;
    if (mask_bits > SQRTCM_MSM_MAX_CELL) mask_bits = SQRTCM_MSM_MAX_CELL;
    if (bit + (size_t)mask_bits > len_bits) return -1;

    out->n_cell = 0;
    for (int s = 0; s < out->n_sat; s++) {
        for (int g = 0; g < out->n_sig; g++) {
            int idx = s * out->n_sig + g;
            if (idx >= mask_bits) break;
            if (get_bits(payload, bit + (size_t)idx, 1)) {
                if (out->n_cell >= SQRTCM_MSM_MAX_CELL) return -1;
                out->cell_sat[out->n_cell] = (unsigned)s;
                out->cell_sig[out->n_cell] = (unsigned)g;
                out->n_cell++;
            }
        }
    }
    bit += (size_t)mask_bits;

    int t = out->msm_type;
    if (out->have_rough_range) {
        if (bit + (size_t)out->n_sat * 18u > len_bits) return -1;
        for (int i = 0; i < out->n_sat; i++)
            out->rough_ms[i] = get_bits(payload, bit + (size_t)i * 8u, 8);
        bit += (size_t)out->n_sat * 8u;
        for (int i = 0; i < out->n_sat; i++)
            out->rough_mod[i] = get_bits(payload, bit + (size_t)i * 10u, 10);
        bit += (size_t)out->n_sat * 10u;
    }
    if (out->have_rough_rate) {
        if (bit + (size_t)out->n_sat * 14u > len_bits) return -1;
        for (int i = 0; i < out->n_sat; i++)
            out->rough_rate[i] = get_bits_signed(payload, bit + (size_t)i * 14u, 14);
        bit += (size_t)out->n_sat * 14u;
    }

    for (int c = 0; c < out->n_cell; c++) {
        if (t == 1) {
            if (bit + 15 > len_bits) return -1;
            out->fine_pr[c] = get_bits_signed(payload, bit, 15);
            bit += 15;
        } else if (t == 2) {
            if (bit + 27 > len_bits) return -1;
            out->fine_phase[c] = get_bits_signed(payload, bit, 22);
            bit += 22;
            out->lock[c] = get_bits(payload, bit, 4);
            bit += 4;
            out->half_cycle[c] = get_bits(payload, bit, 1);
            bit += 1;
        } else if (t == 3) {
            if (bit + 42 > len_bits) return -1;
            out->fine_pr[c] = get_bits_signed(payload, bit, 15);
            bit += 15;
            out->fine_phase[c] = get_bits_signed(payload, bit, 22);
            bit += 22;
            out->lock[c] = get_bits(payload, bit, 4);
            bit += 4;
            out->half_cycle[c] = get_bits(payload, bit, 1);
            bit += 1;
        } else if (t == 4 || t == 5) {
            size_t need = (t == 5) ? 63u : 48u;
            if (bit + need > len_bits) return -1;
            out->fine_pr[c] = get_bits_signed(payload, bit, 15);
            bit += 15;
            out->fine_phase[c] = get_bits_signed(payload, bit, 22);
            bit += 22;
            out->lock[c] = get_bits(payload, bit, 4);
            bit += 4;
            out->half_cycle[c] = get_bits(payload, bit, 1);
            bit += 1;
            out->cnr[c] = get_bits(payload, bit, 6);
            bit += 6;
            if (t == 5) {
                out->fine_phaserate[c] = get_bits_signed(payload, bit, 15);
                bit += 15;
            }
        } else { /* 6 / 7 */
            size_t need = (t == 7) ? 80u : 65u;
            if (bit + need > len_bits) return -1;
            out->fine_pr[c] = get_bits_signed(payload, bit, 20);
            bit += 20;
            out->fine_phase[c] = get_bits_signed(payload, bit, 24);
            bit += 24;
            out->lock[c] = get_bits(payload, bit, 10);
            bit += 10;
            out->half_cycle[c] = get_bits(payload, bit, 1);
            bit += 1;
            out->cnr[c] = get_bits(payload, bit, 10);
            bit += 10;
            if (t == 7) {
                out->fine_phaserate[c] = get_bits_signed(payload, bit, 15);
                bit += 15;
            }
        }
    }

    (void)bit;
    return 0;
}

static int rough_valid(const SqrtcmMsm *m, int sat_index) {
    if (!m || !m->have_rough_range) return 0;
    if (sat_index < 0 || sat_index >= m->n_sat) return 0;
    return m->rough_ms[sat_index] != SQRTCM_MSM_ROUGH_MS_INVALID;
}

double sqrtcm_msm_rough_range_m(const SqrtcmMsm *m, int sat_index) {
    if (!rough_valid(m, sat_index)) return NAN;
    double ms = (double)m->rough_ms[sat_index] + (double)m->rough_mod[sat_index] / 1024.0;
    return ms * SQRTCM_MSM_MS_TO_M;
}

double sqrtcm_msm_pseudorange_m(const SqrtcmMsm *m, int cell_index) {
    if (!m || !m->have_fine_pr || cell_index < 0 || cell_index >= m->n_cell) return NAN;
    int s = (int)m->cell_sat[cell_index];
    if (!rough_valid(m, s)) return NAN;
    int fine = m->fine_pr[cell_index];
    double scale;
    if (m->ext_res) {
        if (fine == SQRTCM_MSM_FINE20_INVALID) return NAN;
        scale = ldexp(1.0, -29); /* 2^-29 ms */
    } else {
        if (fine == SQRTCM_MSM_FINE15_INVALID) return NAN;
        scale = ldexp(1.0, -24); /* 2^-24 ms */
    }
    double rough = (double)m->rough_ms[s] + (double)m->rough_mod[s] / 1024.0;
    return (rough + (double)fine * scale) * SQRTCM_MSM_MS_TO_M;
}

double sqrtcm_msm_phaserange_m(const SqrtcmMsm *m, int cell_index) {
    if (!m || !m->have_fine_phase || cell_index < 0 || cell_index >= m->n_cell) return NAN;
    int s = (int)m->cell_sat[cell_index];
    if (!rough_valid(m, s)) return NAN;
    int fine = m->fine_phase[cell_index];
    double scale;
    if (m->ext_res) {
        if (fine == SQRTCM_MSM_PHASE24_INVALID) return NAN;
        scale = ldexp(1.0, -31); /* 2^-31 ms */
    } else {
        if (fine == SQRTCM_MSM_PHASE22_INVALID) return NAN;
        scale = ldexp(1.0, -29); /* 2^-29 ms */
    }
    double rough = (double)m->rough_ms[s] + (double)m->rough_mod[s] / 1024.0;
    return (rough + (double)fine * scale) * SQRTCM_MSM_MS_TO_M;
}

void sqrtcm_msg_own_payload(SqrtcmMsg *msg) {
    if (!msg) return;
    if (!msg->payload || msg->payload_len == 0) {
        msg->payload_owned = 1;
        msg->payload = msg->payload_copy;
        return;
    }
    if (msg->payload_len > SQRTCM_MAX_PAYLOAD) {
        msg->payload_len = SQRTCM_MAX_PAYLOAD;
    }
    if (msg->payload != msg->payload_copy)
        memcpy(msg->payload_copy, msg->payload, msg->payload_len);
    msg->payload_owned = 1;
    msg->payload = msg->payload_copy;
}

void sqrtcm_msg_fix_payload(SqrtcmMsg *msg) {
    if (!msg) return;
    if (msg->payload_owned) msg->payload = msg->payload_copy;
}

void sqrtcm_msg_assign(SqrtcmMsg *dst, const SqrtcmMsg *src) {
    if (!dst || !src) return;
    *dst = *src;
    sqrtcm_msg_fix_payload(dst);
}

/* ---- framer input / rescan ring ---- */

static size_t inq_cap(const SqrtcmFramer *f) { return sizeof f->inq; }

static int inq_append(SqrtcmFramer *f, unsigned char b) {
    size_t cap = inq_cap(f);
    if (f->inq_n >= cap) {
        f->rescan_drop++;
        return -1;
    }
    size_t i = (f->inq_head + f->inq_n) % cap;
    f->inq[i] = b;
    f->inq_n++;
    return 0;
}

/* Prepend n bytes at the front (rejected-frame rescan from buf[1]).
 * On overflow the whole tail is dropped and counted in rescan_drop. */
static int inq_prepend(SqrtcmFramer *f, const unsigned char *p, size_t n) {
    size_t cap = inq_cap(f);
    if (!n) return 0;
    if (n > cap || f->inq_n + n > cap) {
        f->rescan_drop += (unsigned)n;
        return -1;
    }
    /* Walk head backward so take order stays p[0], p[1], … */
    for (size_t i = n; i > 0; i--) {
        f->inq_head = (f->inq_head + cap - 1) % cap;
        f->inq[f->inq_head] = p[i - 1];
        f->inq_n++;
    }
    return 0;
}

static int inq_take(SqrtcmFramer *f, unsigned char *b) {
    if (f->inq_n == 0) return 0;
    *b = f->inq[f->inq_head];
    f->inq_head = (f->inq_head + 1) % inq_cap(f);
    f->inq_n--;
    return 1;
}

/* ---- framer ---- */

enum { S_IDLE = 0, S_LEN1, S_LEN2, S_BODY };

void sqrtcm_framer_init(SqrtcmFramer *f) { memset(f, 0, sizeof *f); }

/* Step without rescan. Returns 0 = need more, 1 = good frame in *out,
 * 2 = rejected (bad CRC / length); f->buf[0..len) still holds the rejected
 * bytes so the caller can re-queue from buf[1]. */
static int framer_step(SqrtcmFramer *f, unsigned char b, SqrtcmMsg *out) {
    switch (f->state) {
    case S_IDLE:
        if (b == SQRTCM_PREAMBLE) {
            f->buf[0] = b;
            f->len = 1;
            f->state = S_LEN1;
        } else
            f->junk++;
        return 0;
    case S_LEN1:
        f->buf[1] = b;
        f->len = 2;
        f->state = S_LEN2;
        return 0;
    case S_LEN2: {
        f->buf[2] = b;
        f->len = 3;
        if (f->buf[1] & 0xFC) {
            f->bad_len++;
            return 2;
        }
        f->need = ((size_t)(f->buf[1] & 0x03) << 8) | (size_t)f->buf[2];
        if (f->need > SQRTCM_MAX_PAYLOAD) {
            f->bad_len++;
            return 2;
        }
        f->state = S_BODY;
        return 0;
    }
    case S_BODY: {
        if (f->len >= SQRTCM_MAX_FRAME) {
            f->bad_len++;
            return 2;
        }
        f->buf[f->len++] = b;
        if (f->len < 3 + f->need + 3) return 0;
        {
            size_t plen = f->need;
            size_t covered = 3 + plen;
            uint32_t want = ((uint32_t)f->buf[covered] << 16) |
                            ((uint32_t)f->buf[covered + 1] << 8) | (uint32_t)f->buf[covered + 2];
            uint32_t got = sqrtcm_crc24q(f->buf, covered);
            if (want != got) {
                f->bad_crc++;
                return 2;
            }
            f->ok++;
            if (sqrtcm_parse_header(f->buf + 3, plen, out) != 0) {
                memset(out, 0, sizeof *out);
                out->payload = f->buf + 3;
                out->payload_len = plen;
                out->category = SQRTCM_CAT_UNKNOWN;
                out->multi = -1;
            }
            /* Copy before clearing state so the next frame cannot clobber it. */
            sqrtcm_msg_own_payload(out);
            f->state = S_IDLE;
            f->len = 0;
            f->need = 0;
            return 1;
        }
    }
    default:
        f->state = S_IDLE;
        f->len = 0;
        return 0;
    }
}

/* Drain the input queue: re-queue rejected tails from buf[1] so every good
 * frame hidden in a reject is eventually delivered. Returns 1 with *out set
 * on the first good frame; remaining queued bytes stay for the next drain. */
static int framer_drain(SqrtcmFramer *f, SqrtcmMsg *out) {
    unsigned char b;
    while (inq_take(f, &b)) {
        int r = framer_step(f, b, out);
        if (r == 1) return 1;
        if (r == 2) {
            if (f->len > 1)
                (void)inq_prepend(f, f->buf + 1, f->len - 1);
            f->state = S_IDLE;
            f->len = 0;
            f->need = 0;
            continue;
        }
    }
    return 0;
}

int sqrtcm_framer_byte(SqrtcmFramer *f, unsigned char b, SqrtcmMsg *out) {
    if (!f || !out) return 0;
    (void)inq_append(f, b);
    return framer_drain(f, out);
}

int sqrtcm_framer_feed(SqrtcmFramer *f, const void *data, size_t n, SqrtcmMsg *msgs, int max_msgs) {
    if (!f || (!data && n) || max_msgs < 0) return -1;
    const unsigned char *p = (const unsigned char *)data;
    int stored = 0;
    SqrtcmMsg tmp;
    /* n==0: drain frames left queued by sqrtcm_framer_byte (no per-byte
     * loop). When n>0 the per-byte inner drain already empties the ring. */
    if (n == 0) {
        for (;;) {
            SqrtcmMsg *slot = (msgs && stored < max_msgs) ? &msgs[stored] : &tmp;
            if (!framer_drain(f, slot)) break;
            sqrtcm_msg_own_payload(slot);
            if (msgs && stored < max_msgs) stored++;
        }
        return stored;
    }
    for (size_t i = 0; i < n; i++) {
        (void)inq_append(f, p[i]);
        for (;;) {
            SqrtcmMsg *slot = (msgs && stored < max_msgs) ? &msgs[stored] : &tmp;
            if (!framer_drain(f, slot)) break;
            sqrtcm_msg_own_payload(slot);
            if (msgs && stored < max_msgs) stored++;
        }
    }
    return stored;
}
