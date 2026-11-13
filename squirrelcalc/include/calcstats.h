/* calcstats: SquirrelCalc lists: summary statistics and least-squares fits
 * (linear, quadratic, exponential) with r^2. Plain C11, no allocation. */
#ifndef CALCSTATS_H
#define CALCSTATS_H

#include <stddef.h>

#define CALC_LIST_MAX 100 /* values in one list */

typedef struct {
    int n;
    double sum, mean, sd; /* sd: sample standard deviation (n - 1), 0 for one value */
    double min, median, max;
} CalcStats;

/* 0, or -1 when n < 1 or n > CALC_LIST_MAX or a value is not finite */
int calc_stats(const double *v, int n, CalcStats *out);

enum { CALC_FIT_LINEAR, CALC_FIT_QUAD, CALC_FIT_EXP };

typedef struct {
    int kind;
    /* linear: c[0] + c[1] x; quadratic: c[0] + c[1] x + c[2] x^2;
     * exponential: c[0] e^(c[1] x) */
    double c[3];
    double r2; /* 1 - SS_res / SS_tot, measured on y itself (not ln y) */
    double r;  /* linear only: the correlation coefficient */
    int n;
} CalcFit;

/* Least squares through (xs[i], ys[i]). 0, or -1 with why in msg: too few
 * points (2 for linear and exponential, 3 for quadratic), x all the same,
 * a value not finite, or a y not above 0 for the exponential fit. */
int calc_fit(int kind, const double *xs, const double *ys, int n, CalcFit *out, const char **msg);

double calc_fit_eval(const CalcFit *f, double x);

/* "y = 2.03 x + 0.95" with 6 significant digits */
void calc_fit_text(const CalcFit *f, char *out, size_t cap);

#endif
