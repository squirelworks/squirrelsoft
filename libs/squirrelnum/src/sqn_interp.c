/*
 * Linear interpolation, natural cubic spline, table lookup.
 *
 * NR 3rd ed. Ch. 3.1 (linear), Ch. 3.3 (cubic spline) — method ideas only.
 * Natural spline tridiagonal setup as in de Boor, A Practical Guide to Splines.
 */
#include "squirrelnum.h"

#include <string.h>

int sqn_table_lookup(const double *x, int n, double xq, int *i_left) {
    if (!x || n < 2 || !i_left) return SQN_ERR_ARGS;
    if (xq <= x[0]) {
        *i_left = 0;
        return SQN_OK;
    }
    if (xq >= x[n - 1]) {
        *i_left = n - 2;
        return SQN_OK;
    }
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (x[mid] > xq) hi = mid;
        else lo = mid;
    }
    *i_left = lo;
    return SQN_OK;
}

int sqn_lerp(const double *x, const double *y, int n, double xq, double *yq) {
    if (!x || !y || !yq || n < 2) return SQN_ERR_ARGS;
    int i = 0;
    if (sqn_table_lookup(x, n, xq, &i) != SQN_OK) return SQN_ERR_ARGS;
    double x0 = x[i], x1 = x[i + 1];
    double dx = x1 - x0;
    if (dx == 0.0) {
        *yq = y[i];
        return SQN_OK;
    }
    double t = (xq - x0) / dx;
    *yq = y[i] * (1.0 - t) + y[i + 1] * t;
    return SQN_OK;
}

int sqn_spline_build(SqnSpline *s, const double *x, const double *y, int n) {
    if (!s || !x || !y || n < 3 || n > SQN_MAX_SPLINE) return SQN_ERR_ARGS;
    int i;
    for (i = 1; i < n; i++)
        if (!(x[i] > x[i - 1])) return SQN_ERR_ARGS;
    memset(s, 0, sizeof *s);
    s->n = n;
    for (i = 0; i < n; i++) {
        s->x[i] = x[i];
        s->y[i] = y[i];
    }

    double u[SQN_MAX_SPLINE];
    s->y2[0] = 0.0;
    u[0] = 0.0;
    for (i = 1; i < n - 1; i++) {
        double sig = (x[i] - x[i - 1]) / (x[i + 1] - x[i - 1]);
        double p = sig * s->y2[i - 1] + 2.0;
        s->y2[i] = (sig - 1.0) / p;
        u[i] = (y[i + 1] - y[i]) / (x[i + 1] - x[i]) -
               (y[i] - y[i - 1]) / (x[i] - x[i - 1]);
        u[i] = (6.0 * u[i] / (x[i + 1] - x[i - 1]) - sig * u[i - 1]) / p;
    }
    s->y2[n - 1] = 0.0;
    for (i = n - 2; i >= 0; i--) s->y2[i] = s->y2[i] * s->y2[i + 1] + u[i];
    return SQN_OK;
}

int sqn_spline_eval(const SqnSpline *s, double xq, double *yq) {
    if (!s || !yq || s->n < 3) return SQN_ERR_ARGS;
    int klo = 0;
    if (sqn_table_lookup(s->x, s->n, xq, &klo) != SQN_OK) return SQN_ERR_ARGS;
    int khi = klo + 1;
    double h = s->x[khi] - s->x[klo];
    if (h == 0.0) return SQN_ERR_SINGULAR;
    double a = (s->x[khi] - xq) / h;
    double b = (xq - s->x[klo]) / h;
    *yq = a * s->y[klo] + b * s->y[khi] +
          ((a * a * a - a) * s->y2[klo] + (b * b * b - b) * s->y2[khi]) * (h * h) /
              6.0;
    return SQN_OK;
}
