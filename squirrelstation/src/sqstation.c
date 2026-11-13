/* sqstation: the SquirrelStation design core (original code). */
#include "sqstation.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double sqs_dist(SqsPoint a, SqsPoint b) {
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

SqsPoint sqs_mid(SqsPoint a, SqsPoint b) {
    SqsPoint m = {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5};
    return m;
}

/* ---- levels ---- */

void sqs_design_init(SqsDesign *d, const char *title) {
    memset(d, 0, sizeof(*d));
    snprintf(d->title, sizeof(d->title), "%s", title ? title : "untitled");
    d->next_id = 1;
    sqs_add_level(d, "Default", 0xFFFFFF);
}

int sqs_add_level(SqsDesign *d, const char *name, uint32_t color) {
    if (d->level_count >= SQS_MAX_LEVELS || !name || !*name || strlen(name) >= SQS_MAX_NAME) return -1;
    if (sqs_level_id(d, name) > 0) return -1;
    SqsLevel *lv = &d->levels[d->level_count];
    lv->id = d->next_id++;
    snprintf(lv->name, sizeof(lv->name), "%s", name);
    lv->visible = 1;
    lv->frozen = 0;
    lv->color = color;
    d->level_count++;
    return lv->id;
}

const SqsLevel *sqs_level(const SqsDesign *d, int level_id) {
    for (int i = 0; i < d->level_count; i++)
        if (d->levels[i].id == level_id) return &d->levels[i];
    return NULL;
}

int sqs_level_id(const SqsDesign *d, const char *name) {
    for (int i = 0; i < d->level_count; i++)
        if (strcmp(d->levels[i].name, name) == 0) return d->levels[i].id;
    return -1;
}

int sqs_level_set(SqsDesign *d, const char *name, int visible, int frozen) {
    for (int i = 0; i < d->level_count; i++)
        if (strcmp(d->levels[i].name, name) == 0) {
            d->levels[i].visible = visible;
            d->levels[i].frozen = frozen;
            return 0;
        }
    return -1;
}

/* ---- elements ---- */

static int finite3(SqsPoint p) { return isfinite(p.x) && isfinite(p.y) && isfinite(p.z); }

static SqsElement *new_el(SqsDesign *d, int level_id, SqsElementType t) {
    if (d->element_count >= SQS_MAX_ELEMENTS || !sqs_level(d, level_id)) return NULL;
    SqsElement *e = &d->elements[d->element_count++];
    memset(e, 0, sizeof(*e));
    e->id = d->next_id++;
    e->type = t;
    e->level_id = level_id;
    e->scale = 1;
    return e;
}

int sqs_add_line(SqsDesign *d, int level_id, SqsPoint a, SqsPoint b) {
    if (!finite3(a) || !finite3(b)) return -1;
    SqsElement *e = new_el(d, level_id, SQS_LINE);
    if (!e) return -1;
    e->a = a;
    e->b = b;
    return e->id;
}

int sqs_add_arc(SqsDesign *d, int level_id, SqsPoint center, double radius, double start_deg, double sweep_deg) {
    if (!finite3(center) || !(radius > 0) || !isfinite(start_deg) || !isfinite(sweep_deg)) return -1;
    SqsElement *e = new_el(d, level_id, SQS_ARC);
    if (!e) return -1;
    e->a = center;
    e->b = center;
    e->radius = radius;
    e->start_deg = start_deg;
    e->sweep_deg = sweep_deg;
    return e->id;
}

int sqs_add_text(SqsDesign *d, int level_id, SqsPoint p, const char *text) {
    if (!finite3(p)) return -1;
    SqsElement *e = new_el(d, level_id, SQS_TEXT);
    if (!e) return -1;
    e->a = p;
    snprintf(e->text, sizeof(e->text), "%s", text ? text : "");
    return e->id;
}

static int add_points(SqsDesign *d, int level_id, SqsElementType t, const SqsPoint *pts, int n, int min) {
    if (n < min || n > SQS_MAX_PTS) return -1;
    for (int i = 0; i < n; i++)
        if (!finite3(pts[i])) return -1;
    SqsElement *e = new_el(d, level_id, t);
    if (!e) return -1;
    memcpy(e->pts, pts, (size_t)n * sizeof pts[0]);
    e->npts = n;
    e->a = pts[0];
    e->b = pts[n - 1];
    return e->id;
}

int sqs_add_pline(SqsDesign *d, int level_id, const SqsPoint *pts, int n) {
    return add_points(d, level_id, SQS_PLINE, pts, n, 2);
}

int sqs_add_curve(SqsDesign *d, int level_id, const SqsPoint *pts, int n) {
    return add_points(d, level_id, SQS_CURVE, pts, n, 2);
}

int sqs_add_shape(SqsDesign *d, int level_id, const SqsPoint *pts, int n) {
    /* a closing vertex equal to the first is dropped: the shape closes itself */
    if (n > 3 && pts[0].x == pts[n - 1].x && pts[0].y == pts[n - 1].y && pts[0].z == pts[n - 1].z) n--;
    return add_points(d, level_id, SQS_SHAPE, pts, n, 3);
}

int sqs_add_mesh(SqsDesign *d, int level_id, const SqsPoint *pts, int n, const uint8_t (*faces)[4], int nf) {
    if (nf < 1 || nf > SQS_MAX_FACES) return -1;
    for (int f = 0; f < nf; f++)
        for (int k = 0; k < 4; k++)
            if (faces[f][k] >= n) return -1;
    int id = add_points(d, level_id, SQS_MESH, pts, n, 3);
    if (id < 0) return -1;
    SqsElement *e = &d->elements[d->element_count - 1];
    memcpy(e->faces, faces, (size_t)nf * 4);
    e->nfaces = nf;
    return id;
}

int sqs_add_solid(SqsDesign *d, int level_id, SqsSolidKind kind, SqsPoint origin, SqsPoint size) {
    if (!finite3(origin) || !finite3(size)) return -1;
    if (kind == SQS_SOLID_BOX && !(size.x > 0 && size.y > 0 && size.z > 0)) return -1;
    if (kind == SQS_SOLID_CYLINDER && !(size.x > 0 && size.z > 0)) return -1;
    if (kind == SQS_SOLID_SPHERE && !(size.x > 0)) return -1;
    if (kind < SQS_SOLID_BOX || kind > SQS_SOLID_SPHERE) return -1;
    SqsElement *e = new_el(d, level_id, SQS_SOLID);
    if (!e) return -1;
    e->solid_kind = kind;
    e->a = origin;
    e->size = size;
    return e->id;
}

const SqsElement *sqs_find(const SqsDesign *d, int id) {
    for (int i = 0; i < d->element_count; i++)
        if (d->elements[i].id == id) return &d->elements[i];
    return NULL;
}

int sqs_delete(SqsDesign *d, int id) {
    int i = 0;
    while (i < d->element_count && d->elements[i].id != id) i++;
    if (i == d->element_count) return -1;
    memmove(&d->elements[i], &d->elements[i + 1], (size_t)(d->element_count - i - 1) * sizeof d->elements[0]);
    d->element_count--;
    for (int c = 0; c < d->cell_count; c++) {
        SqsCell *cell = &d->cells[c];
        int k = 0;
        for (int j = 0; j < cell->member_count; j++)
            if (cell->member_ids[j] != id) cell->member_ids[k++] = cell->member_ids[j];
        cell->member_count = k;
    }
    return 0;
}

const SqsCell *sqs_find_cell(const SqsDesign *d, const char *name) {
    for (int i = 0; i < d->cell_count; i++)
        if (strcmp(d->cells[i].name, name) == 0) return &d->cells[i];
    return NULL;
}

int sqs_define_cell(SqsDesign *d, const char *name, SqsPoint origin, const int *member_ids, int member_count) {
    if (d->cell_count >= SQS_MAX_CELLS || member_count < 1 || member_count > SQS_MAX_CELL_MEMBERS) return -1;
    if (!name || !*name || strlen(name) >= SQS_MAX_NAME || strchr(name, ' ') || sqs_find_cell(d, name)) return -1;
    for (int i = 0; i < member_count; i++) {
        const SqsElement *m = sqs_find(d, member_ids[i]);
        if (!m) return -1;
        if (m->type == SQS_CELLREF && strcmp(m->text, name) == 0) return -1;
    }
    SqsCell *c = &d->cells[d->cell_count++];
    memset(c, 0, sizeof(*c));
    c->id = d->next_id++;
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->origin = origin;
    c->member_count = member_count;
    for (int i = 0; i < member_count; i++) c->member_ids[i] = member_ids[i];
    return c->id;
}

int sqs_place_cell(SqsDesign *d, int level_id, const char *name, SqsPoint at, double scale, double rot_deg) {
    if (!sqs_find_cell(d, name) || !finite3(at) || !isfinite(scale) || scale == 0 || !isfinite(rot_deg)) return -1;
    SqsElement *e = new_el(d, level_id, SQS_CELLREF);
    if (!e) return -1;
    snprintf(e->text, sizeof e->text, "%s", name);
    e->a = at;
    e->scale = scale;
    e->rot_deg = rot_deg;
    return e->id;
}

int sqs_attach(SqsDesign *d, const char *path, SqsPoint offset, double scale) {
    if (d->ref_count >= SQS_MAX_REFS || !path || !*path || strlen(path) >= SQS_MAX_PATH || !finite3(offset) ||
        !(scale > 0) || strchr(path, '\n'))
        return -1;
    SqsRef *r = &d->refs[d->ref_count];
    memset(r, 0, sizeof *r);
    snprintf(r->path, sizeof r->path, "%s", path);
    r->offset = offset;
    r->scale = scale;
    r->display = 1;
    return d->ref_count++;
}

/* ---- measuring ---- */

static SqsPoint lerp_cr(SqsPoint p0, SqsPoint p1, SqsPoint p2, SqsPoint p3, double u) {
    double u2 = u * u, u3 = u2 * u;
    SqsPoint r;
#define CR(c) (0.5 * (2 * p1.c + (-p0.c + p2.c) * u + (2 * p0.c - 5 * p1.c + 4 * p2.c - p3.c) * u2 + \
                      (-p0.c + 3 * p1.c - 3 * p2.c + p3.c) * u3))
    r.x = CR(x);
    r.y = CR(y);
    r.z = CR(z);
#undef CR
    return r;
}

SqsPoint sqs_curve_at(const SqsElement *e, double t) {
    int n = e->npts;
    if (n < 2) return e->a;
    if (t <= 0) return e->pts[0];
    if (t >= 1) return e->pts[n - 1];
    double s = t * (n - 1);
    int i = (int)floor(s);
    if (i > n - 2) i = n - 2;
    double u = s - i;
    SqsPoint p0 = e->pts[i > 0 ? i - 1 : 0], p1 = e->pts[i], p2 = e->pts[i + 1];
    SqsPoint p3 = e->pts[i + 2 < n ? i + 2 : n - 1];
    return lerp_cr(p0, p1, p2, p3, u);
}

#define CURVE_STEPS 32 /* samples per curve span */

double sqs_length(const SqsElement *e) {
    double len = 0;
    switch (e->type) {
    case SQS_LINE:
        return sqs_dist(e->a, e->b);
    case SQS_ARC:
        return e->radius * fabs(e->sweep_deg) * M_PI / 180;
    case SQS_PLINE:
    case SQS_SHAPE:
        for (int i = 1; i < e->npts; i++) len += sqs_dist(e->pts[i - 1], e->pts[i]);
        if (e->type == SQS_SHAPE) len += sqs_dist(e->pts[e->npts - 1], e->pts[0]);
        return len;
    case SQS_CURVE: {
        int steps = CURVE_STEPS * (e->npts - 1);
        SqsPoint prev = e->pts[0];
        for (int k = 1; k <= steps; k++) {
            SqsPoint p = sqs_curve_at(e, (double)k / steps);
            len += sqs_dist(prev, p);
            prev = p;
        }
        return len;
    }
    default:
        return 0;
    }
}

static double tri_area(SqsPoint a, SqsPoint b, SqsPoint c) {
    double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    double cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
    return 0.5 * sqrt(cx * cx + cy * cy + cz * cz);
}

static double mesh_area(const SqsPoint *p, const uint8_t (*f)[4], int nf) {
    double a = 0;
    for (int i = 0; i < nf; i++) {
        a += tri_area(p[f[i][0]], p[f[i][1]], p[f[i][2]]);
        if (f[i][3] != f[i][2]) a += tri_area(p[f[i][0]], p[f[i][2]], p[f[i][3]]);
    }
    return a;
}

double sqs_area(const SqsElement *e) {
    if (e->type == SQS_SHAPE) {
        double a = 0;
        for (int i = 0; i < e->npts; i++) {
            const SqsPoint *p = &e->pts[i], *q = &e->pts[(i + 1) % e->npts];
            a += p->x * q->y - q->x * p->y;
        }
        return fabs(a) / 2;
    }
    if (e->type == SQS_ARC && fabs(e->sweep_deg) >= 360) return M_PI * e->radius * e->radius;
    if (e->type == SQS_MESH) return mesh_area(e->pts, e->faces, e->nfaces);
    if (e->type == SQS_SOLID) {
        double x = e->size.x, y = e->size.y, z = e->size.z;
        if (e->solid_kind == SQS_SOLID_BOX) return 2 * (x * y + y * z + x * z);
        if (e->solid_kind == SQS_SOLID_CYLINDER) return 2 * M_PI * x * x + 2 * M_PI * x * z;
        if (e->solid_kind == SQS_SOLID_SPHERE) return 4 * M_PI * x * x;
    }
    return 0;
}

double sqs_volume(const SqsElement *e) {
    if (e->type != SQS_SOLID) return 0;
    double x = e->size.x, y = e->size.y, z = e->size.z;
    if (e->solid_kind == SQS_SOLID_BOX) return x * y * z;
    if (e->solid_kind == SQS_SOLID_CYLINDER) return M_PI * x * x * z;
    if (e->solid_kind == SQS_SOLID_SPHERE) return 4.0 / 3 * M_PI * x * x * x;
    return 0;
}

#define CYL_SEGS 12
#define SPH_LAT 6
#define SPH_LON 8

static SqsPoint turn(SqsPoint o, double rot_deg, double x, double y, double z) {
    double a = rot_deg * M_PI / 180, c = cos(a), s = sin(a);
    SqsPoint p = {o.x + c * x - s * y, o.y + s * x + c * y, o.z + z};
    return p;
}

int sqs_solid_mesh(const SqsElement *e, SqsPoint *pts, int cap, int *npts, uint8_t (*faces)[4], int fcap) {
    int n = 0, nf = 0;
#define PT(X, Y, Z) (n < cap ? (pts[n] = turn(e->a, e->rot_deg, X, Y, Z), n++) : n++)
#define FACE(A, B, C, D)                                                                                          \
    do {                                                                                                          \
        if (nf < fcap) faces[nf][0] = (uint8_t)(A), faces[nf][1] = (uint8_t)(B), faces[nf][2] = (uint8_t)(C),     \
                       faces[nf][3] = (uint8_t)(D);                                                               \
        nf++;                                                                                                     \
    } while (0)
    double x = e->size.x, y = e->size.y, z = e->size.z;
    if (e->type != SQS_SOLID) return *npts = 0, 0;
    if (e->solid_kind == SQS_SOLID_BOX) {
        /* from the origin corner */
        PT(0, 0, 0), PT(x, 0, 0), PT(x, y, 0), PT(0, y, 0), PT(0, 0, z), PT(x, 0, z), PT(x, y, z), PT(0, y, z);
        FACE(0, 3, 2, 1);
        FACE(4, 5, 6, 7);
        FACE(0, 1, 5, 4);
        FACE(1, 2, 6, 5);
        FACE(2, 3, 7, 6);
        FACE(3, 0, 4, 7);
    } else if (e->solid_kind == SQS_SOLID_CYLINDER) {
        /* origin at the centre of the base, axis up z */
        for (int k = 0; k < CYL_SEGS; k++) {
            double a = 2 * M_PI * k / CYL_SEGS;
            PT(x * cos(a), x * sin(a), 0);
            PT(x * cos(a), x * sin(a), z);
        }
        int b = n;
        PT(0, 0, 0), PT(0, 0, z);
        for (int k = 0; k < CYL_SEGS; k++) {
            int k2 = (k + 1) % CYL_SEGS;
            FACE(2 * k, 2 * k2, 2 * k2 + 1, 2 * k + 1);
            FACE(b, 2 * k2, 2 * k, 2 * k);
            FACE(b + 1, 2 * k + 1, 2 * k2 + 1, 2 * k2 + 1);
        }
    } else if (e->solid_kind == SQS_SOLID_SPHERE) {
        /* origin at the centre */
        PT(0, 0, -x);
        for (int i = 1; i < SPH_LAT; i++) {
            double phi = -M_PI / 2 + M_PI * i / SPH_LAT;
            for (int k = 0; k < SPH_LON; k++) {
                double a = 2 * M_PI * k / SPH_LON;
                PT(x * cos(phi) * cos(a), x * cos(phi) * sin(a), x * sin(phi));
            }
        }
        int top = n;
        PT(0, 0, x);
#define RING(i, k) (1 + ((i) - 1) * SPH_LON + ((k) % SPH_LON))
        for (int k = 0; k < SPH_LON; k++) FACE(0, RING(1, k + 1), RING(1, k), RING(1, k));
        for (int i = 1; i < SPH_LAT - 1; i++)
            for (int k = 0; k < SPH_LON; k++) FACE(RING(i, k), RING(i, k + 1), RING(i + 1, k + 1), RING(i + 1, k));
        for (int k = 0; k < SPH_LON; k++)
            FACE(top, RING(SPH_LAT - 1, k), RING(SPH_LAT - 1, k + 1), RING(SPH_LAT - 1, k + 1));
#undef RING
    }
#undef PT
#undef FACE
    *npts = n < cap ? n : cap;
    return nf < fcap ? nf : fcap;
}

/* ---- the element graph as drawn ---- */

typedef struct {
    SqsPoint from, to;
    double s, rot;
} Xf;

static SqsPoint xf_point(const Xf *x, SqsPoint p) {
    double a = x->rot * M_PI / 180, c = cos(a), sn = sin(a);
    if (fmod(x->rot, 90.0) == 0) {
        int q = ((int)(x->rot / 90.0) % 4 + 4) % 4;
        c = q == 0 ? 1 : q == 2 ? -1 : 0;
        sn = q == 1 ? 1 : q == 3 ? -1 : 0;
    }
    double dx = (p.x - x->from.x) * x->s, dy = (p.y - x->from.y) * x->s, dz = (p.z - x->from.z) * x->s;
    SqsPoint r = {x->to.x + c * dx - sn * dy, x->to.y + sn * dx + c * dy, x->to.z + dz};
    return r;
}

static void xf_element(const Xf *x, SqsElement *e) {
    e->a = xf_point(x, e->a);
    e->b = xf_point(x, e->b);
    for (int i = 0; i < e->npts; i++) e->pts[i] = xf_point(x, e->pts[i]);
    e->radius *= fabs(x->s);
    e->size.x *= fabs(x->s);
    e->size.y *= fabs(x->s);
    e->size.z *= fabs(x->s);
    if (e->type == SQS_ARC) e->start_deg += x->rot;
    if (e->type == SQS_SOLID || e->type == SQS_CELLREF) e->rot_deg += x->rot;
    if (e->type == SQS_CELLREF) e->scale *= x->s;
}

#define CELL_DEPTH 4
#define VISIT_BUDGET 200000 /* elements given out by one visit, whatever the nesting */

typedef struct {
    SqsVisit fn;
    void *ctx;
    int budget;
} Walk;

static void emit(const SqsDesign *d, const SqsElement *e, const SqsLevel *outer, const Xf *chain, int nchain,
                 int ref, int depth, Walk *w) {
    if (w->budget <= 0) return;
    const SqsLevel *lv = sqs_level(d, e->level_id);
    if (lv && (!lv->visible || lv->frozen)) return;
    if (!lv) lv = outer;
    if (e->type != SQS_CELLREF) {
        SqsElement t = *e;
        for (int i = 0; i < nchain; i++) xf_element(&chain[i], &t); /* innermost first */
        w->budget--;
        w->fn(&t, lv, ref, w->ctx);
        return;
    }
    if (depth >= CELL_DEPTH) return;
    const SqsCell *c = sqs_find_cell(d, e->text);
    if (!c) return;
    Xf inner[CELL_DEPTH + 2];
    if (nchain > 0) memcpy(inner + 1, chain, (size_t)nchain * sizeof chain[0]);
    inner[0] = (Xf){c->origin, e->a, e->scale, e->rot_deg};
    for (int j = 0; j < c->member_count && w->budget > 0; j++) {
        const SqsElement *m = sqs_find(d, c->member_ids[j]);
        if (m) emit(d, m, lv, inner, nchain + 1, ref, depth + 1, w);
    }
}

int sqs_in_cell(const SqsDesign *d, int id) {
    for (int c = 0; c < d->cell_count; c++)
        for (int j = 0; j < d->cells[c].member_count; j++)
            if (d->cells[c].member_ids[j] == id) return 1;
    return 0;
}

/* which elements (by index) belong to a cell definition */
static void members(const SqsDesign *d, unsigned char *in) {
    memset(in, 0, SQS_MAX_ELEMENTS);
    for (int c = 0; c < d->cell_count; c++)
        for (int j = 0; j < d->cells[c].member_count; j++)
            for (int i = 0; i < d->element_count; i++)
                if (d->elements[i].id == d->cells[c].member_ids[j]) {
                    in[i] = 1;
                    break;
                }
}

void sqs_visit(const SqsDesign *d, const SqsDesign *const *refs, int nrefs, SqsVisit fn, void *ctx) {
    Walk w = {fn, ctx, VISIT_BUDGET};
    unsigned char in[SQS_MAX_ELEMENTS];
    members(d, in);
    for (int i = 0; i < d->element_count; i++)
        if (!in[i]) emit(d, &d->elements[i], NULL, NULL, 0, -1, 0, &w);
    for (int r = 0; r < nrefs && r < d->ref_count; r++) {
        if (!refs || !refs[r] || !d->refs[r].display) continue;
        const SqsDesign *rd = refs[r];
        SqsPoint zero = {0, 0, 0};
        Xf x = {zero, d->refs[r].offset, d->refs[r].scale, 0};
        members(rd, in);
        for (int i = 0; i < rd->element_count; i++)
            if (!in[i]) emit(rd, &rd->elements[i], NULL, &x, 1, r, 0, &w);
    }
}

void sqs_visit_element(const SqsDesign *d, int id, SqsVisit fn, void *ctx) {
    const SqsElement *e = sqs_find(d, id);
    if (!e || sqs_in_cell(d, id)) return;
    Walk w = {fn, ctx, VISIT_BUDGET};
    emit(d, e, NULL, NULL, 0, -1, 0, &w);
}

/* ---- snaps ---- */

#define MAX_SEGS 2048

typedef struct {
    SqsPoint cursor;
    SqsSnapResult best, nearest;
    SqsPoint seg[MAX_SEGS][2];
    int seg_id[MAX_SEGS], seg_ref[MAX_SEGS];
    int nseg;
} Snap;

static void consider(SqsSnapResult *best, SqsSnapKind kind, SqsPoint p, int eid, int ref, SqsPoint cursor) {
    double dist = sqs_dist(p, cursor);
    if (best->kind == SQS_SNAP_NONE || dist < best->distance) {
        best->kind = kind;
        best->p = p;
        best->element_id = eid;
        best->ref = ref;
        best->distance = dist;
    }
}

/* Segment–segment intersection in XY (z from the first segment). */
static int seg_intersect_xy(SqsPoint a0, SqsPoint a1, SqsPoint b0, SqsPoint b1, SqsPoint *out) {
    double ax = a1.x - a0.x, ay = a1.y - a0.y;
    double bx = b1.x - b0.x, by = b1.y - b0.y;
    double den = ax * by - ay * bx;
    if (fabs(den) < 1e-12) return 0;
    double t = ((b0.x - a0.x) * by - (b0.y - a0.y) * bx) / den;
    double u = ((b0.x - a0.x) * ay - (b0.y - a0.y) * ax) / den;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return 0;
    out->x = a0.x + t * ax;
    out->y = a0.y + t * ay;
    out->z = a0.z + t * (a1.z - a0.z);
    return 1;
}

static SqsPoint closest_on_seg(SqsPoint a, SqsPoint b, SqsPoint p) {
    double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    double l2 = dx * dx + dy * dy + dz * dz;
    double t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy + (p.z - a.z) * dz) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    SqsPoint r = {a.x + t * dx, a.y + t * dy, a.z + t * dz};
    return r;
}

static void add_seg(Snap *s, SqsPoint a, SqsPoint b, int id, int ref, int mid) {
    if (mid) consider(&s->best, SQS_SNAP_MIDPOINT, sqs_mid(a, b), id, ref, s->cursor);
    consider(&s->nearest, SQS_SNAP_NEAREST, closest_on_seg(a, b, s->cursor), id, ref, s->cursor);
    if (s->nseg < MAX_SEGS) {
        s->seg[s->nseg][0] = a;
        s->seg[s->nseg][1] = b;
        s->seg_id[s->nseg] = id;
        s->seg_ref[s->nseg++] = ref;
    }
}

static void snap_visit(const SqsElement *e, const SqsLevel *lv, int ref, void *ctx) {
    (void)lv;
    Snap *s = ctx;
    SqsSnapResult *b = &s->best;
    int id = e->id;
    switch (e->type) {
    case SQS_LINE:
        consider(b, SQS_SNAP_ENDPOINT, e->a, id, ref, s->cursor);
        consider(b, SQS_SNAP_ENDPOINT, e->b, id, ref, s->cursor);
        add_seg(s, e->a, e->b, id, ref, 1);
        break;
    case SQS_ARC: {
        consider(b, SQS_SNAP_CENTER, e->a, id, ref, s->cursor);
        double a0 = e->start_deg * M_PI / 180, a1 = (e->start_deg + e->sweep_deg) * M_PI / 180;
        if (fabs(e->sweep_deg) < 360) {
            SqsPoint p0 = {e->a.x + e->radius * cos(a0), e->a.y + e->radius * sin(a0), e->a.z};
            SqsPoint p1 = {e->a.x + e->radius * cos(a1), e->a.y + e->radius * sin(a1), e->a.z};
            consider(b, SQS_SNAP_ENDPOINT, p0, id, ref, s->cursor);
            consider(b, SQS_SNAP_ENDPOINT, p1, id, ref, s->cursor);
            double am = (a0 + a1) / 2;
            SqsPoint pm = {e->a.x + e->radius * cos(am), e->a.y + e->radius * sin(am), e->a.z};
            consider(b, SQS_SNAP_MIDPOINT, pm, id, ref, s->cursor);
        }
        /* nearest: toward the cursor, if that direction is on the arc */
        double ang = atan2(s->cursor.y - e->a.y, s->cursor.x - e->a.x);
        double rel = fmod((ang - a0) * 180 / M_PI + 720.0, 360.0);
        if (fabs(e->sweep_deg) >= 360 || (e->sweep_deg > 0 ? rel <= e->sweep_deg : 360 - rel <= -e->sweep_deg)) {
            SqsPoint pn = {e->a.x + e->radius * cos(ang), e->a.y + e->radius * sin(ang), e->a.z};
            consider(&s->nearest, SQS_SNAP_NEAREST, pn, id, ref, s->cursor);
        }
        break;
    }
    case SQS_PLINE:
    case SQS_SHAPE:
        for (int i = 0; i < e->npts; i++) {
            int end = e->type == SQS_PLINE && (i == 0 || i == e->npts - 1);
            consider(b, end ? SQS_SNAP_ENDPOINT : SQS_SNAP_KEYPOINT, e->pts[i], id, ref, s->cursor);
        }
        for (int i = 1; i < e->npts; i++) add_seg(s, e->pts[i - 1], e->pts[i], id, ref, 1);
        if (e->type == SQS_SHAPE) add_seg(s, e->pts[e->npts - 1], e->pts[0], id, ref, 1);
        break;
    case SQS_CURVE: {
        for (int i = 0; i < e->npts; i++)
            consider(b, i == 0 || i == e->npts - 1 ? SQS_SNAP_ENDPOINT : SQS_SNAP_KEYPOINT, e->pts[i], id, ref,
                     s->cursor);
        int steps = 8 * (e->npts - 1);
        SqsPoint prev = e->pts[0];
        for (int k = 1; k <= steps; k++) {
            SqsPoint p = sqs_curve_at(e, (double)k / steps);
            add_seg(s, prev, p, id, ref, 0);
            prev = p;
        }
        break;
    }
    case SQS_MESH:
        for (int i = 0; i < e->npts; i++) consider(b, SQS_SNAP_KEYPOINT, e->pts[i], id, ref, s->cursor);
        break;
    case SQS_SOLID:
        consider(b, SQS_SNAP_KEYPOINT, e->a, id, ref, s->cursor);
        break;
    case SQS_TEXT:
        consider(b, SQS_SNAP_ENDPOINT, e->a, id, ref, s->cursor);
        break;
    default:
        break;
    }
}

SqsSnapResult sqs_snap(const SqsDesign *d, const SqsDesign *const *refs, int nrefs, SqsPoint cursor, double tol) {
    static Snap s;
    memset(&s, 0, sizeof s);
    s.cursor = cursor;
    s.best.distance = s.nearest.distance = 1e300;
    s.best.ref = s.nearest.ref = -1;
    sqs_visit(d, refs, nrefs, snap_visit, &s);
    for (int i = 0; i < s.nseg; i++)
        for (int j = i + 1; j < s.nseg; j++) {
            if (s.seg_id[i] == s.seg_id[j] && s.seg_ref[i] == s.seg_ref[j]) continue; /* not with itself */
            SqsPoint hit;
            if (seg_intersect_xy(s.seg[i][0], s.seg[i][1], s.seg[j][0], s.seg[j][1], &hit))
                consider(&s.best, SQS_SNAP_INTERSECTION, hit, s.seg_id[i], s.seg_ref[i], cursor);
        }
    if (s.best.kind == SQS_SNAP_NONE || s.best.distance > tol) s.best = s.nearest;
    if (s.best.kind != SQS_SNAP_NONE && s.best.distance > tol) s.best.kind = SQS_SNAP_NONE;
    return s.best;
}

SqsSnapResult sqs_accu_snap(const SqsDesign *d, SqsPoint cursor, double tol) {
    return sqs_snap(d, NULL, 0, cursor, tol);
}

/* ---- keyins ---- */

int sqs_keyin(const char *text, SqsPoint last, SqsPoint *out) {
    while (*text == ' ') text++;
    double v[3] = {0, 0, 0};
    const char *p = text;
    int mode = 0; /* 0 absolute, 1 delta, 2 polar */
    if (!strncmp(p, "xy=", 3) || !strncmp(p, "XY=", 3)) p += 3;
    else if (!strncmp(p, "dl=", 3) || !strncmp(p, "DL=", 3)) p += 3, mode = 1;
    else if (!strncmp(p, "di=", 3) || !strncmp(p, "DI=", 3)) p += 3, mode = 2;
    int n = 0;
    for (;;) {
        char *end;
        double x = strtod(p, &end);
        if (end == p || !isfinite(x)) return -1;
        v[n++] = x;
        p = end;
        while (*p == ' ') p++;
        if (*p == ',' && n < 3) {
            p++;
            continue;
        }
        break;
    }
    if (*p || n < 2 || (mode == 2 && n != 2)) return -1;
    if (mode == 0) *out = (SqsPoint){v[0], v[1], n == 3 ? v[2] : 0};
    else if (mode == 1) *out = (SqsPoint){last.x + v[0], last.y + v[1], last.z + v[2]};
    else {
        double a = v[1] * M_PI / 180, c = cos(a), s = sin(a);
        if (fmod(v[1], 90.0) == 0) {
            int q = ((int)(v[1] / 90.0) % 4 + 4) % 4;
            c = q == 0 ? 1 : q == 2 ? -1 : 0;
            s = q == 1 ? 1 : q == 3 ? -1 : 0;
        }
        *out = (SqsPoint){last.x + v[0] * c, last.y + v[0] * s, last.z};
    }
    return 0;
}

/* ---- the text file ---- */

static void put_pts(FILE *f, const SqsElement *e) {
    fprintf(f, " %d", e->npts);
    for (int i = 0; i < e->npts; i++) fprintf(f, " %.17g %.17g %.17g", e->pts[i].x, e->pts[i].y, e->pts[i].z);
}

int sqs_write_sqdgn(const SqsDesign *d, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "SQDGN 2\n");
    fprintf(f, "TITLE %s\n", d->title);
    fprintf(f, "NEXTID %d\n", d->next_id);
    for (int i = 0; i < d->level_count; i++) {
        const SqsLevel *lv = &d->levels[i];
        fprintf(f, "LEVEL %d %s %d %d %u\n", lv->id, lv->name, lv->visible, lv->frozen, lv->color);
    }
    for (int i = 0; i < d->element_count; i++) {
        const SqsElement *e = &d->elements[i];
        switch (e->type) {
        case SQS_LINE:
            fprintf(f, "LINE %d %d %.17g %.17g %.17g %.17g %.17g %.17g\n", e->id, e->level_id, e->a.x, e->a.y,
                    e->a.z, e->b.x, e->b.y, e->b.z);
            break;
        case SQS_ARC:
            fprintf(f, "ARC %d %d %.17g %.17g %.17g %.17g %.17g %.17g\n", e->id, e->level_id, e->a.x, e->a.y,
                    e->a.z, e->radius, e->start_deg, e->sweep_deg);
            break;
        case SQS_TEXT:
            fprintf(f, "TEXT %d %d %.17g %.17g %.17g %s\n", e->id, e->level_id, e->a.x, e->a.y, e->a.z, e->text);
            break;
        case SQS_PLINE:
        case SQS_CURVE:
        case SQS_SHAPE:
            fprintf(f, "%s %d %d", e->type == SQS_PLINE ? "PLINE" : e->type == SQS_CURVE ? "CURVE" : "SHAPE", e->id,
                    e->level_id);
            put_pts(f, e);
            fprintf(f, "\n");
            break;
        case SQS_MESH:
            fprintf(f, "MESH %d %d", e->id, e->level_id);
            put_pts(f, e);
            fprintf(f, " %d", e->nfaces);
            for (int k = 0; k < e->nfaces; k++)
                fprintf(f, " %d %d %d %d", e->faces[k][0], e->faces[k][1], e->faces[k][2], e->faces[k][3]);
            fprintf(f, "\n");
            break;
        case SQS_SOLID:
            fprintf(f, "SOLID %d %d %d %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n", e->id, e->level_id,
                    e->solid_kind, e->a.x, e->a.y, e->a.z, e->size.x, e->size.y, e->size.z, e->rot_deg);
            break;
        case SQS_CELLREF:
            fprintf(f, "CELLREF %d %d %s %.17g %.17g %.17g %.17g %.17g\n", e->id, e->level_id, e->text, e->a.x,
                    e->a.y, e->a.z, e->scale, e->rot_deg);
            break;
        }
    }
    for (int i = 0; i < d->cell_count; i++) {
        const SqsCell *c = &d->cells[i];
        fprintf(f, "CELL %d %s %.17g %.17g %.17g %d", c->id, c->name, c->origin.x, c->origin.y, c->origin.z,
                c->member_count);
        for (int j = 0; j < c->member_count; j++) fprintf(f, " %d", c->member_ids[j]);
        fprintf(f, "\n");
    }
    for (int i = 0; i < d->ref_count; i++) {
        const SqsRef *r = &d->refs[i];
        fprintf(f, "REF %d %.17g %.17g %.17g %.17g %s\n", r->display, r->offset.x, r->offset.y, r->offset.z,
                r->scale, r->path);
    }
    fprintf(f, "END\n");
    int bad = ferror(f);
    bad |= fclose(f) != 0;
    return bad ? -1 : 0;
}

/* a cursor over one line of the file */
typedef struct {
    const char *p;
    int ok;
} Cur;

static int c_int(Cur *c) {
    char *end;
    long v = strtol(c->p, &end, 10);
    if (end == c->p || v < INT_MIN / 2 || v > INT_MAX / 2) c->ok = 0;
    c->p = end;
    return (int)v;
}

static double c_num(Cur *c) {
    char *end;
    double v = strtod(c->p, &end);
    if (end == c->p || !isfinite(v)) c->ok = 0;
    c->p = end;
    return v;
}

static SqsPoint c_pt(Cur *c) {
    SqsPoint p;
    p.x = c_num(c);
    p.y = c_num(c);
    p.z = c_num(c);
    return p;
}

static void c_word(Cur *c, char *out, size_t cap) {
    while (*c->p == ' ') c->p++;
    size_t n = strcspn(c->p, " \r\n");
    if (n == 0 || n >= cap) c->ok = 0, n = n >= cap ? cap - 1 : n;
    memcpy(out, c->p, n);
    out[n] = 0;
    c->p += n;
}

/* the rest of the line after one space */
static void c_rest(Cur *c, char *out, size_t cap) {
    if (*c->p == ' ') c->p++;
    size_t n = strcspn(c->p, "\r\n");
    if (n >= cap) n = cap - 1;
    memcpy(out, c->p, n);
    out[n] = 0;
    c->p += n;
}

static int c_pts(Cur *c, SqsElement *e) {
    int n = c_int(c);
    if (!c->ok || n < 1 || n > SQS_MAX_PTS) return c->ok = 0;
    for (int i = 0; i < n; i++) e->pts[i] = c_pt(c);
    e->npts = n;
    e->a = e->pts[0];
    e->b = e->pts[n - 1];
    return c->ok;
}

int sqs_read_sqdgn(SqsDesign *d, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    memset(d, 0, sizeof(*d));
    static char line[8192];
    if (!fgets(line, sizeof(line), f) || strncmp(line, "SQDGN ", 6) != 0) {
        fclose(f);
        return -1;
    }
    int rc = 0;
    while (fgets(line, sizeof(line), f)) {
        if (!strchr(line, '\n') && !feof(f)) {
            rc = -2; /* a line too long to be ours */
            break;
        }
        if (strncmp(line, "END", 3) == 0) break;
        char kw[16];
        size_t kl = strcspn(line, " \r\n");
        if (kl == 0 || kl >= sizeof kw) continue;
        memcpy(kw, line, kl);
        kw[kl] = 0;
        Cur c = {line + kl, 1};
        if (!strcmp(kw, "TITLE")) {
            c_rest(&c, d->title, sizeof d->title);
        } else if (!strcmp(kw, "NEXTID")) {
            d->next_id = c_int(&c);
        } else if (!strcmp(kw, "LEVEL")) {
            if (d->level_count >= SQS_MAX_LEVELS) {
                rc = -2;
                break;
            }
            SqsLevel *lv = &d->levels[d->level_count++];
            lv->id = c_int(&c);
            c_word(&c, lv->name, sizeof lv->name);
            lv->visible = c_int(&c);
            lv->frozen = c_int(&c);
            lv->color = (uint32_t)strtoul(c.p, NULL, 10);
        } else if (!strcmp(kw, "CELL")) {
            if (d->cell_count >= SQS_MAX_CELLS) {
                rc = -2;
                break;
            }
            SqsCell *cl = &d->cells[d->cell_count++];
            cl->id = c_int(&c);
            c_word(&c, cl->name, sizeof cl->name);
            cl->origin = c_pt(&c);
            int n = c_int(&c);
            if (n < 0 || n > SQS_MAX_CELL_MEMBERS) c.ok = 0;
            for (int i = 0; c.ok && i < n; i++) cl->member_ids[i] = c_int(&c);
            cl->member_count = c.ok ? n : 0;
        } else if (!strcmp(kw, "REF")) {
            if (d->ref_count >= SQS_MAX_REFS) {
                rc = -2;
                break;
            }
            SqsRef *r = &d->refs[d->ref_count++];
            r->display = c_int(&c);
            r->offset = c_pt(&c);
            r->scale = c_num(&c);
            c_rest(&c, r->path, sizeof r->path);
            if (!r->path[0] || !(r->scale > 0)) c.ok = 0;
        } else {
            static const struct {
                const char *kw;
                SqsElementType t;
            } K[] = {{"LINE", SQS_LINE},   {"ARC", SQS_ARC},   {"TEXT", SQS_TEXT},   {"PLINE", SQS_PLINE},
                     {"CURVE", SQS_CURVE}, {"SHAPE", SQS_SHAPE}, {"MESH", SQS_MESH}, {"SOLID", SQS_SOLID},
                     {"CELLREF", SQS_CELLREF}};
            int t = 0;
            for (size_t k = 0; k < sizeof K / sizeof K[0]; k++)
                if (!strcmp(kw, K[k].kw)) t = K[k].t;
            if (!t) continue; /* a later version's record: skipped */
            if (d->element_count >= SQS_MAX_ELEMENTS) {
                rc = -2;
                break;
            }
            SqsElement *e = &d->elements[d->element_count++];
            memset(e, 0, sizeof(*e));
            e->type = (SqsElementType)t;
            e->scale = 1;
            e->id = c_int(&c);
            e->level_id = c_int(&c);
            switch (e->type) {
            case SQS_LINE:
                e->a = c_pt(&c);
                e->b = c_pt(&c);
                break;
            case SQS_ARC:
                e->a = c_pt(&c);
                e->b = e->a;
                e->radius = c_num(&c);
                e->start_deg = c_num(&c);
                e->sweep_deg = c_num(&c);
                break;
            case SQS_TEXT:
                e->a = c_pt(&c);
                c_rest(&c, e->text, sizeof e->text);
                break;
            case SQS_PLINE:
            case SQS_CURVE:
            case SQS_SHAPE:
                c_pts(&c, e);
                break;
            case SQS_MESH: {
                c_pts(&c, e);
                int nf = c_int(&c);
                if (nf < 1 || nf > SQS_MAX_FACES) c.ok = 0;
                for (int k = 0; c.ok && k < nf; k++)
                    for (int j = 0; j < 4; j++) {
                        int v = c_int(&c);
                        if (v < 0 || v >= e->npts) c.ok = 0;
                        e->faces[k][j] = (uint8_t)v;
                    }
                e->nfaces = c.ok ? nf : 0;
                break;
            }
            case SQS_SOLID:
                e->solid_kind = c_int(&c);
                e->a = c_pt(&c);
                e->size = c_pt(&c);
                e->rot_deg = c_num(&c);
                if (e->solid_kind < SQS_SOLID_BOX || e->solid_kind > SQS_SOLID_SPHERE) c.ok = 0;
                break;
            case SQS_CELLREF:
                c_word(&c, e->text, sizeof e->text);
                e->a = c_pt(&c);
                e->scale = c_num(&c);
                e->rot_deg = c_num(&c);
                break;
            }
        }
        if (!c.ok) {
            rc = -2;
            break;
        }
    }
    fclose(f);
    if (rc) return rc;
    /* ids must stay unique: never hand out one already used */
    long next = d->next_id;
    for (int i = 0; i < d->element_count; i++) {
        if (d->elements[i].id <= 0) return -2;
        if (d->elements[i].id >= next) next = (long)d->elements[i].id + 1;
    }
    for (int i = 0; i < d->level_count; i++)
        if (d->levels[i].id >= next) next = (long)d->levels[i].id + 1;
    for (int i = 0; i < d->cell_count; i++)
        if (d->cells[i].id >= next) next = (long)d->cells[i].id + 1;
    if (next <= 0) next = 1;
    if (next > INT_MAX / 2) return -2;
    d->next_id = (int)next;
    if (d->level_count == 0) sqs_add_level(d, "Default", 0xFFFFFF);
    return 0;
}

/* ---- the binary sketch: .sqdgnb ----
 * "SQDGNB" 1 0, then records: tag (1 byte), length (4 bytes), payload.
 * Integers and doubles are little-endian; strings are a 2-byte length and
 * the bytes. Tags: 1 title, 2 next id, 3 level, 4 element, 5 cell, 6 ref,
 * 0 end. Unknown tags are skipped by their length. */

#define B_MAX 4096

typedef struct {
    uint8_t b[B_MAX];
    size_t n;
    int ok;
} Buf;

static void b_u8(Buf *b, unsigned v) {
    if (b->n < B_MAX) b->b[b->n++] = (uint8_t)v;
    else b->ok = 0;
}
static void b_i32(Buf *b, int32_t v) {
    uint32_t u = (uint32_t)v;
    for (int k = 0; k < 4; k++) b_u8(b, (u >> (8 * k)) & 0xff);
}
static void b_f64(Buf *b, double v) {
    uint64_t u;
    memcpy(&u, &v, 8);
    for (int k = 0; k < 8; k++) b_u8(b, (unsigned)((u >> (8 * k)) & 0xff));
}
static void b_pt(Buf *b, SqsPoint p) {
    b_f64(b, p.x);
    b_f64(b, p.y);
    b_f64(b, p.z);
}
static void b_str(Buf *b, const char *s) {
    size_t n = strlen(s);
    b_u8(b, n & 0xff);
    b_u8(b, (n >> 8) & 0xff);
    for (size_t i = 0; i < n; i++) b_u8(b, (uint8_t)s[i]);
}

static int put_rec(FILE *f, int tag, const Buf *b) {
    if (!b->ok) return -1;
    uint8_t h[5] = {(uint8_t)tag, (uint8_t)(b->n & 0xff), (uint8_t)((b->n >> 8) & 0xff), (uint8_t)((b->n >> 16) & 0xff), 0};
    return fwrite(h, 1, 5, f) == 5 && fwrite(b->b, 1, b->n, f) == b->n ? 0 : -1;
}

int sqs_write_sqdgnb(const SqsDesign *d, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    static Buf b;
    int bad = fwrite("SQDGNB\1\0", 1, 8, f) != 8;
#define REC(tag, body)                                                                                            \
    do {                                                                                                          \
        b.n = 0;                                                                                                  \
        b.ok = 1;                                                                                                 \
        body;                                                                                                     \
        bad |= put_rec(f, tag, &b) != 0;                                                                          \
    } while (0)
    REC(1, b_str(&b, d->title));
    REC(2, b_i32(&b, d->next_id));
    for (int i = 0; i < d->level_count; i++) {
        const SqsLevel *lv = &d->levels[i];
        REC(3, (b_i32(&b, lv->id), b_str(&b, lv->name), b_u8(&b, (unsigned)lv->visible), b_u8(&b, (unsigned)lv->frozen),
                b_i32(&b, (int32_t)lv->color)));
    }
    for (int i = 0; i < d->element_count; i++) {
        const SqsElement *e = &d->elements[i];
        REC(4, {
            b_i32(&b, e->id);
            b_u8(&b, (unsigned)e->type);
            b_i32(&b, e->level_id);
            b_pt(&b, e->a);
            b_pt(&b, e->b);
            b_f64(&b, e->radius);
            b_f64(&b, e->start_deg);
            b_f64(&b, e->sweep_deg);
            b_str(&b, e->text);
            b_u8(&b, (unsigned)e->npts);
            for (int k = 0; k < e->npts; k++) b_pt(&b, e->pts[k]);
            b_u8(&b, (unsigned)e->nfaces);
            for (int k = 0; k < e->nfaces; k++)
                for (int j = 0; j < 4; j++) b_u8(&b, e->faces[k][j]);
            b_u8(&b, (unsigned)e->solid_kind);
            b_pt(&b, e->size);
            b_f64(&b, e->scale);
            b_f64(&b, e->rot_deg);
        });
    }
    for (int i = 0; i < d->cell_count; i++) {
        const SqsCell *c = &d->cells[i];
        REC(5, {
            b_i32(&b, c->id);
            b_str(&b, c->name);
            b_pt(&b, c->origin);
            b_u8(&b, (unsigned)c->member_count);
            for (int k = 0; k < c->member_count; k++) b_i32(&b, c->member_ids[k]);
        });
    }
    for (int i = 0; i < d->ref_count; i++) {
        const SqsRef *r = &d->refs[i];
        REC(6, (b_str(&b, r->path), b_pt(&b, r->offset), b_f64(&b, r->scale), b_u8(&b, (unsigned)r->display)));
    }
    REC(0, (void)0);
#undef REC
    bad |= ferror(f);
    bad |= fclose(f) != 0;
    return bad ? -1 : 0;
}

typedef struct {
    const uint8_t *p, *end;
    int ok;
} In;

static unsigned r_u8(In *in) {
    if (in->p >= in->end) return in->ok = 0, 0;
    return *in->p++;
}
static int32_t r_i32(In *in) {
    uint32_t u = 0;
    for (int k = 0; k < 4; k++) u |= (uint32_t)r_u8(in) << (8 * k);
    return (int32_t)u;
}
static double r_f64(In *in) {
    uint64_t u = 0;
    for (int k = 0; k < 8; k++) u |= (uint64_t)r_u8(in) << (8 * k);
    double v;
    memcpy(&v, &u, 8);
    if (!isfinite(v)) in->ok = 0;
    return v;
}
static SqsPoint r_pt(In *in) {
    SqsPoint p;
    p.x = r_f64(in);
    p.y = r_f64(in);
    p.z = r_f64(in);
    return p;
}
static void r_str(In *in, char *out, size_t cap) {
    size_t n = r_u8(in);
    n |= (size_t)r_u8(in) << 8;
    if (n >= cap || (size_t)(in->end - in->p) < n) {
        in->ok = 0;
        out[0] = 0;
        return;
    }
    memcpy(out, in->p, n);
    out[n] = 0;
    if (memchr(out, 0, n) || memchr(out, '\n', n)) in->ok = 0;
    in->p += n;
}

int sqs_read_sqdgnb(SqsDesign *d, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    memset(d, 0, sizeof *d);
    uint8_t magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "SQDGNB", 6) || magic[6] != 1) {
        fclose(f);
        return -1;
    }
    static uint8_t rec[B_MAX];
    int rc = -2; /* until the end record */
    for (;;) {
        uint8_t h[5];
        if (fread(h, 1, 5, f) != 5) break;
        size_t len = h[1] | (size_t)h[2] << 8 | (size_t)h[3] << 16 | (size_t)h[4] << 24;
        if (len > B_MAX || fread(rec, 1, len, f) != len) break;
        In in = {rec, rec + len, 1};
        int tag = h[0];
        if (tag == 0) {
            rc = 0;
            break;
        } else if (tag == 1) {
            r_str(&in, d->title, sizeof d->title);
        } else if (tag == 2) {
            d->next_id = r_i32(&in);
        } else if (tag == 3) {
            if (d->level_count >= SQS_MAX_LEVELS) break;
            SqsLevel *lv = &d->levels[d->level_count++];
            lv->id = r_i32(&in);
            r_str(&in, lv->name, sizeof lv->name);
            lv->visible = (int)r_u8(&in);
            lv->frozen = (int)r_u8(&in);
            lv->color = (uint32_t)r_i32(&in);
        } else if (tag == 4) {
            if (d->element_count >= SQS_MAX_ELEMENTS) break;
            SqsElement *e = &d->elements[d->element_count++];
            memset(e, 0, sizeof *e);
            e->id = r_i32(&in);
            e->type = (SqsElementType)r_u8(&in);
            e->level_id = r_i32(&in);
            e->a = r_pt(&in);
            e->b = r_pt(&in);
            e->radius = r_f64(&in);
            e->start_deg = r_f64(&in);
            e->sweep_deg = r_f64(&in);
            r_str(&in, e->text, sizeof e->text);
            e->npts = (int)r_u8(&in);
            if (e->npts > SQS_MAX_PTS) in.ok = 0;
            for (int k = 0; in.ok && k < e->npts; k++) e->pts[k] = r_pt(&in);
            e->nfaces = (int)r_u8(&in);
            if (e->nfaces > SQS_MAX_FACES) in.ok = 0;
            for (int k = 0; in.ok && k < e->nfaces; k++)
                for (int j = 0; j < 4; j++) {
                    e->faces[k][j] = (uint8_t)r_u8(&in);
                    if (e->faces[k][j] >= e->npts) in.ok = 0;
                }
            e->solid_kind = (int)r_u8(&in);
            e->size = r_pt(&in);
            e->scale = r_f64(&in);
            e->rot_deg = r_f64(&in);
            if (e->type < SQS_LINE || e->type > SQS_CELLREF || e->id <= 0) in.ok = 0;
        } else if (tag == 5) {
            if (d->cell_count >= SQS_MAX_CELLS) break;
            SqsCell *c = &d->cells[d->cell_count++];
            c->id = r_i32(&in);
            r_str(&in, c->name, sizeof c->name);
            c->origin = r_pt(&in);
            c->member_count = (int)r_u8(&in);
            if (c->member_count > SQS_MAX_CELL_MEMBERS) in.ok = 0;
            for (int k = 0; in.ok && k < c->member_count; k++) c->member_ids[k] = r_i32(&in);
        } else if (tag == 6) {
            if (d->ref_count >= SQS_MAX_REFS) break;
            SqsRef *r = &d->refs[d->ref_count++];
            r_str(&in, r->path, sizeof r->path);
            r->offset = r_pt(&in);
            r->scale = r_f64(&in);
            r->display = (int)r_u8(&in);
        }
        if (!in.ok || in.p != in.end) {
            if (tag <= 6) break; /* a known record of the wrong size */
        }
    }
    fclose(f);
    if (rc) return rc;
    long next = d->next_id;
    for (int i = 0; i < d->element_count; i++)
        if (d->elements[i].id >= next) next = (long)d->elements[i].id + 1;
    for (int i = 0; i < d->level_count; i++)
        if (d->levels[i].id >= next) next = (long)d->levels[i].id + 1;
    for (int i = 0; i < d->cell_count; i++)
        if (d->cells[i].id >= next) next = (long)d->cells[i].id + 1;
    if (next <= 0) next = 1;
    if (next > INT_MAX / 2) return -2;
    d->next_id = (int)next;
    if (d->level_count == 0) sqs_add_level(d, "Default", 0xFFFFFF);
    return 0;
}

int sqs_read_any(SqsDesign *d, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char m[6] = {0};
    size_t got = fread(m, 1, 6, f);
    fclose(f);
    if (got == 6 && !memcmp(m, "SQDGNB", 6)) return sqs_read_sqdgnb(d, path);
    return sqs_read_sqdgn(d, path);
}
