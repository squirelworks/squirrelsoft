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

/* everything one solution needs, per epoch and double difference */
typedef struct {
    int n_epochs, n_dd, n_sat;
    int prn[SQG_MAX_SATS]; /* index 0 is the reference */
    double cbb[SQG_MAX_EPOCHS], cbr[SQG_MAX_EPOCHS];
} Setup;

/* the double differences at rover position x: phase (m) minus model,
 * code minus model, and their partials; fn is called per row */
typedef struct {
    double nm[MAXU][MAXU], rhs[MAXU];
    double sse_phase; /* unweighted m^2 */
    int rows_phase;
} Normal;

static void accumulate(const Setup *su, const SqgEpoch *base, const SqgEpoch *rover, const SqgEphemeris *ephs,
                       int n_eph, SqgEcef base_pos, SqgEcef x, const double *amb, int with_amb,
                       const SqgRtkOptions *o, Normal *nq) {
    memset(nq, 0, sizeof *nq);
    double wp = 1 / (o->sigma_phase * o->sigma_phase), wc = 1 / (o->sigma_code * o->sigma_code);
    int nu = with_amb ? 3 + su->n_dd : 3;
    for (int k = 0; k < su->n_epochs; k++) {
        const SqgEpoch *b = &base[k], *r = &rover[k];
        double ub0[3], ur0[3], ub[3], ur[3];
        const SqgEphemeris *e0 = find_eph(ephs, n_eph, su->prn[0]);
        const SqgObservation *b0 = find_obs(b, su->prn[0]), *r0 = find_obs(r, su->prn[0]);
        double rb0 = range_to(b, b0, e0, base_pos, su->cbb[k], ub0), rr0 = range_to(r, r0, e0, x, su->cbr[k], ur0);
        for (int j = 1; j < su->n_sat; j++) {
            const SqgEphemeris *e = find_eph(ephs, n_eph, su->prn[j]);
            const SqgObservation *bj = find_obs(b, su->prn[j]), *rj = find_obs(r, su->prn[j]);
            double rb = range_to(b, bj, e, base_pos, su->cbb[k], ub), rr = range_to(r, rj, e, x, su->cbr[k], ur);
            double model = (rr - rr0) - (rb - rb0);
            double h[MAXU] = {-ur[0] + ur0[0], -ur[1] + ur0[1], -ur[2] + ur0[2]};
            /* code */
            double code = (rj->pseudorange_m - r0->pseudorange_m) - (bj->pseudorange_m - b0->pseudorange_m);
            for (int a = 0; a < 3; a++) {
                nq->rhs[a] += wc * h[a] * (code - model);
                for (int c = 0; c < 3; c++) nq->nm[a][c] += wc * h[a] * h[c];
            }
            /* carrier, in metres */
            double phase = ((rj->carrier_cyc - r0->carrier_cyc) - (bj->carrier_cyc - b0->carrier_cyc)) * SQG_L1_LAMBDA;
            double v = phase - model - SQG_L1_LAMBDA * amb[j - 1];
            if (with_amb) h[3 + j - 1] = SQG_L1_LAMBDA;
            for (int a = 0; a < nu; a++) {
                if (h[a] == 0) continue;
                nq->rhs[a] += wp * h[a] * v;
                for (int c = 0; c < nu; c++) nq->nm[a][c] += wp * h[a] * h[c];
            }
            nq->sse_phase += v * v;
            nq->rows_phase++;
        }
    }
}

/* Gauss-Newton on the rover (and the ambiguities when with_amb); the
 * inverse normal matrix stays in nq->nm */
static int iterate(const Setup *su, const SqgEpoch *base, const SqgEpoch *rover, const SqgEphemeris *ephs, int n_eph,
                   SqgEcef base_pos, SqgEcef *x, double *amb, int with_amb, const SqgRtkOptions *o, Normal *nq) {
    int nu = with_amb ? 3 + su->n_dd : 3;
    for (int it = 0; it < 20; it++) {
        accumulate(su, base, rover, ephs, n_eph, base_pos, *x, amb, with_amb, o, nq);
        if (solve_sym(nq->nm, nu, nq->rhs)) return -1;
        x->x += nq->rhs[0];
        x->y += nq->rhs[1];
        x->z += nq->rhs[2];
        double step = fabs(nq->rhs[0]) + fabs(nq->rhs[1]) + fabs(nq->rhs[2]);
        for (int j = 0; with_amb && j < su->n_dd; j++) amb[j] += nq->rhs[3 + j];
        if (step < 1e-7) break;
    }
    /* the sums at the solution (and its inverse normal for the caller) */
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
    int ref = -1;
    for (int i = 0; i < base[0].n_obs && su.n_sat < SQG_MAX_SATS; i++) {
        const SqgObservation *b = &base[0].obs[i];
        const SqgEphemeris *e = b->valid ? find_eph(ephs, n_eph, b->prn) : NULL;
        if (!e) continue;
        int all = 1;
        for (int k = 0; k < n_epochs && all; k++) {
            const SqgObservation *x = find_obs(&base[k], b->prn), *y = find_obs(&rover[k], b->prn);
            all = x && y && x->has_carrier && y->has_carrier;
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
        su.prn[su.n_sat++] = b->prn;
    }
    if (su.n_sat < 5) return -1;
    int t = su.prn[0];
    su.prn[0] = su.prn[ref];
    su.prn[ref] = t;
    su.n_dd = su.n_sat - 1;

    /* float: ambiguities start from phase minus the code start's ranges */
    double amb[SQG_MAX_SATS] = {0};
    SqgEcef x = start;
    {
        double ub0[3], ur0[3], ub[3], ur[3];
        const SqgEpoch *b = &base[0], *r = &rover[0];
        const SqgEphemeris *e0 = find_eph(ephs, n_eph, su.prn[0]);
        const SqgObservation *b0 = find_obs(b, su.prn[0]), *r0 = find_obs(r, su.prn[0]);
        double rb0 = range_to(b, b0, e0, base_pos, su.cbb[0], ub0), rr0 = range_to(r, r0, e0, x, su.cbr[0], ur0);
        for (int j = 1; j < su.n_sat; j++) {
            const SqgEphemeris *e = find_eph(ephs, n_eph, su.prn[j]);
            const SqgObservation *bj = find_obs(b, su.prn[j]), *rj = find_obs(r, su.prn[j]);
            double model = (range_to(r, rj, e, x, su.cbr[0], ur) - rr0) - (range_to(b, bj, e, base_pos, su.cbb[0], ub) - rb0);
            double phase = (rj->carrier_cyc - r0->carrier_cyc) - (bj->carrier_cyc - b0->carrier_cyc);
            amb[j - 1] = phase - model / SQG_L1_LAMBDA;
        }
    }
    if (iterate(&su, base, rover, ephs, n_eph, base_pos, &x, amb, 1, opt, &nq)) return -1;
    out->n_epochs = n_epochs;
    out->ref_prn = su.prn[0];
    out->n_dd = su.n_dd;
    out->float_rover = out->rover = x;
    out->rms_phase_m = sqrt(nq.sse_phase / nq.rows_phase);
    double far_off = 0;
    for (int j = 0; j < su.n_dd; j++) {
        out->prn[j] = su.prn[j + 1];
        out->float_amb[j] = amb[j];
        out->amb[j] = lround(amb[j]);
        far_off = fmax(far_off, fabs(amb[j] - (double)out->amb[j]));
    }

    /* fixed: the rounded set, then each ambiguity one cycle toward its float
     * value for the next best */
    double fixed_amb[SQG_MAX_SATS];
    for (int j = 0; j < su.n_dd; j++) fixed_amb[j] = (double)out->amb[j];
    SqgEcef xf = x;
    if (iterate(&su, base, rover, ephs, n_eph, base_pos, &xf, fixed_amb, 0, opt, &nq)) return 0;
    double sse1 = nq.sse_phase, rows = nq.rows_phase, sse2 = INFINITY;
    for (int j = 0; j < su.n_dd; j++) {
        double trial[SQG_MAX_SATS];
        memcpy(trial, fixed_amb, sizeof trial);
        trial[j] += amb[j] > fixed_amb[j] ? 1 : -1;
        SqgEcef xt = x;
        if (iterate(&su, base, rover, ephs, n_eph, base_pos, &xt, trial, 0, opt, &nq) == 0)
            sse2 = fmin(sse2, nq.sse_phase);
    }
    /* a floor of the phase noise, so noiseless data does not divide by 0 */
    double floor_sse = rows * (opt->sigma_phase * opt->sigma_phase) * 1e-4;
    out->ratio = sse2 / fmax(sse1, floor_sse);
    if (out->ratio >= opt->ratio_min && far_off < 0.3) {
        out->fixed = 1;
        out->rover = xf;
        out->rms_phase_m = sqrt(sse1 / rows);
    }
    return 0;
}
