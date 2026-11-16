#ifndef SB_AERO_H
#define SB_AERO_H

#include "sb_hydro.h"

typedef struct {
    char program_id[64];
    double k_teach;
    double rho_kg_m3;
    double rn_m;
    double v_m_s;
    double q_ref; /* optional; 0 → Soft recomputes */
} SbHeatFluxCase;

typedef struct {
    int ok;
    int npass;
    int nfail;
    double q;
    char summary[SB_MSG_LEN];
} SbAeroResult;

int sb_aero_parse(SbHeatFluxCase *c, const char *text, char *err, int err_cap);

/* Soft teaching cold-wall heat-flux stub (ISM HB educational identity). */
int sb_aero_selftest(const SbHeatFluxCase *c, SbAeroResult *res);

#endif
