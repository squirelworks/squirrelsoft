/* paintz: zlib and deflate, both ways, and CRC-32 (see paintz.h). Written
 * from RFC 1950, RFC 1951 and the PNG specification. Original code. */
#include "paintz.h"

#include <string.h>

/* ---- checksums ---- */

uint32_t paint_crc32(uint32_t crc, const uint8_t *p, size_t n) {
    static uint32_t table[256];
    static int ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = 1;
    }
    crc = ~crc;
    while (n--) crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

uint32_t paint_adler32(uint32_t adler, const uint8_t *p, size_t n) {
    uint32_t a = adler & 0xFFFF, b = adler >> 16;
    while (n) {
        size_t k = n < 5552 ? n : 5552; /* the most that cannot overflow before the modulo */
        n -= k;
        while (k--) {
            a += *p++;
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    return b << 16 | a;
}

/* ---- the tables both ways share ---- */

static const uint16_t LEN_BASE[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                      2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DIST_BASE[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                       193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                       6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
/* the order the code length code's lengths come in */
static const uint8_t CL_ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

/* ---- inflate ---- */

typedef struct {
    PaintZIn *in;
    uint32_t bits;
    int nbits;
    int err;
    uint8_t *out;
    size_t cap, len;
} Inflate;

typedef struct {
    uint16_t count[16]; /* codes of each length */
    uint16_t sym[288];  /* the symbols in code order */
} Huff;

static int next_byte(Inflate *s) {
    PaintZIn *in = s->in;
    while (!in->n) {
        if (!in->more || !(in->n = in->more(in, &in->p))) return -1;
    }
    in->n--;
    return *in->p++;
}

static uint32_t need(Inflate *s, int n) {
    while (s->nbits < n) {
        int b = next_byte(s);
        if (b < 0) {
            if (!s->err) s->err = PAINTZ_SHORT;
            return 0;
        }
        s->bits |= (uint32_t)b << s->nbits;
        s->nbits += 8;
    }
    uint32_t v = s->bits & ((1u << n) - 1);
    s->bits >>= n;
    s->nbits -= n;
    return v;
}

/* the canonical code for these lengths; -1 when the lengths ask for more
 * codes than there are (an incomplete code is fine: a missing code is an
 * error only if it comes) */
static int build(Huff *h, const uint8_t *len, int n) {
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int k = 1; k < 16; k++) {
        left <<= 1;
        left -= h->count[k];
        if (left < 0) return -1;
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int k = 1; k < 15; k++) offs[k + 1] = (uint16_t)(offs[k] + h->count[k]);
    for (int i = 0; i < n; i++)
        if (len[i]) h->sym[offs[len[i]]++] = (uint16_t)i;
    return 0;
}

static int decode(Inflate *s, const Huff *h) {
    int code = 0, first = 0, index = 0;
    for (int k = 1; k < 16; k++) {
        code |= (int)need(s, 1);
        if (s->err) return -1;
        int count = h->count[k];
        if (code - count < first) return h->sym[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->err = PAINTZ_BAD;
    return -1;
}

static int put(Inflate *s, uint8_t b) {
    if (s->len >= s->cap) {
        s->err = PAINTZ_FULL;
        return -1;
    }
    s->out[s->len++] = b;
    return 0;
}

static int codes(Inflate *s, const Huff *lit, const Huff *dist) {
    for (;;) {
        int sym = decode(s, lit);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (put(s, (uint8_t)sym)) return -1;
            continue;
        }
        if (sym == 256) return 0;
        sym -= 257;
        if (sym >= 29) {
            s->err = PAINTZ_BAD;
            return -1;
        }
        size_t len = LEN_BASE[sym] + need(s, LEN_EXTRA[sym]);
        int d = decode(s, dist);
        if (d < 0) return -1;
        if (d >= 30) {
            s->err = PAINTZ_BAD;
            return -1;
        }
        size_t back = DIST_BASE[d] + need(s, DIST_EXTRA[d]);
        if (s->err) return -1;
        if (back > s->len) {
            s->err = PAINTZ_BAD;
            return -1;
        }
        if (s->len + len > s->cap) {
            s->err = PAINTZ_FULL;
            return -1;
        }
        for (size_t i = 0; i < len; i++, s->len++) s->out[s->len] = s->out[s->len - back];
    }
}

static int block_stored(Inflate *s) {
    s->bits = 0; /* to the byte */
    s->nbits = 0;
    int a = next_byte(s), b = next_byte(s), c = next_byte(s), d = next_byte(s);
    if (d < 0) {
        s->err = PAINTZ_SHORT;
        return -1;
    }
    unsigned len = (unsigned)(a | b << 8), nlen = (unsigned)(c | d << 8);
    if (len != (~nlen & 0xFFFF)) {
        s->err = PAINTZ_BAD;
        return -1;
    }
    while (len--) {
        int v = next_byte(s);
        if (v < 0) {
            s->err = PAINTZ_SHORT;
            return -1;
        }
        if (put(s, (uint8_t)v)) return -1;
    }
    return 0;
}

static Huff fixed_lit, fixed_dist;

static int block_fixed(Inflate *s) {
    static int ready;
    if (!ready) {
        uint8_t len[288];
        for (int i = 0; i < 288; i++) len[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        build(&fixed_lit, len, 288);
        for (int i = 0; i < 30; i++) len[i] = 5;
        build(&fixed_dist, len, 30);
        ready = 1;
    }
    return codes(s, &fixed_lit, &fixed_dist);
}

static int block_dynamic(Inflate *s) {
    int nlen = (int)need(s, 5) + 257, ndist = (int)need(s, 5) + 1, ncode = (int)need(s, 4) + 4;
    if (s->err) return -1;
    if (nlen > 286 || ndist > 30) {
        s->err = PAINTZ_BAD;
        return -1;
    }
    uint8_t len[320];
    memset(len, 0, 19);
    for (int i = 0; i < ncode; i++) len[CL_ORDER[i]] = (uint8_t)need(s, 3);
    Huff cl, lit, dist;
    if (s->err) return -1;
    if (build(&cl, len, 19)) {
        s->err = PAINTZ_BAD;
        return -1;
    }
    for (int i = 0; i < nlen + ndist;) {
        int sym = decode(s, &cl);
        if (sym < 0) return -1;
        if (sym < 16) {
            len[i++] = (uint8_t)sym;
            continue;
        }
        int rep, v = 0;
        if (sym == 16) {
            if (!i) {
                s->err = PAINTZ_BAD;
                return -1;
            }
            v = len[i - 1];
            rep = 3 + (int)need(s, 2);
        } else if (sym == 17) {
            rep = 3 + (int)need(s, 3);
        } else {
            rep = 11 + (int)need(s, 7);
        }
        if (s->err) return -1;
        if (i + rep > nlen + ndist) {
            s->err = PAINTZ_BAD;
            return -1;
        }
        while (rep--) len[i++] = (uint8_t)v;
    }
    if (!len[256] || build(&lit, len, nlen) || build(&dist, len + nlen, ndist)) {
        s->err = PAINTZ_BAD;
        return -1;
    }
    return codes(s, &lit, &dist);
}

static long run(Inflate *s) {
    int last;
    do {
        last = (int)need(s, 1);
        int type = (int)need(s, 2);
        if (s->err) return s->err;
        int rc = type == 0 ? block_stored(s) : type == 1 ? block_fixed(s) : type == 2 ? block_dynamic(s) : -1;
        if (type == 3) s->err = PAINTZ_BAD;
        if (rc || s->err) return s->err ? s->err : PAINTZ_BAD;
    } while (!last);
    return (long)s->len;
}

long paint_inflate_raw(PaintZIn *in, uint8_t *out, size_t cap) {
    Inflate s = {in, 0, 0, 0, out, cap, 0};
    return run(&s);
}

long paint_inflate(PaintZIn *in, uint8_t *out, size_t cap) {
    Inflate s = {in, 0, 0, 0, out, cap, 0};
    int cmf = next_byte(&s), flg = next_byte(&s);
    if (flg < 0) return PAINTZ_SHORT;
    if ((cmf & 15) != 8 || (cmf >> 4) > 7 || (cmf * 256 + flg) % 31 || (flg & 0x20)) return PAINTZ_BAD;
    long n = run(&s);
    if (n < 0) return n;
    s.bits = 0; /* the checksum starts on a byte */
    s.nbits = 0;
    uint32_t want = 0;
    for (int i = 0; i < 4; i++) {
        int b = next_byte(&s);
        if (b < 0) return PAINTZ_SHORT;
        want = want << 8 | (uint32_t)b;
    }
    return paint_adler32(1, out, (size_t)n) == want ? n : PAINTZ_CHECK;
}

/* ---- deflate ---- */

typedef struct {
    uint8_t *out;
    size_t cap, len;
    uint32_t bits;
    int nbits;
    int full;
} Bits;

static void emit(Bits *w, uint32_t v, int n) { /* n <= 24 bits, the first bit lowest */
    w->bits |= v << w->nbits;
    w->nbits += n;
    while (w->nbits >= 8) {
        if (w->len < w->cap) w->out[w->len] = (uint8_t)w->bits;
        else w->full = 1;
        w->len++;
        w->bits >>= 8;
        w->nbits -= 8;
    }
}

static void emit_flush(Bits *w) {
    if (w->nbits) emit(w, 0, 8 - w->nbits);
}

/* a code of n bits, written first bit first as deflate wants Huffman codes */
static void emit_code(Bits *w, uint32_t code, int n) {
    uint32_t r = 0;
    for (int i = 0; i < n; i++) r |= ((code >> i) & 1) << (n - 1 - i);
    emit(w, r, n);
}

#define WBITS 15
#define WSIZE (1 << WBITS)
#define HBITS 15
#define BLOCK_TOKENS 16384
#define MAX_CHAIN 96
#define LAZY 32 /* a match this long is taken without looking one on */

static int32_t head[1 << HBITS];
static int32_t prev[WSIZE];
static uint32_t tokens[BLOCK_TOKENS]; /* a byte, or 1 << 31 | length << 16 | distance */

static unsigned hash3(const uint8_t *p) { return ((unsigned)p[0] << 10 ^ (unsigned)p[1] << 5 ^ p[2]) & ((1u << HBITS) - 1); }

static void insert(const uint8_t *in, size_t n, size_t i) {
    if (i + 3 > n) return;
    unsigned h = hash3(in + i);
    prev[i & (WSIZE - 1)] = head[h];
    head[h] = (int32_t)i;
}

/* the longest earlier match for position i: its length (0 if under 3) and distance */
static int longest(const uint8_t *in, size_t n, size_t i, int *dist) {
    if (i + 3 > n) return 0;
    size_t most = n - i < 258 ? n - i : 258;
    int best = 0;
    int32_t j = head[hash3(in + i)];
    for (int chain = 0; j >= 0 && chain < MAX_CHAIN; chain++) {
        size_t back = i - (size_t)j;
        if (back == 0 || back > WSIZE) break;
        if (in[(size_t)j + (size_t)best] == in[i + (size_t)best]) {
            size_t k = 0;
            while (k < most && in[(size_t)j + k] == in[i + k]) k++;
            if ((int)k > best) {
                best = (int)k;
                *dist = (int)back;
                if (k == most) break;
            }
        }
        int32_t nj = prev[j & (WSIZE - 1)];
        if (nj >= j) break; /* an old slot of the window, overwritten */
        j = nj;
    }
    return best >= 3 ? best : 0;
}

static int len_code(int len) {
    int c = 28;
    while (LEN_BASE[c] > len) c--;
    return c;
}
static int dist_code(int d) {
    int c = 29;
    while (DIST_BASE[c] > d) c--;
    return c;
}

/* code lengths of at most limit bits for these counts: Huffman's, with
 * the counts flattened until the deepest code fits */
static void lengths_for(const uint32_t *freq_in, int n, int limit, uint8_t *len) {
    uint32_t freq[288];
    int used = 0;
    for (int i = 0; i < n; i++) {
        freq[i] = freq_in[i];
        used += freq[i] > 0;
    }
    for (int i = 0; used < 2 && i < n; i++) /* a code needs two symbols to be complete */
        if (!freq[i]) freq[i] = 1, used++;
    for (;;) {
        /* leaves sorted by count (insertion sort: n is small), then two queues */
        int16_t leaf[288];
        int nl = 0;
        for (int i = 0; i < n; i++)
            if (freq[i]) {
                int k = nl++;
                while (k > 0 && freq[leaf[k - 1]] > freq[i]) {
                    leaf[k] = leaf[k - 1];
                    k--;
                }
                leaf[k] = (int16_t)i;
            }
        if (nl < 2) { /* (not reached: two symbols are always there) */
            memset(len, 0, (size_t)n);
            return;
        }
        uint32_t nf[576];
        int16_t parent[576];
        int nn = nl, a = 0, b = nl; /* nodes: the leaves, then the joins; a and b head the two queues */
        for (int i = 0; i < nl; i++) nf[i] = freq[leaf[i]];
        for (int m = 0; m < nl - 1; m++) { /* nl - 1 joins make the tree */
            int pick[2];
            for (int t = 0; t < 2; t++) pick[t] = a < nl && (b >= nn || nf[a] <= nf[b]) ? a++ : b++;
            nf[nn] = nf[pick[0]] + nf[pick[1]];
            parent[pick[0]] = parent[pick[1]] = (int16_t)nn;
            nn++;
        }
        uint8_t depth[576];
        depth[nn - 1] = 0;
        int deepest = 0;
        for (int i = nn - 2; i >= 0; i--) {
            depth[i] = (uint8_t)(depth[parent[i]] + 1);
            if (i < nl && depth[i] > deepest) deepest = depth[i];
        }
        if (deepest <= limit) {
            memset(len, 0, (size_t)n);
            for (int i = 0; i < nl; i++) len[leaf[i]] = depth[i];
            return;
        }
        for (int i = 0; i < n; i++)
            if (freq[i]) freq[i] = (freq[i] + 1) / 2;
    }
}

/* canonical codes for the lengths */
static void codes_for(const uint8_t *len, int n, uint16_t *code) {
    uint16_t count[16] = {0}, next[16];
    for (int i = 0; i < n; i++) count[len[i]]++;
    count[0] = 0;
    uint16_t c = 0;
    for (int k = 1; k < 16; k++) {
        c = (uint16_t)((c + count[k - 1]) << 1);
        next[k] = c;
    }
    for (int i = 0; i < n; i++)
        if (len[i]) code[i] = next[len[i]]++;
}

/* the run-length form of the code lengths: symbols 0..18 with their extra
 * bits in the high byte; returns how many */
static int rle_lengths(const uint8_t *len, int n, uint16_t *out) {
    int k = 0;
    for (int i = 0; i < n;) {
        int v = len[i], run = 1;
        while (i + run < n && len[i + run] == v) run++;
        if (v == 0 && run >= 3) {
            int r = run > 138 ? 138 : run;
            out[k++] = r >= 11 ? (uint16_t)(18 | (r - 11) << 8) : (uint16_t)(17 | (r - 3) << 8);
            i += r;
        } else if (v != 0 && run >= 4) {
            out[k++] = (uint16_t)v;
            int r = run - 1 > 6 ? 6 : run - 1;
            out[k++] = (uint16_t)(16 | (r - 3) << 8);
            i += 1 + r;
        } else {
            out[k++] = (uint16_t)v;
            i++;
        }
    }
    return k;
}

static const uint8_t CL_EXTRA[19] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 7};

/* the tokens as one block: whichever of stored, fixed or dynamic is shortest */
static void write_block(Bits *w, const uint8_t *raw, size_t rawn, int ntok, int last) {
    uint32_t lf[286] = {0}, df[30] = {0};
    for (int i = 0; i < ntok; i++) {
        uint32_t t = tokens[i];
        if (t >> 31) {
            lf[257 + len_code((int)(t >> 16 & 0x1FF))]++;
            df[dist_code((int)(t & 0xFFFF))]++;
        } else {
            lf[t]++;
        }
    }
    lf[256]++;
    /* the dynamic code and its header */
    uint8_t ll[286 + 30], *dl = ll + 286;
    lengths_for(lf, 286, 15, ll);
    lengths_for(df, 30, 15, dl);
    int nlit = 286, ndist = 30;
    while (nlit > 257 && !ll[nlit - 1]) nlit--;
    while (ndist > 1 && !dl[ndist - 1]) ndist--;
    uint8_t both[286 + 30];
    memcpy(both, ll, (size_t)nlit);
    memcpy(both + nlit, dl, (size_t)ndist);
    uint16_t rle[316];
    int nrle = rle_lengths(both, nlit + ndist, rle);
    uint32_t cf[19] = {0};
    for (int i = 0; i < nrle; i++) cf[rle[i] & 0xFF]++;
    uint8_t cl[19];
    lengths_for(cf, 19, 7, cl);
    int ncl = 19;
    while (ncl > 4 && !cl[CL_ORDER[ncl - 1]]) ncl--;
    /* what each costs, in bits */
    size_t extra = 0, dyn = 3 + 14 + 3 * (size_t)ncl, fix = 3;
    for (int i = 0; i < nrle; i++) dyn += cl[rle[i] & 0xFF] + CL_EXTRA[rle[i] & 0xFF];
    for (int i = 0; i < 286; i++) {
        dyn += (size_t)lf[i] * ll[i];
        fix += (size_t)lf[i] * (i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
        if (i >= 257) extra += (size_t)lf[i] * LEN_EXTRA[i - 257];
    }
    for (int i = 0; i < 30; i++) {
        dyn += (size_t)df[i] * dl[i];
        fix += (size_t)df[i] * 5;
        extra += (size_t)df[i] * DIST_EXTRA[i];
    }
    dyn += extra;
    fix += extra;
    size_t stored = (rawn / 65535 + 1) * 40 + rawn * 8 + 7;
    if (stored < dyn && stored < fix) {
        for (size_t at = 0;;) {
            size_t k = rawn - at > 65535 ? 65535 : rawn - at;
            int end = at + k == rawn;
            emit(w, (uint32_t)(last && end), 1);
            emit(w, 0, 2);
            emit_flush(w);
            emit(w, (uint32_t)k, 16);
            emit(w, (uint32_t)(~k & 0xFFFF), 16);
            for (size_t i = 0; i < k; i++) emit(w, raw[at + i], 8);
            at += k;
            if (end) return;
        }
    }
    uint16_t lc[288], dc[30];
    if (fix <= dyn) { /* the fixed code */
        uint8_t fl[288], fd[30];
        for (int i = 0; i < 288; i++) fl[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        for (int i = 0; i < 30; i++) fd[i] = 5;
        codes_for(fl, 288, lc);
        codes_for(fd, 30, dc);
        memcpy(ll, fl, 286);
        memcpy(dl, fd, 30);
        emit(w, (uint32_t)last, 1);
        emit(w, 1, 2);
    } else {
        uint16_t cc[19];
        codes_for(ll, 286, lc);
        codes_for(dl, 30, dc);
        codes_for(cl, 19, cc);
        emit(w, (uint32_t)last, 1);
        emit(w, 2, 2);
        emit(w, (uint32_t)(nlit - 257), 5);
        emit(w, (uint32_t)(ndist - 1), 5);
        emit(w, (uint32_t)(ncl - 4), 4);
        for (int i = 0; i < ncl; i++) emit(w, cl[CL_ORDER[i]], 3);
        for (int i = 0; i < nrle; i++) {
            int sym = rle[i] & 0xFF;
            emit_code(w, cc[sym], cl[sym]);
            if (CL_EXTRA[sym]) emit(w, (uint32_t)(rle[i] >> 8), CL_EXTRA[sym]);
        }
    }
    for (int i = 0; i < ntok; i++) {
        uint32_t t = tokens[i];
        if (!(t >> 31)) {
            emit_code(w, lc[t], ll[t]);
            continue;
        }
        int len = (int)(t >> 16 & 0x1FF), d = (int)(t & 0xFFFF);
        int c = len_code(len), e = dist_code(d);
        emit_code(w, lc[257 + c], ll[257 + c]);
        if (LEN_EXTRA[c]) emit(w, (uint32_t)(len - LEN_BASE[c]), LEN_EXTRA[c]);
        emit_code(w, dc[e], dl[e]);
        if (DIST_EXTRA[e]) emit(w, (uint32_t)(d - DIST_BASE[e]), DIST_EXTRA[e]);
    }
    emit_code(w, lc[256], ll[256]);
}

size_t paint_deflate(const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
    Bits w = {out, cap, 0, 0, 0, 0};
    emit(&w, 0x78, 8); /* deflate, a 32 KB window */
    emit(&w, 0xDA, 8); /* the most compression; (0x78 << 8 | 0xDA) % 31 == 0 */
    for (size_t i = 0; i < (1u << HBITS); i++) head[i] = -1;
    size_t i = 0, start = 0;
    int ntok = 0;
    while (i < n) {
        /* look for a match before this place goes in the table, or it finds itself */
        int dist = 0, len = longest(in, n, i, &dist);
        insert(in, n, i);
        if (len && len < LAZY && i + 1 < n) { /* a longer match one on: a byte now, that match next */
            int d2 = 0, l2 = longest(in, n, i + 1, &d2);
            if (l2 > len) {
                tokens[ntok++] = in[i++];
                insert(in, n, i);
                len = l2;
                dist = d2;
            }
        }
        if (len) {
            tokens[ntok++] = 1u << 31 | (uint32_t)len << 16 | (uint32_t)dist;
            for (int k = 1; k < len; k++) insert(in, n, i + (size_t)k);
            i += (size_t)len;
        } else {
            tokens[ntok++] = in[i++];
        }
        if (ntok > BLOCK_TOKENS - 3 && i < n) {
            write_block(&w, in + start, i - start, ntok, 0);
            ntok = 0;
            start = i;
        }
    }
    write_block(&w, in + start, n - start, ntok, 1);
    emit_flush(&w);
    uint32_t a = paint_adler32(1, in, n);
    for (int k = 3; k >= 0; k--) emit(&w, a >> (8 * k) & 0xFF, 8);
    return w.full ? 0 : w.len;
}
