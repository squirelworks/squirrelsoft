/*
 * Brent root finder on a sign-changing bracket.
 *
 * NR 3rd ed. Ch. 9.3 (Van Wijngaarden–Dekker–Brent method) — narrative only.
 * Brent, R.P., Algorithms for Minimization without Derivatives, Prentice-Hall,
 * 1973. Original Soft implementation (bisection + inverse quadratic / secant).
 */
#include "squirrelnum.h"

#include <math.h>

int sqn_brent(SqnScalarFn f, void *ctx, double a, double b, double tol,
              int max_iter, double *root_out, int *iters_out) {
    if (!f || !root_out || tol <= 0.0 || max_iter <= 0) return SQN_ERR_ARGS;
    double fa = f(a, ctx);
    double fb = f(b, ctx);
    if (!isfinite(fa) || !isfinite(fb)) return SQN_ERR_ARGS;
    if (fa * fb > 0.0) return SQN_ERR_BRACKET;

    double c = a, fc = fa;
    double d = b - a, e = d;
    int it;
    for (it = 0; it < max_iter; it++) {
        if (fb * fc > 0.0) {
            c = a;
            fc = fa;
            d = e = b - a;
        }
        if (fabs(fc) < fabs(fb)) {
            a = b;
            b = c;
            c = a;
            fa = fb;
            fb = fc;
            fc = fa;
        }
        double tol1 = 2.0 * 2.220446049250313e-16 * fabs(b) + 0.5 * tol;
        double xm = 0.5 * (c - b);
        if (fabs(xm) <= tol1 || fb == 0.0) {
            *root_out = b;
            if (iters_out) *iters_out = it + 1;
            return SQN_OK;
        }
        if (fabs(e) >= tol1 && fabs(fa) > fabs(fb)) {
            double p, q, r, s = fb / fa;
            if (a == c) {
                /* secant */
                p = 2.0 * xm * s;
                q = 1.0 - s;
            } else {
                /* inverse quadratic */
                q = fa / fc;
                r = fb / fc;
                p = s * (2.0 * xm * q * (q - r) - (b - a) * (r - 1.0));
                q = (q - 1.0) * (r - 1.0) * (s - 1.0);
            }
            if (p > 0.0) q = -q;
            p = fabs(p);
            double min1 = 3.0 * xm * q - fabs(tol1 * q);
            double min2 = fabs(e * q);
            if (2.0 * p < (min1 < min2 ? min1 : min2)) {
                e = d;
                d = p / q;
            } else {
                d = xm;
                e = d;
            }
        } else {
            d = xm;
            e = d;
        }
        a = b;
        fa = fb;
        if (fabs(d) > tol1) b += d;
        else b += (xm > 0.0 ? tol1 : -tol1);
        fb = f(b, ctx);
        if (!isfinite(fb)) return SQN_ERR_CONV;
    }
    *root_out = b;
    if (iters_out) *iters_out = max_iter;
    return SQN_ERR_CONV;
}
