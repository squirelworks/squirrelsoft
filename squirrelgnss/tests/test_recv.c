/* Receiver streams (NMEA, UBX, the navigation message), L5 and the
 * ionosphere-free solution. All data is synthetic. */
#include "sqrecv.h"
#include "sqrinex.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int *nchecks, *nfails;
#define CHECK(c) check((c), #c, __LINE__)
static void check(int ok, const char *what, int line) {
    (*nchecks)++;
    if (!ok) {
        (*nfails)++;
        fprintf(stderr, "FAIL test_recv.c line %d: %s\n", line, what);
    }
}

static const SqgGeodetic SITE = {35.681236, 139.767125, 40.0};
#define WEEK 2444
#define T0 432000.0 /* a multiple of 16 s, as toe must be */

/* "$body*HH" with the right checksum */
static const char *sentence(const char *body) {
    static char buf[200];
    unsigned x = 0;
    for (const char *p = body; *p; p++) x ^= (unsigned char)*p;
    snprintf(buf, sizeof buf, "$%s*%02X", body, x);
    return buf;
}

static void test_nmea(void) {
    SqrxNmea n;
    sqrx_nmea_init(&n);
    /* the textbook GGA */
    const char *gga = sentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
    CHECK(sqrx_nmea_checksum_ok(gga));
    CHECK(sqrx_nmea_parse(&n, gga) == SQRX_NMEA_GGA);
    CHECK(n.have_pos && n.quality == 1 && n.sats_used == 8 && fabs(n.hdop - 0.9) < 1e-12);
    CHECK(fabs(n.pos.lat_deg - (48 + 7.038 / 60)) < 1e-12 && fabs(n.pos.lon_deg - (11 + 31.0 / 60)) < 1e-12);
    CHECK(fabs(n.msl_m - 545.4) < 1e-9 && fabs(n.pos.height_m - 592.3) < 1e-9);
    CHECK(fabs(n.utc_s - (12 * 3600 + 35 * 60 + 19)) < 1e-9);
    /* one changed character, or no checksum at all: refused */
    char bad[200];
    snprintf(bad, sizeof bad, "%s", gga);
    bad[10] = '6';
    CHECK(sqrx_nmea_parse(&n, bad) == -1);
    CHECK(sqrx_nmea_parse(&n, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,") == -1);
    /* southern and western, from another talker */
    CHECK(sqrx_nmea_parse(&n, sentence("GNGGA,000001.00,3352.1000,S,15112.6000,W,2,12,0.7,10.0,M,-20.0,M,1.0,0001")) ==
          SQRX_NMEA_GGA);
    CHECK(fabs(n.pos.lat_deg + (33 + 52.1 / 60)) < 1e-12 && fabs(n.pos.lon_deg + (151 + 12.6 / 60)) < 1e-12);
    CHECK(n.quality == 2 && fabs(n.pos.height_m + 10) < 1e-9);
    /* no fix: no position */
    CHECK(sqrx_nmea_parse(&n, sentence("GPGGA,000002.00,,,,,0,00,99.9,,,,,,")) == SQRX_NMEA_GGA && !n.have_pos);
    /* a number that is not one */
    CHECK(sqrx_nmea_parse(&n, sentence("GPGGA,123519,48x7.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,")) == -1);
    CHECK(sqrx_nmea_parse(&n, sentence("GPGGA,123519")) == -1);

    CHECK(sqrx_nmea_parse(&n, sentence("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W")) ==
          SQRX_NMEA_RMC);
    CHECK(n.rmc_valid && n.have_date && n.year == 1994 && n.month == 3 && n.day == 23);
    CHECK(fabs(n.speed_mps - 22.4 * 1852 / 3600) < 1e-9 && fabs(n.course_deg - 84.4) < 1e-12);
    CHECK(sqrx_nmea_parse(&n, sentence("GPRMC,123519,V,,,,,,,131126,,,N")) == SQRX_NMEA_RMC && !n.rmc_valid &&
          n.year == 2026);
    CHECK(sqrx_nmea_parse(&n, sentence("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,231394,003.1,W")) == -1);

    CHECK(sqrx_nmea_parse(&n, sentence("GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1")) == SQRX_NMEA_GSA);
    CHECK(n.fix_mode == 3 && n.n_used_prn == 5 && n.used_prn[2] == 9 && fabs(n.pdop - 2.5) < 1e-12 &&
          fabs(n.vdop - 2.1) < 1e-12);

    /* GSV: a set of two for GPS, then one for GLONASS: both kept */
    CHECK(sqrx_nmea_parse(&n, sentence("GPGSV,2,1,06,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45")) ==
          SQRX_NMEA_GSV);
    CHECK(n.n_view == 0); /* not whole yet */
    CHECK(sqrx_nmea_parse(&n, sentence("GPGSV,2,2,06,15,60,050,,17,05,180,30")) == SQRX_NMEA_GSV);
    CHECK(n.n_view == 6 && n.view[4].prn == 15 && n.view[4].snr_dbhz == 0 && n.view[5].az_deg == 180);
    CHECK(sqrx_nmea_parse(&n, sentence("GLGSV,1,1,02,65,30,100,40,66,10,200,35")) == SQRX_NMEA_GSV);
    CHECK(n.n_view == 8 && n.view[6].sys == 'L' && n.view[6].prn == 65);
    /* a new GPS set replaces only GPS; one out of order is dropped */
    CHECK(sqrx_nmea_parse(&n, sentence("GPGSV,2,2,05,30,10,010,20")) == SQRX_NMEA_GSV);
    CHECK(n.n_view == 8);
    CHECK(sqrx_nmea_parse(&n, sentence("GPGSV,1,1,01,30,10,010,20")) == SQRX_NMEA_GSV);
    CHECK(n.n_view == 3);

    /* the writers read back */
    char out[600];
    SqgGeodetic p = {-33.8688, 151.2093, 58.123};
    size_t len = sqrx_nmea_gga(out, sizeof out, 3723.5, p, 1, 9, 0.8);
    CHECK(len > 0 && out[len - 2] == '\r' && out[len - 1] == '\n');
    out[len - 2] = 0;
    CHECK(sqrx_nmea_parse(&n, out) == SQRX_NMEA_GGA);
    CHECK(fabs(n.pos.lat_deg - p.lat_deg) < 1e-9 && fabs(n.pos.lon_deg - p.lon_deg) < 1e-9 &&
          fabs(n.pos.height_m - p.height_m) < 1e-6 && fabs(n.utc_s - 3723.5) < 1e-9 && n.sats_used == 9);
    len = sqrx_nmea_rmc(out, sizeof out, 3723.5, 2026, 11, 13, p, 5.0, 271.5);
    out[len - 2] = 0;
    CHECK(sqrx_nmea_parse(&n, out) == SQRX_NMEA_RMC && fabs(n.speed_mps - 5.0) < 1e-3 && n.day == 13);
    SqrxSatView sv[6];
    for (int i = 0; i < 6; i++) sv[i] = (SqrxSatView){'P', i + 3, 10.0 * i, 50.0 * i, 30 + i};
    len = sqrx_nmea_gsv(out, sizeof out, sv, 6);
    char small[20];
    CHECK(len > 0 && sqrx_nmea_gsv(small, sizeof small, sv, 6) == 0);
    SqrxNmea m;
    sqrx_nmea_init(&m);
    for (char *s = out, *e; (e = strstr(s, "\r\n")); s = e + 2) {
        *e = 0;
        CHECK(sqrx_nmea_parse(&m, s) == SQRX_NMEA_GSV);
    }
    CHECK(m.n_view == 6 && m.view[5].prn == 8 && m.view[5].az_deg == 250 && m.view[5].snr_dbhz == 35);
}

static void test_framer(void) {
    static unsigned char s[4096];
    size_t n = 0;
    const char *junk = "\x01\x02garbage\xb5\x00";
    memcpy(s + n, junk, strlen(junk) + 1);
    n += strlen(junk) + 1;
    n += (size_t)sprintf((char *)s + n, "%s\r\n", sentence("GPGSA,A,1,,,,,,,,,,,,,,,"));
    n += (size_t)sprintf((char *)s + n, "$GPGGA,1,2,3*00\r\n"); /* bad checksum */
    unsigned char pl[3] = {1, 2, 3};
    size_t good = sqrx_ubx_frame(s + n, sizeof s - n, 0x0A, 0x04, pl, 3);
    n += good;
    size_t bad_at = n;
    n += sqrx_ubx_frame(s + n, sizeof s - n, 0x0A, 0x04, pl, 3);
    s[bad_at + 7] ^= 0xFF; /* corrupt the payload */
    /* an impossible length */
    unsigned char huge[6] = {0xB5, 0x62, 1, 2, 0xFF, 0xFF};
    memcpy(s + n, huge, 6);
    n += 6;
    n += (size_t)sprintf((char *)s + n, "$GPGGA,trunc%s\r\n", sentence("GPGSA,A,3,01,02,03,04,,,,,,,,,1.0,1.0,1.0"));
    n += sqrx_ubx_frame(s + n, sizeof s - n, 0x01, 0x07, NULL, 0);
    SqrxFramer f;
    sqrx_framer_init(&f);
    SqrxFrame fr;
    int nmea = 0, ubx = 0, last_gsa_ok = 0;
    for (size_t i = 0; i < n; i++)
        if (sqrx_framer_byte(&f, s[i], &fr)) {
            if (fr.kind == SQRX_FRAME_NMEA) {
                nmea++;
                if (strstr(fr.nmea, "GPGSA,A,3")) last_gsa_ok = 1;
            } else {
                ubx++;
                CHECK(fr.cls == 0x0A || (fr.cls == 1 && fr.id == 7 && fr.len == 0));
                if (fr.cls == 0x0A) CHECK(fr.len == 3 && fr.payload[2] == 3);
            }
        }
    CHECK(nmea == 2 && ubx == 2 && last_gsa_ok);
    CHECK(f.nmea_ok == 2 && f.nmea_bad >= 2 && f.ubx_ok == 2 && f.ubx_bad == 2 && f.junk > 5);
    CHECK(f.bytes == (long)n);

    /* CFG-MSG, checked by hand: B5 62 06 01 03 00 02 15 01 22 70 */
    unsigned char cfg[16];
    const unsigned char want[11] = {0xB5, 0x62, 0x06, 0x01, 0x03, 0x00, 0x02, 0x15, 0x01, 0x22, 0x70};
    CHECK(sqrx_ubx_cfg_msg(cfg, sizeof cfg, SQRX_UBX_RXM, SQRX_UBX_RXM_RAWX, 1) == 11 && !memcmp(cfg, want, 11));
    CHECK(sqrx_ubx_cfg_msg(cfg, 10, 2, 0x15, 1) == 0);
}

static void test_lnav(const SqgEphemeris *eph, int n_eph) {
    SqrxSubframe sf[3];
    double worst = 0;
    for (int i = 0; i < n_eph; i++) {
        CHECK(sqrx_lnav_encode(&eph[i], 40 + i, T0 - 18, sf) == 0);
        SqgEphemeris d;
        CHECK(sqrx_lnav_decode((const SqrxSubframe *)sf, WEEK, &d) == 0);
        CHECK(d.week == WEEK && d.toe == eph[i].toe && d.toc == eph[i].toc && d.health == 0);
        CHECK(fabs(d.af0 - eph[i].af0) <= ldexp(1, -32) && fabs(d.tgd - eph[i].tgd) <= ldexp(1, -32));
        d.prn = eph[i].prn;
        for (double dt = -7200; dt <= 7200; dt += 1800) {
            SqgEcef a = sqg_sat_position(&eph[i], T0 + dt), b = sqg_sat_position(&d, T0 + dt);
            double e = sqg_baseline_length(a, b);
            if (e > worst) worst = e;
            double c = fabs(sqg_sat_clock(&eph[i], T0 + dt) - sqg_sat_clock(&d, T0 + dt)) * SQG_C_LIGHT;
            CHECK(c < 0.15); /* af0 and tgd to 2^-31 s each: up to 7 cm apiece */
        }
    }
    CHECK(worst < 0.1); /* the message's own resolution: centimetres */
    /* the 10-bit week near the hint, either side of a rollover */
    CHECK(sqrx_lnav_encode(&eph[0], 7, T0 - 18, sf) == 0);
    SqgEphemeris d;
    sqrx_lnav_decode((const SqrxSubframe *)sf, 0, &d);
    CHECK(d.week == 2048 + WEEK % 1024);
    sqrx_lnav_decode((const SqrxSubframe *)sf, WEEK + 3, &d);
    CHECK(d.week == WEEK);
    sqrx_lnav_decode((const SqrxSubframe *)sf, WEEK + 1024, &d);
    CHECK(d.week == WEEK + 1024);
    /* issues that disagree, subframes out of order */
    SqrxSubframe other[3];
    sqrx_lnav_encode(&eph[0], 8, T0 - 18, other);
    SqrxSubframe mix[3];
    memcpy(mix[0], sf[0], sizeof sf[0]);
    memcpy(mix[1], other[1], sizeof sf[0]);
    memcpy(mix[2], sf[2], sizeof sf[0]);
    CHECK(sqrx_lnav_decode((const SqrxSubframe *)mix, WEEK, &d) == -1);
    memcpy(mix[1], sf[2], sizeof sf[0]);
    memcpy(mix[2], sf[1], sizeof sf[0]);
    CHECK(sqrx_lnav_decode((const SqrxSubframe *)mix, WEEK, &d) == -1);
    /* values that do not fit */
    SqgEphemeris x = eph[0];
    x.af0 = 0.01;
    CHECK(sqrx_lnav_encode(&x, 1, T0, sf) == -1);
    x = eph[0];
    x.toe += 3;
    CHECK(sqrx_lnav_encode(&x, 1, T0, sf) == -1);
}

static size_t stream_len;
static unsigned char stream[1 << 20];

static void emit(size_t n) {
    stream_len += n;
    if (!n) check(0, "stream full", __LINE__);
}

static void test_rawx(void) {
    SqgEpoch ep;
    memset(&ep, 0, sizeof ep);
    ep.t_receiver = T0 + 0.5;
    ep.n_obs = 3;
    for (int i = 0; i < 3; i++) {
        SqgObservation *o = &ep.obs[i];
        o->prn = 5 + i;
        o->valid = 1;
        o->pseudorange_m = 2.1e7 + 1234.5678 * i;
        o->carrier_cyc = 1.1e8 + 0.125 * i;
        o->has_carrier = i != 2;
        o->snr_dbhz = 41;
        if (i == 0) {
            o->pseudorange2_m = 2.1e7 + 3.25;
            o->carrier2_cyc = 8.6e7 + 0.5;
            o->has_l2 = 1;
        }
        if (i < 2) {
            o->pseudorange5_m = 2.1e7 + 5.75;
            o->carrier5_cyc = 8.2e7 + 0.25;
            o->has_l5 = o->has_l5_carrier = 1;
        }
    }
    SqrxReceiver r;
    sqrx_receiver_init(&r);
    unsigned char buf[4096];
    for (int k = 0; k < 3; k++) {
        ep.obs[0].slip = k == 1;
        size_t n = sqrx_ubx_rawx(buf, sizeof buf, &ep, WEEK);
        CHECK(n == 8 + 16 + 32 * 6u);
        int ev = 0;
        for (size_t i = 0; i < n; i++) ev |= sqrx_receiver_byte(&r, buf[i]);
        CHECK(ev == SQRX_EV_EPOCH && r.week == WEEK && r.epoch.t_receiver == ep.t_receiver);
        CHECK(r.epoch.n_obs == 3);
        const SqgObservation *a = &r.epoch.obs[0], *b = &r.epoch.obs[1], *c = &r.epoch.obs[2];
        CHECK(a->prn == 5 && a->pseudorange_m == ep.obs[0].pseudorange_m && a->carrier_cyc == ep.obs[0].carrier_cyc);
        CHECK(a->has_l2 && a->pseudorange2_m == ep.obs[0].pseudorange2_m && a->carrier2_cyc == ep.obs[0].carrier2_cyc);
        CHECK(a->has_l5 && a->has_l5_carrier && a->pseudorange5_m == ep.obs[0].pseudorange5_m);
        CHECK(!b->has_l2 && b->has_l5 && c->has_carrier == 0 && !c->has_l5 && c->snr_dbhz == 41);
        CHECK(a->slip == (k == 1)); /* the lock time went back to 0 */
        CHECK(!b->slip && !c->slip);
    }
    /* a satellite seen only on L5 is no observation */
    ep.n_obs = 1;
    ep.obs[0].valid = 1;
    size_t n = sqrx_ubx_rawx(buf, sizeof buf, &ep, WEEK);
    buf[6 + 16 + 30] = 0; /* L1: not tracked */
    unsigned char a = 0, bb = 0;
    for (size_t i = 2; i < n - 2; i++) {
        a += buf[i];
        bb += a;
    }
    buf[n - 2] = a;
    buf[n - 1] = bb;
    for (size_t i = 0; i < n; i++) sqrx_receiver_byte(&r, buf[i]);
    CHECK(r.epoch.n_obs == 0);

    SqrxNavPvt v = {T0 + 1, 2026, 11, 13, 0, 0, 1.25, 3, 11, {35.6812361, 139.7671249, 40.123}, 1.5, 2.5, 3.25, 91.5,
                    1.2};
    n = sqrx_ubx_navpvt(buf, sizeof buf, &v);
    int ev = 0;
    for (size_t i = 0; i < n; i++) ev |= sqrx_receiver_byte(&r, buf[i]);
    CHECK(ev == SQRX_EV_NAVPVT && r.have_navpvt);
    CHECK(r.navpvt.fix_type == 3 && r.navpvt.num_sv == 11 && fabs(r.navpvt.pos.lat_deg - v.pos.lat_deg) < 1e-7 &&
          fabs(r.navpvt.pos.height_m - v.pos.height_m) < 1e-3 && fabs(r.navpvt.sec - 1.25) < 1e-6 &&
          fabs(r.navpvt.speed_mps - 3.25) < 1e-3 && fabs(r.navpvt.pdop - 1.2) < 1e-9);
}

/* a whole receiver stream: navigation message, NMEA and raw epochs, solved here */
static void test_stream(SqgEphemeris *eph, int n_eph) {
    /* what the receiver broadcasts is the message's resolution: the sky is built from that */
    SqgEphemeris dec[SQG_DEMO_SATS];
    stream_len = 0;
    for (int i = 0; i < n_eph; i++) {
        eph[i].week = WEEK;
        SqrxSubframe sf[3];
        CHECK(sqrx_lnav_encode(&eph[i], 3, T0 - 18, sf) == 0);
        sqrx_lnav_decode((const SqrxSubframe *)sf, WEEK, &dec[i]);
        dec[i].prn = eph[i].prn;
        for (int k = 0; k < 3; k++) emit(sqrx_ubx_sfrbx(stream + stream_len, sizeof stream - stream_len, eph[i].prn, sf[k]));
    }
    SqgEcef truth;
    sqg_geodetic_to_ecef(SITE, &truth);
    for (int k = 0; k < 5; k++) {
        SqgDemoOptions o = {120.0 + k, 5.0, 0.0, 77u + (unsigned)k, 5, 1, 6.0, 1};
        SqgEpoch ep;
        sqg_demo_epoch_opt(dec, n_eph, SITE, T0 + k, &o, &ep);
        char gga[120];
        size_t g = sqrx_nmea_gga(gga, sizeof gga, T0 + k - 18, SITE, 1, ep.n_obs, 0.9);
        memcpy(stream + stream_len, gga, g);
        emit(g);
        emit(sqrx_ubx_rawx(stream + stream_len, sizeof stream - stream_len, &ep, WEEK));
        stream[stream_len] = 0x55; /* a stray byte between messages */
        emit(1);
    }
    SqrxReceiver r;
    sqrx_receiver_init(&r);
    SqgPvtOptions l1, ifree;
    sqg_pvt_options_default(&l1);
    ifree = l1;
    ifree.iono_free = 1;
    int epochs = 0, nmea = 0, ephs = 0;
    double worst_if = 0, worst_l1_up = 0;
    for (size_t i = 0; i < stream_len; i++) {
        int ev = sqrx_receiver_byte(&r, stream[i]);
        if (ev & SQRX_EV_NMEA) nmea++;
        if (ev & SQRX_EV_EPHEMERIS) ephs++;
        if (!(ev & SQRX_EV_EPOCH)) continue;
        epochs++;
        SqgEphemeris have[32];
        int nh = sqrx_receiver_ephemerides(&r, have, 32);
        CHECK(nh == n_eph);
        SqgPvt a, b;
        SqgEcef zero = {0, 0, 0};
        CHECK(sqg_solve_pvt_opt(&r.epoch, have, nh, zero, &ifree, &b) == 0);
        CHECK(sqg_solve_pvt_opt(&r.epoch, have, nh, zero, &l1, &a) == 0);
        SqgEnu e;
        sqg_ecef_to_enu(truth, b.pos, SITE, &e);
        double d = sqrt(e.e * e.e + e.n * e.n + e.u * e.u);
        if (d > worst_if) worst_if = d;
        sqg_ecef_to_enu(truth, a.pos, SITE, &e);
        if (fabs(e.u) > worst_l1_up) worst_l1_up = fabs(e.u);
    }
    CHECK(epochs == 5 && nmea == 5 && ephs == n_eph);
    CHECK(r.framer.junk == 5 && r.framer.ubx_bad == 0);
    CHECK(worst_if < 0.01);    /* the ionosphere gone, the group delays cancelled */
    CHECK(worst_l1_up > 3.0);  /* L1 alone carries the 6 m ionosphere */
    CHECK(r.nmea.have_pos && fabs(r.nmea.pos.lat_deg - SITE.lat_deg) < 1e-9);

    /* L1/L5 alone (no L2): the same, as far as the demo has no L5 bias */
    SqgDemoOptions o5 = {50.0, 5.0, 0.0, 9u, 5, 0, 6.0, 1};
    SqgEpoch ep;
    sqg_demo_epoch_opt(dec, n_eph, SITE, T0 + 9, &o5, &ep);
    SqgPvt p;
    SqgEcef zero = {0, 0, 0};
    CHECK(sqg_solve_pvt_opt(&ep, dec, n_eph, zero, &ifree, &p) == 0);
    CHECK(sqg_baseline_length(p.pos, truth) < 0.01);
    /* no second band: no satellites for the ionosphere-free solution */
    SqgDemoOptions o1 = {50.0, 5.0, 0.0, 9u, 5, 0, 6.0, 0};
    sqg_demo_epoch_opt(dec, n_eph, SITE, T0 + 9, &o1, &ep);
    CHECK(sqg_solve_pvt_opt(&ep, dec, n_eph, zero, &ifree, &p) == -1 && p.n_used == 0);
}

static void test_rinex_l5(const SqgEphemeris *eph, int n_eph) {
    SqgDemoOptions o = {10.0, 5.0, 0.3, 21u, 5, 1, 2.0, 1};
    SqgEpoch ep[2];
    sqg_demo_epoch_opt(eph, n_eph, SITE, T0, &o, &ep[0]);
    o.dual = 0; /* the second epoch has L5 but not L2 */
    sqg_demo_epoch_opt(eph, n_eph, SITE, T0 + 30, &o, &ep[1]);
    ep[1].obs[0].has_l5_carrier = 0;
    FILE *f = tmpfile();
    CHECK(f != NULL);
    if (!f) return;
    sqr_write_obs_header_bands(f, NULL, SQR_BANDS_L2 | SQR_BANDS_L5);
    for (int k = 0; k < 2; k++) sqr_write_obs_epoch_bands(f, WEEK, &ep[k], SQR_BANDS_L2 | SQR_BANDS_L5);
    long len = ftell(f);
    rewind(f);
    char *text = malloc((size_t)len + 1);
    CHECK(text && fread(text, 1, (size_t)len, f) == (size_t)len);
    fclose(f);
    if (!text) return;
    text[len] = 0;
    CHECK(strstr(text, "G    7 C1C L1C S1C C2W L2W C5Q L5Q") != NULL);
    SqrObs r;
    SqrError err;
    CHECK(sqr_obs_open(&r, text, &err) == 0 && r.code5_index == 5 && r.carrier5_index == 6);
    for (int k = 0; k < 2; k++) {
        SqgEpoch got;
        int week;
        CHECK(sqr_obs_next(&r, &got, &week, &err) == 1 && got.n_obs == ep[k].n_obs);
        for (int i = 0; i < got.n_obs; i++) {
            const SqgObservation *a = &got.obs[i], *b = &ep[k].obs[i];
            CHECK(a->has_l2 == b->has_l2 && a->has_l5 && a->has_l5_carrier == b->has_l5_carrier);
            CHECK(fabs(a->pseudorange5_m - b->pseudorange5_m) < 6e-4);
            if (b->has_l5_carrier) CHECK(fabs(a->carrier5_cyc - b->carrier5_cyc) < 6e-4);
        }
    }
    free(text);
}

void test_recv_all(int *checks, int *failures) {
    nchecks = checks;
    nfails = failures;
    SqgEphemeris eph[SQG_DEMO_SATS];
    int n_eph;
    sqg_demo_ephemerides(eph, &n_eph, T0);
    for (int i = 0; i < n_eph; i++) eph[i].week = WEEK;
    test_nmea();
    test_framer();
    test_lnav(eph, n_eph);
    test_rawx();
    test_stream(eph, n_eph);
    test_rinex_l5(eph, n_eph);
}
