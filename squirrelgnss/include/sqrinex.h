/*
 * sqrinex: RINEX GPS navigation and observation files for SquirrelGNSS
 * (original code, written from the public RINEX 2.11 and 3.x format
 * descriptions). Readers work on a whole file already in memory and never
 * allocate; writers produce RINEX 3.04 for test and demo files.
 */
#ifndef SQRINEX_H
#define SQRINEX_H

#include <stdio.h>

#include "sqgnss.h"

#define SQR_MAX_NAV 512 /* ephemeris records kept from one file */

/* GPS time: whole weeks since 1980-01-06 and seconds into the week. */
void sqr_gps_time(int year, int month, int day, int hour, int min, double sec, int *week, double *sow);
void sqr_calendar(int week, double sow, int *year, int *month, int *day, int *hour, int *min, double *sec);

typedef struct {
    SqgEphemeris eph[SQR_MAX_NAV];
    int n;
    int skipped; /* records of other systems (GLONASS, Galileo, ...) */
} SqrNav;

typedef struct {
    int code; /* 0, or -1 */
    int line; /* 1-based line of the problem */
    char msg[80];
} SqrError;

/* Read a GPS navigation file (RINEX 2.11 "N", or 3.x "N" with any mix of
 * systems; only GPS is kept). Returns the records kept, or -1 with err. */
int sqr_read_nav(const char *text, SqrNav *nav, SqrError *err);

/* For each PRN, the healthy record whose toe is nearest (week, sow) and
 * within 4 hours of it. Returns how many were written to out (up to cap). */
int sqr_nav_select(const SqrNav *nav, int week, double sow, SqgEphemeris *out, int cap);

/* An observation file being read epoch by epoch. */
typedef struct {
    const char *p;   /* next unread byte */
    int line;
    double version;  /* 2.11, 3.04, ... */
    int n_types;     /* GPS observation types */
    int code_index;  /* which one is the pseudorange: C1C, C1, C1W or P1 */
    int snr_index;   /* S1C or S1, or -1 */
    int carrier_index; /* L1C, L1, L1W or L1P, or -1 */
    int code2_index;   /* C2W, C2L, C2X, C2S, P2 or C2, or -1 */
    int carrier2_index; /* L2W, L2L, L2X, L2S or L2, or -1 */
    int code5_index;    /* C5Q, C5X, C5I or C5, or -1 */
    int carrier5_index; /* L5Q, L5X, L5I or L5, or -1 */
    char code_name[4];
    double approx[3]; /* APPROX POSITION XYZ, or 0 */
} SqrObs;

/* Read the header. Returns 0, or -1 with err (not an observation file, no
 * GPS pseudorange type, ...). */
int sqr_obs_open(SqrObs *r, const char *text, SqrError *err);

/* The next epoch with GPS pseudoranges: 1, or 0 at the end, or -1 with err.
 * week receives the GPS week of the epoch. Event records are skipped. L2
 * code and carrier are read when the file has both; L5 code when it has it,
 * with the carrier when there is one. A loss-of-lock flag on any carrier
 * sets the observation's slip. */
int sqr_obs_next(SqrObs *r, SqgEpoch *ep, int *week, SqrError *err);

/* RINEX 3.04 writers. The observation types are C1C L1C S1C, then C2W L2W
 * with SQR_BANDS_L2 and C5Q L5Q with SQR_BANDS_L5; give the header and the
 * epochs the same bands. The plain ones write L2. */
enum { SQR_BANDS_L2 = 1, SQR_BANDS_L5 = 2 };
void sqr_write_nav(FILE *f, const SqgEphemeris *ephs, int n);
void sqr_write_obs_header(FILE *f, const SqgEcef *approx);
void sqr_write_obs_epoch(FILE *f, int week, const SqgEpoch *ep);
void sqr_write_obs_header_bands(FILE *f, const SqgEcef *approx, int bands);
void sqr_write_obs_epoch_bands(FILE *f, int week, const SqgEpoch *ep, int bands);

#endif
