/*
 * Parametric aerodynamics sandbox + replaceable tunnel-table provider (S7.3).
 * Coefficients live in FlAeroModel; tunnel CSV can override CL/CD/Cm vs alpha.
 */
#ifndef FL_AERO_H
#define FL_AERO_H

#include "fl_project.h"
#include "fl_units.h"

typedef enum {
    FL_AERO_PROVIDER_PARAMETRIC = 0,
    FL_AERO_PROVIDER_TUNNEL_TABLE = 1
} FlAeroProviderKind;

typedef struct {
    /* Longitudinal */
    double CL0, CLa, CLde;     /* 1/rad where applicable */
    double CD0, k_induced;     /* CD = CD0 + k CL^2 */
    double Cm0, Cma, Cmde, Cmq;
    /* Lateral-directional */
    double CYb, CYdr;
    double Clb, Clp, Clr, Clda, Cldr;
    double Cnb, Cnp, Cnr, Cnda, Cndr;
    /* Reference */
    double S_m2;   /* wing area */
    double c_m;    /* mean chord */
    double b_m;    /* span */
    double rho;    /* air density */

    /* S7.3 replaceable provider: tunnel table override for CL/CD/Cm(α) */
    FlAeroProviderKind provider;
    int n_tunnel;
    double tun_alpha_deg[FL_MAX_TUNNEL]; /* ascending */
    double tun_CL[FL_MAX_TUNNEL];
    double tun_CD[FL_MAX_TUNNEL];
    double tun_Cm[FL_MAX_TUNNEL];
} FlAeroModel;

typedef struct {
    double alpha_rad;
    double beta_rad;
    double p, q, r; /* body rates rad/s */
    double de, da, dr; /* elevator, aileron, rudder rad */
    double V; /* airspeed m/s */
} FlAeroState;

typedef struct {
    double CL, CD, CY;
    double Cl, Cm, Cn;
    double L, D, Y;       /* N */
    double l, m, n;       /* N·m */
    double qbar;          /* dynamic pressure Pa */
} FlAeroForces;

typedef struct {
    double alpha_deg;
    double CL, CD, Cm;
    double L_N, D_N, m_Nm;
} FlAoARow;

/* Build a parametric model from project geometry (scaffold estimates). */
void fl_aero_from_project(FlAeroModel *m, const FlProject *p);

/*
 * Load Soft tunnel CSV (header alpha_deg,CL,CD,Cm) into model as
 * FL_AERO_PROVIDER_TUNNEL_TABLE. Uses NR Ch. 3.1 linear interp at eval.
 * Optional least-squares CL0/CLa/Cm0/Cma refresh (NR Ch. 15.1 class ideas).
 */
int fl_aero_load_tunnel_csv(FlAeroModel *m, const char *csv_text, char *err,
                            int err_cap);

/* Evaluate forces/moments in body axes at one state. */
void fl_aero_eval(const FlAeroModel *m, const FlAeroState *s, FlAeroForces *out);

/* AoA sweep at fixed V, beta=0, controls=0 (unless de given). */
int fl_aero_aoa_sweep(const FlAeroModel *m, double V_mps, double alpha0_deg,
                      double alpha1_deg, int n, double de_deg,
                      FlAoARow *rows, int rows_cap);

/* Crude static-margin / derivative summary for the numeric table. */
typedef struct {
    double CLa, Cma, static_margin_c; /* SM as -Cma/CLa */
    double Clb, Cnb;
    double Cmde, Clda;
} FlStabilityDerivs;

void fl_aero_stability(const FlAeroModel *m, FlStabilityDerivs *d);

/* Format a CSV table (header + rows). Returns bytes written, or -1. */
int fl_aero_sweep_csv(const FlAoARow *rows, int n, char *out, int out_cap);

typedef struct {
    int ok;
    int npoints;
    double rms_CL;
    double rms_CD;
    double rms_Cm;
    double max_abs_CL;
    char summary[FL_MAX_MSG];
} FlTunnelCorrReport;

/*
 * Correlate parametric scaffold vs loaded tunnel table at tunnel alpha knots
 * (Soft lerp of parametric? — evaluate parametric at each tunnel α).
 */
int fl_aero_tunnel_correlate(const FlAeroModel *tunnel_m, const FlProject *p,
                             FlTunnelCorrReport *rep);

#endif
