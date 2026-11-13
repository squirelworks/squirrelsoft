/* sqgnss: the epoch stream, occupation averaging and stakeout guidance
 * (see sqgnss.h). Original code. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sqgnss.h"

#define PI 3.14159265358979323846

void sqg_stream_init(SqgStream *s, const SqgPvtOptions *opt) {
    memset(s, 0, sizeof *s);
    if (opt) s->opt = *opt;
    else sqg_pvt_options_default(&s->opt);
}

void sqg_stream_reset_average(SqgStream *s) {
    s->n_avg = 0;
    memset(s->mean, 0, sizeof s->mean);
    memset(s->m2, 0, sizeof s->m2);
}

int sqg_stream_push(SqgStream *s, const SqgEpoch *epoch, const SqgEphemeris *ephs, int n_eph) {
    SqgEcef start = {0, 0, 0};
    if (s->have_fix) start = s->pvt.pos;
    SqgPvt p;
    s->n_epochs++;
    int rc = sqg_solve_pvt_opt(epoch, ephs, n_eph, start, &s->opt, &p);
    if (rc != 0 && s->have_fix) rc = sqg_solve_pvt_opt(epoch, ephs, n_eph, (SqgEcef){0, 0, 0}, &s->opt, &p);
    if (rc != 0 || p.excluded_prn < 0) return -1;
    /* speed and course from the last fix */
    s->have_velocity = 0;
    if (s->have_fix && epoch->t_receiver > s->t_fix) {
        SqgGeodetic llh;
        SqgEnu d;
        sqg_ecef_to_geodetic(s->pvt.pos, &llh);
        sqg_ecef_to_enu(s->pvt.pos, p.pos, llh, &d);
        double dt = epoch->t_receiver - s->t_fix;
        s->speed_mps = sqrt(d.e * d.e + d.n * d.n) / dt;
        s->course_deg = fmod(atan2(d.e, d.n) * 180 / PI + 360, 360);
        s->have_velocity = 1;
    }
    s->pvt = p;
    s->have_fix = 1;
    s->t_fix = epoch->t_receiver;
    s->n_fixed++;
    /* the running mean and spread (Welford) about the first fix averaged */
    if (s->n_avg == 0) {
        s->origin = p.pos;
        sqg_ecef_to_geodetic(p.pos, &s->origin_llh);
    }
    SqgEnu d;
    sqg_ecef_to_enu(s->origin, p.pos, s->origin_llh, &d);
    double x[3] = {d.e, d.n, d.u};
    s->n_avg++;
    for (int k = 0; k < 3; k++) {
        double dm = x[k] - s->mean[k];
        s->mean[k] += dm / s->n_avg;
        s->m2[k] += dm * (x[k] - s->mean[k]);
    }
    return 0;
}

int sqg_stream_average(const SqgStream *s, SqgGeodetic *mean, SqgEnu *sigma) {
    if (s->n_avg == 0) return 0;
    SqgEnu m = {s->mean[0], s->mean[1], s->mean[2]};
    SqgEcef p;
    sqg_enu_to_ecef(s->origin, s->origin_llh, m, &p);
    if (mean) sqg_ecef_to_geodetic(p, mean);
    if (sigma) {
        double k = s->n_avg > 1 ? 1.0 / (s->n_avg - 1) : 0;
        sigma->e = sqrt(s->m2[0] * k);
        sigma->n = sqrt(s->m2[1] * k);
        sigma->u = sqrt(s->m2[2] * k);
    }
    return s->n_avg;
}

SqgGuide sqg_stakeout_guide(SqgGeodetic here, SqgGeodetic target, double tol_m) {
    SqgGuide g;
    g.delta = sqg_stakeout_delta(here, target);
    g.horiz_m = sqrt(g.delta.e * g.delta.e + g.delta.n * g.delta.n);
    g.bearing_deg = g.horiz_m > 0 ? fmod(atan2(g.delta.e, g.delta.n) * 180 / PI + 360, 360) : 0;
    g.on_point = g.horiz_m <= tol_m;
    return g;
}

/* a non-negative distance with no "-0.000" */
static double mag(double v) { return fabs(v) < 0.0005 ? 0 : fabs(v); }

void sqg_guide_text(const SqgGuide *g, char *out, size_t cap) {
    size_t at = 0;
    if (!cap) return;
    out[0] = 0;
    if (g->on_point) at += (size_t)snprintf(out, cap, "on point (%.3f m)", g->horiz_m);
    else {
        /* leave out a leg that rounds to nothing, and show due north as 0.0 */
        double n = mag(g->delta.n), e = mag(g->delta.e), b = g->bearing_deg >= 359.95 ? 0 : g->bearing_deg;
        char legs[64];
        if (n > 0 && e > 0)
            snprintf(legs, sizeof legs, "%.3f m %c, %.3f m %c", n, g->delta.n < 0 ? 'S' : 'N', e,
                     g->delta.e < 0 ? 'W' : 'E');
        else if (n > 0) snprintf(legs, sizeof legs, "%.3f m %c", n, g->delta.n < 0 ? 'S' : 'N');
        else snprintf(legs, sizeof legs, "%.3f m %c", e, g->delta.e < 0 ? 'W' : 'E');
        at += (size_t)snprintf(out, cap, "go %s (%.3f m at %.1f deg)", legs, g->horiz_m, b);
    }
    if (at >= cap) return;
    /* the target below here: cut down to it; above: fill up to it */
    if (mag(g->delta.u) == 0) snprintf(out + at, cap - at, "; on grade");
    else snprintf(out + at, cap - at, "; %s %.3f m", g->delta.u < 0 ? "cut" : "fill", mag(g->delta.u));
}
