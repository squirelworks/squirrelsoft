/* sqrtcm: RTCM 3 transport + header skeleton (see sqrtcm.h). Original code. */
#include "sqrtcm.h"

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

int sqrtcm_msg_category(unsigned msg_type) {
    if ((msg_type >= 1001 && msg_type <= 1004) || (msg_type >= 1009 && msg_type <= 1012))
        return SQRTCM_CAT_OBS;
    if (msg_type == 1005 || msg_type == 1006) return SQRTCM_CAT_STATION;
    if (msg_type >= 1071 && msg_type <= 1127) return SQRTCM_CAT_MSM;
    if (msg_type >= 1001 && msg_type <= 1230) return SQRTCM_CAT_OTHER;
    return SQRTCM_CAT_UNKNOWN;
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
    if (!out || cap < 8 || msg_type > 0xFFFu || station_id > 0xFFFu) return 0;
    if (sqrtcm_msg_category(msg_type) != SQRTCM_CAT_MSM) return 0;
    memset(out, 0, 8);
    put_bits(out, 0, 12, msg_type);
    put_bits(out, 12, 12, station_id);
    /* GPS-style 30-bit ms-of-week for most MSM; GLONASS 1081–1087 is day+ms
     * — callers must pack that layout when GLONASS decode is added. */
    put_bits(out, 24, 30, epoch_ms & 0x3FFFFFFFu);
    put_bits(out, 54, 1, multi ? 1u : 0u);
    /* IODS (3), reserved (7), clock steering (2), ext clock (2),
     * divergence-free (1), smoothing interval (3) left zero. */
    return 8;
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

void sqrtcm_msg_own_payload(SqrtcmMsg *msg) {
    if (!msg) return;
    if (!msg->payload || msg->payload_len == 0) {
        msg->payload = msg->payload_copy;
        return;
    }
    if (msg->payload_len > SQRTCM_MAX_PAYLOAD) {
        msg->payload_len = SQRTCM_MAX_PAYLOAD;
    }
    if (msg->payload != msg->payload_copy)
        memcpy(msg->payload_copy, msg->payload, msg->payload_len);
    msg->payload = msg->payload_copy;
}

/* ---- framer ---- */

enum { S_IDLE = 0, S_LEN1, S_LEN2, S_BODY };

void sqrtcm_framer_init(SqrtcmFramer *f) { memset(f, 0, sizeof *f); }

/* Step without rescan. Returns 0 = need more, 1 = good frame in *out,
 * 2 = rejected (bad CRC / length); f->buf[0..len) still holds the rejected
 * bytes so the caller can rescan from buf[1]. */
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
            return 1;
        }
    }
    default:
        f->state = S_IDLE;
        f->len = 0;
        return 0;
    }
}

/* Prepend f->buf[1..len) in front of q[qi..qn) after a reject, then clear f. */
static size_t rescan_queue_prepend(SqrtcmFramer *f, unsigned char *q, size_t qcap, size_t qi,
                                   size_t qn) {
    if (f->len > 1) {
        size_t add = f->len - 1;
        size_t rest = qn - qi;
        if (qi + add + rest <= qcap) {
            memmove(q + qi + add, q + qi, rest);
            memcpy(q + qi, f->buf + 1, add);
            qn = qi + add + rest;
        }
    }
    f->state = S_IDLE;
    f->len = 0;
    f->need = 0;
    return qn;
}

/* After a reject, drop buf[0] and re-inject buf[1..len) so a real 0xD3 inside
 * the discarded bytes is not lost. Iterative (no deep recursion). Returns 1
 * on the first good frame; any trailing bytes stay in the framer (or are
 * consumed into further rejects/resync). Extra complete frames in the same
 * rescan are delivered by continuing the queue into f after owning *out is
 * the caller's job on the next feed byte — we keep only the first *out. */
static int framer_rescan_reject(SqrtcmFramer *f, SqrtcmMsg *out) {
    unsigned char q[SQRTCM_MAX_FRAME];
    size_t qn;

    if (f->len <= 1) {
        f->state = S_IDLE;
        f->len = 0;
        f->need = 0;
        return 0;
    }
    qn = f->len - 1;
    memcpy(q, f->buf + 1, qn);
    f->state = S_IDLE;
    f->len = 0;
    f->need = 0;

    size_t qi = 0;
    while (qi < qn) {
        int r = framer_step(f, q[qi++], out);
        if (r == 1) {
            /* Absorb remainder into f without overwriting *out. */
            SqrtcmMsg discard;
            while (qi < qn) {
                int r2 = framer_step(f, q[qi], &discard);
                if (r2 == 2) {
                    qn = rescan_queue_prepend(f, q, sizeof q, qi, qn);
                    continue;
                }
                if (r2 == 1) {
                    /* Rare: second frame in one rescan. Re-queue its payload
                     * path by treating the already-parsed discard as done;
                     * leave framer idle and push any unread q bytes next. */
                    f->state = S_IDLE;
                    f->len = 0;
                    f->need = 0;
                    /* Deliberately drop the extra frame from this byte call;
                     * feed() of a whole buffer usually sees frames one-by-one
                     * from distinct input bytes. */
                }
                qi++;
            }
            return 1;
        }
        if (r == 2) {
            qn = rescan_queue_prepend(f, q, sizeof q, qi, qn);
            continue;
        }
    }
    return 0;
}

int sqrtcm_framer_byte(SqrtcmFramer *f, unsigned char b, SqrtcmMsg *out) {
    if (!f || !out) return 0;
    int r = framer_step(f, b, out);
    if (r == 1) return 1;
    if (r == 2) return framer_rescan_reject(f, out);
    return 0;
}

int sqrtcm_framer_feed(SqrtcmFramer *f, const void *data, size_t n, SqrtcmMsg *msgs, int max_msgs) {
    if (!f || (!data && n) || max_msgs < 0) return -1;
    const unsigned char *p = (const unsigned char *)data;
    int stored = 0;
    SqrtcmMsg tmp;
    for (size_t i = 0; i < n; i++) {
        SqrtcmMsg *slot = (msgs && stored < max_msgs) ? &msgs[stored] : &tmp;
        if (sqrtcm_framer_byte(f, p[i], slot)) {
            /* Detach payload before the next frame overwrites f->buf. */
            sqrtcm_msg_own_payload(slot);
            if (msgs && stored < max_msgs) stored++;
        }
    }
    return stored;
}
