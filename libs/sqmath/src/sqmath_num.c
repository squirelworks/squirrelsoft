/* sqmath: roots and integrals of compiled programs (see sqmath.h). Original code. */
#include <math.h>

#include "sqmath.h"

static double at(const SqmProgram *p, double *values, int slot, double x) {
    values[slot] = x;
    return sqm_run(p, values);
}

int sqm_solve(const SqmProgram *p, double *values, int slot, double lo, double hi, double *root) {
    if (slot < 0 || slot >= p->nslots || !(lo <= hi)) return -1;
    double a = lo, b = hi;
    double fa = at(p, values, slot, a), fb = at(p, values, slot, b);
    if (!isfinite(fa) || !isfinite(fb)) return -1;
    if (fa == 0) {
        *root = a;
        return SQM_OK;
    }
    if (fb == 0) {
        *root = b;
        return SQM_OK;
    }
    if ((fa < 0) == (fb < 0)) return -1;
    int side = 0; /* Illinois: halve the weight of an end that stays put twice */
    for (int it = 0; it < 400; it++) {
        double c;
        if (it % 4 == 3) {
            c = a + (b - a) / 2; /* every fourth step bisects, so the bracket always shrinks */
        } else {
            c = (a * fb - b * fa) / (fb - fa);
            if (!(c > a && c < b)) c = a + (b - a) / 2;
        }
        if (c <= a || c >= b) break; /* a and b are adjacent doubles */
        double fc = at(p, values, slot, c);
        if (fc == 0) {
            a = b = c;
            break;
        }
        if (!isfinite(fc)) { /* a pole or a gap: plain bisection from here */
            fc = NAN;
        }
        if (isnan(fc) || ((fc < 0) == (fa < 0))) {
            a = c;
            if (!isnan(fc)) fa = fc;
            if (side == -1) fb /= 2;
            side = -1;
        } else {
            b = c;
            fb = fc;
            if (side == 1) fa /= 2;
            side = 1;
        }
        if (b - a <= 4e-16 * fmax(fabs(a), fabs(b)) + 1e-300) break;
    }
    /* the end with the smaller value, or the meeting point */
    double fa2 = at(p, values, slot, a), fb2 = at(p, values, slot, b);
    *root = fabs(fa2) <= fabs(fb2) || !isfinite(fb2) ? a : b;
    return SQM_OK;
}

int sqm_roots(const SqmProgram *p, double *values, int slot, double lo, double hi, int steps,
              double *out, int cap) {
    if (slot < 0 || slot >= p->nslots || !(lo < hi) || steps < 1) return 0;
    int n = 0;
    double last = NAN;
    double x0 = lo, f0 = at(p, values, slot, lo);
    for (int i = 0; i <= steps; i++) {
        double x = i == steps ? hi : lo + (hi - lo) * i / steps;
        double f = i == 0 ? f0 : at(p, values, slot, x);
        double r = NAN;
        if (f == 0) {
            r = x;
        } else if (i > 0 && isfinite(f) && isfinite(f0) && f0 != 0 && (f < 0) != (f0 < 0)) {
            double c;
            if (sqm_solve(p, values, slot, x0, x, &c) == SQM_OK) {
                /* a sign change across a pole (tan at pi/2) is not a root */
                double fc = at(p, values, slot, c);
                double scale = fmax(1.0, fmax(fabs(f0), fabs(f)));
                if (fabs(fc) <= 1e-6 * scale) r = c;
            }
        }
        if (!isnan(r) && !(n > 0 && fabs(r - last) <= 1e-12 * (1 + fabs(r)))) {
            if (n < cap) out[n] = r;
            n++;
            last = r;
        }
        x0 = x;
        f0 = f;
    }
    return n;
}

typedef struct {
    const SqmProgram *p;
    double *values;
    int slot, ok;
} Integ;

static double f_at(Integ *g, double x) {
    double y = at(g->p, g->values, g->slot, x);
    if (!isfinite(y)) g->ok = 0;
    return y;
}

static double simpson(Integ *g, double a, double b, double fa, double fm, double fb, double whole, double tol,
                      int depth) {
    double m = (a + b) / 2, lm = (a + m) / 2, rm = (m + b) / 2;
    double flm = f_at(g, lm), frm = f_at(g, rm);
    double left = (m - a) / 6 * (fa + 4 * flm + fm), right = (b - m) / 6 * (fm + 4 * frm + fb);
    double diff = left + right - whole;
    if (!g->ok) return left + right;
    if (depth <= 0) {
        g->ok = 0;
        return left + right;
    }
    if (fabs(diff) <= 15 * tol) return left + right + diff / 15; /* Richardson step */
    return simpson(g, a, m, fa, flm, fm, left, tol / 2, depth - 1) +
           simpson(g, m, b, fm, frm, fb, right, tol / 2, depth - 1);
}

double sqm_integrate(const SqmProgram *p, double *values, int slot, double a, double b, double tol, int *ok) {
    Integ g = {p, values, slot, 1};
    if (slot < 0 || slot >= p->nslots) {
        if (ok) *ok = 0;
        return NAN;
    }
    if (a == b) {
        if (ok) *ok = 1;
        return 0;
    }
    if (!(tol > 0)) tol = 1e-10;
    /* start from four panels so a symmetric integrand can't fool the first test */
    double sum = 0;
    for (int k = 0; k < 4; k++) {
        double x0 = a + (b - a) * k / 4, x1 = a + (b - a) * (k + 1) / 4;
        double f0 = f_at(&g, x0), f1 = f_at(&g, x1), fm = f_at(&g, (x0 + x1) / 2);
        double whole = (x1 - x0) / 6 * (f0 + 4 * fm + f1);
        sum += simpson(&g, x0, x1, f0, fm, f1, whole, tol / 4, 48);
    }
    if (ok) *ok = g.ok;
    return sum;
}
