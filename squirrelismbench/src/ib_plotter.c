#define _POSIX_C_SOURCE 200809L
#include "ib_plotter.h"

#include "squirrelnum.h"

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

int ib_plotter_parse(IbPlotterIface *p, const char *text, char *err, int err_cap) {
    if (!p || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(p, 0, sizeof *p);
    snprintf(p->status, sizeof p->status, "soft_proposed");
    p->axes = 2;
    p->x_steps_per_mm = 80.0;
    p->y_steps_per_mm = 80.0;
    p->workspace_x_mm = 300.0;
    p->workspace_y_mm = 200.0;

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
            if (strncmp(t, "ISM-PLT plotter interface", 25) != 0) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need ISM-PLT plotter interface", line_no);
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
        if (strcmp(key, "status") == 0) snprintf(p->status, sizeof p->status, "%s", val);
        else if (strcmp(key, "axes") == 0) p->axes = atoi(val);
        else if (strcmp(key, "x_steps_per_mm") == 0) p->x_steps_per_mm = atof(val);
        else if (strcmp(key, "y_steps_per_mm") == 0) p->y_steps_per_mm = atof(val);
        else if (strcmp(key, "workspace_mm") == 0) {
            if (sscanf(val, "%lf %lf", &p->workspace_x_mm, &p->workspace_y_mm) != 2) {
                if (err && err_cap) snprintf(err, err_cap, "line %d: workspace_mm X Y", line_no);
                free(buf);
                return -1;
            }
        } else if (strcmp(key, "waypoint") == 0) {
            double x = 0, y = 0;
            if (sscanf(val, "%lf %lf", &x, &y) != 2) {
                if (err && err_cap) snprintf(err, err_cap, "line %d: waypoint X Y", line_no);
                free(buf);
                return -1;
            }
            if (p->nwp >= IB_PLT_MAX_WP) {
                if (err && err_cap) snprintf(err, err_cap, "too many waypoints");
                free(buf);
                return -1;
            }
            p->wp_x[p->nwp] = x;
            p->wp_y[p->nwp] = y;
            p->nwp++;
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw) {
        if (err && err_cap) snprintf(err, err_cap, "missing header");
        return -1;
    }
    if (p->axes != 2 || p->x_steps_per_mm <= 0 || p->y_steps_per_mm <= 0) {
        if (err && err_cap) snprintf(err, err_cap, "bad plotter geometry");
        return -1;
    }
    if (p->nwp < 2) {
        if (err && err_cap) snprintf(err, err_cap, "need >=2 waypoints");
        return -1;
    }
    return 0;
}

static double chord_len(const IbPlotterIface *p) {
    double s = 0;
    int i;
    for (i = 1; i < p->nwp; i++) {
        double dx = p->wp_x[i] - p->wp_x[i - 1];
        double dy = p->wp_y[i] - p->wp_y[i - 1];
        s += sqrt(dx * dx + dy * dy);
    }
    return s;
}

int ib_plotter_selftest(const IbPlotterIface *p, IbPlotterTestResult *res) {
    int i, k;
    memset(res, 0, sizeof *res);
    res->ok = 1;

    for (i = 0; i < p->nwp; i++) {
        if (p->wp_x[i] < 0 || p->wp_y[i] < 0 || p->wp_x[i] > p->workspace_x_mm ||
            p->wp_y[i] > p->workspace_y_mm) {
            res->nfail++;
            res->ok = 0;
        }
    }
    if (res->nfail == 0) res->npass++;

    res->path_mm = chord_len(p);
    if (!(res->path_mm > 0.0)) {
        res->nfail++;
        res->ok = 0;
    } else
        res->npass++;

    /*
     * NR Ch. 3.1 linear interpolation: sample each segment at mid-parameter
     * and confirm lerp(t=0.5) matches the geometric midpoint.
     */
    for (i = 1; i < p->nwp; i++) {
        double tx[2] = {0.0, 1.0};
        double xs[2] = {p->wp_x[i - 1], p->wp_x[i]};
        double ys[2] = {p->wp_y[i - 1], p->wp_y[i]};
        double xq = 0, yq = 0;
        if (sqn_lerp(tx, xs, 2, 0.5, &xq) != SQN_OK ||
            sqn_lerp(tx, ys, 2, 0.5, &yq) != SQN_OK) {
            res->nfail++;
            res->ok = 0;
            continue;
        }
        double xm = 0.5 * (xs[0] + xs[1]);
        double ym = 0.5 * (ys[0] + ys[1]);
        if (fabs(xq - xm) > 1e-9 || fabs(yq - ym) > 1e-9) {
            res->nfail++;
            res->ok = 0;
        } else
            res->npass++;
    }

    /* Step counts for last hop must be non-negative integers in Soft stub. */
    k = p->nwp - 1;
    {
        double dx = fabs(p->wp_x[k] - p->wp_x[k - 1]);
        double dy = fabs(p->wp_y[k] - p->wp_y[k - 1]);
        long sx = (long)llround(dx * p->x_steps_per_mm);
        long sy = (long)llround(dy * p->y_steps_per_mm);
        if (sx < 0 || sy < 0) {
            res->nfail++;
            res->ok = 0;
        } else
            res->npass++;
    }

    snprintf(res->summary, sizeof res->summary,
             "plotter selftest status=%s wp=%d path_mm=%.3f pass=%d fail=%d "
             "(NR Ch.3.1 lerp) → %s",
             p->status, p->nwp, res->path_mm, res->npass, res->nfail,
             res->ok ? "PASS" : "FAIL");
    return 0;
}
