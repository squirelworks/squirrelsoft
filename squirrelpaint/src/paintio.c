/* paintio: BMP and PPM in and out (see paintio.h). Original code, written
 * from the file layouts. */
#include "paintio.h"

#include <string.h>

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

int paint_read_image(const uint8_t *data, size_t n, uint32_t *px, size_t cap, int *w, int *h, char *msg,
                     size_t msgcap) {
    say(msg, msgcap, "");
    if (n >= 2 && data[0] == 'B' && data[1] == 'M') return read_bmp(data, n, px, cap, w, h, msg, msgcap);
    if (n >= 2 && data[0] == 'P' && (data[1] == '2' || data[1] == '3' || data[1] == '5' || data[1] == '6'))
        return read_pnm(data, n, px, cap, w, h, msg, msgcap);
    if (n >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
        say(msg, msgcap, "a PNG, which this does not read yet: BMP or PPM");
    else if (n >= 3 && data[0] == 0xFF && data[1] == 0xD8)
        say(msg, msgcap, "a JPEG, which this does not read: BMP or PPM");
    else
        say(msg, msgcap, "not a picture this reads: BMP or PPM");
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
    }
    return PAINT_FILE_BMP;
}
