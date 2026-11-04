/* qrimage: Netpbm and BMP readers producing 8-bit grayscale. Original code,
 * written from the Netpbm format pages and the BMP header layout. */
#include "qrimage.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *err, size_t n, const char *fmt, ...) {
    if (err && n) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, n, fmt, ap);
        va_end(ap);
    }
    return -1;
}

static int alloc_image(QrImage *img, long w, long h, char *err, size_t n) {
    if (w < 1 || h < 1 || w > QR_IMAGE_MAX_SIDE || h > QR_IMAGE_MAX_SIDE ||
        (unsigned long)w * (unsigned long)h > QR_IMAGE_MAX_PIXELS)
        return fail(err, n, "unsupported image size %ldx%ld", w, h);
    img->gray = malloc((size_t)w * (size_t)h);
    if (!img->gray) return fail(err, n, "out of memory");
    img->w = (int)w;
    img->h = (int)h;
    return 0;
}

/* ---- Netpbm ---- */

typedef struct {
    const uint8_t *p, *end;
} Cursor;

static long pnm_int(Cursor *c) { /* next ASCII integer, skipping whitespace and comments */
    while (c->p < c->end) {
        if (*c->p == '#') {
            while (c->p < c->end && *c->p != '\n') c->p++;
        } else if (*c->p == ' ' || *c->p == '\t' || *c->p == '\r' || *c->p == '\n' ||
                   *c->p == '\v' || *c->p == '\f') {
            c->p++;
        } else {
            break;
        }
    }
    if (c->p >= c->end || *c->p < '0' || *c->p > '9') return -1;
    long v = 0;
    while (c->p < c->end && *c->p >= '0' && *c->p <= '9') {
        v = v * 10 + (*c->p++ - '0');
        if (v > 1L << 24) return -1;
    }
    return v;
}

static int pnm_bit(Cursor *c) { /* P1 samples may be packed without spaces */
    while (c->p < c->end) {
        uint8_t ch = *c->p++;
        if (ch == '0') return 0;
        if (ch == '1') return 1;
        if (ch == '#') {
            while (c->p < c->end && *c->p != '\n') c->p++;
        } else if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n' && ch != '\v' && ch != '\f') {
            return -1;
        }
    }
    return -1;
}

static int decode_pnm(const uint8_t *buf, size_t len, QrImage *img, char *err, size_t n) {
    Cursor c = {buf + 2, buf + len};
    int kind = buf[1] - '0';
    long w = pnm_int(&c), h = pnm_int(&c), maxv = 1;
    if (w < 0 || h < 0) return fail(err, n, "bad Netpbm header");
    if (kind != 1 && kind != 4) {
        maxv = pnm_int(&c);
        if (maxv < 1 || maxv > 65535) return fail(err, n, "bad Netpbm maxval");
    }
    if (alloc_image(img, w, h, err, n)) return -1;
    size_t px = (size_t)w * (size_t)h;
    int chans = (kind == 3 || kind == 6) ? 3 : 1;
    if (kind >= 4) {
        if (c.p >= c.end) goto short_data;
        c.p++; /* the single whitespace byte before the raster */
    }
    size_t avail = (size_t)(c.end - c.p);
    if (kind == 4) {
        size_t row = ((size_t)w + 7) / 8;
        if (avail < row * (size_t)h) goto short_data;
        for (long y = 0; y < h; y++)
            for (long x = 0; x < w; x++)
                img->gray[y * w + x] = (c.p[y * row + x / 8] >> (7 - x % 8)) & 1 ? 0 : 255;
        return 0;
    }
    if (kind == 1) {
        for (size_t i = 0; i < px; i++) {
            int b = pnm_bit(&c);
            if (b < 0) goto short_data;
            img->gray[i] = b ? 0 : 255;
        }
        return 0;
    }
    int wide = maxv > 255;
    if (kind >= 5 && avail < px * (size_t)chans * (wide ? 2u : 1u)) goto short_data;
    for (size_t i = 0; i < px; i++) {
        unsigned v[3];
        for (int k = 0; k < chans; k++) {
            long s;
            if (kind <= 3) {
                s = pnm_int(&c);
                if (s < 0) goto short_data;
                if (s > maxv) s = maxv;
            } else if (wide) {
                s = (long)c.p[0] << 8 | c.p[1];
                c.p += 2;
            } else {
                s = *c.p++;
            }
            v[k] = (unsigned)((s * 255 + maxv / 2) / maxv);
        }
        img->gray[i] = chans == 3 ? qr_luma(v[0], v[1], v[2]) : (uint8_t)v[0];
    }
    return 0;
short_data:
    qr_image_free(img);
    return fail(err, n, "Netpbm data is truncated or malformed");
}

/* ---- BMP ---- */

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

typedef struct {
    int shift, bits;
} Field;

static Field field_of(uint32_t mask) {
    Field f = {0, 0};
    if (!mask) return f;
    while (!(mask & 1)) {
        mask >>= 1;
        f.shift++;
    }
    while (mask & 1) {
        mask >>= 1;
        f.bits++;
    }
    return f;
}

static unsigned field_get(uint32_t px, Field f) {
    if (!f.bits) return 0;
    uint32_t v = (px >> f.shift) & ((f.bits >= 32) ? 0xFFFFFFFFu : ((1u << f.bits) - 1));
    if (f.bits >= 8) return (unsigned)(v >> (f.bits - 8));
    uint32_t maxv = (1u << f.bits) - 1;
    return (unsigned)((v * 255 + maxv / 2) / maxv);
}

static int decode_bmp(const uint8_t *buf, size_t len, QrImage *img, char *err, size_t n) {
    if (len < 26) return fail(err, n, "BMP header is truncated");
    uint32_t off = le32(buf + 10), hsz = le32(buf + 14);
    long w, h;
    unsigned bpp, comp = 0, ncolors = 0;
    if (hsz == 12) { /* BITMAPCOREHEADER */
        w = (long)le16(buf + 18);
        h = (long)(int16_t)le16(buf + 20);
        bpp = le16(buf + 24);
    } else if (hsz >= 40 && len >= 14 + 40) {
        w = (long)(int32_t)le32(buf + 18);
        h = (long)(int32_t)le32(buf + 22);
        bpp = le16(buf + 28);
        comp = le32(buf + 30);
        ncolors = le32(buf + 46);
    } else {
        return fail(err, n, "unsupported BMP header (size %u)", (unsigned)hsz);
    }
    int topdown = h < 0;
    if (topdown) h = -h;
    if (comp != 0 && comp != 3) return fail(err, n, "compressed BMP (type %u) is not supported", comp);
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
        return fail(err, n, "unsupported BMP depth %u", bpp);

    uint8_t pal[256];
    if (bpp <= 8) {
        unsigned entry = hsz == 12 ? 3 : 4, max = 1u << bpp;
        if (!ncolors || ncolors > max) ncolors = max;
        size_t pal_at = 14 + hsz;
        if (pal_at + (size_t)ncolors * entry > len) return fail(err, n, "BMP palette is truncated");
        memset(pal, 0, sizeof pal);
        for (unsigned i = 0; i < ncolors; i++) {
            const uint8_t *e = buf + pal_at + i * entry; /* B, G, R */
            pal[i] = qr_luma(e[2], e[1], e[0]);
        }
    }
    Field fr, fg, fb;
    if (bpp == 16 || bpp == 32) {
        uint32_t mr, mg, mb;
        if (comp == 3) {
            size_t at = hsz >= 52 ? 14 + 40 : 14 + hsz; /* V2+ headers carry the masks inline */
            if (at + 12 > len) return fail(err, n, "BMP bit masks are truncated");
            mr = le32(buf + at);
            mg = le32(buf + at + 4);
            mb = le32(buf + at + 8);
        } else if (bpp == 16) {
            mr = 0x7C00;
            mg = 0x03E0;
            mb = 0x001F;
        } else {
            mr = 0xFF0000;
            mg = 0x00FF00;
            mb = 0x0000FF;
        }
        fr = field_of(mr);
        fg = field_of(mg);
        fb = field_of(mb);
    } else if (comp == 3) {
        return fail(err, n, "BI_BITFIELDS needs a 16 or 32-bit BMP");
    }
    if (alloc_image(img, w, h, err, n)) return -1;
    size_t row = (((size_t)w * bpp + 31) / 32) * 4;
    if (off > len || (size_t)(len - off) / row < (size_t)h) {
        qr_image_free(img);
        return fail(err, n, "BMP pixel data is truncated");
    }
    for (long y = 0; y < h; y++) {
        const uint8_t *src = buf + off + row * (size_t)(topdown ? y : h - 1 - y);
        uint8_t *dst = img->gray + y * w;
        for (long x = 0; x < w; x++) {
            switch (bpp) {
            case 1: dst[x] = pal[(src[x / 8] >> (7 - x % 8)) & 1]; break;
            case 4: dst[x] = pal[(src[x / 2] >> (x % 2 ? 0 : 4)) & 15]; break;
            case 8: dst[x] = pal[src[x]]; break;
            case 24: dst[x] = qr_luma(src[3 * x + 2], src[3 * x + 1], src[3 * x]); break;
            case 16: {
                uint32_t p = le16(src + 2 * x);
                dst[x] = qr_luma(field_get(p, fr), field_get(p, fg), field_get(p, fb));
                break;
            }
            default: {
                uint32_t p = le32(src + 4 * x);
                dst[x] = qr_luma(field_get(p, fr), field_get(p, fg), field_get(p, fb));
                break;
            }
            }
        }
    }
    return 0;
}

int qr_image_decode(const uint8_t *buf, size_t len, QrImage *img, char *err, size_t n) {
    img->w = img->h = 0;
    img->gray = NULL;
    if (len >= 3 && buf[0] == 'P' && buf[1] >= '1' && buf[1] <= '6') return decode_pnm(buf, len, img, err, n);
    if (len >= 2 && buf[0] == 'B' && buf[1] == 'M') return decode_bmp(buf, len, img, err, n);
    if (len >= 8 && memcmp(buf, "\x89PNG", 4) == 0) return qr_png_decode(buf, len, img, err, n);
    return fail(err, n, "unknown image format (expected PNG, PGM, PPM, PBM or BMP)");
}

int qr_image_load(const char *path, QrImage *img, char *err, size_t n) {
    img->w = img->h = 0;
    img->gray = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return fail(err, n, "cannot open");
    size_t cap = 1 << 16, len = 0;
    uint8_t *buf = malloc(cap);
    for (;;) {
        if (!buf) {
            fclose(f);
            return fail(err, n, "out of memory");
        }
        len += fread(buf + len, 1, cap - len, f);
        if (len < cap) break;
        if (cap >= (size_t)QR_IMAGE_MAX_PIXELS * 4 + 4096) {
            free(buf);
            fclose(f);
            return fail(err, n, "file is too large");
        }
        uint8_t *nb = realloc(buf, cap * 2);
        if (!nb) free(buf);
        buf = nb;
        cap *= 2;
    }
    int bad = ferror(f);
    fclose(f);
    if (bad) {
        free(buf);
        return fail(err, n, "read error");
    }
    int rc = qr_image_decode(buf, len, img, err, n);
    free(buf);
    return rc;
}

int qr_image_save_pgm(const char *path, const QrImage *img) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P5\n%d %d\n255\n", img->w, img->h);
    size_t px = (size_t)img->w * (size_t)img->h;
    int ok = fwrite(img->gray, 1, px, f) == px;
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

void qr_image_free(QrImage *img) {
    free(img->gray);
    img->gray = NULL;
    img->w = img->h = 0;
}
