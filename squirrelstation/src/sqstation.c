#include "sqstation.h"

#include <math.h>
#include <stdio.h>
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

void sqs_design_init(SqsDesign *d, const char *title) {
    memset(d, 0, sizeof(*d));
    snprintf(d->title, sizeof(d->title), "%s", title ? title : "untitled");
    d->next_id = 1;
    sqs_add_level(d, "Default", 0xFFFFFF);
}

int sqs_add_level(SqsDesign *d, const char *name, uint32_t color) {
    if (d->level_count >= SQS_MAX_LEVELS) return -1;
    SqsLevel *lv = &d->levels[d->level_count];
    lv->id = d->next_id++;
    snprintf(lv->name, sizeof(lv->name), "%s", name);
    lv->visible = 1;
    lv->frozen = 0;
    lv->color = color;
    d->level_count++;
    return lv->id;
}

static int valid_level(const SqsDesign *d, int level_id) {
    for (int i = 0; i < d->level_count; i++)
        if (d->levels[i].id == level_id) return 1;
    return 0;
}

int sqs_add_line(SqsDesign *d, int level_id, SqsPoint a, SqsPoint b) {
    if (d->element_count >= SQS_MAX_ELEMENTS || !valid_level(d, level_id)) return -1;
    SqsElement *e = &d->elements[d->element_count++];
    memset(e, 0, sizeof(*e));
    e->id = d->next_id++;
    e->type = SQS_LINE;
    e->level_id = level_id;
    e->a = a;
    e->b = b;
    return e->id;
}

int sqs_add_arc(SqsDesign *d, int level_id, SqsPoint center, double radius,
                double start_deg, double sweep_deg) {
    if (d->element_count >= SQS_MAX_ELEMENTS || !valid_level(d, level_id)) return -1;
    SqsElement *e = &d->elements[d->element_count++];
    memset(e, 0, sizeof(*e));
    e->id = d->next_id++;
    e->type = SQS_ARC;
    e->level_id = level_id;
    e->a = center;
    e->b = center;
    e->radius = radius;
    e->start_deg = start_deg;
    e->sweep_deg = sweep_deg;
    return e->id;
}

int sqs_add_text(SqsDesign *d, int level_id, SqsPoint p, const char *text) {
    if (d->element_count >= SQS_MAX_ELEMENTS || !valid_level(d, level_id)) return -1;
    SqsElement *e = &d->elements[d->element_count++];
    memset(e, 0, sizeof(*e));
    e->id = d->next_id++;
    e->type = SQS_TEXT;
    e->level_id = level_id;
    e->a = p;
    snprintf(e->text, sizeof(e->text), "%s", text ? text : "");
    return e->id;
}

int sqs_define_cell(SqsDesign *d, const char *name, SqsPoint origin,
                    const int *member_ids, int member_count) {
    if (d->cell_count >= 64 || member_count > SQS_MAX_CELL_MEMBERS) return -1;
    SqsCell *c = &d->cells[d->cell_count++];
    memset(c, 0, sizeof(*c));
    c->id = d->next_id++;
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->origin = origin;
    c->member_count = member_count;
    for (int i = 0; i < member_count; i++) c->member_ids[i] = member_ids[i];
    return c->id;
}

static void consider(SqsSnapResult *best, SqsSnapKind kind, SqsPoint p, int eid,
                     SqsPoint cursor) {
    double dist = sqs_dist(p, cursor);
    if (best->kind == SQS_SNAP_NONE || dist < best->distance) {
        best->kind = kind;
        best->p = p;
        best->element_id = eid;
        best->distance = dist;
    }
}

/* Segment–segment intersection in XY (z from first segment). */
static int seg_intersect_xy(SqsPoint a0, SqsPoint a1, SqsPoint b0, SqsPoint b1,
                            SqsPoint *out) {
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

SqsSnapResult sqs_accu_snap(const SqsDesign *d, SqsPoint cursor, double tol) {
    SqsSnapResult best;
    memset(&best, 0, sizeof(best));
    best.distance = 1e300;

    for (int i = 0; i < d->element_count; i++) {
        const SqsElement *e = &d->elements[i];
        if (e->type == SQS_LINE) {
            consider(&best, SQS_SNAP_ENDPOINT, e->a, e->id, cursor);
            consider(&best, SQS_SNAP_ENDPOINT, e->b, e->id, cursor);
            consider(&best, SQS_SNAP_MIDPOINT, sqs_mid(e->a, e->b), e->id, cursor);
        } else if (e->type == SQS_ARC) {
            consider(&best, SQS_SNAP_CENTER, e->a, e->id, cursor);
            double s = e->start_deg * M_PI / 180.0;
            double f = (e->start_deg + e->sweep_deg) * M_PI / 180.0;
            SqsPoint p0 = {e->a.x + e->radius * cos(s), e->a.y + e->radius * sin(s), e->a.z};
            SqsPoint p1 = {e->a.x + e->radius * cos(f), e->a.y + e->radius * sin(f), e->a.z};
            consider(&best, SQS_SNAP_ENDPOINT, p0, e->id, cursor);
            consider(&best, SQS_SNAP_ENDPOINT, p1, e->id, cursor);
        } else if (e->type == SQS_TEXT) {
            consider(&best, SQS_SNAP_ENDPOINT, e->a, e->id, cursor);
        }
    }

    for (int i = 0; i < d->element_count; i++) {
        if (d->elements[i].type != SQS_LINE) continue;
        for (int j = i + 1; j < d->element_count; j++) {
            if (d->elements[j].type != SQS_LINE) continue;
            SqsPoint hit;
            if (seg_intersect_xy(d->elements[i].a, d->elements[i].b,
                                 d->elements[j].a, d->elements[j].b, &hit)) {
                consider(&best, SQS_SNAP_INTERSECTION, hit, d->elements[i].id, cursor);
            }
        }
    }

    if (best.kind != SQS_SNAP_NONE && best.distance > tol) {
        best.kind = SQS_SNAP_NONE;
    }
    return best;
}

int sqs_write_sqdgn(const SqsDesign *d, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "SQDGN 1\n");
    fprintf(f, "TITLE %s\n", d->title);
    fprintf(f, "NEXTID %d\n", d->next_id);
    for (int i = 0; i < d->level_count; i++) {
        const SqsLevel *lv = &d->levels[i];
        fprintf(f, "LEVEL %d %s %d %d %u\n", lv->id, lv->name, lv->visible, lv->frozen,
                lv->color);
    }
    for (int i = 0; i < d->element_count; i++) {
        const SqsElement *e = &d->elements[i];
        if (e->type == SQS_LINE) {
            fprintf(f, "LINE %d %d %.17g %.17g %.17g %.17g %.17g %.17g\n", e->id,
                    e->level_id, e->a.x, e->a.y, e->a.z, e->b.x, e->b.y, e->b.z);
        } else if (e->type == SQS_ARC) {
            fprintf(f, "ARC %d %d %.17g %.17g %.17g %.17g %.17g %.17g\n", e->id,
                    e->level_id, e->a.x, e->a.y, e->a.z, e->radius, e->start_deg,
                    e->sweep_deg);
        } else if (e->type == SQS_TEXT) {
            fprintf(f, "TEXT %d %d %.17g %.17g %.17g %s\n", e->id, e->level_id, e->a.x,
                    e->a.y, e->a.z, e->text);
        }
    }
    for (int i = 0; i < d->cell_count; i++) {
        const SqsCell *c = &d->cells[i];
        fprintf(f, "CELL %d %s %.17g %.17g %.17g %d", c->id, c->name, c->origin.x,
                c->origin.y, c->origin.z, c->member_count);
        for (int j = 0; j < c->member_count; j++) fprintf(f, " %d", c->member_ids[j]);
        fprintf(f, "\n");
    }
    fprintf(f, "END\n");
    fclose(f);
    return 0;
}

int sqs_read_sqdgn(SqsDesign *d, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    memset(d, 0, sizeof(*d));
    char line[512];
    if (!fgets(line, sizeof(line), f) || strncmp(line, "SQDGN", 5) != 0) {
        fclose(f);
        return -1;
    }
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "END", 3) == 0) break;
        if (strncmp(line, "TITLE ", 6) == 0) {
            sscanf(line + 6, "%63s", d->title);
        } else if (strncmp(line, "NEXTID ", 7) == 0) {
            sscanf(line + 7, "%d", &d->next_id);
        } else if (strncmp(line, "LEVEL ", 6) == 0) {
            SqsLevel *lv = &d->levels[d->level_count++];
            sscanf(line + 6, "%d %63s %d %d %u", &lv->id, lv->name, &lv->visible,
                   &lv->frozen, &lv->color);
        } else if (strncmp(line, "LINE ", 5) == 0) {
            SqsElement *e = &d->elements[d->element_count++];
            memset(e, 0, sizeof(*e));
            e->type = SQS_LINE;
            sscanf(line + 5, "%d %d %lf %lf %lf %lf %lf %lf", &e->id, &e->level_id,
                   &e->a.x, &e->a.y, &e->a.z, &e->b.x, &e->b.y, &e->b.z);
        } else if (strncmp(line, "ARC ", 4) == 0) {
            SqsElement *e = &d->elements[d->element_count++];
            memset(e, 0, sizeof(*e));
            e->type = SQS_ARC;
            sscanf(line + 4, "%d %d %lf %lf %lf %lf %lf %lf", &e->id, &e->level_id,
                   &e->a.x, &e->a.y, &e->a.z, &e->radius, &e->start_deg, &e->sweep_deg);
            e->b = e->a;
        } else if (strncmp(line, "TEXT ", 5) == 0) {
            SqsElement *e = &d->elements[d->element_count++];
            memset(e, 0, sizeof(*e));
            e->type = SQS_TEXT;
            sscanf(line + 5, "%d %d %lf %lf %lf %63s", &e->id, &e->level_id, &e->a.x,
                   &e->a.y, &e->a.z, e->text);
        } else if (strncmp(line, "CELL ", 5) == 0) {
            SqsCell *c = &d->cells[d->cell_count++];
            memset(c, 0, sizeof(*c));
            int n = 0;
            char *p = line + 5;
            int consumed = 0;
            if (sscanf(p, "%d %63s %lf %lf %lf %d%n", &c->id, c->name, &c->origin.x,
                       &c->origin.y, &c->origin.z, &n, &consumed) >= 6) {
                c->member_count = n;
                p += consumed;
                for (int i = 0; i < n && i < SQS_MAX_CELL_MEMBERS; i++) {
                    int mid = 0, c2 = 0;
                    if (sscanf(p, " %d%n", &mid, &c2) != 1) break;
                    c->member_ids[i] = mid;
                    p += c2;
                }
            }
        }
    }
    fclose(f);
    if (d->next_id <= 0) d->next_id = 1;
    return 0;
}
