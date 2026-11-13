/*
 * sqgnss: GNSS positioning core for SquirrelGNSS (original code).
 *
 * WGS84 geodesy (ECEF, geodetic, local ENU, azimuth and elevation), GPS-style
 * broadcast orbits and satellite clocks, and pseudorange point positioning
 * by weighted least squares with an elevation mask, dilution of precision
 * and fault detection and exclusion. Also an epoch track, survey helpers, a
 * .sqobs log, and a synthetic sky for tests and demos. C11, no allocation.
 */
#ifndef SQGNSS_H
#define SQGNSS_H

#include <stddef.h>

#define SQG_MAX_SATS 32
#define SQG_MAX_EPOCHS 512
#define SQG_C_LIGHT 299792458.0
#define SQG_WGS84_A 6378137.0
#define SQG_WGS84_F (1.0 / 298.257223563)
#define SQG_MU 3.986005e14           /* GPS value of GM, m^3/s^2 */
#define SQG_OMEGA_E 7.2921151467e-5  /* Earth rotation, rad/s */
#define SQG_F_REL (-4.442807633e-10) /* -2 sqrt(mu) / c^2, s/m^0.5 */
#define SQG_WEEK 604800.0
#define SQG_DEMO_SATS 30 /* satellites in the synthetic sky */
#define SQG_L1_HZ 1575.42e6
#define SQG_L1_LAMBDA (SQG_C_LIGHT / SQG_L1_HZ) /* about 0.19 m */

typedef struct {
    double x, y, z;
} SqgEcef;

typedef struct {
    double lat_deg;
    double lon_deg;
    double height_m; /* above the ellipsoid */
} SqgGeodetic;

typedef struct {
    double e, n, u;
} SqgEnu;

typedef struct {
    double az_deg; /* 0 north, 90 east */
    double el_deg; /* 90 overhead, below 0 under the horizon */
} SqgAzEl;

/* A GPS-style broadcast ephemeris (times in GPS seconds of week). */
typedef struct {
    int prn;
    double sqrt_a;   /* sqrt(semi-major axis), m^0.5 */
    double e;        /* eccentricity */
    double i0;       /* inclination at toe, rad */
    double omega0;   /* longitude of the ascending node at the week start, rad */
    double w;        /* argument of perigee, rad */
    double m0;       /* mean anomaly at toe, rad */
    double toe;      /* ephemeris reference time, s */
    double delta_n;  /* mean motion correction, rad/s */
    double cuc, cus, crc, crs, cic, cis; /* harmonic corrections */
    double omega_dot; /* rate of the node, rad/s */
    double idot;      /* rate of inclination, rad/s */
    /* satellite clock: af0 + af1 (t - toc) + af2 (t - toc)^2, seconds */
    double toc, af0, af1, af2;
    double tgd; /* group delay, s */
    int week;   /* GPS week of toe (0 when not known) */
    int health; /* 0 is healthy */
} SqgEphemeris;

typedef struct {
    int prn;
    double pseudorange_m;
    double snr_dbhz;
    int valid;
    double carrier_cyc; /* L1 carrier phase, cycles, when has_carrier */
    int has_carrier;
} SqgObservation;

typedef struct {
    double t_receiver; /* receiver clock reading, GPS seconds of week */
    SqgObservation obs[SQG_MAX_SATS];
    int n_obs;
} SqgEpoch;

typedef struct {
    SqgEcef pos;
    double clock_bias_m; /* receiver clock offset times c */
    double gdop, pdop, hdop, vdop, tdop;
    int n_used;
    int used_prn[SQG_MAX_SATS];
    double residual_m[SQG_MAX_SATS]; /* measured minus modelled, for used_prn */
    double rms_m;                    /* unweighted residual RMS */
    int excluded_prn;                /* dropped as faulty, or 0 */
    int converged;
} SqgPvt;

typedef struct {
    double elev_mask_deg; /* ignore satellites lower than this */
    int weighted;         /* weight by sin^2(elevation) */
    int raim;             /* detect one faulty satellite and exclude it */
    double raim_limit_m;  /* residual RMS above this means a fault */
} SqgPvtOptions;

typedef struct {
    SqgEpoch epochs[SQG_MAX_EPOCHS];
    int n_epochs;
    SqgPvt solutions[SQG_MAX_EPOCHS];
} SqgTrack;

/* ---- geodesy ---- */
void sqg_geodetic_to_ecef(SqgGeodetic g, SqgEcef *out);
/* Exact to well under a millimetre anywhere from the centre of the Earth
 * out past the satellites, poles included. */
void sqg_ecef_to_geodetic(SqgEcef p, SqgGeodetic *out);
void sqg_ecef_to_enu(SqgEcef origin, SqgEcef point, SqgGeodetic origin_llh, SqgEnu *enu);
void sqg_enu_to_ecef(SqgEcef origin, SqgGeodetic origin_llh, SqgEnu enu, SqgEcef *out);
SqgAzEl sqg_az_el(SqgEcef rx, SqgGeodetic rx_llh, SqgEcef sat);

/* ---- orbits ---- */
/* Satellite position in ECEF at GPS time t (the frame of time t). */
SqgEcef sqg_sat_position(const SqgEphemeris *eph, double t_gps);
/* Satellite clock offset at t, seconds: polynomial + relativistic - tgd. */
double sqg_sat_clock(const SqgEphemeris *eph, double t_gps);

/* ---- positioning ---- */
/* 10 degree mask, weighted, fault exclusion on at 15 m. */
void sqg_pvt_options_default(SqgPvtOptions *o);
/* Solve one epoch from approx (any point, even the centre of the Earth).
 * opt may be NULL for the defaults. Returns 0 when it converged. */
int sqg_solve_pvt_opt(const SqgEpoch *epoch, const SqgEphemeris *ephs, int n_eph, SqgEcef approx,
                      const SqgPvtOptions *opt, SqgPvt *out);
int sqg_solve_pvt(const SqgEpoch *epoch, const SqgEphemeris *ephs, int n_eph, SqgEcef approx, SqgPvt *out);

void sqg_track_init(SqgTrack *tr);
int sqg_track_push(SqgTrack *tr, const SqgEpoch *epoch, const SqgEphemeris *ephs, int n_eph, SqgEcef approx);

/* ---- survey ---- */
SqgEnu sqg_stakeout_delta(SqgGeodetic from, SqgGeodetic to);
double sqg_baseline_length(SqgEcef a, SqgEcef b);
int sqg_write_sqobs(const SqgTrack *tr, const char *path);

/* ---- synthetic sky ---- */
/* SQG_DEMO_SATS satellites in 6 planes of 5 (a Walker-like layout), with
 * clock offsets; ephs needs room for SQG_DEMO_SATS. */
void sqg_demo_ephemerides(SqgEphemeris *ephs, int *n_eph, double toe);
/* The whole-cycle carrier ambiguity sqg_demo_epoch gives a receiver at rx
 * for a satellite: it differs between sites and between satellites. */
long sqg_demo_ambiguity(SqgEcef rx, int prn);
/* The pseudoranges a receiver at truth with clock_bias_m would measure at
 * true time t_true from every satellite above mask_deg, plus Gaussian noise
 * of noise_m (seeded, repeatable). Also the L1 carrier phase: range over
 * the wavelength plus a whole-cycle ambiguity fixed by the PRN and the
 * site, with noise of noise_m / 100. */
void sqg_demo_epoch(const SqgEphemeris *ephs, int n_eph, SqgGeodetic truth, double t_true, double clock_bias_m,
                    double mask_deg, double noise_m, unsigned seed, SqgEpoch *out);

/* ---- epoch stream and survey ---- */
typedef struct {
    SqgPvtOptions opt;
    int n_epochs, n_fixed;
    SqgPvt pvt;          /* the latest solution */
    int have_fix;        /* pvt is a fix */
    double t_fix;        /* its receiver time */
    /* speed and course from the last two fixes */
    double speed_mps, course_deg; /* course: 0 north, 90 east */
    int have_velocity;
    /* occupation: the mean and spread of the fixes since the last reset, in
     * east/north/up about the first of them */
    SqgEcef origin;
    SqgGeodetic origin_llh;
    int n_avg;
    double mean[3], m2[3];
} SqgStream;

/* opt may be NULL for the defaults */
void sqg_stream_init(SqgStream *s, const SqgPvtOptions *opt);
/* Solve the next epoch from the last fix (or the centre of the Earth) and
 * add a fix to the average. Returns 0 for a fix, -1 for none. */
int sqg_stream_push(SqgStream *s, const SqgEpoch *epoch, const SqgEphemeris *ephs, int n_eph);
void sqg_stream_reset_average(SqgStream *s);
/* The mean position of the occupation and the standard deviation of one
 * fix in east, north and up (0 with fewer than 2). Returns the fixes
 * averaged. */
int sqg_stream_average(const SqgStream *s, SqgGeodetic *mean, SqgEnu *sigma);

typedef struct {
    SqgEnu delta;       /* target minus here */
    double horiz_m;     /* horizontal distance */
    double bearing_deg; /* 0 north, 90 east */
    int on_point;       /* within tol_m horizontally */
} SqgGuide;

SqgGuide sqg_stakeout_guide(SqgGeodetic here, SqgGeodetic target, double tol_m);
/* "go 1.230 m N, 0.450 m W (1.310 m at 339.9 deg); fill 0.100 m", or
 * "on point (0.004 m); cut 0.020 m": cut when the target is below here */
void sqg_guide_text(const SqgGuide *g, char *out, size_t cap);

#endif
