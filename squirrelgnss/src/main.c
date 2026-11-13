/* squirrelgnss: command line over sqgnss. Original code. */
#include "sqgnss.h"
#include "sqrinex.h"
#include "sqrtk.h"

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
          "  squirrelgnss rinex-demo OBS NAV [EPOCHS] [--at LAT,LON,H] [--noise M] [--seed N]\n"
          "  squirrelgnss solve OBS NAV [--mask DEG] [--truth LAT,LON,H] [--average]\n"
          "  squirrelgnss guide HERE TARGET [--tol M]  which way and how far to the target\n"
          "  squirrelgnss rtk-demo [EPOCHS] [--baseline E,N,U] [--noise M] [--dual] [--iono M]\n"
          "                       [--kinematic] [--write BASE_OBS ROVER_OBS NAV]\n"
          "  squirrelgnss rtk BASE_OBS ROVER_OBS NAV --base-at LAT,LON,H [--truth LAT,LON,H] [--kinematic]\n"
          "  squirrelgnss help\n"
          "demo-track solves a synthetic 30-satellite sky; --fault adds a bias to one\n"
          "satellite, which the fault detection should exclude.\n"
          "rinex-demo writes the synthetic sky as RINEX 3 observation and navigation\n"
          "files; solve reads GPS from RINEX 2.11 or 3.x files (a receiver's own, or\n"
          "those) and prints a fix for every epoch; --average adds the mean of the\n"
          "occupation and the spread of one fix.\n"
          "rtk-demo and rtk solve a rover from code and carrier double differences\n"
          "against a base at a known place, fixing the whole-cycle ambiguities by\n"
          "integer least squares when the ratio test passes. With L2 (--dual, or in\n"
          "the files) the wide lanes are fixed first and both carriers are used.\n"
          "--kinematic follows a moving rover epoch by epoch (the demo drives a\n"
          "100 m circle at 5 m/s); --write saves the demo as RINEX for rtk.\n",
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
    unsigned seed = 777u;
    SqgGeodetic truth = HOME;
    for (int i = 4; i < argc; i++) {
        int more = i + 1 < argc;
        if (!strcmp(argv[i], "--noise") && more) noise = atof(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && more) seed = (unsigned)strtoul(argv[++i], NULL, 10) * 7919u;
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
        sqg_demo_epoch(ephs, n_eph, truth, t0 + 30.0 * k, 15.0, 5.0, noise, seed + (unsigned)k, &ep);
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
    int have_truth = 0, average = 0;
    for (int i = 4; i < argc; i++) {
        int more = i + 1 < argc;
        if (!strcmp(argv[i], "--average")) average = 1;
        else if (!strcmp(argv[i], "--mask") && more) opt.elev_mask_deg = atof(argv[++i]);
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
    SqgEcef truth_ecef = {0, 0, 0};
    static SqgStream stream;
    sqg_stream_init(&stream, &opt);
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
        if (sqg_stream_push(&stream, &ep, sel, n_sel)) {
            printf(" no fix (%d observations, %d ephemerides)\n", ep.n_obs, n_sel);
            continue;
        }
        fixed++;
        const SqgPvt p = stream.pvt;
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
    if (average && fixed > 0) {
        SqgGeodetic mean;
        SqgEnu sd;
        int n_avg = sqg_stream_average(&stream, &mean, &sd);
        printf("# average of %d fixes: lat=%.8f lon=%.8f h=%.3f sd E=%.3f N=%.3f U=%.3f", n_avg, mean.lat_deg,
               mean.lon_deg, mean.height_m, sd.e, sd.n, sd.u);
        if (have_truth) {
            SqgEnu e = sqg_stakeout_delta(truth, mean);
            char t1[40], t2[40], t3[40];
            printf(" err E=%s N=%s U=%s", num(e.e, 3, t1), num(e.n, 3, t2), num(e.u, 3, t3));
        }
        printf("\n");
    }
    printf("# %d epochs, %d fixed\n", epochs, fixed);
    free(obs_text);
    free(nav_text);
    return rc;
}

static void print_rtk(const SqgRtk *k, const SqgGeodetic *truth) {
    SqgGeodetic g;
    sqg_ecef_to_geodetic(k->rover, &g);
    printf("%s epochs=%d ref=PRN%02d dd=%d ratio=%.1f rms=%.4f m%s\n", k->fixed ? "fixed" : "float", k->n_epochs,
           k->ref_prn, k->n_dd, k->ratio, k->rms_phase_m, k->dual ? " L1+L2" : " L1");
    printf("rover lat=%.9f lon=%.9f h=%.4f\n", g.lat_deg, g.lon_deg, g.height_m);
    printf("ambiguities");
    for (int j = 0; j < k->n_dd; j++) printf(" PRN%02d=%.2f", k->prn[j], k->float_amb[j]);
    printf("\n");
    if (truth) {
        SqgEnu e = sqg_stakeout_delta(*truth, g);
        char t1[40], t2[40], t3[40];
        printf("err E=%s N=%s U=%s\n", num(e.e, 3, t1), num(e.n, 3, t2), num(e.u, 3, t3));
    }
}

#define RTK_MAX_EPOCHS 1024
static SqgEpoch rtk_base[RTK_MAX_EPOCHS], rtk_rover[RTK_MAX_EPOCHS];

/* one kinematic epoch: time, state, ratio, satellites, and where (or how far off) */
static void print_fix(int week, const SqgRtkFix *f, const SqgGeodetic *truth) {
    SqgGeodetic g;
    sqg_ecef_to_geodetic(f->pos, &g);
    int y, mo, d, h, mi;
    double sec;
    sqr_calendar(week, f->t, &y, &mo, &d, &h, &mi, &sec);
    printf("%02d:%02d:%04.1f %s ratio=%.1f dd=%d wl=%d", h, mi, sec, f->fixed ? "fixed" : "float", f->ratio, f->n_dd,
           f->wide_lanes);
    if (truth) {
        SqgEnu e = sqg_stakeout_delta(*truth, g);
        char t1[40], t2[40], t3[40];
        printf(" err E=%s N=%s U=%s", num(e.e, 3, t1), num(e.n, 3, t2), num(e.u, 3, t3));
    } else
        printf(" lat=%.9f lon=%.9f h=%.4f", g.lat_deg, g.lon_deg, g.height_m);
    if (f->slips) printf(" slips=%d", f->slips);
    if (f->reset) printf(" reset");
    if (f->dropped) printf(" dropped");
    printf("\n");
}

static int rtk_demo(int argc, char **argv) {
    int n = -1, dual = 0, kin = 0;
    double noise = 0.3, iono = 0;
    SqgEnu bl = {123.456, -78.9, 2.5};
    const char *wb = NULL, *wr = NULL, *wn = NULL;
    for (int i = 2; i < argc; i++) {
        int more = i + 1 < argc;
        if (!strcmp(argv[i], "--noise") && more) noise = atof(argv[++i]);
        else if (!strcmp(argv[i], "--iono") && more) iono = atof(argv[++i]);
        else if (!strcmp(argv[i], "--dual")) dual = 1;
        else if (!strcmp(argv[i], "--kinematic")) kin = 1;
        else if (!strcmp(argv[i], "--write") && i + 3 < argc) {
            wb = argv[++i];
            wr = argv[++i];
            wn = argv[++i];
        } else if (!strcmp(argv[i], "--baseline") && more) {
            if (sscanf(argv[++i], "%lf,%lf,%lf", &bl.e, &bl.n, &bl.u) != 3) return usage(stderr), 2;
        } else if (argv[i][0] != '-' && i == 2) n = atoi(argv[i]);
        else return usage(stderr), 2;
    }
    if (n < 0) n = kin ? 120 : 40; /* two minutes at 1 s, or ten at 15 s */
    if (n < 1) n = 1;
    if (n > RTK_MAX_EPOCHS) n = RTK_MAX_EPOCHS;
    const int week = 2444;
    const double t0 = 432000.0, dt = kin ? 1.0 : 15.0;
    SqgEphemeris ephs[SQG_DEMO_SATS];
    int n_eph;
    sqg_demo_ephemerides(ephs, &n_eph, t0);
    for (int i = 0; i < n_eph; i++) ephs[i].week = week;
    SqgEcef base_pos;
    sqg_geodetic_to_ecef(HOME, &base_pos);
    /* the rover: at the baseline, or driving a 100 m circle round it at 5 m/s */
    static SqgGeodetic where[RTK_MAX_EPOCHS];
    for (int k = 0; k < n; k++) {
        SqgEnu at = bl;
        if (kin) {
            double a = 5.0 * k / 100.0;
            at.e += 100 * cos(a);
            at.n += 100 * sin(a);
        }
        SqgEcef p;
        sqg_enu_to_ecef(base_pos, HOME, at, &p);
        sqg_ecef_to_geodetic(p, &where[k]);
        SqgDemoOptions ob = {400.0 + k, 5.0, noise, 31u + (unsigned)k, 11, dual, iono};
        SqgDemoOptions orv = {-900.0 - 2.0 * k, 5.0, noise, 5003u + (unsigned)k, 22, dual, iono};
        sqg_demo_epoch_opt(ephs, n_eph, HOME, t0 + dt * k, &ob, &rtk_base[k]);
        sqg_demo_epoch_opt(ephs, n_eph, where[k], t0 + dt * k, &orv, &rtk_rover[k]);
    }
    if (wb) {
        FILE *fb = fopen(wb, "w"), *fr = fopen(wr, "w"), *fn = fopen(wn, "w");
        int bad = !fb || !fr || !fn;
        if (!bad) {
            SqgEcef rp;
            sqg_geodetic_to_ecef(where[0], &rp);
            sqr_write_nav(fn, ephs, n_eph);
            sqr_write_obs_header(fb, &base_pos);
            sqr_write_obs_header(fr, &rp);
            for (int k = 0; k < n; k++) {
                sqr_write_obs_epoch(fb, week, &rtk_base[k]);
                sqr_write_obs_epoch(fr, week, &rtk_rover[k]);
            }
            bad = ferror(fb) || ferror(fr) || ferror(fn);
        }
        if (fb) bad |= fclose(fb) != 0;
        if (fr) bad |= fclose(fr) != 0;
        if (fn) bad |= fclose(fn) != 0;
        if (bad) {
            fprintf(stderr, "squirrelgnss: could not write the RINEX files\n");
            return 1;
        }
    }
    if (kin) {
        static SqgRtkFilter flt;
        sqg_rtk_filter_init(&flt, base_pos, NULL);
        int fixed = 0, first = -1;
        double worst = 0;
        for (int k = 0; k < n; k++) {
            SqgRtkFix fx;
            if (sqg_rtk_filter_push(&flt, &rtk_base[k], &rtk_rover[k], ephs, n_eph, &fx)) {
                printf("epoch %d: no solution\n", k);
                continue;
            }
            print_fix(week, &fx, &where[k]);
            if (fx.fixed) {
                SqgEcef t;
                sqg_geodetic_to_ecef(where[k], &t);
                worst = fmax(worst, sqg_baseline_length(fx.pos, t));
                fixed++;
                if (first < 0) first = k;
            }
        }
        printf("# %d epochs, %d fixed", n, fixed);
        if (first >= 0) printf(", first fix at epoch %d, worst fixed error %.3f m", first, worst);
        printf("\n");
        return 0;
    }
    SqgRtk out;
    if (sqg_rtk_static(rtk_base, rtk_rover, n, ephs, n_eph, base_pos, NULL, &out)) {
        fprintf(stderr, "squirrelgnss: no RTK solution\n");
        return 1;
    }
    print_rtk(&out, &where[0]);
    return 0;
}

/* read every GPS epoch of an observation file into eps */
static int read_epochs(const char *path, const char *text, SqgEpoch *eps, int cap, int *week) {
    SqrObs r;
    SqrError err;
    if (sqr_obs_open(&r, text, &err)) {
        fprintf(stderr, "%s:%d: %s\n", path, err.line, err.msg);
        return -1;
    }
    int n = 0, got;
    SqgEpoch ep;
    while ((got = sqr_obs_next(&r, &ep, week, &err)) == 1)
        if (n < cap) eps[n++] = ep;
    if (got < 0) {
        fprintf(stderr, "%s:%d: %s\n", path, err.line, err.msg);
        return -1;
    }
    return n;
}

static SqgEpoch rtk_in_base[RTK_MAX_EPOCHS], rtk_in_rover[RTK_MAX_EPOCHS];

static int rtk(int argc, char **argv) {
    if (argc < 5) return usage(stderr), 2;
    SqgGeodetic base, truth;
    int have_base = 0, have_truth = 0, kin = 0;
    for (int i = 5; i < argc; i++) {
        int more = i + 1 < argc;
        if (!strcmp(argv[i], "--kinematic")) kin = 1;
        else if (!strcmp(argv[i], "--base-at") && more) {
            if (!parse_llh(argv[++i], &base)) return usage(stderr), 2;
            have_base = 1;
        } else if (!strcmp(argv[i], "--truth") && more) {
            if (!parse_llh(argv[++i], &truth)) return usage(stderr), 2;
            have_truth = 1;
        } else return usage(stderr), 2;
    }
    if (!have_base) return usage(stderr), 2;
    char *bt = slurp(argv[2]), *rt = slurp(argv[3]), *nt = slurp(argv[4]);
    int rc = 1;
    static SqrNav nav;
    SqrError err;
    int week = 0, nb, nr;
    if (!bt || !rt || !nt) {
        fprintf(stderr, "squirrelgnss: could not read %s\n", !bt ? argv[2] : !rt ? argv[3] : argv[4]);
    } else if (sqr_read_nav(nt, &nav, &err) < 0) {
        fprintf(stderr, "%s:%d: %s\n", argv[4], err.line, err.msg);
    } else if ((nb = read_epochs(argv[2], bt, rtk_in_base, RTK_MAX_EPOCHS, &week)) >= 0 &&
               (nr = read_epochs(argv[3], rt, rtk_in_rover, RTK_MAX_EPOCHS, &week)) >= 0) {
        /* pair the epochs by time */
        int n = 0;
        for (int i = 0, j = 0; i < nb && j < nr;) {
            double dt = rtk_in_rover[j].t_receiver - rtk_in_base[i].t_receiver;
            if (fabs(dt) < 0.05) {
                rtk_base[n] = rtk_in_base[i++];
                rtk_rover[n++] = rtk_in_rover[j++];
            } else if (dt > 0) i++;
            else j++;
        }
        SqgEphemeris sel[SQG_MAX_SATS];
        int n_sel = n ? sqr_nav_select(&nav, week, rtk_base[n / 2].t_receiver, sel, SQG_MAX_SATS) : 0;
        SqgEcef base_pos;
        sqg_geodetic_to_ecef(base, &base_pos);
        SqgRtk out;
        printf("# %d common epochs, %d ephemerides\n", n, n_sel);
        if (kin && n > 0) {
            static SqgRtkFilter flt;
            sqg_rtk_filter_init(&flt, base_pos, NULL);
            int fixed = 0;
            for (int k = 0; k < n; k++) {
                SqgRtkFix fx;
                if (sqg_rtk_filter_push(&flt, &rtk_base[k], &rtk_rover[k], sel, n_sel, &fx)) continue;
                print_fix(week, &fx, NULL);
                fixed += fx.fixed;
            }
            printf("# %d epochs, %d fixed\n", n, fixed);
            rc = 0;
        } else if (n == 0 || sqg_rtk_static(rtk_base, rtk_rover, n, sel, n_sel, base_pos, NULL, &out)) {
            fprintf(stderr, "squirrelgnss: no RTK solution (common epochs with carrier on 5 satellites needed)\n");
        } else {
            print_rtk(&out, have_truth ? &truth : NULL);
            rc = 0;
        }
    }
    free(bt);
    free(rt);
    free(nt);
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
    if (!strcmp(cmd, "rtk-demo")) return rtk_demo(argc, argv);
    if (!strcmp(cmd, "rtk")) return rtk(argc, argv);
    if (!strcmp(cmd, "guide")) {
        SqgGeodetic here, target;
        double tol = 0.02;
        if (argc < 4 || !parse_llh(argv[2], &here) || !parse_llh(argv[3], &target)) return usage(stderr), 2;
        for (int i = 4; i < argc; i++) {
            if (!strcmp(argv[i], "--tol") && i + 1 < argc) tol = atof(argv[++i]);
            else return usage(stderr), 2;
        }
        SqgGuide g = sqg_stakeout_guide(here, target, tol);
        char buf[160];
        sqg_guide_text(&g, buf, sizeof buf);
        printf("%s\n", buf);
        return 0;
    }
    usage(stderr);
    return 2;
}
