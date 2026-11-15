/* RTCM 3: synthetic frames only. No network, no real caster dumps. */
#include "sqrtcm.h"
#include "sqntrip.h"

#include <math.h>
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
    unsigned char payload[SQRTCM_MSM_HEADER_BYTES];
    CHECK(sqrtcm_pack_msm_header(payload, sizeof payload, 1077, 2001, 345678901u, 1) ==
          SQRTCM_MSM_HEADER_BYTES);
    SqrtcmMsg m;
    CHECK(sqrtcm_parse_header(payload, SQRTCM_MSM_HEADER_BYTES, &m) == 0);
    CHECK(m.msg_type == 1077);
    CHECK(m.have_station && m.station_id == 2001);
    CHECK(m.have_epoch && m.epoch_ms == 345678901u);
    CHECK(m.multi == 1);
    CHECK(m.category == SQRTCM_CAT_MSM);
    CHECK(sqrtcm_msm_type(1077) == 7);
    CHECK(sqrtcm_msm_type(1074) == 4);
    CHECK(sqrtcm_msm_type(1004) == 0);

    CHECK(sqrtcm_pack_msm_header(payload, sizeof payload, 1004, 1, 0, 0) == 0); /* not MSM */
    CHECK(sqrtcm_pack_obs_header(payload, sizeof payload, 1004, 42) == 3);
    CHECK(sqrtcm_parse_header(payload, 3, &m) == 0);
    CHECK(m.msg_type == 1004 && m.station_id == 42 && m.category == SQRTCM_CAT_OBS);
    CHECK(!m.have_epoch && m.multi == -1);
}

static void test_frame_and_framer(void) {
    unsigned char payload[SQRTCM_MSM_HEADER_BYTES];
    CHECK(sqrtcm_pack_msm_header(payload, sizeof payload, 1084, 17, 1000u, 0) ==
          SQRTCM_MSM_HEADER_BYTES);
    unsigned char frame[32];
    size_t n = sqrtcm_frame(frame, sizeof frame, payload, SQRTCM_MSM_HEADER_BYTES);
    CHECK(n == 3 + SQRTCM_MSM_HEADER_BYTES + 3);

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
    CHECK(msg.payload_len == SQRTCM_MSM_HEADER_BYTES);
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
    unsigned char p1[SQRTCM_MSM_HEADER_BYTES], p2[3];
    CHECK(sqrtcm_pack_msm_header(p1, sizeof p1, 1075, 99, 42u, 0) == SQRTCM_MSM_HEADER_BYTES);
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

static void test_rescan_finds_preamble_after_bad_crc(void) {
    /* False preamble + short payload whose body starts a real good frame.
     * After bad CRC, framer must rescan from buf[1] and recover the good one. */
    unsigned char payload[3];
    CHECK(sqrtcm_pack_obs_header(payload, sizeof payload, 1005, 9) == 3);
    unsigned char good[16];
    size_t gn = sqrtcm_frame(good, sizeof good, payload, 3);
    CHECK(gn == 9);

    /* Craft a bad frame: 0xD3, len=gn (claims the good frame is the body),
     * then the good frame bytes as "payload+crc" so CRC fails, but good[0]
     * (0xD3) sits inside the discarded buffer. */
    unsigned char stream[64];
    size_t off = 0;
    stream[off++] = SQRTCM_PREAMBLE;
    stream[off++] = 0x00;
    stream[off++] = (unsigned char)gn; /* length = size of good frame */
    memcpy(stream + off, good, gn);
    off += gn;
    /* Fake CRC trailer (3 bytes) so the framer reaches finish; wrong CRC. */
    stream[off++] = 0x00;
    stream[off++] = 0x00;
    stream[off++] = 0x00;

    SqrtcmFramer f;
    SqrtcmMsg msgs[2];
    sqrtcm_framer_init(&f);
    int n = sqrtcm_framer_feed(&f, stream, off, msgs, 2);
    CHECK(f.bad_crc >= 1);
    CHECK(n >= 1);
    CHECK(msgs[0].msg_type == 1005 && msgs[0].station_id == 9);
}

static void test_feed_payload_not_stale(void) {
    /* Two frames in one feed: each msgs[i].payload must still match its header. */
    unsigned char p1[3], p2[3];
    CHECK(sqrtcm_pack_obs_header(p1, sizeof p1, 1005, 1) == 3);
    CHECK(sqrtcm_pack_obs_header(p2, sizeof p2, 1006, 2) == 3);
    unsigned char a[16], b[16];
    size_t na = sqrtcm_frame(a, sizeof a, p1, 3);
    size_t nb = sqrtcm_frame(b, sizeof b, p2, 3);
    unsigned char stream[32];
    memcpy(stream, a, na);
    memcpy(stream + na, b, nb);

    SqrtcmFramer f;
    SqrtcmMsg msgs[2];
    sqrtcm_framer_init(&f);
    CHECK(sqrtcm_framer_feed(&f, stream, na + nb, msgs, 2) == 2);
    CHECK(msgs[0].msg_type == 1005 && msgs[0].station_id == 1);
    CHECK(msgs[1].msg_type == 1006 && msgs[1].station_id == 2);
    CHECK(msgs[0].payload_len == 3 && msgs[1].payload_len == 3);
    /* Payload bytes must still decode as the first / second header. */
    SqrtcmMsg again;
    CHECK(sqrtcm_parse_header(sqrtcm_msg_payload(&msgs[0]), msgs[0].payload_len, &again) == 0);
    CHECK(again.msg_type == 1005 && again.station_id == 1);
    CHECK(sqrtcm_parse_header(sqrtcm_msg_payload(&msgs[1]), msgs[1].payload_len, &again) == 0);
    CHECK(again.msg_type == 1006 && again.station_id == 2);
    CHECK(msgs[0].payload_owned && msgs[0].payload == msgs[0].payload_copy);
    CHECK(msgs[1].payload_owned && msgs[1].payload == msgs[1].payload_copy);
}

static void test_rescan_multi_frame(void) {
    /* Bad outer frame whose body is two good frames: rescan must deliver both. */
    unsigned char p1[3], p2[3];
    CHECK(sqrtcm_pack_obs_header(p1, sizeof p1, 1005, 11) == 3);
    CHECK(sqrtcm_pack_obs_header(p2, sizeof p2, 1006, 22) == 3);
    unsigned char g1[16], g2[16];
    size_t n1 = sqrtcm_frame(g1, sizeof g1, p1, 3);
    size_t n2 = sqrtcm_frame(g2, sizeof g2, p2, 3);
    CHECK(n1 == 9 && n2 == 9);

    unsigned char stream[64];
    size_t off = 0;
    size_t body = n1 + n2;
    stream[off++] = SQRTCM_PREAMBLE;
    stream[off++] = 0x00;
    stream[off++] = (unsigned char)body;
    memcpy(stream + off, g1, n1);
    off += n1;
    memcpy(stream + off, g2, n2);
    off += n2;
    stream[off++] = 0x00; /* bogus CRC */
    stream[off++] = 0x00;
    stream[off++] = 0x00;

    SqrtcmFramer f;
    SqrtcmMsg msgs[4];
    sqrtcm_framer_init(&f);
    int n = sqrtcm_framer_feed(&f, stream, off, msgs, 4);
    CHECK(f.bad_crc >= 1);
    CHECK(n == 2);
    CHECK(f.ok == 2);
    CHECK(msgs[0].msg_type == 1005 && msgs[0].station_id == 11);
    CHECK(msgs[1].msg_type == 1006 && msgs[1].station_id == 22);
    CHECK(f.rescan_drop == 0);

    /* Byte API returns one frame per call; feed(..., 0) drains the rest. */
    SqrtcmFramer f2;
    SqrtcmMsg one, two;
    sqrtcm_framer_init(&f2);
    int got1 = 0;
    for (size_t i = 0; i < off; i++) {
        if (sqrtcm_framer_byte(&f2, stream[i], &one)) {
            CHECK(one.msg_type == 1005 && one.station_id == 11);
            got1 = 1;
        }
    }
    CHECK(got1);
    CHECK(sqrtcm_framer_feed(&f2, NULL, 0, &two, 1) == 1);
    CHECK(two.msg_type == 1006 && two.station_id == 22);
    CHECK(f2.ok == 2);
}

static void test_inq_prepend_overflow_drops_tail(void) {
    /* When prepend cannot fit, the whole rejected tail is dropped (counted
     * in rescan_drop) so any good frame inside that tail is lost. Normal
     * feed keeps the ring empty while mid-frame; stuff the ring full to
     * exercise the overflow safety path. */
    unsigned char payload[3];
    CHECK(sqrtcm_pack_obs_header(payload, sizeof payload, 1005, 3) == 3);
    unsigned char good[16];
    size_t gn = sqrtcm_frame(good, sizeof good, payload, 3);
    CHECK(gn == 9);

    unsigned char badstream[32];
    size_t blen = 0;
    badstream[blen++] = SQRTCM_PREAMBLE;
    badstream[blen++] = 0x00;
    badstream[blen++] = (unsigned char)gn;
    memcpy(badstream + blen, good, gn);
    blen += gn;
    badstream[blen++] = 0xAA;
    badstream[blen++] = 0xBB;
    badstream[blen++] = 0xCC;
    /* blen == 15; on reject, prepend size is blen - 1 == 14. */

    SqrtcmFramer f;
    SqrtcmMsg msgs[4];
    sqrtcm_framer_init(&f);
    /* Hold an incomplete bad frame in buf (queue empty). */
    CHECK(sqrtcm_framer_feed(&f, badstream, blen - 1, msgs, 4) == 0);
    CHECK(f.ok == 0 && f.bad_crc == 0);

    /* Completing CRC byte at the ring head; rest of the ring is occupied so
     * free space after take is 0 — prepend of 14 must fail. */
    memset(f.inq, 0x00, sizeof f.inq);
    f.inq[0] = badstream[blen - 1];
    f.inq_head = 0;
    f.inq_n = SQRTCM_INQ_MAX;

    unsigned drop0 = f.rescan_drop;
    int n = sqrtcm_framer_feed(&f, NULL, 0, msgs, 4);
    CHECK(f.bad_crc >= 1);
    CHECK(f.rescan_drop >= drop0 + (unsigned)(blen - 1));
    CHECK(n == 0);
    CHECK(f.ok == 0); /* good frame inside the dropped tail was not delivered */
}

static void test_msg_struct_copy_safety(void) {
    unsigned char p[3];
    CHECK(sqrtcm_pack_obs_header(p, sizeof p, 1005, 42) == 3);
    unsigned char frame[16];
    size_t n = sqrtcm_frame(frame, sizeof frame, p, 3);

    SqrtcmFramer f;
    SqrtcmMsg msg;
    sqrtcm_framer_init(&f);
    CHECK(sqrtcm_framer_feed(&f, frame, n, &msg, 1) == 1);
    CHECK(msg.payload_owned);
    CHECK(msg.payload == msg.payload_copy);
    unsigned char saved0 = sqrtcm_msg_payload(&msg)[0];
    unsigned char saved1 = sqrtcm_msg_payload(&msg)[1];

    /* Plain struct copy: raw .payload points at src's buffer; accessor / fix
     * / assign keep the destination's own copy. */
    SqrtcmMsg plain = msg;
    msg.payload_copy[0] ^= 0xFFu;
    CHECK(plain.payload == msg.payload_copy); /* stale raw pointer */
    CHECK(sqrtcm_msg_payload(&plain)[0] == saved0); /* owned flag => own buf */
    CHECK(sqrtcm_msg_payload(&plain)[1] == saved1);
    sqrtcm_msg_fix_payload(&plain);
    CHECK(plain.payload == plain.payload_copy);
    CHECK(plain.payload[0] == saved0);

    msg.payload_copy[0] = saved0; /* restore for assign check */
    SqrtcmMsg via_assign;
    sqrtcm_msg_assign(&via_assign, &msg);
    CHECK(via_assign.payload == via_assign.payload_copy);
    CHECK(via_assign.payload_owned);
    msg.payload_copy[0] ^= 0xFFu;
    CHECK(via_assign.payload[0] == saved0);
}

static void test_ntrip_body_glue(void) {
    /* Optional glue: after an NTRIP stream 200, feed body into sqrtcm. */
    unsigned char payload[SQRTCM_MSM_HEADER_BYTES];
    CHECK(sqrtcm_pack_msm_header(payload, sizeof payload, 1077, 1234, 555u, 0) ==
          SQRTCM_MSM_HEADER_BYTES);
    unsigned char frame[32];
    size_t flen = sqrtcm_frame(frame, sizeof frame, payload, SQRTCM_MSM_HEADER_BYTES);

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

static void fill_msm4_demo(SqrtcmMsm *m) {
    memset(m, 0, sizeof *m);
    m->msg_type = 1074;
    m->msm_type = 4;
    m->station_id = 2001;
    m->epoch_ms = 345600000u;
    m->multi = 0;
    m->n_sat = 2;
    m->sat_id[0] = 3;
    m->sat_id[1] = 7;
    m->n_sig = 2;
    m->sig_id[0] = 2; /* L1 C/A-style id in the 1..32 table */
    m->sig_id[1] = 16;
    m->n_cell = 3;
    m->cell_sat[0] = 0;
    m->cell_sig[0] = 0;
    m->cell_sat[1] = 0;
    m->cell_sig[1] = 1;
    m->cell_sat[2] = 1;
    m->cell_sig[2] = 0;
    m->rough_ms[0] = 70;
    m->rough_mod[0] = 512; /* +0.5 ms */
    m->rough_ms[1] = 71;
    m->rough_mod[1] = 0;
    m->fine_pr[0] = 100;
    m->fine_phase[0] = 200;
    m->lock[0] = 5;
    m->half_cycle[0] = 0;
    m->cnr[0] = 40;
    m->fine_pr[1] = -50;
    m->fine_phase[1] = -100;
    m->lock[1] = 3;
    m->half_cycle[1] = 1;
    m->cnr[1] = 35;
    m->fine_pr[2] = 0;
    m->fine_phase[2] = 1;
    m->lock[2] = 7;
    m->half_cycle[2] = 0;
    m->cnr[2] = 42;
}

static void test_msm4_obs_roundtrip(void) {
    SqrtcmMsm src, got;
    fill_msm4_demo(&src);
    unsigned char payload[256];
    size_t n = sqrtcm_pack_msm(payload, sizeof payload, &src);
    CHECK(n > SQRTCM_MSM_HEADER_BYTES);
    CHECK(sqrtcm_parse_msm(payload, n, &got) == 0);
    CHECK(got.msg_type == 1074 && got.msm_type == 4);
    CHECK(got.station_id == 2001 && got.epoch_ms == 345600000u);
    CHECK(got.n_sat == 2 && got.sat_id[0] == 3 && got.sat_id[1] == 7);
    CHECK(got.n_sig == 2 && got.sig_id[0] == 2 && got.sig_id[1] == 16);
    CHECK(got.n_cell == 3);
    CHECK(got.have_rough_range && !got.have_rough_rate);
    CHECK(got.have_fine_pr && got.have_fine_phase && got.have_cnr);
    CHECK(!got.ext_res && !got.have_fine_phaserate);
    CHECK(got.rough_ms[0] == 70 && got.rough_mod[0] == 512);
    CHECK(got.rough_ms[1] == 71 && got.rough_mod[1] == 0);
    CHECK(got.cell_sat[0] == 0 && got.cell_sig[0] == 0);
    CHECK(got.cell_sat[1] == 0 && got.cell_sig[1] == 1);
    CHECK(got.cell_sat[2] == 1 && got.cell_sig[2] == 0);
    CHECK(got.fine_pr[0] == 100 && got.fine_phase[0] == 200 && got.cnr[0] == 40);
    CHECK(got.fine_pr[1] == -50 && got.half_cycle[1] == 1 && got.cnr[1] == 35);
    CHECK(got.lock[2] == 7 && got.cnr[2] == 42);

    double rough0 = sqrtcm_msm_rough_range_m(&got, 0);
    CHECK(isfinite(rough0));
    CHECK(fabs(rough0 - 70.5 * SQRTCM_MSM_MS_TO_M) < 1e-6);
    double pr0 = sqrtcm_msm_pseudorange_m(&got, 0);
    CHECK(isfinite(pr0));
    CHECK(pr0 > rough0); /* fine_pr = +100 */
    double ph0 = sqrtcm_msm_phaserange_m(&got, 0);
    CHECK(isfinite(ph0));

    /* Header parse still works on the same payload. */
    SqrtcmMsg hdr;
    CHECK(sqrtcm_parse_header(payload, n, &hdr) == 0);
    CHECK(hdr.msg_type == 1074 && hdr.station_id == 2001);

    /* Framer delivers the payload intact for MSM parse. */
    unsigned char frame[512];
    size_t flen = sqrtcm_frame(frame, sizeof frame, payload, n);
    CHECK(flen > 0);
    SqrtcmFramer f;
    SqrtcmMsg msg;
    sqrtcm_framer_init(&f);
    CHECK(sqrtcm_framer_feed(&f, frame, flen, &msg, 1) == 1);
    SqrtcmMsm via_frame;
    CHECK(sqrtcm_parse_msm(sqrtcm_msg_payload(&msg), msg.payload_len, &via_frame) == 0);
    CHECK(via_frame.n_cell == 3 && via_frame.cnr[0] == 40);
}

static void test_msm7_obs_roundtrip(void) {
    SqrtcmMsm src, got;
    memset(&src, 0, sizeof src);
    src.msg_type = 1077;
    src.msm_type = 7;
    src.station_id = 99;
    src.epoch_ms = 1000u;
    src.n_sat = 1;
    src.sat_id[0] = 12;
    src.n_sig = 1;
    src.sig_id[0] = 2;
    src.n_cell = 1;
    src.cell_sat[0] = 0;
    src.cell_sig[0] = 0;
    src.rough_ms[0] = 65;
    src.rough_mod[0] = 256;
    src.rough_rate[0] = -12;
    src.fine_pr[0] = 1024;
    src.fine_phase[0] = -2048;
    src.lock[0] = 500;
    src.half_cycle[0] = 0;
    src.cnr[0] = 400; /* 10-bit extended CNR raw */
    src.fine_phaserate[0] = 7;

    unsigned char payload[256];
    size_t n = sqrtcm_pack_msm(payload, sizeof payload, &src);
    CHECK(n > 0);
    CHECK(sqrtcm_parse_msm(payload, n, &got) == 0);
    CHECK(got.msm_type == 7 && got.ext_res && got.have_rough_rate);
    CHECK(got.have_fine_phaserate && got.have_cnr);
    CHECK(got.rough_rate[0] == -12);
    CHECK(got.fine_pr[0] == 1024 && got.fine_phase[0] == -2048);
    CHECK(got.lock[0] == 500 && got.cnr[0] == 400);
    CHECK(got.fine_phaserate[0] == 7);
    CHECK(isfinite(sqrtcm_msm_pseudorange_m(&got, 0)));
    CHECK(isfinite(sqrtcm_msm_phaserange_m(&got, 0)));
}

static void test_msm_invalid_and_truncated(void) {
    SqrtcmMsm src, got;
    fill_msm4_demo(&src);
    src.rough_ms[0] = SQRTCM_MSM_ROUGH_MS_INVALID;
    src.fine_pr[0] = SQRTCM_MSM_FINE15_INVALID;
    unsigned char payload[256];
    size_t n = sqrtcm_pack_msm(payload, sizeof payload, &src);
    CHECK(n > 0);
    CHECK(sqrtcm_parse_msm(payload, n, &got) == 0);
    CHECK(isnan(sqrtcm_msm_rough_range_m(&got, 0)));
    CHECK(isnan(sqrtcm_msm_pseudorange_m(&got, 0)));
    /* Sat 1 still valid. */
    CHECK(isfinite(sqrtcm_msm_rough_range_m(&got, 1)));

    CHECK(sqrtcm_parse_msm(payload, 4, &got) == -1); /* truncated */
    CHECK(sqrtcm_parse_msm(payload, n, NULL) == -1);
    CHECK(sqrtcm_msm_type(9999) == 0);
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
    test_rescan_finds_preamble_after_bad_crc();
    test_feed_payload_not_stale();
    test_rescan_multi_frame();
    test_inq_prepend_overflow_drops_tail();
    test_msg_struct_copy_safety();
    test_ntrip_body_glue();
    test_msm4_obs_roundtrip();
    test_msm7_obs_roundtrip();
    test_msm_invalid_and_truncated();
}
