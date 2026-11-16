/*
 * Soft S8.3 — PSU / UPS load + telemetry benches (Soft test software).
 * Soft-proposed fixtures until Eric publishes machine-readable SoT.
 * Energy over a load schedule uses Soft composite Simpson (NR Ch. 4.1
 * class ideas; Soft original — no NR listings).
 */
#ifndef IB_POWER_H
#define IB_POWER_H

#include "ib_iface.h"

#define IB_POWER_MAX_STEPS 16
#define IB_POWER_MAX_TELEM 8

typedef enum {
    IB_POWER_PSU = 0,
    IB_POWER_UPS = 1
} IbPowerKind;

typedef struct {
    IbPowerKind kind;
    char model[IB_NAME_LEN];
    double v_out_nom_v;
    double i_max_a;
    double p_max_w;
    double capacity_wh; /* UPS only; 0 for PSU */
    double load_w[IB_POWER_MAX_STEPS];
    double dwell_s[IB_POWER_MAX_STEPS];
    int nsteps;
    char telem[IB_POWER_MAX_TELEM][32];
    int ntelem;
} IbPowerIface;

typedef struct {
    int ok;
    int npass;
    int nfail;
    double energy_wh; /* Soft Simpson integral of P(t) */
    char summary[IB_MSG_LEN];
} IbPowerTestResult;

int ib_power_parse(IbPowerIface *p, const char *text, char *err, int err_cap);

/* Synthetic load-step + telemetry self-test (no live PSU/UPS hardware). */
int ib_power_selftest(const IbPowerIface *p, IbPowerTestResult *res);

#endif
