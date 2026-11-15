#define _POSIX_C_SOURCE 200809L
#include "fl_project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add_surface(FlProject *p, const char *name, double chord, double span,
                        double area, double hinge_x, double max_def, int ch) {
    if (p->nsurfaces >= FL_MAX_SURFACES) return;
    FlSurface *s = &p->surfaces[p->nsurfaces++];
    memset(s, 0, sizeof *s);
    snprintf(s->name, sizeof s->name, "%s", name);
    s->used = 1;
    s->chord_mm = chord;
    s->span_mm = span;
    s->area_mm2 = area;
    s->hinge_x_mm = hinge_x;
    s->max_deflect_deg = max_def;
    s->channel = ch;
}

static void add_channel(FlProject *p, const char *name, const char *role, int pin,
                        double scale, double bias) {
    if (p->nchannels >= FL_MAX_CHANNELS) return;
    FlChannel *c = &p->channels[p->nchannels++];
    memset(c, 0, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", name);
    snprintf(c->role, sizeof c->role, "%s", role);
    c->used = 1;
    c->header_pin = pin;
    c->scale = scale;
    c->bias = bias;
}

void fl_project_default_ism_fighter(FlProject *p) {
    memset(p, 0, sizeof *p);
    snprintf(p->name, sizeof p->name, "ism-fighter-v0");
    snprintf(p->notes, sizeof p->notes,
             "Scaffold ISM fighter; geometry placeholders pending hardware-doc lock");
    snprintf(p->device_doc_id, sizeof p->device_doc_id, "ism-fighter-avionics-bay-stub");

    p->fuselage_length_mm = 420.0;
    p->fuselage_scale = 1.0;
    p->wing_span_mm = 280.0;
    p->wing_root_chord_mm = 120.0;
    p->wing_tip_chord_mm = 80.0;
    p->wing_area_mm2 = 28000.0;
    p->wing_sweep_deg = 25.0;
    p->hstab_area_mm2 = 4500.0;
    p->vstab_area_mm2 = 3200.0;

    p->mass_g = 850.0;
    p->cg_x_mm = 180.0;
    p->cg_y_mm = 0.0;
    p->cg_z_mm = 5.0;
    p->ixx_g_mm2 = 2.5e7;
    p->iyy_g_mm2 = 8.0e7;
    p->izz_g_mm2 = 9.5e7;

    p->thrust_max_n = 12.0;
    p->throttle_idle = 0.05;

    add_surface(p, "elevator", 40.0, 100.0, 4000.0, 380.0, 25.0, 10);
    add_surface(p, "aileron_l", 30.0, 60.0, 1800.0, 200.0, 20.0, 11);
    add_surface(p, "aileron_r", 30.0, 60.0, 1800.0, 200.0, 20.0, 12);
    add_surface(p, "rudder", 35.0, 70.0, 2450.0, 400.0, 25.0, 13);

    /* KiCad-style header map stubs — pins match samples/device-doc-channels.txt */
    add_channel(p, "IMU_AX", "imu_ax", 1, 9.80665 / 2048.0, 0.0);
    add_channel(p, "IMU_AY", "imu_ay", 2, 9.80665 / 2048.0, 0.0);
    add_channel(p, "IMU_AZ", "imu_az", 3, 9.80665 / 2048.0, 0.0);
    add_channel(p, "IMU_GX", "imu_gx", 4, FL_DEG2RAD / 16.4, 0.0);
    add_channel(p, "IMU_GY", "imu_gy", 5, FL_DEG2RAD / 16.4, 0.0);
    add_channel(p, "IMU_GZ", "imu_gz", 6, FL_DEG2RAD / 16.4, 0.0);
    add_channel(p, "RAD_THR", "radio_throttle", 7, 1.0, 0.0);
    add_channel(p, "RAD_AIL", "radio_aileron", 8, 1.0, 0.0);
    add_channel(p, "RAD_ELE", "radio_elevator", 9, 1.0, 0.0);
    add_channel(p, "RAD_RUD", "radio_rudder", 10, 1.0, 0.0);
    add_channel(p, "ACT_ELE", "act_elevator", 11, 1.0, 0.0);
    add_channel(p, "ACT_AIL_L", "act_aileron_l", 12, 1.0, 0.0);
    add_channel(p, "ACT_AIL_R", "act_aileron_r", 13, 1.0, 0.0);
    add_channel(p, "ACT_RUD", "act_rudder", 14, 1.0, 0.0);
    add_channel(p, "ACT_ESC", "act_throttle", 15, 1.0, 0.0);
}

static int starts_with(const char *s, const char *p) {
    return strncmp(s, p, strlen(p)) == 0;
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

const FlSurface *fl_project_surface(const FlProject *p, const char *name) {
    int i;
    for (i = 0; i < p->nsurfaces; i++)
        if (p->surfaces[i].used && strcmp(p->surfaces[i].name, name) == 0)
            return &p->surfaces[i];
    return NULL;
}

const FlChannel *fl_project_channel(const FlProject *p, const char *role) {
    int i;
    for (i = 0; i < p->nchannels; i++)
        if (p->channels[i].used && strcmp(p->channels[i].role, role) == 0)
            return &p->channels[i];
    return NULL;
}

int fl_project_parse(FlProject *p, const char *text, char *err, int err_cap) {
    fl_project_default_ism_fighter(p);
    if (!text) {
        if (err && err_cap) snprintf(err, err_cap, "null text");
        return -1;
    }
    char *buf = strdup(text);
    if (!buf) {
        if (err && err_cap) snprintf(err, err_cap, "out of memory");
        return -1;
    }
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    int line_no = 0;
    int saw_header = 0;
    while (line) {
        line_no++;
        char *t = trim(line);
        if (!*t || *t == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (!saw_header) {
            if (!starts_with(t, "SquirrelFlightLab project")) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need 'SquirrelFlightLab project N'", line_no);
                free(buf);
                return -1;
            }
            saw_header = 1;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        char key[64], val[160];
        if (sscanf(t, "%63[^:]: %159[^\n]", key, val) != 2) {
            if (err && err_cap) snprintf(err, err_cap, "line %d: expected key: value", line_no);
            free(buf);
            return -1;
        }
        trim(key);
        trim(val);
        if (strcmp(key, "name") == 0) snprintf(p->name, sizeof p->name, "%s", val);
        else if (strcmp(key, "notes") == 0) snprintf(p->notes, sizeof p->notes, "%s", val);
        else if (strcmp(key, "device_doc_id") == 0)
            snprintf(p->device_doc_id, sizeof p->device_doc_id, "%s", val);
        else if (strcmp(key, "fuselage_length_mm") == 0) p->fuselage_length_mm = atof(val);
        else if (strcmp(key, "fuselage_scale") == 0) p->fuselage_scale = atof(val);
        else if (strcmp(key, "wing_span_mm") == 0) p->wing_span_mm = atof(val);
        else if (strcmp(key, "wing_root_chord_mm") == 0) p->wing_root_chord_mm = atof(val);
        else if (strcmp(key, "wing_tip_chord_mm") == 0) p->wing_tip_chord_mm = atof(val);
        else if (strcmp(key, "wing_area_mm2") == 0) p->wing_area_mm2 = atof(val);
        else if (strcmp(key, "wing_sweep_deg") == 0) p->wing_sweep_deg = atof(val);
        else if (strcmp(key, "hstab_area_mm2") == 0) p->hstab_area_mm2 = atof(val);
        else if (strcmp(key, "vstab_area_mm2") == 0) p->vstab_area_mm2 = atof(val);
        else if (strcmp(key, "mass_g") == 0) p->mass_g = atof(val);
        else if (strcmp(key, "cg_x_mm") == 0) p->cg_x_mm = atof(val);
        else if (strcmp(key, "cg_y_mm") == 0) p->cg_y_mm = atof(val);
        else if (strcmp(key, "cg_z_mm") == 0) p->cg_z_mm = atof(val);
        else if (strcmp(key, "ixx_g_mm2") == 0) p->ixx_g_mm2 = atof(val);
        else if (strcmp(key, "iyy_g_mm2") == 0) p->iyy_g_mm2 = atof(val);
        else if (strcmp(key, "izz_g_mm2") == 0) p->izz_g_mm2 = atof(val);
        else if (strcmp(key, "thrust_max_n") == 0) p->thrust_max_n = atof(val);
        else if (strcmp(key, "throttle_idle") == 0) p->throttle_idle = atof(val);
        /* unknown keys ignored so future fields stay forward-compatible */
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw_header) {
        if (err && err_cap) snprintf(err, err_cap, "missing project header");
        return -1;
    }
    if (p->fuselage_length_mm <= 0 || p->wing_area_mm2 <= 0 || p->mass_g <= 0) {
        if (err && err_cap) snprintf(err, err_cap, "fuselage, wing area and mass must be > 0");
        return -1;
    }
    return 0;
}

int fl_project_format(const FlProject *p, char *out, int out_cap) {
    if (!out || out_cap < 32) return -1;
    int n = snprintf(out, out_cap,
                     "SquirrelFlightLab project 1\n"
                     "name: %s\n"
                     "notes: %s\n"
                     "device_doc_id: %s\n"
                     "fuselage_length_mm: %.3f\n"
                     "fuselage_scale: %.6f\n"
                     "wing_span_mm: %.3f\n"
                     "wing_root_chord_mm: %.3f\n"
                     "wing_tip_chord_mm: %.3f\n"
                     "wing_area_mm2: %.3f\n"
                     "wing_sweep_deg: %.3f\n"
                     "hstab_area_mm2: %.3f\n"
                     "vstab_area_mm2: %.3f\n"
                     "mass_g: %.3f\n"
                     "cg_x_mm: %.3f\n"
                     "cg_y_mm: %.3f\n"
                     "cg_z_mm: %.3f\n"
                     "ixx_g_mm2: %.6g\n"
                     "iyy_g_mm2: %.6g\n"
                     "izz_g_mm2: %.6g\n"
                     "thrust_max_n: %.3f\n"
                     "throttle_idle: %.3f\n",
                     p->name, p->notes, p->device_doc_id, p->fuselage_length_mm,
                     p->fuselage_scale, p->wing_span_mm, p->wing_root_chord_mm,
                     p->wing_tip_chord_mm, p->wing_area_mm2, p->wing_sweep_deg,
                     p->hstab_area_mm2, p->vstab_area_mm2, p->mass_g, p->cg_x_mm,
                     p->cg_y_mm, p->cg_z_mm, p->ixx_g_mm2, p->iyy_g_mm2, p->izz_g_mm2,
                     p->thrust_max_n, p->throttle_idle);
    return (n < 0 || n >= out_cap) ? -1 : n;
}
