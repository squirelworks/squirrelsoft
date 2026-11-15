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
 */
#ifndef SQRTCM_H
#define SQRTCM_H

#include <stddef.h>
#include <stdint.h>

#define SQRTCM_PREAMBLE 0xD3
#define SQRTCM_MAX_PAYLOAD 1023 /* 10-bit length field */
#define SQRTCM_MAX_FRAME (3 + SQRTCM_MAX_PAYLOAD + 3)

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
    unsigned epoch_ms; /* MSM GPS-style GNSS epoch (30 bits), when present */
    int have_epoch;
    int multi; /* MSM multiple-message bit, or -1 if unknown */
    int category;
    const unsigned char *payload; /* message body only (no preamble/CRC) */
    size_t payload_len;
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
 * Returns 8, or 0 on bad args / overflow. */
size_t sqrtcm_pack_msm_header(unsigned char *out, size_t cap, unsigned msg_type,
                              unsigned station_id, unsigned epoch_ms, int multi);

/* Pack a minimal legacy observation / station header: msg + station only
 * (3 bytes). Returns 3, or 0 on overflow. */
size_t sqrtcm_pack_obs_header(unsigned char *out, size_t cap, unsigned msg_type,
                              unsigned station_id);

/* Parse DF002 / DF003 (and MSM epoch / multi when category is MSM) from a
 * payload that already passed CRC. Returns 0, or -1 if too short. */
int sqrtcm_parse_header(const unsigned char *payload, size_t len, SqrtcmMsg *out);

/* ---- streaming framer ---- */

typedef struct {
    int state;
    size_t need; /* payload length from the length field */
    size_t len;  /* bytes stored in buf (includes preamble) */
    unsigned char buf[SQRTCM_MAX_FRAME];
    unsigned ok;
    unsigned bad_crc;
    unsigned junk;
    unsigned bad_len; /* reserved bits set or length rejected */
} SqrtcmFramer;

void sqrtcm_framer_init(SqrtcmFramer *f);
/* One byte in: 1 when a CRC-good frame completes (*out filled; payload
 * points into f->buf until the next call), else 0. */
int sqrtcm_framer_byte(SqrtcmFramer *f, unsigned char b, SqrtcmMsg *out);

/* Feed a buffer (e.g. NTRIP stream body). Stores up to max_msgs good
 * frames in msgs[]. Returns the number stored; framer->ok counts all
 * good frames seen even when msgs fills up. */
int sqrtcm_framer_feed(SqrtcmFramer *f, const void *data, size_t n, SqrtcmMsg *msgs,
                       int max_msgs);

#endif /* SQRTCM_H */
