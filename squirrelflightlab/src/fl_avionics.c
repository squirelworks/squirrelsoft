#include "fl_avionics.h"

#include <stdio.h>
#include <string.h>

int fl_avionics_default_expect(FlDeviceExpect *out, int cap) {
    /* Matches ISM bay I/O (J1–J4 → A1–A4): wiring-harness + Soft fixture. */
    static const FlDeviceExpect k[] = {
        {"imu_ax", 1, 1},
        {"imu_ay", 1, 2},
        {"imu_az", 1, 3},
        {"imu_gx", 1, 4},
        {"imu_gy", 1, 5},
        {"imu_gz", 1, 6},
        {"radio_throttle", 1, 7},
        {"radio_aileron", 1, 8},
        {"radio_elevator", 1, 9},
        {"radio_rudder", 1, 10},
        {"act_aileron", 1, 11},
        {"act_elevator", 1, 12},
        {"act_rudder", 1, 13},
        {"act_throttle", 1, 14},
    };
    int n = (int)(sizeof k / sizeof k[0]);
    int i;
    if (!out || cap < n) return -1;
    for (i = 0; i < n; i++) out[i] = k[i];
    return n;
}

void fl_avionics_check(const FlProject *p, const FlDeviceExpect *expect, int nexpect,
                       FlAvionicsReport *rep) {
    memset(rep, 0, sizeof *rep);
    int i;
    for (i = 0; i < nexpect; i++) {
        if (rep->nflags >= FL_MAX_CHANNELS) break;
        FlChannelFlag *f = &rep->flags[rep->nflags++];
        snprintf(f->role, sizeof f->role, "%s", expect[i].role);
        const FlChannel *ch = fl_project_channel(p, expect[i].role);
        if (!ch) {
            f->ok = 0;
            snprintf(f->detail, sizeof f->detail, "missing in project (device doc pin %d)",
                     expect[i].header_pin);
            if (expect[i].required) rep->nmissing++;
            continue;
        }
        if (ch->header_pin != expect[i].header_pin) {
            f->ok = 0;
            snprintf(f->detail, sizeof f->detail,
                     "pin mismatch: project=%d device-doc=%d", ch->header_pin,
                     expect[i].header_pin);
            rep->nmismatch++;
        } else {
            f->ok = 1;
            snprintf(f->detail, sizeof f->detail, "ok pin %d", ch->header_pin);
        }
    }
    /* Extra project channels not in the device doc */
    for (i = 0; i < p->nchannels; i++) {
        if (!p->channels[i].used) continue;
        int found = 0, j;
        for (j = 0; j < nexpect; j++) {
            if (strcmp(p->channels[i].role, expect[j].role) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            if (rep->nflags >= FL_MAX_CHANNELS) break;
            FlChannelFlag *f = &rep->flags[rep->nflags++];
            snprintf(f->role, sizeof f->role, "%s", p->channels[i].role);
            f->ok = 0;
            snprintf(f->detail, sizeof f->detail, "extra role not in device doc (pin %d)",
                     p->channels[i].header_pin);
            rep->nmismatch++;
        }
    }
}

int fl_avionics_sample(const FlSim *sim, FlSensorSample *out, int cap) {
    if (!sim || !out || cap < 12) return -1;
    double V, a, b;
    fl_dyn_airdata(&sim->state, &V, &a, &b);
    int n = 0;
#define PUT(role_, val_)                         \
    do {                                         \
        if (n < cap) {                           \
            snprintf(out[n].role, sizeof out[n].role, "%s", role_); \
            out[n].value = (val_);               \
            n++;                                 \
        }                                        \
    } while (0)
    PUT("imu_ax", 0.0); /* scaffold: body accel stubs */
    PUT("imu_ay", 0.0);
    PUT("imu_az", FL_G0);
    PUT("imu_gx", sim->state.p);
    PUT("imu_gy", sim->state.q);
    PUT("imu_gz", sim->state.r);
    PUT("radio_throttle", sim->ctrl.throttle);
    PUT("radio_aileron", sim->ctrl.da_deg);
    PUT("radio_elevator", sim->ctrl.de_deg);
    PUT("radio_rudder", sim->ctrl.dr_deg);
    PUT("act_aileron", sim->ctrl.da_deg);
    PUT("act_elevator", sim->ctrl.de_deg);
    PUT("act_rudder", sim->ctrl.dr_deg);
    PUT("act_throttle", sim->ctrl.throttle);
#undef PUT
    (void)V;
    (void)a;
    (void)b;
    return n;
}

int fl_avionics_report_text(const FlAvionicsReport *rep, char *out, int out_cap) {
    if (!rep || !out || out_cap < 32) return -1;
    int used = 0;
    int w = snprintf(out, out_cap, "avionics co-check: mismatches=%d missing=%d\n",
                     rep->nmismatch, rep->nmissing);
    if (w < 0 || w >= out_cap) return -1;
    used = w;
    int i;
    for (i = 0; i < rep->nflags; i++) {
        w = snprintf(out + used, out_cap - used, "  %s: %s — %s\n", rep->flags[i].role,
                     rep->flags[i].ok ? "PASS" : "FLAG", rep->flags[i].detail);
        if (w < 0 || used + w >= out_cap) return -1;
        used += w;
    }
    return used;
}
