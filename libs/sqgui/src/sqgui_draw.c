/* libs/sqgui: canvas, primitives, text and theme (original code). */
#include "sqgui.h"

#include <stdio.h>

#include "font5x7.inc"

const sqg_theme sqg_theme_warp = {
    .face = SQG_RGB(0xCC, 0xCC, 0xCC),
    .light = SQG_RGB(0xFF, 0xFF, 0xFF),
    .shadow = SQG_RGB(0x80, 0x80, 0x80),
    .dark = SQG_RGB(0x00, 0x00, 0x00),
    .text = SQG_RGB(0x00, 0x00, 0x00),
    .text_disabled = SQG_RGB(0x80, 0x80, 0x80),
    .window = SQG_RGB(0xFF, 0xFF, 0xFF),
    .window_text = SQG_RGB(0x00, 0x00, 0x00),
    .select = SQG_RGB(0x00, 0x00, 0x80),
    .select_text = SQG_RGB(0xFF, 0xFF, 0xFF),
    .accent = SQG_RGB(0x00, 0x80, 0x80),
};

sqg_rect sqg_rect_make(int x, int y, int w, int h)
{
    sqg_rect r = { x, y, w, h };
    return r;
}

sqg_rect sqg_rect_intersect(sqg_rect a, sqg_rect b)
{
    int x0 = a.x > b.x ? a.x : b.x;
    int y0 = a.y > b.y ? a.y : b.y;
    int x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    sqg_rect r = { x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0 };
    return r;
}

int sqg_rect_contains(sqg_rect r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

sqg_rect sqg_rect_inset(sqg_rect r, int n)
{
    r.x += n;
    r.y += n;
    r.w -= 2 * n;
    r.h -= 2 * n;
    if (r.w < 0)
        r.w = 0;
    if (r.h < 0)
        r.h = 0;
    return r;
}

void sqg_canvas_init(sqg_canvas *c, uint32_t *px, int w, int h, int stride)
{
    c->px = px;
    c->w = w;
    c->h = h;
    c->stride = stride;
    c->clip = sqg_rect_make(0, 0, w, h);
}

sqg_rect sqg_clip(sqg_canvas *c, sqg_rect r)
{
    sqg_rect old = c->clip;
    c->clip = sqg_rect_intersect(r, sqg_rect_make(0, 0, c->w, c->h));
    return old;
}

void sqg_clip_reset(sqg_canvas *c)
{
    c->clip = sqg_rect_make(0, 0, c->w, c->h);
}

void sqg_pixel(sqg_canvas *c, int x, int y, uint32_t argb)
{
    if (sqg_rect_contains(c->clip, x, y))
        c->px[(size_t)y * (size_t)c->stride + (size_t)x] = argb;
}

uint32_t sqg_get(const sqg_canvas *c, int x, int y)
{
    if (x < 0 || y < 0 || x >= c->w || y >= c->h)
        return 0;
    return c->px[(size_t)y * (size_t)c->stride + (size_t)x];
}

void sqg_fill(sqg_canvas *c, sqg_rect r, uint32_t argb)
{
    r = sqg_rect_intersect(r, c->clip);
    for (int y = r.y; y < r.y + r.h; y++) {
        uint32_t *p = c->px + (size_t)y * (size_t)c->stride + (size_t)r.x;
        for (int x = 0; x < r.w; x++)
            p[x] = argb;
    }
}

void sqg_hline(sqg_canvas *c, int x0, int x1, int y, uint32_t argb)
{
    if (x1 < x0) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    sqg_fill(c, sqg_rect_make(x0, y, x1 - x0 + 1, 1), argb);
}

void sqg_vline(sqg_canvas *c, int x, int y0, int y1, uint32_t argb)
{
    if (y1 < y0) {
        int t = y0;
        y0 = y1;
        y1 = t;
    }
    sqg_fill(c, sqg_rect_make(x, y0, 1, y1 - y0 + 1), argb);
}

void sqg_frame(sqg_canvas *c, sqg_rect r, uint32_t argb)
{
    if (r.w <= 0 || r.h <= 0)
        return;
    sqg_hline(c, r.x, r.x + r.w - 1, r.y, argb);
    sqg_hline(c, r.x, r.x + r.w - 1, r.y + r.h - 1, argb);
    sqg_vline(c, r.x, r.y, r.y + r.h - 1, argb);
    sqg_vline(c, r.x + r.w - 1, r.y, r.y + r.h - 1, argb);
}

/* Bresenham, both octant families */
void sqg_line(sqg_canvas *c, int x0, int y0, int x1, int y1, uint32_t argb)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        sqg_pixel(c, x0, y0, argb);
        if (x0 == x1 && y0 == y1)
            break;
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

void sqg_blend(sqg_canvas *c, sqg_rect r, uint32_t argb)
{
    uint32_t a = argb >> 24;
    if (a == 0)
        return;
    if (a == 255) {
        sqg_fill(c, r, argb);
        return;
    }
    r = sqg_rect_intersect(r, c->clip);
    uint32_t sr = (argb >> 16) & 0xFF, sg = (argb >> 8) & 0xFF, sb = argb & 0xFF;
    for (int y = r.y; y < r.y + r.h; y++) {
        uint32_t *p = c->px + (size_t)y * (size_t)c->stride + (size_t)r.x;
        for (int x = 0; x < r.w; x++) {
            uint32_t d = p[x];
            uint32_t dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
            dr = (sr * a + dr * (255 - a) + 127) / 255;
            dg = (sg * a + dg * (255 - a) + 127) / 255;
            db = (sb * a + db * (255 - a) + 127) / 255;
            p[x] = 0xFF000000u | dr << 16 | dg << 8 | db;
        }
    }
}

void sqg_blit(sqg_canvas *c, int x, int y, const uint32_t *src, int w, int h, int stride)
{
    sqg_rect r = sqg_rect_intersect(sqg_rect_make(x, y, w, h), c->clip);
    for (int yy = r.y; yy < r.y + r.h; yy++) {
        const uint32_t *s = src + (size_t)(yy - y) * (size_t)stride + (size_t)(r.x - x);
        uint32_t *d = c->px + (size_t)yy * (size_t)c->stride + (size_t)r.x;
        for (int xx = 0; xx < r.w; xx++)
            d[xx] = s[xx];
    }
}

void sqg_blit_scaled(sqg_canvas *c, sqg_rect dst, const uint32_t *src, int w, int h, int stride)
{
    if (dst.w <= 0 || dst.h <= 0 || w <= 0 || h <= 0)
        return;
    sqg_rect r = sqg_rect_intersect(dst, c->clip);
    for (int yy = r.y; yy < r.y + r.h; yy++) {
        int sy = (int)((long long)(yy - dst.y) * h / dst.h);
        const uint32_t *s = src + (size_t)sy * (size_t)stride;
        uint32_t *d = c->px + (size_t)yy * (size_t)c->stride;
        for (int xx = r.x; xx < r.x + r.w; xx++)
            d[xx] = s[(long long)(xx - dst.x) * w / dst.w];
    }
}

static const unsigned char *glyph(unsigned char ch)
{
    if (ch < 0x20 || ch > 0x7E)
        ch = '?';
    return sqg_font5x7[ch - 0x20];
}

int sqg_text(sqg_canvas *c, int x, int y, const char *s, uint32_t argb, int scale)
{
    if (scale < 1)
        scale = 1;
    int cx = x, widest = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '\n') {
            if (cx - x > widest)
                widest = cx - x;
            cx = x;
            y += SQG_GLYPH_H * scale;
            continue;
        }
        if ((*p & 0xC0) == 0x80)
            continue;                   /* UTF-8 continuation: one '?' per character */
        const unsigned char *g = glyph(*p >= 0x80 ? '?' : *p);
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g[row] & (0x10 >> col))
                    sqg_fill(c, sqg_rect_make(cx + col * scale, y + row * scale, scale, scale), argb);
        cx += SQG_GLYPH_W * scale;
    }
    if (cx - x > widest)
        widest = cx - x;
    return widest;
}

int sqg_text_width(const char *s, int scale)
{
    if (scale < 1)
        scale = 1;
    int n = 0, widest = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '\n') {
            if (n > widest)
                widest = n;
            n = 0;
        } else if ((*p & 0xC0) != 0x80) {
            n++;
        }
    }
    if (n > widest)
        widest = n;
    return widest * SQG_GLYPH_W * scale;
}

int sqg_text_height(const char *s, int scale)
{
    if (scale < 1)
        scale = 1;
    int lines = 1;
    for (; *s; s++)
        if (*s == '\n')
            lines++;
    return lines * SQG_GLYPH_H * scale;
}

void sqg_bevel(sqg_canvas *c, sqg_rect r, int sunken, int fill, const sqg_theme *t)
{
    if (r.w < 4 || r.h < 4)
        return;
    uint32_t tl1 = sunken ? t->shadow : t->light;
    uint32_t tl2 = sunken ? t->dark : t->face;
    uint32_t br1 = sunken ? t->light : t->dark;
    uint32_t br2 = sunken ? t->face : t->shadow;
    if (fill)
        sqg_fill(c, sqg_rect_inset(r, 2), t->face);
    int x1 = r.x + r.w - 1, y1 = r.y + r.h - 1;
    sqg_hline(c, r.x, x1 - 1, r.y, tl1);
    sqg_vline(c, r.x, r.y, y1 - 1, tl1);
    sqg_hline(c, r.x + 1, x1 - 2, r.y + 1, tl2);
    sqg_vline(c, r.x + 1, r.y + 1, y1 - 2, tl2);
    sqg_hline(c, r.x, x1, y1, br1);
    sqg_vline(c, x1, r.y, y1, br1);
    sqg_hline(c, r.x + 1, x1 - 1, y1 - 1, br2);
    sqg_vline(c, x1 - 1, r.y + 1, y1 - 1, br2);
}

int sqg_write_ppm(const sqg_canvas *c, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            uint32_t p = c->px[(size_t)y * (size_t)c->stride + (size_t)x];
            unsigned char rgb[3] = { (unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p };
            fwrite(rgb, 1, 3, f);
        }
    return fclose(f) == 0 ? 0 : -1;
}

uint32_t sqg_hash(const sqg_canvas *c)
{
    uint32_t h = 2166136261u;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < c->w; x++) {
            uint32_t p = c->px[(size_t)y * (size_t)c->stride + (size_t)x];
            for (int k = 16; k >= 0; k -= 8) {
                h ^= (p >> k) & 0xFF;
                h *= 16777619u;
            }
        }
    return h;
}
