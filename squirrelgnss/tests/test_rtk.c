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

/* ---- integer least squares against a brute-force search ---- */

static unsigned rs = 12345;
static double urand(void) {
    rs = rs * 1103515245u + 12345u;
    return ((rs >> 8) & 0xffffff) / 16777216.0;
}

/* (x - a)' Q^-1 (x - a), with Q^-1 given */
static double qform(const double *qi, const double *a, const long *x, int n) {
    double s = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) s += (x[i] - a[i]) * qi[i * n + j] * (x[j] - a[j]);
    return s;
}

/* the inverse of an n x n matrix by Gauss-Jordan, for the test only */
static int invert(const double *m, double *inv, int n) {
    double a[8][16];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < 2 * n; j++) a[i][j] = j < n ? m[i * n + j] : (j - n == i);
    for (int c = 0; c < n; c++) {
        int p = c;
        for (int r = c + 1; r < n; r++)
            if (fabs(a[r][c]) > fabs(a[p][c])) p = r;
        if (fabs(a[p][c]) < 1e-14) return -1;
        for (int k = 0; k < 2 * n; k++) {
            double t = a[c][k];
            a[c][k] = a[p][k];
            a[p][k] = t;
        }
        double d = a[c][c];
        for (int k = 0; k < 2 * n; k++) a[c][k] /= d;
        for (int r = 0; r < n; r++)
            if (r != c) {
                double f = a[r][c];
                for (int k = 0; k < 2 * n; k++) a[r][k] -= f * a[c][k];
            }
    }
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) inv[i * n + j] = a[i][n + j];
    return 0;
}

static void test_ils(void) {
    /* strongly correlated covariances (a cycle or two along one direction, a
       few hundredths across), where rounding each ambiguity goes wrong */
    int agree = 0, rounding_wrong = 0, trials = 0;
    for (int t = 0; t < 40; t++) {
        int n = 2 + t % 4;
        double m[25], q[25], qi[25], a[5];
        for (int i = 0; i < n * n; i++) m[i] = urand() * 2 - 1;
        /* q = m m' / 10 + a long thin direction */
        double dir[5];
        for (int i = 0; i < n; i++) dir[i] = urand() * 2 - 1;
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                double s = 0;
                for (int k = 0; k < n; k++) s += m[i * n + k] * m[j * n + k];
                q[i * n + j] = s * 0.01 + dir[i] * dir[j];
            }
        for (int i = 0; i < n; i++) a[i] = (urand() - 0.5) * 40;
        if (invert(q, qi, n)) continue;
        trials++;
        long best[5], round_x[5];
        double ratio, dist[2];
        CHECK(sqg_ils(a, q, n, best, &ratio, dist) == 0);
        /* brute force over a box wide enough for the long direction */
        long lo[5], x[5], bx[5] = {0};
        double b1 = INFINITY, b2 = INFINITY;
        int span = 10;
        for (int i = 0; i < n; i++) lo[i] = lround(a[i]) - span, x[i] = lo[i];
        for (;;) {
            double v = qform(qi, a, x, n);
            if (v < b1) {
                b2 = b1;
                b1 = v;
                memcpy(bx, x, sizeof x);
            } else if (v < b2)
                b2 = v;
            int i = 0;
            while (i < n && ++x[i] > lo[i] + 2 * span) x[i] = lo[i], i++;
            if (i == n) break;
        }
        int same = 1;
        for (int i = 0; i < n; i++) same &= best[i] == bx[i];
        agree += same && fabs(dist[0] - b1) < 1e-6 * (1 + b1) && fabs(dist[1] - b2) < 1e-6 * (1 + b2);
        for (int i = 0; i < n; i++) round_x[i] = lround(a[i]);
        rounding_wrong += qform(qi, a, round_x, n) > b1 + 1e-9;
        CHECK(fabs(ratio - b2 / fmax(b1, 1e-12)) < 1e-6 * ratio);
    }
    CHECK(trials >= 35 && agree == trials);
    CHECK(rounding_wrong >= trials / 3); /* the cases rounding alone gets wrong */
    /* one ambiguity; and a covariance that is not positive definite */
    long b1v[1];
    double r1, q1[1] = {0.01}, a1[1] = {3.1};
    CHECK(sqg_ils(a1, q1, 1, b1v, &r1, NULL) == 0 && b1v[0] == 3 && fabs(r1 - 0.81 / 0.01) < 1e-9);
    double qbad[4] = {1, 2, 2, 1}, a2[2] = {0.2, 0.3};
    long b2v[2];
    CHECK(sqg_ils(a2, qbad, 2, b2v, &r1, NULL) == -1);
    double nanv[2] = {NAN, 0}, qok[4] = {1, 0, 0, 1};
    CHECK(sqg_ils(nanv, qok, 2, b2v, &r1, NULL) == -1);
    CHECK(sqg_ils(a2, qok, 0, b2v, &r1, NULL) == -1);
}

/* ---- dual frequency ---- */

static SqgEpoch dual_b[200], dual_r[200];

/* the double-difference ambiguity of receivers 22 (rover) and 11 (base) */
static long dd_amb(int prn, int ref, int band) {
    return (sqg_demo_receiver_ambiguity(22, prn, band) - sqg_demo_receiver_ambiguity(22, ref, band)) -
           (sqg_demo_receiver_ambiguity(11, prn, band) - sqg_demo_receiver_ambiguity(11, ref, band));
}

static void dual_session(const SqgEphemeris *eph, int n_eph, int n, double gap, double noise, double iono, int dual,
                         SqgGeodetic (*path)(int k)) {
    for (int k = 0; k < n; k++) {
        SqgDemoOptions ob = {300.0 + k, 5.0, noise, 41u + (unsigned)k, 11, dual, iono};
        SqgDemoOptions orv = {-700.0 - k, 5.0, noise, 9001u + (unsigned)k, 22, dual, iono};
        sqg_demo_epoch_opt(eph, n_eph, BASE, T0 + gap * k, &ob, &dual_b[k]);
        sqg_demo_epoch_opt(eph, n_eph, path(k), T0 + gap * k, &orv, &dual_r[k]);
    }
}

static SqgGeodetic still(int k) {
    (void)k;
    return moved(BASE, 350.0, 420.0, -3.0);
}

/* a 100 m circle at 5 m/s, a second apart */
static SqgGeodetic circle(int k) {
    double a = 5.0 * k / 100.0;
    return moved(BASE, 600 + 100 * cos(a), -200 + 100 * sin(a), 1.5);
}

static void test_dual_static(const SqgEphemeris *eph, int n_eph) {
    SqgEcef base_pos, truth;
    sqg_geodetic_to_ecef(BASE, &base_pos);
    sqg_geodetic_to_ecef(still(0), &truth);
    SqgRtkOptions opt;
    sqg_rtk_options_default(&opt);
    SqgRtk l1, both;
    /* 3 m of ionosphere at the zenith, 0.3 m code: 12 epochs 15 s apart */
    dual_session(eph, n_eph, 12, 15.0, 0.3, 3.0, 1, still);
    CHECK(sqg_rtk_static(dual_b, dual_r, 12, eph, n_eph, base_pos, &opt, &both) == 0);
    CHECK(both.wl_fixed && both.dual && both.fixed);
    int wl_right = 1, n1_right = 1;
    for (int j = 0; j < both.n_dd; j++) {
        long n1 = dd_amb(both.prn[j], both.ref_prn, 1), n2 = dd_amb(both.prn[j], both.ref_prn, 2);
        wl_right &= both.wl[j] == n1 - n2;
        n1_right &= both.amb[j] == n1;
    }
    CHECK(wl_right && n1_right);
    CHECK(sqg_baseline_length(both.rover, truth) < 0.02);
    /* the same data on L1 alone fits far less sharply */
    opt.dual = 0;
    CHECK(sqg_rtk_static(dual_b, dual_r, 12, eph, n_eph, base_pos, &opt, &l1) == 0);
    CHECK(!l1.dual && !l1.wl_fixed && l1.ratio < both.ratio / 5);
    /* noiseless, with the ionosphere: exact, because it cancels over a short
       baseline (told the code is good, three epochs are enough for the wide lanes) */
    opt.dual = 1;
    opt.sigma_code = 0.05;
    dual_session(eph, n_eph, 3, 15.0, 0, 3.0, 1, still);
    CHECK(sqg_rtk_static(dual_b, dual_r, 3, eph, n_eph, base_pos, &opt, &both) == 0);
    CHECK(both.fixed && both.dual && sqg_baseline_length(both.rover, truth) < 2e-3);
}

static void test_kinematic(const SqgEphemeris *eph, int n_eph) {
    SqgEcef base_pos;
    sqg_geodetic_to_ecef(BASE, &base_pos);
    static SqgRtkFilter f;
    SqgRtkFix fx;
    const int N = 160;
    /* dual frequency, 0.3 m code, 3 m of ionosphere, a rover on a circle */
    dual_session(eph, n_eph, N, 1.0, 0.3, 3.0, 1, circle);
    sqg_rtk_filter_init(&f, base_pos, NULL);
    int fixed = 0, bad = 0, first = -1, wl_all = 0, ok = 1;
    for (int k = 0; k < N; k++) {
        if (sqg_rtk_filter_push(&f, &dual_b[k], &dual_r[k], eph, n_eph, &fx)) {
            ok = 0;
            continue;
        }
        SqgEcef t;
        sqg_geodetic_to_ecef(circle(k), &t);
        if (fx.fixed) {
            fixed++;
            if (first < 0) first = k;
            if (sqg_baseline_length(fx.pos, t) > 0.06) bad++; /* 3-D, one epoch of 3 mm carrier */
        } else if (sqg_baseline_length(fx.pos, t) > 3.0)
            bad++;
        if (k == 0) CHECK(fx.reset);
        wl_all = fx.wide_lanes == fx.n_dd;
    }
    CHECK(ok && bad == 0 && first >= 0 && fixed >= N - 20 && wl_all);
    /* the fixed ambiguities are the true ones */
    int right = 1;
    for (int j = 0; j < f.n_sat - 1; j++) right &= f.fixed_amb[j] == dd_amb(f.prn[j + 1], f.prn[0], 1);
    CHECK(f.fixed && right);

    /* noiseless: every fixed epoch to the millimetre */
    dual_session(eph, n_eph, 20, 1.0, 0, 3.0, 1, circle);
    sqg_rtk_filter_init(&f, base_pos, NULL);
    double worst = 0;
    fixed = 0;
    for (int k = 0; k < 20; k++)
        if (sqg_rtk_filter_push(&f, &dual_b[k], &dual_r[k], eph, n_eph, &fx) == 0 && fx.fixed) {
            SqgEcef t;
            sqg_geodetic_to_ecef(circle(k), &t);
            worst = fmax(worst, sqg_baseline_length(fx.pos, t));
            fixed++;
        }
    CHECK(fixed >= 18 && worst < 1e-3);

    /* slips: one the rover flags; two it does not, which show as jumps in the
     * geometry-free carrier (3 cycles on L1; 1 on L1 and 2 on L2); then the
     * reference satellite goes */
    dual_session(eph, n_eph, N, 1.0, 0.3, 3.0, 1, circle);
    sqg_rtk_filter_init(&f, base_pos, NULL);
    for (int k = 0; k < 40; k++) sqg_rtk_filter_push(&f, &dual_b[k], &dual_r[k], eph, n_eph, &fx);
    CHECK(fx.fixed);
    int ref = f.prn[0], p1 = f.prn[1], p2 = f.prn[2], p3 = f.prn[3];
    for (int k = 40; k < N; k++)
        for (int i = 0; i < dual_r[k].n_obs; i++) {
            SqgObservation *o = &dual_r[k].obs[i];
            if (o->prn == p1) o->carrier_cyc += 7, o->carrier2_cyc += 5, o->slip = k == 40;
            if (o->prn == p2 && k >= 60) o->carrier_cyc += 3;
            if (o->prn == p3 && k >= 80) o->carrier_cyc += 1, o->carrier2_cyc += 2;
        }
    int slips40 = 0, slips60 = 0, slips80 = 0, wrong = 0, refixed = 0;
    for (int k = 40; k < 120; k++) {
        if (sqg_rtk_filter_push(&f, &dual_b[k], &dual_r[k], eph, n_eph, &fx)) continue;
        SqgEcef t;
        sqg_geodetic_to_ecef(circle(k), &t);
        if (k == 40) slips40 = fx.slips;
        if (k == 60) slips60 = fx.slips;
        if (k == 80) slips80 = fx.slips;
        if (fx.fixed && sqg_baseline_length(fx.pos, t) > 0.06) wrong++;
        if (k >= 110) refixed += fx.fixed;
    }
    CHECK(slips40 == 1 && slips60 == 1 && slips80 == 1);
    CHECK(wrong == 0 && refixed == 10);
    /* the reference goes: the filter starts over on what is left */
    for (int k = 120; k < N; k++)
        for (int i = 0; i < dual_r[k].n_obs; i++)
            if (dual_r[k].obs[i].prn == ref) dual_r[k].obs[i].has_carrier = 0;
    CHECK(sqg_rtk_filter_push(&f, &dual_b[120], &dual_r[120], eph, n_eph, &fx) == 0 && fx.reset && !fx.fixed);
    CHECK(f.prn[0] != ref);

    /* L1 only: an unflagged slip of one cycle is caught when the fix stops fitting */
    dual_session(eph, n_eph, N, 1.0, 0.3, 0, 0, circle);
    sqg_rtk_filter_init(&f, base_pos, NULL);
    for (int k = 0; k < 60; k++) sqg_rtk_filter_push(&f, &dual_b[k], &dual_r[k], eph, n_eph, &fx);
    CHECK(fx.fixed && fx.wide_lanes == 0);
    p1 = f.prn[2];
    for (int k = 60; k < N; k++)
        for (int i = 0; i < dual_r[k].n_obs; i++)
            if (dual_r[k].obs[i].prn == p1) dual_r[k].obs[i].carrier_cyc += 1;
    int dropped = 0;
    wrong = 0;
    for (int k = 60; k < N; k++) {
        if (sqg_rtk_filter_push(&f, &dual_b[k], &dual_r[k], eph, n_eph, &fx)) continue;
        SqgEcef t;
        sqg_geodetic_to_ecef(circle(k), &t);
        dropped |= fx.dropped;
        if (fx.fixed && sqg_baseline_length(fx.pos, t) > 0.06) wrong++;
    }
    CHECK(dropped && wrong == 0);

    /* too few satellites: no solution, and nothing kept */
    SqgEpoch few = dual_r[0];
    few.n_obs = 3;
    sqg_rtk_filter_init(&f, base_pos, NULL);
    CHECK(sqg_rtk_filter_push(&f, &dual_b[0], &few, eph, n_eph, &fx) == -1);
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
    test_ils();
    test_dual_static(eph, n_eph);
    test_kinematic(eph, n_eph);
}
