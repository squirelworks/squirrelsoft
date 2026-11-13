/* squirrelgnss: command line over sqgnss. Original code. */
#include "sqgnss.h"
#include "sqrecv.h"
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
          "                          [--dual] [--l5] [--iono M]\n"
          "  squirrelgnss solve OBS NAV [--mask DEG] [--truth LAT,LON,H] [--average] [--iono-free]\n"
          "  squirrelgnss guide HERE TARGET [--tol M]  which way and how far to the target\n"
          "  squirrelgnss rtk-demo [EPOCHS] [--baseline E,N,U] [--noise M] [--dual] [--iono M]\n"
          "                       [--kinematic] [--write BASE_OBS ROVER_OBS NAV]\n"
          "  squirrelgnss rtk BASE_OBS ROVER_OBS NAV --base-at LAT,LON,H [--truth LAT,LON,H] [--kinematic]\n"
          "  squirrelgnss receiver PORT|FILE|- [--baud N] [--configure] [--epochs N] [--iono-free]\n"
          "                       [--mask DEG] [--truth LAT,LON,H] [--nmea] [--rinex OBS NAV]\n"
          "  squirrelgnss ubx-demo FILE [EPOCHS] [--at LAT,LON,H] [--noise M] [--iono M] [--dual] [--l5]\n"
          "                       [--no-nmea] [--junk]\n"
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
          "100 m circle at 5 m/s); --write saves the demo as RINEX for rtk.\n"
          "receiver reads a receiver live (a serial port, 9600 baud by default) or a\n"
          "recording: NMEA for its own fix, and u-blox UBX RXM-RAWX, RXM-SFRBX and\n"
          "NAV-PVT for raw measurements, the broadcast orbits and its solution. Raw\n"
          "epochs are solved here and set beside the receiver's fix; --configure\n"
          "asks a u-blox receiver (CFG-MSG) for those messages; --rinex logs RINEX.\n"
          "ubx-demo writes such a stream from the synthetic sky. --iono-free solves\n"
          "from L1/L2 (or L1/L5) code, without the ionosphere.\n",
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
    int dual = 0, l5 = 0;
    double iono = 0;
    for (int i = 4; i < argc; i++) {
        int more = i + 1 < argc;
        if (!strcmp(argv[i], "--dual")) dual = 1;
        else if (!strcmp(argv[i], "--l5")) l5 = 1;
        else if (!strcmp(argv[i], "--iono") && more) iono = atof(argv[++i]);
        else if (!strcmp(argv[i], "--noise") && more) noise = atof(argv[++i]);
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
    int bands = SQR_BANDS_L2 | (l5 ? SQR_BANDS_L5 : 0);
    sqr_write_obs_header_bands(obs, &approx, bands);
    for (int k = 0; k < n; k++) {
        SqgEpoch ep;
        SqgDemoOptions o = {15.0, 5.0, noise, seed + (unsigned)k, 0, dual, iono, l5};
        sqg_demo_epoch_opt(ephs, n_eph, truth, t0 + 30.0 * k, &o, &ep);
        sqr_write_obs_epoch_bands(obs, week, &ep, bands);
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
        else if (!strcmp(argv[i], "--iono-free")) opt.iono_free = 1;
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
        SqgDemoOptions ob = {400.0 + k, 5.0, noise, 31u + (unsigned)k, 11, dual, iono, 0};
        SqgDemoOptions orv = {-900.0 - 2.0 * k, 5.0, noise, 5003u + (unsigned)k, 22, dual, iono, 0};
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

/* ---- a receiver ---- */

static int has_band_l5(const SqgEpoch *ep) {
    for (int i = 0; i < ep->n_obs; i++)
        if (ep->obs[i].has_l5) return 1;
    return 0;
}

static void gps_clock(int week, double tow, char *buf, size_t cap) {
    int y, mo, d, h, mi;
    double sec;
    sqr_calendar(week, tow, &y, &mo, &d, &h, &mi, &sec);
    snprintf(buf, cap, "%04d-%02d-%02d %02d:%02d:%04.1f", y, mo, d, h, mi, sec);
}

static SqrxReceiver rx;

static int receiver(int argc, char **argv) {
    if (argc < 3) return usage(stderr), 2;
    const char *src = argv[2], *obs_path = NULL, *nav_path = NULL;
    int baud = 9600, configure = 0, max_epochs = 0, nmea_lines = 0, have_truth = 0;
    SqgGeodetic truth;
    SqgPvtOptions opt;
    sqg_pvt_options_default(&opt);
    for (int i = 3; i < argc; i++) {
        int more = i + 1 < argc;
        if (!strcmp(argv[i], "--baud") && more) baud = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--configure")) configure = 1;
        else if (!strcmp(argv[i], "--epochs") && more) max_epochs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--iono-free")) opt.iono_free = 1;
        else if (!strcmp(argv[i], "--mask") && more) opt.elev_mask_deg = atof(argv[++i]);
        else if (!strcmp(argv[i], "--nmea")) nmea_lines = 1;
        else if (!strcmp(argv[i], "--truth") && more) {
            if (!parse_llh(argv[++i], &truth)) return usage(stderr), 2;
            have_truth = 1;
        } else if (!strcmp(argv[i], "--rinex") && i + 2 < argc) {
            obs_path = argv[++i];
            nav_path = argv[++i];
        } else return usage(stderr), 2;
    }
    char err[256];
    int fd = sqrx_serial_open(src, baud, configure, err, sizeof err);
    if (fd < 0) {
        fprintf(stderr, "squirrelgnss: %s\n", err);
        return 1;
    }
    if (configure) {
        /* raw measurements, subframes and the receiver's solution, every epoch */
        static const int msgs[3][2] = {{SQRX_UBX_RXM, SQRX_UBX_RXM_RAWX},
                                       {SQRX_UBX_RXM, SQRX_UBX_RXM_SFRBX},
                                       {SQRX_UBX_NAV, SQRX_UBX_NAV_PVT}};
        for (int k = 0; k < 3; k++) {
            unsigned char m[16];
            size_t n = sqrx_ubx_cfg_msg(m, sizeof m, msgs[k][0], msgs[k][1], 1);
            if (sqrx_serial_write(fd, m, n)) {
                fprintf(stderr, "squirrelgnss: could not configure the receiver on %s\n", src);
                sqrx_serial_close(fd);
                return 1;
            }
        }
    }
    sqrx_receiver_init(&rx);
    static SqgStream stream;
    sqg_stream_init(&stream, &opt);
    SqgEcef truth_ecef = {0, 0, 0};
    if (have_truth) sqg_geodetic_to_ecef(truth, &truth_ecef);
    FILE *obs = NULL;
    int rc = 0, epochs = 0, fixed = 0, ggas = 0, done = 0, bands = SQR_BANDS_L2;
    unsigned char buf[4096];
    long got;
    while (!done && (got = sqrx_serial_read(fd, buf, sizeof buf)) > 0) {
        for (long i = 0; i < got && !done; i++) {
            int ev = sqrx_receiver_byte(&rx, buf[i]);
            if ((ev & SQRX_EV_NMEA) && (nmea_lines || rx.n_epochs == 0)) {
                const SqrxNmea *n = &rx.nmea;
                /* a GGA just read: the receiver's fix */
                static double last_utc = -1;
                if (n->utc_s != last_utc && (n->have_pos || n->quality == 0)) {
                    last_utc = n->utc_s;
                    int s = (int)n->utc_s;
                    printf("nmea %02d:%02d:%04.1f", s / 3600, s / 60 % 60, fmod(n->utc_s, 60));
                    if (n->have_pos)
                        printf(" fix=%d sats=%d lat=%.8f lon=%.8f h=%.3f hdop=%.1f\n", n->quality, n->sats_used,
                               n->pos.lat_deg, n->pos.lon_deg, n->pos.height_m, n->hdop);
                    else
                        printf(" no fix\n");
                    ggas++;
                    if (rx.n_epochs == 0 && max_epochs > 0 && ggas >= max_epochs) done = 1;
                }
            }
            if (!(ev & SQRX_EV_EPOCH)) continue;
            epochs++;
            SqgEphemeris ephs[32];
            int n_eph = sqrx_receiver_ephemerides(&rx, ephs, 32);
            char when[40];
            gps_clock(rx.week, rx.epoch.t_receiver, when, sizeof when);
            printf("%s", when);
            if (nav_path && !obs) {
                obs = fopen(obs_path, "w");
                if (!obs) {
                    fprintf(stderr, "squirrelgnss: could not write %s\n", obs_path);
                    rc = 1;
                    done = 1;
                    break;
                }
                /* the types are fixed by the first epoch: L5 only if it has some */
                bands = SQR_BANDS_L2 | (has_band_l5(&rx.epoch) ? SQR_BANDS_L5 : 0);
                sqr_write_obs_header_bands(obs, NULL, bands);
            }
            if (obs) sqr_write_obs_epoch_bands(obs, rx.week, &rx.epoch, bands);
            if (sqg_stream_push(&stream, &rx.epoch, ephs, n_eph)) {
                printf(" no fix (%d observations, %d ephemerides)\n", rx.epoch.n_obs, n_eph);
            } else {
                fixed++;
                const SqgPvt *p = &stream.pvt;
                SqgGeodetic g;
                sqg_ecef_to_geodetic(p->pos, &g);
                printf(" used=%d lat=%.8f lon=%.8f h=%.3f hdop=%.2f rms=%.2f", p->n_used, g.lat_deg, g.lon_deg,
                       g.height_m, p->hdop, p->rms_m);
                /* beside the receiver's own: its NAV-PVT, else its last GGA */
                const SqgGeodetic *own = rx.have_navpvt && rx.navpvt.fix_type >= 2 ? &rx.navpvt.pos
                                         : rx.nmea.have_pos                       ? &rx.nmea.pos
                                                                                  : NULL;
                if (own) {
                    SqgEnu d = sqg_stakeout_delta(*own, g);
                    printf(" vs-receiver=%.2f m", sqrt(d.e * d.e + d.n * d.n + d.u * d.u));
                }
                if (have_truth) {
                    SqgEnu e;
                    char t1[40], t2[40], t3[40];
                    sqg_ecef_to_enu(truth_ecef, p->pos, truth, &e);
                    printf(" err E=%s N=%s U=%s", num(e.e, 2, t1), num(e.n, 2, t2), num(e.u, 2, t3));
                }
                if (p->excluded_prn > 0) printf(" excluded=PRN%02d", p->excluded_prn);
                printf("\n");
            }
            if (max_epochs > 0 && epochs >= max_epochs) done = 1;
        }
        fflush(stdout);
    }
    sqrx_serial_close(fd);
    if (obs) {
        int bad = ferror(obs) != 0;
        bad |= fclose(obs) != 0;
        FILE *nav = fopen(nav_path, "w");
        if (nav) {
            SqgEphemeris ephs[32];
            int n_eph = sqrx_receiver_ephemerides(&rx, ephs, 32);
            sqr_write_nav(nav, ephs, n_eph);
            bad |= ferror(nav) != 0;
            bad |= fclose(nav) != 0;
        }
        if (bad || !nav) {
            fprintf(stderr, "squirrelgnss: could not write %s\n", bad ? obs_path : nav_path);
            rc = 1;
        }
    }
    const SqrxFramer *f = &rx.framer;
    printf("# %ld bytes: nmea %ld (%ld bad), ubx %ld (%ld bad), %ld stray; %d epochs, %d fixed, %d ephemerides\n",
           f->bytes, f->nmea_ok, f->nmea_bad, f->ubx_ok, f->ubx_bad, f->junk, epochs, fixed, rx.n_eph_decoded);
    return rc;
}

/* A u-blox-style stream of the synthetic sky: the navigation message, then
 * every second NAV-PVT, NMEA (GGA, RMC, GSV) and RXM-RAWX. */
static int ubx_demo(int argc, char **argv) {
    if (argc < 3) return usage(stderr), 2;
    const char *path = argv[2];
    int n = 10, dual = 0, l5 = 0, nmea = 1, junk = 0;
    double noise = 0.5, iono = 0;
    SqgGeodetic truth = HOME;
    for (int i = 3; i < argc; i++) {
        int more = i + 1 < argc;
        if (!strcmp(argv[i], "--dual")) dual = 1;
        else if (!strcmp(argv[i], "--l5")) l5 = 1;
        else if (!strcmp(argv[i], "--no-nmea")) nmea = 0;
        else if (!strcmp(argv[i], "--junk")) junk = 1;
        else if (!strcmp(argv[i], "--noise") && more) noise = atof(argv[++i]);
        else if (!strcmp(argv[i], "--iono") && more) iono = atof(argv[++i]);
        else if (!strcmp(argv[i], "--at") && more) {
            if (!parse_llh(argv[++i], &truth)) return usage(stderr), 2;
        } else if (argv[i][0] != '-' && i == 3) n = atoi(argv[i]);
        else return usage(stderr), 2;
    }
    if (n < 1) n = 1;
    if (n > 3600) n = 3600;
    const int week = 2444;
    const double t0 = 432000.0, leap = 18.0;
    SqgEphemeris ephs[SQG_DEMO_SATS], dec[SQG_DEMO_SATS];
    int n_eph;
    sqg_demo_ephemerides(ephs, &n_eph, t0);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "squirrelgnss: could not write %s\n", path);
        return 1;
    }
    static unsigned char buf[1 << 16];
    size_t len;
    for (int i = 0; i < n_eph; i++) {
        ephs[i].week = week;
        SqrxSubframe sf[3];
        if (sqrx_lnav_encode(&ephs[i], 1 + i, t0 - 18, sf)) {
            fprintf(stderr, "squirrelgnss: PRN%02d does not fit the navigation message\n", ephs[i].prn);
            fclose(f);
            return 1;
        }
        sqrx_lnav_decode((const SqrxSubframe *)sf, week, &dec[i]); /* the sky as broadcast */
        dec[i].prn = ephs[i].prn;
        for (int k = 0; k < 3; k++) {
            len = sqrx_ubx_sfrbx(buf, sizeof buf, ephs[i].prn, sf[k]);
            fwrite(buf, 1, len, f);
        }
    }
    SqgEcef rxe;
    sqg_geodetic_to_ecef(truth, &rxe);
    for (int k = 0; k < n; k++) {
        double t = t0 + k;
        SqgDemoOptions o = {30.0 + 0.1 * k, 5.0, noise, 101u + (unsigned)k, 5, dual, iono, l5};
        SqgEpoch ep;
        sqg_demo_epoch_opt(dec, n_eph, truth, t, &o, &ep);
        int y, mo, d, h, mi;
        double sec;
        sqr_calendar(week, t - leap, &y, &mo, &d, &h, &mi, &sec);
        SqrxNavPvt v = {t, y, mo, d, h, mi, sec, 3, ep.n_obs, truth, 1.0, 1.5, 0, 0, 1.4};
        len = sqrx_ubx_navpvt(buf, sizeof buf, &v);
        fwrite(buf, 1, len, f);
        if (nmea) {
            double utc = fmod(t - leap, 86400.0);
            char s[2048];
            len = sqrx_nmea_gga(s, sizeof s, utc, truth, 1, ep.n_obs, 0.9);
            fwrite(s, 1, len, f);
            len = sqrx_nmea_rmc(s, sizeof s, utc, y, mo, d, truth, 0, 0);
            fwrite(s, 1, len, f);
            SqrxSatView sv[SQG_MAX_SATS];
            for (int i = 0; i < ep.n_obs; i++) {
                const SqgEphemeris *e = NULL;
                for (int j = 0; j < n_eph; j++)
                    if (dec[j].prn == ep.obs[i].prn) e = &dec[j];
                SqgAzEl ae = sqg_az_el(rxe, truth, sqg_sat_position(e, t - 0.07));
                sv[i] = (SqrxSatView){'P', ep.obs[i].prn, ae.el_deg, ae.az_deg, ep.obs[i].snr_dbhz};
            }
            len = sqrx_nmea_gsv(s, sizeof s, sv, ep.n_obs);
            fwrite(s, 1, len, f);
        }
        len = sqrx_ubx_rawx(buf, sizeof buf, &ep, week);
        fwrite(buf, 1, len, f);
        if (junk) {
            /* line noise, and a message with a broken checksum */
            static const unsigned char noise_bytes[] = {0x00, 0xFF, 0xB5, 0x13, '$', 'G', 'P', 0x07};
            fwrite(noise_bytes, 1, sizeof noise_bytes, f);
            len = sqrx_ubx_navpvt(buf, sizeof buf, &v);
            buf[20] ^= 0x5A;
            fwrite(buf, 1, len, f);
        }
    }
    int bad = ferror(f) != 0;
    bad |= fclose(f) != 0;
    if (bad) {
        fprintf(stderr, "squirrelgnss: write failed\n");
        return 1;
    }
    printf("wrote %s (%d epochs, %d satellites)\n", path, n, n_eph);
    return 0;
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
    if (!strcmp(cmd, "receiver")) return receiver(argc, argv);
    if (!strcmp(cmd, "ubx-demo")) return ubx_demo(argc, argv);
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
