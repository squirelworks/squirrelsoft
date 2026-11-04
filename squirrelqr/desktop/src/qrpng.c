/* qrpng: PNG reader producing 8-bit grayscale, with its own zlib inflater.
 * Original code, written from RFC 1950 (zlib), RFC 1951 (DEFLATE) and the
 * PNG specification (ISO/IEC 15948). No libpng or zlib, so the desktop reader
 * opens screenshots the same way on Linux, macOS and Windows.
 *
 * All colour types and bit depths, PLTE, tRNS and Adam7 interlacing are
 * supported. Colour is turned into BT.601 luma; transparency is composited
 * over white, since a QR code saved with a transparent background is meant to
 * be shown on a light page. CRCs are not checked: a damaged file shows up as
 * a bad zlib stream or wrong sizes, and the QR decoder copes with noise. */
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

/* ---------------------------------------------------------------- inflate ---- */

typedef struct {
    const uint8_t *in;
    size_t len, pos;  /* byte position */
    uint32_t bits;    /* bit buffer, LSB first */
    int nbits;
    uint8_t *out;
    size_t cap, olen;
} Inflate;

/* Canonical Huffman code as counts per length and symbols sorted by code. */
typedef struct {
    uint16_t count[16];
    uint16_t sym[288];
} Huff;

static int need(Inflate *s, int n) {
    while (s->nbits < n) {
        if (s->pos >= s->len) return -1;
        s->bits |= (uint32_t)s->in[s->pos++] << s->nbits;
        s->nbits += 8;
    }
    return 0;
}

static int getbits(Inflate *s, int n, unsigned *v) {
    if (n == 0) {
        *v = 0;
        return 0;
    }
    if (need(s, n)) return -1;
    *v = s->bits & ((1u << n) - 1);
    s->bits >>= n;
    s->nbits -= n;
    return 0;
}

/* Build from code lengths; an incomplete code is allowed only for the
 * one-code distance tree case (RFC 1951 3.2.7), over-subscribed never is. */
static int huff_build(Huff *h, const uint8_t *lens, int n) {
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[lens[i]]++;
    h->count[0] = 0;
    int left = 1;
    for (int l = 1; l < 16; l++) {
        left <<= 1;
        left -= h->count[l];
        if (left < 0) return -1;
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = (uint16_t)(offs[l] + h->count[l]);
    for (int i = 0; i < n; i++)
        if (lens[i]) h->sym[offs[lens[i]]++] = (uint16_t)i;
    return 0;
}

/* Codes are sent MSB first, so walk one bit at a time. */
static int huff_decode(Inflate *s, const Huff *h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) {
        unsigned b;
        if (getbits(s, 1, &b)) return -1;
        code |= (int)b;
        int count = h->count[l];
        if (code - first < count) return h->sym[index + code - first];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

static int put(Inflate *s, uint8_t b) {
    if (s->olen >= s->cap) return -1;
    s->out[s->olen++] = b;
    return 0;
}

static const uint16_t LEN_BASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DIST_BASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                       193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                       6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int codes(Inflate *s, const Huff *lit, const Huff *dist) {
    for (;;) {
        int sym = huff_decode(s, lit);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (put(s, (uint8_t)sym)) return -1;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            unsigned e, d;
            if (getbits(s, LEN_EXTRA[sym], &e)) return -1;
            size_t len = LEN_BASE[sym] + e;
            int ds = huff_decode(s, dist);
            if (ds < 0 || ds >= 30) return -1;
            if (getbits(s, DIST_EXTRA[ds], &d)) return -1;
            size_t back = DIST_BASE[ds] + d;
            if (back > s->olen || len > s->cap - s->olen) return -1;
            for (size_t i = 0; i < len; i++, s->olen++) s->out[s->olen] = s->out[s->olen - back];
        }
    }
}

static int block_stored(Inflate *s) {
    s->bits = 0; /* drop to the byte boundary; whole bytes were never buffered past it */
    s->nbits = 0;
    if (s->len - s->pos < 4) return -1;
    unsigned len = s->in[s->pos] | (unsigned)s->in[s->pos + 1] << 8;
    unsigned nlen = s->in[s->pos + 2] | (unsigned)s->in[s->pos + 3] << 8;
    s->pos += 4;
    if ((len ^ 0xFFFF) != nlen || len > s->len - s->pos || len > s->cap - s->olen) return -1;
    memcpy(s->out + s->olen, s->in + s->pos, len);
    s->pos += len;
    s->olen += len;
    return 0;
}

static int block_fixed(Inflate *s) {
    static Huff lit, dist;
    static int ready;
    if (!ready) {
        uint8_t l[288];
        int i = 0;
        for (; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        huff_build(&lit, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        huff_build(&dist, l, 30);
        ready = 1;
    }
    return codes(s, &lit, &dist);
}

static int block_dynamic(Inflate *s) {
    static const uint8_t ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    unsigned hlit, hdist, hclen;
    if (getbits(s, 5, &hlit) || getbits(s, 5, &hdist) || getbits(s, 4, &hclen)) return -1;
    hlit += 257;
    hdist += 1;
    hclen += 4;
    if (hlit > 286 || hdist > 30) return -1;
    uint8_t lens[320] = {0};
    for (unsigned i = 0; i < hclen; i++) {
        unsigned v;
        if (getbits(s, 3, &v)) return -1;
        lens[ORDER[i]] = (uint8_t)v;
    }
    Huff cl, lit, dist;
    if (huff_build(&cl, lens, 19)) return -1;
    memset(lens, 0, sizeof lens);
    for (unsigned i = 0; i < hlit + hdist;) {
        int sym = huff_decode(s, &cl);
        if (sym < 0) return -1;
        if (sym < 16) {
            lens[i++] = (uint8_t)sym;
            continue;
        }
        unsigned rep, v = 0;
        if (sym == 16) {
            if (i == 0 || getbits(s, 2, &rep)) return -1;
            v = lens[i - 1];
            rep += 3;
        } else if (sym == 17) {
            if (getbits(s, 3, &rep)) return -1;
            rep += 3;
        } else {
            if (getbits(s, 7, &rep)) return -1;
            rep += 11;
        }
        if (i + rep > hlit + hdist) return -1;
        while (rep--) lens[i++] = (uint8_t)v;
    }
    if (lens[256] == 0) return -1; /* no end-of-block code */
    if (huff_build(&lit, lens, (int)hlit) || huff_build(&dist, lens + hlit, (int)hdist)) return -1;
    return codes(s, &lit, &dist);
}

int qr_inflate_zlib(const uint8_t *in, size_t len, uint8_t *out, size_t cap, size_t *olen) {
    if (len < 2 || (in[0] & 0x0F) != 8 || (in[0] >> 4) > 7 || ((unsigned)in[0] << 8 | in[1]) % 31 || (in[1] & 0x20))
        return -1; /* deflate, window <= 32K, header check, no preset dictionary */
    Inflate s = {in, len, 2, 0, 0, out, cap, 0};
    unsigned last, type;
    do {
        if (getbits(&s, 1, &last) || getbits(&s, 2, &type)) return -1;
        int rc = type == 0 ? block_stored(&s) : type == 1 ? block_fixed(&s) : type == 2 ? block_dynamic(&s) : -1;
        if (rc) return -1;
    } while (!last);
    *olen = s.olen;
    return 0; /* the Adler-32 trailer is not checked, like the chunk CRCs */
}

/* -------------------------------------------------------------------- PNG ---- */

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

typedef struct {
    uint32_t w, h;
    int depth, ctype, interlace;
    int channels, bpp; /* bpp: bytes per complete pixel, at least 1 (for filtering) */
    uint8_t pal[256][4];
    int npal;
    int has_key;          /* tRNS for gray / RGB: one transparent colour */
    unsigned key[3];
} Png;

static size_t row_bytes(const Png *p, uint32_t w) { return ((size_t)w * p->channels * p->depth + 7) / 8; }

static int paeth(int a, int b, int c) {
    int pp = a + b - c, pa = abs(pp - a), pb = abs(pp - b), pc = abs(pp - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* Undo the filters of one (sub)image in place; rows are 1 + rb bytes. */
static int unfilter(uint8_t *d, uint32_t h, size_t rb, int bpp) {
    uint8_t *prev = NULL;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = d + y * (rb + 1), f = row[0], *x = row + 1;
        for (size_t i = 0; i < rb; i++) {
            int a = i >= (size_t)bpp ? x[i - bpp] : 0, b = prev ? prev[i] : 0, c = prev && i >= (size_t)bpp ? prev[i - bpp] : 0;
            switch (f) {
            case 0: break;
            case 1: x[i] = (uint8_t)(x[i] + a); break;
            case 2: x[i] = (uint8_t)(x[i] + b); break;
            case 3: x[i] = (uint8_t)(x[i] + ((a + b) >> 1)); break;
            case 4: x[i] = (uint8_t)(x[i] + paeth(a, b, c)); break;
            default: return -1;
            }
        }
        prev = x;
    }
    return 0;
}

static unsigned sample(const uint8_t *row, size_t i, int depth) {
    if (depth == 8) return row[i];
    if (depth == 16) return (unsigned)row[2 * i] << 8 | row[2 * i + 1];
    size_t bit = i * (size_t)depth;
    return (row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
}

static uint8_t over_white(unsigned v, unsigned a) { return (uint8_t)((v * a + 255u * (255u - a) + 127u) / 255u); }

/* Gray value of pixel x of an unfiltered row. */
static uint8_t pixel(const Png *p, const uint8_t *row, uint32_t x) {
    size_t c = (size_t)x * p->channels;
    unsigned maxv = (1u << p->depth) - 1;
#define TO8(v) (p->depth == 16 ? (v) >> 8 : (v) * 255u / maxv)
    switch (p->ctype) {
    case 0: {
        unsigned v = sample(row, c, p->depth);
        if (p->has_key && v == p->key[0]) return 255;
        return (uint8_t)TO8(v);
    }
    case 2: {
        unsigned r = sample(row, c, p->depth), g = sample(row, c + 1, p->depth), b = sample(row, c + 2, p->depth);
        if (p->has_key && r == p->key[0] && g == p->key[1] && b == p->key[2]) return 255;
        return qr_luma(TO8(r), TO8(g), TO8(b));
    }
    case 3: {
        unsigned i = sample(row, c, p->depth);
        if ((int)i >= p->npal) return 255; /* out of range: treat as background */
        const uint8_t *e = p->pal[i];
        return over_white(qr_luma(e[0], e[1], e[2]), e[3]);
    }
    case 4: return over_white(TO8(sample(row, c, p->depth)), TO8(sample(row, c + 1, p->depth)));
    default: {
        unsigned r = sample(row, c, p->depth), g = sample(row, c + 1, p->depth), b = sample(row, c + 2, p->depth);
        return over_white(qr_luma(TO8(r), TO8(g), TO8(b)), TO8(sample(row, c + 3, p->depth)));
    }
    }
#undef TO8
}

int qr_png_decode(const uint8_t *buf, size_t len, QrImage *img, char *err, size_t n) {
    static const int CHANNELS[7] = {1, 0, 3, 1, 2, 0, 4};
    Png p;
    memset(&p, 0, sizeof p);
    if (len < 8 + 25 || memcmp(buf, "\x89PNG\r\n\x1a\n", 8)) return fail(err, n, "not a PNG file");
    /* Gather IDAT data; check the header first. */
    size_t pos = 8, zlen = 0;
    uint8_t *z = NULL;
    int seen_hdr = 0, seen_end = 0;
    while (pos + 12 <= len) {
        uint32_t clen = be32(buf + pos);
        const uint8_t *type = buf + pos + 4, *d = buf + pos + 8;
        if (clen > len - pos - 12) break;
        if (!seen_hdr) {
            if (memcmp(type, "IHDR", 4) || clen != 13) break;
            p.w = be32(d);
            p.h = be32(d + 4);
            p.depth = d[8];
            p.ctype = d[9];
            p.interlace = d[12];
            int ok_depth = p.ctype == 0   ? (p.depth == 1 || p.depth == 2 || p.depth == 4 || p.depth == 8 || p.depth == 16)
                           : p.ctype == 3 ? (p.depth == 1 || p.depth == 2 || p.depth == 4 || p.depth == 8)
                           : (p.ctype == 2 || p.ctype == 4 || p.ctype == 6) && (p.depth == 8 || p.depth == 16);
            if (!ok_depth || d[10] || d[11] || p.interlace > 1) {
                free(z);
                return fail(err, n, "unsupported PNG (colour type %d, depth %d)", p.ctype, p.depth);
            }
            if (p.w < 1 || p.h < 1 || p.w > QR_IMAGE_MAX_SIDE || p.h > QR_IMAGE_MAX_SIDE ||
                (uint64_t)p.w * p.h > QR_IMAGE_MAX_PIXELS) {
                free(z);
                return fail(err, n, "unsupported image size %lux%lu", (unsigned long)p.w, (unsigned long)p.h);
            }
            p.channels = CHANNELS[p.ctype];
            p.bpp = (p.channels * p.depth + 7) / 8;
            seen_hdr = 1;
        } else if (!memcmp(type, "PLTE", 4)) {
            if (clen % 3 || clen > 768) break;
            p.npal = (int)(clen / 3);
            for (int i = 0; i < p.npal; i++) {
                memcpy(p.pal[i], d + 3 * i, 3);
                p.pal[i][3] = 255;
            }
        } else if (!memcmp(type, "tRNS", 4)) {
            if (p.ctype == 3) {
                for (uint32_t i = 0; i < clen && i < 256; i++) p.pal[i][3] = d[i];
            } else if ((p.ctype == 0 && clen >= 2) || (p.ctype == 2 && clen >= 6)) {
                p.has_key = 1;
                for (int i = 0; i < (p.ctype == 2 ? 3 : 1); i++) p.key[i] = (unsigned)d[2 * i] << 8 | d[2 * i + 1];
            }
        } else if (!memcmp(type, "IDAT", 4)) {
            uint8_t *nz = realloc(z, zlen + clen + 1);
            if (!nz) {
                free(z);
                return fail(err, n, "out of memory");
            }
            z = nz;
            memcpy(z + zlen, d, clen);
            zlen += clen;
        } else if (!memcmp(type, "IEND", 4)) {
            seen_end = 1;
            break;
        } else if (!(type[0] & 0x20)) { /* unknown critical chunk */
            free(z);
            return fail(err, n, "unsupported PNG chunk %.4s", (const char *)type);
        }
        pos += 12 + clen;
    }
    if (!seen_hdr || !zlen || (p.ctype == 3 && !p.npal)) {
        free(z);
        return fail(err, n, "damaged PNG file");
    }
    (void)seen_end; /* a missing IEND is tolerated when the image data is complete */

    /* Raw size: one filter byte per row of each pass. */
    static const uint8_t X0[7] = {0, 4, 0, 2, 0, 1, 0}, Y0[7] = {0, 0, 4, 0, 2, 0, 1};
    static const uint8_t DX[7] = {8, 8, 4, 4, 2, 2, 1}, DY[7] = {8, 8, 8, 4, 4, 2, 2};
    int passes = p.interlace ? 7 : 1;
    uint32_t pw[7], ph[7];
    size_t raw = 0;
    for (int k = 0; k < passes; k++) {
        pw[k] = p.interlace ? (p.w - X0[k] + DX[k] - 1) / DX[k] * (p.w > X0[k]) : p.w;
        ph[k] = p.interlace ? (p.h - Y0[k] + DY[k] - 1) / DY[k] * (p.h > Y0[k]) : p.h;
        if (pw[k] && ph[k]) raw += (row_bytes(&p, pw[k]) + 1) * ph[k];
    }
    uint8_t *d = malloc(raw ? raw : 1);
    size_t got = 0;
    if (!d) {
        free(z);
        return fail(err, n, "out of memory");
    }
    int rc = qr_inflate_zlib(z, zlen, d, raw, &got);
    free(z);
    if (rc || got != raw) {
        free(d);
        return fail(err, n, "damaged PNG image data");
    }
    img->gray = malloc((size_t)p.w * p.h);
    if (!img->gray) {
        free(d);
        return fail(err, n, "out of memory");
    }
    img->w = (int)p.w;
    img->h = (int)p.h;
    uint8_t *sub = d;
    for (int k = 0; k < passes; k++) {
        if (!pw[k] || !ph[k]) continue;
        size_t rb = row_bytes(&p, pw[k]);
        if (unfilter(sub, ph[k], rb, p.bpp)) {
            free(d);
            qr_image_free(img);
            return fail(err, n, "damaged PNG image data (bad filter)");
        }
        for (uint32_t y = 0; y < ph[k]; y++) {
            const uint8_t *row = sub + y * (rb + 1) + 1;
            uint32_t oy = p.interlace ? Y0[k] + y * DY[k] : y;
            for (uint32_t x = 0; x < pw[k]; x++) {
                uint32_t ox = p.interlace ? X0[k] + x * DX[k] : x;
                img->gray[(size_t)oy * p.w + ox] = pixel(&p, row, x);
            }
        }
        sub += (rb + 1) * ph[k];
    }
    free(d);
    return 0;
}
