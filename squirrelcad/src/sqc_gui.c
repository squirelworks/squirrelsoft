/* sqc_gui: the SquirrelCAD window panel (see sqc_gui.h). Original code. */
#include "sqc_gui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BG SQG_RGB(0, 0, 0)
#define GRIDC SQG_RGB(60, 60, 60)
#define SNAPC SQG_RGB(255, 210, 0)
#define SELC SQG_RGB(80, 160, 255)
#define RUBBER SQG_RGB(170, 170, 170)
#define CROSS SQG_RGB(200, 200, 200)
#define SNAP_PX 8.0

/* ACI 1..7 on black; anything else grey */
static uint32_t aci(uint32_t c) {
    static const uint32_t T[8] = {0,
                                  SQG_RGB(255, 60, 60),
                                  SQG_RGB(255, 255, 0),
                                  SQG_RGB(0, 230, 0),
                                  SQG_RGB(0, 230, 230),
                                  SQG_RGB(70, 110, 255),
                                  SQG_RGB(255, 0, 255),
                                  SQG_RGB(255, 255, 255)};
    return c >= 1 && c <= 7 ? T[c] : SQG_RGB(170, 170, 170);
}

void sqc_gui_init(SqcGui *g, SqcSession *s) {
    memset(g, 0, sizeof *g);
    g->s = s;
    g->ppu = 10;
    g->snap_on = 1;
    g->grid = 1;
    g->tool = SQC_TOOL_SELECT;
}

void sqc_gui_to_screen(const SqcGui *g, SqcPoint p, double *x, double *y) {
    *x = g->area.x + g->area.w / 2.0 + (p.x - g->cx) * g->ppu;
    *y = g->area.y + g->area.h / 2.0 - (p.y - g->cy) * g->ppu;
}

SqcPoint sqc_gui_to_world(const SqcGui *g, double x, double y) {
    SqcPoint p = {g->cx + (x - g->area.x - g->area.w / 2.0) / g->ppu,
                  g->cy - (y - g->area.y - g->area.h / 2.0) / g->ppu, 0};
    return p;
}

void sqc_gui_fit(SqcGui *g) {
    SqcPoint lo, hi;
    if (g->area.w <= 0 || g->area.h <= 0) return;
    if (!sqc_extents(&g->s->dwg, &lo, &hi)) {
        g->cx = g->cy = 0;
        g->ppu = 10;
        return;
    }
    double dx = hi.x - lo.x, dy = hi.y - lo.y;
    g->cx = (lo.x + hi.x) / 2;
    g->cy = (lo.y + hi.y) / 2;
    double sx = dx > 0 ? g->area.w / (dx * 1.1) : 1e9, sy = dy > 0 ? g->area.h / (dy * 1.1) : 1e9;
    g->ppu = fmin(sx, sy);
    if (g->ppu > 1e6 || !(g->ppu > 0)) g->ppu = 10;
}

/* ---- geometry in window pixels ---- */

static double seg_dist(double px, double py, double ax, double ay, double bx, double by) {
    double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return hypot(px - (ax + t * dx), py - (ay + t * dy));
}

/* is angle a (degrees) on the arc from s counter-clockwise to e? */
static int on_arc(double a, double s, double e) {
    double sweep = fmod(e - s + 720.0, 360.0), off = fmod(a - s + 720.0, 360.0);
    if (sweep == 0) sweep = 360;
    return off <= sweep;
}

/* how far (in pixels) a primitive is from a window point */
static double ent_dist(const SqcGui *g, const SqcEntity *e, double sx, double sy) {
    double ax, ay, bx, by;
    switch (e->type) {
    case SQC_ENT_LINE:
        sqc_gui_to_screen(g, e->p1, &ax, &ay);
        sqc_gui_to_screen(g, e->p2, &bx, &by);
        return seg_dist(sx, sy, ax, ay, bx, by);
    case SQC_ENT_CIRCLE:
    case SQC_ENT_ARC: {
        sqc_gui_to_screen(g, e->p1, &ax, &ay);
        double d = hypot(sx - ax, sy - ay), r = e->radius * g->ppu;
        if (e->type == SQC_ENT_ARC) {
            double a = atan2(-(sy - ay), sx - ax) * 180 / M_PI;
            if (!on_arc(a, e->start_ang, e->end_ang)) return 1e9;
        }
        return fabs(d - r);
    }
    case SQC_ENT_PLINE: {
        double best = 1e9;
        for (int i = 1; i < e->vert_count; i++) {
            sqc_gui_to_screen(g, e->verts[i - 1], &ax, &ay);
            sqc_gui_to_screen(g, e->verts[i], &bx, &by);
            best = fmin(best, seg_dist(sx, sy, ax, ay, bx, by));
        }
        return best;
    }
    case SQC_ENT_TEXT: {
        sqc_gui_to_screen(g, e->p1, &ax, &ay);
        double w = sqg_text_width(e->text, 1), h = SQG_GLYPH_H;
        if (sx >= ax && sx <= ax + w && sy >= ay - h && sy <= ay) return 0;
        return hypot(sx - ax, sy - ay);
    }
    default:
        return 1e9;
    }
}

/* ---- snapping: over everything drawn, blocks expanded ---- */

typedef struct {
    const SqcGui *g;
    double sx, sy, tol, best;
    int kind;
    SqcPoint p;
} SnapWalk;

static void cand(SnapWalk *w, SqcPoint p, int kind) {
    double x, y;
    sqc_gui_to_screen(w->g, p, &x, &y);
    double d = hypot(x - w->sx, y - w->sy);
    /* a point a little farther wins over a midpoint only when it is an end or centre */
    if (d <= w->tol && (d < w->best - 0.5 || (d < w->best + 0.5 && kind < w->kind))) {
        w->best = d;
        w->kind = kind;
        w->p = p;
    }
}

static SqcPoint polar(SqcPoint c, double r, double deg) {
    SqcPoint p = {c.x + r * cos(deg * M_PI / 180), c.y + r * sin(deg * M_PI / 180), c.z};
    return p;
}

static void snap_visit(const SqcEntity *e, const SqcLayer *L, void *ctx) {
    (void)L;
    SnapWalk *w = ctx;
    switch (e->type) {
    case SQC_ENT_LINE: {
        SqcPoint m = {(e->p1.x + e->p2.x) / 2, (e->p1.y + e->p2.y) / 2, 0};
        cand(w, e->p1, SQC_SNAP_END);
        cand(w, e->p2, SQC_SNAP_END);
        cand(w, m, SQC_SNAP_MID);
        break;
    }
    case SQC_ENT_CIRCLE:
        cand(w, e->p1, SQC_SNAP_CEN);
        for (int q = 0; q < 4; q++) cand(w, polar(e->p1, e->radius, 90.0 * q), SQC_SNAP_END);
        break;
    case SQC_ENT_ARC: {
        double sweep = fmod(e->end_ang - e->start_ang + 720.0, 360.0);
        cand(w, e->p1, SQC_SNAP_CEN);
        cand(w, polar(e->p1, e->radius, e->start_ang), SQC_SNAP_END);
        cand(w, polar(e->p1, e->radius, e->end_ang), SQC_SNAP_END);
        cand(w, polar(e->p1, e->radius, e->start_ang + sweep / 2), SQC_SNAP_MID);
        break;
    }
    case SQC_ENT_PLINE:
        for (int i = 0; i < e->vert_count; i++) {
            cand(w, e->verts[i], SQC_SNAP_END);
            if (i) {
                SqcPoint m = {(e->verts[i - 1].x + e->verts[i].x) / 2, (e->verts[i - 1].y + e->verts[i].y) / 2, 0};
                cand(w, m, SQC_SNAP_MID);
            }
        }
        break;
    case SQC_ENT_TEXT:
        cand(w, e->p1, SQC_SNAP_END);
        break;
    default:
        break;
    }
}

int sqc_gui_snap(const SqcGui *g, double sx, double sy, double tol_px, SqcPoint *out) {
    SnapWalk w = {g, sx, sy, tol_px, 1e9, SQC_SNAP_OFF, {0, 0, 0}};
    if (g->snap_on) sqc_visit(&g->s->dwg, snap_visit, &w);
    /* the points already picked for the tool snap too (to close a polyline) */
    for (int i = 0; g->snap_on && i < g->npts; i++) cand(&w, g->pts[i], SQC_SNAP_END);
    if (w.kind != SQC_SNAP_OFF) {
        *out = w.p;
        return w.kind;
    }
    *out = sqc_gui_to_world(g, sx, sy);
    if (g->grid_on && g->grid > 0) {
        out->x = round(out->x / g->grid) * g->grid;
        out->y = round(out->y / g->grid) * g->grid;
        return SQC_SNAP_GRID;
    }
    return SQC_SNAP_OFF;
}

/* ---- picking ---- */

typedef struct {
    const SqcGui *g;
    double sx, sy, best;
} PickWalk;

static void pick_visit(const SqcEntity *e, const SqcLayer *L, void *ctx) {
    (void)L;
    PickWalk *w = ctx;
    w->best = fmin(w->best, ent_dist(w->g, e, w->sx, w->sy));
}

int sqc_gui_pick(const SqcGui *g, double sx, double sy, double tol_px) {
    const SqcDrawing *d = &g->s->dwg;
    int best_id = 0;
    double best = tol_px;
    for (int i = 0; i < d->ent_count; i++) {
        const SqcEntity *e = &d->ents[i];
        if (sqc_in_block(d, e->id)) continue;
        const SqcLayer *L = sqc_layer_of(d, e->layer_id);
        if (L && !L->on) continue;
        double dist;
        if (e->type == SQC_ENT_INSERT) {
            /* an insert is as near as the nearest of its parts: walk a drawing
             * that holds only it (and the block definitions) */
            static SqcDrawing one;
            one = *d;
            one.ent_count = 0;
            for (int j = 0; j < d->ent_count; j++)
                if (sqc_in_block(d, d->ents[j].id) || d->ents[j].id == e->id) one.ents[one.ent_count++] = d->ents[j];
            PickWalk w = {g, sx, sy, 1e9};
            sqc_visit(&one, pick_visit, &w);
            dist = w.best;
        } else
            dist = ent_dist(g, e, sx, sy);
        if (dist <= best) {
            best = dist;
            best_id = e->id;
        }
    }
    return best_id;
}

/* ---- commands ---- */

int sqc_gui_exec(SqcGui *g, const char *line) {
    int rc = sqc_exec(g->s, line, g->out, sizeof g->out);
    if (g->selected && !sqc_find(&g->s->dwg, g->selected)) g->selected = 0;
    return rc;
}

static void fmt_pt(char *buf, size_t cap, SqcPoint p) { snprintf(buf, cap, "%.10g,%.10g", p.x, p.y); }

static void cancel(SqcGui *g) { g->npts = 0; }

/* a point picked with the current tool */
static void tool_point(SqcGui *g, SqcPoint p) {
    char a[64], b[64], cmd[1400];
    switch (g->tool) {
    case SQC_TOOL_LINE:
        if (g->npts == 0) {
            g->pts[g->npts++] = p;
            return;
        }
        fmt_pt(a, sizeof a, g->pts[0]);
        fmt_pt(b, sizeof b, p);
        snprintf(cmd, sizeof cmd, "LINE %s %s", a, b);
        if (sqc_gui_exec(g, cmd) == SQC_CMD_OK) g->pts[0] = p; /* the chain goes on from here */
        return;
    case SQC_TOOL_CIRCLE:
        if (g->npts == 0) {
            g->pts[g->npts++] = p;
            return;
        }
        fmt_pt(a, sizeof a, g->pts[0]);
        snprintf(cmd, sizeof cmd, "CIRCLE %s %.10g", a, hypot(p.x - g->pts[0].x, p.y - g->pts[0].y));
        sqc_gui_exec(g, cmd);
        g->npts = 0;
        return;
    case SQC_TOOL_PLINE: {
        int closing = g->npts >= 3 && p.x == g->pts[0].x && p.y == g->pts[0].y;
        if (!closing && g->npts < SQC_MAX_PLINE) g->pts[g->npts++] = p;
        if (closing || g->npts == SQC_MAX_PLINE) {
            int n = snprintf(cmd, sizeof cmd, "PLINE");
            for (int i = 0; i < g->npts; i++) {
                fmt_pt(a, sizeof a, g->pts[i]);
                n += snprintf(cmd + n, sizeof cmd - (size_t)n, " %s", a);
            }
            if (closing) snprintf(cmd + n, sizeof cmd - (size_t)n, " CLOSE");
            sqc_gui_exec(g, cmd);
            g->npts = 0;
        }
        return;
    }
    default:
        return;
    }
}

/* Enter: an open polyline ends here */
static void finish(SqcGui *g) {
    if (g->tool == SQC_TOOL_PLINE && g->npts >= 2) {
        char a[64], cmd[1400];
        int n = snprintf(cmd, sizeof cmd, "PLINE");
        for (int i = 0; i < g->npts; i++) {
            fmt_pt(a, sizeof a, g->pts[i]);
            n += snprintf(cmd + n, sizeof cmd - (size_t)n, " %s", a);
        }
        sqc_gui_exec(g, cmd);
    }
    g->npts = 0;
}

/* ---- drawing ---- */

typedef struct {
    const SqcGui *g;
    sqg_canvas *cv;
    int sel; /* draw everything in the selection colour */
} DrawWalk;

/* Liang-Barsky: cut a segment to a rectangle (a pixel of slack); 0 when none of it is inside */
static int clip_seg(sqg_rect r, double *ax, double *ay, double *bx, double *by) {
    double x0 = r.x - 1, y0 = r.y - 1, x1 = r.x + r.w + 1, y1 = r.y + r.h + 1;
    double dx = *bx - *ax, dy = *by - *ay, t0 = 0, t1 = 1;
    const double p[4] = {-dx, dx, -dy, dy}, q[4] = {*ax - x0, x1 - *ax, *ay - y0, y1 - *ay};
    if (!(isfinite(*ax) && isfinite(*ay) && isfinite(*bx) && isfinite(*by))) return 0;
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0) return 0;
            continue;
        }
        double t = q[i] / p[i];
        if (p[i] < 0) {
            if (t > t1) return 0;
            if (t > t0) t0 = t;
        } else {
            if (t < t0) return 0;
            if (t < t1) t1 = t;
        }
    }
    double sx = *ax, sy = *ay;
    *ax = sx + t0 * dx, *ay = sy + t0 * dy;
    *bx = sx + t1 * dx, *by = sy + t1 * dy;
    return 1;
}

static void wline(const SqcGui *g, sqg_canvas *cv, SqcPoint a, SqcPoint b, uint32_t c) {
    double ax, ay, bx, by;
    sqc_gui_to_screen(g, a, &ax, &ay);
    sqc_gui_to_screen(g, b, &bx, &by);
    /* only the part on the drawing area: a long line zoomed in stays cheap */
    if (!clip_seg(g->area, &ax, &ay, &bx, &by)) return;
    sqg_line(cv, (int)lround(ax), (int)lround(ay), (int)lround(bx), (int)lround(by), c);
}

static void warc(const SqcGui *g, sqg_canvas *cv, SqcPoint c, double r, double s, double sweep, uint32_t col) {
    int n = (int)fmin(256, fmax(12, fabs(sweep) / 360.0 * r * g->ppu));
    SqcPoint prev = polar(c, r, s);
    for (int i = 1; i <= n; i++) {
        SqcPoint p = polar(c, r, s + sweep * i / n);
        wline(g, cv, prev, p, col);
        prev = p;
    }
}

static void draw_visit(const SqcEntity *e, const SqcLayer *L, void *ctx) {
    DrawWalk *w = ctx;
    const SqcGui *g = w->g;
    uint32_t c = w->sel ? SELC : aci(L ? L->color : 7);
    switch (e->type) {
    case SQC_ENT_LINE:
        wline(g, w->cv, e->p1, e->p2, c);
        break;
    case SQC_ENT_CIRCLE:
        warc(g, w->cv, e->p1, e->radius, 0, 360, c);
        break;
    case SQC_ENT_ARC:
        warc(g, w->cv, e->p1, e->radius, e->start_ang, fmod(e->end_ang - e->start_ang + 720.0, 360.0), c);
        break;
    case SQC_ENT_PLINE:
        for (int i = 1; i < e->vert_count; i++) wline(g, w->cv, e->verts[i - 1], e->verts[i], c);
        break;
    case SQC_ENT_TEXT: {
        double x, y;
        sqc_gui_to_screen(g, e->p1, &x, &y);
        if (fabs(x) < 1e6 && fabs(y) < 1e6) sqg_text(w->cv, (int)x, (int)y - SQG_GLYPH_H + 2, e->text, c, 1);
        break;
    }
    default:
        break;
    }
}

static void draw_grid(const SqcGui *g, sqg_canvas *cv) {
    double step = g->grid;
    while (step * g->ppu < 8) step *= 5; /* never denser than 8 pixels */
    SqcPoint lo = sqc_gui_to_world(g, g->area.x, g->area.y + g->area.h);
    SqcPoint hi = sqc_gui_to_world(g, g->area.x + g->area.w, g->area.y);
    long n = (long)((hi.x - lo.x) / step + 2) * (long)((hi.y - lo.y) / step + 2);
    if (n > 200000) return;
    for (double x = floor(lo.x / step) * step; x <= hi.x; x += step)
        for (double y = floor(lo.y / step) * step; y <= hi.y; y += step) {
            double px, py;
            sqc_gui_to_screen(g, (SqcPoint){x, y, 0}, &px, &py);
            sqg_pixel(cv, (int)lround(px), (int)lround(py), GRIDC);
        }
}

static void marker(sqg_canvas *cv, double x, double y, int kind) {
    int ix = (int)lround(x), iy = (int)lround(y);
    if (kind == SQC_SNAP_END || kind == SQC_SNAP_GRID) sqg_frame(cv, sqg_rect_make(ix - 5, iy - 5, 11, 11), SNAPC);
    else if (kind == SQC_SNAP_MID) {
        sqg_line(cv, ix - 6, iy + 5, ix + 6, iy + 5, SNAPC);
        sqg_line(cv, ix - 6, iy + 5, ix, iy - 6, SNAPC);
        sqg_line(cv, ix + 6, iy + 5, ix, iy - 6, SNAPC);
    } else if (kind == SQC_SNAP_CEN) {
        for (int i = 0; i < 24; i++) {
            double a0 = i * M_PI / 12, a1 = (i + 1) * M_PI / 12;
            sqg_line(cv, ix + (int)lround(6 * cos(a0)), iy + (int)lround(6 * sin(a0)), ix + (int)lround(6 * cos(a1)),
                     iy + (int)lround(6 * sin(a1)), SNAPC);
        }
    }
}

static const char *const TOOL_NAMES[SQC_TOOLS] = {"Select", "Line", "Circle", "PLine"};

static const char *prompt(const SqcGui *g) {
    switch (g->tool) {
    case SQC_TOOL_LINE:
        return g->npts ? "Line: next point (Esc ends)" : "Line: first point";
    case SQC_TOOL_CIRCLE:
        return g->npts ? "Circle: a point on it" : "Circle: centre";
    case SQC_TOOL_PLINE:
        return g->npts ? "PLine: next point (Enter ends, the first point closes)" : "PLine: first point";
    default:
        return g->selected ? "Selected: Erase or Delete removes it" : "Select: click an entity, drag to pan";
    }
}

/* the drawing area: input, then pixels */
static void area(SqcGui *g, sqg_ui *ui, sqg_rect r) {
    sqg_canvas *cv = ui->cv;
    const sqg_input *in = &ui->in;
    int had = g->area.w > 0;
    g->area = r;
    if (!g->view_ready || !had) {
        sqc_gui_fit(g);
        g->view_ready = 1;
    }
    int over = sqg_rect_contains(r, in->mx, in->my) && !ui->active;
    g->cursor_in = over;
    if (over) {
        if (in->wheel) {
            /* zoom about the pointer: the point under it stays put */
            SqcPoint before = sqc_gui_to_world(g, in->mx, in->my);
            g->ppu *= pow(1.25, in->wheel);
            g->ppu = fmin(fmax(g->ppu, 1e-6), 1e7);
            SqcPoint after = sqc_gui_to_world(g, in->mx, in->my);
            g->cx += before.x - after.x;
            g->cy += before.y - after.y;
        }
        g->cursor = sqc_gui_to_world(g, in->mx, in->my);
        g->snap_kind = sqc_gui_snap(g, in->mx, in->my, SNAP_PX, &g->pick);
        if (g->ortho && g->npts > 0 && g->tool != SQC_TOOL_SELECT && g->snap_kind != SQC_SNAP_END &&
            g->snap_kind != SQC_SNAP_CEN && g->snap_kind != SQC_SNAP_MID) {
            /* keep to the axis nearer the pointer */
            SqcPoint last = g->pts[g->npts - 1];
            if (fabs(g->pick.x - last.x) >= fabs(g->pick.y - last.y)) g->pick.y = last.y;
            else g->pick.x = last.x;
        }
        if (in->pressed) {
            if (g->tool == SQC_TOOL_SELECT) {
                g->selected = sqc_gui_pick(g, in->mx, in->my, 5);
                g->dragging = g->selected == 0;
                g->last_mx = in->mx;
                g->last_my = in->my;
            } else
                tool_point(g, g->pick);
        }
    }
    if (g->dragging) {
        if (in->down) {
            g->cx -= (in->mx - g->last_mx) / g->ppu;
            g->cy += (in->my - g->last_my) / g->ppu;
            g->last_mx = in->mx;
            g->last_my = in->my;
        } else
            g->dragging = 0;
    }

    sqg_rect old = sqg_clip(cv, r);
    sqg_fill(cv, r, BG);
    if (g->grid_on) draw_grid(g, cv);
    DrawWalk w = {g, cv, 0};
    sqc_visit(&g->s->dwg, draw_visit, &w);
    if (g->selected) {
        /* the selection again, in its colour */
        static SqcDrawing one;
        const SqcDrawing *d = &g->s->dwg;
        one = *d;
        one.ent_count = 0;
        for (int j = 0; j < d->ent_count; j++)
            if (sqc_in_block(d, d->ents[j].id) || d->ents[j].id == g->selected) one.ents[one.ent_count++] = d->ents[j];
        w.sel = 1;
        sqc_visit(&one, draw_visit, &w);
    }
    /* what the tool would make, to the pointer */
    if (g->npts > 0 && over) {
        if (g->tool == SQC_TOOL_CIRCLE)
            warc(g, cv, g->pts[0], hypot(g->pick.x - g->pts[0].x, g->pick.y - g->pts[0].y), 0, 360, RUBBER);
        else {
            for (int i = 1; i < g->npts; i++) wline(g, cv, g->pts[i - 1], g->pts[i], RUBBER);
            wline(g, cv, g->pts[g->npts - 1], g->pick, RUBBER);
        }
    }
    /* the axes' origin, and the crosshair with its snap marker */
    double ox, oy;
    sqc_gui_to_screen(g, (SqcPoint){0, 0, 0}, &ox, &oy);
    if (sqg_rect_contains(r, (int)ox, (int)oy)) {
        sqg_line(cv, (int)ox, (int)oy, (int)ox + 20, (int)oy, SQG_RGB(255, 60, 60));
        sqg_line(cv, (int)ox, (int)oy, (int)ox, (int)oy - 20, SQG_RGB(0, 230, 0));
    }
    if (over) {
        sqg_hline(cv, in->mx - 12, in->mx + 12, in->my, CROSS);
        sqg_vline(cv, in->mx, in->my - 12, in->my + 12, CROSS);
        if (g->snap_kind != SQC_SNAP_OFF) {
            double px, py;
            sqc_gui_to_screen(g, g->pick, &px, &py);
            marker(cv, px, py, g->snap_kind);
        }
    }
    sqg_clip(cv, old);
}

void sqc_gui_frame(SqcGui *g, sqg_ui *ui) {
    sqg_canvas *cv = ui->cv;
    const sqg_theme *t = ui->theme;
    sqg_rect all = sqg_rect_make(0, 0, cv->w, cv->h);
    sqg_fill(cv, all, t->face);
    int lh = sqg_line_h(ui), pad = 4;

    /* keys that are not for the command line */
    if (!ui->focus) {
        if (ui->in.key == SQG_KEY_ESCAPE) {
            if (g->npts) cancel(g);
            else {
                g->tool = SQC_TOOL_SELECT;
                g->selected = 0;
            }
        } else if (ui->in.key == SQG_KEY_ENTER)
            finish(g);
        else if (ui->in.key == SQG_KEY_DELETE && g->selected) {
            char cmd[48];
            snprintf(cmd, sizeof cmd, "ERASE %d", g->selected);
            sqc_gui_exec(g, cmd);
        }
    }

    /* toolbar */
    sqg_row row = sqg_row_begin(sqg_rect_make(pad, pad, cv->w - 2 * pad, lh), 4);
    for (int k = 0; k < SQC_TOOLS; k++) {
        int on = g->tool == k;
        if (sqg_toggle(ui, sqg_id_str(TOOL_NAMES[k]), sqg_row_take(&row, sqg_button_w(ui, TOOL_NAMES[k])),
                       TOOL_NAMES[k], &on)) {
            g->tool = k;
            g->npts = 0;
        }
    }
    sqg_row_take(&row, 8);
    sqg_toggle(ui, sqg_id_str("snap"), sqg_row_take(&row, sqg_button_w(ui, "Snap")), "Snap", &g->snap_on);
    sqg_toggle(ui, sqg_id_str("grid"), sqg_row_take(&row, sqg_button_w(ui, "Grid")), "Grid", &g->grid_on);
    sqg_toggle(ui, sqg_id_str("ortho"), sqg_row_take(&row, sqg_button_w(ui, "Ortho")), "Ortho", &g->ortho);
    sqg_row_take(&row, 8);
    if (sqg_button(ui, sqg_id_str("fit"), sqg_row_take(&row, sqg_button_w(ui, "Fit")), "Fit")) sqc_gui_fit(g);
    if (sqg_button(ui, sqg_id_str("undo"), sqg_row_take(&row, sqg_button_w(ui, "Undo")), "Undo")) {
        g->npts = 0;
        sqc_gui_exec(g, "UNDO");
    }
    if (sqg_button(ui, sqg_id_str("erase"), sqg_row_take(&row, sqg_button_w(ui, "Erase")), "Erase") && g->selected) {
        char cmd[48];
        snprintf(cmd, sizeof cmd, "ERASE %d", g->selected);
        sqc_gui_exec(g, cmd);
    }
    if (sqg_button(ui, sqg_id_str("save"), sqg_row_take(&row, sqg_button_w(ui, "Save")), "Save"))
        sqc_gui_exec(g, "SAVE");

    /* the command line and the status line at the bottom */
    int y_status = cv->h - pad - lh, y_cmd = y_status - pad - lh;
    sqg_row crow = sqg_row_begin(sqg_rect_make(pad, y_cmd, cv->w - 2 * pad, lh), 4);
    sqg_label(ui, sqg_row_take(&crow, sqg_text_width("Command:", ui->scale) + 4), "Command:", SQG_ALIGN_LEFT);
    if (sqg_textfield(ui, sqg_id_str("cmdline"), sqg_row_take(&crow, 0), g->line, sizeof g->line) && g->line[0]) {
        int rc = sqc_gui_exec(g, g->line);
        if (rc == SQC_CMD_QUIT) ui->quit = 1;
        g->line[0] = 0;
        ui->cursor = 0;
    }
    char status[700];
    if (g->cursor_in)
        snprintf(status, sizeof status, "%s   %.4f, %.4f%s   %s", prompt(g), g->pick.x, g->pick.y,
                 g->snap_kind == SQC_SNAP_END   ? " end"
                 : g->snap_kind == SQC_SNAP_MID ? " mid"
                 : g->snap_kind == SQC_SNAP_CEN ? " cen"
                                                : "",
                 g->out);
    else
        snprintf(status, sizeof status, "%s   %s", prompt(g), g->out);
    for (char *p = status; *p; p++)
        if (*p == '\n') *p = ' ';
    sqg_label(ui, sqg_rect_make(pad, y_status, cv->w - 2 * pad, lh), status, SQG_ALIGN_LEFT);

    /* the drawing between them */
    int top = pad + lh + pad;
    sqg_rect r = sqg_rect_make(pad, top, cv->w - 2 * pad, y_cmd - pad - top);
    sqg_bevel(cv, sqg_rect_inset(r, -2), 1, 0, t);
    area(g, ui, r);
}
