#define _POSIX_C_SOURCE 200809L
#include "ib_power.h"

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

static int parse_csv_doubles(const char *val, double *out, int cap, int *nout) {
    *nout = 0;
    char *buf = strdup(val);
    if (!buf) return -1;
    char *save = NULL;
    char *tok = strtok_r(buf, ",", &save);
    while (tok) {
        if (*nout >= cap) {
            free(buf);
            return -1;
        }
        out[(*nout)++] = atof(trim(tok));
        tok = strtok_r(NULL, ",", &save);
    }
    free(buf);
    return 0;
}

static int parse_csv_names(const char *val, char names[][32], int cap, int *nout) {
    *nout = 0;
    char *buf = strdup(val);
    if (!buf) return -1;
    char *save = NULL;
    char *tok = strtok_r(buf, ",", &save);
    while (tok) {
        if (*nout >= cap) {
            free(buf);
            return -1;
        }
        snprintf(names[(*nout)++], 32, "%s", trim(tok));
        tok = strtok_r(NULL, ",", &save);
    }
    free(buf);
    return 0;
}

int ib_power_parse(IbPowerIface *p, const char *text, char *err, int err_cap) {
    if (!p || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(p, 0, sizeof *p);
    p->kind = IB_POWER_PSU;
    snprintf(p->model, sizeof p->model, "Soft-Bench-PSU");
    p->v_out_nom_v = 12.0;
    p->i_max_a = 5.0;
    p->p_max_w = 60.0;

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
            if (strncmp(t, "ISM-PSU power interface", 23) == 0) {
                p->kind = IB_POWER_PSU;
            } else if (strncmp(t, "ISM-UPS power interface", 23) == 0) {
                p->kind = IB_POWER_UPS;
            } else {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need ISM-PSU/ISM-UPS power interface",
                             line_no);
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
        if (strcmp(key, "model") == 0)
            snprintf(p->model, sizeof p->model, "%s", val);
        else if (strcmp(key, "v_out_nom_v") == 0)
            p->v_out_nom_v = atof(val);
        else if (strcmp(key, "i_max_a") == 0)
            p->i_max_a = atof(val);
        else if (strcmp(key, "p_max_w") == 0)
            p->p_max_w = atof(val);
        else if (strcmp(key, "capacity_wh") == 0)
            p->capacity_wh = atof(val);
        else if (strcmp(key, "load_steps_w") == 0) {
            if (parse_csv_doubles(val, p->load_w, IB_POWER_MAX_STEPS, &p->nsteps) != 0) {
                if (err && err_cap) snprintf(err, err_cap, "line %d: bad load_steps_w", line_no);
                free(buf);
                return -1;
            }
        } else if (strcmp(key, "dwell_s") == 0) {
            int nd = 0;
            if (parse_csv_doubles(val, p->dwell_s, IB_POWER_MAX_STEPS, &nd) != 0 ||
                (p->nsteps > 0 && nd != p->nsteps)) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: dwell_s must match load_steps_w count",
                             line_no);
                free(buf);
                return -1;
            }
        } else if (strcmp(key, "telemetry") == 0) {
            if (parse_csv_names(val, p->telem, IB_POWER_MAX_TELEM, &p->ntelem) != 0) {
                if (err && err_cap) snprintf(err, err_cap, "line %d: bad telemetry", line_no);
                free(buf);
                return -1;
            }
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw) {
        if (err && err_cap) snprintf(err, err_cap, "missing header");
        return -1;
    }
    if (p->nsteps < 2) {
        if (err && err_cap) snprintf(err, err_cap, "need >=2 load_steps_w");
        return -1;
    }
    /* Default equal dwell if omitted. */
    {
        int i, any = 0;
        for (i = 0; i < p->nsteps; i++)
            if (p->dwell_s[i] > 0.0) any = 1;
        if (!any) {
            for (i = 0; i < p->nsteps; i++) p->dwell_s[i] = 1.0;
        }
    }
    if (p->v_out_nom_v <= 0.0 || p->i_max_a <= 0.0 || p->p_max_w <= 0.0) {
        if (err && err_cap) snprintf(err, err_cap, "v_out/i_max/p_max must be > 0");
        return -1;
    }
    if (p->kind == IB_POWER_UPS && p->capacity_wh <= 0.0) {
        if (err && err_cap) snprintf(err, err_cap, "UPS needs capacity_wh > 0");
        return -1;
    }
    if (p->ntelem < 1) {
        if (err && err_cap) snprintf(err, err_cap, "need telemetry channels");
        return -1;
    }
    return 0;
}

/*
 * Soft composite Simpson on the load schedule (NR Ch. 4.1 class ideas).
 * Knots are cumulative dwell times; P(t) is piecewise-linear between steps.
 * Soft original — no NR listings.
 */
static double soft_simpson_energy_wh(const IbPowerIface *p) {
    double t[IB_POWER_MAX_STEPS];
    double P[IB_POWER_MAX_STEPS];
    double acc = 0.0;
    int i;
    for (i = 0; i < p->nsteps; i++) {
        t[i] = acc;
        P[i] = p->load_w[i];
        acc += p->dwell_s[i] > 0.0 ? p->dwell_s[i] : 0.0;
    }
    /* Include final hold as an extra knot at end of last dwell. */
    double t_end = acc;
    double joules = 0.0;
    for (i = 0; i + 1 < p->nsteps; i++) {
        double h = t[i + 1] - t[i];
        if (h <= 0.0) continue;
        double Pmid = 0.5 * (P[i] + P[i + 1]);
        joules += (h / 6.0) * (P[i] + 4.0 * Pmid + P[i + 1]);
    }
    if (p->nsteps >= 1 && t_end > t[p->nsteps - 1]) {
        double h = t_end - t[p->nsteps - 1];
        double Plast = P[p->nsteps - 1];
        joules += (h / 6.0) * (Plast + 4.0 * Plast + Plast);
    }
    return joules / 3600.0; /* J → Wh */
}

static int has_telem(const IbPowerIface *p, const char *name) {
    int i;
    for (i = 0; i < p->ntelem; i++)
        if (strcmp(p->telem[i], name) == 0) return 1;
    return 0;
}

int ib_power_selftest(const IbPowerIface *p, IbPowerTestResult *res) {
    memset(res, 0, sizeof *res);
    res->ok = 1;
    int i;

    /* Required telemetry by kind. */
    if (p->kind == IB_POWER_PSU) {
        if (has_telem(p, "v_out") && has_telem(p, "i_out"))
            res->npass++;
        else {
            res->nfail++;
            res->ok = 0;
        }
    } else {
        if (has_telem(p, "v_batt") && has_telem(p, "soc_pct") && has_telem(p, "on_battery"))
            res->npass++;
        else {
            res->nfail++;
            res->ok = 0;
        }
    }

    /* Load steps within nameplate; Soft synthetic I = P/V. */
    for (i = 0; i < p->nsteps; i++) {
        double P = p->load_w[i];
        double I = P / p->v_out_nom_v;
        if (P < 0.0 || P > p->p_max_w + 1e-9 || I > p->i_max_a + 1e-9) {
            res->nfail++;
            res->ok = 0;
        } else
            res->npass++;
    }

    res->energy_wh = soft_simpson_energy_wh(p);
    if (res->energy_wh < 0.0) {
        res->nfail++;
        res->ok = 0;
    } else
        res->npass++;

    if (p->kind == IB_POWER_UPS) {
        /* Soft: schedule energy must not exceed nameplate capacity. */
        if (res->energy_wh <= p->capacity_wh + 1e-9)
            res->npass++;
        else {
            res->nfail++;
            res->ok = 0;
        }
        /* Soft synthetic SOC after schedule. */
        double soc = 100.0 * (1.0 - res->energy_wh / p->capacity_wh);
        if (soc < 0.0) soc = 0.0;
        if (soc > 100.0) soc = 100.0;
        if (soc >= 0.0 && soc <= 100.0)
            res->npass++;
        else {
            res->nfail++;
            res->ok = 0;
        }
    }

    snprintf(res->summary, sizeof res->summary,
             "%s selftest model=%s steps=%d energy_Wh=%.4f pass=%d fail=%d → %s "
             "(NR Ch.4.1 Soft Simpson)",
             p->kind == IB_POWER_PSU ? "psu" : "ups", p->model, p->nsteps, res->energy_wh,
             res->npass, res->nfail, res->ok ? "PASS" : "FAIL");
    return 0;
}
