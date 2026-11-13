/* sqrtk: double differences, DGNSS and static carrier solutions
 * (see sqrtk.h). Original code. */
#include "sqrtk.h"

#include <math.h>
#include <string.h>

#define MAXU (3 + SQG_MAX_SATS) /* unknowns: position and ambiguities */

void sqg_rtk_options_default(SqgRtkOptions *o) {
    o->mask_deg = 15;
    o->sigma_code = 0.5;
    o->sigma_phase = 0.003;
    o->ratio_min = 3;
    o->dual = 1;
}

static const SqgObservation *find_obs(const SqgEpoch *ep, int prn) {
    for (int i = 0; i < ep->n_obs; i++)
        if (ep->obs[i].valid && ep->obs[i].prn == prn) return &ep->obs[i];
    return NULL;
}

static const SqgEphemeris *find_eph(const SqgEphemeris *ephs, int n, int prn) {
    for (int i = 0; i < n; i++)
        if (ephs[i].prn == prn) return &ephs[i];
    return NULL;
}

/* range from rx to the satellite of o (at its transmit time, in the frame
 * of the receive time) and the unit vector toward it */
static double range_to(const SqgEpoch *ep, const SqgObservation *o, const SqgEphemeris *eph, SqgEcef rx, double cb,
                       double u[3]) {
    double t_tx = ep->t_receiver - o->pseudorange_m / SQG_C_LIGHT, dts = 0;
    for (int j = 0; j < 2; j++) dts = sqg_sat_clock(eph, t_tx - dts);
    t_tx -= dts;
    SqgEcef s = sqg_sat_position(eph, t_tx);
    double a = SQG_OMEGA_E * (ep->t_receiver - cb / SQG_C_LIGHT - t_tx);
    SqgEcef sr = {cos(a) * s.x + sin(a) * s.y, -sin(a) * s.x + cos(a) * s.y, s.z};
    double dx = sr.x - rx.x, dy = sr.y - rx.y, dz = sr.z - rx.z, r = sqrt(dx * dx + dy * dy + dz * dz);
    u[0] = dx / r;
    u[1] = dy / r;
    u[2] = dz / r;
    return r;
}

/* the receiver clock of an epoch, from a single-point fix near pos */
static double clock_of(const SqgEpoch *ep, const SqgEphemeris *ephs, int n_eph, SqgEcef pos) {
    SqgPvt p;
    SqgPvtOptions o;
    sqg_pvt_options_default(&o);
    o.raim = 0;
    if (sqg_solve_pvt_opt(ep, ephs, n_eph, pos, &o, &p) == 0) return p.clock_bias_m;
    return 0;
}

/* solve the symmetric n x n system a x = b by Gauss-Jordan with pivoting;
 * a is replaced by its inverse. -1 when singular. */
static int solve_sym(double a[MAXU][MAXU], int n, double *b) {
    int perm[MAXU];
    double inv[MAXU][MAXU];
    for (int i = 0; i < n; i++) {
        perm[i] = i;
        for (int j = 0; j < n; j++) inv[i][j] = i == j;
    }
    (void)perm;
    for (int c = 0; c < n; c++) {
        int p = c;
        for (int r = c + 1; r < n; r++)
            if (fabs(a[r][c]) > fabs(a[p][c])) p = r;
        if (fabs(a[p][c]) < 1e-300) return -1;
        if (p != c) {
            for (int k = 0; k < n; k++) {
                double t = a[c][k];
                a[c][k] = a[p][k];
                a[p][k] = t;
                t = inv[c][k];
                inv[c][k] = inv[p][k];
                inv[p][k] = t;
            }
            double t = b[c];
            b[c] = b[p];
            b[p] = t;
        }
        double d = a[c][c];
        for (int k = 0; k < n; k++) {
            a[c][k] /= d;
            inv[c][k] /= d;
        }
        b[c] /= d;
        for (int r = 0; r < n; r++) {
            if (r == c || a[r][c] == 0) continue;
            double f = a[r][c];
            for (int k = 0; k < n; k++) {
                a[r][k] -= f * a[c][k];
                inv[r][k] -= f * inv[c][k];
            }
            b[r] -= f * b[c];
        }
    }
    memcpy(a, inv, sizeof inv);
    return 0;
}

int sqg_dgnss(const SqgEpoch *base, const SqgEpoch *rover, const SqgEphemeris *ephs, int n_eph, SqgEcef base_pos,
              const SqgRtkOptions *opt, SqgEcef *out, int *n_dd) {
    SqgRtkOptions def;
    if (!opt) {
        sqg_rtk_options_default(&def);
        opt = &def;
    }
    *n_dd = 0;
    SqgGeodetic bl;
    sqg_ecef_to_geodetic(base_pos, &bl);
    double cbb = clock_of(base, ephs, n_eph, base_pos);
    /* satellites in both, above the mask; the highest is the reference */
    int prn[SQG_MAX_SATS], n = 0, ref = -1;
    double best = -90;
    for (int i = 0; i < base->n_obs; i++) {
        const SqgObservation *b = &base->obs[i];
        const SqgEphemeris *e = b->valid ? find_eph(ephs, n_eph, b->prn) : NULL;
        if (!e || !find_obs(rover, b->prn)) continue;
        double u[3];
        range_to(base, b, e, base_pos, cbb, u);
        SqgEcef far = {base_pos.x + u[0] * 2e7, base_pos.y + u[1] * 2e7, base_pos.z + u[2] * 2e7};
        double el = sqg_az_el(base_pos, bl, far).el_deg;
        if (el < opt->mask_deg) continue;
        if (el > best) {
            best = el;
            ref = n;
        }
        prn[n++] = b->prn;
    }
    if (n < 5) return -1;
    /* start from the rover's own fix */
    SqgPvt sp;
    if (sqg_solve_pvt(rover, ephs, n_eph, base_pos, &sp)) return -1;
    SqgEcef x = sp.pos;
    double cbr = sp.clock_bias_m;
    for (int it = 0; it < 10; it++) {
        double nm[MAXU][MAXU] = {{0}}, rhs[MAXU] = {0};
        double ub[3], ur[3], ubr[3], urr[3];
        const SqgObservation *bref = find_obs(base, prn[ref]), *rref = find_obs(rover, prn[ref]);
        const SqgEphemeris *eref = find_eph(ephs, n_eph, prn[ref]);
        double rb0 = range_to(base, bref, eref, base_pos, cbb, ubr), rr0 = range_to(rover, rref, eref, x, cbr, urr);
        for (int j = 0; j < n; j++) {
            if (j == ref) continue;
            const SqgObservation *b = find_obs(base, prn[j]), *r = find_obs(rover, prn[j]);
            const SqgEphemeris *e = find_eph(ephs, n_eph, prn[j]);
            double rb = range_to(base, b, e, base_pos, cbb, ub), rr = range_to(rover, r, e, x, cbr, ur);
            double obs = (r->pseudorange_m - rref->pseudorange_m) - (b->pseudorange_m - bref->pseudorange_m);
            double model = (rr - rr0) - (rb - rb0);
            double h[3] = {-ur[0] + urr[0], -ur[1] + urr[1], -ur[2] + urr[2]};
            for (int a = 0; a < 3; a++) {
                rhs[a] += h[a] * (obs - model);
                for (int c = 0; c < 3; c++) nm[a][c] += h[a] * h[c];
            }
        }
        if (solve_sym(nm, 3, rhs)) return -1;
        x.x += rhs[0];
        x.y += rhs[1];
        x.z += rhs[2];
        if (fabs(rhs[0]) + fabs(rhs[1]) + fabs(rhs[2]) < 1e-6) break;
    }
    *out = x;
    *n_dd = n - 1;
    return 0;
}

/* ---- double-difference rows ---- */

/* one double difference (satellite j against the reference) in one epoch,
 * at a rover position: partials, model, and the measurements */
typedef struct {
    int ok, has2, slip;
    double h[3];   /* d(model)/d(rover) */
    double model;  /* m */
    double p1, l1; /* code (m), carrier (cycles) */
    double p2, l2;
} Row;

/* Rows for prn[1..n_sat-1] against prn[0]. Returns the rows with data, or
 * -1 when the reference has no carrier at either receiver. */
static int dd_rows(const int *prn, int n_sat, const SqgEpoch *b, const SqgEpoch *r, const SqgEphemeris *ephs,
                   int n_eph, SqgEcef base_pos, SqgEcef x, double cbb, double cbr, Row *rows) {
    const SqgObservation *b0 = find_obs(b, prn[0]), *r0 = find_obs(r, prn[0]);
    const SqgEphemeris *e0 = find_eph(ephs, n_eph, prn[0]);
    if (!b0 || !r0 || !e0 || !b0->has_carrier || !r0->has_carrier) return -1;
    double ub0[3], ur0[3];
    double rb0 = range_to(b, b0, e0, base_pos, cbb, ub0), rr0 = range_to(r, r0, e0, x, cbr, ur0);
    int ref2 = b0->has_l2 && r0->has_l2, n = 0;
    for (int j = 1; j < n_sat; j++) {
        Row *w = &rows[j - 1];
        memset(w, 0, sizeof *w);
        const SqgObservation *bj = find_obs(b, prn[j]), *rj = find_obs(r, prn[j]);
        const SqgEphemeris *e = find_eph(ephs, n_eph, prn[j]);
        if (!bj || !rj || !e || !bj->has_carrier || !rj->has_carrier) continue;
        double ub[3], ur[3];
        double rb = range_to(b, bj, e, base_pos, cbb, ub), rr = range_to(r, rj, e, x, cbr, ur);
        w->ok = 1;
        w->model = (rr - rr0) - (rb - rb0);
        for (int a = 0; a < 3; a++) w->h[a] = -ur[a] + ur0[a];
        w->p1 = (rj->pseudorange_m - r0->pseudorange_m) - (bj->pseudorange_m - b0->pseudorange_m);
        w->l1 = (rj->carrier_cyc - r0->carrier_cyc) - (bj->carrier_cyc - b0->carrier_cyc);
        if (ref2 && bj->has_l2 && rj->has_l2) {
            w->has2 = 1;
            w->p2 = (rj->pseudorange2_m - r0->pseudorange2_m) - (bj->pseudorange2_m - b0->pseudorange2_m);
            w->l2 = (rj->carrier2_cyc - r0->carrier2_cyc) - (bj->carrier2_cyc - b0->carrier2_cyc);
        }
        w->slip = bj->slip || rj->slip;
        n++;
    }
    return n;
}

/* the Melbourne-Wubbena combination of a row, in wide-lane cycles: the
 * wide-lane ambiguity plus code noise, free of geometry and ionosphere */
static double mw_cycles(const Row *w) {
    return (w->l1 - w->l2) - (SQG_L1_HZ * w->p1 + SQG_L2_HZ * w->p2) / ((SQG_L1_HZ + SQG_L2_HZ) * SQG_WL_LAMBDA);
}

/* the geometry-free carrier, m: constant but for the ionosphere, until a slip */
static double gf_m(const Row *w) { return SQG_L1_LAMBDA * w->l1 - SQG_L2_LAMBDA * w->l2; }

/* the noise of one epoch's Melbourne-Wubbena double difference, in cycles */
static double mw_sigma(const SqgRtkOptions *o) {
    double k = sqrt(SQG_L1_HZ * SQG_L1_HZ + SQG_L2_HZ * SQG_L2_HZ) / (SQG_L1_HZ + SQG_L2_HZ);
    return 2 * k * o->sigma_code / SQG_WL_LAMBDA; /* four receivers-satellites in a double difference */
}

/* how well n Melbourne-Wubbena values (their sum and sum of squares) pin
 * the mean: from their own scatter once there are enough of them, else
 * from the code noise expected */
static double wl_sd(double sum, double sq, int n, const SqgRtkOptions *o) {
    if (n < 1) return INFINITY;
    if (n < 8) return mw_sigma(o) / sqrt((double)n);
    double m = sum / n, var = (sq - n * m * m) / (n - 1);
    return sqrt(fmax(var, 0) / n);
}

/* Add one row to normal equations over [rover (3), ambiguity j (at 3 + j)]:
 * code on L1 (and L2), carrier on L1 (and on L2 when the wide lane wl is
 * known, through N2 = N1 - wl). amb is the ambiguity the residual is taken
 * against (0 for an absolute unknown); fixed_amb >= 0 puts it on the
 * right-hand side instead of in the unknowns. */
static void add_row(const Row *w, int j, int with_l2, long wl, double amb, int amb_unknown, const SqgRtkOptions *o,
                    double nm[MAXU][MAXU], double *rhs, int nu, double *sse, int *rows) {
    double wp = 1 / (o->sigma_phase * o->sigma_phase), wc = 1 / (o->sigma_code * o->sigma_code);
    double h[MAXU] = {w->h[0], w->h[1], w->h[2]};
    double obs[4], lam[4], wt[4];
    int n = 0;
    obs[n] = w->p1, lam[n] = 0, wt[n++] = wc;
    obs[n] = w->l1 * SQG_L1_LAMBDA - SQG_L1_LAMBDA * amb, lam[n] = SQG_L1_LAMBDA, wt[n++] = wp;
    if (with_l2 && w->has2) {
        obs[n] = w->p2, lam[n] = 0, wt[n++] = wc;
        obs[n] = (w->l2 + (double)wl) * SQG_L2_LAMBDA - SQG_L2_LAMBDA * amb, lam[n] = SQG_L2_LAMBDA, wt[n++] = wp;
    }
    for (int q = 0; q < n; q++) {
        double v = obs[q] - w->model;
        if (amb_unknown) h[3 + j] = lam[q];
        for (int a = 0; a < nu; a++) {
            if (h[a] == 0) continue;
            rhs[a] += wt[q] * h[a] * v;
            for (int c = 0; c < nu; c++) nm[a][c] += wt[q] * h[a] * h[c];
        }
        if (lam[q] != 0) {
            *sse += v * v;
            (*rows)++;
        }
        if (amb_unknown) h[3 + j] = 0;
    }
}

/* ---- integer least squares (LAMBDA-style) ---- */

#define ILS (SQG_MAX_SATS)

/* q = L' diag(D) L with L unit lower triangular; -1 when q is not positive definite */
static int ltdl(const double *q, int n, double L[ILS][ILS], double *D) {
    double A[ILS][ILS];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            A[i][j] = q[i * n + j];
            L[i][j] = 0;
        }
    for (int i = n - 1; i >= 0; i--) {
        D[i] = A[i][i];
        if (!(D[i] > 0)) return -1;
        double a = sqrt(D[i]);
        for (int j = 0; j <= i; j++) L[i][j] = A[i][j] / a;
        for (int j = 0; j < i; j++)
            for (int k = 0; k <= j; k++) A[j][k] -= L[i][k] * L[i][j];
        for (int j = 0; j <= i; j++) L[i][j] /= L[i][i];
    }
    return 0;
}

/* integer Gauss transformation of column j by column i */
static void ils_gauss(double L[ILS][ILS], double Z[ILS][ILS], int n, int i, int j) {
    double mu = round(L[i][j]);
    if (mu == 0) return;
    for (int k = i; k < n; k++) L[k][j] -= mu * L[k][i];
    for (int k = 0; k < n; k++) Z[k][j] -= mu * Z[k][i];
}

/* swap ambiguities j and j + 1 and update the factors */
static void ils_swap(double L[ILS][ILS], double *D, int n, int j, double del, double Z[ILS][ILS]) {
    double eta = D[j] / del, lam = D[j + 1] * L[j + 1][j] / del;
    D[j] = eta * D[j + 1];
    D[j + 1] = del;
    for (int k = 0; k < j; k++) {
        double a0 = L[j][k], a1 = L[j + 1][k];
        L[j][k] = -L[j + 1][j] * a0 + a1;
        L[j + 1][k] = eta * a0 + lam * a1;
    }
    L[j + 1][j] = lam;
    for (int k = j + 2; k < n; k++) {
        double t = L[k][j];
        L[k][j] = L[k][j + 1];
        L[k][j + 1] = t;
    }
    for (int k = 0; k < n; k++) {
        double t = Z[k][j];
        Z[k][j] = Z[k][j + 1];
        Z[k][j + 1] = t;
    }
}

/* decorrelate: Z unimodular, the conditional variances D ordered as far as
 * the swaps allow. 0, or -1 when it does not settle. */
static int ils_reduce(double L[ILS][ILS], double *D, int n, double Z[ILS][ILS]) {
    int j = n - 2, k = n - 2;
    for (long guard = 0; j >= 0; guard++) {
        if (guard > 100000) return -1;
        if (j <= k)
            for (int i = j + 1; i < n; i++) ils_gauss(L, Z, n, i, j);
        double del = D[j] + L[j + 1][j] * L[j + 1][j] * D[j + 1];
        if (del + 1e-6 < D[j + 1]) {
            ils_swap(L, D, n, j, del, Z);
            k = j;
            j = n - 2;
        } else
            j--;
    }
    return 0;
}

static double sgn(double y) { return y <= 0 ? -1 : 1; }

/* the two integer vectors nearest zs in the metric of L, D: a depth-first
 * search that shrinks its ellipsoid as candidates are found. 0, or -1 when
 * it runs too long. */
static int ils_search(int n, double L[ILS][ILS], const double *D, const double *zs, double zc[2][ILS], double *s) {
    double S[ILS][ILS], dist[ILS], zb[ILS], z[ILS], step[ILS];
    memset(S, 0, sizeof S);
    int k = n - 1, c = 0, imax = 0;
    double maxdist = INFINITY;
    dist[k] = 0;
    zb[k] = zs[k];
    z[k] = round(zb[k]);
    double y = zb[k] - z[k];
    step[k] = sgn(y);
    for (long it = 0;; it++) {
        if (it > 5000000) return -1;
        double nd = dist[k] + y * y / D[k];
        if (nd < maxdist) {
            if (k != 0) {
                dist[--k] = nd;
                for (int i = 0; i <= k; i++) S[k][i] = S[k + 1][i] + (z[k + 1] - zb[k + 1]) * L[k + 1][i];
                zb[k] = zs[k] + S[k][k];
                z[k] = round(zb[k]);
                y = zb[k] - z[k];
                step[k] = sgn(y);
            } else {
                if (c < 2) {
                    memcpy(zc[c], z, sizeof(double) * (size_t)n);
                    s[c++] = nd;
                    if (c == 2) {
                        imax = s[1] > s[0];
                        maxdist = s[imax];
                    }
                } else if (nd < s[imax]) {
                    memcpy(zc[imax], z, sizeof(double) * (size_t)n);
                    s[imax] = nd;
                    imax = s[1] > s[0];
                    maxdist = s[imax];
                }
                z[0] += step[0];
                y = zb[0] - z[0];
                step[0] = -step[0] - sgn(step[0]);
            }
        } else {
            if (k == n - 1) break;
            k++;
            z[k] += step[k];
            y = zb[k] - z[k];
            step[k] = -step[k] - sgn(step[k]);
        }
    }
    if (c < 2) return -1;
    if (s[1] < s[0]) {
        double t = s[0];
        s[0] = s[1];
        s[1] = t;
        for (int i = 0; i < n; i++) {
            t = zc[0][i];
            zc[0][i] = zc[1][i];
            zc[1][i] = t;
        }
    }
    return 0;
}

int sqg_ils(const double *a, const double *q, int n, long *best, double *ratio, double *dist) {
    if (n < 1 || n > ILS) return -1;
    for (int i = 0; i < n; i++)
        if (!isfinite(a[i])) return -1;
    double L[ILS][ILS], D[ILS], Z[ILS][ILS];
    if (ltdl(q, n, L, D)) return -1;
    if (n == 1) {
        /* one ambiguity: the nearest integer and the next */
        double r = round(a[0]), y = a[0] - r;
        best[0] = (long)r;
        double s0 = y * y / q[0], s1 = (1 - fabs(y)) * (1 - fabs(y)) / q[0];
        if (dist) dist[0] = s0, dist[1] = s1;
        if (ratio) *ratio = s1 / fmax(s0, 1e-12);
        return 0;
    }
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) Z[i][j] = i == j;
    if (ils_reduce(L, D, n, Z)) return -1;
    /* the float ambiguities in the decorrelated space: z = Z' a, about
     * whole numbers so they lose nothing to rounding */
    double zs[ILS], off[ILS];
    for (int i = 0; i < n; i++) off[i] = round(a[i]);
    for (int i = 0; i < n; i++) {
        zs[i] = 0;
        for (int k = 0; k < n; k++) zs[i] += Z[k][i] * (a[k] - off[k]);
    }
    double zc[2][ILS], s[2];
    if (ils_search(n, L, D, zs, zc, s)) return -1;
    /* back: Z' x = z, solved exactly (Z is whole numbers with determinant +-1) */
    double M[ILS][ILS + 1];
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < n; k++) M[i][k] = Z[k][i];
        M[i][n] = zc[0][i];
    }
    for (int c = 0; c < n; c++) {
        int p = c;
        for (int r = c + 1; r < n; r++)
            if (fabs(M[r][c]) > fabs(M[p][c])) p = r;
        if (fabs(M[p][c]) < 1e-9) return -1;
        if (p != c)
            for (int k = 0; k <= n; k++) {
                double t = M[c][k];
                M[c][k] = M[p][k];
                M[p][k] = t;
            }
        for (int r = 0; r < n; r++) {
            if (r == c || M[r][c] == 0) continue;
            double f = M[r][c] / M[c][c];
            for (int k = c; k <= n; k++) M[r][k] -= f * M[c][k];
        }
    }
    for (int i = 0; i < n; i++) best[i] = (long)off[i] + lround(M[i][n] / M[i][i]);
    if (dist) dist[0] = s[0], dist[1] = s[1];
    if (ratio) *ratio = s[1] / fmax(s[0], 1e-12);
    return 0;
}

/* ---- static ---- */

/* everything one solution needs, per epoch and double difference */
typedef struct {
    int n_epochs, n_dd, n_sat;
    int prn[SQG_MAX_SATS]; /* index 0 is the reference */
    double cbb[SQG_MAX_EPOCHS], cbr[SQG_MAX_EPOCHS];
    int dual;              /* L2 rows, with the wide lanes fixed */
    long wl[SQG_MAX_SATS];
} Setup;

typedef struct {
    double nm[MAXU][MAXU], rhs[MAXU];
    double sse_phase; /* unweighted m^2 */
    int rows_phase;
} Normal;

static void accumulate(const Setup *su, const SqgEpoch *base, const SqgEpoch *rover, const SqgEphemeris *ephs,
                       int n_eph, SqgEcef base_pos, SqgEcef x, const double *amb, int with_amb,
                       const SqgRtkOptions *o, Normal *nq) {
    memset(nq, 0, sizeof *nq);
    int nu = with_amb ? 3 + su->n_dd : 3;
    Row rows[SQG_MAX_SATS];
    for (int k = 0; k < su->n_epochs; k++) {
        if (dd_rows(su->prn, su->n_sat, &base[k], &rover[k], ephs, n_eph, base_pos, x, su->cbb[k], su->cbr[k],
                    rows) < 0)
            continue;
        for (int j = 0; j < su->n_dd; j++)
            if (rows[j].ok)
                add_row(&rows[j], j, su->dual, su->wl[j], with_amb ? 0 : amb[j], with_amb, o, nq->nm, nq->rhs, nu,
                        &nq->sse_phase, &nq->rows_phase);
    }
    if (with_amb) {
        /* the residuals against the current ambiguities, for the RMS */
        nq->sse_phase = 0;
        nq->rows_phase = 0;
        double dn[MAXU][MAXU], dr[MAXU];
        for (int k = 0; k < su->n_epochs; k++) {
            if (dd_rows(su->prn, su->n_sat, &base[k], &rover[k], ephs, n_eph, base_pos, x, su->cbb[k], su->cbr[k],
                        rows) < 0)
                continue;
            for (int j = 0; j < su->n_dd; j++)
                if (rows[j].ok) {
                    memset(dn, 0, sizeof dn);
                    memset(dr, 0, sizeof dr);
                    add_row(&rows[j], j, su->dual, su->wl[j], amb[j], 0, o, dn, dr, 0, &nq->sse_phase,
                            &nq->rows_phase);
                }
        }
    }
}

/* Gauss-Newton on the rover (and the ambiguities when with_amb, as absolute
 * unknowns); the inverse normal matrix stays in nq->nm */
static int iterate(const Setup *su, const SqgEpoch *base, const SqgEpoch *rover, const SqgEphemeris *ephs, int n_eph,
                   SqgEcef base_pos, SqgEcef *x, double *amb, int with_amb, const SqgRtkOptions *o, Normal *nq) {
    int nu = with_amb ? 3 + su->n_dd : 3;
    for (int it = 0; it < 20; it++) {
        accumulate(su, base, rover, ephs, n_eph, base_pos, *x, amb, with_amb, o, nq);
        /* with absolute ambiguities the right-hand side holds A'W(obs - model): move it to a step */
        if (with_amb)
            for (int a = 0; a < nu; a++)
                for (int j = 0; j < su->n_dd; j++) nq->rhs[a] -= nq->nm[a][3 + j] * amb[j];
        if (solve_sym(nq->nm, nu, nq->rhs)) return -1;
        x->x += nq->rhs[0];
        x->y += nq->rhs[1];
        x->z += nq->rhs[2];
        double step = fabs(nq->rhs[0]) + fabs(nq->rhs[1]) + fabs(nq->rhs[2]);
        for (int j = 0; with_amb && j < su->n_dd; j++) amb[j] += nq->rhs[3 + j];
        if (step < 1e-7) break;
    }
    /* the sums at the solution, and the inverse normal for the caller */
    accumulate(su, base, rover, ephs, n_eph, base_pos, *x, amb, with_amb, o, nq);
    double sse = nq->sse_phase;
    int rows = nq->rows_phase;
    if (solve_sym(nq->nm, nu, nq->rhs)) return -1;
    nq->sse_phase = sse;
    nq->rows_phase = rows;
    return 0;
}

int sqg_rtk_static(const SqgEpoch *base, const SqgEpoch *rover, int n_epochs, const SqgEphemeris *ephs, int n_eph,
                   SqgEcef base_pos, const SqgRtkOptions *opt, SqgRtk *out) {
    static Setup su; /* the clocks of up to SQG_MAX_EPOCHS epochs */
    static Normal nq;
    SqgRtkOptions def;
    if (!opt) {
        sqg_rtk_options_default(&def);
        opt = &def;
    }
    memset(out, 0, sizeof *out);
    if (n_epochs < 1 || n_epochs > SQG_MAX_EPOCHS) return -1;
    memset(&su, 0, sizeof su);
    su.n_epochs = n_epochs;
    SqgGeodetic bl;
    sqg_ecef_to_geodetic(base_pos, &bl);
    /* satellites with carrier at both receivers in every epoch, above the
     * mask at the base in the first; the highest is the reference */
    SqgEcef start;
    int n0;
    if (sqg_dgnss(&base[0], &rover[0], ephs, n_eph, base_pos, opt, &start, &n0)) return -1;
    for (int k = 0; k < n_epochs; k++) {
        su.cbb[k] = clock_of(&base[k], ephs, n_eph, base_pos);
        su.cbr[k] = clock_of(&rover[k], ephs, n_eph, start);
    }
    double best = -90;
    int ref = -1, all_l2 = opt->dual;
    for (int i = 0; i < base[0].n_obs && su.n_sat < SQG_MAX_SATS; i++) {
        const SqgObservation *b = &base[0].obs[i];
        const SqgEphemeris *e = b->valid ? find_eph(ephs, n_eph, b->prn) : NULL;
        if (!e) continue;
        int all = 1, l2 = 1;
        for (int k = 0; k < n_epochs && all; k++) {
            const SqgObservation *x = find_obs(&base[k], b->prn), *y = find_obs(&rover[k], b->prn);
            all = x && y && x->has_carrier && y->has_carrier;
            l2 = l2 && all && x->has_l2 && y->has_l2;
        }
        if (!all) continue;
        double u[3];
        range_to(&base[0], b, e, base_pos, su.cbb[0], u);
        SqgEcef far = {base_pos.x + u[0] * 2e7, base_pos.y + u[1] * 2e7, base_pos.z + u[2] * 2e7};
        double el = sqg_az_el(base_pos, bl, far).el_deg;
        if (el < opt->mask_deg) continue;
        if (el > best) {
            best = el;
            ref = su.n_sat;
        }
        all_l2 = all_l2 && l2;
        su.prn[su.n_sat++] = b->prn;
    }
    if (su.n_sat < 5) return -1;
    int t = su.prn[0];
    su.prn[0] = su.prn[ref];
    su.prn[ref] = t;
    su.n_dd = su.n_sat - 1;
    out->n_epochs = n_epochs;
    out->ref_prn = su.prn[0];
    out->n_dd = su.n_dd;
    for (int j = 0; j < su.n_dd; j++) out->prn[j] = su.prn[j + 1];

    /* the wide lanes from the Melbourne-Wubbena average; L2 joins in only
     * when every one of them rounds cleanly */
    Row rows[SQG_MAX_SATS];
    if (all_l2) {
        double sum[SQG_MAX_SATS] = {0}, sq[SQG_MAX_SATS] = {0}, ref0[SQG_MAX_SATS] = {0};
        for (int k = 0; k < n_epochs; k++) {
            dd_rows(su.prn, su.n_sat, &base[k], &rover[k], ephs, n_eph, base_pos, start, su.cbb[k], su.cbr[k], rows);
            for (int j = 0; j < su.n_dd; j++) {
                double m = mw_cycles(&rows[j]);
                if (k == 0) ref0[j] = round(m);
                m -= ref0[j]; /* about the first value, so the squares keep their bits */
                sum[j] += m;
                sq[j] += m * m;
            }
        }
        int clean = 1;
        for (int j = 0; j < su.n_dd; j++) {
            double m = sum[j] / n_epochs;
            su.wl[j] = lround(m) + (long)ref0[j];
            out->wl[j] = su.wl[j];
            if (fabs(m - round(m)) > 0.25 || wl_sd(sum[j], sq[j], n_epochs, opt) > 0.15) clean = 0;
        }
        su.dual = out->wl_fixed = clean;
    }

    /* float: ambiguities and rover together */
    double amb[SQG_MAX_SATS] = {0};
    SqgEcef x = start;
    if (dd_rows(su.prn, su.n_sat, &base[0], &rover[0], ephs, n_eph, base_pos, x, su.cbb[0], su.cbr[0], rows) < 0)
        return -1;
    for (int j = 0; j < su.n_dd; j++) amb[j] = rows[j].l1 - rows[j].model / SQG_L1_LAMBDA;
    if (iterate(&su, base, rover, ephs, n_eph, base_pos, &x, amb, 1, opt, &nq)) return -1;
    out->dual = su.dual;
    out->float_rover = out->rover = x;
    out->rms_phase_m = nq.rows_phase ? sqrt(nq.sse_phase / nq.rows_phase) : 0;
    double q[SQG_MAX_SATS * SQG_MAX_SATS] = {0};
    for (int i = 0; i < su.n_dd; i++) {
        out->float_amb[i] = amb[i];
        out->amb[i] = lround(amb[i]);
        for (int j = 0; j < su.n_dd; j++) q[i * su.n_dd + j] = nq.nm[3 + i][3 + j];
    }

    /* fixed: the integer set nearest the float one in its own metric */
    long fixed[SQG_MAX_SATS];
    if (sqg_ils(amb, q, su.n_dd, fixed, &out->ratio, NULL)) return 0;
    double fixed_amb[SQG_MAX_SATS];
    for (int j = 0; j < su.n_dd; j++) fixed_amb[j] = (double)fixed[j];
    SqgEcef xf = x;
    if (iterate(&su, base, rover, ephs, n_eph, base_pos, &xf, fixed_amb, 0, opt, &nq)) return 0;
    if (out->ratio >= opt->ratio_min) {
        out->fixed = 1;
        out->rover = xf;
        for (int j = 0; j < su.n_dd; j++) out->amb[j] = fixed[j];
        out->rms_phase_m = nq.rows_phase ? sqrt(nq.sse_phase / nq.rows_phase) : 0;
    }
    return 0;
}

/* ---- kinematic ---- */

void sqg_rtk_filter_init(SqgRtkFilter *f, SqgEcef base_pos, const SqgRtkOptions *opt) {
    memset(f, 0, sizeof *f);
    if (opt) f->opt = *opt;
    else sqg_rtk_options_default(&f->opt);
    f->base_pos = base_pos;
}

/* forget ambiguity j: take it out of the information matrix (keeping what
 * the others know without it), then start it from nothing */
static void forget(SqgRtkFilter *f, int j) {
    int n = f->n_sat - 1;
    double d = f->info[j][j];
    if (d > 0)
        for (int a = 0; a < n; a++) {
            if (a == j) continue;
            for (int c = 0; c < n; c++)
                if (c != j) f->info[a][c] -= f->info[a][j] * f->info[j][c] / d;
            f->rhs[a] -= f->info[a][j] * f->rhs[j] / d;
        }
    for (int a = 0; a < n; a++) f->info[a][j] = f->info[j][a] = 0;
    f->rhs[j] = 0;
    f->mw_sum[j] = f->mw_sq[j] = 0;
    f->mw_n[j] = 0;
    f->wl_ok[j] = 0;
    f->gf_have[j] = 0;
    f->fixed = 0;
}

/* a new satellite set, from what both receivers see with carrier now */
static int restart(SqgRtkFilter *f, const SqgEpoch *base, const SqgEpoch *rover, const SqgEphemeris *ephs, int n_eph,
                   double cbb) {
    SqgRtkOptions o = f->opt;
    SqgEcef bp = f->base_pos;
    SqgGeodetic bl;
    sqg_ecef_to_geodetic(bp, &bl);
    memset(f, 0, sizeof *f);
    f->opt = o;
    f->base_pos = bp;
    double best = -90;
    int ref = -1;
    for (int i = 0; i < base->n_obs && f->n_sat < SQG_MAX_SATS; i++) {
        const SqgObservation *b = &base->obs[i], *r = find_obs(rover, b->prn);
        const SqgEphemeris *e = b->valid ? find_eph(ephs, n_eph, b->prn) : NULL;
        if (!e || !r || !b->has_carrier || !r->has_carrier) continue;
        double u[3];
        range_to(base, b, e, bp, cbb, u);
        SqgEcef far = {bp.x + u[0] * 2e7, bp.y + u[1] * 2e7, bp.z + u[2] * 2e7};
        double el = sqg_az_el(bp, bl, far).el_deg;
        if (el < o.mask_deg) continue;
        if (el > best) {
            best = el;
            ref = f->n_sat;
        }
        f->prn[f->n_sat++] = b->prn;
    }
    if (f->n_sat < 5) {
        f->n_sat = 0;
        return -1;
    }
    int t = f->prn[0];
    f->prn[0] = f->prn[ref];
    f->prn[ref] = t;
    f->started = 1;
    return 0;
}

/* the rover in one epoch given the ambiguities: least squares on code and
 * carrier, re-linearised until it settles; returns the carrier RMS (m) */
static double position_fixed(const SqgRtkFilter *f, const SqgEpoch *b, const SqgEpoch *r, const SqgEphemeris *ephs,
                             int n_eph, double cbb, double cbr, const double *amb, SqgEcef *x) {
    Row rows[SQG_MAX_SATS];
    double rms = INFINITY;
    for (int it = 0; it < 6; it++) {
        if (dd_rows(f->prn, f->n_sat, b, r, ephs, n_eph, f->base_pos, *x, cbb, cbr, rows) < 0) return INFINITY;
        double nm[MAXU][MAXU] = {{0}}, rhs[MAXU] = {0}, sse = 0;
        int nr = 0;
        for (int j = 0; j < f->n_sat - 1; j++)
            if (rows[j].ok) add_row(&rows[j], j, f->wl_ok[j], f->wl[j], amb[j], 0, &f->opt, nm, rhs, 3, &sse, &nr);
        if (nr < 4 || solve_sym(nm, 3, rhs)) return INFINITY;
        x->x += rhs[0];
        x->y += rhs[1];
        x->z += rhs[2];
        rms = sqrt(sse / nr);
        if (fabs(rhs[0]) + fabs(rhs[1]) + fabs(rhs[2]) < 1e-6) break;
    }
    return rms;
}

int sqg_rtk_filter_push(SqgRtkFilter *f, const SqgEpoch *base, const SqgEpoch *rover, const SqgEphemeris *ephs,
                        int n_eph, SqgRtkFix *out) {
    memset(out, 0, sizeof *out);
    out->t = rover->t_receiver;
    SqgEcef x;
    int n0;
    if (sqg_dgnss(base, rover, ephs, n_eph, f->base_pos, &f->opt, &x, &n0)) return -1;
    double cbb = clock_of(base, ephs, n_eph, f->base_pos), cbr = clock_of(rover, ephs, n_eph, x);
    Row rows[SQG_MAX_SATS];
    int got = f->started ? dd_rows(f->prn, f->n_sat, base, rover, ephs, n_eph, f->base_pos, x, cbb, cbr, rows) : -1;
    const SqgObservation *b0 = f->started ? find_obs(base, f->prn[0]) : NULL;
    const SqgObservation *r0 = f->started ? find_obs(rover, f->prn[0]) : NULL;
    if (got < 4 || (b0 && b0->slip) || (r0 && r0->slip)) {
        /* no reference, too few left, or the reference slipped: start over */
        if (restart(f, base, rover, ephs, n_eph, cbb)) return -1;
        out->reset = 1;
        got = dd_rows(f->prn, f->n_sat, base, rover, ephs, n_eph, f->base_pos, x, cbb, cbr, rows);
        if (got < 4) return -1;
    }
    int n = f->n_sat - 1;
    f->epochs++;

    /* slips: flagged by a receiver, or a jump in the geometry-free carrier */
    for (int j = 0; j < n; j++) {
        if (!rows[j].ok) continue;
        int slip = rows[j].slip;
        if (rows[j].has2) {
            double g = gf_m(&rows[j]);
            if (f->gf_have[j] && fabs(g - f->gf_last[j]) > 0.05) slip = 1;
            f->gf_last[j] = g;
            f->gf_have[j] = 1;
        }
        if (slip && f->epochs > 1) {
            forget(f, j);
            if (rows[j].has2) {
                f->gf_last[j] = gf_m(&rows[j]);
                f->gf_have[j] = 1;
            }
            out->slips++;
        }
    }

    /* wide lanes: averaged until the mean is good to a few hundredths */
    if (f->opt.dual)
        for (int j = 0; j < n; j++) {
            if (!rows[j].ok || !rows[j].has2 || f->wl_ok[j]) continue;
            double v = mw_cycles(&rows[j]);
            if (f->mw_n[j] == 0) f->mw_ref[j] = round(v);
            v -= f->mw_ref[j]; /* about its first value, so the squares keep their bits */
            f->mw_sum[j] += v;
            f->mw_sq[j] += v * v;
            f->mw_n[j]++;
            double m = f->mw_sum[j] / f->mw_n[j];
            if (wl_sd(f->mw_sum[j], f->mw_sq[j], f->mw_n[j], &f->opt) < 0.15 && fabs(m - round(m)) < 0.25) {
                f->wl[j] = lround(m) + (long)f->mw_ref[j];
                f->wl_ok[j] = 1;
            }
        }

    /* this epoch's normals over [rover, ambiguities], the rover eliminated
     * into the ambiguity information */
    double nm[MAXU][MAXU] = {{0}}, rhs[MAXU] = {0}, sse = 0;
    int nr = 0, nu = 3 + n;
    for (int j = 0; j < n; j++)
        if (rows[j].ok) add_row(&rows[j], j, f->wl_ok[j], f->wl[j], 0, 1, &f->opt, nm, rhs, nu, &sse, &nr);
    double pp[MAXU][MAXU] = {{0}}, bp[MAXU] = {rhs[0], rhs[1], rhs[2]};
    for (int a = 0; a < 3; a++)
        for (int c = 0; c < 3; c++) pp[a][c] = nm[a][c];
    if (solve_sym(pp, 3, bp)) return -1; /* pp is now the inverse */
    for (int a = 0; a < n; a++) {
        double t[3];
        for (int c = 0; c < 3; c++) t[c] = nm[3 + a][0] * pp[0][c] + nm[3 + a][1] * pp[1][c] + nm[3 + a][2] * pp[2][c];
        for (int c = 0; c < n; c++) f->info[a][c] += nm[3 + a][3 + c] - (t[0] * nm[0][3 + c] + t[1] * nm[1][3 + c] + t[2] * nm[2][3 + c]);
        f->rhs[a] += rhs[3 + a] - (t[0] * rhs[0] + t[1] * rhs[1] + t[2] * rhs[2]);
    }

    /* the float ambiguities and their covariance (a faint prior keeps an
     * ambiguity with no data yet from making it singular) */
    double qi[MAXU][MAXU], amb[MAXU];
    for (int a = 0; a < n; a++) {
        for (int c = 0; c < n; c++) qi[a][c] = f->info[a][c];
        qi[a][a] += 1e-8;
        amb[a] = f->rhs[a];
    }
    if (solve_sym(qi, n, amb)) return -1;
    double q[SQG_MAX_SATS * SQG_MAX_SATS];
    int have_all = 1;
    for (int a = 0; a < n; a++) {
        for (int c = 0; c < n; c++) q[a * n + c] = qi[a][c];
        if (f->info[a][a] == 0) have_all = 0;
    }
    if (!f->fixed && have_all) {
        long cand[SQG_MAX_SATS];
        double ratio;
        if (sqg_ils(amb, q, n, cand, &ratio, NULL) == 0) {
            f->ratio = ratio;
            if (ratio >= f->opt.ratio_min) {
                f->fixed = 1;
                memcpy(f->fixed_amb, cand, sizeof(long) * (size_t)n);
            }
        }
    }
    out->ratio = f->ratio;
    out->n_dd = got;
    out->wide_lanes = 0;
    for (int j = 0; j < n; j++) out->wide_lanes += f->wl_ok[j];

    if (f->fixed) {
        double fa[MAXU];
        for (int j = 0; j < n; j++) fa[j] = (double)f->fixed_amb[j];
        SqgEcef xf = x;
        double rms = position_fixed(f, base, rover, ephs, n_eph, cbb, cbr, fa, &xf);
        /* a wrong fix shows as carrier that no longer fits: drop it */
        if (rms < 6 * f->opt.sigma_phase) {
            out->fixed = 1;
            out->pos = xf;
            out->rms_phase_m = rms;
            f->last = xf;
            return 0;
        }
        f->fixed = 0;
        out->dropped = 1;
    }
    /* float: the rover given the float ambiguities */
    double fa[MAXU];
    for (int j = 0; j < n; j++) fa[j] = amb[j];
    SqgEcef xf = x;
    out->rms_phase_m = position_fixed(f, base, rover, ephs, n_eph, cbb, cbr, fa, &xf);
    out->pos = xf;
    f->last = xf;
    return 0;
}
