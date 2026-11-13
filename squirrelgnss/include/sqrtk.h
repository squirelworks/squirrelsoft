/* sqrtk: differential GNSS math for SquirrelGNSS (original code).
 *
 * Base and rover see the same satellites. Differencing between the two
 * receivers removes the satellite clocks (and most of the orbit error), and
 * differencing that between two satellites removes the receiver clocks: the
 * double difference. Code double differences give a rover position good to
 * the code noise (DGNSS). Carrier double differences over a static session,
 * with one whole-cycle ambiguity per satellite pair, give it to millimetres
 * once the ambiguities are fixed.
 *
 * Ambiguities are fixed by integer least squares (LAMBDA-style: integer
 * Gauss decorrelation, then a search that shrinks its ellipsoid), accepted
 * when the second-best set fits at least `ratio_min` times worse.
 *
 * With L2 the Melbourne-Wubbena combination gives each double difference's
 * wide-lane ambiguity (about 86 cm, free of geometry and ionosphere); once
 * those are whole numbers the L2 carrier joins the L1 one on the same
 * ambiguities, which fixes in far fewer epochs.
 *
 * A moving rover goes through SqgRtkFilter: each epoch's rover position is
 * eliminated into an information matrix of the ambiguities alone, so they
 * gather strength while the rover moves, and once fixed every epoch is
 * placed to the centimetre. A slip (flagged by the receiver, or a jump in
 * the geometry-free carrier) restarts only that ambiguity. */
#ifndef SQRTK_H
#define SQRTK_H

#include "sqgnss.h"

typedef struct {
    double mask_deg;   /* satellites below this at the base are left out */
    double sigma_code; /* m, for weighting */
    double sigma_phase; /* m */
    double ratio_min;  /* to accept a fix */
    int dual;          /* use L2 when it is there */
} SqgRtkOptions;

typedef struct {
    int n_epochs;               /* epochs used */
    int ref_prn;                /* the reference satellite of the differences */
    int n_dd;                   /* double differences per epoch */
    int prn[SQG_MAX_SATS];      /* the other satellite of each */
    double float_amb[SQG_MAX_SATS]; /* cycles */
    long amb[SQG_MAX_SATS];     /* the fixed whole cycles, when fixed */
    int fixed;
    double ratio;               /* how much worse the next-best set fits */
    double rms_phase_m;         /* phase residual RMS of the solution given */
    SqgEcef float_rover;
    SqgEcef rover;              /* fixed when fixed, else float */
    int dual;                   /* L2 used: every wide lane fixed */
    int wl_fixed;
    long wl[SQG_MAX_SATS];      /* wide-lane ambiguities (L1 minus L2 cycles) */
} SqgRtk;

/* 15 degrees, 0.5 m code, 3 mm phase, ratio 3, L2 when there */
void sqg_rtk_options_default(SqgRtkOptions *o);

/* The rover from code double differences in one epoch, with the base at a
 * known place. Returns 0, or -1 with fewer than 4 double differences. */
int sqg_dgnss(const SqgEpoch *base, const SqgEpoch *rover, const SqgEphemeris *ephs, int n_eph, SqgEcef base_pos,
              const SqgRtkOptions *opt, SqgEcef *out, int *n_dd);

/* A static rover from n epochs (base[k] and rover[k] at the same time) of
 * code and carrier double differences on the satellites seen with carrier
 * in every epoch: the float solution, then the fixed one when the ratio
 * test passes. opt may be NULL. Returns 0, or -1 with fewer than 4
 * double differences or a failed solution. */
int sqg_rtk_static(const SqgEpoch *base, const SqgEpoch *rover, int n_epochs, const SqgEphemeris *ephs, int n_eph,
                   SqgEcef base_pos, const SqgRtkOptions *opt, SqgRtk *out);

/* Integer least squares: the integer vector nearest the float ambiguities a
 * (n of them, up to SQG_MAX_SATS) in the metric of their covariance q (n x n,
 * row by row). ratio gets the second-best squared distance over the best;
 * dist (may be NULL) the two distances. Returns 0, or -1 when q is not
 * positive definite or the search gives up. */
int sqg_ils(const double *a, const double *q, int n, long *best, double *ratio, double *dist);

/* ---- kinematic ---- */
typedef struct {
    double t;          /* rover receiver time */
    SqgEcef pos;
    int fixed;         /* placed with fixed ambiguities */
    double ratio;      /* of the last integer search */
    double rms_phase_m;
    int n_dd;          /* double differences this epoch */
    int wide_lanes;    /* of them with the wide lane fixed (L2 in use) */
    int slips;         /* ambiguities restarted this epoch */
    int reset;         /* the satellite set started over */
    int dropped;       /* a fix stopped fitting and was let go */
} SqgRtkFix;

typedef struct {
    SqgRtkOptions opt;
    SqgEcef base_pos;
    int started, epochs;
    int n_sat, prn[SQG_MAX_SATS]; /* prn[0] is the reference */
    double info[SQG_MAX_SATS][SQG_MAX_SATS], rhs[SQG_MAX_SATS];
    double mw_ref[SQG_MAX_SATS], mw_sum[SQG_MAX_SATS], mw_sq[SQG_MAX_SATS];
    int mw_n[SQG_MAX_SATS], wl_ok[SQG_MAX_SATS];
    long wl[SQG_MAX_SATS];
    double gf_last[SQG_MAX_SATS];
    int gf_have[SQG_MAX_SATS];
    int fixed;
    long fixed_amb[SQG_MAX_SATS];
    double ratio;
    SqgEcef last;
} SqgRtkFilter;

void sqg_rtk_filter_init(SqgRtkFilter *f, SqgEcef base_pos, const SqgRtkOptions *opt);
/* One epoch of base and rover at the same time. Returns 0 with out filled,
 * or -1 when there is no solution this epoch (too few satellites). */
int sqg_rtk_filter_push(SqgRtkFilter *f, const SqgEpoch *base, const SqgEpoch *rover, const SqgEphemeris *ephs,
                        int n_eph, SqgRtkFix *out);

#endif
