/*
 * Scenario runner: trim cruise, pull-up, roll with editable pass/fail thresholds.
 */
#ifndef FL_SCENARIO_H
#define FL_SCENARIO_H

#include "fl_dyn.h"

typedef enum {
    FL_SCEN_TRIM_CRUISE = 0,
    FL_SCEN_PULL_UP,
    FL_SCEN_ROLL,
    FL_SCEN_COUNT
} FlScenarioId;

typedef struct {
    FlScenarioId id;
    char name[FL_NAME_LEN];
    double duration_s;
    double dt;
    double V0_mps;
    double throttle;
    double de_deg, da_deg, dr_deg;
    /* Pass/fail thresholds (editable) */
    double max_abs_pitch_rate_dps; /* trim */
    double min_nz;                /* pull-up */
    double min_abs_roll_deg;      /* roll */
    double max_altitude_loss_m;
} FlScenario;

typedef struct {
    FlScenarioId id;
    int passed;
    char summary[FL_MAX_MSG];
    double metric_a, metric_b;
} FlScenarioResult;

void fl_scenario_defaults(FlScenario *out, int *n); /* fills FL_SCEN_COUNT */
const FlScenario *fl_scenario_by_name(const FlScenario *list, int n, const char *name);

int fl_scenario_run(FlSim *sim, const FlScenario *sc, FlScenarioResult *res);

#endif
