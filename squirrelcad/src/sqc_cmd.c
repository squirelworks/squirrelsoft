/* The SquirrelCAD command line (original code). */
#include "sqc_cmd.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqc_dxf.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_TOK 48

void sqc_session_init(SqcSession *s, const char *path) {
    memset(s, 0, sizeof *s);
    sqc_drawing_init(&s->dwg, "Drawing1");
    if (path) snprintf(s->path, sizeof s->path, "%s", path);
}

static int say(char *out, size_t cap, int rc, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (cap) vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    return rc;
}

static int eqi(const char *a, const char *b) {
    while (*a && *b && toupper((unsigned char)*a) == toupper((unsigned char)*b)) a++, b++;
    return *a == 0 && *b == 0;
}

/* split on blanks; *rest_at[i] is where token i starts in the line */
static int tokens(char *buf, char **tok, const char **at, const char *line) {
    int n = 0;
    char *p = buf;
    while (*p && n < MAX_TOK) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        tok[n] = p;
        at[n] = line + (p - buf);
        n++;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    return n;
}

static int number(const char *s, double *v) {
    char *end;
    *v = strtod(s, &end);
    return end != s && *end == 0 && isfinite(*v);
}

static int is_point(const char *t) { return strchr(t, ',') || strchr(t, '<'); }

/* a point; updates s->last */
static int point(SqcSession *s, const char *t, SqcPoint *p) {
    int rel = *t == '@';
    if (rel) t++;
    SqcPoint from = rel ? s->last : (SqcPoint){0, 0, 0};
    const char *lt = strchr(t, '<');
    if (lt) {
        char d[64];
        size_t n = (size_t)(lt - t);
        if (n == 0 || n >= sizeof d) return 0;
        memcpy(d, t, n);
        d[n] = 0;
        double dist, ang;
        if (!number(d, &dist) || !number(lt + 1, &ang)) return 0;
        double a = ang * M_PI / 180;
        double c = cos(a), sn = sin(a);
        if (fmod(ang, 90.0) == 0) {
            int q = ((int)(ang / 90.0) % 4 + 4) % 4;
            c = q == 0 ? 1 : q == 2 ? -1 : 0;
            sn = q == 1 ? 1 : q == 3 ? -1 : 0;
        }
        *p = (SqcPoint){from.x + dist * c, from.y + dist * sn, from.z};
    } else {
        double x, y, z = 0;
        char tail;
        int got = sscanf(t, "%lf,%lf,%lf%c", &x, &y, &z, &tail);
        if (got < 2 || got > 3) {
            got = sscanf(t, "%lf,%lf%c", &x, &y, &tail);
            if (got != 2) return 0;
            z = 0;
        }
        if (!isfinite(x) || !isfinite(y) || !isfinite(z)) return 0;
        *p = (SqcPoint){from.x + x, from.y + y, from.z + z};
    }
    s->last = *p;
    return 1;
}

static void checkpoint(SqcSession *s) {
    s->undo = s->dwg;
    s->can_undo = 1;
    s->dirty = 1;
}

/* the ids of a selection in tok[i..n): ids, ALL or LAST; stops at the first
 * point. Returns how many tokens it used, or -1 with out set. */
static int selection(SqcSession *s, char **tok, int i, int n, int *ids, int *n_ids, char *out, size_t cap) {
    int used = 0;
    *n_ids = 0;
    for (; i < n && !is_point(tok[i]); i++, used++) {
        if (eqi(tok[i], "ALL")) {
            for (int k = 0; k < s->dwg.ent_count && *n_ids < SQC_MAX_ENTS; k++)
                if (!sqc_in_block(&s->dwg, s->dwg.ents[k].id)) ids[(*n_ids)++] = s->dwg.ents[k].id;
            continue;
        }
        int id;
        char tail;
        if (eqi(tok[i], "LAST")) id = s->last_id;
        else if (sscanf(tok[i], "%d%c", &id, &tail) != 1) break;
        if (!sqc_find(&s->dwg, id)) return say(out, cap, -1, "no entity %s", tok[i]);
        if (sqc_in_block(&s->dwg, id)) return say(out, cap, -1, "entity %d is inside a block", id);
        if (*n_ids < SQC_MAX_ENTS) ids[(*n_ids)++] = id;
    }
    if (*n_ids == 0) return say(out, cap, -1, "select entities: ids, ALL or LAST");
    return used;
}

static size_t describe(const SqcDrawing *d, const SqcEntity *e, char *out, size_t cap) {
    const SqcLayer *L = sqc_layer_of(d, e->layer_id);
    const char *ln = L ? L->name : "?";
    switch (e->type) {
    case SQC_ENT_LINE:
        return (size_t)snprintf(out, cap, "%d LINE %s %g,%g %g,%g len %g", e->id, ln, e->p1.x, e->p1.y, e->p2.x,
                                e->p2.y, sqc_ent_length(e));
    case SQC_ENT_CIRCLE:
        return (size_t)snprintf(out, cap, "%d CIRCLE %s %g,%g r %g", e->id, ln, e->p1.x, e->p1.y, e->radius);
    case SQC_ENT_ARC:
        return (size_t)snprintf(out, cap, "%d ARC %s %g,%g r %g %g..%g deg", e->id, ln, e->p1.x, e->p1.y, e->radius,
                                e->start_ang, e->end_ang);
    case SQC_ENT_PLINE:
        return (size_t)snprintf(out, cap, "%d PLINE %s %d vertices len %g", e->id, ln, e->vert_count,
                                sqc_ent_length(e));
    case SQC_ENT_TEXT:
        return (size_t)snprintf(out, cap, "%d TEXT %s %g,%g \"%s\"", e->id, ln, e->p1.x, e->p1.y, e->text);
    case SQC_ENT_INSERT:
        return (size_t)snprintf(out, cap, "%d INSERT %s %s at %g,%g scale %g rot %g", e->id, ln, e->block_name,
                                e->p1.x, e->p1.y, e->scale, e->rotation_deg);
    }
    return 0;
}

static int cmd_layer(SqcSession *s, char **tok, int n, char *out, size_t cap) {
    SqcDrawing *d = &s->dwg;
    if (n >= 2 && eqi(tok[1], "LIST")) {
        size_t at = 0;
        for (int i = 0; i < d->layer_count && at < cap; i++)
            at += (size_t)snprintf(out + at, cap - at, "%s%s%s color %u%s", i ? "\n" : "", d->layers[i].name,
                                   d->layers[i].id == d->current_layer ? " (current)" : "", d->layers[i].color,
                                   d->layers[i].on ? "" : " off");
        return SQC_CMD_OK;
    }
    if (n < 3) return say(out, cap, -1, "LAYER NEW|SET|ON|OFF|COLOR name, or LAYER LIST");
    const char *name = tok[2];
    if (strlen(name) >= SQC_MAX_NAME) return say(out, cap, -1, "layer name too long");
    SqcLayer *L = NULL;
    for (int i = 0; i < d->layer_count; i++)
        if (strcmp(d->layers[i].name, name) == 0) L = &d->layers[i];
    if (eqi(tok[1], "NEW")) {
        double c = 7;
        if (n >= 4 && (!number(tok[3], &c) || c < 0 || c > 0xffffff)) return say(out, cap, -1, "bad color %s", tok[3]);
        if (L) return say(out, cap, -1, "layer %s exists", name);
        checkpoint(s);
        if (sqc_add_layer(d, name, (uint32_t)c) < 0) return say(out, cap, -1, "too many layers");
        sqc_set_layer(d, name);
        return say(out, cap, 0, "layer %s (current)", name);
    }
    if (!L) return say(out, cap, -1, "no layer %s", name);
    if (eqi(tok[1], "SET")) {
        s->dirty = 1;
        d->current_layer = L->id;
        return say(out, cap, 0, "layer %s (current)", name);
    }
    if (eqi(tok[1], "ON") || eqi(tok[1], "OFF")) {
        checkpoint(s);
        L->on = eqi(tok[1], "ON");
        return say(out, cap, 0, "layer %s %s", name, L->on ? "on" : "off");
    }
    if (eqi(tok[1], "COLOR") || eqi(tok[1], "COLOUR")) {
        double c;
        if (n < 4 || !number(tok[3], &c) || c < 0 || c > 0xffffff) return say(out, cap, -1, "LAYER COLOR name N");
        checkpoint(s);
        L->color = (uint32_t)c;
        return say(out, cap, 0, "layer %s color %u", name, L->color);
    }
    return say(out, cap, -1, "LAYER NEW|SET|ON|OFF|COLOR name, or LAYER LIST");
}

int sqc_exec(SqcSession *s, const char *line, char *out, size_t cap) {
    char buf[1024];
    char *tok[MAX_TOK];
    const char *at[MAX_TOK];
    if (cap) out[0] = 0;
    size_t len = strcspn(line, "#\r\n");
    if (len >= sizeof buf) return say(out, cap, -1, "line too long");
    memcpy(buf, line, len);
    buf[len] = 0;
    int n = tokens(buf, tok, at, line);
    if (n == 0) return SQC_CMD_OK;
    SqcDrawing *d = &s->dwg;
    const char *c = tok[0];

    if (eqi(c, "LINE")) {
        if (n < 3) return say(out, cap, -1, "LINE P1 P2 [P3 ...]");
        SqcPoint p[MAX_TOK];
        for (int i = 1; i < n; i++)
            if (!point(s, tok[i], &p[i - 1])) return say(out, cap, -1, "bad point %s", tok[i]);
        checkpoint(s);
        int first = 0;
        for (int i = 1; i < n - 1; i++) {
            int id = sqc_line(d, p[i - 1], p[i]);
            if (id < 0) return say(out, cap, -1, "drawing full");
            if (!first) first = id;
            s->last_id = id;
        }
        return n == 3 ? say(out, cap, 0, "id=%d", first) : say(out, cap, 0, "ids=%d..%d", first, s->last_id);
    }
    if (eqi(c, "CIRCLE")) {
        SqcPoint p;
        double r;
        if (n != 3 || !point(s, tok[1], &p) || !number(tok[2], &r) || r <= 0)
            return say(out, cap, -1, "CIRCLE CENTRE RADIUS");
        checkpoint(s);
        if ((s->last_id = sqc_circle(d, p, r)) < 0) return say(out, cap, -1, "drawing full");
        return say(out, cap, 0, "id=%d", s->last_id);
    }
    if (eqi(c, "ARC")) {
        SqcPoint p;
        double r, a0, a1;
        if (n != 5 || !point(s, tok[1], &p) || !number(tok[2], &r) || r <= 0 || !number(tok[3], &a0) ||
            !number(tok[4], &a1))
            return say(out, cap, -1, "ARC CENTRE RADIUS START END");
        checkpoint(s);
        if ((s->last_id = sqc_arc(d, p, r, a0, a1)) < 0) return say(out, cap, -1, "drawing full");
        return say(out, cap, 0, "id=%d", s->last_id);
    }
    if (eqi(c, "PLINE")) {
        SqcPoint p[SQC_MAX_PLINE + 1];
        int k = 0, close = 0;
        for (int i = 1; i < n; i++) {
            if (eqi(tok[i], "CLOSE") && i == n - 1) {
                close = 1;
                break;
            }
            if (k >= SQC_MAX_PLINE) return say(out, cap, -1, "at most %d vertices", SQC_MAX_PLINE);
            if (!point(s, tok[i], &p[k++])) return say(out, cap, -1, "bad point %s", tok[i]);
        }
        if (close) {
            if (k >= SQC_MAX_PLINE) return say(out, cap, -1, "at most %d vertices", SQC_MAX_PLINE);
            p[k] = p[0];
            k++;
        }
        if (k < 2) return say(out, cap, -1, "PLINE P1 P2 ... [CLOSE]");
        checkpoint(s);
        if ((s->last_id = sqc_pline(d, p, k)) < 0) return say(out, cap, -1, "drawing full");
        return say(out, cap, 0, "id=%d", s->last_id);
    }
    if (eqi(c, "TEXT")) {
        SqcPoint p;
        if (n < 3 || !point(s, tok[1], &p)) return say(out, cap, -1, "TEXT POINT words");
        const char *text = at[2];
        size_t tl = strcspn(text, "#\r\n");
        while (tl && (text[tl - 1] == ' ' || text[tl - 1] == '\t')) tl--;
        char t[SQC_MAX_NAME];
        if (tl >= sizeof t) return say(out, cap, -1, "text longer than %d", SQC_MAX_NAME - 1);
        memcpy(t, text, tl);
        t[tl] = 0;
        checkpoint(s);
        if ((s->last_id = sqc_text(d, p, t)) < 0) return say(out, cap, -1, "drawing full");
        return say(out, cap, 0, "id=%d", s->last_id);
    }
    if (eqi(c, "LAYER")) return cmd_layer(s, tok, n, out, cap);
    if (eqi(c, "BLOCK")) {
        SqcPoint base;
        if (n < 4 || !point(s, tok[2], &base)) return say(out, cap, -1, "BLOCK NAME BASE sel");
        if (strlen(tok[1]) >= SQC_MAX_NAME) return say(out, cap, -1, "block name too long");
        if (sqc_find_block(d, tok[1])) return say(out, cap, -1, "block %s exists", tok[1]);
        int ids[SQC_MAX_ENTS], k;
        int used = selection(s, tok, 3, n, ids, &k, out, cap);
        if (used < 0) return SQC_CMD_ERROR;
        if (3 + used != n) return say(out, cap, -1, "BLOCK NAME BASE sel");
        if (k > SQC_MAX_BLOCK_ENTS) return say(out, cap, -1, "at most %d entities in a block", SQC_MAX_BLOCK_ENTS);
        for (int i = 0; i < k; i++) {
            const SqcEntity *e = sqc_find(d, ids[i]);
            if (e->type == SQC_ENT_INSERT && strcmp(e->block_name, tok[1]) == 0)
                return say(out, cap, -1, "a block can't hold itself");
        }
        checkpoint(s);
        if (sqc_block_def(d, tok[1], base, ids, k) < 0) return say(out, cap, -1, "too many blocks");
        return say(out, cap, 0, "block %s of %d entities", tok[1], k);
    }
    if (eqi(c, "INSERT")) {
        SqcPoint p;
        double sc = 1, rot = 0;
        if (n < 3 || n > 5 || !point(s, tok[2], &p) || (n >= 4 && (!number(tok[3], &sc) || sc == 0)) ||
            (n == 5 && !number(tok[4], &rot)))
            return say(out, cap, -1, "INSERT NAME POINT [SCALE [ROTATION]]");
        if (!sqc_find_block(d, tok[1])) return say(out, cap, -1, "no block %s", tok[1]);
        checkpoint(s);
        if ((s->last_id = sqc_insert(d, tok[1], p, sc, rot)) < 0) return say(out, cap, -1, "drawing full");
        return say(out, cap, 0, "id=%d", s->last_id);
    }
    if (eqi(c, "ERASE") || eqi(c, "MOVE") || eqi(c, "COPY") || eqi(c, "ROTATE") || eqi(c, "SCALE")) {
        int ids[SQC_MAX_ENTS], k;
        int used = selection(s, tok, 1, n, ids, &k, out, cap);
        if (used < 0) return SQC_CMD_ERROR;
        int i = 1 + used;
        if (eqi(c, "ERASE")) {
            if (i != n) return say(out, cap, -1, "ERASE sel");
            checkpoint(s);
            for (int j = 0; j < k; j++) sqc_erase(d, ids[j]);
            return say(out, cap, 0, "erased %d", k);
        }
        SqcPoint a, b = {0, 0, 0};
        double v = 0;
        int two_points = eqi(c, "MOVE") || eqi(c, "COPY");
        if (i + 2 != n || !point(s, tok[i], &a) || (two_points ? !point(s, tok[i + 1], &b) : !number(tok[i + 1], &v)))
            return say(out, cap, -1, two_points ? "%s sel FROM TO" : eqi(c, "ROTATE") ? "ROTATE sel BASE DEGREES"
                                                                                      : "SCALE sel BASE FACTOR",
                       c);
        if (eqi(c, "SCALE") && v == 0) return say(out, cap, -1, "a scale of 0");
        if (eqi(c, "COPY") && d->ent_count + k > SQC_MAX_ENTS) return say(out, cap, -1, "drawing full");
        checkpoint(s);
        SqcPoint mv = {b.x - a.x, b.y - a.y, b.z - a.z};
        for (int j = 0; j < k; j++) {
            int id = ids[j];
            if (eqi(c, "COPY")) s->last_id = id = sqc_copy(d, id);
            SqcEntity *e = sqc_find(d, id);
            if (two_points) sqc_transform(e, a, 1, 0, mv);
            else if (eqi(c, "ROTATE")) sqc_transform(e, a, 1, v, (SqcPoint){0, 0, 0});
            else sqc_transform(e, a, v, 0, (SqcPoint){0, 0, 0});
        }
        return say(out, cap, 0, "%s %d", eqi(c, "COPY") ? "copied" : eqi(c, "MOVE") ? "moved"
                                       : eqi(c, "ROTATE")                           ? "rotated"
                                                                                    : "scaled",
                   k);
    }
    if (eqi(c, "LIST")) {
        int ids[SQC_MAX_ENTS], k = 0;
        if (n > 1) {
            int used = selection(s, tok, 1, n, ids, &k, out, cap);
            if (used < 0) return SQC_CMD_ERROR;
        } else {
            for (int j = 0; j < d->ent_count; j++)
                if (!sqc_in_block(d, d->ents[j].id)) ids[k++] = d->ents[j].id;
        }
        size_t w = (size_t)snprintf(out, cap, "%s: %d entities, %d layers, %d blocks", d->title, d->ent_count,
                                    d->layer_count, d->block_count);
        for (int j = 0; j < k && w < cap; j++) {
            w += (size_t)snprintf(out + w, cap - w, "\n");
            if (w < cap) w += describe(d, sqc_find(d, ids[j]), out + w, cap - w);
        }
        return SQC_CMD_OK;
    }
    if (eqi(c, "DIST")) {
        SqcPoint a, b;
        if (n != 3 || !point(s, tok[1], &a) || !point(s, tok[2], &b)) return say(out, cap, -1, "DIST P1 P2");
        double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        double ang = atan2(dy, dx) * 180 / M_PI;
        if (ang < 0) ang += 360;
        return say(out, cap, 0, "dist %.6g  angle %.6g deg  dx %.6g  dy %.6g  dz %.6g", sqrt(dx * dx + dy * dy + dz * dz),
                   ang, dx, dy, dz);
    }
    if (eqi(c, "AREA")) {
        int id;
        char tail;
        if (n == 2 && eqi(tok[1], "LAST")) id = s->last_id;
        else if (n != 2 || sscanf(tok[1], "%d%c", &id, &tail) != 1) return say(out, cap, -1, "AREA id");
        const SqcEntity *e = sqc_find(d, id);
        if (!e || (e->type != SQC_ENT_PLINE && e->type != SQC_ENT_CIRCLE))
            return say(out, cap, -1, "AREA needs a polyline or circle");
        return say(out, cap, 0, "area %.6g  perimeter %.6g", sqc_ent_area(e), sqc_ent_length(e));
    }
    if (eqi(c, "EXTENTS")) {
        SqcPoint lo, hi;
        if (!sqc_extents(d, &lo, &hi)) return say(out, cap, 0, "empty");
        return say(out, cap, 0, "%g,%g to %g,%g", lo.x, lo.y, hi.x, hi.y);
    }
    if (eqi(c, "UNDO")) {
        if (!s->can_undo) return say(out, cap, -1, "nothing to undo");
        s->dwg = s->undo;
        s->can_undo = 0;
        s->dirty = 1;
        return say(out, cap, 0, "undone");
    }
    if (eqi(c, "NEW")) {
        checkpoint(s);
        sqc_drawing_init(d, n >= 2 ? tok[1] : "Drawing1");
        return say(out, cap, 0, "new drawing %s", d->title);
    }
    if (eqi(c, "TITLE")) {
        if (n < 2) return say(out, cap, -1, "TITLE name");
        checkpoint(s);
        snprintf(d->title, sizeof d->title, "%s", tok[1]);
        return say(out, cap, 0, "title %s", d->title);
    }
    if (eqi(c, "SAVE")) {
        if (n >= 2) snprintf(s->path, sizeof s->path, "%s", tok[1]);
        if (!s->path[0]) return say(out, cap, -1, "SAVE path");
        if (sqc_write_sqdwg(d, s->path)) return say(out, cap, -1, "can't write %s", s->path);
        s->dirty = 0;
        return say(out, cap, 0, "saved %s", s->path);
    }
    if (eqi(c, "OPEN")) {
        if (n != 2) return say(out, cap, -1, "OPEN path");
        static SqcDrawing tmp;
        if (sqc_read_sqdwg(&tmp, tok[1])) return say(out, cap, -1, "can't read %s", tok[1]);
        checkpoint(s);
        *d = tmp;
        snprintf(s->path, sizeof s->path, "%s", tok[1]);
        s->dirty = 0;
        return say(out, cap, 0, "opened %s: %d entities", tok[1], d->ent_count);
    }
    if (eqi(c, "EXPORT-DXF")) {
        if (n != 2) return say(out, cap, -1, "EXPORT-DXF path");
        if (sqc_export_dxf(d, tok[1])) return say(out, cap, -1, "can't write %s", tok[1]);
        return say(out, cap, 0, "wrote %s", tok[1]);
    }
    if (eqi(c, "PLOT")) {
        if (n < 2) return say(out, cap, -1, "PLOT out.svg [A4|A3|A2|A1|A0|LETTER|TABLOID] [1:N|FIT] [PORTRAIT]");
        const char *size = "A3";
        double scale = 0;
        int landscape = 1;
        for (int i = 2; i < n; i++) {
            double den;
            if (eqi(tok[i], "PORTRAIT")) landscape = 0;
            else if (eqi(tok[i], "LANDSCAPE")) landscape = 1;
            else if (eqi(tok[i], "FIT")) scale = 0;
            else if (!strncmp(tok[i], "1:", 2) && number(tok[i] + 2, &den) && den > 0) scale = 1000.0 / den;
            else size = tok[i];
        }
        SqcSheet sh;
        if (sqc_sheet(size, landscape, &sh)) return say(out, cap, -1, "unknown sheet %s", size);
        sh.scale = scale;
        if (sqc_plot_svg(d, &sh, tok[1])) return say(out, cap, -1, "can't write %s", tok[1]);
        return say(out, cap, 0, "plotted %s on %s", tok[1], sh.name);
    }
    if (eqi(c, "QUIT") || eqi(c, "EXIT")) return SQC_CMD_QUIT;
    if (eqi(c, "HELP") || eqi(c, "?"))
        return say(out, cap, 0,
                   "LINE CIRCLE ARC PLINE TEXT LAYER BLOCK INSERT ERASE MOVE COPY ROTATE SCALE LIST DIST AREA "
                   "EXTENTS UNDO NEW TITLE SAVE OPEN EXPORT-DXF PLOT QUIT");
    return say(out, cap, -1, "unknown command %s", c);
}
