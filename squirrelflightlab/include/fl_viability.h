/*
 * Evaluate ISM viability-criteria.yaml → MD/JSON report (Soft #176).
 * Soft test software only; Victor owns criteria SoT.
 */
#ifndef FL_VIABILITY_H
#define FL_VIABILITY_H

#include "fl_yaml.h"

#define FL_VIA_MAX_GATES 24
#define FL_VIA_MSG 192

typedef struct {
    char id[32];
    char name[64];
    char result[32]; /* pass | provisional_pass | fail */
    int soft_pass;   /* Soft recomputed band check */
    double measured;
    double band_min, band_max;
    char detail[FL_VIA_MSG];
} FlViaGate;

typedef struct {
    char aircraft_id[64];
    char schema_version[32];
    int provisional;
    FlViaGate gates[FL_VIA_MAX_GATES];
    int ngates;
    int npass, nprov, nfail;
    char overall[32];
    char label[80];
} FlViaReport;

/* Load + evaluate viability-criteria.yaml text. Returns 0 if no hard fails. */
int fl_viability_eval(const char *yaml_text, FlViaReport *rep, char *err,
                      int err_cap);

/* Load from path (typically fixtures/ism-interfaces/viability-criteria.yaml). */
int fl_viability_eval_file(const char *path, FlViaReport *rep, char *err,
                           int err_cap);

int fl_viability_report_md(const FlViaReport *rep, char *out, int out_cap);
int fl_viability_report_json(const FlViaReport *rep, char *out, int out_cap);

/* Optional tool xcheck: skip (1) if paths missing; 0 if present+ok; -1 error. */
int fl_viability_optional_xcheck(const char *openvsp, const char *avl,
                                 const char *calculix, char *note, int note_cap);

#endif
