/* RTCM 3: synthetic frames only. No network, no real caster dumps. */
#include "sqrtcm.h"
#include "sqntrip.h"

#include <stdio.h>
#include <string.h>

static int *nchecks, *nfails;
#define CHECK(c) check((c), #c, __LINE__)
static void check(int ok, const char *what, int line) {
    (*nchecks)++;
    if (!ok) {
        (*nfails)++;
        fprintf(stderr, "FAIL test_rtcm.c line %d: %s\n", line, what);
    }
}

static void test_crc_known(void) {
    /* Empty length-0 frame: preamble + len0 + CRC. */
    unsigned char empty[6];
    CHECK(sqrtcm_frame(empty, sizeof empty, NULL, 0) == 6);
    CHECK(empty[0] == 0xD3 && empty[1] == 0x00 && empty[2] == 0x00);
    uint32_t crc = sqrtcm_crc24q(empty, 3);
    CHECK(((crc >> 16) & 0xFF) == empty[3]);
    CHECK(((crc >> 8) & 0xFF) == empty[4]);
    CHECK((crc & 0xFF) == empty[5]);
    /* Recomputed CRC over the whole preamble+len matches the trailer. */
    CHECK(sqrtcm_crc24q(empty, 3) ==
          (((uint32_t)empty[3] << 16) | ((uint32_t)empty[4] << 8) | empty[5]));
}

static void test_categories(void) {
    CHECK(sqrtcm_msg_category(1004) == SQRTCM_CAT_OBS);
    CHECK(sqrtcm_msg_category(1012) == SQRTCM_CAT_OBS);
    CHECK(sqrtcm_msg_category(1005) == SQRTCM_CAT_STATION);
    CHECK(sqrtcm_msg_category(1006) == SQRTCM_CAT_STATION);
    CHECK(sqrtcm_msg_category(1077) == SQRTCM_CAT_MSM);
    CHECK(sqrtcm_msg_category(1087) == SQRTCM_CAT_MSM);
    CHECK(sqrtcm_msg_category(1127) == SQRTCM_CAT_MSM);
    CHECK(sqrtcm_msg_category(1019) == SQRTCM_CAT_OTHER); /* GPS ephemeris */
    CHECK(sqrtcm_msg_category(1) == SQRTCM_CAT_UNKNOWN);
    CHECK(sqrtcm_msg_category(9999) == SQRTCM_CAT_UNKNOWN);
}

static void test_msm_header_roundtrip(void) {
    unsigned char payload[8];
    CHECK(sqrtcm_pack_msm_header(payload, sizeof payload, 1077, 2001, 345678901u, 1) == 8);
    SqrtcmMsg m;
    CHECK(sqrtcm_parse_header(payload, 8, &m) == 0);
    CHECK(m.msg_type == 1077);
    CHECK(m.have_station && m.station_id == 2001);
    CHECK(m.have_epoch && m.epoch_ms == 345678901u);
    CHECK(m.multi == 1);
    CHECK(m.category == SQRTCM_CAT_MSM);

    CHECK(sqrtcm_pack_msm_header(payload, sizeof payload, 1004, 1, 0, 0) == 0); /* not MSM */
    CHECK(sqrtcm_pack_obs_header(payload, sizeof payload, 1004, 42) == 3);
    CHECK(sqrtcm_parse_header(payload, 3, &m) == 0);
    CHECK(m.msg_type == 1004 && m.station_id == 42 && m.category == SQRTCM_CAT_OBS);
    CHECK(!m.have_epoch && m.multi == -1);
}

static void test_frame_and_framer(void) {
    unsigned char payload[8];
    CHECK(sqrtcm_pack_msm_header(payload, sizeof payload, 1084, 17, 1000u, 0) == 8);
    unsigned char frame[32];
    size_t n = sqrtcm_frame(frame, sizeof frame, payload, 8);
    CHECK(n == 3 + 8 + 3);

    SqrtcmFramer f;
    SqrtcmMsg msg;
    sqrtcm_framer_init(&f);
    int got = 0;
    for (size_t i = 0; i < n; i++) {
        if (sqrtcm_framer_byte(&f, frame[i], &msg)) got = 1;
    }
    CHECK(got && f.ok == 1 && f.bad_crc == 0);
    CHECK(msg.msg_type == 1084 && msg.station_id == 17 && msg.epoch_ms == 1000u);
    CHECK(msg.multi == 0 && msg.category == SQRTCM_CAT_MSM);
    CHECK(msg.payload_len == 8);
}

static void test_resync_and_bad_crc(void) {
    unsigned char payload[3];
    CHECK(sqrtcm_pack_obs_header(payload, sizeof payload, 1005, 7) == 3);
    unsigned char good[16];
    size_t gn = sqrtcm_frame(good, sizeof good, payload, 3);
    CHECK(gn == 9);

    unsigned char stream[64];
    size_t off = 0;
    stream[off++] = 0x00; /* junk */
    stream[off++] = 0xFF;
    memcpy(stream + off, good, gn);
    off += gn;
    /* Corrupted copy: flip one payload bit after a fresh CRC'd frame. */
    unsigned char bad[16];
    memcpy(bad, good, gn);
    bad[4] ^= 0x01;
    memcpy(stream + off, bad, gn);
    off += gn;
    memcpy(stream + off, good, gn);
    off += gn;

    SqrtcmFramer f;
    SqrtcmMsg msgs[4];
    sqrtcm_framer_init(&f);
    int n = sqrtcm_framer_feed(&f, stream, off, msgs, 4);
    CHECK(n == 2);
    CHECK(f.ok == 2 && f.bad_crc == 1 && f.junk >= 2);
    CHECK(msgs[0].msg_type == 1005 && msgs[0].station_id == 7);
    CHECK(msgs[1].msg_type == 1005 && msgs[1].station_id == 7);
}

static void test_chunked_feed(void) {
    unsigned char p1[8], p2[3];
    CHECK(sqrtcm_pack_msm_header(p1, sizeof p1, 1075, 99, 42u, 0) == 8);
    CHECK(sqrtcm_pack_obs_header(p2, sizeof p2, 1006, 5) == 3);
    unsigned char a[32], b[16];
    size_t na = sqrtcm_frame(a, sizeof a, p1, 8);
    size_t nb = sqrtcm_frame(b, sizeof b, p2, 3);
    unsigned char stream[64];
    memcpy(stream, a, na);
    memcpy(stream + na, b, nb);

    SqrtcmFramer f;
    SqrtcmMsg msgs[2];
    sqrtcm_framer_init(&f);
    /* Awkward chunks across frame boundaries. */
    CHECK(sqrtcm_framer_feed(&f, stream, 1, msgs, 2) == 0);
    CHECK(sqrtcm_framer_feed(&f, stream + 1, 5, msgs, 2) == 0);
    int n1 = sqrtcm_framer_feed(&f, stream + 6, na - 6, msgs, 2);
    CHECK(n1 == 1 && msgs[0].msg_type == 1075 && msgs[0].station_id == 99);
    int n2 = sqrtcm_framer_feed(&f, stream + na, nb, msgs + 1, 1);
    CHECK(n2 == 1 && msgs[1].msg_type == 1006 && msgs[1].station_id == 5);
    CHECK(f.ok == 2);
}

static void test_reserved_length_bits(void) {
    /* Reserved bits in the length word must be zero; otherwise resync. */
    unsigned char bogus[] = {0xD3, 0x04, 0x00, 0x00, 0x00, 0x00};
    SqrtcmFramer f;
    SqrtcmMsg m;
    sqrtcm_framer_init(&f);
    for (size_t i = 0; i < sizeof bogus; i++) sqrtcm_framer_byte(&f, bogus[i], &m);
    CHECK(f.ok == 0 && f.bad_len >= 1);
}

static void test_ntrip_body_glue(void) {
    /* Optional glue: after an NTRIP stream 200, feed body into sqrtcm. */
    unsigned char payload[8];
    CHECK(sqrtcm_pack_msm_header(payload, sizeof payload, 1077, 1234, 555u, 0) == 8);
    unsigned char frame[32];
    size_t flen = sqrtcm_frame(frame, sizeof frame, payload, 8);

    unsigned char reply[128];
    size_t rlen = 0;
    const char *hdr = "ICY 200 OK\r\nServer: Synthetic\r\n\r\n";
    memcpy(reply, hdr, strlen(hdr));
    rlen = strlen(hdr);
    memcpy(reply + rlen, frame, flen);
    rlen += flen;

    unsigned char body[128];
    SqntParser p;
    sqnt_parser_init(&p, body, sizeof body);
    CHECK(sqnt_parser_feed(&p, reply, rlen) == 0);
    CHECK(p.header_done && p.status == 200 && p.kind == SQNT_KIND_STREAM);
    CHECK(p.body_len == flen);

    SqrtcmFramer f;
    SqrtcmMsg msgs[2];
    sqrtcm_framer_init(&f);
    CHECK(sqrtcm_framer_feed(&f, body, p.body_len, msgs, 2) == 1);
    CHECK(msgs[0].msg_type == 1077 && msgs[0].station_id == 1234);
    CHECK(msgs[0].epoch_ms == 555u && msgs[0].category == SQRTCM_CAT_MSM);
}

void test_rtcm_all(int *checks, int *failures) {
    nchecks = checks;
    nfails = failures;
    test_crc_known();
    test_categories();
    test_msm_header_roundtrip();
    test_frame_and_framer();
    test_resync_and_bad_crc();
    test_chunked_feed();
    test_reserved_length_bits();
    test_ntrip_body_glue();
}
