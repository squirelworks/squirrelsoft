/* calcstats: lists and least-squares fits (see calcstats.h). Original code. */
#include "calcstats.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sqmath.h"

#define SQRT_2PI 2.5066282746310002 /* sqrt(2 pi) */

static int all_finite(const double *v, int n) {
    for (int i = 0; i < n; i++)
        if (!isfinite(v[i])) return 0;
    return 1;
}

int calc_stats(const double *v, int n, CalcStats *out) {
    if (n < 1 || n > CALC_LIST_MAX || !all_finite(v, n)) return -1;
    double s[CALC_LIST_MAX];
    memcpy(s, v, (size_t)n * sizeof *s);
    for (int i = 1; i < n; i++) { /* insertion sort, for the median */
        double t = s[i];
        int j = i;
        for (; j > 0 && s[j - 1] > t; j--) s[j] = s[j - 1];
        s[j] = t;
    }
    double sum = 0;
    for (int i = 0; i < n; i++) sum += v[i];
    double mean = sum / n, ss = 0;
    for (int i = 0; i < n; i++) ss += (v[i] - mean) * (v[i] - mean);
    out->n = n;
    out->sum = sum;
    out->mean = mean;
    out->sd = n > 1 ? sqrt(ss / (n - 1)) : 0;
    out->min = s[0];
    out->max = s[n - 1];
    out->median = n % 2 ? s[n / 2] : (s[n / 2 - 1] + s[n / 2]) / 2;
    out->var = n > 1 ? ss / (n - 1) : 0;
    out->sd_pop = sqrt(ss / n);
    /* quartiles: medians of the halves below and above the median */
    int h = n / 2;
    if (n < 2) {
        out->q1 = out->q3 = s[0];
    } else {
        const double *hi = s + n - h;
        out->q1 = h % 2 ? s[h / 2] : (s[h / 2 - 1] + s[h / 2]) / 2;
        out->q3 = h % 2 ? hi[h / 2] : (hi[h / 2 - 1] + hi[h / 2]) / 2;
    }
    return 0;
}

/* the nearest of 1, 2 or 5 times a power of ten to x, measured by ratio */
static double nice_width(double x) {
    double p = pow(10, floor(log10(x))), best = p;
    const double m[] = {1, 2, 5, 10};
    for (int i = 1; i < 4; i++)
        if (fabs(log(m[i] * p / x)) < fabs(log(best / x))) best = m[i] * p;
    return best;
}

int calc_histogram(const double *v, int n, double *start, double *width, int *counts, int cap) {
    if (n < 1 || cap < 1 || !all_finite(v, n) || !start || !width) return -1;
    double lo = v[0], hi = v[0];
    for (int i = 1; i < n; i++) {
        if (v[i] < lo) lo = v[i];
        if (v[i] > hi) hi = v[i];
    }
    if (!(*width > 0) || !isfinite(*width) || !isfinite(*start)) {
        int want = 1 + (int)ceil(log2((double)n));
        double span = hi - lo;
        *width = span > 0 ? nice_width(span / want) : 1;
        *start = floor(lo / *width) * *width;
    }
    double top = (hi - *start) / *width;
    int bins = (int)floor(top) + 1;
    if (bins < 1) bins = 1;
    if (top == floor(top) && top >= 1) bins--; /* the maximum on a bin edge closes the bin below */
    if (bins > cap) bins = cap;
    for (int k = 0; k < bins; k++) counts[k] = 0;
    for (int i = 0; i < n; i++) {
        double t = (v[i] - *start) / *width;
        if (t < 0) continue;
        int k = (int)floor(t);
        if (k == bins && t == (double)bins) k = bins - 1;
        if (k < bins) counts[k]++;
    }
    return bins;
}

/* ---- distributions ---- */

double calc_normal_pdf(double x, double mu, double sd) {
    if (!(sd > 0)) return NAN;
    double z = (x - mu) / sd;
    return exp(-0.5 * z * z) / (sd * SQRT_2PI);
}

/* P(Z < z), accurate in both tails */
static double phi(double z) { return 0.5 * erfc(-z / sqrt(2.0)); }

double calc_normal_cdf(double lo, double hi, double mu, double sd) {
    if (!(sd > 0) || isnan(lo) || isnan(hi) || isnan(mu)) return NAN;
    if (lo >= 1e99) lo = INFINITY;
    if (lo <= -1e99) lo = -INFINITY;
    if (hi >= 1e99) hi = INFINITY;
    if (hi <= -1e99) hi = -INFINITY;
    double a = (lo - mu) / sd, b = (hi - mu) / sd;
    if (a > 0) return phi(-a) - phi(-b); /* the upper tail, without cancelling */
    return phi(b) - phi(a);
}

double calc_inv_norm(double p, double mu, double sd) {
    if (!(p > 0 && p < 1) || !(sd > 0)) return NAN;
    /* solve phi(z) = p on the side of the smaller tail, by bisection then Newton */
    double q = p < 0.5 ? p : 1 - p, lo = -40, hi = 0;
    for (int i = 0; i < 80; i++) {
        double m = (lo + hi) / 2;
        if (phi(m) < q) lo = m;
        else hi = m;
    }
    double z = (lo + hi) / 2;
    for (int i = 0; i < 3; i++) {
        double d = exp(-0.5 * z * z) / SQRT_2PI;
        if (!(d > 0)) break;
        z -= (phi(z) - q) / d;
    }
    return mu + sd * (p < 0.5 ? z : -z);
}

static int whole(double x) { return isfinite(x) && x == floor(x); }

double calc_ncr(double n, double k) {
    if (!whole(n) || !whole(k) || k < 0 || n < k) return NAN;
    if (k > n - k) k = n - k;
    double r = 1;
    for (double i = 1; i <= k; i++) r = r * (n - k + i) / i;
    return r < 1e15 ? round(r) : r; /* the divisions leave a last-bit error */
}

double calc_npr(double n, double k) {
    if (!whole(n) || !whole(k) || k < 0 || n < k) return NAN;
    double r = 1;
    for (double i = 0; i < k; i++) r *= n - i;
    return r;
}

double calc_binom_pdf(double n, double p, double k) {
    if (!whole(n) || n < 0 || !(p >= 0 && p <= 1)) return NAN;
    if (!whole(k) || k < 0 || k > n) return 0;
    if (p == 0) return k == 0;
    if (p == 1) return k == n;
    return exp(lgamma(n + 1) - lgamma(k + 1) - lgamma(n - k + 1) + k * log(p) + (n - k) * log1p(-p));
}

double calc_binom_cdf(double n, double p, double k) {
    if (!whole(n) || n < 0 || !(p >= 0 && p <= 1) || isnan(k)) return NAN;
    k = floor(k);
    if (k < 0) return 0;
    if (k >= n) return 1;
    double s = 0;
    for (double i = 0; i <= k; i++) s += calc_binom_pdf(n, p, i);
    return s > 1 ? 1 : s;
}

double calc_poisson_pdf(double lambda, double k) {
    if (!(lambda > 0) || !isfinite(lambda)) return NAN;
    if (!whole(k) || k < 0) return 0;
    return exp(k * log(lambda) - lambda - lgamma(k + 1));
}

double calc_poisson_cdf(double lambda, double k) {
    if (!(lambda > 0) || !isfinite(lambda) || isnan(k)) return NAN;
    k = floor(k);
    if (k < 0) return 0;
    double s = 0;
    for (double i = 0; i <= k && i < 1e7; i++) {
        double t = calc_poisson_pdf(lambda, i);
        s += t;
        if (i > lambda && t < 1e-17 * s) break;
    }
    return s > 1 ? 1 : s;
}

double calc_fit_eval(const CalcFit *f, double x) {
    switch (f->kind) {
    case CALC_FIT_LINEAR: return f->c[0] + f->c[1] * x;
    case CALC_FIT_QUAD: return f->c[0] + x * (f->c[1] + x * f->c[2]);
    default: return f->c[0] * exp(f->c[1] * x);
    }
}

/* y = c0 + c1 x by centred sums; -1 when the x are all the same */
static int line_fit(const double *xs, const double *ys, int n, double *c0, double *c1, double *r) {
    double mx = 0, my = 0;
    for (int i = 0; i < n; i++) {
        mx += xs[i];
        my += ys[i];
    }
    mx /= n;
    my /= n;
    double sxx = 0, sxy = 0, syy = 0;
    for (int i = 0; i < n; i++) {
        sxx += (xs[i] - mx) * (xs[i] - mx);
        sxy += (xs[i] - mx) * (ys[i] - my);
        syy += (ys[i] - my) * (ys[i] - my);
    }
    if (!(sxx > 0)) return -1;
    *c1 = sxy / sxx;
    *c0 = my - *c1 * mx;
    *r = syy > 0 ? sxy / sqrt(sxx * syy) : 1;
    return 0;
}

/* y = p0 + p1 u + p2 u^2 with u = (x - mx) / sx, by the normal equations
 * and Gaussian elimination with partial pivoting; then back to x */
static int quad_fit(const double *xs, const double *ys, int n, double c[3]) {
    double mx = 0;
    for (int i = 0; i < n; i++) mx += xs[i];
    mx /= n;
    double sx = 0;
    for (int i = 0; i < n; i++) sx += (xs[i] - mx) * (xs[i] - mx);
    sx = sqrt(sx / n);
    if (!(sx > 0)) return -1;
    double a[3][4] = {{0}};
    for (int i = 0; i < n; i++) {
        double u = (xs[i] - mx) / sx, pw[5] = {1, u, u * u, u * u * u, u * u * u * u};
        for (int r = 0; r < 3; r++) {
            for (int k = 0; k < 3; k++) a[r][k] += pw[r + k];
            a[r][3] += pw[r] * ys[i];
        }
    }
    for (int col = 0; col < 3; col++) {
        int piv = col;
        for (int r = col + 1; r < 3; r++)
            if (fabs(a[r][col]) > fabs(a[piv][col])) piv = r;
        if (fabs(a[piv][col]) < 1e-12 * n) return -1; /* fewer than 3 distinct x */
        if (piv != col)
            for (int k = 0; k < 4; k++) {
                double t = a[col][k];
                a[col][k] = a[piv][k];
                a[piv][k] = t;
            }
        for (int r = 0; r < 3; r++) {
            if (r == col) continue;
            double m = a[r][col] / a[col][col];
            for (int k = col; k < 4; k++) a[r][k] -= m * a[col][k];
        }
    }
    double p0 = a[0][3] / a[0][0], p1 = a[1][3] / a[1][1], p2 = a[2][3] / a[2][2];
    c[2] = p2 / (sx * sx);
    c[1] = p1 / sx - 2 * p2 * mx / (sx * sx);
    c[0] = p0 - p1 * mx / sx + p2 * mx * mx / (sx * sx);
    return 0;
}

int calc_fit(int kind, const double *xs, const double *ys, int n, CalcFit *out, const char **msg) {
    static const char *dummy;
    if (!msg) msg = &dummy;
    memset(out, 0, sizeof *out);
    out->kind = kind;
    out->n = n;
    int need = kind == CALC_FIT_QUAD ? 3 : 2;
    if (n < need) {
        *msg = kind == CALC_FIT_QUAD ? "a quadratic fit needs 3 points" : "a fit needs 2 points";
        return -1;
    }
    if (n > CALC_LIST_MAX || !all_finite(xs, n) || !all_finite(ys, n)) {
        *msg = "every value needs to be a number";
        return -1;
    }
    double r = 0;
    if (kind == CALC_FIT_LINEAR) {
        if (line_fit(xs, ys, n, &out->c[0], &out->c[1], &r)) {
            *msg = "the x values are all the same";
            return -1;
        }
        out->r = r;
    } else if (kind == CALC_FIT_QUAD) {
        if (quad_fit(xs, ys, n, out->c)) {
            *msg = "a quadratic fit needs 3 different x values";
            return -1;
        }
    } else if (kind == CALC_FIT_EXP) {
        double ly[CALC_LIST_MAX];
        for (int i = 0; i < n; i++) {
            if (!(ys[i] > 0)) {
                *msg = "an exponential fit needs every y above 0";
                return -1;
            }
            ly[i] = log(ys[i]);
        }
        double b0, b1;
        if (line_fit(xs, ly, n, &b0, &b1, &r)) {
            *msg = "the x values are all the same";
            return -1;
        }
        out->c[0] = exp(b0);
        out->c[1] = b1;
    } else {
        *msg = "unknown fit";
        return -1;
    }
    double my = 0;
    for (int i = 0; i < n; i++) my += ys[i];
    my /= n;
    double res = 0, tot = 0;
    for (int i = 0; i < n; i++) {
        double e = ys[i] - calc_fit_eval(out, xs[i]);
        res += e * e;
        tot += (ys[i] - my) * (ys[i] - my);
    }
    out->r2 = tot > 0 ? 1 - res / tot : 1;
    return 0;
}

/* " + 2.5 x" or " - 2.5 x" after the first term; the first term as is */
static size_t term(char *out, size_t cap, size_t at, double c, const char *what, int first) {
    char num[40];
    if (at >= cap) return at;
    if (first) {
        sqm_format(c, 6, num, sizeof num);
        return at + (size_t)snprintf(out + at, cap - at, "%s%s", num, what);
    }
    sqm_format(fabs(c), 6, num, sizeof num);
    if (!strcmp(num, "0")) return at; /* "y = 2 x", not "y = 2 x + 0" */
    return at + (size_t)snprintf(out + at, cap - at, " %c %s%s", c < 0 ? '-' : '+', num, what);
}

void calc_fit_text(const CalcFit *f, char *out, size_t cap) {
    if (!cap) return;
    size_t at = (size_t)snprintf(out, cap, "y = ");
    char a[40], b[40];
    switch (f->kind) {
    case CALC_FIT_LINEAR:
        at = term(out, cap, at, f->c[1], " x", 1);
        term(out, cap, at, f->c[0], "", 0);
        break;
    case CALC_FIT_QUAD:
        at = term(out, cap, at, f->c[2], " x^2", 1);
        at = term(out, cap, at, f->c[1], " x", 0);
        term(out, cap, at, f->c[0], "", 0);
        break;
    default:
        sqm_format(f->c[0], 6, a, sizeof a);
        sqm_format(f->c[1], 6, b, sizeof b);
        if (at < cap) snprintf(out + at, cap - at, "%s e^(%s x)", a, b);
    }
}
