#define _POSIX_C_SOURCE 200809L
#include "fl_ism.h"

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

static FlChannel *ensure_channel(FlProject *p, const char *role) {
    int i;
    for (i = 0; i < p->nchannels; i++)
        if (p->channels[i].used && strcmp(p->channels[i].role, role) == 0)
            return &p->channels[i];
    if (p->nchannels >= FL_MAX_CHANNELS) return NULL;
    FlChannel *c = &p->channels[p->nchannels++];
    memset(c, 0, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", role);
    snprintf(c->role, sizeof c->role, "%s", role);
    c->used = 1;
    c->header_pin = -1;
    c->scale = 1.0;
    return c;
}

static FlSurface *ensure_surface(FlProject *p, const char *name) {
    int i;
    for (i = 0; i < p->nsurfaces; i++)
        if (p->surfaces[i].used && strcmp(p->surfaces[i].name, name) == 0)
            return &p->surfaces[i];
    if (p->nsurfaces >= FL_MAX_SURFACES) return NULL;
    FlSurface *s = &p->surfaces[p->nsurfaces++];
    memset(s, 0, sizeof *s);
    snprintf(s->name, sizeof s->name, "%s", name);
    s->used = 1;
    s->channel = -1;
    s->max_deflect_deg = 25.0;
    return s;
}

int fl_ism_parse_params(FlProject *p, FlIsmMeta *meta, const char *text,
                        char *err, int err_cap) {
    if (!p || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    fl_project_default_ism_fighter(p);
    if (meta) {
        memset(meta, 0, sizeof *meta);
        meta->loaded_params = 0;
    }
    char *buf = strdup(text);
    if (!buf) {
        if (err && err_cap) snprintf(err, err_cap, "out of memory");
        return -1;
    }
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    int line_no = 0, saw = 0;
    while (line) {
        line_no++;
        char *t = trim(line);
        if (!*t || *t == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (!saw) {
            if (!starts_with(t, "ISM-FJ parameters")) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need 'ISM-FJ parameters N'", line_no);
                free(buf);
                return -1;
            }
            saw = 1;
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
        if (strcmp(key, "vehicle_id") == 0) {
            snprintf(p->name, sizeof p->name, "%s", val);
            if (meta) snprintf(meta->vehicle_id, sizeof meta->vehicle_id, "%s", val);
        } else if (strcmp(key, "source_note") == 0) {
            snprintf(p->notes, sizeof p->notes, "%s", val);
            if (meta) snprintf(meta->source_note, sizeof meta->source_note, "%s", val);
        } else if (strcmp(key, "device_doc_id") == 0)
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
        else if (starts_with(key, "surface.")) {
            /* surface.elevator.chord_mm: 40 */
            char sname[64], field[64];
            if (sscanf(key, "surface.%63[^.].%63s", sname, field) == 2) {
                FlSurface *s = ensure_surface(p, sname);
                if (s) {
                    if (strcmp(field, "chord_mm") == 0) s->chord_mm = atof(val);
                    else if (strcmp(field, "span_mm") == 0) s->span_mm = atof(val);
                    else if (strcmp(field, "area_mm2") == 0) s->area_mm2 = atof(val);
                    else if (strcmp(field, "hinge_x_mm") == 0) s->hinge_x_mm = atof(val);
                    else if (strcmp(field, "max_deflect_deg") == 0) s->max_deflect_deg = atof(val);
                }
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw) {
        if (err && err_cap) snprintf(err, err_cap, "missing ISM-FJ parameters header");
        return -1;
    }
    /* Derive wing area if omitted */
    if (p->wing_area_mm2 <= 0.0 && p->wing_span_mm > 0.0) {
        double cavg = 0.5 * (p->wing_root_chord_mm + p->wing_tip_chord_mm);
        p->wing_area_mm2 = p->wing_span_mm * cavg;
    }
    if (meta) meta->loaded_params = 1;
    return 0;
}

int fl_ism_parse_interface(FlProject *p, FlIsmMeta *meta, const char *text,
                           char *err, int err_cap) {
    if (!p || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    char *buf = strdup(text);
    if (!buf) {
        if (err && err_cap) snprintf(err, err_cap, "out of memory");
        return -1;
    }
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    int line_no = 0, saw = 0;
    while (line) {
        line_no++;
        char *t = trim(line);
        if (!*t || *t == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (!saw) {
            if (!starts_with(t, "ISM-FJ interface")) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need 'ISM-FJ interface N'", line_no);
                free(buf);
                return -1;
            }
            saw = 1;
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
        if (strcmp(key, "device_doc_id") == 0)
            snprintf(p->device_doc_id, sizeof p->device_doc_id, "%s", val);
        else if (starts_with(key, "channel ")) {
            const char *role = key + 8;
            while (*role == ' ') role++;
            FlChannel *c = ensure_channel(p, role);
            if (!c) {
                if (err && err_cap) snprintf(err, err_cap, "too many channels");
                free(buf);
                return -1;
            }
            /* val: pin N [header Jx] [trunk Ax] */
            int pin = -1;
            char header[32] = "", trunk[32] = "";
            if (sscanf(val, "pin %d header %31s trunk %31s", &pin, header, trunk) >= 1) {
                c->header_pin = pin;
            } else if (sscanf(val, "pin %d", &pin) == 1) {
                c->header_pin = pin;
            }
            (void)header;
            (void)trunk;
        } else if (starts_with(key, "surface ")) {
            const char *sname = key + 8;
            while (*sname == ' ') sname++;
            FlSurface *s = ensure_surface(p, sname);
            char role[64];
            int ch = -1;
            if (sscanf(val, "channel %63s", role) == 1) {
                const FlChannel *c = fl_project_channel(p, role);
                if (c) ch = c->header_pin;
                if (s) s->channel = ch;
            } else if (sscanf(val, "pin %d", &ch) == 1) {
                if (s) s->channel = ch;
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw) {
        if (err && err_cap) snprintf(err, err_cap, "missing ISM-FJ interface header");
        return -1;
    }
    if (meta) meta->loaded_iface = 1;
    return 0;
}

static int near(double a, double b, double tol) { return fabs(a - b) <= tol; }

int fl_ism_verify_project(const FlProject *p, const FlIsmMeta *meta, FlIsmVerify *out) {
    if (!p || !out) return -1;
    memset(out, 0, sizeof *out);
    out->ok = 1;
    /* Soft checks: study-model length class from published ISM shell defaults. */
    out->nchecks = 3;
    if (!near(p->fuselage_length_mm, 420.0, 0.5)) {
        out->nfail++;
        out->ok = 0;
    }
    if (!near(p->wing_span_mm, 280.0, 0.5)) {
        out->nfail++;
        out->ok = 0;
    }
    if (p->wing_sweep_deg < 30.0 || p->wing_sweep_deg > 40.0) {
        /* ISM study uses ~35°; allow band until Victor freezes. */
        out->nfail++;
        out->ok = 0;
    }
    snprintf(out->summary, sizeof out->summary,
             "ISM verify vehicle=%s params=%d iface=%d checks=%d fail=%d → %s",
             meta && meta->vehicle_id[0] ? meta->vehicle_id : p->name,
             meta ? meta->loaded_params : 0, meta ? meta->loaded_iface : 0, out->nchecks,
             out->nfail, out->ok ? "PASS" : "FAIL");
    return 0;
}
