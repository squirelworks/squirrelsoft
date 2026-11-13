/* RINEX tests: GPS time, RINEX 3 written and read back, RINEX 2.11 written
 * here by hand, mixed systems, selection, events and errors. All data is
 * synthetic (sqg_demo_*). */
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
        fprintf(stderr, "FAIL test_rinex.c line %d: %s\n", line, what);
    }
}

static char text[1 << 20];

/* everything written to a temporary file, as text */
static const char *slurp(FILE *f) {
    rewind(f);
    size_t n = fread(text, 1, sizeof text - 1, f);
    text[n] = 0;
    fclose(f);
    return text;
}

static void copy_text(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static size_t put(char *buf, size_t at, const char *fmt, double a, double b, double c, double d) {
    return at + (size_t)snprintf(buf + at, (size_t)(1 << 20) - at, fmt, a, b, c, d);
}

static const SqgGeodetic HOME = {35.681236, 139.767125, 40.0};
#define WEEK 2444
#define T0 432000.0 /* Friday 2026-11-13 00:00 */

static void test_time(void) {
    int w, y, mo, d, h, mi;
    double s;
    sqr_gps_time(1980, 1, 6, 0, 0, 0, &w, &s);
    CHECK(w == 0 && s == 0);
    sqr_gps_time(2024, 1, 1, 0, 0, 0, &w, &s);
    CHECK(w == 2295 && s == 86400);
    sqr_gps_time(2026, 11, 13, 12, 34, 56.5, &w, &s);
    CHECK(w == 2444 && s == 432000 + 45296.5);
    sqr_gps_time(1999, 8, 22, 0, 0, 0, &w, &s); /* the first week rollover */
    CHECK(w == 1024 && s == 0);
    for (int week = 0; week < 5000; week += 37)
        for (double sow = 0.25; sow < SQG_WEEK; sow += 86399.75) {
            sqr_calendar(week, sow, &y, &mo, &d, &h, &mi, &s);
            double back;
            sqr_gps_time(y, mo, d, h, mi, s, &w, &back);
            CHECK(w == week && fabs(back - sow) < 1e-6);
        }
    sqr_calendar(2295, 86400 * 3 + 3600, &y, &mo, &d, &h, &mi, &s);
    CHECK(y == 2024 && mo == 1 && d == 3 && h == 1 && mi == 0 && s == 0);
    sqr_gps_time(2000, 2, 29, 0, 0, 0, &w, &s);
    sqr_calendar(w, s, &y, &mo, &d, &h, &mi, &s);
    CHECK(y == 2000 && mo == 2 && d == 29);
}

static int same_eph(const SqgEphemeris *a, const SqgEphemeris *b) {
    const double *x = &a->sqrt_a, *y = &b->sqrt_a;
    for (int k = 0; k < 15; k++) /* sqrt_a .. idot */
        if (fabs(x[k] - y[k]) > 1e-11 * (fabs(x[k]) + 1e-12)) return 0;
    return a->prn == b->prn && fabs(a->af0 - b->af0) < 1e-20 + 1e-11 * fabs(a->af0) &&
           fabs(a->af1 - b->af1) <= 1e-11 * fabs(a->af1) && a->toc == b->toc && a->week == b->week &&
           a->health == b->health && fabs(a->tgd - b->tgd) <= 1e-11 * fabs(a->tgd);
}

/* solve every epoch of an observation text against a navigation set */
static double solve_all(const char *obs, const SqrNav *nav, int *epochs) {
    SqrObs r;
    SqrError err;
    SqgEcef truth;
    sqg_geodetic_to_ecef(HOME, &truth);
    *epochs = 0;
    if (sqr_obs_open(&r, obs, &err)) {
        fprintf(stderr, "  obs_open: %s at %d\n", err.msg, err.line);
        return 1e9;
    }
    double worst = 0;
    SqgEpoch ep;
    int week, rc;
    while ((rc = sqr_obs_next(&r, &ep, &week, &err)) == 1) {
        SqgEphemeris sel[SQG_MAX_SATS];
        int n = sqr_nav_select(nav, week, ep.t_receiver, sel, SQG_MAX_SATS);
        SqgPvt p;
        SqgEcef start = {r.approx[0], r.approx[1], r.approx[2]};
        if (sqg_solve_pvt(&ep, sel, n, start, &p)) return 1e9;
        double e = sqg_baseline_length(p.pos, truth);
        if (e > worst) worst = e;
        (*epochs)++;
    }
    if (rc < 0) fprintf(stderr, "  obs_next: %s at %d\n", err.msg, err.line);
    return rc < 0 ? 1e9 : worst;
}

static SqgEphemeris eph[SQG_DEMO_SATS];
static int n_eph;
static SqrNav nav;

static void test_rinex3(void) {
    sqg_demo_ephemerides(eph, &n_eph, T0);
    for (int i = 0; i < n_eph; i++) eph[i].week = WEEK;
    eph[5].health = 1;
    FILE *f = tmpfile();
    sqr_write_nav(f, eph, n_eph);
    SqrError err;
    CHECK(sqr_read_nav(slurp(f), &nav, &err) == n_eph && nav.skipped == 0);
    for (int i = 0; i < n_eph && i < nav.n; i++) CHECK(same_eph(&eph[i], &nav.eph[i]));
    eph[5].health = 0;

    /* observations: written, read back, solved to a centimetre (the file keeps millimetres) */
    f = tmpfile();
    SqgEcef truth;
    sqg_geodetic_to_ecef(HOME, &truth);
    SqgEcef rough = {truth.x + 3000, truth.y - 2000, truth.z + 1000};
    sqr_write_obs_header(f, &rough);
    int total = 0;
    SqgEpoch first;
    for (int k = 0; k < 10; k++) {
        SqgEpoch ep;
        sqg_demo_epoch(eph, n_eph, HOME, T0 + 30 * k, 25.0, 5.0, 0, 1, &ep);
        if (k == 0) {
            ep.obs[1].has_carrier = 0; /* one satellite without a carrier: the field stays blank */
            first = ep;
        }
        sqr_write_obs_epoch(f, WEEK, &ep);
        total += ep.n_obs;
    }
    const char *obs = slurp(f);
    CHECK(strstr(obs, "G    3 C1C L1C S1C") != NULL);
    int epochs;
    double worst = solve_all(obs, &nav, &epochs);
    CHECK(epochs == 10);
    CHECK(worst < 0.01);
    SqrObs r;
    CHECK(sqr_obs_open(&r, obs, &err) == 0 && r.version > 3.03 && !strcmp(r.code_name, "C1C") && r.snr_index == 2 &&
          r.carrier_index == 1);
    CHECK(fabs(r.approx[0] - rough.x) < 1e-3);
    SqgEpoch ep;
    int week, seen = 0;
    while (sqr_obs_next(&r, &ep, &week, &err) == 1) {
        if (seen == 0) { /* carriers to the thousandth of a cycle, and the blank one */
            CHECK(ep.obs[0].has_carrier && fabs(ep.obs[0].carrier_cyc - first.obs[0].carrier_cyc) < 6e-4);
            CHECK(!ep.obs[1].has_carrier && ep.obs[2].has_carrier && ep.obs[1].snr_dbhz > 30);
        }
        seen += ep.n_obs;
        CHECK(week == WEEK && ep.obs[0].snr_dbhz > 30);
    }
    CHECK(seen == total);
}

/* RINEX 2.11, as a receiver converter would write it, with D exponents */
static void d_exponents(char *s) {
    for (; *s; s++)
        if (*s == 'E' && (s[1] == '+' || s[1] == '-')) *s = 'D';
}

static void test_rinex2(void) {
    static char nav2[1 << 20], obs2[1 << 20];
    size_t at = 0;
    at += (size_t)snprintf(nav2 + at, sizeof nav2 - at, "%-60s%-20s\n%-60s%-20s\n", "     2.11           N: GPS NAV DATA",
                           "RINEX VERSION / TYPE", "", "END OF HEADER");
    for (int i = 0; i < n_eph; i++) {
        const SqgEphemeris *e = &eph[i];
        at += (size_t)snprintf(nav2 + at, sizeof nav2 - at, "%2d %02d %2d %2d %2d %2d%5.1f", e->prn, 26, 11, 13, 0, 0, 0.0);
        at = put(nav2, at, "%19.12E%19.12E%19.12E\n", e->af0, e->af1, e->af2, 0);
        double v[28] = {7, e->crs, e->delta_n, e->m0, e->cuc, e->e, e->cus, e->sqrt_a, e->toe, e->cic, e->omega0,
                        e->cis, e->i0, e->crc, e->w, e->omega_dot, e->idot, 1, WEEK - 2048, 0, 2.0, 0, e->tgd, 7,
                        e->toc, 4};
        for (int k = 0; k < 7; k++) {
            at += (size_t)snprintf(nav2 + at, sizeof nav2 - at, "   ");
            at = put(nav2, at, k == 6 ? "%19.12E%19.12E\n" : "%19.12E%19.12E%19.12E%19.12E\n", v[4 * k], v[4 * k + 1],
                     v[4 * k + 2], v[4 * k + 3]);
        }
    }
    d_exponents(nav2);
    static SqrNav nav_b;
    SqrError err;
    CHECK(sqr_read_nav(nav2, &nav_b, &err) == n_eph);
    for (int i = 0; i < n_eph && i < nav_b.n; i++) CHECK(same_eph(&eph[i], &nav_b.eph[i])); /* 10-bit week mended */

    /* 7 types: two lines per satellite, and more than 12 satellites on the epoch line */
    for (int pass = 0; pass < 2; pass++) {
        at = 0;
        at += (size_t)snprintf(obs2 + at, sizeof obs2 - at, "%-60s%-20s\n", "     2.11           OBSERVATION DATA    G (GPS)",
                               "RINEX VERSION / TYPE");
        if (pass == 0)
            at += (size_t)snprintf(obs2 + at, sizeof obs2 - at, "%-60s%-20s\n", "     3    L1    C1    S1",
                                   "# / TYPES OF OBSERV");
        else
            at += (size_t)snprintf(obs2 + at, sizeof obs2 - at, "%-60s%-20s\n", "     7    L1    L2    P2    D1    S1    C1",
                                   "# / TYPES OF OBSERV");
        if (pass == 1)
            at += (size_t)snprintf(obs2 + at, sizeof obs2 - at, "%-60s%-20s\n", "          S2", "# / TYPES OF OBSERV");
        at += (size_t)snprintf(obs2 + at, sizeof obs2 - at, "%-60s%-20s\n", "", "END OF HEADER");
        for (int k = 0; k < 3; k++) {
            SqgEpoch ep;
            sqg_demo_epoch(eph, n_eph, HOME, T0 + 30 * k, -40.0, -90.0, 0, 1, &ep); /* every satellite, even below */
            /* only those above 5 degrees are usable; keep the others too, to make the list long */
            at += (size_t)snprintf(obs2 + at, sizeof obs2 - at, " 26 11 13  0 %2d%11.7f  0%3d", (int)(30 * k / 60),
                                   fmod(30.0 * k, 60.0) + ep.t_receiver - (T0 + 30 * k), ep.n_obs);
            for (int i = 0; i < ep.n_obs; i++) {
                if (i && i % 12 == 0) at += (size_t)snprintf(obs2 + at, sizeof obs2 - at, "\n%32s", "");
                at += (size_t)snprintf(obs2 + at, sizeof obs2 - at, i % 2 ? "G%02d" : " %02d", ep.obs[i].prn);
            }
            at += (size_t)snprintf(obs2 + at, sizeof obs2 - at, "\n");
            for (int i = 0; i < ep.n_obs; i++) {
                double c1 = ep.obs[i].pseudorange_m, s1 = ep.obs[i].snr_dbhz;
                if (pass == 0) {
                    at = put(obs2, at, "%14.3f  %14.3f  %14.3f  \n", c1 / 0.19, c1, s1, 0);
                } else {
                    at += (size_t)snprintf(obs2 + at, sizeof obs2 - at, "%14.3f  %14s  %14.3f  %14s  %14.3f  \n", c1 / 0.19,
                                           "", c1 + 3, "", s1);
                    at = put(obs2, at, "%14.3f  %14.3f\n", c1, s1 - 6, 0, 0);
                }
            }
        }
        int epochs;
        double worst = solve_all(obs2, &nav_b, &epochs);
        CHECK(epochs == 3);
        CHECK(worst < 0.01);
        if (epochs != 3 || !(worst < 0.01)) printf("pass %d: epochs %d worst %g\n", pass, epochs, worst);
        SqrObs r;
        CHECK(sqr_obs_open(&r, obs2, &err) == 0 && !strcmp(r.code_name, "C1"));
        CHECK(r.n_types == (pass ? 7 : 3));
    }
}

static void test_mixed_and_select(void) {
    /* a mixed RINEX 3 file: GLONASS (3 more lines) and Galileo (7) are skipped */
    static char mixed[1 << 16];
    FILE *f = tmpfile();
    sqr_write_nav(f, eph, 2);
    const char *gps = slurp(f);
    const char *body = strstr(gps, "END OF HEADER");
    body = strchr(body, '\n') + 1;
    size_t at = (size_t)snprintf(mixed, sizeof mixed, "%-60s%-20s\n%-60s%-20s\n",
                                 "     3.04           N: GNSS NAV DATA    M: MIXED", "RINEX VERSION / TYPE", "",
                                 "END OF HEADER");
    at += (size_t)snprintf(mixed + at, sizeof mixed - at, "R05 2026 11 13 00 15 00 1.0E-05 0.0 0.0\n");
    for (int k = 0; k < 3; k++) at += (size_t)snprintf(mixed + at, sizeof mixed - at, "     1.0 2.0 3.0 4.0\n");
    at += (size_t)snprintf(mixed + at, sizeof mixed - at, "%s", body);
    at += (size_t)snprintf(mixed + at, sizeof mixed - at, "E11 2026 11 13 00 10 00 1.0E-05 0.0 0.0\n");
    for (int k = 0; k < 7; k++) at += (size_t)snprintf(mixed + at, sizeof mixed - at, "     1.0 2.0 3.0 4.0\n");
    SqrNav m;
    SqrError err;
    CHECK(sqr_read_nav(mixed, &m, &err) == 2 && m.skipped == 2);

    /* selection: nearest toe, healthy, within 4 hours */
    SqrNav s;
    s.n = 4;
    s.eph[0] = eph[0];
    s.eph[1] = eph[0];
    s.eph[1].toe += 7200;
    s.eph[1].af0 = 1;
    s.eph[2] = eph[1];
    s.eph[2].health = 1;
    s.eph[3] = eph[2];
    s.eph[3].toe -= 6 * 3600;
    SqgEphemeris out[4];
    CHECK(sqr_nav_select(&s, WEEK, T0 + 7000, out, 4) == 1 && out[0].af0 == 1);
    CHECK(sqr_nav_select(&s, WEEK, T0 + 100, out, 4) == 1 && out[0].af0 == eph[0].af0);
    CHECK(sqr_nav_select(&s, WEEK + 1, T0, out, 4) == 0);
    /* across the week boundary */
    s.n = 1;
    s.eph[0].week = WEEK;
    s.eph[0].toe = SQG_WEEK - 1800;
    CHECK(sqr_nav_select(&s, WEEK + 1, 1800, out, 4) == 1);
}

static void test_errors(void) {
    SqrNav nav_c;
    SqrError err;
    CHECK(sqr_read_nav("     3.04           N: GNSS NAV DATA    G: GPS              RINEX VERSION / TYPE\n", &nav_c,
                       &err) == -1 &&
          strstr(err.msg, "END OF HEADER"));
    FILE *f = tmpfile();
    sqr_write_nav(f, eph, 2);
    static char cut[1 << 16];
    copy_text(cut, sizeof cut, slurp(f));
    *strrchr(cut, '\n') = 0;
    *(strrchr(cut, '\n') + 1) = 0; /* lose the last two lines */
    *strrchr(cut, '\n') = 0;
    *(strrchr(cut, '\n') + 1) = 0;
    CHECK(sqr_read_nav(cut, &nav_c, &err) == -1 && strstr(err.msg, "cut short") && err.line == 18);
    f = tmpfile();
    sqr_write_nav(f, eph, 1);
    copy_text(cut, sizeof cut, slurp(f));
    memcpy(strstr(cut, "G01") + 30, "x", 1);
    CHECK(sqr_read_nav(cut, &nav_c, &err) == -1 && err.line == 4);

    SqrObs r;
    CHECK(sqr_obs_open(&r, cut, &err) == -1 && strstr(err.msg, "not an observation file"));
    const char *no_code = "     3.04           OBSERVATION DATA    G: GPS              RINEX VERSION / TYPE\n"
                          "G    2 L1C S1C                                              SYS / # / OBS TYPES\n"
                          "                                                            END OF HEADER\n";
    CHECK(sqr_obs_open(&r, no_code, &err) == -1 && strstr(err.msg, "pseudorange"));
    const char *event = "     3.04           OBSERVATION DATA    G: GPS              RINEX VERSION / TYPE\n"
                        "G    1 C1C                                                  SYS / # / OBS TYPES\n"
                        "                                                            END OF HEADER\n"
                        "> 2026 11 13 00 00  0.0000000  4  1\n"
                        "a comment inside an event                                   COMMENT\n"
                        "> 2026 11 13 00 00 30.0000000  0  2\n"
                        "G01  21000000.000\n"
                        "E05  22000000.000\n"
                        "oops\n";
    SqgEpoch ep;
    int week;
    CHECK(sqr_obs_open(&r, event, &err) == 0);
    CHECK(sqr_obs_next(&r, &ep, &week, &err) == 1 && ep.n_obs == 1 && ep.t_receiver == T0 + 30);
    CHECK(sqr_obs_next(&r, &ep, &week, &err) == -1 && err.line == 9);
}

void test_rinex(int *checks, int *failures) {
    nchecks = checks;
    nfails = failures;
    test_time();
    test_rinex3();
    test_rinex2();
    test_mixed_and_select();
    test_errors();
}
