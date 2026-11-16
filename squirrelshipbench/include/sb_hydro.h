#ifndef SB_HYDRO_H
#define SB_HYDRO_H

#define SB_MSG_LEN 240
#define SB_MAX_ORD 32

typedef struct {
    char ship_id[64];
    double rho_kg_m3;
    double length_wl_m;
    double beam_wl_m;
    double volume_m3;
    double kb_m;
    double kg_m;
    double i_t_ref_m4; /* YAML / rectangular teaching reference */
    double bm_ref_m;
    double gm_ref_m;
    int nord;
    double station_m[SB_MAX_ORD]; /* along LWL */
    double half_breadth_m[SB_MAX_ORD];
} SbHydroCase;

typedef struct {
    int ok;
    int npass;
    int nfail;
    double i_t_simpson;
    double bm_m;
    double gm_m;
    char summary[SB_MSG_LEN];
} SbHydroResult;

int sb_hydro_parse(SbHydroCase *c, const char *text, char *err, int err_cap);

/*
 * Soft hydrostatics self-test: GM oracle vs Soft fixture, plus Soft original
 * Simpson 1/3 transverse I from waterplane half-breadths (NR Ch. 4.1 class
 * of closed Newton–Cotes; Soft code, no NR listings).
 */
int sb_hydro_selftest(const SbHydroCase *c, SbHydroResult *res);

#endif
