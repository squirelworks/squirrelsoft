/* paint: the SquirrelPaint core (see paint.h). Original code. */
#include "paint_int.h"

#include <string.h>

/* ---- pixels ---- */

void paint__dirty_reset(PaintDoc *d) { d->dirty = (PaintRect){0, 0, 0, 0}; }

/* grow the changed area to take in x0..x1, y0..y1 (inclusive, clipped) */
void paint__touch(PaintDoc *d, int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= d->w) x1 = d->w - 1;
    if (y1 >= d->h) y1 = d->h - 1;
    if (x0 > x1 || y0 > y1) return;
    PaintRect *r = &d->dirty;
    if (!r->w) {
        *r = (PaintRect){x0, y0, x1 - x0 + 1, y1 - y0 + 1};
        return;
    }
    int rx1 = r->x + r->w - 1, ry1 = r->y + r->h - 1;
    if (x0 < r->x) r->x = x0;
    if (y0 < r->y) r->y = y0;
    if (x1 > rx1) rx1 = x1;
    if (y1 > ry1) ry1 = y1;
    r->w = rx1 - r->x + 1;
    r->h = ry1 - r->y + 1;
}

static void plot(PaintDoc *d, int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= d->w || y >= d->h) return;
    d->px[(size_t)y * (size_t)d->w + (size_t)x] = c;
}

/* x0..x1 on row y, already in order */
void paint__span(PaintDoc *d, int x0, int x1, int y, uint32_t c) {
    if (y < 0 || y >= d->h) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= d->w) x1 = d->w - 1;
    uint32_t *p = d->px + (size_t)y * (size_t)d->w;
    for (int x = x0; x <= x1; x++) p[x] = c;
}

uint32_t paint_get(const PaintDoc *d, int x, int y) {
    if (x < 0 || y < 0 || x >= d->w || y >= d->h) return 0;
    return d->px[(size_t)y * (size_t)d->w + (size_t)x];
}

static void undo_reset(PaintDoc *d) {
    d->nsteps = d->done = 0;
    d->used = 0;
    d->active = 0;
    d->sel.w = d->sel.h = d->from.w = 0;
    d->floating = d->sel_mode = 0;
    paint__dirty_reset(d);
}

int paint_new(PaintDoc *d, int w, int h) {
    if (w < 1 || h < 1 || w > PAINT_MAX_W || h > PAINT_MAX_H) return -1;
    d->w = w;
    d->h = h;
    for (size_t i = 0; i < (size_t)w * (size_t)h; i++) d->px[i] = 0xFFFFFFFFu;
    d->tool = PAINT_BRUSH;
    d->fg = 0xFF000000u;
    d->bg = 0xFFFFFFFFu;
    d->size = 4;
    d->filled = 0;
    d->tolerance = 32;
    d->changed = 0;
    undo_reset(d);
    return 0;
}

int paint_load(PaintDoc *d, const uint32_t *px, int w, int h) {
    if (w < 1 || h < 1 || w > PAINT_MAX_W || h > PAINT_MAX_H) return -1;
    d->w = w;
    d->h = h;
    for (size_t i = 0; i < (size_t)w * (size_t)h; i++) d->px[i] = px[i] | 0xFF000000u;
    d->changed = 0;
    undo_reset(d);
    return 0;
}

/* ---- shapes ---- */

static int clamp_size(int s) { return s < 1 ? 1 : s > PAINT_SIZE_MAX ? PAINT_SIZE_MAX : s; }

/* a round dab size across, centred on (x, y) (for an even size, the centre
 * is the corner up and left of the middle) */
void paint_dab(PaintDoc *d, int x, int y, int size, uint32_t c) {
    size = clamp_size(size);
    int x0 = x - (size - 1) / 2, y0 = y - (size - 1) / 2;
    paint__touch(d, x0, y0, x0 + size - 1, y0 + size - 1);
    if (size <= 2) { /* a pixel, or a 2x2 square */
        for (int j = 0; j < size; j++) paint__span(d, x0, x0 + size - 1, y0 + j, c);
        return;
    }
    /* row j covers the columns whose centres are within size/2 of the dab's centre */
    double m = (size - 1) / 2.0, r2 = (size / 2.0) * (size / 2.0);
    for (int j = 0; j < size; j++) {
        double dy = j - m;
        int i = 0;
        while (i < size && (i - m) * (i - m) + dy * dy > r2) i++;
        if (i < size) paint__span(d, x0 + i, x0 + size - 1 - i, y0 + j, c);
    }
}

void paint_line(PaintDoc *d, int x0, int y0, int x1, int y1, int size, uint32_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (size <= 1) {
            plot(d, x0, y0, c);
            paint__touch(d, x0, y0, x0, y0);
        } else {
            paint_dab(d, x0, y0, size, c);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void order(int *a, int *b) {
    if (*a > *b) {
        int t = *a;
        *a = *b;
        *b = t;
    }
}

void paint_rect(PaintDoc *d, int x0, int y0, int x1, int y1, int size, int filled, uint32_t c) {
    order(&x0, &x1);
    order(&y0, &y1);
    size = clamp_size(size);
    paint__touch(d, x0, y0, x1, y1);
    if (filled || 2 * size >= x1 - x0 + 1 || 2 * size >= y1 - y0 + 1) {
        for (int y = y0; y <= y1; y++) paint__span(d, x0, x1, y, c);
        return;
    }
    for (int k = 0; k < size; k++) { /* the outline, size thick, inside the box */
        paint__span(d, x0, x1, y0 + k, c);
        paint__span(d, x0, x1, y1 - k, c);
    }
    for (int y = y0 + size; y <= y1 - size; y++) {
        paint__span(d, x0, x0 + size - 1, y, c);
        paint__span(d, x1 - size + 1, x1, y, c);
    }
}

static int ifloor(double v) {
    int i = (int)v;
    return v < i ? i - 1 : i;
}
static int iceil(double v) { return -ifloor(-v); }

/* the columns of row y inside the ellipse that fills the box of half
 * widths rx, ry about (cx, cy): returns 0 when none, else 1 with lo..hi */
static int ellipse_row(double cx, double cy, double rx, double ry, int y, int *lo, int *hi) {
    if (rx <= 0 || ry <= 0) return 0;
    double t = (y - cy) / ry;
    double k = 1 - t * t;
    if (k < 0) return 0;
    /* the half width at this row, from x/rx = sqrt(k), by Newton (no libm) */
    double s = k > 0 ? (k < 1 ? 1 : k) : 0;
    for (int i = 0; i < 40 && s > 0; i++) s = 0.5 * (s + k / s);
    double half = rx * s;
    int l = iceil(cx - half - 1e-9), h = ifloor(cx + half + 1e-9); /* pixel centres within */
    if (l > h) return 0;
    *lo = l;
    *hi = h;
    return 1;
}

void paint_ellipse(PaintDoc *d, int x0, int y0, int x1, int y1, int size, int filled, uint32_t c) {
    order(&x0, &x1);
    order(&y0, &y1);
    size = clamp_size(size);
    paint__touch(d, x0, y0, x1, y1);
    /* pixel centres are at whole numbers; the box's edges half a pixel out */
    double cx = (x0 + x1) / 2.0, cy = (y0 + y1) / 2.0;
    double rx = (x1 - x0 + 1) / 2.0, ry = (y1 - y0 + 1) / 2.0;
    double irx = rx - size, iry = ry - size; /* the hole inside an outline */
    for (int y = y0; y <= y1; y++) {
        int lo, hi, ilo, ihi;
        if (y < 0 || y >= d->h || !ellipse_row(cx, cy, rx, ry, y, &lo, &hi)) continue;
        if (filled || !ellipse_row(cx, cy, irx, iry, y, &ilo, &ihi)) {
            paint__span(d, lo, hi, y, c);
        } else {
            if (ilo > lo) paint__span(d, lo, ilo - 1, y, c);
            else plot(d, lo, y, c); /* keep the outline closed where it is steep */
            if (ihi < hi) paint__span(d, ihi + 1, hi, y, c);
            else plot(d, hi, y, c);
        }
    }
}

/* how far apart two colours are: the most any channel differs */
static int distance(uint32_t a, uint32_t b) {
    int m = 0;
    for (int s = 0; s < 24; s += 8) {
        int k = (int)((a >> s) & 0xFF) - (int)((b >> s) & 0xFF);
        if (k < 0) k = -k;
        if (k > m) m = k;
    }
    return m;
}

long paint_flood(PaintDoc *d, int x, int y, uint32_t c, int tolerance) {
    if (x < 0 || y < 0 || x >= d->w || y >= d->h) return 0;
    size_t w = (size_t)d->w, n = w * (size_t)d->h;
    uint32_t target = d->px[(size_t)y * w + (size_t)x];
    if (tolerance < 0) tolerance = 0;
    if (target == c && tolerance == 0) return 0;
    memset(d->mark, 0, (n + 7) / 8);
    size_t top = 0;
    long count = 0;
    size_t start = (size_t)y * w + (size_t)x;
    d->todo[top++] = (uint32_t)start;
    d->mark[start >> 3] |= (uint8_t)(1u << (start & 7));
    int minx = x, maxx = x, miny = y, maxy = y;
    while (top) {
        size_t i = d->todo[--top];
        int px = (int)(i % w), py = (int)(i / w);
        d->px[i] = c;
        count++;
        if (px < minx) minx = px;
        if (px > maxx) maxx = px;
        if (py < miny) miny = py;
        if (py > maxy) maxy = py;
        /* the four neighbours: marked when they go on the list, so each goes once */
        size_t nb[4];
        int k = 0;
        if (px > 0) nb[k++] = i - 1;
        if ((size_t)px + 1 < w) nb[k++] = i + 1;
        if (py > 0) nb[k++] = i - w;
        if (py + 1 < d->h) nb[k++] = i + w;
        for (int j = 0; j < k; j++) {
            size_t q = nb[j];
            if (d->mark[q >> 3] & (1u << (q & 7))) continue;
            if (distance(d->px[q], target) > tolerance) continue;
            d->mark[q >> 3] |= (uint8_t)(1u << (q & 7));
            d->todo[top++] = (uint32_t)q;
        }
    }
    paint__touch(d, minx, miny, maxx, maxy);
    return count;
}

/* ---- undo ---- */

static size_t area(PaintRect r) { return (size_t)r.w * (size_t)r.h; }

/* swap the step's pixels with the picture's: in its rectangle, or the
 * whole picture (the sizes swap too, with todo to hold one of them) */
static void swap_step(PaintDoc *d, PaintStep *s) {
    uint32_t *p = d->store + s->at;
    if (s->whole) {
        size_t now = (size_t)d->w * (size_t)d->h, then = area(s->r);
        memcpy(d->todo, d->px, now * sizeof *p);
        memcpy(d->px, p, then * sizeof *p);
        memcpy(p, d->todo, now * sizeof *p);
        int w = d->w, h = d->h;
        d->w = s->r.w;
        d->h = s->r.h;
        s->r.w = w;
        s->r.h = h;
        return;
    }
    for (int j = 0; j < s->r.h; j++) {
        uint32_t *q = d->px + (size_t)(s->r.y + j) * (size_t)d->w + (size_t)s->r.x;
        for (int i = 0; i < s->r.w; i++) {
            uint32_t t = q[i];
            q[i] = p[i];
            p[i] = t;
        }
        p += s->r.w;
    }
}

static void drop_oldest(PaintDoc *d) {
    size_t n = d->step[0].cap;
    memmove(d->store, d->store + n, (d->used - n) * sizeof d->store[0]);
    d->used -= n;
    memmove(&d->step[0], &d->step[1], (size_t)(d->nsteps - 1) * sizeof d->step[0]);
    d->nsteps--;
    d->done--;
    for (int k = 0; k < d->nsteps; k++) d->step[k].at -= n;
}

/* room for a step of need pixels at the end of the store; 0, or -1 when it
 * can never fit (then nothing before it can be undone either) */
static int make_room(PaintDoc *d, size_t need) {
    d->changed = 1;
    if (d->done < d->nsteps) { /* what was undone can't come back now */
        d->nsteps = d->done;
        d->used = d->done ? d->step[d->done - 1].at + d->step[d->done - 1].cap : 0;
    }
    if (need > PAINT_UNDO_PX) {
        d->nsteps = d->done = 0;
        d->used = 0;
        return -1;
    }
    while (d->nsteps && (d->nsteps == PAINT_UNDO_STEPS || d->used + need > PAINT_UNDO_PX)) drop_oldest(d);
    return 0;
}

static void add_step(PaintDoc *d, PaintRect r, size_t cap, int whole) {
    d->step[d->nsteps] = (PaintStep){r, d->used, cap, whole};
    d->used += cap;
    d->nsteps++;
    d->done = d->nsteps;
}

void paint__push_step(PaintDoc *d, PaintRect r) {
    if (make_room(d, area(r))) return;
    uint32_t *p = d->store + d->used;
    for (int j = 0; j < r.h; j++) {
        memcpy(p, d->before + (size_t)(r.y + j) * (size_t)d->w + (size_t)r.x, (size_t)r.w * sizeof *p);
        p += r.w;
    }
    add_step(d, r, area(r), 0);
}

void paint__push_whole(PaintDoc *d, int ow, int oh) {
    size_t then = (size_t)ow * (size_t)oh, now = (size_t)d->w * (size_t)d->h;
    size_t cap = then > now ? then : now; /* undo puts the other size here */
    if (make_room(d, cap)) return;
    memcpy(d->store + d->used, d->before, then * sizeof d->store[0]);
    add_step(d, (PaintRect){0, 0, ow, oh}, cap, 1);
}

int paint_can_undo(const PaintDoc *d) { return d->done > 0 && !d->active; }
int paint_can_redo(const PaintDoc *d) { return d->done < d->nsteps && !d->active; }

int paint_undo(PaintDoc *d) {
    if (d->floating) paint_drop(d); /* what floats is a step of its own first */
    if (!paint_can_undo(d)) return -1;
    swap_step(d, &d->step[--d->done]);
    d->sel.w = 0;
    d->changed = 1;
    return 0;
}

int paint_redo(PaintDoc *d) {
    if (d->floating) paint_drop(d);
    if (!paint_can_redo(d)) return -1;
    swap_step(d, &d->step[d->done++]);
    d->sel.w = 0;
    d->changed = 1;
    return 0;
}

/* ---- the tools ---- */

void paint__save_before(PaintDoc *d) {
    memcpy(d->before, d->px, (size_t)d->w * (size_t)d->h * sizeof d->px[0]);
    paint__dirty_reset(d);
}

/* what the press has drawn so far, taken back */
static void restore_dirty(PaintDoc *d) {
    PaintRect r = d->dirty;
    for (int j = 0; j < r.h; j++) {
        size_t o = (size_t)(r.y + j) * (size_t)d->w + (size_t)r.x;
        memcpy(d->px + o, d->before + o, (size_t)r.w * sizeof d->px[0]);
    }
    paint__dirty_reset(d);
}

static int iabs(int v) { return v < 0 ? -v : v; }

/* the end of a shape from (x0, y0) held to a square, a circle, or a line
 * at a multiple of 45 degrees */
static void constrain(const PaintDoc *d, int *x, int *y) {
    int dx = *x - d->x0, dy = *y - d->y0, ax = iabs(dx), ay = iabs(dy);
    if (d->tool == PAINT_LINE) {
        if (ax > 2 * ay) {
            *y = d->y0;
            return;
        }
        if (ay > 2 * ax) {
            *x = d->x0;
            return;
        }
    }
    int m = ax > ay ? ax : ay;
    *x = d->x0 + (dx < 0 ? -m : m);
    *y = d->y0 + (dy < 0 ? -m : m);
}

static uint32_t ink(const PaintDoc *d) {
    if (d->tool == PAINT_ERASER) return d->active == 2 ? d->fg : d->bg;
    return d->active == 2 ? d->bg : d->fg;
}

/* the shape tools: the shape from the press to (x, y), in place of the last */
static void shape(PaintDoc *d, int x, int y, int square) {
    restore_dirty(d);
    if (square) constrain(d, &x, &y);
    uint32_t c = ink(d);
    if (d->tool == PAINT_LINE) paint_line(d, d->x0, d->y0, x, y, d->size, c);
    else if (d->tool == PAINT_RECT) paint_rect(d, d->x0, d->y0, x, y, d->size, d->filled, c);
    else paint_ellipse(d, d->x0, d->y0, x, y, d->size, d->filled, c);
}

static int stroke_size(const PaintDoc *d) { return d->tool == PAINT_PENCIL ? 1 : d->size; }

void paint_press(PaintDoc *d, int x, int y, int button, int square) {
    if (d->active) paint_release(d, d->lx, d->ly, square);
    button = button == 2 ? 2 : 1;
    if (d->tool == PAINT_SELECT) {
        paint__sel_press(d, x, y);
        return;
    }
    if (d->floating) paint_drop(d); /* drawing puts down what floats */
    if (d->tool == PAINT_PICKER) {
        if (x < 0 || y < 0 || x >= d->w || y >= d->h) return;
        if (button == 2) d->bg = paint_get(d, x, y);
        else d->fg = paint_get(d, x, y);
        return;
    }
    paint__save_before(d);
    if (d->tool == PAINT_FILL) {
        if (paint_flood(d, x, y, button == 2 ? d->bg : d->fg, d->tolerance) > 0) paint__push_step(d, d->dirty);
        return;
    }
    d->active = button;
    d->x0 = d->lx = x;
    d->y0 = d->ly = y;
    if (d->tool == PAINT_PENCIL || d->tool == PAINT_BRUSH || d->tool == PAINT_ERASER)
        paint_line(d, x, y, x, y, stroke_size(d), ink(d));
    else
        shape(d, x, y, square);
}

void paint_drag(PaintDoc *d, int x, int y, int square) {
    if (d->sel_mode) {
        paint__sel_drag(d, x, y);
        return;
    }
    if (!d->active || (x == d->lx && y == d->ly)) return;
    if (d->tool == PAINT_PENCIL || d->tool == PAINT_BRUSH || d->tool == PAINT_ERASER)
        paint_line(d, d->lx, d->ly, x, y, stroke_size(d), ink(d));
    else
        shape(d, x, y, square);
    d->lx = x;
    d->ly = y;
}

void paint_release(PaintDoc *d, int x, int y, int square) {
    if (d->sel_mode) {
        paint__sel_release(d, x, y);
        return;
    }
    if (!d->active) return;
    if (d->tool == PAINT_LINE || d->tool == PAINT_RECT || d->tool == PAINT_ELLIPSE) shape(d, x, y, square);
    else paint_drag(d, x, y, square);
    d->active = 0;
    if (d->dirty.w) paint__push_step(d, d->dirty);
}

void paint_cancel(PaintDoc *d) {
    if (d->sel_mode == 1) d->sel.w = d->sel.h = 0; /* a rectangle half drawn */
    d->sel_mode = 0;
    if (!d->active) return;
    restore_dirty(d);
    d->active = 0;
}

/* ---- commands ---- */

void paint__say(char *msg, size_t cap, const char *a, const char *b) {
    if (!cap) return;
    size_t n = 0;
    for (const char *s = a; s && *s && n + 1 < cap; s++) msg[n++] = *s;
    for (const char *s = b; s && *s && n + 1 < cap; s++) msg[n++] = *s;
    msg[n] = 0;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* the next word: its start and length; 0 at the end */
size_t paint__word(const char **p, const char **start) {
    while (**p == ' ' || **p == '\t' || **p == ',') (*p)++;
    *start = *p;
    while (**p && **p != ' ' && **p != '\t' && **p != ',') (*p)++;
    return (size_t)(*p - *start);
}

int paint__is(const char *w, size_t n, const char *s) {
    size_t k = 0;
    for (; k < n && s[k]; k++)
        if (lower(w[k]) != s[k]) return 0;
    return k == n && !s[k];
}

int paint__number(const char *w, size_t n, int *v) {
    size_t i = 0;
    int neg = 0;
    long x = 0;
    if (i < n && (w[i] == '-' || w[i] == '+')) neg = w[i++] == '-';
    if (i == n) return -1;
    for (; i < n; i++) {
        if (w[i] < '0' || w[i] > '9') return -1;
        x = x * 10 + (w[i] - '0');
        if (x > 100000) return -1;
    }
    *v = (int)(neg ? -x : x);
    return 0;
}

int paint_parse_color(const char *s, size_t n, uint32_t *c) {
    if (n && s[0] == '#') s++, n--;
    if (n != 6) return -1;
    uint32_t v = 0;
    for (size_t i = 0; i < 6; i++) {
        int h = lower(s[i]);
        int k = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : -1;
        if (k < 0) return -1;
        v = v << 4 | (uint32_t)k;
    }
    *c = 0xFF000000u | v;
    return 0;
}

int paint_command(PaintDoc *d, const char *line, char *msg, size_t msgcap) {
    const char *p = line, *w;
    size_t n = paint__word(&p, &w);
    paint__say(msg, msgcap, "", "");
    if (!n) return 0;
    static const char *const tools[] = {"pencil", "brush", "eraser", "line", "rect", "ellipse", "fill", "pick"};
    int tool = -1;
    for (int k = 0; k < (int)(sizeof tools / sizeof tools[0]); k++)
        if (paint__is(w, n, tools[k])) tool = k;
    int rc;
    if (paint__sel_command(d, w, n, p, &rc, msg, msgcap)) return rc;
    if (paint__is(w, n, "undo") || paint__is(w, n, "redo")) {
        int redo = lower(w[0]) == 'r';
        if (paint__word(&p, &w)) {
            paint__say(msg, msgcap, redo ? "redo" : "undo", " takes nothing after it");
            return -1;
        }
        if ((redo ? paint_redo(d) : paint_undo(d)) < 0) {
            paint__say(msg, msgcap, redo ? "nothing to redo" : "nothing to undo", "");
            return -1;
        }
        return 0;
    }
    if (paint__is(w, n, "clear")) {
        if (d->floating) paint_drop(d);
        paint__save_before(d);
        for (int y = 0; y < d->h; y++) paint__span(d, 0, d->w - 1, y, d->bg);
        paint__push_step(d, (PaintRect){0, 0, d->w, d->h});
        return 0;
    }
    if (paint__is(w, n, "color") || paint__is(w, n, "colour") || paint__is(w, n, "bg")) {
        int bg = paint__is(w, n, "bg");
        uint32_t c;
        n = paint__word(&p, &w);
        const char *e;
        if (paint_parse_color(w, n, &c) || paint__word(&p, &e)) {
            paint__say(msg, msgcap, "expected a colour as RRGGBB, as in ", bg ? "bg ffffff" : "color ff0000");
            return -1;
        }
        if (bg) d->bg = c;
        else d->fg = c;
        return 0;
    }
    int vals[64], nv = 0, right = 0, square = 0, onoff = -1;
    const char *head = w;
    size_t headn = n;
    while ((n = paint__word(&p, &w))) {
        if (paint__is(w, n, "right")) right = 1;
        else if (paint__is(w, n, "square")) square = 1;
        else if (paint__is(w, n, "on")) onoff = 1;
        else if (paint__is(w, n, "off")) onoff = 0;
        else if (nv < 64 && paint__number(w, n, &vals[nv]) == 0) nv++;
        else {
            paint__say(msg, msgcap, "not a number: ", "");
            size_t k = 0;
            while (msg[k]) k++;
            for (size_t i = 0; i < n && k + 1 < msgcap && i < 20; i++) msg[k++] = w[i];
            if (msgcap) msg[k < msgcap ? k : msgcap - 1] = 0;
            return -1;
        }
    }
    if (paint__is(head, headn, "new")) {
        if (nv != 2 || paint_new(d, vals[0], vals[1])) {
            paint__say(msg, msgcap, "new W H: a size from 1x1 to the largest picture", "");
            return -1;
        }
        return 0;
    }
    if (paint__is(head, headn, "size") || paint__is(head, headn, "tolerance")) {
        int size = lower(head[0]) == 's';
        if (nv != 1 || vals[0] < (size ? 1 : 0) || vals[0] > (size ? PAINT_SIZE_MAX : 255)) {
            paint__say(msg, msgcap, size ? "size N: 1 to 64" : "tolerance N: 0 to 255", "");
            return -1;
        }
        if (size) d->size = vals[0];
        else d->tolerance = vals[0];
        return 0;
    }
    if (paint__is(head, headn, "fill") && onoff >= 0) {
        if (nv) {
            paint__say(msg, msgcap, "fill on or fill off, or fill X Y", "");
            return -1;
        }
        d->filled = onoff;
        return 0;
    }
    if (tool < 0) {
        paint__say(msg, msgcap, "unknown: try line, rect, ellipse, brush, fill, color, undo", "");
        return -1;
    }
    int stroke = tool <= PAINT_ERASER, two = tool >= PAINT_LINE && tool <= PAINT_ELLIPSE;
    if ((stroke && (nv < 2 || nv % 2)) || (two && nv != 4) || (!stroke && !two && nv != 2) || onoff >= 0) {
        paint__say(msg, msgcap, tools[tool],
            stroke ? ": X Y [X Y ...]" : two ? ": X0 Y0 X1 Y1 [square]" : ": X Y");
        return -1;
    }
    int keep = d->tool;
    d->tool = tool;
    paint_press(d, vals[0], vals[1], right ? 2 : 1, square);
    for (int k = 2; k + 1 < nv; k += 2) paint_drag(d, vals[k], vals[k + 1], square);
    paint_release(d, vals[nv - 2], vals[nv - 1], square);
    d->tool = keep;
    return 0;
}
