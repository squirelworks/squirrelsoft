/* sqs_gui: the SquirrelStation window panel (see sqs_gui.h). Original code. */
#include "sqs_gui.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BG SQG_RGB(0, 0, 0)
#define SNAPC SQG_RGB(255, 210, 0)
#define SELC SQG_RGB(80, 160, 255)
#define RUBBER SQG_RGB(170, 170, 170)
#define CROSS SQG_RGB(200, 200, 200)
#define XAXIS SQG_RGB(255, 60, 60)
#define YAXIS SQG_RGB(0, 230, 0)
#define ZAXIS SQG_RGB(70, 110, 255)
#define COMPASS SQG_RGB(0, 200, 200)
#define SNAP_PX 8.0
#define INDEX_PX 6.0
#define COMPASS_PX 40.0

static SqsDesign undo_buf; /* the design before the last change (one window) */

/* ---- the view ---- */

typedef struct {
    SqsPoint r, u, v; /* right, up, toward the viewer */
} Basis;

static Basis basis(int view) {
    const double a = 1 / sqrt(2.0), b = 1 / sqrt(6.0), c = 1 / sqrt(3.0);
    switch (view) {
    case SQS_VIEW_FRONT:
        return (Basis){{1, 0, 0}, {0, 0, 1}, {0, -1, 0}};
    case SQS_VIEW_RIGHT:
        return (Basis){{0, 1, 0}, {0, 0, 1}, {1, 0, 0}};
    case SQS_VIEW_ISO:
        return (Basis){{a, a, 0}, {-b, b, 2 * b}, {c, -c, c}};
    default:
        return (Basis){{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    }
}

static double dot(SqsPoint a, SqsPoint b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static SqsPoint add(SqsPoint a, SqsPoint b, double k) { return (SqsPoint){a.x + k * b.x, a.y + k * b.y, a.z + k * b.z}; }
static SqsPoint sub(SqsPoint a, SqsPoint b) { return (SqsPoint){a.x - b.x, a.y - b.y, a.z - b.z}; }

/* the compass axes: the view plane's, or the horizontal plane's in Iso */
static void compass_axes(int view, SqsPoint *a1, SqsPoint *a2) {
    Basis B = basis(view);
    *a1 = view == SQS_VIEW_ISO ? (SqsPoint){1, 0, 0} : B.r;
    *a2 = view == SQS_VIEW_ISO ? (SqsPoint){0, 1, 0} : B.u;
}

void sqs_gui_to_screen(const SqsGui *g, SqsPoint p, double *x, double *y) {
    Basis B = basis(g->view);
    *x = g->area.x + g->area.w / 2.0 + (dot(p, B.r) - g->cx) * g->ppu;
    *y = g->area.y + g->area.h / 2.0 - (dot(p, B.u) - g->cy) * g->ppu;
}

SqsPoint sqs_gui_to_world(const SqsGui *g, double x, double y, SqsPoint through) {
    Basis B = basis(g->view);
    double s = g->cx + (x - g->area.x - g->area.w / 2.0) / g->ppu;
    double t = g->cy - (y - g->area.y - g->area.h / 2.0) / g->ppu;
    SqsPoint p = add(add((SqsPoint){0, 0, 0}, B.r, s), B.u, t);
    SqsPoint n = g->view == SQS_VIEW_ISO ? (SqsPoint){0, 0, 1} : B.v;
    double k = dot(sub(through, p), n) / dot(B.v, n);
    p = add(p, B.v, k);
    /* no -0 from the projection */
    if (p.x == 0) p.x = 0;
    if (p.y == 0) p.y = 0;
    if (p.z == 0) p.z = 0;
    return p;
}

/* ---- an element as segments ---- */

typedef void (*SegFn)(void *ctx, SqsPoint a, SqsPoint b);

static void segments(const SqsElement *e, SegFn fn, void *ctx) {
    switch (e->type) {
    case SQS_LINE:
        fn(ctx, e->a, e->b);
        break;
    case SQS_ARC: {
        int n = (int)fmax(8, ceil(fabs(e->sweep_deg) / 360.0 * 64));
        SqsPoint prev = {0, 0, 0};
        for (int i = 0; i <= n; i++) {
            double a = (e->start_deg + e->sweep_deg * i / n) * M_PI / 180;
            SqsPoint p = {e->a.x + e->radius * cos(a), e->a.y + e->radius * sin(a), e->a.z};
            if (i) fn(ctx, prev, p);
            prev = p;
        }
        break;
    }
    case SQS_PLINE:
    case SQS_SHAPE:
        for (int i = 1; i < e->npts; i++) fn(ctx, e->pts[i - 1], e->pts[i]);
        if (e->type == SQS_SHAPE && e->npts > 2) fn(ctx, e->pts[e->npts - 1], e->pts[0]);
        if (e->npts == 1) fn(ctx, e->pts[0], e->pts[0]);
        break;
    case SQS_CURVE: {
        if (e->npts < 2) break;
        int n = 8 * (e->npts - 1);
        SqsPoint prev = e->pts[0];
        for (int k = 1; k <= n; k++) {
            SqsPoint p = sqs_curve_at(e, (double)k / n);
            fn(ctx, prev, p);
            prev = p;
        }
        break;
    }
    case SQS_MESH:
    case SQS_SOLID: {
        static SqsPoint pts[256];
        static uint8_t faces[256][4];
        const SqsPoint *P = e->pts;
        const uint8_t(*F)[4] = e->faces;
        int np = e->npts, nf = e->nfaces;
        if (e->type == SQS_SOLID) {
            nf = sqs_solid_mesh(e, pts, 256, &np, faces, 256);
            P = pts;
            F = (const uint8_t(*)[4])faces;
        }
        for (int f = 0; f < nf; f++)
            for (int k = 0; k < 4; k++) {
                int i = F[f][k], j = F[f][(k + 1) % 4];
                if (i != j && i < np && j < np) fn(ctx, P[i], P[j]);
            }
        break;
    }
    case SQS_TEXT:
        fn(ctx, e->a, e->a);
        break;
    default:
        break;
    }
}

/* ---- fit ---- */

typedef struct {
    const SqsGui *g;
    Basis B;
    double lo_s, hi_s, lo_t, hi_t;
    int n;
} Ext;

static void ext_pt(Ext *x, SqsPoint p) {
    double s = dot(p, x->B.r), t = dot(p, x->B.u);
    if (!isfinite(s) || !isfinite(t)) return;
    if (!x->n++) x->lo_s = x->hi_s = s, x->lo_t = x->hi_t = t;
    x->lo_s = fmin(x->lo_s, s), x->hi_s = fmax(x->hi_s, s);
    x->lo_t = fmin(x->lo_t, t), x->hi_t = fmax(x->hi_t, t);
}
static void ext_seg(void *ctx, SqsPoint a, SqsPoint b) {
    ext_pt(ctx, a);
    ext_pt(ctx, b);
}
static void ext_visit(const SqsElement *e, const SqsLevel *lv, int ref, void *ctx) {
    (void)lv;
    (void)ref;
    segments(e, ext_seg, ctx);
}

void sqs_gui_fit(SqsGui *g) {
    if (g->area.w <= 0 || g->area.h <= 0) return;
    Ext x = {g, basis(g->view), 0, 0, 0, 0, 0};
    sqs_visit(g->d, g->refs, g->nrefs, ext_visit, &x);
    if (!x.n) {
        g->cx = g->cy = 0;
        g->ppu = 10;
        return;
    }
    double ds = x.hi_s - x.lo_s, dt = x.hi_t - x.lo_t;
    g->cx = (x.lo_s + x.hi_s) / 2;
    g->cy = (x.lo_t + x.hi_t) / 2;
    double ks = ds > 0 ? g->area.w / (ds * 1.15) : 1e9, kt = dt > 0 ? g->area.h / (dt * 1.15) : 1e9;
    g->ppu = fmin(ks, kt);
    if (g->ppu > 1e6 || !(g->ppu > 0)) g->ppu = 10;
}

void sqs_gui_set_view(SqsGui *g, int view) {
    if (view < 0 || view >= SQS_VIEWS) return;
    g->view = view;
    sqs_gui_fit(g);
}

void sqs_gui_init(SqsGui *g, SqsDesign *d, const char *path) {
    memset(g, 0, sizeof *g);
    g->d = d;
    snprintf(g->path, sizeof g->path, "%s", path ? path : "");
    g->ppu = 10;
    g->snap_on = 1;
    g->tool = SQS_TOOL_SELECT;
    g->level_id = d->level_count ? d->levels[0].id : 0;
    g->pick_kind = SQS_PICK_FREE;
}

/* ---- picking: whole top-level elements, in pixels ---- */

typedef struct {
    const SqsGui *g;
    double sx, sy, best;
} PickWalk;

static double seg_px(double px, double py, double ax, double ay, double bx, double by) {
    double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    double t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return hypot(px - (ax + t * dx), py - (ay + t * dy));
}

static void pick_seg(void *ctx, SqsPoint a, SqsPoint b) {
    PickWalk *w = ctx;
    double ax, ay, bx, by;
    sqs_gui_to_screen(w->g, a, &ax, &ay);
    sqs_gui_to_screen(w->g, b, &bx, &by);
    w->best = fmin(w->best, seg_px(w->sx, w->sy, ax, ay, bx, by));
}

static void pick_visit(const SqsElement *e, const SqsLevel *lv, int ref, void *ctx) {
    (void)lv;
    (void)ref;
    PickWalk *w = ctx;
    if (e->type == SQS_TEXT) {
        double x, y;
        sqs_gui_to_screen(w->g, e->a, &x, &y);
        if (w->sx >= x && w->sx <= x + sqg_text_width(e->text, 1) && w->sy >= y - SQG_GLYPH_H && w->sy <= y) w->best = 0;
    }
    segments(e, pick_seg, ctx);
}

int sqs_gui_pick(const SqsGui *g, double sx, double sy, double tol_px) {
    int best_id = 0;
    double best = tol_px;
    for (int i = 0; i < g->d->element_count; i++) {
        PickWalk w = {g, sx, sy, 1e300};
        sqs_visit_element(g->d, g->d->elements[i].id, pick_visit, &w);
        if (w.best <= best) {
            best = w.best;
            best_id = g->d->elements[i].id;
        }
    }
    return best_id;
}

/* ---- locating points ---- */

typedef struct {
    const SqsGui *g;
    double sx, sy, best;
    int kind;
    SqsPoint p;
} KeyWalk;

static void key_cand(KeyWalk *w, SqsPoint p, int kind) {
    double x, y;
    sqs_gui_to_screen(w->g, p, &x, &y);
    double d = hypot(x - w->sx, y - w->sy);
    if (d <= SNAP_PX && d < w->best) {
        w->best = d;
        w->kind = kind;
        w->p = p;
    }
}

static SqsPoint on_arc(const SqsElement *e, double deg) {
    double a = deg * M_PI / 180;
    return (SqsPoint){e->a.x + e->radius * cos(a), e->a.y + e->radius * sin(a), e->a.z};
}

/* the points worth snapping to, found on screen (the views other than Top) */
static void key_visit(const SqsElement *e, const SqsLevel *lv, int ref, void *ctx) {
    (void)lv;
    (void)ref;
    KeyWalk *w = ctx;
    switch (e->type) {
    case SQS_LINE:
        key_cand(w, e->a, SQS_SNAP_ENDPOINT);
        key_cand(w, e->b, SQS_SNAP_ENDPOINT);
        key_cand(w, sqs_mid(e->a, e->b), SQS_SNAP_MIDPOINT);
        break;
    case SQS_ARC:
        key_cand(w, e->a, SQS_SNAP_CENTER);
        if (fabs(e->sweep_deg) < 360) {
            key_cand(w, on_arc(e, e->start_deg), SQS_SNAP_ENDPOINT);
            key_cand(w, on_arc(e, e->start_deg + e->sweep_deg), SQS_SNAP_ENDPOINT);
            key_cand(w, on_arc(e, e->start_deg + e->sweep_deg / 2), SQS_SNAP_MIDPOINT);
        } else
            for (int q = 0; q < 4; q++) key_cand(w, on_arc(e, 90.0 * q), SQS_SNAP_KEYPOINT);
        break;
    case SQS_PLINE:
    case SQS_CURVE:
    case SQS_SHAPE:
    case SQS_MESH:
        for (int i = 0; i < e->npts; i++)
            key_cand(w, e->pts[i],
                     e->type != SQS_SHAPE && e->type != SQS_MESH && (i == 0 || i == e->npts - 1) ? SQS_SNAP_ENDPOINT
                                                                                                : SQS_SNAP_KEYPOINT);
        break;
    case SQS_SOLID: {
        static SqsPoint pts[256];
        static uint8_t faces[256][4];
        int np = 0;
        sqs_solid_mesh(e, pts, 256, &np, faces, 256);
        key_cand(w, e->a, SQS_SNAP_KEYPOINT);
        if (e->solid_kind == SQS_SOLID_BOX)
            for (int i = 0; i < np; i++) key_cand(w, pts[i], SQS_SNAP_KEYPOINT);
        break;
    }
    case SQS_TEXT:
        key_cand(w, e->a, SQS_SNAP_ENDPOINT);
        break;
    default:
        break;
    }
}

int sqs_gui_locate(const SqsGui *g, double sx, double sy, SqsPoint *out) {
    SqsPoint through = g->npts ? g->last : (SqsPoint){0, 0, 0};
    SqsPoint free_p = sqs_gui_to_world(g, sx, sy, through);
    if (g->snap_on) {
        if (g->view == SQS_VIEW_TOP) {
            SqsSnapResult r = sqs_snap(g->d, g->refs, g->nrefs, free_p, SNAP_PX / g->ppu);
            if (r.kind != SQS_SNAP_NONE) {
                /* sqs_snap measures in 3-D: keep only what is near on screen */
                double x, y;
                sqs_gui_to_screen(g, r.p, &x, &y);
                if (hypot(x - sx, y - sy) <= SNAP_PX + 0.5) {
                    *out = r.p;
                    return r.kind;
                }
            }
        } else {
            KeyWalk w = {g, sx, sy, 1e300, SQS_SNAP_NONE, {0, 0, 0}};
            sqs_visit(g->d, g->refs, g->nrefs, key_visit, &w);
            if (w.kind != SQS_SNAP_NONE) {
                *out = w.p;
                return w.kind;
            }
        }
    }
    *out = free_p;
    if (!g->npts || g->tool == SQS_TOOL_SELECT) return SQS_PICK_FREE;
    /* AccuDraw: lock to the compass axis nearer the pointer (Ortho), or index
     * to an axis when the pointer is within a few pixels of it */
    SqsPoint a1, a2;
    compass_axes(g->view, &a1, &a2);
    SqsPoint d = sub(free_p, g->last);
    double s = dot(d, a1), t = dot(d, a2);
    int kind = SQS_PICK_FREE;
    if (g->ortho) {
        if (fabs(s) >= fabs(t)) t = 0;
        else s = 0;
        kind = SQS_PICK_ORTHO;
    } else if (fabs(t) * g->ppu < INDEX_PX && fabs(s) * g->ppu >= INDEX_PX) {
        t = 0;
        kind = SQS_PICK_INDEX;
    } else if (fabs(s) * g->ppu < INDEX_PX && fabs(t) * g->ppu >= INDEX_PX) {
        s = 0;
        kind = SQS_PICK_INDEX;
    }
    if (kind != SQS_PICK_FREE) *out = add(add(g->last, a1, s), a2, t);
    return kind;
}

/* ---- changes ---- */

static void checkpoint(SqsGui *g) {
    undo_buf = *g->d;
    g->can_undo = 1;
    g->dirty = 1;
}

static void say(SqsGui *g, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g->out, sizeof g->out, fmt, ap);
    va_end(ap);
}

static void tool_point(SqsGui *g, SqsPoint p) {
    if (g->tool == SQS_TOOL_SELECT) return;
    if (!g->npts) {
        g->last = p;
        g->npts = 1;
        return;
    }
    int id;
    if (g->tool == SQS_TOOL_LINE) {
        if (sqs_dist(p, g->last) == 0) return;
        checkpoint(g);
        id = sqs_add_line(g->d, g->level_id, g->last, p);
        g->last = p; /* the next line goes on from here */
    } else {
        double r = hypot(p.x - g->last.x, p.y - g->last.y);
        if (!(r > 0)) {
            say(g, "a circle needs a radius");
            return;
        }
        checkpoint(g);
        id = sqs_add_arc(g->d, g->level_id, g->last, r, 0, 360);
        g->npts = 0;
    }
    if (id < 0) {
        g->can_undo = 0;
        say(g, "could not add it (the design is full, or no level)");
    } else
        say(g, "%s id=%d", g->tool == SQS_TOOL_LINE ? "line" : "circle", id);
}

static int eqi(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

static int ends_with(const char *s, const char *tail) {
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && strcmp(s + a - b, tail) == 0;
}

static void do_undo(SqsGui *g) {
    if (!g->can_undo) {
        say(g, "nothing to undo");
        return;
    }
    *g->d = undo_buf;
    g->can_undo = 0;
    g->npts = 0;
    if (g->selected && !sqs_find(g->d, g->selected)) g->selected = 0;
    say(g, "undone");
}

static void do_delete(SqsGui *g) {
    if (!g->selected) {
        say(g, "select an element first");
        return;
    }
    checkpoint(g);
    sqs_delete(g->d, g->selected);
    say(g, "deleted %d", g->selected);
    g->selected = 0;
}

static void do_save(SqsGui *g) {
    if (!g->path[0]) {
        say(g, "no file to save to");
        return;
    }
    int rc = ends_with(g->path, ".sqdgnb") ? sqs_write_sqdgnb(g->d, g->path) : sqs_write_sqdgn(g->d, g->path);
    if (rc) say(g, "cannot write %s", g->path);
    else {
        g->dirty = 0;
        say(g, "saved %s", g->path);
    }
}

static void set_tool(SqsGui *g, int tool) {
    g->tool = tool;
    g->npts = 0;
    if (tool != SQS_TOOL_SELECT) g->selected = 0;
}

static const char *const VIEW_NAMES[SQS_VIEWS] = {"Top", "Front", "Right", "Iso"};
static const char *const TOOL_NAMES[SQS_TOOLS] = {"Select", "Line", "Circle"};

int sqs_gui_keyin(SqsGui *g, const char *text) {
    char w[96];
    while (*text == ' ') text++;
    snprintf(w, sizeof w, "%s", text);
    for (size_t n = strlen(w); n && (w[n - 1] == ' ' || w[n - 1] == '\n' || w[n - 1] == '\r'); n--) w[n - 1] = 0;
    if (!w[0]) return 0;
    SqsPoint p;
    if (g->tool != SQS_TOOL_SELECT && sqs_keyin(w, g->npts ? g->last : (SqsPoint){0, 0, 0}, &p) == 0) {
        tool_point(g, p);
        return 0;
    }
    if (!strncmp(w, "lv=", 3) || !strncmp(w, "LV=", 3)) {
        int id = sqs_level_id(g->d, w + 3);
        if (id < 0) {
            say(g, "no level %s", w + 3);
            return -1;
        }
        g->level_id = id;
        say(g, "active level %s", w + 3);
        return 0;
    }
    for (int k = 0; k < SQS_TOOLS; k++)
        if (eqi(w, TOOL_NAMES[k])) {
            set_tool(g, k);
            g->out[0] = 0;
            return 0;
        }
    for (int v = 0; v < SQS_VIEWS; v++)
        if (eqi(w, VIEW_NAMES[v])) {
            sqs_gui_set_view(g, v);
            g->out[0] = 0;
            return 0;
        }
    if (eqi(w, "fit")) sqs_gui_fit(g);
    else if (eqi(w, "undo")) do_undo(g);
    else if (eqi(w, "save")) do_save(g);
    else if (eqi(w, "delete")) do_delete(g);
    else if (eqi(w, "quit")) return 1;
    else {
        say(g, g->tool == SQS_TOOL_SELECT && sqs_keyin(w, g->last, &p) == 0 ? "pick Line or Circle for %s"
                                                                             : "unknown keyin: %s",
            w);
        return -1;
    }
    return 0;
}

/* ---- describing ---- */

static const char *type_name(SqsElementType t) {
    switch (t) {
    case SQS_LINE: return "Line";
    case SQS_ARC: return "Arc";
    case SQS_PLINE: return "Line string";
    case SQS_TEXT: return "Text";
    case SQS_CURVE: return "Curve";
    case SQS_SHAPE: return "Shape";
    case SQS_MESH: return "Mesh";
    case SQS_SOLID: return "Solid";
    case SQS_CELLREF: return "Cell";
    }
    return "?";
}

void sqs_gui_describe(const SqsGui *g, int id, char *buf, size_t cap) {
    const SqsElement *e = sqs_find(g->d, id);
    if (!e) {
        snprintf(buf, cap, "no element %d", id);
        return;
    }
    const SqsLevel *lv = sqs_level(g->d, e->level_id);
    int n = snprintf(buf, cap, "%s #%d on %s", e->type == SQS_ARC && fabs(e->sweep_deg) >= 360 ? "Circle"
                                                                                             : type_name(e->type),
                     id, lv ? lv->name : "?");
    if (n < 0 || (size_t)n >= cap) return;
    if (e->type == SQS_CELLREF)
        n += snprintf(buf + n, cap - (size_t)n, ": %s at %.3f,%.3f,%.3f", e->text, e->a.x, e->a.y, e->a.z);
    else if (e->type == SQS_TEXT)
        n += snprintf(buf + n, cap - (size_t)n, ": \"%s\"", e->text);
    double L = sqs_length(e), A = sqs_area(e), V = sqs_volume(e);
    if (L > 0 && (size_t)n < cap) n += snprintf(buf + n, cap - (size_t)n, "  length %.4f", L);
    if (A > 0 && (size_t)n < cap) n += snprintf(buf + n, cap - (size_t)n, "  area %.4f", A);
    if (V > 0 && (size_t)n < cap) snprintf(buf + n, cap - (size_t)n, "  volume %.4f", V);
}

/* ---- drawing ---- */

typedef struct {
    const SqsGui *g;
    sqg_canvas *cv;
    uint32_t col;
    int use_col;
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

static void wline(const SqsGui *g, sqg_canvas *cv, SqsPoint a, SqsPoint b, uint32_t c) {
    double ax, ay, bx, by;
    sqs_gui_to_screen(g, a, &ax, &ay);
    sqs_gui_to_screen(g, b, &bx, &by);
    /* only the part on the drawing area: a long line zoomed in stays cheap */
    if (!clip_seg(g->area, &ax, &ay, &bx, &by)) return;
    sqg_line(cv, (int)lround(ax), (int)lround(ay), (int)lround(bx), (int)lround(by), c);
}

static void draw_seg(void *ctx, SqsPoint a, SqsPoint b) {
    DrawWalk *w = ctx;
    wline(w->g, w->cv, a, b, w->col);
}

static uint32_t level_col(const SqsLevel *lv) {
    uint32_t c = lv ? lv->color & 0xFFFFFFu : 0xFFFFFFu;
    return c ? 0xFF000000u | c : SQG_RGB(255, 255, 255); /* black would vanish */
}

static void draw_visit(const SqsElement *e, const SqsLevel *lv, int ref, void *ctx) {
    DrawWalk *w = ctx;
    uint32_t c = w->use_col ? w->col : level_col(lv);
    if (ref >= 0 && !w->use_col) c = 0xFF000000u | ((c >> 1) & 0x7F7F7Fu); /* references dimmer */
    if (e->type == SQS_TEXT) {
        double x, y;
        sqs_gui_to_screen(w->g, e->a, &x, &y);
        if (fabs(x) < 1e6 && fabs(y) < 1e6) sqg_text(w->cv, (int)x, (int)y - SQG_GLYPH_H + 2, e->text, c, 1);
        return;
    }
    DrawWalk one = *w;
    one.col = c;
    segments(e, draw_seg, &one);
}

static void marker(sqg_canvas *cv, double x, double y, int kind) {
    int ix = (int)lround(x), iy = (int)lround(y);
    switch (kind) {
    case SQS_SNAP_MIDPOINT:
        sqg_line(cv, ix - 6, iy + 5, ix + 6, iy + 5, SNAPC);
        sqg_line(cv, ix - 6, iy + 5, ix, iy - 6, SNAPC);
        sqg_line(cv, ix + 6, iy + 5, ix, iy - 6, SNAPC);
        break;
    case SQS_SNAP_CENTER:
        for (int i = 0; i < 24; i++) {
            double a0 = i * M_PI / 12, a1 = (i + 1) * M_PI / 12;
            sqg_line(cv, ix + (int)lround(6 * cos(a0)), iy + (int)lround(6 * sin(a0)), ix + (int)lround(6 * cos(a1)),
                     iy + (int)lround(6 * sin(a1)), SNAPC);
        }
        break;
    case SQS_SNAP_INTERSECTION:
        sqg_line(cv, ix - 5, iy - 5, ix + 5, iy + 5, SNAPC);
        sqg_line(cv, ix - 5, iy + 5, ix + 5, iy - 5, SNAPC);
        break;
    case SQS_SNAP_NEAREST:
        sqg_line(cv, ix - 6, iy, ix, iy - 6, SNAPC);
        sqg_line(cv, ix, iy - 6, ix + 6, iy, SNAPC);
        sqg_line(cv, ix + 6, iy, ix, iy + 6, SNAPC);
        sqg_line(cv, ix, iy + 6, ix - 6, iy, SNAPC);
        break;
    case SQS_SNAP_ENDPOINT:
    case SQS_SNAP_KEYPOINT:
        sqg_frame(cv, sqg_rect_make(ix - 5, iy - 5, 11, 11), SNAPC);
        break;
    default:
        break;
    }
}

/* the screen direction of a design direction, unit length */
static void screen_dir(const SqsGui *g, SqsPoint d, double *dx, double *dy) {
    Basis B = basis(g->view);
    double x = dot(d, B.r), y = -dot(d, B.u), l = hypot(x, y);
    *dx = l > 0 ? x / l : 0;
    *dy = l > 0 ? y / l : 0;
}

/* the AccuDraw compass at the tool's point */
static void compass(const SqsGui *g, sqg_canvas *cv) {
    double cx, cy, x1, y1, x2, y2;
    SqsPoint a1, a2;
    compass_axes(g->view, &a1, &a2);
    sqs_gui_to_screen(g, g->last, &cx, &cy);
    if (!(fabs(cx) < 1e6 && fabs(cy) < 1e6)) return;
    screen_dir(g, a1, &x1, &y1);
    screen_dir(g, a2, &x2, &y2);
    const double R = COMPASS_PX;
    /* the frame through the four axis tips */
    double tx[4] = {cx + R * x1, cx + R * x2, cx - R * x1, cx - R * x2};
    double ty[4] = {cy + R * y1, cy + R * y2, cy - R * y1, cy - R * y2};
    for (int i = 0; i < 4; i++)
        sqg_line(cv, (int)lround(tx[i]), (int)lround(ty[i]), (int)lround(tx[(i + 1) % 4]), (int)lround(ty[(i + 1) % 4]),
                 COMPASS);
    sqg_line(cv, (int)lround(cx), (int)lround(cy), (int)lround(tx[0]), (int)lround(ty[0]), XAXIS);
    sqg_line(cv, (int)lround(cx), (int)lround(cy), (int)lround(tx[1]), (int)lround(ty[1]), YAXIS);
    sqg_line(cv, (int)lround(cx), (int)lround(cy), (int)lround(tx[2]), (int)lround(ty[2]), COMPASS);
    sqg_line(cv, (int)lround(cx), (int)lround(cy), (int)lround(tx[3]), (int)lround(ty[3]), COMPASS);
}

/* the view's axes, bottom left */
static void triad(const SqsGui *g, sqg_canvas *cv, sqg_rect r) {
    int ox = r.x + 30, oy = r.y + r.h - 30;
    const SqsPoint ax[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const uint32_t col[3] = {XAXIS, YAXIS, ZAXIS};
    const char *name[3] = {"X", "Y", "Z"};
    Basis B = basis(g->view);
    for (int i = 0; i < 3; i++) {
        double x = dot(ax[i], B.r) * 20, y = -dot(ax[i], B.u) * 20;
        if (hypot(x, y) < 2) continue; /* along the line of sight */
        sqg_line(cv, ox, oy, ox + (int)lround(x), oy + (int)lround(y), col[i]);
        sqg_text(cv, ox + (int)lround(x * 1.3) - 2, oy + (int)lround(y * 1.3) - 3, name[i], col[i], 1);
    }
}

static const char *prompt(const SqsGui *g) {
    switch (g->tool) {
    case SQS_TOOL_LINE:
        return g->npts ? "Line: next point or keyin (Esc ends)" : "Line: first point or keyin";
    case SQS_TOOL_CIRCLE:
        return g->npts ? "Circle: a point on it, or keyin" : "Circle: centre";
    default:
        return g->selected ? "" : "Select: click an element, drag to pan";
    }
}

static const char *pick_name(int k) {
    switch (k) {
    case SQS_SNAP_ENDPOINT: return " end";
    case SQS_SNAP_MIDPOINT: return " mid";
    case SQS_SNAP_CENTER: return " cen";
    case SQS_SNAP_INTERSECTION: return " int";
    case SQS_SNAP_KEYPOINT: return " key";
    case SQS_SNAP_NEAREST: return " near";
    case SQS_PICK_INDEX: return " index";
    case SQS_PICK_ORTHO: return " ortho";
    default: return "";
    }
}

static void area(SqsGui *g, sqg_ui *ui, sqg_rect r) {
    sqg_canvas *cv = ui->cv;
    const sqg_input *in = &ui->in;
    int had = g->area.w > 0;
    g->area = r;
    if (!g->view_ready || !had) {
        sqs_gui_fit(g);
        g->view_ready = 1;
    }
    int over = sqg_rect_contains(r, in->mx, in->my) && !ui->active;
    g->cursor_in = over;
    if (over) {
        if (in->wheel) {
            double ox = r.x + r.w / 2.0, oy = r.y + r.h / 2.0;
            double s0 = g->cx + (in->mx - ox) / g->ppu, t0 = g->cy - (in->my - oy) / g->ppu;
            g->ppu = fmin(fmax(g->ppu * pow(1.25, in->wheel), 1e-6), 1e7);
            double s1 = g->cx + (in->mx - ox) / g->ppu, t1 = g->cy - (in->my - oy) / g->ppu;
            g->cx += s0 - s1;
            g->cy += t0 - t1;
        }
        g->pick_kind = sqs_gui_locate(g, in->mx, in->my, &g->pick);
        if (in->pressed) {
            if (g->tool == SQS_TOOL_SELECT) {
                g->selected = sqs_gui_pick(g, in->mx, in->my, 5);
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
    DrawWalk w = {g, cv, 0, 0};
    sqs_visit(g->d, g->refs, g->nrefs, draw_visit, &w);
    if (g->selected) {
        DrawWalk s = {g, cv, SELC, 1};
        sqs_visit_element(g->d, g->selected, draw_visit, &s);
    }
    if (g->npts && g->tool != SQS_TOOL_SELECT) {
        compass(g, cv);
        if (over) {
            if (g->tool == SQS_TOOL_LINE) wline(g, cv, g->last, g->pick, RUBBER);
            else {
                SqsElement c = {0};
                c.type = SQS_ARC;
                c.a = g->last;
                c.radius = hypot(g->pick.x - g->last.x, g->pick.y - g->last.y);
                c.sweep_deg = 360;
                DrawWalk rw = {g, cv, RUBBER, 1};
                segments(&c, draw_seg, &rw);
            }
        }
    }
    triad(g, cv, r);
    sqg_text(cv, r.x + 6, r.y + 6, VIEW_NAMES[g->view], SQG_RGB(150, 150, 150), 1);
    if (over) {
        sqg_hline(cv, in->mx - 12, in->mx + 12, in->my, CROSS);
        sqg_vline(cv, in->mx, in->my - 12, in->my + 12, CROSS);
        if (g->pick_kind < SQS_PICK_FREE && g->pick_kind != SQS_SNAP_NONE) {
            double px, py;
            sqs_gui_to_screen(g, g->pick, &px, &py);
            marker(cv, px, py, g->pick_kind);
        }
    }
    sqg_clip(cv, old);
}

void sqs_gui_frame(SqsGui *g, sqg_ui *ui) {
    sqg_canvas *cv = ui->cv;
    const sqg_theme *t = ui->theme;
    sqg_fill(cv, sqg_rect_make(0, 0, cv->w, cv->h), t->face);
    int lh = sqg_line_h(ui), pad = 4;

    /* Esc works from an empty keyin line too, so keyins and Esc mix */
    if (!ui->focus || (ui->in.key == SQG_KEY_ESCAPE && !g->keyin[0])) {
        if (ui->in.key == SQG_KEY_ESCAPE) {
            if (g->npts) g->npts = 0;
            else {
                g->tool = SQS_TOOL_SELECT;
                g->selected = 0;
            }
        } else if (ui->in.key == SQG_KEY_DELETE && g->selected)
            do_delete(g);
    }

    /* toolbar */
    sqg_row row = sqg_row_begin(sqg_rect_make(pad, pad, cv->w - 2 * pad, lh), 4);
    for (int k = 0; k < SQS_TOOLS; k++) {
        int on = g->tool == k;
        if (sqg_toggle(ui, sqg_id_str(TOOL_NAMES[k]), sqg_row_take(&row, sqg_button_w(ui, TOOL_NAMES[k])), TOOL_NAMES[k],
                       &on))
            set_tool(g, k);
    }
    sqg_row_take(&row, 8);
    for (int v = 0; v < SQS_VIEWS; v++) {
        int on = g->view == v;
        if (sqg_toggle(ui, sqg_id_str(VIEW_NAMES[v]), sqg_row_take(&row, sqg_button_w(ui, VIEW_NAMES[v])), VIEW_NAMES[v],
                       &on))
            sqs_gui_set_view(g, v);
    }
    sqg_row_take(&row, 8);
    sqg_toggle(ui, sqg_id_str("snap"), sqg_row_take(&row, sqg_button_w(ui, "Snap")), "Snap", &g->snap_on);
    sqg_toggle(ui, sqg_id_str("ortho"), sqg_row_take(&row, sqg_button_w(ui, "Ortho")), "Ortho", &g->ortho);
    sqg_row_take(&row, 8);
    if (sqg_button(ui, sqg_id_str("fit"), sqg_row_take(&row, sqg_button_w(ui, "Fit")), "Fit")) sqs_gui_fit(g);
    if (sqg_button(ui, sqg_id_str("undo"), sqg_row_take(&row, sqg_button_w(ui, "Undo")), "Undo")) do_undo(g);
    if (sqg_button(ui, sqg_id_str("delete"), sqg_row_take(&row, sqg_button_w(ui, "Delete")), "Delete")) do_delete(g);
    if (sqg_button(ui, sqg_id_str("save"), sqg_row_take(&row, sqg_button_w(ui, "Save")), "Save")) do_save(g);

    /* the keyin line and the status line */
    int y_status = cv->h - pad - lh, y_key = y_status - pad - lh;
    sqg_row krow = sqg_row_begin(sqg_rect_make(pad, y_key, cv->w - 2 * pad, lh), 4);
    sqg_label(ui, sqg_row_take(&krow, sqg_text_width("Keyin:", ui->scale) + 4), "Keyin:", SQG_ALIGN_LEFT);
    if (sqg_textfield(ui, sqg_id_str("keyin"), sqg_row_take(&krow, 0), g->keyin, sizeof g->keyin) && g->keyin[0]) {
        if (sqs_gui_keyin(g, g->keyin) == 1) ui->quit = 1;
        g->keyin[0] = 0;
        ui->cursor = 0;
    }

    int top = pad + lh + pad;
    sqg_rect r = sqg_rect_make(pad, top, cv->w - 2 * pad, y_key - pad - top);
    sqg_bevel(cv, sqg_rect_inset(r, -2), 1, 0, t);
    area(g, ui, r);

    char status[600], info[256] = "";
    if (g->selected) sqs_gui_describe(g, g->selected, info, sizeof info);
    const SqsLevel *lv = sqs_level(g->d, g->level_id);
    if (g->cursor_in && g->tool != SQS_TOOL_SELECT) {
        char rel[96] = "";
        if (g->npts) {
            SqsPoint a1, a2;
            compass_axes(g->view, &a1, &a2);
            SqsPoint d = sub(g->pick, g->last);
            snprintf(rel, sizeof rel, "  dist %.4f angle %.2f", sqs_dist(g->pick, g->last),
                     atan2(dot(d, a2), dot(d, a1)) * 180 / M_PI);
        }
        snprintf(status, sizeof status, "%s  [%s]  %.4f, %.4f, %.4f%s%s   %s", prompt(g), lv ? lv->name : "?", g->pick.x,
                 g->pick.y, g->pick.z, pick_name(g->pick_kind), rel, g->out);
    } else
        snprintf(status, sizeof status, "%s%s  [%s]   %s", prompt(g), info, lv ? lv->name : "?", g->out);
    sqg_label(ui, sqg_rect_make(pad, y_status, cv->w - 2 * pad, lh), status, SQG_ALIGN_LEFT);
}
