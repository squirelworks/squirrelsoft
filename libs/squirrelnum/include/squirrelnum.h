/*
 * squirrelnum — original numerical methods for Soft Flight Lab and friends.
 *
 * Numerical Recipes (3rd ed., Cambridge) is the primary *method* reference
 * (algorithm choice, step-size control, pitfalls). Code listings are NOT
 * copied; each routine is written from that understanding and cross-checked
 * against the cited papers. See per-function comments for NR chapter/section
 * and paper cites.
 *
 * C11, no OS calls, no heap allocation in the core APIs.
 */
#ifndef SQUIRRELNUM_H
#define SQUIRRELNUM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SQN_OK = 0,
    SQN_ERR_ARGS = -1,
    SQN_ERR_CONV = -2,   /* failed to converge */
    SQN_ERR_BRACKET = -3,
    SQN_ERR_SINGULAR = -4,
    SQN_ERR_FULL = -5
};

#define SQN_MAX_DIM 16
#define SQN_MAX_TABLE 256
#define SQN_MAX_SPLINE 256

/* ---- ODE right-hand side: y' = f(t, y) ---------------------------------- */
typedef void (*SqnOdeFn)(double t, const double *y, double *dydt, void *ctx);

/*
 * Classical 4th-order Runge–Kutta, one step.
 * NR 3rd ed. Ch. 17.1 (Runge-Kutta method) — method description only.
 * Butcher tableau: classical RK4 (Kutta 1901).
 */
int sqn_rk4_step(SqnOdeFn f, void *ctx, double t, double *y, int n, double dt);

/* Advance from t0 over duration with fixed dt using RK4; writes final y. */
int sqn_rk4_integrate(SqnOdeFn f, void *ctx, double t0, double *y, int n,
                      double duration, double dt);

typedef struct {
    double atol;      /* absolute tolerance (default 1e-8) */
    double rtol;      /* relative tolerance (default 1e-6) */
    double dt_min;    /* minimum step */
    double dt_max;    /* maximum step */
    int max_steps;    /* safety cap */
    int naccept;      /* out: accepted steps */
    int nreject;      /* out: rejected steps */
} SqnRk45Opt;

void sqn_rk45_opt_default(SqnRk45Opt *o);

/*
 * Dormand–Prince 5(4) embedded RK with adaptive step (one accepted step).
 * Advances *t and y; may take internal rejected attempts.
 * NR 3rd ed. Ch. 17.2 (Adaptive Stepsize Control) — ideas only.
 * Paper: Dormand & Prince, J. Comput. Appl. Math. 6 (1980) 19–26.
 */
int sqn_rk45_step(SqnOdeFn f, void *ctx, double *t, double *y, int n,
                  double *dt, const SqnRk45Opt *opt);

int sqn_rk45_integrate(SqnOdeFn f, void *ctx, double t0, double *y, int n,
                       double t_end, double dt_init, SqnRk45Opt *opt);

/* ---- Root finding ------------------------------------------------------ */

typedef double (*SqnScalarFn)(double x, void *ctx);

/*
 * Brent's method on a sign-changing bracket [a,b].
 * NR 3rd ed. Ch. 9.3 (Van Wijngaarden–Dekker–Brent) — method narrative.
 * Paper: Brent, Algorithms for Minimization without Derivatives, 1973.
 */
int sqn_brent(SqnScalarFn f, void *ctx, double a, double b, double tol,
              int max_iter, double *root_out, int *iters_out);

/* ---- Interpolation / table lookup -------------------------------------- */

/* Linear interpolation; x must be sorted ascending. NR 3rd ed. Ch. 3.1. */
int sqn_lerp(const double *x, const double *y, int n, double xq, double *yq);

typedef struct {
    int n;
    double x[SQN_MAX_SPLINE];
    double y[SQN_MAX_SPLINE];
    double y2[SQN_MAX_SPLINE]; /* second derivatives */
} SqnSpline;

/*
 * Natural cubic spline (y''=0 at ends). NR 3rd ed. Ch. 3.3 — algorithm idea.
 * Implementation follows the standard tridiagonal natural-spline setup
 * (e.g. de Boor), not an NR listing.
 */
int sqn_spline_build(SqnSpline *s, const double *x, const double *y, int n);
int sqn_spline_eval(const SqnSpline *s, double xq, double *yq);

/* Table lookup with clamp; returns index of left knot (or -1). */
int sqn_table_lookup(const double *x, int n, double xq, int *i_left);

/* ---- Small dense linear algebra ---------------------------------------- */

/*
 * Solve A x = b for n×n dense A (row-major), Gauss elimination with partial
 * pivoting. NR 3rd ed. Ch. 2.1–2.3 (Gaussian elimination / pivoting) — ideas.
 * n <= SQN_MAX_DIM. A is destroyed.
 */
int sqn_gesv(double *A, double *b, int n, double *x);

/* ---- Quaternions (Hamilton: w,x,y,z) ------------------------------------ */

typedef struct {
    double w, x, y, z;
} SqnQuat;

/*
 * Unit quaternions for attitude; Hamilton product; integrate ω in body frame.
 * NR does not own quaternion kinematics — cite Shoemake (SIGGRAPH 1985) and
 * Hamilton; renormalise after integration (Hairer geometric integration notes).
 */
void sqn_quat_identity(SqnQuat *q);
void sqn_quat_normalize(SqnQuat *q);
double sqn_quat_norm(const SqnQuat *q);
void sqn_quat_mul(const SqnQuat *a, const SqnQuat *b, SqnQuat *out);
/* q' = 0.5 * q ⊗ [0, ωx, ωy, ωz] */
void sqn_quat_deriv(const SqnQuat *q, double wx, double wy, double wz, SqnQuat *dq);
void sqn_quat_to_euler_zyx(const SqnQuat *q, double *yaw, double *pitch,
                           double *roll);
void sqn_quat_from_euler_zyx(double yaw, double pitch, double roll, SqnQuat *q);

#ifdef __cplusplus
}
#endif

#endif
