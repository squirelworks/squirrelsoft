/*
 * Load International Squirrel Machines fighter-jet parameter + interface
 * files (Victor / ISM SoT). Soft never invents airframe truth — it parses
 * what ISM publishes (or a CI fixture snapshot of published docs).
 */
#ifndef FL_ISM_H
#define FL_ISM_H

#include "fl_project.h"

typedef struct {
    char vehicle_id[FL_NAME_LEN];
    char source_note[160];
    char params_path[FL_PATH_LEN];
    char iface_path[FL_PATH_LEN];
    int loaded_params;
    int loaded_iface;
} FlIsmMeta;

/* Parse ISM-FJ parameters text into an FlProject (surfaces/channels may be
 * filled from interface text via fl_ism_apply_interface). */
int fl_ism_parse_params(FlProject *p, FlIsmMeta *meta, const char *text,
                        char *err, int err_cap);

/* Parse bay interface / channel map; merge into project channels + surface
 * actuator links. */
int fl_ism_parse_interface(FlProject *p, FlIsmMeta *meta, const char *text,
                           char *err, int err_cap);

/* Verify project geometry matches loaded ISM params within tolerances. */
typedef struct {
    int ok;
    int nchecks;
    int nfail;
    char summary[FL_MAX_MSG];
} FlIsmVerify;

int fl_ism_verify_project(const FlProject *p, const FlIsmMeta *meta,
                          FlIsmVerify *out);

#endif
