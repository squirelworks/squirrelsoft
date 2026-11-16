/*
 * Scripted FCS co-check against an ISM-loaded project.
 * Produces a pass/fail report artifact (text). Soft test software only.
 */
#ifndef FL_FCS_H
#define FL_FCS_H

#include "fl_dyn.h"
#include "fl_ism.h"

typedef struct {
    double aoa_soft_limit_deg; /* from envelope stub / fixture */
    double nz_soft_limit;
    double roll_rate_soft_dps;
    double duration_s;
    double dt;
} FlFcsLimits;

typedef struct {
    int passed;
    char summary[FL_MAX_MSG];
    double max_aoa_deg;
    double max_nz;
    double max_roll_rate_dps;
} FlFcsResult;

void fl_fcs_limits_default(FlFcsLimits *lim);

/* Run a short pitch+roll command script; fail if soft envelope breached. */
int fl_fcs_co_check(FlSim *sim, const FlFcsLimits *lim, FlFcsResult *res);

/* Format a report suitable for writing to a file. */
int fl_fcs_report(const FlIsmMeta *meta, const FlFcsLimits *lim,
                  const FlFcsResult *res, char *out, int out_cap);

#endif
