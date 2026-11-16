#include "fl_dyn.h"

#include "squirrelnum.h"

#include <stdio.h>
#include <string.h>

void fl_dyn_airdata(const FlRigidState *s, double *V, double *alpha, double *beta) {
    double vv = s->u * s->u + s->v * s->v + s->w * s->w;
    double speed = sqrt(vv);
    if (V) *V = speed;
    if (alpha) *alpha = atan2(s->w, s->u);
    if (beta) *beta = atan2(s->v, speed > 1e-6 ? speed : 1e-6);
}

void fl_dyn_state_pack(const FlRigidState *s, double *y) {
    y[0] = s->u;
    y[1] = s->v;
    y[2] = s->w;
    y[3] = s->p;
    y[4] = s->q;
    y[5] = s->r;
    y[6] = s->phi;
    y[7] = s->theta;
    y[8] = s->psi;
    y[9] = s->x;
    y[10] = s->y;
    y[11] = s->z;
}

void fl_dyn_state_unpack(FlRigidState *s, const double *y) {
    s->u = y[0];
    s->v = y[1];
    s->w = y[2];
    s->p = y[3];
    s->q = y[4];
    s->r = y[5];
    s->phi = y[6];
    s->theta = y[7];
    s->psi = y[8];
    s->x = y[9];
    s->y = y[10];
    s->z = y[11];
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
    sim->integ = FL_INTEG_RK4;
    set_trim_state(sim, 22.0);
    sim->paused = 0;
}

void fl_sim_reset(FlSim *sim) {
    FlControls c = sim->ctrl;
    int cap = sim->hist_cap;
    FlIntegMethod integ = sim->integ;
    FlProject p = sim->project;
    fl_sim_init(sim, &p);
    sim->ctrl = c;
    sim->hist_cap = cap;
    sim->integ = integ;
}

void fl_sim_set_controls(FlSim *sim, const FlControls *c) {
    if (c) sim->ctrl = *c;
}

void fl_sim_pause(FlSim *sim, int paused) { sim->paused = paused ? 1 : 0; }

void fl_sim_set_integrator(FlSim *sim, FlIntegMethod m) {
    if (sim) sim->integ = m;
}

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
    h->nz = 1.0 + (sim->state.q * V) / FL_G0;
}

/* SI mass / inertia from project mm/g inputs — never use raw g as kg. */
static void project_si_inertia(const FlProject *p, double *mass_kg, double *Ixx,
                               double *Iyy, double *Izz) {
    *mass_kg = fl_g_to_kg(p->mass_g);
    if (*mass_kg < 0.05) *mass_kg = 0.05;
    *Ixx = fl_g_mm2_to_kg_m2(p->ixx_g_mm2);
    *Iyy = fl_g_mm2_to_kg_m2(p->iyy_g_mm2);
    *Izz = fl_g_mm2_to_kg_m2(p->izz_g_mm2);
    if (*Ixx < 1e-6) *Ixx = 1e-6;
    if (*Iyy < 1e-6) *Iyy = 1e-6;
    if (*Izz < 1e-6) *Izz = 1e-6;
}

typedef struct {
    FlSim *sim;
} FlDynCtx;

/* y' for [u v w p q r phi theta psi x y z] */
static void fl_dyn_ode(double t, const double *y, double *dydt, void *ctx) {
    (void)t;
    FlDynCtx *dc = ctx;
    FlSim *sim = dc->sim;
    FlRigidState st;
    fl_dyn_state_unpack(&st, y);

    double V, alpha, beta;
    fl_dyn_airdata(&st, &V, &alpha, &beta);

    FlAeroState as;
    memset(&as, 0, sizeof as);
    as.V = V;
    as.alpha_rad = alpha;
    as.beta_rad = beta;
    as.p = st.p;
    as.q = st.q;
    as.r = st.r;
    as.de = fl_rad(sim->ctrl.de_deg);
    as.da = fl_rad(sim->ctrl.da_deg);
    as.dr = fl_rad(sim->ctrl.dr_deg);

    FlAeroForces f;
    fl_aero_eval(&sim->aero, &as, &f);

    double mass, Ixx, Iyy, Izz;
    project_si_inertia(&sim->project, &mass, &Ixx, &Iyy, &Izz);

    double thrust = sim->ctrl.throttle * sim->project.thrust_max_n;
    double ca = cos(alpha), sa = sin(alpha);
    double Fx = thrust - f.D * ca + f.L * sa;
    double Fy = f.Y;
    double Fz = -f.L * ca - f.D * sa;

    double ct = cos(st.theta), stt = sin(st.theta);
    double cp = cos(st.phi), sp = sin(st.phi);
    double gx = -FL_G0 * stt;
    double gy = FL_G0 * sp * ct;
    double gz = FL_G0 * cp * ct;

    dydt[0] = (Fx / mass) + gx - (st.q * st.w - st.r * st.v);
    dydt[1] = (Fy / mass) + gy - (st.r * st.u - st.p * st.w);
    dydt[2] = (Fz / mass) + gz - (st.p * st.v - st.q * st.u);
    dydt[3] = (f.l - (Izz - Iyy) * st.q * st.r) / Ixx;
    dydt[4] = (f.m - (Ixx - Izz) * st.p * st.r) / Iyy;
    dydt[5] = (f.n - (Iyy - Ixx) * st.p * st.q) / Izz;

    double cth = cos(st.theta);
    if (fabs(cth) < 1e-3) cth = (cth >= 0 ? 1e-3 : -1e-3);
    dydt[6] = st.p + (st.q * sin(st.phi) + st.r * cos(st.phi)) * tan(st.theta);
    dydt[7] = st.q * cos(st.phi) - st.r * sin(st.phi);
    dydt[8] = (st.q * sin(st.phi) + st.r * cos(st.phi)) / cth;

    double cph = cos(st.phi), sph = sin(st.phi);
    double cps = cos(st.psi), sps = sin(st.psi);
    cth = cos(st.theta);
    stt = sin(st.theta);
    double R11 = cth * cps;
    double R12 = sph * stt * cps - cph * sps;
    double R13 = cph * stt * cps + sph * sps;
    double R21 = cth * sps;
    double R22 = sph * stt * sps + cph * cps;
    double R23 = cph * stt * sps - sph * cps;
    double R31 = -stt;
    double R32 = sph * cth;
    double R33 = cph * cth;
    dydt[9] = R11 * st.u + R12 * st.v + R13 * st.w;
    dydt[10] = R21 * st.u + R22 * st.v + R23 * st.w;
    dydt[11] = R31 * st.u + R32 * st.v + R33 * st.w;
}

int fl_sim_step_euler(FlSim *sim, double dt) {
    if (sim->paused) return -1;
    if (dt <= 0.0) return -1;
    double y[FL_DYN_NSTATE], dydt[FL_DYN_NSTATE];
    fl_dyn_state_pack(&sim->state, y);
    FlDynCtx ctx = {sim};
    fl_dyn_ode(sim->state.t, y, dydt, &ctx);
    int i;
    for (i = 0; i < FL_DYN_NSTATE; i++) y[i] += dydt[i] * dt;
    fl_dyn_state_unpack(&sim->state, y);
    sim->state.t += dt;
    return 0;
}

int fl_sim_step_rk4(FlSim *sim, double dt) {
    if (sim->paused) return -1;
    if (dt <= 0.0) return -1;
    double y[FL_DYN_NSTATE];
    fl_dyn_state_pack(&sim->state, y);
    FlDynCtx ctx = {sim};
    if (sqn_rk4_step(fl_dyn_ode, &ctx, sim->state.t, y, FL_DYN_NSTATE, dt) !=
        SQN_OK)
        return -1;
    fl_dyn_state_unpack(&sim->state, y);
    sim->state.t += dt;
    return 0;
}

int fl_sim_step(FlSim *sim, double dt) {
    if (sim->integ == FL_INTEG_EULER) return fl_sim_step_euler(sim, dt);
    return fl_sim_step_rk4(sim, dt);
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
