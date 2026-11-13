/* paint: the selection, the clipboard and the transforms (see paint.h).
 * Original code. */
#include "paint_int.h"

#include <string.h>

static int inside(PaintRect r, int x, int y) { return r.w > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

/* r cut to the picture */
static PaintRect on_picture(const PaintDoc *d, PaintRect r) {
    int x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
    int x1 = r.x + r.w > d->w ? d->w : r.x + r.w, y1 = r.y + r.h > d->h ? d->h : r.y + r.h;
    if (r.w <= 0 || r.h <= 0 || x1 <= x0 || y1 <= y0) return (PaintRect){0, 0, 0, 0};
    return (PaintRect){x0, y0, x1 - x0, y1 - y0};
}

uint32_t paint_shown(const PaintDoc *d, int x, int y) {
    if (d->floating && inside(d->sel, x, y)) return d->flt[(size_t)(y - d->sel.y) * (size_t)d->sel.w + (size_t)(x - d->sel.x)];
    return paint_get(d, x, y);
}

static void fill_rect(PaintDoc *d, PaintRect r, uint32_t c) {
    for (int y = r.y; y < r.y + r.h; y++) paint__span(d, r.x, r.x + r.w - 1, y, c);
}

/* ---- selecting ---- */

static PaintRect corners(int x0, int y0, int x1, int y1) {
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y0 > y1) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    return (PaintRect){x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

int paint_select(PaintDoc *d, int x0, int y0, int x1, int y1) {
    if (d->floating) paint_drop(d);
    d->sel = on_picture(d, corners(x0, y0, x1, y1));
    return d->sel.w ? 0 : -1;
}

void paint_select_all(PaintDoc *d) { paint_select(d, 0, 0, d->w - 1, d->h - 1); }

void paint_select_none(PaintDoc *d) {
    if (d->floating) paint_drop(d);
    d->sel = (PaintRect){0, 0, 0, 0};
}

int paint_lift(PaintDoc *d) {
    if (!d->sel.w) return -1;
    if (d->floating) return 0;
    paint__save_before(d);
    PaintRect r = d->sel;
    for (int j = 0; j < r.h; j++)
        memcpy(d->flt + (size_t)j * (size_t)r.w, d->px + (size_t)(r.y + j) * (size_t)d->w + (size_t)r.x,
               (size_t)r.w * sizeof d->flt[0]);
    fill_rect(d, r, d->bg);
    d->from = r;
    d->floating = 1;
    return 0;
}

int paint_move_selection(PaintDoc *d, int dx, int dy) {
    if (paint_lift(d)) return -1;
    d->sel.x += dx;
    d->sel.y += dy;
    return 0;
}

void paint_drop(PaintDoc *d) {
    if (!d->floating) return;
    PaintRect r = d->sel, on = on_picture(d, r);
    for (int y = on.y; y < on.y + on.h; y++)
        memcpy(d->px + (size_t)y * (size_t)d->w + (size_t)on.x,
               d->flt + (size_t)(y - r.y) * (size_t)r.w + (size_t)(on.x - r.x), (size_t)on.w * sizeof d->px[0]);
    paint__dirty_reset(d);
    if (d->from.w) paint__touch(d, d->from.x, d->from.y, d->from.x + d->from.w - 1, d->from.y + d->from.h - 1);
    if (on.w) paint__touch(d, on.x, on.y, on.x + on.w - 1, on.y + on.h - 1);
    if (d->dirty.w) paint__push_step(d, d->dirty);
    d->floating = 0;
    d->from.w = 0;
    d->sel = on;
}

/* ---- the clipboard ---- */

int paint_copy(PaintDoc *d) {
    PaintRect r = d->sel;
    if (!r.w) return -1;
    for (int j = 0; j < r.h; j++) {
        const uint32_t *src = d->floating ? d->flt + (size_t)j * (size_t)r.w
                                          : d->px + (size_t)(r.y + j) * (size_t)d->w + (size_t)r.x;
        memcpy(d->clip + (size_t)j * (size_t)r.w, src, (size_t)r.w * sizeof d->clip[0]);
    }
    d->clip_w = r.w;
    d->clip_h = r.h;
    return 0;
}

int paint_delete(PaintDoc *d) {
    if (!d->sel.w) return -1;
    if (d->floating) { /* what floats goes; the hole it left stays */
        d->floating = 0;
        if (d->from.w) paint__push_step(d, d->from);
        d->from.w = 0;
    } else {
        paint__save_before(d);
        fill_rect(d, d->sel, d->bg);
        paint__push_step(d, d->sel);
    }
    d->sel = (PaintRect){0, 0, 0, 0};
    return 0;
}

int paint_cut(PaintDoc *d) { return paint_copy(d) ? -1 : paint_delete(d); }

int paint_paste(PaintDoc *d, int x, int y) {
    if (!d->clip_w) return -1;
    if (d->floating) paint_drop(d);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= d->w) x = d->w - 1;
    if (y >= d->h) y = d->h - 1;
    paint__save_before(d);
    memcpy(d->flt, d->clip, (size_t)d->clip_w * (size_t)d->clip_h * sizeof d->flt[0]);
    d->sel = (PaintRect){x, y, d->clip_w, d->clip_h};
    d->from = (PaintRect){0, 0, 0, 0};
    d->floating = 1;
    return 0;
}

/* ---- the select tool ---- */

void paint__sel_press(PaintDoc *d, int x, int y) {
    if (inside(d->sel, x, y)) { /* take hold of it */
        paint_lift(d);
        d->sel_mode = 2;
        d->grab_x = x - d->sel.x;
        d->grab_y = y - d->sel.y;
        return;
    }
    if (d->floating) paint_drop(d);
    d->sel_mode = 1;
    d->x0 = x;
    d->y0 = y;
    d->sel = on_picture(d, corners(x, y, x, y));
}

void paint__sel_drag(PaintDoc *d, int x, int y) {
    if (d->sel_mode == 1) {
        d->sel = on_picture(d, corners(d->x0, d->y0, x, y));
    } else if (d->sel_mode == 2) {
        d->sel.x = x - d->grab_x;
        d->sel.y = y - d->grab_y;
    }
}

void paint__sel_release(PaintDoc *d, int x, int y) {
    int mode = d->sel_mode;
    paint__sel_drag(d, x, y);
    d->sel_mode = 0;
    if (mode == 1 && x == d->x0 && y == d->y0) d->sel = (PaintRect){0, 0, 0, 0}; /* a click: nothing selected */
}

/* ---- transforms ---- */

/* src (sw x sh) into dst; dst must not be src */
static void flip_px(const uint32_t *src, int w, int h, int vertical, uint32_t *dst) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            dst[(size_t)y * (size_t)w + (size_t)x] =
                vertical ? src[(size_t)(h - 1 - y) * (size_t)w + (size_t)x] : src[(size_t)y * (size_t)w + (size_t)(w - 1 - x)];
}

/* q quarter turns clockwise (0..3); the result is h x w for odd q */
static void rotate_px(const uint32_t *src, int w, int h, int q, uint32_t *dst) {
    int dw = q & 1 ? h : w;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int nx, ny;
            if (q == 1) nx = h - 1 - y, ny = x;
            else if (q == 2) nx = w - 1 - x, ny = h - 1 - y;
            else if (q == 3) nx = y, ny = w - 1 - x;
            else nx = x, ny = y;
            dst[(size_t)ny * (size_t)dw + (size_t)nx] = src[(size_t)y * (size_t)w + (size_t)x];
        }
}

static uint32_t mix(uint32_t a, uint32_t b, int t) { /* t in 0..256 of the way from a to b */
    uint32_t out = 0xFF000000u;
    for (int s = 0; s < 24; s += 8) {
        int ca = (int)(a >> s & 0xFF), cb = (int)(b >> s & 0xFF);
        out |= (uint32_t)((ca * (256 - t) + cb * t + 128) >> 8) << s;
    }
    return out;
}

static void resize_px(const uint32_t *src, int sw, int sh, uint32_t *dst, int dw, int dh, int smooth) {
    for (int y = 0; y < dh; y++) {
        /* the source position of this pixel's middle, in 1/256ths */
        long fy = smooth ? ((long)(2 * y + 1) * sh * 128) / dh - 128 : 0;
        int y0 = smooth ? (int)(fy < 0 ? 0 : fy >> 8) : (int)(((long)(2 * y + 1) * sh) / (2 * dh));
        int ty = smooth ? (int)(fy < 0 ? 0 : fy & 255) : 0, y1 = y0 + 1 < sh ? y0 + 1 : y0;
        for (int x = 0; x < dw; x++) {
            if (!smooth) {
                int sx = (int)(((long)(2 * x + 1) * sw) / (2 * dw));
                dst[(size_t)y * (size_t)dw + (size_t)x] = src[(size_t)y0 * (size_t)sw + (size_t)sx];
                continue;
            }
            long fx = ((long)(2 * x + 1) * sw * 128) / dw - 128;
            int x0 = (int)(fx < 0 ? 0 : fx >> 8), tx = (int)(fx < 0 ? 0 : fx & 255), x1 = x0 + 1 < sw ? x0 + 1 : x0;
            uint32_t top = mix(src[(size_t)y0 * (size_t)sw + (size_t)x0], src[(size_t)y0 * (size_t)sw + (size_t)x1], tx);
            uint32_t bot = mix(src[(size_t)y1 * (size_t)sw + (size_t)x0], src[(size_t)y1 * (size_t)sw + (size_t)x1], tx);
            dst[(size_t)y * (size_t)dw + (size_t)x] = mix(top, bot, ty);
        }
    }
}

enum { T_FLIP_H, T_FLIP_V, T_ROTATE, T_RESIZE, T_INVERT };

/* one transform: on the floating selection (lifted first) when there is a
 * selection, else on the whole picture as one step */
static int transform(PaintDoc *d, int kind, int q, int nw, int nh, int smooth) {
    int sel = d->sel.w > 0;
    int w = sel ? d->sel.w : d->w, h = sel ? d->sel.h : d->h;
    int dw = w, dh = h;
    if (kind == T_ROTATE && (q & 1)) dw = h, dh = w;
    if (kind == T_RESIZE) dw = nw, dh = nh;
    if (dw < 1 || dh < 1 || dw > PAINT_MAX_W || dh > PAINT_MAX_H || (size_t)dw * (size_t)dh > (size_t)PAINT_MAX_W * PAINT_MAX_H)
        return -1;
    const uint32_t *src;
    uint32_t *dst;
    if (sel) {
        paint_lift(d);
        memcpy(d->todo, d->flt, (size_t)w * (size_t)h * sizeof d->todo[0]);
        src = d->todo;
        dst = d->flt;
    } else {
        paint__save_before(d);
        src = d->before;
        dst = d->px;
    }
    switch (kind) {
    case T_FLIP_H:
    case T_FLIP_V: flip_px(src, w, h, kind == T_FLIP_V, dst); break;
    case T_ROTATE: rotate_px(src, w, h, q, dst); break;
    case T_RESIZE: resize_px(src, w, h, dst, dw, dh, smooth); break;
    default:
        for (size_t i = 0; i < (size_t)w * (size_t)h; i++) dst[i] = (src[i] ^ 0x00FFFFFFu) | 0xFF000000u;
    }
    if (sel) {
        d->sel.w = dw;
        d->sel.h = dh;
        return 0;
    }
    d->w = dw;
    d->h = dh;
    if (dw == w && dh == h) paint__push_step(d, (PaintRect){0, 0, w, h});
    else paint__push_whole(d, w, h);
    return 0;
}

int paint_flip(PaintDoc *d, int vertical) { return transform(d, vertical ? T_FLIP_V : T_FLIP_H, 0, 0, 0, 0); }

int paint_rotate(PaintDoc *d, int quarters) {
    int q = ((quarters % 4) + 4) % 4;
    return q ? transform(d, T_ROTATE, q, 0, 0, 0) : 0;
}

int paint_resize(PaintDoc *d, int w, int h, int smooth) { return transform(d, T_RESIZE, 0, w, h, smooth); }

int paint_invert(PaintDoc *d) { return transform(d, T_INVERT, 0, 0, 0, 0); }

int paint_crop(PaintDoc *d) {
    if (d->floating) paint_drop(d);
    PaintRect r = d->sel;
    if (!r.w) return -1;
    int ow = d->w, oh = d->h;
    paint__save_before(d);
    for (int j = 0; j < r.h; j++)
        memcpy(d->px + (size_t)j * (size_t)r.w, d->before + (size_t)(r.y + j) * (size_t)ow + (size_t)r.x,
               (size_t)r.w * sizeof d->px[0]);
    d->w = r.w;
    d->h = r.h;
    paint__push_whole(d, ow, oh);
    d->sel = (PaintRect){0, 0, 0, 0};
    return 0;
}

int paint_canvas(PaintDoc *d, int w, int h) {
    if (w < 1 || h < 1 || w > PAINT_MAX_W || h > PAINT_MAX_H) return -1;
    if (d->floating) paint_drop(d);
    int ow = d->w, oh = d->h;
    paint__save_before(d);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            d->px[(size_t)y * (size_t)w + (size_t)x] = x < ow && y < oh ? d->before[(size_t)y * (size_t)ow + (size_t)x] : d->bg;
    d->w = w;
    d->h = h;
    if (w == ow && h == oh) return 0; /* nothing changed */
    paint__push_whole(d, ow, oh);
    d->sel = (PaintRect){0, 0, 0, 0};
    return 0;
}

/* ---- commands ---- */

int paint__sel_command(PaintDoc *d, const char *word, size_t n, const char *rest, int *rc, char *msg, size_t cap) {
    static const char *const words[] = {"select", "copy",   "cut",  "paste",  "delete", "move",  "drop",
                                        "flip",   "rotate", "resize", "crop", "canvas", "invert"};
    int k = -1;
    for (int i = 0; i < (int)(sizeof words / sizeof words[0]); i++)
        if (paint__is(word, n, words[i])) k = i;
    if (k < 0) return 0;
    int v[8], nv = 0, all = 0, none = 0, h = 0, vert = 0, smooth = 0, other = 0;
    const char *w;
    size_t m;
    while ((m = paint__word(&rest, &w))) {
        if (paint__is(w, m, "all")) all = 1;
        else if (paint__is(w, m, "none")) none = 1;
        else if (paint__is(w, m, "h")) h = 1;
        else if (paint__is(w, m, "v")) vert = 1;
        else if (paint__is(w, m, "smooth")) smooth = 1;
        else if (nv < 8 && paint__number(w, m, &v[nv]) == 0) nv++;
        else other = 1;
    }
    int words_used = all + none + h + vert + smooth;
    *rc = -1;
    const char *use = 0;
    switch (k) {
    case 0: /* select */
        if (all && nv == 0 && words_used == 1 && !other) paint_select_all(d), *rc = 0;
        else if (none && nv == 0 && words_used == 1 && !other) paint_select_none(d), *rc = 0;
        else if (nv == 4 && !words_used && !other) {
            *rc = paint_select(d, v[0], v[1], v[2], v[3]);
            if (*rc) use = "select: the rectangle is off the picture";
        } else use = "select X0 Y0 X1 Y1, select all or select none";
        break;
    case 1: case 2: case 4: case 6: case 10: /* copy, cut, delete, drop, crop */
        if (nv || words_used || other) {
            use = k == 1 ? "copy takes nothing after it" : k == 2 ? "cut takes nothing after it"
                : k == 4 ? "delete takes nothing after it" : k == 6 ? "drop takes nothing after it" : "crop takes nothing after it";
            break;
        }
        if (k == 6) {
            paint_drop(d);
            *rc = 0;
            break;
        }
        *rc = k == 1 ? paint_copy(d) : k == 2 ? paint_cut(d) : k == 4 ? paint_delete(d) : paint_crop(d);
        if (*rc) use = "nothing is selected";
        break;
    case 3: /* paste */
        if ((nv != 0 && nv != 2) || words_used || other) use = "paste, or paste X Y";
        else if ((*rc = paint_paste(d, nv ? v[0] : 0, nv ? v[1] : 0))) use = "nothing to paste";
        break;
    case 5: /* move */
        if (nv != 2 || words_used || other) use = "move DX DY";
        else if ((*rc = paint_move_selection(d, v[0], v[1]))) use = "nothing is selected";
        break;
    case 7: /* flip */
        if (nv || h + vert != 1 || words_used != 1 || other) use = "flip h or flip v";
        else *rc = paint_flip(d, vert);
        break;
    case 8: /* rotate */
        if (nv != 1 || words_used || other || (v[0] != 90 && v[0] != 180 && v[0] != 270 && v[0] != -90))
            use = "rotate 90, 180, 270 or -90";
        else *rc = paint_rotate(d, v[0] / 90);
        break;
    case 9: /* resize */
    case 11: /* canvas */
        if (nv != 2 || other || (k == 11 ? words_used : words_used != smooth)) {
            use = k == 9 ? "resize W H [smooth]" : "canvas W H";
            break;
        }
        *rc = k == 9 ? paint_resize(d, v[0], v[1], smooth) : paint_canvas(d, v[0], v[1]);
        if (*rc) use = "the size is out of range";
        break;
    default: /* invert */
        if (nv || words_used || other) use = "invert takes nothing after it";
        else *rc = paint_invert(d);
    }
    if (*rc) paint__say(msg, cap, use ? use : "that did not work", "");
    return 1;
}
