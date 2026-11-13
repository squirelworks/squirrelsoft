/* calcstats: lists and least-squares fits (see calcstats.h). Original code. */
#include "calcstats.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sqmath.h"

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
    return 0;
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
