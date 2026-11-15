#include "fl_aero.h"

#include <stdio.h>
#include <string.h>

void fl_aero_from_project(FlAeroModel *m, const FlProject *p) {
    memset(m, 0, sizeof *m);
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

void fl_aero_eval(const FlAeroModel *m, const FlAeroState *s, FlAeroForces *out) {
    memset(out, 0, sizeof *out);
    double V = s->V > 0.5 ? s->V : 0.5;
    out->qbar = 0.5 * m->rho * V * V;

    out->CL = m->CL0 + m->CLa * s->alpha_rad + m->CLde * s->de;
    out->CD = m->CD0 + m->k_induced * out->CL * out->CL;
    out->CY = m->CYb * s->beta_rad + m->CYdr * s->dr;

    double p_hat = (m->b_m / (2.0 * V)) * s->p;
    double q_hat = (m->c_m / (2.0 * V)) * s->q;
    double r_hat = (m->b_m / (2.0 * V)) * s->r;

    out->Cl = m->Clb * s->beta_rad + m->Clp * p_hat + m->Clr * r_hat +
              m->Clda * s->da + m->Cldr * s->dr;
    out->Cm = m->Cm0 + m->Cma * s->alpha_rad + m->Cmde * s->de + m->Cmq * q_hat;
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
