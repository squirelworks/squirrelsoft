/*
 * sqrtcm: RTCM 3 frame sync, CRC-24Q and message-header parse for
 * SquirrelGNSS (original code).
 *
 * Buffer-fed only — no network and no third-party RTCM libraries. CI uses
 * synthetic frames built in-tree. Opaque NTRIP correction bodies from
 * sqntrip can be fed through SqrtcmFramer once a stream 200 body arrives.
 *
 * Layout follows the public RTCM 10403.x transport layer (preamble 0xD3,
 * 10-bit length, CRC-24Q) and the common 12+12-bit message / station
 * header shared by legacy observations, station ARP and MSM messages.
 * Satellite/signal masks and observation fields are left for a later step.
 *
 * GLONASS MSM (1081–1087) epoch note: the 30-bit DF004-style field is
 * day-of-week (3 bits) + ms-of-day (27 bits), not GPS ms-of-week. This
 * skeleton still exposes it as epoch_ms; split when GLONASS decode lands.
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

/* CRC-24Q over data[0..n). Polynomial 0x1864CFB, init 0, no reflection. */
uint32_t sqrtcm_crc24q(const unsigned char *data, size_t n);

/* Category for a 12-bit message number. */
int sqrtcm_msg_category(unsigned msg_type);

/* Pack a validated frame: 0xD3 | reserved+len | payload | CRC-24Q.
 * Returns total byte length, or 0 if cap is too small / plen > 1023. */
size_t sqrtcm_frame(unsigned char *out, size_t cap, const unsigned char *payload,
                    size_t plen);

/* Pack the MSM common header bits (GPS-style 30-bit epoch) used by
 * 1071–1077 / 1081–1087 / 1091–1097 / 1101–1107 / 1111–1117 / 1121–1127.
 * Writes 8 bytes (64 bits through the smoothing interval); remaining MSM
 * fields (masks, observations) are left zero / absent for the skeleton.
 * Returns 8, or 0 on bad args / overflow.
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
    /* Input/rescan queue: new bytes and rejected-frame tails (from buf[1]).
     * Sized for worst-case single reject re-inject + another max frame. */
    unsigned char inq[SQRTCM_INQ_MAX];
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
