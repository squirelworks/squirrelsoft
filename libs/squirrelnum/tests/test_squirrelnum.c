/*
 * squirrelnum unit tests — oracles from known analytic behaviour and
 * NR/textbook worked examples (values), never NR listing code.
 */
#include "squirrelnum.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

/* y' = -y → y = exp(-t); NR Ch.17 style decay example. */
static void ode_decay(double t, const double *y, double *dydt, void *ctx) {
    (void)t;
    (void)ctx;
    dydt[0] = -y[0];
}

/* Harmonic oscillator: y'' = -ω² y → [y, v], energy conserved. */
typedef struct {
    double w2;
} HarmCtx;

static void ode_harm(double t, const double *y, double *dydt, void *ctx) {
    (void)t;
    HarmCtx *c = ctx;
    dydt[0] = y[1];
    dydt[1] = -c->w2 * y[0];
}

/* Torque-free Euler top with I1=I2≠I3 → simple spin; use 3D ω with L conserved
 * for principal-axis free body: dω/dt = I^{-1}(L × ω) with L = I ω.
 * Here: I = diag(2,3,4), energy and |L| conserved. */
static void ode_rigid(double t, const double *w, double *dw, void *ctx) {
    (void)t;
    (void)ctx;
    const double I1 = 2.0, I2 = 3.0, I3 = 4.0;
    double L1 = I1 * w[0], L2 = I2 * w[1], L3 = I3 * w[2];
    /* Euler: I ω̇ = ω × (I ω) with body rates — torque-free:
       I1 dw1 = (I2-I3) w2 w3, etc. */
    dw[0] = ((I2 - I3) / I1) * w[1] * w[2];
    dw[1] = ((I3 - I1) / I2) * w[2] * w[0];
    dw[2] = ((I1 - I2) / I3) * w[0] * w[1];
    (void)L1;
    (void)L2;
    (void)L3;
}

static double energy_rigid(const double *w) {
    const double I1 = 2.0, I2 = 3.0, I3 = 4.0;
    return 0.5 * (I1 * w[0] * w[0] + I2 * w[1] * w[1] + I3 * w[2] * w[2]);
}

static double f_quad(double x, void *ctx) {
    (void)ctx;
    return x * x - 2.0; /* root √2; classic Brent/NR Ch.9 example */
}

static void test_rk4_decay(void) {
    double y = 1.0;
    expect(sqn_rk4_integrate(ode_decay, NULL, 0.0, &y, 1, 1.0, 0.01) == SQN_OK,
           "rk4 integrate");
    double exact = exp(-1.0);
    expect(fabs(y - exact) < 1e-6, "rk4 exp(-t) at t=1");
}

static void test_rk45_decay(void) {
    double y = 1.0;
    SqnRk45Opt opt;
    sqn_rk45_opt_default(&opt);
    opt.atol = 1e-10;
    opt.rtol = 1e-10;
    expect(sqn_rk45_integrate(ode_decay, NULL, 0.0, &y, 1, 1.0, 0.1, &opt) ==
               SQN_OK,
           "rk45 integrate");
    expect(fabs(y - exp(-1.0)) < 1e-8, "rk45 exp(-t)");
    expect(opt.naccept > 0, "rk45 accepted steps");
}

static void test_energy_convergence(void) {
    /* D1 / #174: RK4 energy drift on torque-free body vs dt */
    const double T = 100.0;
    double dts[] = {0.1, 0.05, 0.025, 0.0125};
    double drifts[4];
    int i;
    for (i = 0; i < 4; i++) {
        double w[3] = {1.0, 0.5, 0.2};
        double E0 = energy_rigid(w);
        expect(sqn_rk4_integrate(ode_rigid, NULL, 0.0, w, 3, T, dts[i]) == SQN_OK,
               "rigid rk4");
        double E1 = energy_rigid(w);
        drifts[i] = fabs(E1 - E0) / E0;
    }
    /* finer dt → smaller drift (roughly O(dt^4) locally; global better than Euler) */
    expect(drifts[3] < drifts[0], "rk4 energy drift shrinks with dt");
    expect(drifts[3] < 1e-3, "rk4 energy drift < 0.1% at dt=0.0125 over 100s");

    /* RK45 at default tol */
    {
        double w[3] = {1.0, 0.5, 0.2};
        double E0 = energy_rigid(w);
        SqnRk45Opt opt;
        sqn_rk45_opt_default(&opt);
        opt.atol = 1e-10;
        opt.rtol = 1e-8;
        opt.dt_max = 0.2;
        expect(sqn_rk45_integrate(ode_rigid, NULL, 0.0, w, 3, T, 0.05, &opt) ==
                   SQN_OK,
               "rigid rk45");
        double drift = fabs(energy_rigid(w) - E0) / E0;
        expect(drift < 1e-3, "rk45 energy drift < 0.1% over 100s");
    }

    /* Harmonic energy with RK4 */
    {
        HarmCtx c = {1.0};
        double y[2] = {1.0, 0.0};
        double E0 = 0.5 * (y[0] * y[0] + y[1] * y[1]);
        sqn_rk4_integrate(ode_harm, &c, 0.0, y, 2, 100.0, 0.01);
        double E1 = 0.5 * (y[0] * y[0] + y[1] * y[1]);
        expect(fabs(E1 - E0) / E0 < 1e-6, "harmonic energy RK4");
    }
}

static void test_brent(void) {
    double root = 0.0;
    int iters = 0;
    expect(sqn_brent(f_quad, NULL, 0.0, 2.0, 1e-12, 100, &root, &iters) == SQN_OK,
           "brent");
    expect(fabs(root - sqrt(2.0)) < 1e-10, "brent root √2");
    expect(iters > 0 && iters < 100, "brent iters");
}

static void test_interp(void) {
    double x[] = {0.0, 1.0, 2.0, 3.0};
    double y[] = {0.0, 1.0, 4.0, 9.0}; /* y=x^2 */
    double yq = 0.0;
    expect(sqn_lerp(x, y, 4, 1.5, &yq) == SQN_OK, "lerp");
    expect(fabs(yq - 2.5) < 1e-12, "lerp midpoint 1–4 → 2.5");

    SqnSpline s;
    expect(sqn_spline_build(&s, x, y, 4) == SQN_OK, "spline build");
    expect(sqn_spline_eval(&s, 1.5, &yq) == SQN_OK, "spline eval");
    /* natural spline of x^2 is not exact x^2, but close near centre */
    expect(fabs(yq - 2.25) < 0.15, "spline near x^2 at 1.5");

    int i = -1;
    expect(sqn_table_lookup(x, 4, 2.2, &i) == SQN_OK && i == 2, "table lookup");
}

static void test_la(void) {
    /* [[2,1],[1,3]] [x,y] = [5,5] → x=2, y=1 */
    double A[] = {2.0, 1.0, 1.0, 3.0};
    double b[] = {5.0, 5.0};
    double x[2];
    expect(sqn_gesv(A, b, 2, x) == SQN_OK, "gesv");
    expect(fabs(x[0] - 2.0) < 1e-12 && fabs(x[1] - 1.0) < 1e-12, "gesv solution");
}

static void test_quat(void) {
    SqnQuat q;
    sqn_quat_from_euler_zyx(0.1, 0.2, 0.3, &q);
    expect(fabs(sqn_quat_norm(&q) - 1.0) < 1e-14, "quat unit");
    double yaw, pitch, roll;
    sqn_quat_to_euler_zyx(&q, &yaw, &pitch, &roll);
    expect(fabs(yaw - 0.1) < 1e-10 && fabs(pitch - 0.2) < 1e-10 &&
               fabs(roll - 0.3) < 1e-10,
           "euler roundtrip");

    /* pitch through ~90° via quaternion — no singularity */
    sqn_quat_from_euler_zyx(0.0, 1.5707963267948966, 0.0, &q);
    expect(fabs(sqn_quat_norm(&q) - 1.0) < 1e-14, "quat at 90° pitch");
    SqnQuat dq, qn;
    sqn_quat_deriv(&q, 0.0, 0.1, 0.0, &dq);
    qn.w = q.w + dq.w * 0.01;
    qn.x = q.x + dq.x * 0.01;
    qn.y = q.y + dq.y * 0.01;
    qn.z = q.z + dq.z * 0.01;
    sqn_quat_normalize(&qn);
    expect(fabs(sqn_quat_norm(&qn) - 1.0) < 1e-14, "renorm after step");
}

int main(void) {
    test_rk4_decay();
    test_rk45_decay();
    test_energy_convergence();
    test_brent();
    test_interp();
    test_la();
    test_quat();
    if (fails) {
        fprintf(stderr, "%d squirrelnum failure(s)\n", fails);
        return 1;
    }
    puts("test_squirrelnum ok");
    return 0;
}
