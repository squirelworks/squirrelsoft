/* calcgraph: view, axes and curves for SquirrelCalc (see calcgraph.h). Original code. */
#include "calcgraph.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COL_GRID 0xFFE4E8EEu
#define COL_AXIS 0xFF404850u
#define COL_LABEL 0xFF505860u
#define COL_BG 0xFFFFFFFFu

void calc_view_square(CalcView *v, int w, int h) {
    if (w <= 0 || h <= 0) return;
    double per = (v->xmax - v->xmin) / w;
    double cy = (v->ymin + v->ymax) / 2;
    v->ymin = cy - per * h / 2;
    v->ymax = cy + per * h / 2;
}

void calc_view_default(CalcView *v, int w, int h) {
    v->xmin = -10;
    v->xmax = 10;
    v->ymin = -10;
    v->ymax = 10;
    calc_view_square(v, w, h);
}

static double clampd(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }

void calc_view_zoom(CalcView *v, double factor, double cx, double cy) {
    if (!(factor > 0) || !isfinite(factor)) return;
    double sx = v->xmax - v->xmin, sy = v->ymax - v->ymin;
    double nx = clampd(sx / factor, 1e-9, 1e12), ny = clampd(sy / factor, 1e-9, 1e12);
    /* the point keeps its fraction of the span */
    double fx = sx > 0 ? (cx - v->xmin) / sx : 0.5, fy = sy > 0 ? (cy - v->ymin) / sy : 0.5;
    v->xmin = cx - fx * nx;
    v->xmax = v->xmin + nx;
    v->ymin = cy - fy * ny;
    v->ymax = v->ymin + ny;
}

void calc_view_pan(CalcView *v, int dx, int dy, int w, int h) {
    if (w <= 0 || h <= 0) return;
    double mx = (v->xmax - v->xmin) * dx / w, my = (v->ymax - v->ymin) * dy / h;
    v->xmin -= mx;
    v->xmax -= mx;
    v->ymin += my;
    v->ymax += my;
}

double calc_px_x(const CalcView *v, sqg_rect r, double x) {
    return r.x + (x - v->xmin) / (v->xmax - v->xmin) * r.w;
}

double calc_px_y(const CalcView *v, sqg_rect r, double y) {
    return r.y + (v->ymax - y) / (v->ymax - v->ymin) * r.h;
}

double calc_x_at(const CalcView *v, sqg_rect r, double px) {
    return v->xmin + (px - r.x) / r.w * (v->xmax - v->xmin);
}

double calc_y_at(const CalcView *v, sqg_rect r, double py) {
    return v->ymax - (py - r.y) / r.h * (v->ymax - v->ymin);
}

double calc_nice_step(double span, int max_ticks) {
    if (!(span > 0) || max_ticks < 1) return 1;
    double raw = span / max_ticks;
    double p = pow(10, floor(log10(raw)));
    double m = raw / p;
    double step = m <= 1 ? 1 : m <= 2 ? 2 : m <= 5 ? 5 : 10;
    return step * p;
}

void calc_tick_label(double v, double step, char *out, size_t cap) {
    if (fabs(v) < step * 1e-6) v = 0; /* -0.000000001 is 0 */
    double a = fabs(v);
    if (a != 0 && (a >= 1e6 || a < 1e-4)) {
        snprintf(out, cap, "%.3g", v);
        return;
    }
    int dec = step >= 1 ? 0 : (int)ceil(-log10(step) - 1e-9);
    if (dec > 8) dec = 8;
    snprintf(out, cap, "%.*f", dec, v);
}

/* a pixel coordinate safe to hand to integer drawing */
static int px_int(double p, int lo, int hi) {
    if (p != p) return lo;
    return (int)lround(clampd(p, lo - 4096.0, hi + 4096.0));
}

void calc_draw_axes(sqg_canvas *c, sqg_rect r, const CalcView *v) {
    sqg_rect old = c->clip;
    sqg_clip(c, r);
    sqg_fill(c, r, COL_BG);
    double sx = calc_nice_step(v->xmax - v->xmin, r.w / 70 > 2 ? r.w / 70 : 2);
    double sy = calc_nice_step(v->ymax - v->ymin, r.h / 50 > 2 ? r.h / 50 : 2);
    /* grid */
    for (double x = ceil(v->xmin / sx) * sx; x <= v->xmax; x += sx) {
        int px = px_int(calc_px_x(v, r, x), r.x, r.x + r.w);
        sqg_vline(c, px, r.y, r.y + r.h - 1, COL_GRID);
    }
    for (double y = ceil(v->ymin / sy) * sy; y <= v->ymax; y += sy) {
        int py = px_int(calc_px_y(v, r, y), r.y, r.y + r.h);
        sqg_hline(c, r.x, r.x + r.w - 1, py, COL_GRID);
    }
    /* axes, or the edge they would be beyond */
    int ax = px_int(calc_px_x(v, r, 0), r.x, r.x + r.w), ay = px_int(calc_px_y(v, r, 0), r.y, r.y + r.h);
    int in_x = ax >= r.x && ax < r.x + r.w, in_y = ay >= r.y && ay < r.y + r.h;
    if (in_x) sqg_vline(c, ax, r.y, r.y + r.h - 1, COL_AXIS);
    if (in_y) sqg_hline(c, r.x, r.x + r.w - 1, ay, COL_AXIS);
    int ly = in_y ? ay + 3 : (ay < r.y ? r.y + 2 : r.y + r.h - 10);
    if (ly > r.y + r.h - 10) ly = ay - 10; /* x axis at the bottom: labels above it */
    int lx = in_x ? ax + 3 : (ax < r.x ? r.x + 2 : r.x + r.w - 40);
    char t[32];
    for (double x = ceil(v->xmin / sx) * sx; x <= v->xmax; x += sx) {
        if (fabs(x) < sx * 1e-6) continue;
        int px = px_int(calc_px_x(v, r, x), r.x, r.x + r.w);
        calc_tick_label(x, sx, t, sizeof t);
        if (in_y) sqg_vline(c, px, ay - 2, ay + 2, COL_AXIS);
        int tw = sqg_text_width(t, 1);
        if (px - tw / 2 < r.x || px - tw / 2 + tw > r.x + r.w) continue; /* would be cut at the edge */
        sqg_text(c, px - tw / 2, ly, t, COL_LABEL, 1);
    }
    for (double y = ceil(v->ymin / sy) * sy; y <= v->ymax; y += sy) {
        if (fabs(y) < sy * 1e-6) continue;
        int py = px_int(calc_px_y(v, r, y), r.y, r.y + r.h);
        calc_tick_label(y, sy, t, sizeof t);
        if (in_x) sqg_hline(c, ax - 2, ax + 2, py, COL_AXIS);
        int tx = lx;
        if (tx + sqg_text_width(t, 1) > r.x + r.w - 2) tx = ax - 3 - sqg_text_width(t, 1);
        sqg_text(c, tx, py - 4, t, COL_LABEL, 1);
    }
    if (in_x && in_y) sqg_text(c, ax + 3, ay + 3, "0", COL_LABEL, 1);
    c->clip = old;
}

/* a 2-pixel-wide line, so curves stand out from the grid */
static void thick(sqg_canvas *c, int x0, int y0, int x1, int y1, uint32_t col) {
    sqg_line(c, x0, y0, x1, y1, col);
    if (abs(x1 - x0) >= abs(y1 - y0)) sqg_line(c, x0, y0 + 1, x1, y1 + 1, col);
    else sqg_line(c, x0 + 1, y0, x1 + 1, y1, col);
}

int calc_plot(sqg_canvas *c, sqg_rect r, const CalcView *v, const SqmProgram *p, double *values, int slot,
              uint32_t color) {
    if (r.w <= 0 || r.h <= 0 || p->nops == 0) return 0;
    sqg_rect old = c->clip;
    sqg_clip(c, r);
    int n = 2 * r.w, drawn = 0;
    double px0 = 0, py0 = 0, x0 = 0, y0 = 0;
    int have = 0;
    for (int i = 0; i <= n; i++) {
        double px = r.x + (double)i / 2.0;
        double x = calc_x_at(v, r, px);
        values[slot] = x;
        double y = sqm_run(p, values);
        if (!isfinite(y)) {
            have = 0;
            continue;
        }
        double py = calc_px_y(v, r, y);
        if (have) {
            int draw = 1;
            int above0 = py0 < r.y, above1 = py < r.y, below0 = py0 >= r.y + r.h, below1 = py >= r.y + r.h;
            if ((above0 && above1) || (below0 && below1)) draw = 0; /* both off the same edge */
            if (draw && fabs(py - py0) > r.h / 2.0) {
                /* a jump: is there a value in between, or a pole (1/x) or gap (floor)? */
                values[slot] = (x0 + x) / 2;
                double ym = sqm_run(p, values);
                double lo = y0 < y ? y0 : y, hi = y0 < y ? y : y0;
                if (!isfinite(ym) || ym < lo || ym > hi) draw = 0;
            }
            if (draw) {
                thick(c, px_int(px0, r.x, r.x + r.w), px_int(py0, r.y, r.y + r.h), px_int(px, r.x, r.x + r.w),
                      px_int(py, r.y, r.y + r.h), color);
                drawn++;
            }
        }
        px0 = px;
        py0 = py;
        x0 = x;
        y0 = y;
        have = 1;
    }
    c->clip = old;
    return drawn;
}

int calc_mark_points(sqg_canvas *c, sqg_rect r, const CalcView *v, const double *xs, const double *ys, int n,
                     uint32_t color) {
    sqg_rect old = c->clip;
    sqg_clip(c, r);
    int shown = 0;
    for (int i = 0; i < n; i++) {
        int px = px_int(calc_px_x(v, r, xs[i]), r.x, r.x + r.w);
        int py = px_int(calc_px_y(v, r, ys ? ys[i] : 0), r.y, r.y + r.h);
        if (px < r.x || px >= r.x + r.w || py < r.y || py >= r.y + r.h) continue;
        shown++;
        /* a ring of radius 4 */
        for (int k = 0; k < 32; k++) {
            double a = k * 6.283185307179586 / 32;
            sqg_pixel(c, px + (int)lround(4 * cos(a)), py + (int)lround(4 * sin(a)), color);
        }
        sqg_fill(c, sqg_rect_make(px - 1, py - 1, 3, 3), color);
    }
    c->clip = old;
    return shown;
}

int calc_mark_roots(sqg_canvas *c, sqg_rect r, const CalcView *v, const double *xs, int n, uint32_t color) {
    return calc_mark_points(c, r, v, xs, NULL, n, color);
}

int calc_shade(sqg_canvas *c, sqg_rect r, const CalcView *v, const SqmProgram *p, double *values, int slot,
               double a, double b, uint32_t argb) {
    if (r.w <= 0 || r.h <= 0 || !(a == a) || !(b == b)) return 0;
    if (a > b) {
        double t = a;
        a = b;
        b = t;
    }
    sqg_rect old = c->clip;
    sqg_clip(c, r);
    int cols = 0;
    int y0 = px_int(calc_px_y(v, r, 0), r.y, r.y + r.h);
    for (int px = r.x; px < r.x + r.w; px++) {
        double x = calc_x_at(v, r, px + 0.5);
        if (x < a || x > b) continue;
        values[slot] = x;
        double y = sqm_run(p, values);
        if (!isfinite(y)) continue;
        int py = px_int(calc_px_y(v, r, y), r.y, r.y + r.h);
        int top = py < y0 ? py : y0, bot = py < y0 ? y0 : py;
        if (bot > top) sqg_blend(c, sqg_rect_make(px, top, 1, bot - top), argb);
        cols++;
    }
    c->clip = old;
    return cols;
}
