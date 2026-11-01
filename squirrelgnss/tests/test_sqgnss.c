#include "sqgnss.h"

#include <math.h>
#include <stdio.h>

static int failures;
static void expect(int c, const char *m) {
    if (!c) {
        fprintf(stderr, "FAIL: %s\n", m);
        failures++;
    }
}

int main(void) {
    SqgGeodetic g = {35.0, 139.0, 100.0};
    SqgEcef e;
    sqg_geodetic_to_ecef(g, &e);
    SqgGeodetic g2;
    sqg_ecef_to_geodetic(e, &g2);
    expect(fabs(g2.lat_deg - g.lat_deg) < 1e-6, "lat roundtrip");
    expect(fabs(g2.lon_deg - g.lon_deg) < 1e-6, "lon roundtrip");
    expect(fabs(g2.height_m - g.height_m) < 1e-2, "height roundtrip");

    SqgEphemeris ephs[8];
    int n_eph = 0;
    SqgEpoch epoch;
    sqg_demo_constellation(ephs, &n_eph, g, 200000.0, &epoch);
    expect(n_eph == 6 && epoch.n_obs == 6, "demo constellation");

    SqgEcef approx;
    sqg_geodetic_to_ecef(g, &approx);
    approx.x += 100.0;
    SqgPvt pvt;
    int rc = sqg_solve_pvt(&epoch, ephs, n_eph, approx, &pvt);
    expect(rc == 0 && pvt.converged, "pvt converge");
    SqgEcef truth;
    sqg_geodetic_to_ecef(g, &truth);
    double err = sqg_baseline_length(pvt.pos, truth);
    expect(err < 5.0, "pvt error < 5 m on noiseless demo");
    expect(fabs(pvt.clock_bias_m - 15.0) < 5.0, "clock bias near 15 m");

    SqgGeodetic a = {35.0, 139.0, 50}, b = {35.001, 139.001, 51};
    SqgEnu d = sqg_stakeout_delta(a, b);
    expect(fabs(d.e) > 10.0 && fabs(d.n) > 10.0, "stakeout EN deltas");

    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    puts("squirrelgnss tests ok");
    return 0;
}
