/* paintio: PNG, BMP and PPM in and out (see paintio.h). Original code, written
 * from the file layouts. */
#include "paintio.h"

#include <string.h>

#include "paint.h"
#include "paintz.h"

static void say(char *msg, size_t cap, const char *s) {
    if (!cap) return;
    size_t n = 0;
    while (s[n] && n + 1 < cap) {
        msg[n] = s[n];
        n++;
    }
    msg[n] = 0;
}

static uint32_t u16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t u32(const uint8_t *p) { return u16(p) | u16(p + 2) << 16; }

/* ---- BMP ---- */

/* a channel by its mask, scaled to 0..255 */
static uint32_t channel(uint32_t v, uint32_t mask) {
    if (!mask) return 0;
    int shift = 0, bits = 0;
    while (!(mask >> shift & 1)) shift++;
    while (shift + bits < 32 && (mask >> (shift + bits) & 1)) bits++;
    uint32_t x = (v & mask) >> shift, top = bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1;
    return top ? (uint32_t)(((uint64_t)x * 255 + top / 2) / top) : 0;
}

static int read_bmp(const uint8_t *d, size_t n, uint32_t *px, size_t cap, int *pw, int *ph, char *msg, size_t mc) {
    if (n < 26) {
        say(msg, mc, "a BMP cut short");
        return -1;
    }
    uint32_t off = u32(d + 10), hs = u32(d + 14);
    int32_t w, h;
    uint32_t bpp, comp = 0, used = 0;
    int core = hs == 12; /* the OS/2 1.x header: 16-bit sizes, 3-byte palette entries */
    if (core) {
        w = (int32_t)u16(d + 18);
        h = (int32_t)u16(d + 20);
        bpp = u16(d + 24);
    } else if (hs == 40 || hs == 52 || hs == 56 || hs == 64 || hs == 108 || hs == 124) {
        if (n < 14 + (size_t)hs) {
            say(msg, mc, "a BMP cut short");
            return -1;
        }
        w = (int32_t)u32(d + 18);
        h = (int32_t)u32(d + 22);
        bpp = u16(d + 28);
        comp = u32(d + 30);
        used = u32(d + 46);
    } else {
        say(msg, mc, "a BMP with a header this does not know");
        return -1;
    }
    int topdown = h < 0;
    if (topdown) h = -h;
    if (w <= 0 || h <= 0 || w > 1 << 20 || h > 1 << 20) {
        say(msg, mc, "a BMP of no size");
        return -1;
    }
    if ((size_t)w * (size_t)h > cap) {
        say(msg, mc, "the picture is bigger than SquirrelPaint takes");
        return -1;
    }
    uint32_t rm = 0, gm = 0, bm = 0;
    if (comp == 3 || comp == 6) { /* masks: in the header, or after a 40-byte one */
        if (bpp != 16 && bpp != 32) {
            say(msg, mc, "a BMP with masks on a depth that has none");
            return -1;
        }
        size_t at = 14 + 40;
        if (n < at + 12) {
            say(msg, mc, "a BMP cut short");
            return -1;
        }
        rm = u32(d + at);
        gm = u32(d + at + 4);
        bm = u32(d + at + 8);
    } else if (comp == 0) {
        if (bpp == 16) rm = 0x7C00, gm = 0x03E0, bm = 0x001F;
        if (bpp == 32) rm = 0xFF0000, gm = 0xFF00, bm = 0xFF;
    } else {
        say(msg, mc, "a compressed BMP (RLE, JPEG or PNG inside), which this does not read");
        return -1;
    }
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) {
        say(msg, mc, "a BMP of a depth this does not read");
        return -1;
    }
    uint32_t pal[256];
    if (bpp <= 8) {
        size_t es = core ? 3 : 4, at = 14 + hs, max = (size_t)1 << bpp;
        size_t k = used && used <= max ? used : max;
        if (at + k * es > n) {
            k = at < n ? (n - at) / es : 0; /* a short palette: what is there */
            if (!k) {
                say(msg, mc, "a BMP without its palette");
                return -1;
            }
        }
        for (size_t i = 0; i < 256; i++) pal[i] = 0xFF000000u;
        for (size_t i = 0; i < k; i++) {
            const uint8_t *e = d + at + i * es;
            pal[i] = 0xFF000000u | (uint32_t)e[2] << 16 | (uint32_t)e[1] << 8 | e[0];
        }
    }
    size_t stride = ((size_t)w * bpp + 31) / 32 * 4;
    if (off > n || (n - off) / stride < (size_t)h) {
        say(msg, mc, "a BMP cut short");
        return -1;
    }
    for (int y = 0; y < h; y++) {
        const uint8_t *row = d + off + (size_t)(topdown ? y : h - 1 - y) * stride;
        uint32_t *o = px + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            uint32_t c;
            switch (bpp) {
            case 1: c = pal[row[x >> 3] >> (7 - (x & 7)) & 1]; break;
            case 4: c = pal[row[x >> 1] >> (x & 1 ? 0 : 4) & 15]; break;
            case 8: c = pal[row[x]]; break;
            case 24:
                c = 0xFF000000u | (uint32_t)row[3 * x + 2] << 16 | (uint32_t)row[3 * x + 1] << 8 | row[3 * x];
                break;
            default: {
                uint32_t v = bpp == 16 ? u16(row + 2 * x) : u32(row + 4 * x);
                c = 0xFF000000u | channel(v, rm) << 16 | channel(v, gm) << 8 | channel(v, bm);
            }
            }
            o[x] = c;
        }
    }
    *pw = w;
    *ph = h;
    return 0;
}

/* ---- netpbm ---- */

/* the next number in a PNM header, past spaces and # comments; -1 if none */
static long pnm_number(const uint8_t *d, size_t n, size_t *at) {
    size_t i = *at;
    for (;;) {
        while (i < n && (d[i] == ' ' || d[i] == '\t' || d[i] == '\r' || d[i] == '\n')) i++;
        if (i < n && d[i] == '#') {
            while (i < n && d[i] != '\n') i++;
            continue;
        }
        break;
    }
    if (i >= n || d[i] < '0' || d[i] > '9') return -1;
    long v = 0;
    while (i < n && d[i] >= '0' && d[i] <= '9') {
        v = v * 10 + (d[i++] - '0');
        if (v > 1L << 24) return -1;
    }
    *at = i;
    return v;
}

static int read_pnm(const uint8_t *d, size_t n, uint32_t *px, size_t cap, int *pw, int *ph, char *msg, size_t mc) {
    int kind = d[1] - '0'; /* 2 PGM text, 3 PPM text, 5 PGM, 6 PPM */
    size_t at = 2;
    long w = pnm_number(d, n, &at), h = pnm_number(d, n, &at), max = pnm_number(d, n, &at);
    if (w <= 0 || h <= 0 || max <= 0 || max > 65535 || w > 1 << 20 || h > 1 << 20) {
        say(msg, mc, "a PPM with a bad header");
        return -1;
    }
    if ((size_t)w * (size_t)h > cap) {
        say(msg, mc, "the picture is bigger than SquirrelPaint takes");
        return -1;
    }
    int chans = kind == 3 || kind == 6 ? 3 : 1;
    size_t count = (size_t)w * (size_t)h * (size_t)chans;
    if (kind >= 5) {
        if (at >= n) {
            say(msg, mc, "a PPM cut short");
            return -1;
        }
        at++; /* the one space after the header */
        size_t bytes = max > 255 ? 2 : 1;
        if ((n - at) / bytes < count) {
            say(msg, mc, "a PPM cut short");
            return -1;
        }
    }
    for (size_t p = 0; p < (size_t)w * (size_t)h; p++) {
        uint32_t ch[3];
        for (int c = 0; c < chans; c++) {
            long v;
            if (kind >= 5) {
                v = max > 255 ? (long)(d[at] << 8 | d[at + 1]) : d[at];
                at += max > 255 ? 2 : 1;
            } else if ((v = pnm_number(d, n, &at)) < 0) {
                say(msg, mc, "a PPM cut short");
                return -1;
            }
            if (v > max) v = max;
            ch[c] = (uint32_t)((v * 255 + max / 2) / max);
        }
        if (chans == 1) ch[1] = ch[2] = ch[0];
        px[p] = 0xFF000000u | ch[0] << 16 | ch[1] << 8 | ch[2];
    }
    *pw = (int)w;
    *ph = (int)h;
    return 0;
}

/* ---- PNG ---- */

/* the inflated rows of a PNG (or the filtered rows of one being written):
 * enough for the biggest picture the core holds at 16 bits a sample with
 * alpha, and a filter byte on every row of all seven interlace passes */
#define PNG_RAW_CAP ((size_t)8 * PAINT_MAX_W * PAINT_MAX_H + (size_t)8 * PAINT_MAX_H + 64)
static uint8_t png_raw[PNG_RAW_CAP];

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static int is_type(const uint8_t *p, const char *t) { return p[4] == t[0] && p[5] == t[1] && p[6] == t[2] && p[7] == t[3]; }

/* the IDAT chunks one after another, for the inflater */
typedef struct {
    const uint8_t *d;
    size_t n, at; /* at: the chunk after the one being read */
} PngIdat;

static size_t png_more(PaintZIn *in, const uint8_t **p) {
    PngIdat *s = in->ctx;
    while (s->at + 12 <= s->n && is_type(s->d + s->at, "IDAT")) { /* (the chunks were checked whole) */
        size_t len = be32(s->d + s->at);
        *p = s->d + s->at + 8;
        s->at += 12 + len;
        if (len) return len; /* an empty IDAT is passed over */
    }
    return 0;
}

static const uint8_t ADAM7[7][4] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                                    {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}}; /* x, y, step x, step y */

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* undo the filters on h rows of rb bytes (each after its filter byte) in
 * place; bpp is the bytes a pixel takes, at least 1 */
static int unfilter(uint8_t *r, size_t rb, size_t h, size_t bpp) {
    const uint8_t *up = 0;
    for (size_t y = 0; y < h; y++, r += rb + 1) {
        int f = r[0];
        uint8_t *x = r + 1;
        if (f > 4) return -1;
        for (size_t i = 0; i < rb; i++) {
            int a = i >= bpp ? x[i - bpp] : 0, b = up ? up[i] : 0, c = up && i >= bpp ? up[i - bpp] : 0;
            int add = f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) / 2 : f == 4 ? paeth(a, b, c) : 0;
            x[i] = (uint8_t)(x[i] + add);
        }
        up = x;
    }
    return 0;
}

/* sample i (of depth bits) of a row */
static unsigned sample(const uint8_t *row, size_t i, int depth) {
    if (depth == 16) return (unsigned)row[2 * i] << 8 | row[2 * i + 1];
    if (depth == 8) return row[i];
    size_t bit = i * (size_t)depth;
    return (unsigned)(row[bit / 8] >> (8 - depth - (int)(bit % 8))) & ((1u << depth) - 1);
}

static uint32_t over_white(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    uint32_t w = 255 * (255 - a);
    r = (r * a + w + 127) / 255;
    g = (g * a + w + 127) / 255;
    b = (b * a + w + 127) / 255;
    return 0xFF000000u | r << 16 | g << 8 | b;
}

static int read_png(const uint8_t *d, size_t n, uint32_t *px, size_t cap, int *pw, int *ph, char *msg, size_t mc) {
    static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    int sig = n >= 8;
    for (int i = 0; sig && i < 8; i++) sig = d[i] == SIG[i];
    if (!sig) {
        say(msg, mc, "a damaged PNG (its first bytes are wrong)");
        return -1;
    }
    /* the chunks: every one whole with a right CRC, IHDR first, IEND last */
    const uint8_t *ihdr = 0, *plte = 0, *trns = 0;
    size_t nplte = 0, ntrns = 0, idat = 0;
    int ended = 0, idat_done = 0;
    for (size_t at = 8; !ended;) {
        if (at + 12 > n || be32(d + at) > n - at - 12) {
            say(msg, mc, "a PNG cut short");
            return -1;
        }
        size_t len = be32(d + at);
        const uint8_t *c = d + at, *body = c + 8;
        if (paint_crc32(0, c + 4, len + 4) != be32(body + len)) {
            say(msg, mc, "a damaged PNG (a chunk's CRC is wrong)");
            return -1;
        }
        if (!ihdr && !is_type(c, "IHDR")) {
            say(msg, mc, "a damaged PNG (no IHDR first)");
            return -1;
        }
        if (idat && !is_type(c, "IDAT")) idat_done = 1;
        if (is_type(c, "IHDR")) {
            if (ihdr || len != 13) {
                say(msg, mc, "a damaged PNG (its IHDR)");
                return -1;
            }
            ihdr = body;
        } else if (is_type(c, "PLTE")) {
            if (len % 3 || len == 0 || len > 768 || idat) {
                say(msg, mc, "a damaged PNG (its palette)");
                return -1;
            }
            plte = body;
            nplte = len / 3;
        } else if (is_type(c, "tRNS")) {
            trns = body;
            ntrns = len;
        } else if (is_type(c, "IDAT")) {
            if (idat_done) {
                say(msg, mc, "a damaged PNG (its IDAT chunks are apart)");
                return -1;
            }
            if (!idat) idat = at;
        } else if (is_type(c, "IEND")) {
            ended = 1;
        } else if (!(c[4] & 0x20)) { /* a chunk a reader must know, and this one does not */
            say(msg, mc, "a PNG with a chunk this does not know");
            return -1;
        }
        at += 12 + len;
    }
    if (!idat) {
        say(msg, mc, "a damaged PNG (no picture data)");
        return -1;
    }
    uint32_t w = be32(ihdr), h = be32(ihdr + 4);
    int depth = ihdr[8], ct = ihdr[9], lace = ihdr[12];
    int ch = ct == 0 ? 1 : ct == 2 ? 3 : ct == 3 ? 1 : ct == 4 ? 2 : ct == 6 ? 4 : 0;
    int okdepth = ct == 0   ? depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16
                  : ct == 3 ? depth == 1 || depth == 2 || depth == 4 || depth == 8
                            : depth == 8 || depth == 16;
    if (!ch || !okdepth || ihdr[10] || ihdr[11] || lace > 1 || !w || !h || w > 0x7FFFFFFFu || h > 0x7FFFFFFFu) {
        say(msg, mc, "a PNG of a kind this does not read (or a damaged one)");
        return -1;
    }
    if (ct == 3 && !plte) {
        say(msg, mc, "a damaged PNG (a palette picture with no palette)");
        return -1;
    }
    if ((uint64_t)w * h > cap) {
        say(msg, mc, "the picture is bigger than SquirrelPaint takes");
        return -1;
    }
    /* how many bytes the rows take, all passes */
    size_t bits = (size_t)ch * (size_t)depth, bpp = bits < 8 ? 1 : bits / 8, need = 0;
    for (int p = lace ? 0 : 6; p < 7; p++) {
        size_t x0 = lace ? ADAM7[p][0] : 0, y0 = lace ? ADAM7[p][1] : 0, dx = lace ? ADAM7[p][2] : 1, dy = lace ? ADAM7[p][3] : 1;
        size_t cw = w > x0 ? (w - x0 + dx - 1) / dx : 0, chh = h > y0 ? (h - y0 + dy - 1) / dy : 0;
        if (cw && chh) need += chh * ((cw * bits + 7) / 8 + 1);
    }
    if (need > PNG_RAW_CAP) {
        say(msg, mc, "the picture is bigger than SquirrelPaint takes");
        return -1;
    }
    PngIdat s = {d, n, idat};
    PaintZIn in = {0, 0, png_more, &s};
    long got = paint_inflate(&in, png_raw, PNG_RAW_CAP);
    if (got < 0 || (size_t)got < need) {
        say(msg, mc, got == PAINTZ_SHORT || got >= 0 ? "a PNG cut short (its picture data stops early)"
                                                      : "a damaged PNG (its picture data does not unpack)");
        return -1;
    }
    /* the transparent colour, if one, in samples as they stand */
    int key = 0;
    unsigned kr = 0, kg = 0, kb = 0;
    if (trns && ct == 0 && ntrns >= 2) key = 1, kr = kg = kb = (unsigned)trns[0] << 8 | trns[1];
    if (trns && ct == 2 && ntrns >= 6) {
        key = 1;
        kr = (unsigned)trns[0] << 8 | trns[1];
        kg = (unsigned)trns[2] << 8 | trns[3];
        kb = (unsigned)trns[4] << 8 | trns[5];
    }
    unsigned top = (1u << depth) - 1;
    uint8_t *r = png_raw;
    for (int p = lace ? 0 : 6; p < 7; p++) {
        size_t x0 = lace ? ADAM7[p][0] : 0, y0 = lace ? ADAM7[p][1] : 0, dx = lace ? ADAM7[p][2] : 1, dy = lace ? ADAM7[p][3] : 1;
        size_t cw = w > x0 ? (w - x0 + dx - 1) / dx : 0, chh = h > y0 ? (h - y0 + dy - 1) / dy : 0;
        if (!cw || !chh) continue;
        size_t rb = (cw * bits + 7) / 8;
        if (unfilter(r, rb, chh, bpp)) {
            say(msg, mc, "a damaged PNG (a row has an unknown filter)");
            return -1;
        }
        for (size_t y = 0; y < chh; y++, r += rb + 1) {
            const uint8_t *row = r + 1;
            uint32_t *o = px + (y0 + y * dy) * w + x0;
            for (size_t x = 0; x < cw; x++, o += dx) {
                unsigned v[4];
                for (int k = 0; k < ch; k++) v[k] = sample(row, x * (size_t)ch + (size_t)k, depth);
                uint32_t R, G, B, A = 255;
                if (ct == 3) {
                    if (v[0] >= nplte) {
                        say(msg, mc, "a damaged PNG (a colour past the end of its palette)");
                        return -1;
                    }
                    R = plte[3 * v[0]], G = plte[3 * v[0] + 1], B = plte[3 * v[0] + 2];
                    if (trns && v[0] < ntrns) A = trns[v[0]];
                } else {
                    unsigned gr = v[0], gg = ch >= 3 ? v[1] : v[0], gb = ch >= 3 ? v[2] : v[0];
                    if (key && gr == kr && gg == kg && gb == kb) A = 0;
                    if (ct == 4) A = (uint32_t)((v[1] * 255u + top / 2) / top);
                    if (ct == 6) A = (uint32_t)((v[3] * 255u + top / 2) / top);
                    R = (uint32_t)((gr * 255u + top / 2) / top);
                    G = (uint32_t)((gg * 255u + top / 2) / top);
                    B = (uint32_t)((gb * 255u + top / 2) / top);
                }
                *o = A == 255 ? 0xFF000000u | R << 16 | G << 8 | B : over_white(R, G, B, A);
            }
        }
    }
    *pw = (int)w;
    *ph = (int)h;
    return 0;
}

int paint_read_image(const uint8_t *data, size_t n, uint32_t *px, size_t cap, int *w, int *h, char *msg,
                     size_t msgcap) {
    say(msg, msgcap, "");
    if (n >= 2 && data[0] == 'B' && data[1] == 'M') return read_bmp(data, n, px, cap, w, h, msg, msgcap);
    if (n >= 2 && data[0] == 'P' && (data[1] == '2' || data[1] == '3' || data[1] == '5' || data[1] == '6'))
        return read_pnm(data, n, px, cap, w, h, msg, msgcap);
    if (n >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
        return read_png(data, n, px, cap, w, h, msg, msgcap);
    if (n >= 3 && data[0] == 0xFF && data[1] == 0xD8)
        say(msg, msgcap, "a JPEG, which this does not read: PNG, BMP or PPM");
    else
        say(msg, msgcap, "not a picture this reads: PNG, BMP or PPM");
    return -1;
}

/* ---- writing ---- */

static void put16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v) {
    put16(p, v & 0xFFFF);
    put16(p + 2, v >> 16);
}
static void put32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* the picture as an 8-bit RGB PNG: each row filtered the way that leaves
 * the smallest bytes (counted as signed), then deflated into one IDAT */
static size_t write_png(const uint32_t *px, int w, int h, uint8_t *out, size_t cap) {
    size_t rb = (size_t)w * 3, raw = (rb + 1) * (size_t)h;
    if (raw > PNG_RAW_CAP) return 0;
    uint8_t *r = png_raw;
    for (int y = 0; y < h; y++) {
        uint8_t *row = r + (size_t)y * (rb + 1), *x = row + 1;
        const uint32_t *p = px + (size_t)y * (size_t)w;
        for (int i = 0; i < w; i++) {
            x[3 * i] = (uint8_t)(p[i] >> 16);
            x[3 * i + 1] = (uint8_t)(p[i] >> 8);
            x[3 * i + 2] = (uint8_t)p[i];
        }
    }
    /* filter from the last row up, so each row's row above is still plain */
    static uint8_t trial[5][3 * PAINT_MAX_W];
    if (rb > sizeof trial[0]) return 0;
    for (int y = h - 1; y >= 0; y--) {
        uint8_t *x = r + (size_t)y * (rb + 1) + 1;
        const uint8_t *up = y ? x - (rb + 1) : 0;
        int best = 0;
        unsigned long best_sum = 0;
        for (int f = 0; f < 5; f++) {
            unsigned long sum = 0;
            for (size_t i = 0; i < rb; i++) {
                int a = i >= 3 ? x[i - 3] : 0, b = up ? up[i] : 0, c = up && i >= 3 ? up[i - 3] : 0;
                int pred = f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) / 2 : f == 4 ? paeth(a, b, c) : 0;
                uint8_t v = (uint8_t)(x[i] - pred);
                trial[f][i] = v;
                sum += v < 128 ? v : 256u - v;
            }
            if (f == 0 || sum < best_sum) best = f, best_sum = sum;
        }
        x[-1] = (uint8_t)best;
        for (size_t i = 0; i < rb; i++) x[i] = trial[best][i];
    }
    static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    size_t head = 8 + 25 + 8, tail = 4 + 12;
    /* when it does not fit: at most what stored blocks would take (a block
     * ends every 16 K tokens or sooner, each costing a few bytes more) */
    size_t most = head + 6 + raw + 6 * (raw / 16000 + 2) + tail;
    if (cap < head + tail + 8) return most;
    size_t z = paint_deflate(r, raw, out + head, cap - head - tail);
    if (!z) return most > cap ? most : cap + 1;
    memcpy(out, SIG, 8);
    uint8_t *c = out + 8;
    put32be(c, 13);
    memcpy(c + 4, "IHDR", 4);
    put32be(c + 8, (uint32_t)w);
    put32be(c + 12, (uint32_t)h);
    c[16] = 8; /* bits a sample */
    c[17] = 2; /* RGB */
    c[18] = c[19] = c[20] = 0; /* deflate, the five filters, not interlaced */
    put32be(c + 21, paint_crc32(0, c + 4, 17));
    c = out + 33;
    put32be(c, (uint32_t)z);
    memcpy(c + 4, "IDAT", 4);
    put32be(c + 8 + z, paint_crc32(0, c + 4, z + 4));
    c += 12 + z;
    put32be(c, 0);
    memcpy(c + 4, "IEND", 4);
    put32be(c + 8, paint_crc32(0, c + 4, 4));
    return (size_t)(c + 12 - out);
}

/* v in decimal at p; returns the digits */
static size_t decimal(char *p, unsigned long v) {
    char t[24];
    size_t k = 0;
    do t[k++] = (char)('0' + v % 10);
    while (v /= 10);
    for (size_t i = 0; i < k; i++) p[i] = t[k - 1 - i];
    return k;
}

size_t paint_write_image(int kind, const uint32_t *px, int w, int h, uint8_t *out, size_t cap) {
    if (w <= 0 || h <= 0) return 0;
    if (kind == PAINT_FILE_PNG) return write_png(px, w, h, out, cap);
    if (kind == PAINT_FILE_PPM) {
        char head[64];
        size_t k = 0;
        head[k++] = 'P';
        head[k++] = '6';
        head[k++] = '\n';
        k += decimal(head + k, (unsigned long)w);
        head[k++] = ' ';
        k += decimal(head + k, (unsigned long)h);
        memcpy(head + k, "\n255\n", 5);
        k += 5;
        size_t need = k + (size_t)w * (size_t)h * 3;
        if (need > cap) return need;
        memcpy(out, head, k);
        uint8_t *o = out + k;
        for (size_t i = 0; i < (size_t)w * (size_t)h; i++) {
            *o++ = (uint8_t)(px[i] >> 16);
            *o++ = (uint8_t)(px[i] >> 8);
            *o++ = (uint8_t)px[i];
        }
        return need;
    }
    size_t stride = ((size_t)w * 3 + 3) / 4 * 4, body = stride * (size_t)h, need = 54 + body;
    if (need > cap) return need;
    memset(out, 0, 54);
    out[0] = 'B';
    out[1] = 'M';
    put32(out + 2, (uint32_t)need);
    put32(out + 10, 54);
    put32(out + 14, 40);
    put32(out + 18, (uint32_t)w);
    put32(out + 22, (uint32_t)h); /* bottom-up, as most readers expect */
    put16(out + 26, 1);
    put16(out + 28, 24);
    put32(out + 34, (uint32_t)body);
    put32(out + 38, 2835); /* 72 dots an inch */
    put32(out + 42, 2835);
    for (int y = 0; y < h; y++) {
        uint8_t *o = out + 54 + (size_t)(h - 1 - y) * stride;
        const uint32_t *p = px + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++) {
            *o++ = (uint8_t)p[x];
            *o++ = (uint8_t)(p[x] >> 8);
            *o++ = (uint8_t)(p[x] >> 16);
        }
        for (size_t k = (size_t)w * 3; k < stride; k++) *o++ = 0;
    }
    return need;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

int paint_kind_of_name(const char *name) {
    size_t n = strlen(name);
    if (n >= 4 && name[n - 4] == '.') {
        const char *e = name + n - 3;
        if ((lower(e[0]) == 'p' && lower(e[1]) == 'p' && lower(e[2]) == 'm') ||
            (lower(e[0]) == 'p' && lower(e[1]) == 'n' && lower(e[2]) == 'm'))
            return PAINT_FILE_PPM;
        if (lower(e[0]) == 'p' && lower(e[1]) == 'n' && lower(e[2]) == 'g') return PAINT_FILE_PNG;
    }
    return PAINT_FILE_BMP;
}
