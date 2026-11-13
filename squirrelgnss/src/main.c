/* squirrelgnss: command line over sqgnss. Original code. */
#include "sqgnss.h"
#include "sqrinex.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_llh(const char *s, SqgGeodetic *g) {
    return sscanf(s, "%lf,%lf,%lf", &g->lat_deg, &g->lon_deg, &g->height_m) == 3;
}

/* v with `places` decimals, and no "-0.00" for a value that rounds to zero */
static const char *num(double v, int places, char *buf) {
    snprintf(buf, 40, "%.*f", places, v);
    if (buf[0] == '-' && strspn(buf + 1, "0.") == strlen(buf + 1)) memmove(buf, buf + 1, strlen(buf));
    return buf;
}

static void usage(FILE *f) {
    fputs("SquirrelGNSS: GPS positioning and land-survey helpers\n"
          "  squirrelgnss llh2ecef LAT,LON,H\n"
          "  squirrelgnss ecef2llh X,Y,Z\n"
          "  squirrelgnss stakeout LAT,LON,H LAT,LON,H\n"
          "  squirrelgnss sky [LAT,LON,H]            the demo satellites in view: PRN, azimuth, elevation\n"
          "  squirrelgnss demo-track [EPOCHS] [--at LAT,LON,H] [--noise M] [--fault PRN:METRES]\n"
          "                          [--mask DEG] [--out FILE.sqobs]\n"
          "  squirrelgnss rinex-demo OBS NAV [EPOCHS] [--at LAT,LON,H] [--noise M]\n"
          "  squirrelgnss solve OBS NAV [--mask DEG] [--truth LAT,LON,H]\n"
          "  squirrelgnss help\n"
          "demo-track solves a synthetic 30-satellite sky; --fault adds a bias to one\n"
          "satellite, which the fault detection should exclude.\n"
          "rinex-demo writes the synthetic sky as RINEX 3 observation and navigation\n"
          "files; solve reads GPS from RINEX 2.11 or 3.x files (a receiver's own, or\n"
          "those) and prints a fix for every epoch.\n",
          f);
}

static const SqgGeodetic HOME = {35.681236, 139.767125, 40.0};

/* the whole of a file, NUL-terminated, or NULL */
static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = NULL;
    size_t len = 0, cap = 0, got;
    do {
        if (cap - len < 65536) {
            cap = cap ? cap * 2 : 1 << 20;
            char *more = realloc(buf, cap + 1);
            if (!more) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = more;
        }
        got = fread(buf + len, 1, cap - len, f);
        len += got;
    } while (got > 0);
    fclose(f);
    buf[len] = 0;
    return buf;
}

static int rinex_demo(int argc, char **argv) {
    if (argc < 4) return usage(stderr), 2;
    const char *obs_path = argv[2], *nav_path = argv[3];
    int n = 10;
    double noise = 0.5;
    SqgGeodetic truth = HOME;
    for (int i = 4; i < argc; i++) {
        int more = i + 1 < argc;
        if (!strcmp(argv[i], "--noise") && more) noise = atof(argv[++i]);
        else if (!strcmp(argv[i], "--at") && more) {
            if (!parse_llh(argv[++i], &truth)) return usage(stderr), 2;
        } else if (argv[i][0] != '-' && i == 4) n = atoi(argv[i]);
        else return usage(stderr), 2;
    }
    if (n < 1) n = 1;
    if (n > 2880) n = 2880; /* a day at 30 s */
    const int week = 2444;
    const double t0 = 432000.0; /* 2026-11-13 00:00 GPS */
    SqgEphemeris ephs[SQG_DEMO_SATS];
    int n_eph;
    sqg_demo_ephemerides(ephs, &n_eph, t0);
    for (int i = 0; i < n_eph; i++) ephs[i].week = week;
    FILE *nav = fopen(nav_path, "w"), *obs = fopen(obs_path, "w");
    if (!nav || !obs) {
        fprintf(stderr, "squirrelgnss: could not write %s\n", !nav ? nav_path : obs_path);
        if (nav) fclose(nav);
        if (obs) fclose(obs);
        return 1;
    }
    sqr_write_nav(nav, ephs, n_eph);
    SqgEcef approx;
    sqg_geodetic_to_ecef(truth, &approx);
    sqr_write_obs_header(obs, &approx);
    for (int k = 0; k < n; k++) {
        SqgEpoch ep;
        sqg_demo_epoch(ephs, n_eph, truth, t0 + 30.0 * k, 15.0, 5.0, noise, 777u + (unsigned)k, &ep);
        sqr_write_obs_epoch(obs, week, &ep);
    }
    int bad = ferror(nav) || ferror(obs);
    bad |= fclose(nav) != 0;
    bad |= fclose(obs) != 0;
    if (bad) {
        fprintf(stderr, "squirrelgnss: write failed\n");
        return 1;
    }
    printf("wrote %s (%d epochs) and %s (%d satellites)\n", obs_path, n, nav_path, n_eph);
    return 0;
}

static int solve(int argc, char **argv) {
    if (argc < 4) return usage(stderr), 2;
    SqgPvtOptions opt;
    sqg_pvt_options_default(&opt);
    SqgGeodetic truth;
    int have_truth = 0;
    for (int i = 4; i < argc; i++) {
        int more = i + 1 < argc;
        if (!strcmp(argv[i], "--mask") && more) opt.elev_mask_deg = atof(argv[++i]);
        else if (!strcmp(argv[i], "--truth") && more) {
            if (!parse_llh(argv[++i], &truth)) return usage(stderr), 2;
            have_truth = 1;
        } else return usage(stderr), 2;
    }
    char *obs_text = slurp(argv[2]), *nav_text = slurp(argv[3]);
    if (!obs_text || !nav_text) {
        fprintf(stderr, "squirrelgnss: could not read %s\n", !obs_text ? argv[2] : argv[3]);
        free(obs_text);
        free(nav_text);
        return 1;
    }
    static SqrNav nav;
    SqrError err;
    int rc = 0;
    SqrObs r;
    if (sqr_read_nav(nav_text, &nav, &err) < 0) {
        fprintf(stderr, "%s:%d: %s\n", argv[3], err.line, err.msg);
        rc = 1;
    } else if (sqr_obs_open(&r, obs_text, &err)) {
        fprintf(stderr, "%s:%d: %s\n", argv[2], err.line, err.msg);
        rc = 1;
    }
    if (rc) {
        free(obs_text);
        free(nav_text);
        return rc;
    }
    SqgEcef truth_ecef = {0, 0, 0}, last = {r.approx[0], r.approx[1], r.approx[2]};
    if (have_truth) sqg_geodetic_to_ecef(truth, &truth_ecef);
    printf("# %d GPS ephemerides (%d other records skipped), pseudorange %s\n", nav.n, nav.skipped, r.code_name);
    int epochs = 0, fixed = 0, week, got;
    SqgEpoch ep;
    while ((got = sqr_obs_next(&r, &ep, &week, &err)) == 1) {
        epochs++;
        SqgEphemeris sel[SQG_MAX_SATS];
        int n_sel = sqr_nav_select(&nav, week, ep.t_receiver, sel, SQG_MAX_SATS);
        int y, mo, d, h, mi;
        double sec;
        sqr_calendar(week, ep.t_receiver, &y, &mo, &d, &h, &mi, &sec);
        printf("%04d-%02d-%02d %02d:%02d:%04.1f", y, mo, d, h, mi, sec);
        SqgPvt p;
        if (sqg_solve_pvt_opt(&ep, sel, n_sel, last, &opt, &p)) {
            printf(" no fix (%d observations, %d ephemerides)\n", ep.n_obs, n_sel);
            continue;
        }
        fixed++;
        last = p.pos;
        SqgGeodetic g;
        sqg_ecef_to_geodetic(p.pos, &g);
        printf(" used=%d lat=%.8f lon=%.8f h=%.3f hdop=%.2f vdop=%.2f rms=%.2f", p.n_used, g.lat_deg, g.lon_deg,
               g.height_m, p.hdop, p.vdop, p.rms_m);
        if (have_truth) {
            SqgEnu e;
            char t1[40], t2[40], t3[40];
            sqg_ecef_to_enu(truth_ecef, p.pos, truth, &e);
            printf(" err E=%s N=%s U=%s", num(e.e, 2, t1), num(e.n, 2, t2), num(e.u, 2, t3));
        }
        if (p.excluded_prn > 0) printf(" excluded=PRN%02d", p.excluded_prn);
        printf("\n");
    }
    if (got < 0) {
        fprintf(stderr, "%s:%d: %s\n", argv[2], err.line, err.msg);
        rc = 1;
    }
    printf("# %d epochs, %d fixed\n", epochs, fixed);
    free(obs_text);
    free(nav_text);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    const char *cmd = argv[1];
    if (!strcmp(cmd, "help") || !strcmp(cmd, "--help")) {
        usage(stdout);
        return 0;
    }
    if (!strcmp(cmd, "llh2ecef")) {
        SqgGeodetic g;
        SqgEcef e;
        if (argc != 3 || !parse_llh(argv[2], &g)) return usage(stderr), 2;
        sqg_geodetic_to_ecef(g, &e);
        char t1[40], t2[40], t3[40];
        printf("%s %s %s\n", num(e.x, 4, t1), num(e.y, 4, t2), num(e.z, 4, t3));
        return 0;
    }
    if (!strcmp(cmd, "ecef2llh")) {
        SqgEcef e;
        SqgGeodetic g;
        if (argc != 3 || sscanf(argv[2], "%lf,%lf,%lf", &e.x, &e.y, &e.z) != 3) return usage(stderr), 2;
        sqg_ecef_to_geodetic(e, &g);
        char t1[40], t2[40], t3[40];
        printf("%s %s %s\n", num(g.lat_deg, 10, t1), num(g.lon_deg, 10, t2), num(g.height_m, 4, t3));
        return 0;
    }
    if (!strcmp(cmd, "stakeout")) {
        SqgGeodetic a, b;
        if (argc != 4 || !parse_llh(argv[2], &a) || !parse_llh(argv[3], &b)) return usage(stderr), 2;
        SqgEnu d = sqg_stakeout_delta(a, b);
        char t1[40], t2[40], t3[40];
        printf("delta ENU: E=%s N=%s U=%s  horiz=%.3f\n", num(d.e, 3, t1), num(d.n, 3, t2), num(d.u, 3, t3),
               sqrt(d.e * d.e + d.n * d.n));
        return 0;
    }
    if (!strcmp(cmd, "sky")) {
        SqgGeodetic at = HOME;
        if (argc > 3 || (argc == 3 && !parse_llh(argv[2], &at))) return usage(stderr), 2;
        SqgEphemeris ephs[SQG_DEMO_SATS];
        int n;
        sqg_demo_ephemerides(ephs, &n, 100000.0);
        SqgEcef rx;
        sqg_geodetic_to_ecef(at, &rx);
        for (int i = 0; i < n; i++) {
            SqgAzEl ae = sqg_az_el(rx, at, sqg_sat_position(&ephs[i], 100000.0));
            if (ae.el_deg >= 0) printf("PRN%02d az=%6.1f el=%5.1f\n", ephs[i].prn, ae.az_deg, ae.el_deg);
        }
        return 0;
    }
    if (!strcmp(cmd, "demo-track")) {
        int n = 5, fault_prn = 0;
        double noise = 0.5, fault_m = 0, mask = 10;
        const char *out = NULL;
        SqgGeodetic truth = HOME;
        for (int i = 2; i < argc; i++) {
            int more = i + 1 < argc;
            if (!strcmp(argv[i], "--noise") && more) noise = atof(argv[++i]);
            else if (!strcmp(argv[i], "--mask") && more) mask = atof(argv[++i]);
            else if (!strcmp(argv[i], "--out") && more) out = argv[++i];
            else if (!strcmp(argv[i], "--at") && more) {
                if (!parse_llh(argv[++i], &truth)) return usage(stderr), 2;
            } else if (!strcmp(argv[i], "--fault") && more) {
                if (sscanf(argv[++i], "%d:%lf", &fault_prn, &fault_m) != 2) return usage(stderr), 2;
            } else if (argv[i][0] != '-' && i == 2) n = atoi(argv[i]);
            else return usage(stderr), 2;
        }
        if (n < 1) n = 1;
        if (n > 100) n = 100;
        SqgEcef truth_ecef;
        sqg_geodetic_to_ecef(truth, &truth_ecef);
        SqgEphemeris ephs[SQG_DEMO_SATS];
        int n_eph;
        sqg_demo_ephemerides(ephs, &n_eph, 100000.0);
        static SqgTrack track;
        sqg_track_init(&track);
        SqgEcef start = {0, 0, 0}; /* no idea where we are */
        SqgPvtOptions opt;
        sqg_pvt_options_default(&opt);
        opt.elev_mask_deg = mask;
        for (int k = 0; k < n; k++) {
            SqgEpoch ep;
            sqg_demo_epoch(ephs, n_eph, truth, 100000.0 + k, 15.0 + 0.3 * k, 5.0, noise, 1234u + (unsigned)k, &ep);
            for (int i = 0; i < ep.n_obs; i++)
                if (ep.obs[i].prn == fault_prn) ep.obs[i].pseudorange_m += fault_m;
            SqgPvt *p = &track.solutions[track.n_epochs];
            track.epochs[track.n_epochs++] = ep;
            sqg_solve_pvt_opt(&ep, ephs, n_eph, k ? track.solutions[k - 1].pos : start, &opt, p);
            SqgEnu d;
            sqg_ecef_to_enu(truth_ecef, p->pos, truth, &d);
            char t1[40], t2[40], t3[40];
            printf("epoch %d used=%d conv=%d hdop=%.2f vdop=%.2f err E=%s N=%s U=%s m cb=%.2f m rms=%.2f", k,
                   p->n_used, p->converged, p->hdop, p->vdop, num(d.e, 2, t1), num(d.n, 2, t2), num(d.u, 2, t3),
                   p->clock_bias_m, p->rms_m);
            if (p->excluded_prn > 0) printf(" excluded=PRN%02d", p->excluded_prn);
            if (p->excluded_prn < 0) printf(" FAULT");
            printf("\n");
        }
        if (out) {
            if (sqg_write_sqobs(&track, out)) {
                fprintf(stderr, "squirrelgnss: could not write %s\n", out);
                return 1;
            }
            printf("wrote %s\n", out);
        }
        return 0;
    }
    if (!strcmp(cmd, "rinex-demo")) return rinex_demo(argc, argv);
    if (!strcmp(cmd, "solve")) return solve(argc, argv);
    usage(stderr);
    return 2;
}
