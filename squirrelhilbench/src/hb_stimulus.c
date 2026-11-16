#define _POSIX_C_SOURCE 200809L
#include "hb_stimulus.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

static int starts_with(const char *s, const char *p) {
    return strncmp(s, p, strlen(p)) == 0;
}

int hb_stimulus_parse(HbStimulus *s, const char *text, char *err, int err_cap) {
    if (!s || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(s, 0, sizeof *s);
    snprintf(s->name, sizeof s->name, "unnamed");
    s->dt_s = 0.01;
    s->duration_s = 1.0;

    char *buf = strdup(text);
    if (!buf) {
        if (err && err_cap) snprintf(err, err_cap, "oom");
        return -1;
    }
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    int saw = 0, line_no = 0;
    while (line) {
        line_no++;
        char *t = trim(line);
        if (!*t || *t == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (!saw) {
            if (!starts_with(t, "ISM-HIL stimulus")) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need ISM-HIL stimulus", line_no);
                free(buf);
                return -1;
            }
            saw = 1;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (starts_with(t, "step ")) {
            double ts = 0, val = 0;
            char ch[HB_NAME_LEN];
            if (sscanf(t + 5, "%lf %63s %lf", &ts, ch, &val) != 3) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: step t channel value", line_no);
                free(buf);
                return -1;
            }
            if (s->nsteps >= HB_STIM_MAX_STEPS) {
                if (err && err_cap) snprintf(err, err_cap, "too many steps");
                free(buf);
                return -1;
            }
            HbStimStep *st = &s->steps[s->nsteps++];
            st->t_s = ts;
            snprintf(st->channel, sizeof st->channel, "%s", ch);
            st->value = val;
        } else {
            char key[64], val[160];
            if (sscanf(t, "%63[^:]: %159[^\n]", key, val) != 2) {
                if (err && err_cap) snprintf(err, err_cap, "line %d: key: value", line_no);
                free(buf);
                return -1;
            }
            trim(key);
            trim(val);
            if (strcmp(key, "name") == 0) snprintf(s->name, sizeof s->name, "%s", val);
            else if (strcmp(key, "dt_s") == 0) s->dt_s = atof(val);
            else if (strcmp(key, "duration_s") == 0) s->duration_s = atof(val);
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw) {
        if (err && err_cap) snprintf(err, err_cap, "missing header");
        return -1;
    }
    if (s->dt_s <= 0 || s->duration_s <= 0) {
        if (err && err_cap) snprintf(err, err_cap, "bad dt/duration");
        return -1;
    }
    return 0;
}

static double active_value(const HbStimulus *s, const char *channel, double t) {
    double v = 0.0;
    int i;
    for (i = 0; i < s->nsteps; i++) {
        if (strcmp(s->steps[i].channel, channel) == 0 && s->steps[i].t_s <= t + 1e-12)
            v = s->steps[i].value;
    }
    return v;
}

int hb_stimulus_run(const HbProfile *p, const HbStimulus *s, HbStimResult *out, char *err,
                    int err_cap) {
    if (!p || !s || !out) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(out, 0, sizeof *out);
    if (!p->connected) {
        if (err && err_cap) snprintf(err, err_cap, "profile not connected");
        return -1;
    }
    if (p->estop_latched) {
        if (err && err_cap) snprintf(err, err_cap, "estop latched — stimulus blocked");
        return -1;
    }

    snprintf(out->script_name, sizeof out->script_name, "%s", s->name);
    snprintf(out->aircraft_id, sizeof out->aircraft_id, "%s", p->aircraft_id);

    /*
     * Lightweight mock plant for HIL log shape only. Full aero / FCS lives in
     * squirrelflightlab/; this fills Flight Lab–compatible history columns so
     * operators can hand logs across for replay naming / tooling.
     */
    double V = 80.0;
    double alpha = 2.0, beta = 0.0;
    double theta = 2.0, phi = 0.0, psi = 0.0;
    double x = 0.0, y = 0.0, z = -1000.0;
    double t;
    for (t = 0.0; t <= s->duration_s + 1e-12; t += s->dt_s) {
        if (out->nsamples >= HB_LOG_MAX_SAMPLES) {
            if (err && err_cap) snprintf(err, err_cap, "log sample cap");
            return -1;
        }
        double stick_x = active_value(s, "inceptor.side_stick_x", t);
        double stick_y = active_value(s, "inceptor.side_stick_y", t);
        double rudder = active_value(s, "inceptor.pedals", t);
        double throttle = active_value(s, "inceptor.throttle", t);
        double de = active_value(s, "actuator.cmd.eha_ht", t);
        double da = active_value(s, "actuator.cmd.eha_aileron", t);
        double dr = active_value(s, "actuator.cmd.eha_rudder", t);

        double pitch_cmd = stick_y + de * 0.1;
        double roll_cmd = stick_x + da * 0.1;
        double yaw_cmd = rudder + dr * 0.1;
        V += (throttle - 0.5) * 2.0 * s->dt_s;
        if (V < 40.0) V = 40.0;
        if (V > 200.0) V = 200.0;
        alpha += (pitch_cmd * 0.5 - (alpha - 2.0) * 0.2) * s->dt_s;
        beta += (yaw_cmd * 0.3 - beta * 0.3) * s->dt_s;
        theta += pitch_cmd * 2.0 * s->dt_s;
        phi += roll_cmd * 3.0 * s->dt_s;
        psi += (yaw_cmd * 1.5 + phi * 0.05) * s->dt_s;
        double nz = 1.0 + pitch_cmd * 0.05;
        double vx = V * cos(theta * 0.017453292519943295);
        double vz = V * sin(theta * 0.017453292519943295);
        x += vx * s->dt_s;
        y += (V * sin(psi * 0.017453292519943295) * 0.1) * s->dt_s;
        z -= vz * s->dt_s;

        HbLogSample *h = &out->samples[out->nsamples++];
        h->t_s = t;
        h->V_mps = V;
        h->alpha_deg = alpha;
        h->beta_deg = beta;
        h->theta_deg = theta;
        h->phi_deg = phi;
        h->psi_deg = psi;
        h->nz = nz;
        h->x_m = x;
        h->y_m = y;
        h->z_m = z;
    }

    out->ok = 1;
    snprintf(out->summary, sizeof out->summary,
             "PASS stimulus=%s samples=%d duration=%.2f s aircraft=%s", s->name, out->nsamples,
             s->duration_s, p->aircraft_id);
    return 0;
}
