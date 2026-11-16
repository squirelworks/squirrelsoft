#include "fl_scenario.h"

#include <stdio.h>
#include <string.h>

void fl_scenario_defaults(FlScenario *out, int *n) {
    if (!out || !n) return;
    memset(out, 0, sizeof(FlScenario) * FL_SCEN_COUNT);

    out[0].id = FL_SCEN_TRIM_CRUISE;
    snprintf(out[0].name, sizeof out[0].name, "trim_cruise");
    out[0].duration_s = 3.0;
    out[0].dt = 0.01;
    out[0].V0_mps = 22.0;
    out[0].throttle = 0.55;
    out[0].de_deg = -1.5;
    /* Scaffold model is not trimmed tightly; keep loose gates. */
    out[0].max_abs_pitch_rate_dps = 15.0;
    out[0].max_altitude_loss_m = 12.0;

    out[1].id = FL_SCEN_PULL_UP;
    snprintf(out[1].name, sizeof out[1].name, "pull_up");
    out[1].duration_s = 2.0;
    out[1].dt = 0.01;
    out[1].V0_mps = 24.0;
    out[1].throttle = 0.70;
    out[1].de_deg = -8.0;
    out[1].min_nz = 1.15;
    out[1].max_altitude_loss_m = 20.0;

    out[2].id = FL_SCEN_ROLL;
    snprintf(out[2].name, sizeof out[2].name, "roll");
    out[2].duration_s = 2.0;
    out[2].dt = 0.01;
    out[2].V0_mps = 22.0;
    out[2].throttle = 0.60;
    out[2].da_deg = 12.0;
    out[2].min_abs_roll_deg = 15.0;
    out[2].max_altitude_loss_m = 15.0;

    *n = FL_SCEN_COUNT;
}

const FlScenario *fl_scenario_by_name(const FlScenario *list, int n, const char *name) {
    int i;
    for (i = 0; i < n; i++)
        if (strcmp(list[i].name, name) == 0) return &list[i];
    return NULL;
}

int fl_scenario_run(FlSim *sim, const FlScenario *sc, FlScenarioResult *res) {
    if (!sim || !sc || !res) return -1;
    memset(res, 0, sizeof *res);
    res->id = sc->id;

    fl_sim_reset(sim);
    sim->state.u = sc->V0_mps;
    FlControls c;
    memset(&c, 0, sizeof c);
    c.throttle = sc->throttle;
    c.de_deg = sc->de_deg;
    c.da_deg = sc->da_deg;
    c.dr_deg = sc->dr_deg;
    fl_sim_set_controls(sim, &c);

    double z0 = sim->state.z;
    if (fl_sim_run(sim, sc->duration_s, sc->dt, sc->dt) < 0) {
        snprintf(res->summary, sizeof res->summary, "sim failed");
        return -1;
    }

    double max_q = 0.0, max_nz = 0.0, max_abs_phi = 0.0;
    int i;
    for (i = 0; i < sim->nhist; i++) {
        double q_dps = 0.0;
        if (i + 1 < sim->nhist) {
            double dt = sim->hist[i + 1].t - sim->hist[i].t;
            if (dt > 1e-9)
                q_dps = fabs(sim->hist[i + 1].theta_deg - sim->hist[i].theta_deg) / dt;
        }
        if (q_dps > max_q) max_q = q_dps;
        if (sim->hist[i].nz > max_nz) max_nz = sim->hist[i].nz;
        double ap = fabs(sim->hist[i].phi_deg);
        if (ap > max_abs_phi) max_abs_phi = ap;
    }
    double alt_loss = sim->state.z - z0; /* NED: positive z is down */
    if (alt_loss < 0.0) alt_loss = 0.0;

    res->passed = 1;
    switch (sc->id) {
    case FL_SCEN_TRIM_CRUISE:
        res->metric_a = max_q;
        res->metric_b = alt_loss;
        if (max_q > sc->max_abs_pitch_rate_dps || alt_loss > sc->max_altitude_loss_m)
            res->passed = 0;
        snprintf(res->summary, sizeof res->summary,
                 "trim_cruise: max|q|=%.2f deg/s (lim %.2f), alt_loss=%.2f m (lim %.2f) → %s",
                 max_q, sc->max_abs_pitch_rate_dps, alt_loss, sc->max_altitude_loss_m,
                 res->passed ? "PASS" : "FAIL");
        break;
    case FL_SCEN_PULL_UP:
        res->metric_a = max_nz;
        res->metric_b = alt_loss;
        if (max_nz < sc->min_nz || alt_loss > sc->max_altitude_loss_m) res->passed = 0;
        snprintf(res->summary, sizeof res->summary,
                 "pull_up: max_nz=%.3f (need >= %.3f), alt_loss=%.2f m → %s", max_nz,
                 sc->min_nz, alt_loss, res->passed ? "PASS" : "FAIL");
        break;
    case FL_SCEN_ROLL:
        res->metric_a = max_abs_phi;
        res->metric_b = alt_loss;
        if (max_abs_phi < sc->min_abs_roll_deg || alt_loss > sc->max_altitude_loss_m)
            res->passed = 0;
        snprintf(res->summary, sizeof res->summary,
                 "roll: max|phi|=%.2f deg (need >= %.2f), alt_loss=%.2f m → %s",
                 max_abs_phi, sc->min_abs_roll_deg, alt_loss, res->passed ? "PASS" : "FAIL");
        break;
    default:
        res->passed = 0;
        snprintf(res->summary, sizeof res->summary, "unknown scenario");
        return -1;
    }
    return 0;
}
