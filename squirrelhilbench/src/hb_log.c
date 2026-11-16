#include "hb_log.h"

#include <stdio.h>

int hb_log_format_replay_csv(const HbStimResult *r, char *out, int out_cap) {
    if (!r || !out || out_cap < 64) return -1;
    int used = 0;
    int w = snprintf(out, out_cap, "%s", HB_REPLAY_CSV_HEADER);
    if (w < 0 || w >= out_cap) return -1;
    used = w;
    int i;
    for (i = 0; i < r->nsamples; i++) {
        const HbLogSample *h = &r->samples[i];
        w = snprintf(out + used, out_cap - used,
                     "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n", h->t_s, h->V_mps,
                     h->alpha_deg, h->beta_deg, h->theta_deg, h->phi_deg, h->psi_deg, h->nz,
                     h->x_m, h->y_m, h->z_m);
        if (w < 0 || used + w >= out_cap) return -1;
        used += w;
    }
    return used;
}
