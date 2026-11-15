/*
 * sqrtcm: RTCM 3 frame sync, CRC-24Q, header parse and MSM observation
 * decode for SquirrelGNSS (original code).
 *
 * Buffer-fed only — no network and no third-party RTCM libraries. CI uses
 * synthetic frames built in-tree. Opaque NTRIP correction bodies from
 * sqntrip can be fed through SqrtcmFramer once a stream 200 body arrives.
 *
 * Layout follows the public RTCM 10403.x transport layer (preamble 0xD3,
 * 10-bit length, CRC-24Q), the common MSM header (DF002–DF418), satellite /
 * signal / cell masks (DF394–DF396) and MSM1–MSM7 observation fields
 * (DF397–DF408 / DF420). See squirrelgnss/docs/rtcm-decode.md.
 *
 * GLONASS MSM (1081–1087) epoch note: the 30-bit DF004-style field is
 * day-of-week (3 bits) + ms-of-day (27 bits), not GPS ms-of-week. This
 * library still exposes it as epoch_ms; split day/ms when a GLONASS-aware
 * consumer needs them.
 */
#ifndef SQRTCM_H
#define SQRTCM_H

#include <stddef.h>
#include <stdint.h>

#define SQRTCM_PREAMBLE 0xD3
#define SQRTCM_MAX_PAYLOAD 1023 /* 10-bit length field */
#define SQRTCM_MAX_FRAME (3 + SQRTCM_MAX_PAYLOAD + 3)
/* Input/rescan queue sized for a rejected max frame plus another max frame. */
#define SQRTCM_INQ_MAX (SQRTCM_MAX_FRAME * 2)

/* MSM mask / cell limits from RTCM 10403.x (DF394/DF395/DF396). */
#define SQRTCM_MSM_MAX_SAT 64
#define SQRTCM_MSM_MAX_SIG 32
#define SQRTCM_MSM_MAX_CELL 64
/* Full MSM header through DF418 is 73 bits → 10 bytes. */
#define SQRTCM_MSM_HEADER_BYTES 10
#define SQRTCM_MSM_HEADER_BITS 73

/* Rough-range / fine-obs invalid sentinels (RTCM DF tables). */
#define SQRTCM_MSM_ROUGH_MS_INVALID 255u
#define SQRTCM_MSM_ROUGH_RATE_INVALID (-8192)
#define SQRTCM_MSM_FINE15_INVALID (-16384)
#define SQRTCM_MSM_FINE20_INVALID (-524288)
#define SQRTCM_MSM_PHASE22_INVALID (-2097152)
#define SQRTCM_MSM_PHASE24_INVALID (-8388608)

/* Approximate metres per millisecond of light travel (RTCM ranging unit). */
#define SQRTCM_MSM_MS_TO_M 299792.458

/* Message-number ranges we recognise for v1 header parse. */
enum {
    SQRTCM_CAT_UNKNOWN = 0,
    SQRTCM_CAT_OBS = 1,     /* 1001–1004 GPS, 1009–1012 GLONASS legacy obs */
    SQRTCM_CAT_STATION = 2, /* 1005–1006 station ARP */
    SQRTCM_CAT_MSM = 3,     /* 1071–1127 MSM */
    SQRTCM_CAT_OTHER = 4    /* known RTCM3 number outside the v1 subset */
};

typedef struct {
    unsigned msg_type;   /* DF002, 12 bits */
    unsigned station_id; /* DF003, 12 bits when payload has them */
    int have_station;
    /* MSM epoch: GPS-style ms-of-week for 107x/109x/…; for GLONASS 1081–1087
     * the same 30 bits are day+ms (see file header). Full split later. */
    unsigned epoch_ms;
    int have_epoch;
    int multi; /* MSM multiple-message bit, or -1 if unknown */
    int category;
    /* Prefer sqrtcm_msg_payload() after a plain struct copy: when
     * payload_owned is set, bytes live in payload_copy and a raw
     * `a = b` leaves a.payload pointing at b's buffer. */
    const unsigned char *payload;
    size_t payload_len;
    unsigned char payload_owned; /* 1 => use payload_copy, not a borrow */
    unsigned char payload_copy[SQRTCM_MAX_PAYLOAD];
} SqrtcmMsg;

/* Decoded MSM observation content (masks + satellite/cell fields). */
typedef struct {
    unsigned msg_type;
    unsigned station_id;
    unsigned epoch_ms;
    int multi;
    unsigned iods; /* DF409 */
    unsigned clock_steering; /* DF411 */
    unsigned ext_clock;      /* DF412 */
    int divergence_free;     /* DF417 */
    unsigned smooth_interval; /* DF418 */
    int msm_type; /* 1–7 from message number; 0 if not MSM */

    unsigned sat_id[SQRTCM_MSM_MAX_SAT]; /* 1..64 in ascending mask order */
    int n_sat;
    unsigned sig_id[SQRTCM_MSM_MAX_SIG]; /* 1..32 */
    int n_sig;

    /* Per-satellite rough fields (index matches sat_id[]). */
    unsigned rough_ms[SQRTCM_MSM_MAX_SAT];  /* DF397; 255 = invalid */
    unsigned rough_mod[SQRTCM_MSM_MAX_SAT];  /* DF398, 0..1023 */
    int rough_rate[SQRTCM_MSM_MAX_SAT];      /* DF399; -8192 = invalid */
    unsigned char have_rough_range;         /* MSM1,3,4,5,6,7 */
    unsigned char have_rough_rate;          /* MSM5,7 */

    /* Cells in satellite-major × signal-minor order of set DF396 bits. */
    unsigned cell_sat[SQRTCM_MSM_MAX_CELL]; /* index into sat_id[] */
    unsigned cell_sig[SQRTCM_MSM_MAX_CELL];  /* index into sig_id[] */
    int n_cell;

    int fine_pr[SQRTCM_MSM_MAX_CELL];       /* DF400/DF405 raw */
    int fine_phase[SQRTCM_MSM_MAX_CELL];    /* DF401/DF406 raw */
    unsigned lock[SQRTCM_MSM_MAX_CELL];     /* DF402/DF407 */
    unsigned half_cycle[SQRTCM_MSM_MAX_CELL]; /* DF420 */
    unsigned cnr[SQRTCM_MSM_MAX_CELL];      /* DF403/DF408 */
    int fine_phaserate[SQRTCM_MSM_MAX_CELL]; /* DF404 raw */

    unsigned char have_fine_pr;
    unsigned char have_fine_phase;
    unsigned char have_lock;
    unsigned char have_half_cycle;
    unsigned char have_cnr;
    unsigned char have_fine_phaserate;
    unsigned char ext_res; /* 1 for MSM6/7 extended resolution */
} SqrtcmMsm;

/* CRC-24Q over data[0..n). Polynomial 0x1864CFB, init 0, no reflection. */
uint32_t sqrtcm_crc24q(const unsigned char *data, size_t n);

/* Category for a 12-bit message number. */
int sqrtcm_msg_category(unsigned msg_type);

/* MSM subtype 1–7 from a 1071–1127-style message number, or 0 if not MSM. */
int sqrtcm_msm_type(unsigned msg_type);

/* Pack a validated frame: 0xD3 | reserved+len | payload | CRC-24Q.
 * Returns total byte length, or 0 if cap is too small / plen > 1023. */
size_t sqrtcm_frame(unsigned char *out, size_t cap, const unsigned char *payload,
                    size_t plen);

/* Pack the MSM common header bits through DF418 (73 bits → 10 bytes) used by
 * 1071–1077 / 1081–1087 / 1091–1097 / 1101–1107 / 1111–1117 / 1121–1127.
 * IODS / reserved / clock / smoothing are left zero. Returns 10, or 0 on
 * bad args / overflow.
 * Note: 1081–1087 callers should eventually pass day+ms packing, not
 * GPS ms-of-week (documented stub; decode split is a later step). */
size_t sqrtcm_pack_msm_header(unsigned char *out, size_t cap, unsigned msg_type,
                              unsigned station_id, unsigned epoch_ms, int multi);

/* Pack a minimal legacy observation / station header: msg + station only
 * (3 bytes). Returns 3, or 0 on overflow. */
size_t sqrtcm_pack_obs_header(unsigned char *out, size_t cap, unsigned msg_type,
                              unsigned station_id);

/* Parse DF002 / DF003 (and MSM epoch / multi when category is MSM) from a
 * payload that already passed CRC. Returns 0, or -1 if too short. */
int sqrtcm_parse_header(const unsigned char *payload, size_t len, SqrtcmMsg *out);

/* Pack a full synthetic MSM payload (header + masks + obs fields) from *msm.
 * msm->msm_type / have_* flags select which DF blocks are written. Returns
 * byte length, or 0 on overflow / inconsistent masks. */
size_t sqrtcm_pack_msm(unsigned char *out, size_t cap, const SqrtcmMsm *msm);

/* Decode MSM header, DF394–DF396 masks and MSM1–7 observation fields.
 * Returns 0 on success, -1 if not MSM / truncated / inconsistent. */
int sqrtcm_parse_msm(const unsigned char *payload, size_t len, SqrtcmMsm *out);

/* Rough range in metres for sat_index, or NaN if invalid / absent. */
double sqrtcm_msm_rough_range_m(const SqrtcmMsm *m, int sat_index);

/* Full pseudorange / phaserange in metres for a cell (rough + fine), or NaN. */
double sqrtcm_msm_pseudorange_m(const SqrtcmMsm *m, int cell_index);
double sqrtcm_msm_phaserange_m(const SqrtcmMsm *m, int cell_index);

/* Copy payload into msg->payload_copy, set payload_owned, point payload there. */
void sqrtcm_msg_own_payload(SqrtcmMsg *msg);

/* After a plain struct copy (`dst = src`), re-point dst->payload into
 * dst->payload_copy when payload_owned. No-op for external borrows. */
void sqrtcm_msg_fix_payload(SqrtcmMsg *msg);

/* Struct assign that copies bytes then fixes the payload pointer. */
void sqrtcm_msg_assign(SqrtcmMsg *dst, const SqrtcmMsg *src);

/* Safe payload view: owned buffer when payload_owned, else payload borrow.
 * Safe after a plain `a = b` without calling fix_payload. */
static inline const unsigned char *sqrtcm_msg_payload(const SqrtcmMsg *msg) {
    if (!msg) return NULL;
    if (msg->payload_owned) return msg->payload_copy;
    return msg->payload;
}

/* ---- streaming framer ---- */

typedef struct {
    int state;
    size_t need; /* payload length from the length field */
    size_t len;  /* bytes stored in buf (includes preamble) */
    unsigned char buf[SQRTCM_MAX_FRAME];
    /* Input/rescan ring: new bytes and rejected-frame tails (from buf[1]).
     * Sized for worst-case single reject re-inject + another max frame.
     * inq_head is the next byte to take; inq_n is the live count. */
    unsigned char inq[SQRTCM_INQ_MAX];
    size_t inq_head;
    size_t inq_n;
    unsigned ok;
    unsigned bad_crc;
    unsigned junk;
    unsigned bad_len;     /* reserved bits set or length rejected */
    unsigned rescan_drop; /* bytes dropped when inq could not fit a prepend */
} SqrtcmFramer;

void sqrtcm_framer_init(SqrtcmFramer *f);

/* One byte in: 1 when a CRC-good frame completes (*out filled). After a bad
 * CRC or rejected length, bytes already buffered are re-queued from buf[1]
 * and drained so every good frame in that tail is delivered (byte API returns
 * one per call; further frames stay queued until the next call / feed).
 * On success, payload is owned in out. */
int sqrtcm_framer_byte(SqrtcmFramer *f, unsigned char b, SqrtcmMsg *out);

/* Feed a buffer (e.g. NTRIP stream body). Stores up to max_msgs good
 * frames in msgs[] with payloads copied into each msg's payload_copy.
 * Returns the number stored; framer->ok counts all good frames seen even
 * when msgs fills up. Drains the rescan/input queue so multi-frame rescans
 * are not truncated at the first hit. */
int sqrtcm_framer_feed(SqrtcmFramer *f, const void *data, size_t n, SqrtcmMsg *msgs,
                       int max_msgs);

#endif /* SQRTCM_H */
