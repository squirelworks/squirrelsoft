#include "sqgnss.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_llh(const char *s, SqgGeodetic *g) {
    return sscanf(s, "%lf,%lf,%lf", &g->lat_deg, &g->lon_deg, &g->height_m) == 3;
}

static void usage(void) {
    puts("SquirrelGNSS — continuous GPS / land-survey positioning");
    puts("  squirrelgnss demo-track [epochs]");
    puts("  squirrelgnss stakeout lat,lon,h lat,lon,h");
    puts("  squirrelgnss llh2ecef lat,lon,h");
    puts("  squirrelgnss ecef2llh x,y,z");
    puts("  squirrelgnss help");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "help") == 0) {
        usage();
        return 0;
    }
    if (strcmp(cmd, "llh2ecef") == 0) {
        SqgGeodetic g;
        SqgEcef e;
        if (argc < 3 || !parse_llh(argv[2], &g)) return 1;
        sqg_geodetic_to_ecef(g, &e);
        printf("%.6f %.6f %.6f\n", e.x, e.y, e.z);
        return 0;
    }
    if (strcmp(cmd, "ecef2llh") == 0) {
        SqgEcef e;
        SqgGeodetic g;
        if (argc < 3 || sscanf(argv[2], "%lf,%lf,%lf", &e.x, &e.y, &e.z) != 3) return 1;
        sqg_ecef_to_geodetic(e, &g);
        printf("%.10f %.10f %.4f\n", g.lat_deg, g.lon_deg, g.height_m);
        return 0;
    }
    if (strcmp(cmd, "stakeout") == 0) {
        SqgGeodetic a, b;
        if (argc < 4 || !parse_llh(argv[2], &a) || !parse_llh(argv[3], &b)) return 1;
        SqgEnu d = sqg_stakeout_delta(a, b);
        printf("delta ENU: E=%.3f N=%.3f U=%.3f  horiz=%.3f\n", d.e, d.n, d.u,
               sqrt(d.e * d.e + d.n * d.n));
        return 0;
    }
    if (strcmp(cmd, "demo-track") == 0) {
        int n = argc >= 3 ? atoi(argv[2]) : 5;
        if (n < 1) n = 1;
        if (n > 100) n = 100;
        SqgGeodetic truth = {35.681236, 139.767125, 40.0}; /* Tokyo-ish */
        SqgEcef truth_ecef;
        sqg_geodetic_to_ecef(truth, &truth_ecef);
        SqgEphemeris ephs[8];
        int n_eph = 0;
        SqgTrack track;
        sqg_track_init(&track);
        SqgEcef approx = truth_ecef;
        approx.x += 50.0;
        approx.y -= 30.0;
        for (int k = 0; k < n; k++) {
            double t0 = 100000.0 + k * 1.0;
            SqgEpoch epoch;
            sqg_demo_constellation(ephs, &n_eph, truth, t0, &epoch);
            /* slight PR noise for later epochs */
            for (int i = 0; i < epoch.n_obs; i++)
                epoch.obs[i].pseudorange_m += 0.2 * ((i + k) % 3 - 1);
            sqg_track_push(&track, &epoch, ephs, n_eph, approx);
            const SqgPvt *p = &track.solutions[track.n_epochs - 1];
            double err = sqg_baseline_length(p->pos, truth_ecef);
            printf("epoch %d t=%.1f used=%d conv=%d pdop=%.2f err=%.3f m  cb=%.3f m\n",
                   k, epoch.t_receiver, p->n_used, p->converged, p->pdop, err,
                   p->clock_bias_m);
        }
        sqg_write_sqobs(&track, "/tmp/demo.sqobs");
        puts("wrote /tmp/demo.sqobs");
        return 0;
    }
    usage();
    return 1;
}
