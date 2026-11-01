#ifndef SQGNSS_H
#define SQGNSS_H

#include <stddef.h>

#define SQG_MAX_SATS 32
#define SQG_MAX_EPOCHS 512
#define SQG_C_LIGHT 299792458.0
#define SQG_WGS84_A 6378137.0
#define SQG_WGS84_F (1.0 / 298.257223563)
#define SQG_MU 3.986005e14
#define SQG_OMEGA_E 7.2921151467e-5

typedef struct {
    double x, y, z;
} SqgEcef;

typedef struct {
    double lat_deg;
    double lon_deg;
    double height_m;
} SqgGeodetic;

typedef struct {
    double e, n, u;
} SqgEnu;

typedef struct {
    int prn;
    double sqrt_a;   /* sqrt(semi-major) m^0.5 */
    double e;        /* eccentricity */
    double i0;       /* inclination rad */
    double omega0;   /* right ascension rad */
    double w;        /* argument of perigee rad */
    double m0;       /* mean anomaly at toe rad */
    double toe;      /* ephemeris reference time s */
    double delta_n;  /* mean motion difference */
    double cuc, cus, crc, crs, cic, cis;
    double omega_dot;
    double idot;
} SqgEphemeris;

typedef struct {
    int prn;
    double pseudorange_m;
    double snr_dbhz;
    int valid;
} SqgObservation;

typedef struct {
    double t_receiver;
    SqgObservation obs[SQG_MAX_SATS];
    int n_obs;
} SqgEpoch;

typedef struct {
    SqgEcef pos;
    double clock_bias_m;
    double pdop;
    double gdop;
    int n_used;
    int converged;
} SqgPvt;

typedef struct {
    SqgEpoch epochs[SQG_MAX_EPOCHS];
    int n_epochs;
    SqgPvt solutions[SQG_MAX_EPOCHS];
} SqgTrack;

void sqg_geodetic_to_ecef(SqgGeodetic g, SqgEcef *out);
void sqg_ecef_to_geodetic(SqgEcef p, SqgGeodetic *out);
void sqg_ecef_to_enu(SqgEcef origin, SqgEcef point, SqgGeodetic origin_llh, SqgEnu *enu);
SqgEcef sqg_sat_position(const SqgEphemeris *eph, double t_gps);
int sqg_solve_pvt(const SqgEpoch *epoch, const SqgEphemeris *ephs, int n_eph,
                  SqgEcef approx, SqgPvt *out);
void sqg_track_init(SqgTrack *tr);
int sqg_track_push(SqgTrack *tr, const SqgEpoch *epoch, const SqgEphemeris *ephs,
                   int n_eph, SqgEcef approx);
SqgEnu sqg_stakeout_delta(SqgGeodetic from, SqgGeodetic to);
double sqg_baseline_length(SqgEcef a, SqgEcef b);
int sqg_write_sqobs(const SqgTrack *tr, const char *path);
void sqg_demo_constellation(SqgEphemeris *ephs, int *n_eph, SqgGeodetic truth,
                            double t0, SqgEpoch *epoch_out);

#endif
