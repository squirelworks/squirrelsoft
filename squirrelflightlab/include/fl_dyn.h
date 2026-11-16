/*
 * Rigid-body flight-dynamics sketch (reduced-order 6-DOF).
 * Pause / step / reset and short time-history recording.
 * Integration: classical RK4 via libs/squirrelnum (Soft #174); Euler kept
 * for comparison / convergence tables.
 */
#ifndef FL_DYN_H
#define FL_DYN_H

#include "fl_aero.h"

typedef struct {
    double throttle; /* 0..1 */
    double de_deg, da_deg, dr_deg;
} FlControls;

typedef struct {
    /* Earth position NED (m) */
    double x, y, z;
    /* Body velocity (m/s) */
    double u, v, w;
    /* Euler angles rad (yaw, pitch, roll) and body rates */
    double psi, theta, phi;
    double p, q, r;
    double t;
} FlRigidState;

typedef struct {
    double t;
    double V, alpha_deg, beta_deg;
    double theta_deg, phi_deg, psi_deg;
    double nz; /* load factor approx */
    double x, y, z;
} FlHistSample;

typedef enum {
    FL_INTEG_EULER = 0,
    FL_INTEG_RK4 = 1
} FlIntegMethod;

typedef struct {
    FlProject project;
    FlAeroModel aero;
    FlRigidState state;
    FlControls ctrl;
    int paused;
    FlIntegMethod integ;
    FlHistSample hist[FL_MAX_HISTORY];
    int nhist;
    int hist_cap; /* usable cap, <= FL_MAX_HISTORY */
} FlSim;

void fl_sim_init(FlSim *sim, const FlProject *p);
void fl_sim_reset(FlSim *sim);
void fl_sim_set_controls(FlSim *sim, const FlControls *c);
void fl_sim_pause(FlSim *sim, int paused);
void fl_sim_set_integrator(FlSim *sim, FlIntegMethod m);

/* Advance one step. Default method is RK4. dt in seconds. */
int fl_sim_step(FlSim *sim, double dt);
int fl_sim_step_euler(FlSim *sim, double dt);
int fl_sim_step_rk4(FlSim *sim, double dt);

/* Run for duration_s, recording every sample_dt (or every step if 0). */
int fl_sim_run(FlSim *sim, double duration_s, double dt, double sample_dt);

int fl_sim_history_csv(const FlSim *sim, char *out, int out_cap);

/* Convenience kinematics helpers (also used by tests). */
void fl_dyn_airdata(const FlRigidState *s, double *V, double *alpha, double *beta);

/* Pack/unpack 12-vector [u v w p q r phi theta psi x y z] for squirrelnum. */
#define FL_DYN_NSTATE 12
void fl_dyn_state_pack(const FlRigidState *s, double *y);
void fl_dyn_state_unpack(FlRigidState *s, const double *y);

#endif
