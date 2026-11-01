#include "sqgnss.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double wrap_pi(double a) {
    while (a > M_PI) a -= 2.0 * M_PI;
    while (a < -M_PI) a += 2.0 * M_PI;
    return a;
}

void sqg_geodetic_to_ecef(SqgGeodetic g, SqgEcef *out) {
    double lat = g.lat_deg * M_PI / 180.0;
    double lon = g.lon_deg * M_PI / 180.0;
    double e2 = SQG_WGS84_F * (2.0 - SQG_WGS84_F);
    double sin_lat = sin(lat), cos_lat = cos(lat);
    double N = SQG_WGS84_A / sqrt(1.0 - e2 * sin_lat * sin_lat);
    out->x = (N + g.height_m) * cos_lat * cos(lon);
    out->y = (N + g.height_m) * cos_lat * sin(lon);
    out->z = (N * (1.0 - e2) + g.height_m) * sin_lat;
}

void sqg_ecef_to_geodetic(SqgEcef p, SqgGeodetic *out) {
    double e2 = SQG_WGS84_F * (2.0 - SQG_WGS84_F);
    double lon = atan2(p.y, p.x);
    double pxy = sqrt(p.x * p.x + p.y * p.y);
    double lat = atan2(p.z, pxy * (1.0 - e2));
    for (int i = 0; i < 8; i++) {
        double sin_lat = sin(lat);
        double N = SQG_WGS84_A / sqrt(1.0 - e2 * sin_lat * sin_lat);
        double h = pxy / cos(lat) - N;
        lat = atan2(p.z, pxy * (1.0 - e2 * N / (N + h)));
        out->height_m = h;
    }
    double sin_lat = sin(lat);
    double N = SQG_WGS84_A / sqrt(1.0 - e2 * sin_lat * sin_lat);
    out->height_m = pxy / cos(lat) - N;
    out->lat_deg = lat * 180.0 / M_PI;
    out->lon_deg = lon * 180.0 / M_PI;
}

void sqg_ecef_to_enu(SqgEcef origin, SqgEcef point, SqgGeodetic origin_llh, SqgEnu *enu) {
    double lat = origin_llh.lat_deg * M_PI / 180.0;
    double lon = origin_llh.lon_deg * M_PI / 180.0;
    double dx = point.x - origin.x;
    double dy = point.y - origin.y;
    double dz = point.z - origin.z;
    enu->e = -sin(lon) * dx + cos(lon) * dy;
    enu->n = -sin(lat) * cos(lon) * dx - sin(lat) * sin(lon) * dy + cos(lat) * dz;
    enu->u = cos(lat) * cos(lon) * dx + cos(lat) * sin(lon) * dy + sin(lat) * dz;
}

SqgEcef sqg_sat_position(const SqgEphemeris *eph, double t_gps) {
    double A = eph->sqrt_a * eph->sqrt_a;
    double n0 = sqrt(SQG_MU / (A * A * A));
    double n = n0 + eph->delta_n;
    double tk = t_gps - eph->toe;
    if (tk > 302400.0) tk -= 604800.0;
    if (tk < -302400.0) tk += 604800.0;
    double Mk = eph->m0 + n * tk;
    double Ek = Mk;
    for (int i = 0; i < 12; i++)
        Ek = Mk + eph->e * sin(Ek);
    double sinE = sin(Ek), cosE = cos(Ek);
    double vk = atan2(sqrt(1.0 - eph->e * eph->e) * sinE, cosE - eph->e);
    double phi = vk + eph->w;
    double du = eph->cus * sin(2.0 * phi) + eph->cuc * cos(2.0 * phi);
    double dr = eph->crs * sin(2.0 * phi) + eph->crc * cos(2.0 * phi);
    double di = eph->cis * sin(2.0 * phi) + eph->cic * cos(2.0 * phi);
    double uk = phi + du;
    double rk = A * (1.0 - eph->e * cosE) + dr;
    double ik = eph->i0 + di + eph->idot * tk;
    double xk_p = rk * cos(uk);
    double yk_p = rk * sin(uk);
    double Omega = eph->omega0 + (eph->omega_dot - SQG_OMEGA_E) * tk - SQG_OMEGA_E * eph->toe;
    SqgEcef s;
    s.x = xk_p * cos(Omega) - yk_p * cos(ik) * sin(Omega);
    s.y = xk_p * sin(Omega) + yk_p * cos(ik) * cos(Omega);
    s.z = yk_p * sin(ik);
    return s;
}

static int find_eph(const SqgEphemeris *ephs, int n, int prn) {
    for (int i = 0; i < n; i++)
        if (ephs[i].prn == prn) return i;
    return -1;
}

int sqg_solve_pvt(const SqgEpoch *epoch, const SqgEphemeris *ephs, int n_eph,
                  SqgEcef approx, SqgPvt *out) {
    memset(out, 0, sizeof(*out));
    SqgEcef x = approx;
    double cb = 0.0;
    int used = 0;

    for (int iter = 0; iter < 12; iter++) {
        double H[SQG_MAX_SATS][4];
        double r[SQG_MAX_SATS];
        used = 0;
        for (int i = 0; i < epoch->n_obs; i++) {
            if (!epoch->obs[i].valid) continue;
            int ei = find_eph(ephs, n_eph, epoch->obs[i].prn);
            if (ei < 0) continue;
            /* transmit time approx */
            double t_tx = epoch->t_receiver - epoch->obs[i].pseudorange_m / SQG_C_LIGHT;
            SqgEcef sat = sqg_sat_position(&ephs[ei], t_tx);
            /* Earth rotation during signal flight */
            double tau = epoch->obs[i].pseudorange_m / SQG_C_LIGHT;
            double cos_om = cos(SQG_OMEGA_E * tau);
            double sin_om = sin(SQG_OMEGA_E * tau);
            SqgEcef srot = {cos_om * sat.x + sin_om * sat.y, -sin_om * sat.x + cos_om * sat.y,
                            sat.z};
            double dx = srot.x - x.x, dy = srot.y - x.y, dz = srot.z - x.z;
            double rho = sqrt(dx * dx + dy * dy + dz * dz);
            if (rho < 1.0) continue;
            H[used][0] = -dx / rho;
            H[used][1] = -dy / rho;
            H[used][2] = -dz / rho;
            H[used][3] = 1.0;
            r[used] = epoch->obs[i].pseudorange_m - (rho + cb);
            used++;
        }
        if (used < 4) {
            out->n_used = used;
            return -1;
        }
        /* Normal equations ATA dx = ATr */
        double ATA[4][4] = {{0}}, ATr[4] = {0};
        for (int i = 0; i < used; i++) {
            for (int j = 0; j < 4; j++) {
                ATr[j] += H[i][j] * r[i];
                for (int k = 0; k < 4; k++) ATA[j][k] += H[i][j] * H[i][k];
            }
        }
        /* Gaussian elimination 4x4 */
        double M[4][5];
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) M[i][j] = ATA[i][j];
            M[i][4] = ATr[i];
        }
        for (int col = 0; col < 4; col++) {
            int piv = col;
            for (int row = col + 1; row < 4; row++)
                if (fabs(M[row][col]) > fabs(M[piv][col])) piv = row;
            if (fabs(M[piv][col]) < 1e-12) {
                out->n_used = used;
                return -1;
            }
            if (piv != col)
                for (int j = 0; j < 5; j++) {
                    double tmp = M[col][j];
                    M[col][j] = M[piv][j];
                    M[piv][j] = tmp;
                }
            double div = M[col][col];
            for (int j = 0; j < 5; j++) M[col][j] /= div;
            for (int row = 0; row < 4; row++) {
                if (row == col) continue;
                double f = M[row][col];
                for (int j = 0; j < 5; j++) M[row][j] -= f * M[col][j];
            }
        }
        double dxv[4] = {M[0][4], M[1][4], M[2][4], M[3][4]};
        x.x += dxv[0];
        x.y += dxv[1];
        x.z += dxv[2];
        cb += dxv[3];
        double step = sqrt(dxv[0] * dxv[0] + dxv[1] * dxv[1] + dxv[2] * dxv[2] +
                           dxv[3] * dxv[3]);
        if (step < 1e-4) {
            out->pos = x;
            out->clock_bias_m = cb;
            out->n_used = used;
            out->converged = 1;
            /* PDOP from Q = inv(ATA) geometry 3x3 */
            double qxx = 0, qyy = 0, qzz = 0, qtt = 0;
            /* reuse inverted diagonal-ish via ATA inverse approximate from M path:
               re-invert ATA quickly */
            double A[4][8];
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    A[i][j] = ATA[i][j];
                    A[i][j + 4] = (i == j) ? 1.0 : 0.0;
                }
            }
            for (int col = 0; col < 4; col++) {
                int piv = col;
                for (int row = col + 1; row < 4; row++)
                    if (fabs(A[row][col]) > fabs(A[piv][col])) piv = row;
                for (int j = 0; j < 8; j++) {
                    double tmp = A[col][j];
                    A[col][j] = A[piv][j];
                    A[piv][j] = tmp;
                }
                double div = A[col][col];
                if (fabs(div) < 1e-18) break;
                for (int j = 0; j < 8; j++) A[col][j] /= div;
                for (int row = 0; row < 4; row++) {
                    if (row == col) continue;
                    double f = A[row][col];
                    for (int j = 0; j < 8; j++) A[row][j] -= f * A[col][j];
                }
            }
            qxx = A[0][4];
            qyy = A[1][5];
            qzz = A[2][6];
            qtt = A[3][7];
            out->pdop = sqrt(fabs(qxx + qyy + qzz));
            out->gdop = sqrt(fabs(qxx + qyy + qzz + qtt));
            (void)wrap_pi;
            return 0;
        }
    }
    out->pos = x;
    out->clock_bias_m = cb;
    out->n_used = used;
    out->converged = 0;
    return -1;
}

void sqg_track_init(SqgTrack *tr) { memset(tr, 0, sizeof(*tr)); }

int sqg_track_push(SqgTrack *tr, const SqgEpoch *epoch, const SqgEphemeris *ephs,
                   int n_eph, SqgEcef approx) {
    if (tr->n_epochs >= SQG_MAX_EPOCHS) return -1;
    tr->epochs[tr->n_epochs] = *epoch;
    SqgPvt pvt;
    SqgEcef seed = approx;
    if (tr->n_epochs > 0 && tr->solutions[tr->n_epochs - 1].converged)
        seed = tr->solutions[tr->n_epochs - 1].pos;
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
        fprintf(f, "EPOCH %.3f n=%d converged=%d pdop=%.3f xyz=%.3f %.3f %.3f cb=%.3f\n",
                e->t_receiver, p->n_used, p->converged, p->pdop, p->pos.x, p->pos.y,
                p->pos.z, p->clock_bias_m);
        for (int j = 0; j < e->n_obs; j++)
            if (e->obs[j].valid)
                fprintf(f, "  PRN%02d %.3f %.1f\n", e->obs[j].prn, e->obs[j].pseudorange_m,
                        e->obs[j].snr_dbhz);
    }
    fprintf(f, "END\n");
    fclose(f);
    return 0;
}

void sqg_demo_constellation(SqgEphemeris *ephs, int *n_eph, SqgGeodetic truth,
                            double t0, SqgEpoch *epoch_out) {
    /* Six synthetic GPS-like birds spaced in RAAN / mean anomaly. */
    *n_eph = 6;
    memset(ephs, 0, sizeof(SqgEphemeris) * 6);
    for (int i = 0; i < 6; i++) {
        ephs[i].prn = i + 1;
        ephs[i].sqrt_a = sqrt(26560000.0); /* ~medium Earth orbit */
        ephs[i].e = 0.001;
        ephs[i].i0 = 55.0 * M_PI / 180.0;
        ephs[i].omega0 = (i * 60.0) * M_PI / 180.0;
        ephs[i].w = 0.1 * i;
        ephs[i].m0 = (i * 40.0) * M_PI / 180.0;
        ephs[i].toe = t0;
        ephs[i].omega_dot = -2.6e-9;
    }
    SqgEcef rx;
    sqg_geodetic_to_ecef(truth, &rx);
    memset(epoch_out, 0, sizeof(*epoch_out));
    double cb = 15.0; /* meters clock bias truth */
    /* Pick a receiver time; transmit time = t_rx - range/c (iterated). */
    double t_rx = t0 + 0.08;
    epoch_out->t_receiver = t_rx;
    epoch_out->n_obs = 6;
    for (int i = 0; i < 6; i++) {
        double t_tx = t0;
        SqgEcef srot;
        double rho = 0.0;
        for (int it = 0; it < 4; it++) {
            SqgEcef sat = sqg_sat_position(&ephs[i], t_tx);
            double tau = (t_rx - t_tx);
            if (tau < 0.0) tau = 0.0;
            double cos_om = cos(SQG_OMEGA_E * tau);
            double sin_om = sin(SQG_OMEGA_E * tau);
            srot.x = cos_om * sat.x + sin_om * sat.y;
            srot.y = -sin_om * sat.x + cos_om * sat.y;
            srot.z = sat.z;
            double dx = srot.x - rx.x, dy = srot.y - rx.y, dz = srot.z - rx.z;
            rho = sqrt(dx * dx + dy * dy + dz * dz);
            t_tx = t_rx - rho / SQG_C_LIGHT;
        }
        epoch_out->obs[i].prn = ephs[i].prn;
        epoch_out->obs[i].pseudorange_m = rho + cb;
        epoch_out->obs[i].snr_dbhz = 45.0 - i;
        epoch_out->obs[i].valid = 1;
    }
}
