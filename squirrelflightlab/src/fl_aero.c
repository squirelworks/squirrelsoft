#include "fl_aero.h"

#include "squirrelnum.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fl_aero_from_project(FlAeroModel *m, const FlProject *p) {
    memset(m, 0, sizeof *m);
    m->provider = FL_AERO_PROVIDER_PARAMETRIC;
    m->S_m2 = (p->wing_area_mm2 * 1e-6) * (p->fuselage_scale * p->fuselage_scale);
    m->b_m = fl_mm_to_m(p->wing_span_mm) * p->fuselage_scale;
    m->c_m = (m->b_m > 0.0) ? (m->S_m2 / m->b_m) : 0.1;
    m->rho = FL_RHO_SL;

    /* Scaffold parametric estimates — intentionally coarse and replaceable. */
    m->CL0 = 0.15;
    m->CLa = 4.8; /* /rad */
    m->CLde = 0.35;
    m->CD0 = 0.035;
    m->k_induced = 1.0 / (FL_PI * 0.75 * (m->b_m * m->b_m / (m->S_m2 > 0 ? m->S_m2 : 1.0)));
    m->Cm0 = 0.02;
    m->Cma = -0.55; /* mildly stable */
    m->Cmde = -0.90;
    m->Cmq = -8.0;

    m->CYb = -0.60;
    m->CYdr = 0.15;
    m->Clb = -0.08;
    m->Clp = -0.35;
    m->Clr = 0.06;
    m->Clda = 0.12;
    m->Cldr = 0.01;
    m->Cnb = 0.10;
    m->Cnp = -0.02;
    m->Cnr = -0.15;
    m->Cnda = -0.02;
    m->Cndr = -0.08;
}

/*
 * Soft original 2×2 normal equations for y ≈ a + b x (NR Ch. 15.1 class of
 * linear least-squares ideas — Soft code, no NR listings).
 */
static int fit_line(const double *x, const double *y, int n, double *a, double *b) {
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int i;
    if (n < 2) return -1;
    for (i = 0; i < n; i++) {
        sx += x[i];
        sy += y[i];
        sxx += x[i] * x[i];
        sxy += x[i] * y[i];
    }
    double det = (double)n * sxx - sx * sx;
    if (fabs(det) < 1e-18) return -1;
    *a = (sxx * sy - sx * sxy) / det;
    *b = ((double)n * sxy - sx * sy) / det;
    return 0;
}

int fl_aero_load_tunnel_csv(FlAeroModel *m, const char *csv_text, char *err,
                            int err_cap) {
    if (!m || !csv_text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    char *buf = strdup(csv_text);
    if (!buf) {
        if (err && err_cap) snprintf(err, err_cap, "oom");
        return -1;
    }
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    int n = 0, line_no = 0, saw_hdr = 0;
    double alphas[FL_MAX_TUNNEL];
    while (line) {
        line_no++;
        while (*line == ' ' || *line == '\t') line++;
        size_t L = strlen(line);
        while (L > 0 && (line[L - 1] == '\r' || line[L - 1] == ' ')) line[--L] = 0;
        if (!*line || *line == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (!saw_hdr) {
            if (strncmp(line, "alpha_deg", 9) != 0) {
                if (err && err_cap)
                    snprintf(err, err_cap, "tunnel CSV needs alpha_deg,CL,CD,Cm header");
                free(buf);
                return -1;
            }
            saw_hdr = 1;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        double a, cl, cd, cm;
        if (sscanf(line, "%lf,%lf,%lf,%lf", &a, &cl, &cd, &cm) != 4) {
            if (err && err_cap) snprintf(err, err_cap, "line %d: bad tunnel row", line_no);
            free(buf);
            return -1;
        }
        if (n >= FL_MAX_TUNNEL) {
            if (err && err_cap) snprintf(err, err_cap, "too many tunnel rows");
            free(buf);
            return -1;
        }
        if (n > 0 && !(a > alphas[n - 1])) {
            if (err && err_cap) snprintf(err, err_cap, "alpha_deg must be ascending");
            free(buf);
            return -1;
        }
        alphas[n] = a;
        m->tun_alpha_deg[n] = a;
        m->tun_CL[n] = cl;
        m->tun_CD[n] = cd;
        m->tun_Cm[n] = cm;
        n++;
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw_hdr || n < 2) {
        if (err && err_cap) snprintf(err, err_cap, "need header + >=2 tunnel rows");
        return -1;
    }
    m->n_tunnel = n;
    m->provider = FL_AERO_PROVIDER_TUNNEL_TABLE;

    /* Refresh longitudinal linear coeffs from table (rad basis). */
    {
        double x[FL_MAX_TUNNEL], ycl[FL_MAX_TUNNEL], ycm[FL_MAX_TUNNEL];
        int i;
        double a0, b0;
        for (i = 0; i < n; i++) {
            x[i] = fl_rad(m->tun_alpha_deg[i]);
            ycl[i] = m->tun_CL[i];
            ycm[i] = m->tun_Cm[i];
        }
        if (fit_line(x, ycl, n, &a0, &b0) == 0) {
            m->CL0 = a0;
            m->CLa = b0;
        }
        if (fit_line(x, ycm, n, &a0, &b0) == 0) {
            m->Cm0 = a0;
            m->Cma = b0;
        }
    }
    return 0;
}

static int tunnel_lerp(const FlAeroModel *m, double alpha_deg, double *CL,
                       double *CD, double *Cm) {
    double cl = 0, cd = 0, cm = 0;
    if (m->n_tunnel < 2) return -1;
    if (sqn_lerp(m->tun_alpha_deg, m->tun_CL, m->n_tunnel, alpha_deg, &cl) != SQN_OK)
        return -1;
    if (sqn_lerp(m->tun_alpha_deg, m->tun_CD, m->n_tunnel, alpha_deg, &cd) != SQN_OK)
        return -1;
    if (sqn_lerp(m->tun_alpha_deg, m->tun_Cm, m->n_tunnel, alpha_deg, &cm) != SQN_OK)
        return -1;
    *CL = cl;
    *CD = cd;
    *Cm = cm;
    return 0;
}

void fl_aero_eval(const FlAeroModel *m, const FlAeroState *s, FlAeroForces *out) {
    memset(out, 0, sizeof *out);
    double V = s->V > 0.5 ? s->V : 0.5;
    out->qbar = 0.5 * m->rho * V * V;

    if (m->provider == FL_AERO_PROVIDER_TUNNEL_TABLE && m->n_tunnel >= 2) {
        double cl = 0, cd = 0, cm = 0;
        if (tunnel_lerp(m, fl_deg(s->alpha_rad), &cl, &cd, &cm) == 0) {
            out->CL = cl + m->CLde * s->de;
            out->CD = cd;
            out->Cm = cm + m->Cmde * s->de;
        } else {
            out->CL = m->CL0 + m->CLa * s->alpha_rad + m->CLde * s->de;
            out->CD = m->CD0 + m->k_induced * out->CL * out->CL;
            out->Cm = m->Cm0 + m->Cma * s->alpha_rad + m->Cmde * s->de;
        }
    } else {
        out->CL = m->CL0 + m->CLa * s->alpha_rad + m->CLde * s->de;
        out->CD = m->CD0 + m->k_induced * out->CL * out->CL;
        out->Cm = m->Cm0 + m->Cma * s->alpha_rad + m->Cmde * s->de;
    }

    out->CY = m->CYb * s->beta_rad + m->CYdr * s->dr;

    double p_hat = (m->b_m / (2.0 * V)) * s->p;
    double q_hat = (m->c_m / (2.0 * V)) * s->q;
    double r_hat = (m->b_m / (2.0 * V)) * s->r;

    out->Cl = m->Clb * s->beta_rad + m->Clp * p_hat + m->Clr * r_hat +
              m->Clda * s->da + m->Cldr * s->dr;
    if (m->provider != FL_AERO_PROVIDER_TUNNEL_TABLE)
        out->Cm = m->Cm0 + m->Cma * s->alpha_rad + m->Cmde * s->de + m->Cmq * q_hat;
    else
        out->Cm += m->Cmq * q_hat;
    out->Cn = m->Cnb * s->beta_rad + m->Cnp * p_hat + m->Cnr * r_hat +
              m->Cnda * s->da + m->Cndr * s->dr;

    double qS = out->qbar * m->S_m2;
    out->L = qS * out->CL;
    out->D = qS * out->CD;
    out->Y = qS * out->CY;
    out->l = qS * m->b_m * out->Cl;
    out->m = qS * m->c_m * out->Cm;
    out->n = qS * m->b_m * out->Cn;
}

int fl_aero_aoa_sweep(const FlAeroModel *m, double V_mps, double alpha0_deg,
                      double alpha1_deg, int n, double de_deg,
                      FlAoARow *rows, int rows_cap) {
    if (!m || !rows || n < 2 || n > rows_cap || n > FL_MAX_SWEEP) return -1;
    int i;
    for (i = 0; i < n; i++) {
        double a = alpha0_deg + (alpha1_deg - alpha0_deg) * (double)i / (double)(n - 1);
        FlAeroState s;
        memset(&s, 0, sizeof s);
        s.V = V_mps;
        s.alpha_rad = fl_rad(a);
        s.de = fl_rad(de_deg);
        FlAeroForces f;
        fl_aero_eval(m, &s, &f);
        rows[i].alpha_deg = a;
        rows[i].CL = f.CL;
        rows[i].CD = f.CD;
        rows[i].Cm = f.Cm;
        rows[i].L_N = f.L;
        rows[i].D_N = f.D;
        rows[i].m_Nm = f.m;
    }
    return n;
}

void fl_aero_stability(const FlAeroModel *m, FlStabilityDerivs *d) {
    memset(d, 0, sizeof *d);
    d->CLa = m->CLa;
    d->Cma = m->Cma;
    d->static_margin_c = (m->CLa != 0.0) ? (-m->Cma / m->CLa) : 0.0;
    d->Clb = m->Clb;
    d->Cnb = m->Cnb;
    d->Cmde = m->Cmde;
    d->Clda = m->Clda;
}

int fl_aero_sweep_csv(const FlAoARow *rows, int n, char *out, int out_cap) {
    if (!rows || !out || n <= 0 || out_cap < 64) return -1;
    int used = 0;
    int w = snprintf(out, out_cap, "alpha_deg,CL,CD,Cm,L_N,D_N,m_Nm\n");
    if (w < 0 || w >= out_cap) return -1;
    used = w;
    int i;
    for (i = 0; i < n; i++) {
        w = snprintf(out + used, out_cap - used, "%.4f,%.6f,%.6f,%.6f,%.4f,%.4f,%.6f\n",
                     rows[i].alpha_deg, rows[i].CL, rows[i].CD, rows[i].Cm,
                     rows[i].L_N, rows[i].D_N, rows[i].m_Nm);
        if (w < 0 || used + w >= out_cap) return -1;
        used += w;
    }
    return used;
}

int fl_aero_tunnel_correlate(const FlAeroModel *tunnel_m, const FlProject *p,
                             FlTunnelCorrReport *rep) {
    FlAeroModel param;
    int i;
    double sCL = 0, sCD = 0, sCm = 0, maxCL = 0;
    memset(rep, 0, sizeof *rep);
    if (!tunnel_m || !p || tunnel_m->n_tunnel < 2 ||
        tunnel_m->provider != FL_AERO_PROVIDER_TUNNEL_TABLE) {
        snprintf(rep->summary, sizeof rep->summary, "tunnel correlate: need tunnel table");
        return -1;
    }
    fl_aero_from_project(&param, p);
    for (i = 0; i < tunnel_m->n_tunnel; i++) {
        FlAeroState s;
        FlAeroForces f;
        memset(&s, 0, sizeof s);
        s.V = 20.0;
        s.alpha_rad = fl_rad(tunnel_m->tun_alpha_deg[i]);
        fl_aero_eval(&param, &s, &f);
        {
            double dCL = f.CL - tunnel_m->tun_CL[i];
            double dCD = f.CD - tunnel_m->tun_CD[i];
            double dCm = f.Cm - tunnel_m->tun_Cm[i];
            sCL += dCL * dCL;
            sCD += dCD * dCD;
            sCm += dCm * dCm;
            if (fabs(dCL) > maxCL) maxCL = fabs(dCL);
        }
    }
    rep->npoints = tunnel_m->n_tunnel;
    rep->rms_CL = sqrt(sCL / (double)tunnel_m->n_tunnel);
    rep->rms_CD = sqrt(sCD / (double)tunnel_m->n_tunnel);
    rep->rms_Cm = sqrt(sCm / (double)tunnel_m->n_tunnel);
    rep->max_abs_CL = maxCL;
    /* Soft CI threshold: synthetic tunnel is near scaffold — expect modest RMS. */
    rep->ok = (rep->rms_CL < 0.25 && rep->rms_CD < 0.05 && rep->rms_Cm < 0.25);
    snprintf(rep->summary, sizeof rep->summary,
             "tunnel correlate n=%d rms_CL=%.4f rms_CD=%.4f rms_Cm=%.4f "
             "(NR Ch.3.1 lerp provider) → %s",
             rep->npoints, rep->rms_CL, rep->rms_CD, rep->rms_Cm,
             rep->ok ? "PASS" : "FAIL");
    return 0;
}
