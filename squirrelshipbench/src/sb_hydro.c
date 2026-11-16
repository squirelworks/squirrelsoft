#define _POSIX_C_SOURCE 200809L
#include "sb_hydro.h"

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

/*
 * Composite Simpson 1/3 on equally spaced stations. Half-breadth y(x); full
 * waterplane breadth b=2y ⇒ I_T = ∫ b³/12 dx = (2/3) ∫ y³ dx.
 * Constant y=B/2 on [0,L] recovers I_T = L B³ / 12 (ISM teaching identity).
 *
 * NR 3rd ed. Ch. 4.1 (Classical Formulas for Equally Spaced Abscissas) —
 * Simpson's rule class; Soft original, no NR listings.
 */
static double simpson_it(const SbHydroCase *c) {
    int n = c->nord;
    int i;
    double h, sum;
    if (n < 3 || ((n - 1) % 2) != 0) return -1.0;
    h = (c->station_m[n - 1] - c->station_m[0]) / (double)(n - 1);
    if (!(h > 0.0)) return -1.0;
    for (i = 1; i < n; i++) {
        double hi = c->station_m[i] - c->station_m[i - 1];
        if (fabs(hi - h) > 1e-9 * fmax(1.0, fabs(h))) return -1.0;
    }
    sum = pow(c->half_breadth_m[0], 3.0) + pow(c->half_breadth_m[n - 1], 3.0);
    for (i = 1; i < n - 1; i++) {
        double y3 = pow(c->half_breadth_m[i], 3.0);
        sum += (i % 2 == 1) ? 4.0 * y3 : 2.0 * y3;
    }
    /* ∫ y³ dx ≈ (h/3)*sum ; I_T = (2/3) ∫ y³ dx */
    return (2.0 / 3.0) * ((h / 3.0) * sum);
}

int sb_hydro_parse(SbHydroCase *c, const char *text, char *err, int err_cap) {
    if (!c || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(c, 0, sizeof *c);
    c->rho_kg_m3 = 1025.0;
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
            if (strncmp(t, "ISM-WS hydrostatics", 19) != 0) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need ISM-WS hydrostatics", line_no);
                free(buf);
                return -1;
            }
            saw = 1;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        char key[64], val[200];
        if (sscanf(t, "%63[^:]: %199[^\n]", key, val) != 2) {
            if (err && err_cap) snprintf(err, err_cap, "line %d: key: value", line_no);
            free(buf);
            return -1;
        }
        trim(key);
        trim(val);
        if (strcmp(key, "ship_id") == 0) snprintf(c->ship_id, sizeof c->ship_id, "%s", val);
        else if (strcmp(key, "rho_kg_m3") == 0) c->rho_kg_m3 = atof(val);
        else if (strcmp(key, "length_wl_m") == 0) c->length_wl_m = atof(val);
        else if (strcmp(key, "beam_wl_m") == 0) c->beam_wl_m = atof(val);
        else if (strcmp(key, "volume_m3") == 0) c->volume_m3 = atof(val);
        else if (strcmp(key, "kb_m") == 0) c->kb_m = atof(val);
        else if (strcmp(key, "kg_m") == 0) c->kg_m = atof(val);
        else if (strcmp(key, "i_t_ref_m4") == 0) c->i_t_ref_m4 = atof(val);
        else if (strcmp(key, "bm_ref_m") == 0) c->bm_ref_m = atof(val);
        else if (strcmp(key, "gm_ref_m") == 0) c->gm_ref_m = atof(val);
        else if (strcmp(key, "ordinate") == 0) {
            double s = 0, y = 0;
            if (sscanf(val, "%lf %lf", &s, &y) != 2) {
                if (err && err_cap) snprintf(err, err_cap, "line %d: ordinate s y", line_no);
                free(buf);
                return -1;
            }
            if (c->nord >= SB_MAX_ORD) {
                if (err && err_cap) snprintf(err, err_cap, "too many ordinates");
                free(buf);
                return -1;
            }
            c->station_m[c->nord] = s;
            c->half_breadth_m[c->nord] = y;
            c->nord++;
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw || c->volume_m3 <= 0 || c->nord < 3) {
        if (err && err_cap) snprintf(err, err_cap, "incomplete hydrostatics");
        return -1;
    }
    return 0;
}

int sb_hydro_selftest(const SbHydroCase *c, SbHydroResult *res) {
    double delta_t, bm, gm, it;
    memset(res, 0, sizeof *res);
    res->ok = 1;

    delta_t = (c->rho_kg_m3 * c->volume_m3) / 1000.0;
    if (fabs(delta_t - 3587.5) > 1.0) {
        res->nfail++;
        res->ok = 0;
    } else
        res->npass++;

    it = simpson_it(c);
    res->i_t_simpson = it;
    if (!(it > 0.0) || fabs(it - c->i_t_ref_m4) > 0.5) {
        res->nfail++;
        res->ok = 0;
    } else
        res->npass++;

    bm = it / c->volume_m3;
    gm = (c->kb_m + bm) - c->kg_m;
    res->bm_m = bm;
    res->gm_m = gm;
    if (fabs(bm - c->bm_ref_m) > 0.02 || fabs(gm - c->gm_ref_m) > 0.02 || !(gm > 0.0)) {
        res->nfail++;
        res->ok = 0;
    } else
        res->npass++;

    snprintf(res->summary, sizeof res->summary,
             "warship hydro ship=%s I_T=%.3f BM=%.3f GM=%.3f pass=%d fail=%d "
             "(NR Ch.4.1 Simpson) → %s",
             c->ship_id, it, bm, gm, res->npass, res->nfail, res->ok ? "PASS" : "FAIL");
    return 0;
}
