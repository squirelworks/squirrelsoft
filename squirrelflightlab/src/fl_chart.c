#include "fl_chart.h"

#include "squirrelnum.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void fl_chart_style_default(FlChartStyle *st) {
    memset(st, 0, sizeof *st);
    st->nch = 2;
    st->ch[0] = FL_CHART_V;
    st->ch[1] = FL_CHART_NZ;
    st->w = 640;
    st->h = 360;
    st->bg = SQG_RGB(24, 28, 32);
    st->grid = SQG_RGB(48, 56, 64);
    st->axis = SQG_RGB(180, 188, 196);
    st->trace[0] = SQG_RGB(80, 200, 120);
    st->trace[1] = SQG_RGB(80, 160, 240);
    st->trace[2] = SQG_RGB(240, 180, 60);
    st->trace[3] = SQG_RGB(220, 100, 140);
}

static double sample_ch(const FlHistSample *h, FlChartChannel ch) {
    switch (ch) {
    case FL_CHART_V:
        return h->V;
    case FL_CHART_NZ:
        return h->nz;
    case FL_CHART_ALPHA:
        return h->alpha_deg;
    case FL_CHART_THETA:
        return h->theta_deg;
    default:
        return 0.0;
    }
}

static const char *ch_name(FlChartChannel ch) {
    switch (ch) {
    case FL_CHART_V:
        return "V";
    case FL_CHART_NZ:
        return "nz";
    case FL_CHART_ALPHA:
        return "alpha";
    case FL_CHART_THETA:
        return "theta";
    default:
        return "?";
    }
}

int fl_chart_draw(sqg_canvas *c, sqg_rect r, const FlSim *sim, const FlChartStyle *st) {
    int i, col, segs = 0;
    if (!c || !sim || !st || sim->nhist < 2 || st->nch < 1 || r.w < 8 || r.h < 8)
        return -1;

    sqg_fill(c, r, st->bg);
    /* Grid */
    for (i = 1; i < 4; i++) {
        int y = r.y + (r.h * i) / 4;
        sqg_hline(c, r.x, r.x + r.w - 1, y, st->grid);
    }
    for (i = 1; i < 8; i++) {
        int x = r.x + (r.w * i) / 8;
        sqg_vline(c, x, r.y, r.y + r.h - 1, st->grid);
    }
    sqg_frame(c, r, st->axis);

    {
        char title[96];
        snprintf(title, sizeof title, "Flight Lab strip  n=%d", sim->nhist);
        sqg_text(c, r.x + 4, r.y + 4, title, st->axis, 1);
    }

    for (col = 0; col < st->nch && col < 4; col++) {
        FlChartChannel ch = st->ch[col];
        double t[FL_MAX_HISTORY], y[FL_MAX_HISTORY];
        double ymin = 1e300, ymax = -1e300;
        int n = sim->nhist;
        int px0 = -1, py0 = -1;
        if (n > FL_MAX_HISTORY) n = FL_MAX_HISTORY;
        for (i = 0; i < n; i++) {
            t[i] = sim->hist[i].t;
            y[i] = sample_ch(&sim->hist[i], ch);
            if (y[i] < ymin) ymin = y[i];
            if (y[i] > ymax) ymax = y[i];
        }
        if (!(ymax > ymin)) {
            ymax = ymin + 1.0;
            ymin -= 1.0;
        } else {
            double pad = 0.05 * (ymax - ymin);
            ymin -= pad;
            ymax += pad;
        }

        /* Resample onto each pixel column via NR Ch. 3.1 linear interp. */
        for (i = 0; i < r.w; i++) {
            double u = (r.w == 1) ? 0.0 : (double)i / (double)(r.w - 1);
            double tq = t[0] + u * (t[n - 1] - t[0]);
            double yq = 0.0;
            if (sqn_lerp(t, y, n, tq, &yq) != SQN_OK) continue;
            double vn = (yq - ymin) / (ymax - ymin);
            if (vn < 0.0) vn = 0.0;
            if (vn > 1.0) vn = 1.0;
            int px = r.x + i;
            int py = r.y + r.h - 1 - (int)(vn * (double)(r.h - 1) + 0.5);
            if (px0 >= 0) {
                sqg_line(c, px0, py0, px, py, st->trace[col]);
                segs++;
            }
            px0 = px;
            py0 = py;
        }
        {
            char lab[48];
            snprintf(lab, sizeof lab, "%s", ch_name(ch));
            sqg_text(c, r.x + 4 + col * 48, r.y + r.h - 12, lab, st->trace[col], 1);
        }
    }
    return segs;
}

int fl_chart_render_demo(sqg_canvas *c, const FlProject *p, const FlChartStyle *st,
                         double duration_s, double dt) {
    FlSim sim;
    FlControls ctrl;
    if (!c || !p || !st) return -1;
    fl_sim_init(&sim, p);
    memset(&ctrl, 0, sizeof ctrl);
    ctrl.throttle = 0.55;
    ctrl.de_deg = -1.5;
    fl_sim_set_controls(&sim, &ctrl);
    /* fl_sim_run returns nhist (>0) on success, -1 on error. */
    if (fl_sim_run(&sim, duration_s, dt, 0.0) < 2) return -1;
    return fl_chart_draw(c, sqg_rect_make(0, 0, c->w, c->h), &sim, st);
}
