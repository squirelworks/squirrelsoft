/* squirrelgnss: command line over sqgnss. Original code. */
#include "sqgnss.h"

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
          "  squirrelgnss help\n"
          "demo-track solves a synthetic 30-satellite sky; --fault adds a bias to one\n"
          "satellite, which the fault detection should exclude.\n",
          f);
}

static const SqgGeodetic HOME = {35.681236, 139.767125, 40.0};

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
    usage(stderr);
    return 2;
}
