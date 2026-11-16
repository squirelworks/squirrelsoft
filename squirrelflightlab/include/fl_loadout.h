/*
 * Soft #173: validate station × adapter × store loadouts (inert only).
 * No weapons employment — mass/CG/asymmetry + release envelope co-check.
 */
#ifndef FL_LOADOUT_H
#define FL_LOADOUT_H

#define FL_LO_MAX_ASSIGN 16
#define FL_LO_MAX_CHECKS 48
#define FL_LO_MSG 192

typedef struct {
    char station[32];
    char store_id[64];
    char adapter[64]; /* optional mechanical id; empty → station default */
} FlLoAssign;

typedef struct {
    double keas_kt;
    double aoa_deg;
    double nz_g;
    double sideslip_deg;
    int wow;
    int has_condition; /* 0 if omitted */
} FlLoFlightCond;

typedef struct {
    char id[48];
    char result[16]; /* PASS | FAIL */
    char detail[FL_LO_MSG];
} FlLoCheck;

typedef struct {
    char aircraft_id[64];
    char label[80];
    FlLoAssign assigns[FL_LO_MAX_ASSIGN];
    int nassign;
    FlLoFlightCond cond;
    FlLoCheck checks[FL_LO_MAX_CHECKS];
    int nchecks;
    int npass, nfail;
    double mass_left_kg, mass_right_kg, mass_center_kg;
    double lateral_cg_m;
    char overall[16];
} FlLoReport;

/*
 * iface_dir: fixtures/ism-interfaces (hardpoints, inert-stores, multi-origin,
 * sms-interface, mass-properties). loadout_path: Soft loadout YAML.
 * Returns 0 if all checks pass; 1 if any fail; -1 on load/parse error.
 */
int fl_loadout_validate(const char *iface_dir, const char *loadout_path,
                        FlLoReport *rep, char *err, int err_cap);

int fl_loadout_report_md(const FlLoReport *rep, char *out, int out_cap);

#endif
