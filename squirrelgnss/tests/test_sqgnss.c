/* sqgnss unit tests: geodesy, orbits, clocks, positioning, fault exclusion. */
#include "sqgnss.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) check((c), #c, __LINE__)
static void check(int ok, const char *what, int line) {
    checks++;
    if (!ok) {
        failures++;
        fprintf(stderr, "FAIL line %d: %s\n", line, what);
    }
}

static void test_geodesy(void) {
    SqgEcef e;
    SqgGeodetic g;
    /* fixed points of the ellipsoid */
    sqg_geodetic_to_ecef((SqgGeodetic){0, 0, 0}, &e);
    CHECK(fabs(e.x - SQG_WGS84_A) < 1e-6 && fabs(e.y) < 1e-6 && fabs(e.z) < 1e-6);
    sqg_geodetic_to_ecef((SqgGeodetic){90, 0, 0}, &e);
    CHECK(fabs(e.z - 6356752.314245) < 1e-5 && fabs(e.x) < 1e-6);
    sqg_geodetic_to_ecef((SqgGeodetic){0, 90, 100}, &e);
    CHECK(fabs(e.y - SQG_WGS84_A - 100) < 1e-6);
    /* round trips from below the ground to past the satellites, poles too */
    static const double LAT[] = {-90, -89.9999, -60, -35.5, 0, 0.001, 12.3456789, 45, 80, 89.99999, 90};
    static const double LON[] = {-180, -120.5, -0.0001, 0, 33.3, 139.767125, 179.9999};
    static const double H[] = {-5000, -30, 0, 40, 8848, 400000, 20200000};
    double worst_deg = 0, worst_h = 0;
    for (size_t a = 0; a < sizeof LAT / sizeof *LAT; a++)
        for (size_t b = 0; b < sizeof LON / sizeof *LON; b++)
            for (size_t c = 0; c < sizeof H / sizeof *H; c++) {
                SqgGeodetic in = {LAT[a], LON[b], H[c]};
                sqg_geodetic_to_ecef(in, &e);
                sqg_ecef_to_geodetic(e, &g);
                double dlat = fabs(g.lat_deg - in.lat_deg), dh = fabs(g.height_m - in.height_m);
                double dlon = fabs(LAT[a]) > 89.9999 ? 0 : fabs(remainder(g.lon_deg - in.lon_deg, 360.0));
                if (dlat > worst_deg) worst_deg = dlat;
                if (dlon > worst_deg) worst_deg = dlon;
                if (dh > worst_h) worst_h = dh;
            }
    CHECK(worst_deg < 1e-10); /* about 0.01 mm */
    CHECK(worst_h < 1e-4);
    /* exactly on the axis */
    sqg_ecef_to_geodetic((SqgEcef){0, 0, -6356752.314245 - 12}, &g);
    CHECK(g.lat_deg == -90 && fabs(g.height_m - 12) < 1e-6);

    /* ENU both ways, and azimuth/elevation */
    SqgGeodetic o = {35.0, 139.0, 50};
    SqgEcef oe, p;
    sqg_geodetic_to_ecef(o, &oe);
    SqgEnu d = {120.5, -33.25, 7.75}, back;
    sqg_enu_to_ecef(oe, o, d, &p);
    sqg_ecef_to_enu(oe, p, o, &back);
    CHECK(fabs(back.e - d.e) < 1e-6 && fabs(back.n - d.n) < 1e-6 && fabs(back.u - d.u) < 1e-6);
    CHECK(fabs(sqg_baseline_length(oe, p) - sqrt(d.e * d.e + d.n * d.n + d.u * d.u)) < 1e-6);
    SqgAzEl ae = sqg_az_el(oe, o, (SqgEcef){0, 0, 0});
    sqg_enu_to_ecef(oe, o, (SqgEnu){0, 0, 1000}, &p);
    ae = sqg_az_el(oe, o, p);
    CHECK(fabs(ae.el_deg - 90) < 1e-9);
    sqg_enu_to_ecef(oe, o, (SqgEnu){1000, 0, 0}, &p);
    ae = sqg_az_el(oe, o, p);
    CHECK(fabs(ae.az_deg - 90) < 1e-9 && fabs(ae.el_deg) < 1e-9);
    sqg_enu_to_ecef(oe, o, (SqgEnu){-1000, -1000, 1000 * sqrt(2.0)}, &p);
    ae = sqg_az_el(oe, o, p);
    CHECK(fabs(ae.az_deg - 225) < 1e-9 && fabs(ae.el_deg - 45) < 1e-9);
    /* stakeout: 0.001 degree north is about 111 m */
    d = sqg_stakeout_delta((SqgGeodetic){35, 139, 50}, (SqgGeodetic){35.001, 139, 50});
    CHECK(fabs(d.n - 110.95) < 0.1 && fabs(d.e) < 1e-6 && fabs(d.u) < 0.01);
}

static void test_orbits(void) {
    SqgEphemeris eph[SQG_DEMO_SATS];
    int n;
    sqg_demo_ephemerides(eph, &n, 100000.0);
    CHECK(n == SQG_DEMO_SATS);
    /* an orbit without corrections: radius a(1 - e cos E), speed from vis-viva */
    SqgEphemeris k = eph[3];
    k.cuc = k.cus = k.crc = k.crs = k.cic = k.cis = 0;
    k.delta_n = k.omega_dot = k.idot = 0;
    double a = k.sqrt_a * k.sqrt_a;
    for (double t = 100000; t < 100000 + 43082; t += 3600) {
        SqgEcef s = sqg_sat_position(&k, t);
        double r = sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
        CHECK(r > a * (1 - k.e) - 1e-3 && r < a * (1 + k.e) + 1e-3);
    }
    /* about one revolution per half sidereal day: back near the start in inertial space */
    double period = 2 * 3.14159265358979323846 * sqrt(a * a * a / SQG_MU);
    SqgEcef s0 = sqg_sat_position(&k, 100000), s1 = sqg_sat_position(&k, 100000 + period);
    double turn = SQG_OMEGA_E * period; /* undo the Earth's turn */
    SqgEcef s1i = {cos(turn) * s1.x - sin(turn) * s1.y, sin(turn) * s1.x + cos(turn) * s1.y, s1.z};
    CHECK(sqg_baseline_length(s0, s1i) < 1e-3);
    /* the week wraps: toe late in the week, t early in the next */
    k.toe = SQG_WEEK - 100;
    SqgEcef w0 = sqg_sat_position(&k, SQG_WEEK - 100 + 200), w1 = sqg_sat_position(&k, 100);
    CHECK(sqg_baseline_length(w0, w1) < 1e-6);
    /* the clock: polynomial, relativistic term, group delay */
    SqgEphemeris c = eph[0];
    c.af0 = 1e-4;
    c.af1 = 1e-11;
    c.af2 = 0;
    c.tgd = 0;
    c.e = 0;
    CHECK(fabs(sqg_sat_clock(&c, c.toc + 1000) - (1e-4 + 1e-8)) < 1e-15);
    c.tgd = 5e-9;
    CHECK(fabs(sqg_sat_clock(&c, c.toc) - (1e-4 - 5e-9)) < 1e-15);
    c.tgd = 0;
    c.e = 0.01;
    double rel_max = 0;
    for (double t = c.toc; t < c.toc + 43200; t += 60) {
        double r = fabs(sqg_sat_clock(&c, t) - 1e-4 - 1e-11 * (t - c.toc));
        if (r > rel_max) rel_max = r;
    }
    CHECK(fabs(rel_max - 4.442807633e-10 * 0.01 * c.sqrt_a) < 1e-12); /* about 23 ns */
}

static void test_pvt(void) {
    SqgEphemeris eph[SQG_DEMO_SATS];
    int n_eph;
    sqg_demo_ephemerides(eph, &n_eph, 100000.0);
    static const SqgGeodetic SITES[] = {
        {35.681236, 139.767125, 40}, {-33.86, 151.21, 20}, {64.15, -21.94, 30}, {0.0, -78.5, 2800}, {89.5, 10, 5}};
    for (size_t s = 0; s < sizeof SITES / sizeof *SITES; s++) {
        SqgEcef truth;
        sqg_geodetic_to_ecef(SITES[s], &truth);
        for (double t = 100000; t < 100000 + 86400; t += 10800) {
            SqgEpoch ep;
            sqg_demo_epoch(eph, n_eph, SITES[s], t, 1234.5, 5.0, 0, 7, &ep);
            SqgPvt p;
            int rc = sqg_solve_pvt(&ep, eph, n_eph, (SqgEcef){0, 0, 0}, &p);
            if (rc != 0 || sqg_baseline_length(p.pos, truth) > 1e-3) {
                fprintf(stderr, "  site %zu t %.0f: rc %d used %d err %.6f m\n", s, t, rc, p.n_used,
                        sqg_baseline_length(p.pos, truth));
            }
            CHECK(rc == 0 && p.converged);
            CHECK(sqg_baseline_length(p.pos, truth) < 1e-3); /* noiseless: millimetres */
            CHECK(fabs(p.clock_bias_m - 1234.5) < 1e-3);
            CHECK(p.n_used >= 4 && p.rms_m < 1e-3 && p.excluded_prn == 0);
            CHECK(p.hdop > 0 && p.vdop > 0 && p.pdop * p.pdop <= p.hdop * p.hdop + p.vdop * p.vdop + 1e-9);
            CHECK(fabs(p.pdop * p.pdop - p.hdop * p.hdop - p.vdop * p.vdop) < 1e-6);
            CHECK(p.gdop >= p.pdop && p.tdop > 0);
            /* every used satellite is above the 10 degree mask */
            for (int i = 0; i < p.n_used; i++) {
                const SqgEphemeris *e = NULL;
                for (int k = 0; k < n_eph; k++)
                    if (eph[k].prn == p.used_prn[i]) e = &eph[k];
                CHECK(e && sqg_az_el(truth, SITES[s], sqg_sat_position(e, t)).el_deg > 9.5);
            }
        }
    }
    /* noise: metres in, metres out */
    SqgGeodetic home = SITES[0];
    SqgEcef truth;
    sqg_geodetic_to_ecef(home, &truth);
    double worst = 0;
    for (unsigned seed = 1; seed <= 20; seed++) {
        SqgEpoch ep;
        sqg_demo_epoch(eph, n_eph, home, 100000 + seed * 60, 15, 5.0, 1.0, seed, &ep);
        SqgPvt p;
        CHECK(sqg_solve_pvt(&ep, eph, n_eph, truth, &p) == 0);
        double err = sqg_baseline_length(p.pos, truth);
        if (err > worst) worst = err;
        CHECK(p.rms_m > 0.2 && p.rms_m < 3 && p.excluded_prn == 0);
    }
    CHECK(worst < 10 && worst > 0.1);

    /* a 100 m fault on one satellite is found and left out */
    SqgEpoch ep;
    sqg_demo_epoch(eph, n_eph, home, 100000, 15, 5.0, 0.5, 3, &ep);
    SqgPvt good;
    CHECK(sqg_solve_pvt(&ep, eph, n_eph, truth, &good) == 0 && good.n_used >= 7);
    int bad = good.used_prn[2];
    for (int i = 0; i < ep.n_obs; i++)
        if (ep.obs[i].prn == bad) ep.obs[i].pseudorange_m += 100;
    SqgPvt p;
    CHECK(sqg_solve_pvt(&ep, eph, n_eph, truth, &p) == 0);
    CHECK(p.excluded_prn == bad);
    CHECK(sqg_baseline_length(p.pos, truth) < 10 && p.n_used == good.n_used - 1);
    /* with fault exclusion off, the fault pulls the solution */
    SqgPvtOptions opt;
    sqg_pvt_options_default(&opt);
    opt.raim = 0;
    CHECK(sqg_solve_pvt_opt(&ep, eph, n_eph, truth, &opt, &p) == 0 && p.excluded_prn == 0 && p.rms_m > 15);
    /* a mask of 0 uses more satellites than 10; 85 leaves too few */
    sqg_demo_epoch(eph, n_eph, home, 100000, 15, 0.0, 0, 3, &ep);
    opt.raim = 1;
    opt.elev_mask_deg = 0;
    SqgPvt low, high;
    CHECK(sqg_solve_pvt_opt(&ep, eph, n_eph, truth, &opt, &low) == 0);
    opt.elev_mask_deg = 10;
    CHECK(sqg_solve_pvt_opt(&ep, eph, n_eph, truth, &opt, &high) == 0 && low.n_used > high.n_used);
    opt.elev_mask_deg = 85;
    CHECK(sqg_solve_pvt_opt(&ep, eph, n_eph, truth, &opt, &p) == -1 && !p.converged);
    /* fewer than four observations */
    ep.n_obs = 3;
    CHECK(sqg_solve_pvt(&ep, eph, n_eph, truth, &p) == -1 && p.n_used == 3);
    /* the track seeds each epoch from the last */
    static SqgTrack tr;
    sqg_track_init(&tr);
    for (int k = 0; k < 5; k++) {
        sqg_demo_epoch(eph, n_eph, home, 100000 + k, 15, 5.0, 0.3, 10u + (unsigned)k, &ep);
        CHECK(sqg_track_push(&tr, &ep, eph, n_eph, (SqgEcef){0, 0, 0}) == 0);
    }
    CHECK(tr.n_epochs == 5 && sqg_baseline_length(tr.solutions[4].pos, truth) < 5);
}

void test_rinex(int *checks, int *failures);

int main(void) {
    test_geodesy();
    test_orbits();
    test_pvt();
    test_rinex(&checks, &failures);
    printf("test_sqgnss: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
