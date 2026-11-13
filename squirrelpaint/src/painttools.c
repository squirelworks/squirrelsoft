/* painttools: the spray, polygon, curve and text tools (see paint.h).
 * Original code. The font is the suite's own 5x7 one, from libs/sqgui. */
#include "paint_int.h"

#include <string.h>

#include "../../libs/sqgui/src/font5x7.inc"

static int ifloor(double v) {
    int i = (int)v;
    return v < i ? i - 1 : i;
}

static void plot(PaintDoc *d, int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= d->w || y >= d->h) return;
    d->px[(size_t)y * (size_t)d->w + (size_t)x] = c;
    paint__touch(d, x, y, x, y);
}

/* ---- drawing ---- */

static uint32_t next_random(PaintDoc *d) { /* xorshift32 */
    uint32_t v = d->seed ? d->seed : 0x9E3779B9u;
    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    return d->seed = v;
}

void paint_spray(PaintDoc *d, int x, int y, int size, uint32_t c) {
    int r = size < 2 ? 2 : size > PAINT_SIZE_MAX ? PAINT_SIZE_MAX : size;
    int dots = 2 + r * r / 3;
    for (int k = 0; k < dots; k++) {
        int dx, dy;
        do { /* a point of the square, until one falls in the circle */
            dx = (int)(next_random(d) % (uint32_t)(2 * r + 1)) - r;
            dy = (int)(next_random(d) % (uint32_t)(2 * r + 1)) - r;
        } while (dx * dx + dy * dy > r * r);
        plot(d, x + dx, y + dy, c);
    }
}

void paint_polygon(PaintDoc *d, const int (*pts)[2], int n, int size, int filled, uint32_t c) {
    if (n <= 0) return;
    if (filled && n >= 3) {
        /* rows through pixel centres: where each side crosses, then the
         * spans between the crossings taken in pairs (even-odd) */
        int ylo = pts[0][1], yhi = pts[0][1];
        for (int i = 1; i < n; i++) {
            if (pts[i][1] < ylo) ylo = pts[i][1];
            if (pts[i][1] > yhi) yhi = pts[i][1];
        }
        if (ylo < 0) ylo = 0;
        if (yhi > d->h - 1) yhi = d->h - 1;
        for (int y = ylo; y <= yhi; y++) {
            double cross[PAINT_POLY_MAX];
            int nc = 0;
            for (int i = 0; i < n; i++) {
                const int *a = pts[i], *b = pts[(i + 1) % n];
                if (a[1] == b[1]) continue;
                int lo = a[1] < b[1] ? a[1] : b[1], hi = a[1] < b[1] ? b[1] : a[1];
                if (y < lo || y >= hi) continue; /* a corner counts once: in the side that leaves it downwards */
                double x = a[0] + (double)(y - a[1]) * (b[0] - a[0]) / (b[1] - a[1]);
                int k = nc++;
                while (k > 0 && cross[k - 1] > x) {
                    cross[k] = cross[k - 1];
                    k--;
                }
                cross[k] = x;
            }
            for (int k = 0; k + 1 < nc; k += 2) { /* the pixel centres between */
                int x0 = -ifloor(-cross[k]), x1 = ifloor(cross[k + 1]);
                if (x1 < x0) continue;
                paint__span(d, x0, x1, y, c);
                paint__touch(d, x0, y, x1, y);
            }
        }
        size = 1; /* and the sides, so the edge is all there */
    }
    for (int i = 0; i < n; i++) {
        const int *a = pts[i], *b = pts[(i + 1) % n];
        paint_line(d, a[0], a[1], b[0], b[1], size, c);
        if (n == 2) break; /* two corners: one line, not there and back */
    }
}

static int iabs(int v) { return v < 0 ? -v : v; }
static int far_apart(int ax, int ay, int bx, int by) { return iabs(ax - bx) > iabs(ay - by) ? iabs(ax - bx) : iabs(ay - by); }
static int round_i(double v) { return v < 0 ? (int)(v - 0.5) : (int)(v + 0.5); }

void paint_curve(PaintDoc *d, int x0, int y0, int x1, int y1, int bx0, int by0, int bx1, int by1, int size,
                 uint32_t c) {
    /* enough pieces that each is a few pixels: the control polygon's length */
    int len = far_apart(x0, y0, bx0, by0) + far_apart(bx0, by0, bx1, by1) + far_apart(bx1, by1, x1, y1);
    int n = len / 3 + 2;
    if (n > 1024) n = 1024;
    int px = x0, py = y0;
    for (int k = 1; k <= n; k++) {
        double t = (double)k / n, u = 1 - t;
        double a = u * u * u, b = 3 * u * u * t, e = 3 * u * t * t, f = t * t * t;
        int x = round_i(a * x0 + b * bx0 + e * bx1 + f * x1), y = round_i(a * y0 + b * by0 + e * by1 + f * y1);
        if (k == 1 || x != px || y != py) paint_line(d, px, py, x, y, size, c);
        px = x;
        py = y;
    }
}

void paint_text(PaintDoc *d, int x, int y, const char *s, size_t n, int scale, uint32_t c) {
    if (scale < 1) scale = 1;
    int cx = x;
    for (size_t i = 0; i < n; i++) {
        unsigned ch = (unsigned char)s[i];
        if (ch == '\n') {
            cx = x;
            y += PAINT_GLYPH_H * scale;
            continue;
        }
        if (ch < 0x20 || ch > 0x7E) ch = '?';
        const unsigned char *g = sqg_font5x7[ch - 0x20];
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g[row] & (0x10 >> col)) {
                    int ax = cx + col * scale, ay = y + row * scale;
                    for (int j = 0; j < scale; j++) paint__span(d, ax, ax + scale - 1, ay + j, c);
                    paint__touch(d, ax, ay, ax + scale - 1, ay + scale - 1);
                }
        cx += PAINT_GLYPH_W * scale;
    }
}

/* ---- the tools ---- */

static uint32_t ink(const PaintDoc *d, int button) { return button == 2 ? d->bg : d->fg; }
static int text_scale(const PaintDoc *d) { return (d->size + 1) / 2; }

/* (x, y) held to a line at a multiple of 45 degrees from (ax, ay) */
static void hold45(int ax, int ay, int *x, int *y) {
    int dx = *x - ax, dy = *y - ay, mx = iabs(dx), my = iabs(dy);
    if (mx > 2 * my) *y = ay;
    else if (my > 2 * mx) *x = ax;
    else {
        int m = mx > my ? mx : my;
        *x = ax + (dx < 0 ? -m : m);
        *y = ay + (dy < 0 ? -m : m);
    }
}

/* what is under way, drawn again over the picture as it was; closed, the
 * polygon as it will be kept */
static void redraw(PaintDoc *d, int closed) {
    paint__restore_dirty(d);
    uint32_t c = ink(d, d->mbutton);
    if (d->multi == PAINT_POLYGON) {
        if (closed || d->npts < 3) paint_polygon(d, (const int (*)[2])d->pts, d->npts, d->size, closed && d->filled, c);
        else
            for (int i = 0; i + 1 < d->npts; i++)
                paint_line(d, d->pts[i][0], d->pts[i][1], d->pts[i + 1][0], d->pts[i + 1][1], d->size, c);
    } else if (d->multi == PAINT_CURVE) {
        int(*p)[2] = d->pts;
        if (d->stage == 0) paint_line(d, p[0][0], p[0][1], p[1][0], p[1][1], d->size, c);
        else paint_curve(d, p[0][0], p[0][1], p[1][0], p[1][1], p[2][0], p[2][1], p[3][0], p[3][1], d->size, c);
    } else if (d->multi == PAINT_TEXT) {
        paint_text(d, d->tx, d->ty, d->text, (size_t)d->ntext, text_scale(d), c);
    }
}

void paint_finish(PaintDoc *d) {
    if (!d->multi) return;
    redraw(d, 1);
    d->multi = 0;
    d->active = 0;
    if (d->dirty.w) paint__push_step(d, d->dirty);
}

void paint__multi_cancel(PaintDoc *d) {
    if (!d->multi) return;
    paint__restore_dirty(d);
    d->multi = 0;
    d->active = 0;
}

void paint_set_tool(PaintDoc *d, int tool) {
    if (tool < 0 || tool >= PAINT_TOOLS) return;
    if (d->active || d->sel_mode) paint_cancel(d);
    paint_finish(d);
    d->tool = tool;
}

void paint__multi_press(PaintDoc *d, int x, int y, int button, int square) {
    if (d->multi && d->multi != d->tool) paint_finish(d);
    if (d->tool == PAINT_TEXT) {
        paint_finish(d); /* a click elsewhere keeps the last text and starts another */
        paint__save_before(d);
        d->multi = PAINT_TEXT;
        d->mbutton = button;
        d->ntext = 0;
        d->tx = x;
        d->ty = y;
        return;
    }
    if (!d->multi) {
        paint__save_before(d);
        d->multi = d->tool;
        d->mbutton = button;
        d->npts = 2;
        d->stage = 0;
        d->pts[0][0] = d->pts[1][0] = x;
        d->pts[0][1] = d->pts[1][1] = y;
    } else if (d->multi == PAINT_POLYGON) {
        if (square) hold45(d->pts[d->npts - 1][0], d->pts[d->npts - 1][1], &x, &y);
        d->pts[d->npts][0] = x;
        d->pts[d->npts][1] = y;
        d->npts++;
    } else { /* a curve's bend: the first moves both, the second only its own */
        if (d->stage == 1) d->pts[2][0] = x, d->pts[2][1] = y;
        d->pts[3][0] = x;
        d->pts[3][1] = y;
    }
    d->active = button;
    d->lx = x;
    d->ly = y;
    redraw(d, 0);
}

void paint__multi_drag(PaintDoc *d, int x, int y, int square) {
    if (d->multi == PAINT_POLYGON) {
        int k = d->npts - 1;
        if (square) hold45(d->pts[k - 1][0], d->pts[k - 1][1], &x, &y);
        d->pts[k][0] = x;
        d->pts[k][1] = y;
    } else if (d->stage == 0) {
        if (square) hold45(d->pts[0][0], d->pts[0][1], &x, &y);
        d->pts[1][0] = x;
        d->pts[1][1] = y;
    } else {
        if (d->stage == 1) d->pts[2][0] = x, d->pts[2][1] = y;
        d->pts[3][0] = x;
        d->pts[3][1] = y;
    }
    d->lx = x;
    d->ly = y;
    redraw(d, 0);
}

void paint__multi_release(PaintDoc *d, int x, int y, int square) {
    paint__multi_drag(d, x, y, square);
    d->active = 0;
    int(*p)[2] = d->pts;
    if (d->multi == PAINT_CURVE) {
        if (d->stage == 0) {
            if (p[1][0] == p[0][0] && p[1][1] == p[0][1]) { /* a click is no line */
                paint__multi_cancel(d);
                return;
            }
            p[2][0] = p[0][0]; /* not bent yet: the bends on the ends */
            p[2][1] = p[0][1];
            p[3][0] = p[1][0];
            p[3][1] = p[1][1];
        }
        if (++d->stage == 3) paint_finish(d);
        return;
    }
    int n = d->npts, *last = p[n - 1], *prev = p[n - 2];
    if (n == 2 && last[0] == prev[0] && last[1] == prev[1]) {
        d->npts = 1; /* a click: the first corner */
        return;
    }
    if (n >= 3 && last[0] == prev[0] && last[1] == prev[1]) { /* twice on the last corner */
        d->npts--;
        paint_finish(d);
    } else if (n >= 4 && far_apart(last[0], last[1], p[0][0], p[0][1]) <= 3) { /* back at the first */
        d->npts--;
        paint_finish(d);
    } else if (n == PAINT_POLY_MAX) {
        paint_finish(d);
    }
}

void paint_tick(PaintDoc *d) {
    if (d->active && d->tool == PAINT_SPRAY) paint_spray(d, d->lx, d->ly, d->size, ink(d, d->active));
}

int paint_text_key(PaintDoc *d, int ch) {
    if (d->multi != PAINT_TEXT) return -1;
    if (ch == '\b') {
        if (d->ntext) d->ntext--;
    } else if ((ch == '\n' || (ch >= ' ' && ch <= '~')) && d->ntext < PAINT_TEXT_MAX) {
        d->text[d->ntext++] = (char)ch;
    }
    d->text[d->ntext] = 0;
    redraw(d, 0);
    return 0;
}

int paint_text_caret(const PaintDoc *d, int *x, int *y, int *h) {
    if (d->multi != PAINT_TEXT) return -1;
    int s = text_scale(d), col = 0, row = 0;
    for (int i = 0; i < d->ntext; i++) {
        if (d->text[i] == '\n') col = 0, row++;
        else col++;
    }
    *x = d->tx + col * PAINT_GLYPH_W * s;
    *y = d->ty + row * PAINT_GLYPH_H * s;
    *h = 7 * s;
    return 0;
}

/* ---- commands ---- */

int paint__tools_command(PaintDoc *d, const char *w, size_t n, const char *rest, int *rc, char *msg, size_t cap) {
    int text = paint__is(w, n, "text"), poly = paint__is(w, n, "polygon"), curve = paint__is(w, n, "curve");
    if (!text && !poly && !curve) return 0;
    int v[64], nv = 0, right = 0;
    const char *p = rest, *a;
    size_t k;
    *rc = -1;
    while ((k = paint__word(&p, &a))) {
        if (paint__is(a, k, "right") && !nv) right = 1;
        else if (nv < 64 && paint__number(a, k, &v[nv]) == 0) nv++;
        else {
            paint__say(msg, cap, text ? "text X Y WORDS" : poly ? "polygon X Y X Y X Y ..." : "curve X0 Y0 X1 Y1 [BX BY [BX BY]]",
                       ": not a number where one goes");
            return 1;
        }
        if (text && nv == 2) { /* the words: the rest of the line, as it is */
            while (*p == ' ' || *p == '\t') p++;
            break;
        }
    }
    if ((text && (nv != 2 || !*p)) || (poly && (nv < 6 || nv % 2)) || (curve && nv != 4 && nv != 6 && nv != 8)) {
        paint__say(msg, cap,
                   text ? "text X Y WORDS" : poly ? "polygon X Y X Y X Y ...: three corners or more" : "curve X0 Y0 X1 Y1 [BX BY [BX BY]]",
                   "");
        return 1;
    }
    if (d->floating) paint_drop(d);
    paint__save_before(d);
    uint32_t c = ink(d, right ? 2 : 1);
    if (text) {
        paint_text(d, v[0], v[1], p, strlen(p), text_scale(d), c);
    } else if (poly) {
        int pts[32][2];
        for (int i = 0; i < nv / 2; i++) pts[i][0] = v[2 * i], pts[i][1] = v[2 * i + 1];
        paint_polygon(d, (const int (*)[2])pts, nv / 2, d->size, d->filled, c);
    } else {
        int b0x = nv >= 6 ? v[4] : v[0], b0y = nv >= 6 ? v[5] : v[1];
        int b1x = nv == 8 ? v[6] : nv == 6 ? v[4] : v[2], b1y = nv == 8 ? v[7] : nv == 6 ? v[5] : v[3];
        paint_curve(d, v[0], v[1], v[2], v[3], b0x, b0y, b1x, b1y, d->size, c);
    }
    if (d->dirty.w) paint__push_step(d, d->dirty);
    *rc = 0;
    return 1;
}
