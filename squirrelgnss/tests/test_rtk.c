/* Epoch stream, stakeout guidance, DGNSS and static RTK tests. All data is
 * synthetic (sqg_demo_*). */
#include "sqrtk.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int *nchecks, *nfails;
#define CHECK(c) check((c), #c, __LINE__)
static void check(int ok, const char *what, int line) {
    (*nchecks)++;
    if (!ok) {
        (*nfails)++;
        fprintf(stderr, "FAIL test_rtk.c line %d: %s\n", line, what);
    }
}

static const SqgGeodetic BASE = {35.681236, 139.767125, 40.0};
#define T0 200000.0

static SqgGeodetic moved(SqgGeodetic from, double e, double n, double u) {
    SqgEcef o, p;
    SqgEnu d = {e, n, u};
    SqgGeodetic g;
    sqg_geodetic_to_ecef(from, &o);
    sqg_enu_to_ecef(o, from, d, &p);
    sqg_ecef_to_geodetic(p, &g);
    return g;
}

static void test_stream(const SqgEphemeris *eph, int n_eph) {
    SqgStream s;
    SqgEpoch ep;
    sqg_stream_init(&s, NULL);
    SqgGeodetic mean;
    SqgEnu sig;
    CHECK(sqg_stream_average(&s, &mean, &sig) == 0);

    /* an occupation: 60 epochs with 1 m code noise */
    for (int k = 0; k < 60; k++) {
        sqg_demo_epoch(eph, n_eph, BASE, T0 + k, 30.0 + k * 0.01, 10.0, 1.0, 100 + k, &ep);
        CHECK(sqg_stream_push(&s, &ep, eph, n_eph) == 0);
    }
    CHECK(s.n_epochs == 60 && s.n_fixed == 60 && s.have_fix);
    CHECK(sqg_stream_average(&s, &mean, &sig) == 60);
    SqgEnu off = sqg_stakeout_delta(BASE, mean);
    CHECK(hypot(off.e, off.n) < 0.6 && fabs(off.u) < 1.2);
    CHECK(sig.e > 0.1 && sig.e < 3 && sig.n > 0.1 && sig.n < 3 && sig.u > 0.2 && sig.u < 6);

    sqg_stream_reset_average(&s);
    CHECK(sqg_stream_average(&s, &mean, &sig) == 0);

    /* noiseless: the average is exact and the spread 0 */
    for (int k = 0; k < 5; k++) {
        sqg_demo_epoch(eph, n_eph, BASE, T0 + 100 + k, 30.0, 10.0, 0, 1, &ep);
        sqg_stream_push(&s, &ep, eph, n_eph);
    }
    CHECK(sqg_stream_average(&s, &mean, &sig) == 5);
    off = sqg_stakeout_delta(BASE, mean);
    CHECK(fabs(off.e) < 1e-3 && fabs(off.n) < 1e-3 && fabs(off.u) < 1e-3);
    CHECK(sig.e < 1e-3 && sig.n < 1e-3 && sig.u < 1e-3);

    /* moving 3 m/s at 60 degrees (east of north), one epoch a second */
    sqg_stream_init(&s, NULL);
    for (int k = 0; k < 4; k++) {
        double d = 3.0 * k;
        SqgGeodetic at = moved(BASE, d * sin(60 * 3.14159265358979323846 / 180), d * cos(60 * 3.14159265358979323846 / 180), 0);
        sqg_demo_epoch(eph, n_eph, at, T0 + 200 + k, 30.0, 10.0, 0, 1, &ep);
        sqg_stream_push(&s, &ep, eph, n_eph);
    }
    CHECK(s.have_velocity);
    CHECK(fabs(s.speed_mps - 3.0) < 0.01 && fabs(s.course_deg - 60.0) < 0.2);

    /* an epoch with too few satellites is no fix, and the count says so */
    SqgEpoch few = ep;
    few.n_obs = 3;
    CHECK(sqg_stream_push(&s, &few, eph, n_eph) == -1);
    CHECK(s.n_epochs == 5 && s.n_fixed == 4); /* the last fix stays */
}

static void test_guide(void) {
    char buf[160];
    SqgGeodetic target = moved(BASE, -0.45, 1.23, 0.1);
    SqgGuide g = sqg_stakeout_guide(BASE, target, 0.01);
    CHECK(!g.on_point && fabs(g.horiz_m - hypot(0.45, 1.23)) < 1e-6);
    CHECK(fabs(g.bearing_deg - (360 - atan2(0.45, 1.23) * 180 / 3.14159265358979323846)) < 1e-4);
    sqg_guide_text(&g, buf, sizeof buf);
    CHECK(strcmp(buf, "go 1.230 m N, 0.450 m W (1.310 m at 339.9 deg); fill 0.100 m") == 0);

    target = moved(BASE, 0.003, -0.002, -0.02);
    g = sqg_stakeout_guide(BASE, target, 0.01);
    CHECK(g.on_point);
    sqg_guide_text(&g, buf, sizeof buf);
    CHECK(strcmp(buf, "on point (0.004 m); cut 0.020 m") == 0);

    target = moved(BASE, 2.0, -5.0, 0.0);
    g = sqg_stakeout_guide(BASE, target, 0.01);
    sqg_guide_text(&g, buf, sizeof buf);
    CHECK(strcmp(buf, "go 5.000 m S, 2.000 m E (5.385 m at 158.2 deg); on grade") == 0);

    /* due north: no east leg, and 0.0 deg rather than 360.0 */
    target = moved(BASE, -1e-7, 1.109, 0.0);
    g = sqg_stakeout_guide(BASE, target, 0.01);
    sqg_guide_text(&g, buf, sizeof buf);
    CHECK(strcmp(buf, "go 1.109 m N (1.109 m at 0.0 deg); on grade") == 0);

    /* a short buffer is cut, not overrun */
    char tiny[12];
    memset(tiny, 'x', sizeof tiny);
    sqg_guide_text(&g, tiny, 8);
    CHECK(strlen(tiny) == 7 && tiny[8] == 'x');
}

static void test_dgnss(const SqgEphemeris *eph, int n_eph) {
    SqgRtkOptions opt;
    sqg_rtk_options_default(&opt);
    SqgEcef base_pos, truth, got;
    sqg_geodetic_to_ecef(BASE, &base_pos);
    SqgGeodetic rover = moved(BASE, 830.0, -1210.0, 12.0);
    sqg_geodetic_to_ecef(rover, &truth);
    SqgEpoch b, r;
    int n_dd = 0;

    /* noiseless: exact, whatever the two receiver clocks */
    sqg_demo_epoch(eph, n_eph, BASE, T0, 1234.0, 5.0, 0, 1, &b);
    sqg_demo_epoch(eph, n_eph, rover, T0, -87654.0, 5.0, 0, 2, &r);
    CHECK(sqg_dgnss(&b, &r, eph, n_eph, base_pos, &opt, &got, &n_dd) == 0);
    CHECK(n_dd >= 4);
    CHECK(sqg_baseline_length(got, truth) < 1e-3);

    /* 0.3 m code noise: well under a metre */
    double worst = 0;
    for (int k = 0; k < 10; k++) {
        sqg_demo_epoch(eph, n_eph, BASE, T0 + 60 * k, 100.0, 5.0, 0.3, 10 + k, &b);
        sqg_demo_epoch(eph, n_eph, rover, T0 + 60 * k, -50.0, 5.0, 0.3, 50 + k, &r);
        if (sqg_dgnss(&b, &r, eph, n_eph, base_pos, NULL, &got, &n_dd) != 0) worst = 1e9;
        else worst = fmax(worst, sqg_baseline_length(got, truth));
    }
    CHECK(worst < 2.5); /* 3-D, from about 0.4 m of double-difference noise per satellite pair */

    /* fewer than 4 double differences */
    r.n_obs = 3;
    CHECK(sqg_dgnss(&b, &r, eph, n_eph, base_pos, &opt, &got, &n_dd) == -1);
}

#define NE 20
static SqgEpoch base_eps[NE], rover_eps[NE];

static int make_session(const SqgEphemeris *eph, int n_eph, SqgGeodetic rover, double noise, double gap) {
    for (int k = 0; k < NE; k++) {
        sqg_demo_epoch(eph, n_eph, BASE, T0 + gap * k, 500.0 + k, 5.0, noise, 300 + k, &base_eps[k]);
        sqg_demo_epoch(eph, n_eph, rover, T0 + gap * k, -2000.0 - 3 * k, 5.0, noise, 700 + k, &rover_eps[k]);
    }
    return NE;
}

static void test_rtk(const SqgEphemeris *eph, int n_eph) {
    SqgRtkOptions opt;
    sqg_rtk_options_default(&opt);
    SqgEcef base_pos, truth;
    sqg_geodetic_to_ecef(BASE, &base_pos);
    SqgGeodetic rover = moved(BASE, 123.456, -78.9, 2.5);
    sqg_geodetic_to_ecef(rover, &truth);
    SqgRtk out;

    /* 0.3 m code, 3 mm phase, 20 epochs 15 s apart: fixed to millimetres */
    int n = make_session(eph, n_eph, rover, 0.3, 15.0);
    CHECK(sqg_rtk_static(base_eps, rover_eps, n, eph, n_eph, base_pos, &opt, &out) == 0);
    CHECK(out.n_epochs == NE && out.n_dd >= 4);
    CHECK(out.fixed);
    CHECK(out.ratio >= opt.ratio_min);
    int right = 1;
    for (int j = 0; j < out.n_dd; j++) {
        long want = (sqg_demo_ambiguity(truth, out.prn[j]) - sqg_demo_ambiguity(truth, out.ref_prn)) -
                    (sqg_demo_ambiguity(base_pos, out.prn[j]) - sqg_demo_ambiguity(base_pos, out.ref_prn));
        if (out.amb[j] != want) right = 0;
    }
    CHECK(right);
    SqgGeodetic got;
    sqg_ecef_to_geodetic(out.rover, &got);
    SqgEnu err = sqg_stakeout_delta(rover, got);
    CHECK(hypot(err.e, err.n) < 0.01 && fabs(err.u) < 0.02);
    CHECK(sqg_baseline_length(out.float_rover, truth) < 0.5);
    CHECK(out.rms_phase_m < 0.01);
    /* the ambiguities really differ from satellite to satellite */
    int differ = 0;
    for (int j = 1; j < out.n_dd; j++) differ |= out.amb[j] != out.amb[0];
    CHECK(differ);

    /* noiseless: exact */
    n = make_session(eph, n_eph, rover, 0, 15.0);
    CHECK(sqg_rtk_static(base_eps, rover_eps, n, eph, n_eph, base_pos, NULL, &out) == 0);
    CHECK(out.fixed && sqg_baseline_length(out.rover, truth) < 1e-4);

    /* an impossible ratio leaves the float solution */
    opt.ratio_min = 1e12;
    n = make_session(eph, n_eph, rover, 0.3, 15.0);
    CHECK(sqg_rtk_static(base_eps, rover_eps, n, eph, n_eph, base_pos, &opt, &out) == 0);
    CHECK(!out.fixed);
    CHECK(out.rover.x == out.float_rover.x && out.rover.y == out.float_rover.y && out.rover.z == out.float_rover.z);
    sqg_rtk_options_default(&opt);

    /* no carrier: not enough double differences */
    for (int k = 0; k < NE; k++)
        for (int i = 0; i < rover_eps[k].n_obs; i++) rover_eps[k].obs[i].has_carrier = 0;
    CHECK(sqg_rtk_static(base_eps, rover_eps, n, eph, n_eph, base_pos, &opt, &out) == -1);
    CHECK(sqg_rtk_static(base_eps, rover_eps, 0, eph, n_eph, base_pos, &opt, &out) == -1);
}

void test_rtk_all(int *checks, int *failures) {
    nchecks = checks;
    nfails = failures;
    SqgEphemeris eph[SQG_DEMO_SATS];
    int n_eph;
    sqg_demo_ephemerides(eph, &n_eph, T0);
    test_stream(eph, n_eph);
    test_guide();
    test_dgnss(eph, n_eph);
    test_rtk(eph, n_eph);
}
