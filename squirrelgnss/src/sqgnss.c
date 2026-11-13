/* sqgnss: GNSS positioning core (see sqgnss.h). Original code: the orbit and
 * clock model follows the published GPS interface specification formulas. */
#include "sqgnss.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265358979323846
#define DEG (PI / 180.0)

static double e2(void) { return SQG_WGS84_F * (2.0 - SQG_WGS84_F); }

/* ---- geodesy ---- */

void sqg_geodetic_to_ecef(SqgGeodetic g, SqgEcef *out) {
    double lat = g.lat_deg * DEG, lon = g.lon_deg * DEG;
    double s = sin(lat), c = cos(lat);
    double n = SQG_WGS84_A / sqrt(1.0 - e2() * s * s);
    out->x = (n + g.height_m) * c * cos(lon);
    out->y = (n + g.height_m) * c * sin(lon);
    out->z = (n * (1.0 - e2()) + g.height_m) * s;
}

void sqg_ecef_to_geodetic(SqgEcef p, SqgGeodetic *out) {
    double ee = e2(), b = SQG_WGS84_A * (1.0 - SQG_WGS84_F);
    double rho = sqrt(p.x * p.x + p.y * p.y);
    out->lon_deg = rho > 0 ? atan2(p.y, p.x) / DEG : 0.0;
    if (rho < 1e-9) { /* on the axis */
        out->lat_deg = p.z >= 0 ? 90.0 : -90.0;
        out->height_m = fabs(p.z) - b;
        return;
    }
    double lat = atan2(p.z, rho * (1.0 - ee));
    for (int i = 0; i < 12; i++) {
        double s = sin(lat), c = cos(lat);
        double w = sqrt(1.0 - ee * s * s), n = SQG_WGS84_A / w;
        /* this form of the height has no 1/cos(lat), so it holds at the poles */
        double h = rho * c + p.z * s - SQG_WGS84_A * w;
        double next = atan2(p.z, rho * (1.0 - ee * n / (n + h)));
        if (fabs(next - lat) < 1e-15) {
            lat = next;
            break;
        }
        lat = next;
    }
    double s = sin(lat);
    out->lat_deg = lat / DEG;
    out->height_m = rho * cos(lat) + p.z * s - SQG_WGS84_A * sqrt(1.0 - ee * s * s);
}

void sqg_ecef_to_enu(SqgEcef origin, SqgEcef point, SqgGeodetic origin_llh, SqgEnu *enu) {
    double lat = origin_llh.lat_deg * DEG, lon = origin_llh.lon_deg * DEG;
    double dx = point.x - origin.x, dy = point.y - origin.y, dz = point.z - origin.z;
    enu->e = -sin(lon) * dx + cos(lon) * dy;
    enu->n = -sin(lat) * cos(lon) * dx - sin(lat) * sin(lon) * dy + cos(lat) * dz;
    enu->u = cos(lat) * cos(lon) * dx + cos(lat) * sin(lon) * dy + sin(lat) * dz;
}

void sqg_enu_to_ecef(SqgEcef origin, SqgGeodetic origin_llh, SqgEnu enu, SqgEcef *out) {
    double lat = origin_llh.lat_deg * DEG, lon = origin_llh.lon_deg * DEG;
    /* the transpose of the rotation above */
    out->x = origin.x - sin(lon) * enu.e - sin(lat) * cos(lon) * enu.n + cos(lat) * cos(lon) * enu.u;
    out->y = origin.y + cos(lon) * enu.e - sin(lat) * sin(lon) * enu.n + cos(lat) * sin(lon) * enu.u;
    out->z = origin.z + cos(lat) * enu.n + sin(lat) * enu.u;
}

SqgAzEl sqg_az_el(SqgEcef rx, SqgGeodetic rx_llh, SqgEcef sat) {
    SqgEnu d;
    sqg_ecef_to_enu(rx, sat, rx_llh, &d);
    SqgAzEl r;
    r.az_deg = atan2(d.e, d.n) / DEG;
    if (r.az_deg < 0) r.az_deg += 360.0;
    r.el_deg = atan2(d.u, sqrt(d.e * d.e + d.n * d.n)) / DEG;
    return r;
}

/* ---- orbits ---- */

/* t - ref, folded into one week either way */
static double since(double t, double ref) {
    double d = t - ref;
    if (d > SQG_WEEK / 2) d -= SQG_WEEK;
    if (d < -SQG_WEEK / 2) d += SQG_WEEK;
    return d;
}

/* eccentric anomaly at t: Kepler's equation by Newton's method */
static double ecc_anomaly(const SqgEphemeris *eph, double tk) {
    double a = eph->sqrt_a * eph->sqrt_a;
    double n = sqrt(SQG_MU / (a * a * a)) + eph->delta_n;
    double m = eph->m0 + n * tk, e = m;
    for (int i = 0; i < 30; i++) {
        double d = (e - eph->e * sin(e) - m) / (1.0 - eph->e * cos(e));
        e -= d;
        if (fabs(d) < 1e-15) break;
    }
    return e;
}

SqgEcef sqg_sat_position(const SqgEphemeris *eph, double t_gps) {
    double a = eph->sqrt_a * eph->sqrt_a, tk = since(t_gps, eph->toe);
    double ek = ecc_anomaly(eph, tk), se = sin(ek), ce = cos(ek);
    double v = atan2(sqrt(1.0 - eph->e * eph->e) * se, ce - eph->e);
    double phi = v + eph->w, s2 = sin(2 * phi), c2 = cos(2 * phi);
    double u = phi + eph->cus * s2 + eph->cuc * c2;
    double r = a * (1.0 - eph->e * ce) + eph->crs * s2 + eph->crc * c2;
    double i = eph->i0 + eph->cis * s2 + eph->cic * c2 + eph->idot * tk;
    double xp = r * cos(u), yp = r * sin(u);
    double om = eph->omega0 + (eph->omega_dot - SQG_OMEGA_E) * tk - SQG_OMEGA_E * eph->toe;
    SqgEcef s = {xp * cos(om) - yp * cos(i) * sin(om), xp * sin(om) + yp * cos(i) * cos(om), yp * sin(i)};
    return s;
}

double sqg_sat_clock(const SqgEphemeris *eph, double t_gps) {
    double dt = since(t_gps, eph->toc);
    double ek = ecc_anomaly(eph, since(t_gps, eph->toe));
    double rel = SQG_F_REL * eph->e * eph->sqrt_a * sin(ek);
    return eph->af0 + eph->af1 * dt + eph->af2 * dt * dt + rel - eph->tgd;
}

/* ---- least squares ---- */

/* invert a 4x4 in place by Gauss-Jordan with pivoting; -1 if singular */
static int inv4(double m[4][4]) {
    double a[4][8];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            a[i][j] = m[i][j];
            a[i][j + 4] = i == j;
        }
    for (int c = 0; c < 4; c++) {
        int p = c;
        for (int r = c + 1; r < 4; r++)
            if (fabs(a[r][c]) > fabs(a[p][c])) p = r;
        if (fabs(a[p][c]) < 1e-14) return -1;
        for (int j = 0; j < 8; j++) {
            double t = a[c][j];
            a[c][j] = a[p][j];
            a[p][j] = t;
        }
        double d = a[c][c];
        for (int j = 0; j < 8; j++) a[c][j] /= d;
        for (int r = 0; r < 4; r++) {
            if (r == c) continue;
            double f = a[r][c];
            for (int j = 0; j < 8; j++) a[r][j] -= f * a[c][j];
        }
    }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) m[i][j] = a[i][j + 4];
    return 0;
}

void sqg_pvt_options_default(SqgPvtOptions *o) {
    o->elev_mask_deg = 10.0;
    o->weighted = 1;
    o->raim = 1;
    o->raim_limit_m = 15.0;
}

static int find_eph(const SqgEphemeris *ephs, int n, int prn) {
    for (int i = 0; i < n; i++)
        if (ephs[i].prn == prn) return i;
    return -1;
}

/* one solution, leaving out satellite skip (0: none) */
static int solve_core(const SqgEpoch *ep, const SqgEphemeris *ephs, int n_eph, SqgEcef approx,
                      const SqgPvtOptions *opt, int skip, SqgPvt *out) {
    memset(out, 0, sizeof *out);
    SqgEcef x = approx;
    double cb = 0;
    double h[SQG_MAX_SATS][4], res[SQG_MAX_SATS], w[SQG_MAX_SATS];
    int prn[SQG_MAX_SATS], used = 0;
    for (int iter = 0; iter < 30; iter++) {
        double radius = sqrt(x.x * x.x + x.y * x.y + x.z * x.z);
        int near = radius > 6.2e6 && radius < 6.6e6; /* near the ground: the mask applies */
        SqgGeodetic llh;
        sqg_ecef_to_geodetic(x, &llh);
        used = 0;
        for (int k = 0; k < ep->n_obs && used < SQG_MAX_SATS; k++) {
            const SqgObservation *o = &ep->obs[k];
            int ei = o->valid && o->prn != skip ? find_eph(ephs, n_eph, o->prn) : -1;
            if (ei < 0) continue;
            const SqgEphemeris *eph = &ephs[ei];
            /* transmit time, with the satellite clock read at that time */
            double t_tx = ep->t_receiver - o->pseudorange_m / SQG_C_LIGHT, dts = 0;
            for (int j = 0; j < 2; j++) dts = sqg_sat_clock(eph, t_tx - dts);
            t_tx -= dts;
            SqgEcef s = sqg_sat_position(eph, t_tx);
            /* the Earth turns while the signal flies */
            double tau = ep->t_receiver - cb / SQG_C_LIGHT - t_tx, a = SQG_OMEGA_E * tau;
            SqgEcef sr = {cos(a) * s.x + sin(a) * s.y, -sin(a) * s.x + cos(a) * s.y, s.z};
            double dx = sr.x - x.x, dy = sr.y - x.y, dz = sr.z - x.z;
            double rho = sqrt(dx * dx + dy * dy + dz * dz);
            if (rho < 1.0) continue;
            double wt = 1.0;
            if (near) {
                double el = sqg_az_el(x, llh, sr).el_deg;
                if (el < opt->elev_mask_deg) continue;
                if (opt->weighted) {
                    double se = sin((el > 5 ? el : 5) * DEG);
                    wt = se * se;
                }
            }
            h[used][0] = -dx / rho;
            h[used][1] = -dy / rho;
            h[used][2] = -dz / rho;
            h[used][3] = 1.0;
            res[used] = o->pseudorange_m - (rho + cb - SQG_C_LIGHT * dts);
            w[used] = wt;
            prn[used++] = o->prn;
        }
        out->n_used = used;
        if (used < 4) return -1;
        double n[4][4] = {{0}}, b[4] = {0};
        for (int i = 0; i < used; i++)
            for (int j = 0; j < 4; j++) {
                b[j] += w[i] * h[i][j] * res[i];
                for (int k = 0; k < 4; k++) n[j][k] += w[i] * h[i][j] * h[i][k];
            }
        if (inv4(n)) return -1;
        double d[4] = {0};
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++) d[j] += n[j][k] * b[k];
        x.x += d[0];
        x.y += d[1];
        x.z += d[2];
        cb += d[3];
        double step = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2] + d[3] * d[3]);
        if (step < 1e-4 && near) {
            out->converged = 1;
            break;
        }
    }
    out->pos = x;
    out->clock_bias_m = cb;
    for (int i = 0; i < used; i++) {
        out->used_prn[i] = prn[i];
        out->residual_m[i] = res[i];
    }
    if (!out->converged) return -1;

    /* residuals at the solution, and their RMS over the redundancy */
    double sse = 0; /* the last step was below 0.1 mm, so these stand */
    for (int i = 0; i < used; i++) sse += res[i] * res[i];
    out->rms_m = used > 4 ? sqrt(sse / (used - 4)) : 0;

    /* dilution of precision: the unweighted geometry, turned into east/north/up */
    double q[4][4] = {{0}};
    for (int i = 0; i < used; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 4; k++) q[j][k] += h[i][j] * h[i][k];
    if (inv4(q) == 0) {
        SqgGeodetic llh;
        sqg_ecef_to_geodetic(x, &llh);
        double lat = llh.lat_deg * DEG, lon = llh.lon_deg * DEG;
        double r[3][3] = {{-sin(lon), cos(lon), 0},
                          {-sin(lat) * cos(lon), -sin(lat) * sin(lon), cos(lat)},
                          {cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat)}};
        double qe[3] = {0}; /* diagonal of R Q R^T */
        for (int a = 0; a < 3; a++)
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < 3; k++) qe[a] += r[a][j] * q[j][k] * r[a][k];
        out->hdop = sqrt(fabs(qe[0] + qe[1]));
        out->vdop = sqrt(fabs(qe[2]));
        out->pdop = sqrt(fabs(q[0][0] + q[1][1] + q[2][2]));
        out->tdop = sqrt(fabs(q[3][3]));
        out->gdop = sqrt(fabs(q[0][0] + q[1][1] + q[2][2] + q[3][3]));
    }
    return 0;
}

int sqg_solve_pvt_opt(const SqgEpoch *epoch, const SqgEphemeris *ephs, int n_eph, SqgEcef approx,
                      const SqgPvtOptions *opt, SqgPvt *out) {
    SqgPvtOptions def;
    if (!opt) {
        sqg_pvt_options_default(&def);
        opt = &def;
    }
    int rc = solve_core(epoch, ephs, n_eph, approx, opt, 0, out);
    if (rc || !opt->raim || out->rms_m <= opt->raim_limit_m) return rc;
    /* a fault: with two or more spare satellites, try leaving out each one */
    if (out->n_used < 6) {
        out->excluded_prn = -1;
        return rc;
    }
    SqgPvt best, trial;
    int found = 0;
    for (int i = 0; i < out->n_used; i++) {
        if (solve_core(epoch, ephs, n_eph, out->pos, opt, out->used_prn[i], &trial)) continue;
        if (!found || trial.rms_m < best.rms_m) {
            best = trial;
            best.excluded_prn = out->used_prn[i];
            found = 1;
        }
    }
    if (found && best.rms_m <= opt->raim_limit_m) *out = best;
    else out->excluded_prn = -1; /* detected, but no single satellite explains it */
    return rc;
}

int sqg_solve_pvt(const SqgEpoch *epoch, const SqgEphemeris *ephs, int n_eph, SqgEcef approx, SqgPvt *out) {
    return sqg_solve_pvt_opt(epoch, ephs, n_eph, approx, NULL, out);
}

/* ---- track and survey ---- */

void sqg_track_init(SqgTrack *tr) { memset(tr, 0, sizeof(*tr)); }

int sqg_track_push(SqgTrack *tr, const SqgEpoch *epoch, const SqgEphemeris *ephs, int n_eph, SqgEcef approx) {
    if (tr->n_epochs >= SQG_MAX_EPOCHS) return -1;
    tr->epochs[tr->n_epochs] = *epoch;
    SqgPvt pvt;
    SqgEcef seed = approx;
    if (tr->n_epochs > 0 && tr->solutions[tr->n_epochs - 1].converged) seed = tr->solutions[tr->n_epochs - 1].pos;
    sqg_solve_pvt(epoch, ephs, n_eph, seed, &pvt);
    tr->solutions[tr->n_epochs] = pvt;
    tr->n_epochs++;
    return pvt.converged ? 0 : -1;
}

SqgEnu sqg_stakeout_delta(SqgGeodetic from, SqgGeodetic to) {
    SqgEcef a, b;
    sqg_geodetic_to_ecef(from, &a);
    sqg_geodetic_to_ecef(to, &b);
    SqgEnu enu;
    sqg_ecef_to_enu(a, b, from, &enu);
    return enu;
}

double sqg_baseline_length(SqgEcef a, SqgEcef b) {
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

int sqg_write_sqobs(const SqgTrack *tr, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "SQOBS 1\n");
    for (int i = 0; i < tr->n_epochs; i++) {
        const SqgEpoch *e = &tr->epochs[i];
        const SqgPvt *p = &tr->solutions[i];
        fprintf(f, "EPOCH %.3f n=%d converged=%d pdop=%.3f hdop=%.3f vdop=%.3f xyz=%.3f %.3f %.3f cb=%.3f rms=%.3f",
                e->t_receiver, p->n_used, p->converged, p->pdop, p->hdop, p->vdop, p->pos.x, p->pos.y, p->pos.z,
                p->clock_bias_m, p->rms_m);
        if (p->excluded_prn) fprintf(f, " excluded=%d", p->excluded_prn);
        fprintf(f, "\n");
        for (int j = 0; j < e->n_obs; j++)
            if (e->obs[j].valid)
                fprintf(f, "  PRN%02d %.3f %.1f\n", e->obs[j].prn, e->obs[j].pseudorange_m, e->obs[j].snr_dbhz);
    }
    fprintf(f, "END\n");
    return fclose(f) == 0 ? 0 : -1;
}

/* ---- synthetic sky ---- */

void sqg_demo_ephemerides(SqgEphemeris *ephs, int *n_eph, double toe) {
    *n_eph = SQG_DEMO_SATS;
    memset(ephs, 0, sizeof(SqgEphemeris) * SQG_DEMO_SATS);
    for (int i = 0; i < SQG_DEMO_SATS; i++) {
        int plane = i / 5, slot = i % 5;
        SqgEphemeris *e = &ephs[i];
        e->prn = i + 1;
        e->sqrt_a = sqrt(26559700.0 + 1500.0 * (i % 5));
        e->e = 0.002 + 0.0015 * (i % 5);
        e->i0 = (55.0 + 0.3 * (i % 3)) * DEG;
        e->omega0 = (plane * 60.0 + 10.0) * DEG;
        e->w = 0.35 * i;
        e->m0 = (slot * 72.0 + plane * 12.0) * DEG - e->w;
        e->toe = toe;
        e->delta_n = 4.5e-9;
        e->omega_dot = -8.1e-9;
        e->idot = 1.0e-10 * ((i % 3) - 1);
        e->cuc = 1.2e-6 * ((i % 4) - 1.5);
        e->cus = 6.0e-6 * ((i % 3) - 1);
        e->crc = 180.0 + 20.0 * (i % 4);
        e->crs = -40.0 + 15.0 * (i % 5);
        e->cic = 1.0e-7 * ((i % 2) ? 1 : -1);
        e->cis = -6.0e-8;
        e->toc = toe;
        e->af0 = 2.5e-5 * ((i % 7) - 3); /* up to 75 us: tens of kilometres */
        e->af1 = 1.0e-11 * ((i % 3) - 1);
        e->tgd = -1.0e-8 * (i % 4);
    }
}

static unsigned lcg(unsigned *s) {
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

/* standard normal, Box-Muller */
static double gauss(unsigned *s) {
    double u1 = ((lcg(s) >> 8) + 1.0) / 16777217.0, u2 = (lcg(s) >> 8) / 16777216.0;
    return sqrt(-2.0 * log(u1)) * cos(2 * PI * u2);
}

long sqg_demo_ambiguity(SqgEcef rx, int prn) {
    double site = fabs(rx.x) + 3 * fabs(rx.y) + 7 * fabs(rx.z);
    return 1000L * prn + (long)fmod(site * 0.01 * (prn % 7 + 1), 997.0);
}

void sqg_demo_epoch(const SqgEphemeris *ephs, int n_eph, SqgGeodetic truth, double t_true, double clock_bias_m,
                    double mask_deg, double noise_m, unsigned seed, SqgEpoch *out) {
    SqgEcef rx;
    sqg_geodetic_to_ecef(truth, &rx);
    memset(out, 0, sizeof *out);
    out->t_receiver = t_true + clock_bias_m / SQG_C_LIGHT;
    unsigned s = seed ? seed : 1;
    for (int i = 0; i < n_eph && out->n_obs < SQG_MAX_SATS; i++) {
        double t_tx = t_true - 0.075, rho = 0;
        SqgEcef sr = {0, 0, 0};
        for (int it = 0; it < 6; it++) {
            SqgEcef sat = sqg_sat_position(&ephs[i], t_tx);
            double a = SQG_OMEGA_E * (t_true - t_tx);
            sr.x = cos(a) * sat.x + sin(a) * sat.y;
            sr.y = -sin(a) * sat.x + cos(a) * sat.y;
            sr.z = sat.z;
            rho = sqg_baseline_length(sr, rx);
            t_tx = t_true - rho / SQG_C_LIGHT;
        }
        double el = sqg_az_el(rx, truth, sr).el_deg;
        double n = gauss(&s);
        if (el < mask_deg) continue;
        SqgObservation *o = &out->obs[out->n_obs++];
        o->prn = ephs[i].prn;
        o->pseudorange_m = rho + clock_bias_m - SQG_C_LIGHT * sqg_sat_clock(&ephs[i], t_tx) + noise_m * n;
        o->snr_dbhz = 30.0 + 20.0 * sin(el * DEG);
        o->valid = 1;
        /* carrier: the same clocks, an ambiguity of whole cycles, less noise */
        long amb = sqg_demo_ambiguity(rx, ephs[i].prn);
        o->carrier_cyc = (rho + clock_bias_m - SQG_C_LIGHT * sqg_sat_clock(&ephs[i], t_tx)) / SQG_L1_LAMBDA + amb +
                         noise_m / 100 * gauss(&s) / SQG_L1_LAMBDA;
        o->has_carrier = 1;
    }
}
