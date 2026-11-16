#include "fl_fcs.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void fl_fcs_limits_default(FlFcsLimits *lim) {
    memset(lim, 0, sizeof *lim);
    /* Envelope.md says numeric limits TBD — Soft uses conservative study stubs
     * until Victor publishes envelope numbers in the param file. */
    lim->aoa_soft_limit_deg = 18.0;
    lim->nz_soft_limit = 4.5;
    lim->roll_rate_soft_dps = 360.0;
    lim->duration_s = 2.0;
    lim->dt = 0.01;
}

int fl_fcs_co_check(FlSim *sim, const FlFcsLimits *lim, FlFcsResult *res) {
    if (!sim || !lim || !res) return -1;
    memset(res, 0, sizeof *res);
    fl_sim_reset(sim);
    FlControls c;
    memset(&c, 0, sizeof c);
    c.throttle = 0.65;
    c.de_deg = -6.0;
    c.da_deg = 8.0;
    fl_sim_set_controls(sim, &c);

    double max_aoa = 0.0, max_nz = 0.0, max_rr = 0.0;
    double prev_phi = fl_deg(sim->state.phi);
    double t_end = lim->duration_s;
    while (sim->state.t + 1e-12 < t_end) {
        if (fl_sim_step(sim, lim->dt) != 0) return -1;
        double V, a, b;
        fl_dyn_airdata(&sim->state, &V, &a, &b);
        double aoa = fabs(fl_deg(a));
        if (aoa > max_aoa) max_aoa = aoa;
        double nz = 1.0 + (sim->state.q * (V > 0.5 ? V : 0.5)) / FL_G0;
        if (nz > max_nz) max_nz = nz;
        double phi = fl_deg(sim->state.phi);
        double rr = fabs(phi - prev_phi) / lim->dt;
        if (rr > max_rr) max_rr = rr;
        prev_phi = phi;
        (void)b;
    }
    res->max_aoa_deg = max_aoa;
    res->max_nz = max_nz;
    res->max_roll_rate_dps = max_rr;
    res->passed = 1;
    if (max_aoa > lim->aoa_soft_limit_deg) res->passed = 0;
    if (max_nz > lim->nz_soft_limit) res->passed = 0;
    if (max_rr > lim->roll_rate_soft_dps) res->passed = 0;
    snprintf(res->summary, sizeof res->summary,
             "fcs co-check: max_aoa=%.2f (lim %.2f) max_nz=%.3f (lim %.3f) "
             "max_roll_rate=%.1f dps (lim %.1f) → %s",
             max_aoa, lim->aoa_soft_limit_deg, max_nz, lim->nz_soft_limit, max_rr,
             lim->roll_rate_soft_dps, res->passed ? "PASS" : "FAIL");
    return 0;
}

int fl_fcs_report(const FlIsmMeta *meta, const FlFcsLimits *lim, const FlFcsResult *res,
                  char *out, int out_cap) {
    if (!res || !out || out_cap < 64) return -1;
    return snprintf(out, out_cap,
                    "SquirrelFlightLab FCS co-check report 1\n"
                    "vehicle: %s\n"
                    "params_loaded: %d\n"
                    "iface_loaded: %d\n"
                    "source_note: %s\n"
                    "aoa_soft_limit_deg: %.3f\n"
                    "nz_soft_limit: %.3f\n"
                    "roll_rate_soft_dps: %.3f\n"
                    "max_aoa_deg: %.4f\n"
                    "max_nz: %.4f\n"
                    "max_roll_rate_dps: %.4f\n"
                    "result: %s\n"
                    "summary: %s\n",
                    meta && meta->vehicle_id[0] ? meta->vehicle_id : "(unknown)",
                    meta ? meta->loaded_params : 0, meta ? meta->loaded_iface : 0,
                    meta && meta->source_note[0] ? meta->source_note : "",
                    lim ? lim->aoa_soft_limit_deg : 0.0, lim ? lim->nz_soft_limit : 0.0,
                    lim ? lim->roll_rate_soft_dps : 0.0, res->max_aoa_deg, res->max_nz,
                    res->max_roll_rate_dps, res->passed ? "PASS" : "FAIL", res->summary);
}
