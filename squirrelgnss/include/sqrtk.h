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
 * The ambiguity fix here is rounding checked by a ratio test: each
 * ambiguity moved one cycle toward its float value must fit at least
 * `ratio_min` times worse. It is not LAMBDA: correlated ambiguities from a
 * short session can fail the test, and then the float solution stands. */
#ifndef SQRTK_H
#define SQRTK_H

#include "sqgnss.h"

typedef struct {
    double mask_deg;   /* satellites below this at the base are left out */
    double sigma_code; /* m, for weighting */
    double sigma_phase; /* m */
    double ratio_min;  /* to accept a fix */
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
} SqgRtk;

/* 15 degrees, 0.5 m code, 3 mm phase, ratio 3 */
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

#endif
