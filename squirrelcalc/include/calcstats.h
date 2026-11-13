/* calcstats: SquirrelCalc lists: summary statistics, quartiles, histograms,
 * least-squares fits (linear, quadratic, exponential) with r^2, and the
 * normal, binomial and Poisson distributions. Plain C11, no allocation. */
#ifndef CALCSTATS_H
#define CALCSTATS_H

#include <stddef.h>

#define CALC_LIST_MAX 100 /* values in one list */

typedef struct {
    int n;
    double sum, mean, sd; /* sd: sample standard deviation (n - 1), 0 for one value */
    double min, median, max;
    double q1, q3;   /* medians of the lower and upper halves (the middle value of an odd n left out) */
    double var;      /* sample variance (n - 1) */
    double sd_pop;   /* population standard deviation (n) */
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

/* Counts of v in bins [start + k width, start + (k + 1) width), k from 0;
 * the last bin also takes a value equal to its top. width <= 0 picks one:
 * the round width (1, 2 or 5 times a power of ten) nearest to the range
 * over 1 + log2(n) bins (Sturges), from a multiple of it at or below the
 * minimum, written back
 * to *start and *width. Returns the bins used (up to cap, more values are
 * left out), or -1 for no values, a value not finite or cap < 1. */
int calc_histogram(const double *v, int n, double *start, double *width, int *counts, int cap);

/* ---- distributions ---- */

/* The normal density at x (sd > 0; NaN otherwise) */
double calc_normal_pdf(double x, double mu, double sd);
/* P(lo < X < hi) for X normal; lo or hi may be infinite (or +-1e99) */
double calc_normal_cdf(double lo, double hi, double mu, double sd);
/* The x with P(X < x) = p, for 0 < p < 1 (NaN otherwise) */
double calc_inv_norm(double p, double mu, double sd);
/* P(X = k) and P(X <= k) for X binomial with n trials of probability p */
double calc_binom_pdf(double n, double p, double k);
double calc_binom_cdf(double n, double p, double k);
/* P(X = k) and P(X <= k) for X Poisson with mean lambda */
double calc_poisson_pdf(double lambda, double k);
double calc_poisson_cdf(double lambda, double k);
/* n choose k, and the k-permutations of n (whole n >= k >= 0; NaN otherwise) */
double calc_ncr(double n, double k);
double calc_npr(double n, double k);

#endif
