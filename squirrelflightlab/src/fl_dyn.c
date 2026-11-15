#include "fl_dyn.h"

#include <stdio.h>
#include <string.h>

void fl_dyn_airdata(const FlRigidState *s, double *V, double *alpha, double *beta) {
    double vv = s->u * s->u + s->v * s->v + s->w * s->w;
    double speed = sqrt(vv);
    if (V) *V = speed;
    if (alpha) *alpha = atan2(s->w, s->u);
    if (beta) *beta = atan2(s->v, speed > 1e-6 ? speed : 1e-6);
}

static void set_trim_state(FlSim *sim, double V) {
    memset(&sim->state, 0, sizeof sim->state);
    sim->state.u = V;
    sim->state.z = -50.0; /* 50 m AGL in NED */
    sim->state.theta = fl_rad(2.0);
    sim->state.t = 0.0;
}

void fl_sim_init(FlSim *sim, const FlProject *p) {
    memset(sim, 0, sizeof *sim);
    sim->project = *p;
    fl_aero_from_project(&sim->aero, &sim->project);
    sim->ctrl.throttle = 0.55;
    sim->hist_cap = FL_MAX_HISTORY;
    set_trim_state(sim, 22.0);
    sim->paused = 0;
}

void fl_sim_reset(FlSim *sim) {
    FlControls c = sim->ctrl;
    int cap = sim->hist_cap;
    FlProject p = sim->project;
    fl_sim_init(sim, &p);
    sim->ctrl = c;
    sim->hist_cap = cap;
}

void fl_sim_set_controls(FlSim *sim, const FlControls *c) {
    if (c) sim->ctrl = *c;
}

void fl_sim_pause(FlSim *sim, int paused) { sim->paused = paused ? 1 : 0; }

static void record(FlSim *sim) {
    if (sim->nhist >= sim->hist_cap || sim->nhist >= FL_MAX_HISTORY) return;
    FlHistSample *h = &sim->hist[sim->nhist++];
    double V, a, b;
    fl_dyn_airdata(&sim->state, &V, &a, &b);
    h->t = sim->state.t;
    h->V = V;
    h->alpha_deg = fl_deg(a);
    h->beta_deg = fl_deg(b);
    h->theta_deg = fl_deg(sim->state.theta);
    h->phi_deg = fl_deg(sim->state.phi);
    h->psi_deg = fl_deg(sim->state.psi);
    h->x = sim->state.x;
    h->y = sim->state.y;
    h->z = sim->state.z;
    /* approx load factor from pitch rate + alpha (scaffold) */
    h->nz = 1.0 + (sim->state.q * V) / FL_G0;
}

int fl_sim_step(FlSim *sim, double dt) {
    if (sim->paused) return -1;
    if (dt <= 0.0) return -1;

    FlRigidState *s = &sim->state;
    double V, alpha, beta;
    fl_dyn_airdata(s, &V, &alpha, &beta);

    FlAeroState as;
    memset(&as, 0, sizeof as);
    as.V = V;
    as.alpha_rad = alpha;
    as.beta_rad = beta;
    as.p = s->p;
    as.q = s->q;
    as.r = s->r;
    as.de = fl_rad(sim->ctrl.de_deg);
    as.da = fl_rad(sim->ctrl.da_deg);
    as.dr = fl_rad(sim->ctrl.dr_deg);

    FlAeroForces f;
    fl_aero_eval(&sim->aero, &as, &f);

    double mass = fl_g_to_kg(sim->project.mass_g);
    if (mass < 0.05) mass = 0.05;
    /* Convert g·mm² → kg·m²: * 1e-9 */
    double Ixx = sim->project.ixx_g_mm2 * 1e-9;
    double Iyy = sim->project.iyy_g_mm2 * 1e-9;
    double Izz = sim->project.izz_g_mm2 * 1e-9;
    if (Ixx < 1e-6) Ixx = 1e-6;
    if (Iyy < 1e-6) Iyy = 1e-6;
    if (Izz < 1e-6) Izz = 1e-6;

    double thrust = sim->ctrl.throttle * sim->project.thrust_max_n;
    /* Wind axes forces → body (small-alpha approx) */
    double ca = cos(alpha), sa = sin(alpha);
    double Fx = thrust - f.D * ca + f.L * sa;
    double Fy = f.Y;
    double Fz = -f.L * ca - f.D * sa;

    /* Gravity in body axes from Euler */
    double ct = cos(s->theta), st = sin(s->theta);
    double cp = cos(s->phi), sp = sin(s->phi);
    double gx = -FL_G0 * st;
    double gy = FL_G0 * sp * ct;
    double gz = FL_G0 * cp * ct;

    double du = (Fx / mass) + gx - (s->q * s->w - s->r * s->v);
    double dv = (Fy / mass) + gy - (s->r * s->u - s->p * s->w);
    double dw = (Fz / mass) + gz - (s->p * s->v - s->q * s->u);

    double dp = (f.l - (Izz - Iyy) * s->q * s->r) / Ixx;
    double dq = (f.m - (Ixx - Izz) * s->p * s->r) / Iyy;
    double dr = (f.n - (Iyy - Ixx) * s->p * s->q) / Izz;

    s->u += du * dt;
    s->v += dv * dt;
    s->w += dw * dt;
    s->p += dp * dt;
    s->q += dq * dt;
    s->r += dr * dt;

    /* Euler kinematics */
    double sth = sin(s->theta), cth = cos(s->theta);
    if (fabs(cth) < 1e-3) cth = (cth >= 0 ? 1e-3 : -1e-3);
    s->phi += (s->p + (s->q * sin(s->phi) + s->r * cos(s->phi)) * tan(s->theta)) * dt;
    s->theta += (s->q * cos(s->phi) - s->r * sin(s->phi)) * dt;
    s->psi += ((s->q * sin(s->phi) + s->r * cos(s->phi)) / cth) * dt;

    /* NED position from body velocity */
    double cph = cos(s->phi), sph = sin(s->phi);
    double cps = cos(s->psi), sps = sin(s->psi);
    cth = cos(s->theta);
    sth = sin(s->theta);
    double R11 = cth * cps;
    double R12 = sph * sth * cps - cph * sps;
    double R13 = cph * sth * cps + sph * sps;
    double R21 = cth * sps;
    double R22 = sph * sth * sps + cph * cps;
    double R23 = cph * sth * sps - sph * cps;
    double R31 = -sth;
    double R32 = sph * cth;
    double R33 = cph * cth;
    s->x += (R11 * s->u + R12 * s->v + R13 * s->w) * dt;
    s->y += (R21 * s->u + R22 * s->v + R23 * s->w) * dt;
    s->z += (R31 * s->u + R32 * s->v + R33 * s->w) * dt;

    s->t += dt;
    (void)sth;
    return 0;
}

int fl_sim_run(FlSim *sim, double duration_s, double dt, double sample_dt) {
    if (dt <= 0.0 || duration_s < 0.0) return -1;
    if (sample_dt <= 0.0) sample_dt = dt;
    sim->nhist = 0;
    record(sim);
    double next = sample_dt;
    double end = sim->state.t + duration_s;
    while (sim->state.t + 1e-12 < end) {
        double step = dt;
        if (sim->state.t + step > end) step = end - sim->state.t;
        if (fl_sim_step(sim, step) != 0) return -1;
        if (sim->state.t + 1e-12 >= next) {
            record(sim);
            next += sample_dt;
        }
    }
    if (sim->nhist == 0 || sim->hist[sim->nhist - 1].t < sim->state.t - 0.5 * sample_dt)
        record(sim);
    return sim->nhist;
}

int fl_sim_history_csv(const FlSim *sim, char *out, int out_cap) {
    if (!sim || !out || out_cap < 64) return -1;
    int used = 0;
    int w = snprintf(out, out_cap, "t_s,V_mps,alpha_deg,beta_deg,theta_deg,phi_deg,psi_deg,nz,x_m,y_m,z_m\n");
    if (w < 0 || w >= out_cap) return -1;
    used = w;
    int i;
    for (i = 0; i < sim->nhist; i++) {
        const FlHistSample *h = &sim->hist[i];
        w = snprintf(out + used, out_cap - used,
                     "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                     h->t, h->V, h->alpha_deg, h->beta_deg, h->theta_deg, h->phi_deg,
                     h->psi_deg, h->nz, h->x, h->y, h->z);
        if (w < 0 || used + w >= out_cap) return -1;
        used += w;
    }
    return used;
}
