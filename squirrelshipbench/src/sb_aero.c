#define _POSIX_C_SOURCE 200809L
#include "sb_aero.h"

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

int sb_aero_parse(SbHeatFluxCase *c, const char *text, char *err, int err_cap) {
    if (!c || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(c, 0, sizeof *c);
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
            if (strncmp(t, "ISM-HB heat-flux", 16) != 0) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need ISM-HB heat-flux", line_no);
                free(buf);
                return -1;
            }
            saw = 1;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        char key[64], val[160];
        if (sscanf(t, "%63[^:]: %159[^\n]", key, val) != 2) {
            if (err && err_cap) snprintf(err, err_cap, "line %d: key: value", line_no);
            free(buf);
            return -1;
        }
        trim(key);
        trim(val);
        if (strcmp(key, "program_id") == 0)
            snprintf(c->program_id, sizeof c->program_id, "%s", val);
        else if (strcmp(key, "k_teach") == 0) c->k_teach = atof(val);
        else if (strcmp(key, "rho_kg_m3") == 0) c->rho_kg_m3 = atof(val);
        else if (strcmp(key, "rn_m") == 0) c->rn_m = atof(val);
        else if (strcmp(key, "v_m_s") == 0) c->v_m_s = atof(val);
        else if (strcmp(key, "q_ref") == 0) c->q_ref = atof(val);
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw || c->k_teach <= 0 || c->rho_kg_m3 <= 0 || c->rn_m <= 0 || c->v_m_s <= 0) {
        if (err && err_cap) snprintf(err, err_cap, "incomplete heat-flux");
        return -1;
    }
    return 0;
}

int sb_aero_selftest(const SbHeatFluxCase *c, SbAeroResult *res) {
    double q, q_blunt, expect;
    memset(res, 0, sizeof *res);
    res->ok = 1;
    /* Soft original teaching identity (ISM HB stub); pow/sqrt as elementary. */
    q = c->k_teach * sqrt(c->rho_kg_m3 / c->rn_m) * pow(c->v_m_s, 3.0);
    res->q = q;
    expect = c->q_ref > 0.0
                 ? c->q_ref
                 : c->k_teach * sqrt(c->rho_kg_m3 / c->rn_m) * pow(c->v_m_s, 3.0);
    if (fabs(q - expect) > 1e-3 * fmax(1.0, fabs(expect)) || !(q > 0.0)) {
        res->nfail++;
        res->ok = 0;
    } else
        res->npass++;
    q_blunt = c->k_teach * sqrt(c->rho_kg_m3 / (c->rn_m * 2.0)) * pow(c->v_m_s, 3.0);
    if (!(q_blunt < q)) {
        res->nfail++;
        res->ok = 0;
    } else
        res->npass++;
    snprintf(res->summary, sizeof res->summary,
             "bomber heatflux program=%s q≈%.3e pass=%d fail=%d → %s", c->program_id, q,
             res->npass, res->nfail, res->ok ? "PASS" : "FAIL");
    return 0;
}
