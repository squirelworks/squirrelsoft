/*
 * Fixed-step RK4 and adaptive Dormand–Prince 5(4).
 *
 * Method references (no NR listing code):
 *   - Numerical Recipes 3rd ed. Ch. 17.1–17.2 (RK methods; adaptive step ideas)
 *   - Dormand & Prince, J. Comput. Appl. Math. 6 (1980) 19–26 (DP5(4) tableau)
 *   - Classical RK4: Kutta, Z. Math. Phys. 46 (1901)
 */
#include "squirrelnum.h"

#include <math.h>
#include <string.h>

static void ode_eval(SqnOdeFn f, void *ctx, double t, const double *y, int n,
                     double *dydt) {
    (void)n;
    f(t, y, dydt, ctx);
}

int sqn_rk4_step(SqnOdeFn f, void *ctx, double t, double *y, int n, double dt) {
    if (!f || !y || n <= 0 || n > SQN_MAX_DIM || dt == 0.0) return SQN_ERR_ARGS;
    double k1[SQN_MAX_DIM], k2[SQN_MAX_DIM], k3[SQN_MAX_DIM], k4[SQN_MAX_DIM];
    double yt[SQN_MAX_DIM];
    int i;

    ode_eval(f, ctx, t, y, n, k1);
    for (i = 0; i < n; i++) yt[i] = y[i] + 0.5 * dt * k1[i];
    ode_eval(f, ctx, t + 0.5 * dt, yt, n, k2);
    for (i = 0; i < n; i++) yt[i] = y[i] + 0.5 * dt * k2[i];
    ode_eval(f, ctx, t + 0.5 * dt, yt, n, k3);
    for (i = 0; i < n; i++) yt[i] = y[i] + dt * k3[i];
    ode_eval(f, ctx, t + dt, yt, n, k4);

    for (i = 0; i < n; i++)
        y[i] += (dt / 6.0) * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
    return SQN_OK;
}

int sqn_rk4_integrate(SqnOdeFn f, void *ctx, double t0, double *y, int n,
                      double duration, double dt) {
    if (duration < 0.0 || dt <= 0.0) return SQN_ERR_ARGS;
    double t = t0;
    double end = t0 + duration;
    while (t + 1e-15 < end) {
        double h = dt;
        if (t + h > end) h = end - t;
        int rc = sqn_rk4_step(f, ctx, t, y, n, h);
        if (rc != SQN_OK) return rc;
        t += h;
    }
    return SQN_OK;
}

void sqn_rk45_opt_default(SqnRk45Opt *o) {
    if (!o) return;
    memset(o, 0, sizeof *o);
    o->atol = 1e-8;
    o->rtol = 1e-6;
    o->dt_min = 1e-12;
    o->dt_max = 1.0;
    o->max_steps = 100000;
}

/* Dormand–Prince 5(4) coefficients (Butcher tableau). */
static const double c2 = 1.0 / 5.0;
static const double c3 = 3.0 / 10.0;
static const double c4 = 4.0 / 5.0;
static const double c5 = 8.0 / 9.0;

static const double a21 = 1.0 / 5.0;
static const double a31 = 3.0 / 40.0, a32 = 9.0 / 40.0;
static const double a41 = 44.0 / 45.0, a42 = -56.0 / 15.0, a43 = 32.0 / 9.0;
static const double a51 = 19372.0 / 6561.0, a52 = -25360.0 / 2187.0,
                    a53 = 64448.0 / 6561.0, a54 = -212.0 / 729.0;
static const double a61 = 9017.0 / 3168.0, a62 = -355.0 / 33.0,
                    a63 = 46732.0 / 5247.0, a64 = 49.0 / 176.0,
                    a65 = -5103.0 / 18656.0;
static const double a71 = 35.0 / 384.0, a73 = 500.0 / 1113.0,
                    a74 = 125.0 / 192.0, a75 = -2187.0 / 6784.0,
                    a76 = 11.0 / 84.0;

/* 5th-order solution weights (b); 4th-order embedded (bh). */
static const double b1 = 35.0 / 384.0, b3 = 500.0 / 1113.0, b4 = 125.0 / 192.0,
                    b5 = -2187.0 / 6784.0, b6 = 11.0 / 84.0;
static const double bh1 = 5179.0 / 57600.0, bh3 = 7571.0 / 16695.0,
                    bh4 = 393.0 / 640.0, bh5 = -92097.0 / 339200.0,
                    bh6 = 187.0 / 2100.0, bh7 = 1.0 / 40.0;

int sqn_rk45_step(SqnOdeFn f, void *ctx, double *t, double *y, int n,
                  double *dt, const SqnRk45Opt *opt) {
    if (!f || !t || !y || !dt || n <= 0 || n > SQN_MAX_DIM) return SQN_ERR_ARGS;
    SqnRk45Opt local;
    if (!opt) {
        sqn_rk45_opt_default(&local);
        opt = &local;
    }
    double h = *dt;
    if (h <= 0.0) return SQN_ERR_ARGS;
    if (h < opt->dt_min) h = opt->dt_min;
    if (h > opt->dt_max) h = opt->dt_max;

    double k1[SQN_MAX_DIM], k2[SQN_MAX_DIM], k3[SQN_MAX_DIM], k4[SQN_MAX_DIM];
    double k5[SQN_MAX_DIM], k6[SQN_MAX_DIM], k7[SQN_MAX_DIM], yt[SQN_MAX_DIM];
    int i, attempt;

    ode_eval(f, ctx, *t, y, n, k1);

    for (attempt = 0; attempt < 40; attempt++) {
        for (i = 0; i < n; i++) yt[i] = y[i] + h * a21 * k1[i];
        ode_eval(f, ctx, *t + c2 * h, yt, n, k2);

        for (i = 0; i < n; i++) yt[i] = y[i] + h * (a31 * k1[i] + a32 * k2[i]);
        ode_eval(f, ctx, *t + c3 * h, yt, n, k3);

        for (i = 0; i < n; i++)
            yt[i] = y[i] + h * (a41 * k1[i] + a42 * k2[i] + a43 * k3[i]);
        ode_eval(f, ctx, *t + c4 * h, yt, n, k4);

        for (i = 0; i < n; i++)
            yt[i] = y[i] +
                    h * (a51 * k1[i] + a52 * k2[i] + a53 * k3[i] + a54 * k4[i]);
        ode_eval(f, ctx, *t + c5 * h, yt, n, k5);

        for (i = 0; i < n; i++)
            yt[i] = y[i] + h * (a61 * k1[i] + a62 * k2[i] + a63 * k3[i] +
                                a64 * k4[i] + a65 * k5[i]);
        ode_eval(f, ctx, *t + h, yt, n, k6);

        for (i = 0; i < n; i++)
            yt[i] = y[i] + h * (a71 * k1[i] + a73 * k3[i] + a74 * k4[i] +
                                a75 * k5[i] + a76 * k6[i]);
        ode_eval(f, ctx, *t + h, yt, n, k7);

        double y5[SQN_MAX_DIM], err = 0.0;
        for (i = 0; i < n; i++) {
            y5[i] = y[i] + h * (b1 * k1[i] + b3 * k3[i] + b4 * k4[i] +
                                b5 * k5[i] + b6 * k6[i]);
            double y4 = y[i] + h * (bh1 * k1[i] + bh3 * k3[i] + bh4 * k4[i] +
                                    bh5 * k5[i] + bh6 * k6[i] + bh7 * k7[i]);
            double sc = opt->atol + opt->rtol * fmax(fabs(y[i]), fabs(y5[i]));
            double e = fabs(y5[i] - y4) / (sc > 0.0 ? sc : 1.0);
            if (e > err) err = e;
        }

        if (err <= 1.0 || h <= opt->dt_min * 1.000001) {
            *t += h;
            for (i = 0; i < n; i++) y[i] = y5[i];
            /* PI step-size controller (Hairer / NR adaptive-step ideas) */
            double fac = 0.9 * pow(err > 1e-16 ? err : 1e-16, -0.2);
            if (fac < 0.2) fac = 0.2;
            if (fac > 5.0) fac = 5.0;
            h *= fac;
            if (h < opt->dt_min) h = opt->dt_min;
            if (h > opt->dt_max) h = opt->dt_max;
            *dt = h;
            return SQN_OK;
        }
        /* reject */
        double fac = 0.9 * pow(err, -0.25);
        if (fac < 0.2) fac = 0.2;
        if (fac > 0.9) fac = 0.9;
        h *= fac;
        if (h < opt->dt_min) h = opt->dt_min;
    }
    return SQN_ERR_CONV;
}

int sqn_rk45_integrate(SqnOdeFn f, void *ctx, double t0, double *y, int n,
                       double t_end, double dt_init, SqnRk45Opt *opt) {
    if (t_end < t0 || dt_init <= 0.0) return SQN_ERR_ARGS;
    SqnRk45Opt local;
    if (!opt) {
        sqn_rk45_opt_default(&local);
        opt = &local;
    }
    double t = t0;
    double dt = dt_init;
    opt->naccept = 0;
    opt->nreject = 0;
    int steps = 0;
    while (t + 1e-15 < t_end) {
        if (steps++ >= opt->max_steps) return SQN_ERR_CONV;
        if (t + dt > t_end) dt = t_end - t;
        double t_before = t;
        double y_before[SQN_MAX_DIM];
        memcpy(y_before, y, (size_t)n * sizeof(double));
        double dt_try = dt;
        int rc = sqn_rk45_step(f, ctx, &t, y, n, &dt, opt);
        if (rc != SQN_OK) return rc;
        if (t > t_before + 1e-18)
            opt->naccept++;
        else {
            /* shouldn't happen */
            memcpy(y, y_before, (size_t)n * sizeof(double));
            opt->nreject++;
        }
        (void)dt_try;
    }
    return SQN_OK;
}
