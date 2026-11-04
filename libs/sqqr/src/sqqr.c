#include "sqqr.h"

#include <string.h>

/* ------------------------------------------------------------ GF(256) */

typedef struct {
    uint8_t exp[512];
    uint8_t log[256];
} Gf;

static void gf_init(Gf *g) {
    unsigned x = 1;
    for (int i = 0; i < 255; i++) {
        g->exp[i] = (uint8_t)x;
        g->log[x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100) x ^= 0x11D;
    }
    for (int i = 255; i < 512; i++) g->exp[i] = g->exp[i - 255];
    g->log[0] = 0; /* unused */
}

static uint8_t gf_mul(const Gf *g, uint8_t a, uint8_t b) {
    if (!a || !b) return 0;
    return g->exp[g->log[a] + g->log[b]];
}

static uint8_t gf_div(const Gf *g, uint8_t a, uint8_t b) {
    if (!a) return 0;
    return g->exp[(g->log[a] + 255 - g->log[b]) % 255];
}

static uint8_t gf_pow_a(const Gf *g, int e) { /* a^e, any integer e */
    e %= 255;
    if (e < 0) e += 255;
    return g->exp[e];
}

/* Evaluate p (p[0] = constant term, degree deg) at x. */
static uint8_t poly_eval_lo(const Gf *g, const uint8_t *p, int deg, uint8_t x) {
    uint8_t r = 0;
    for (int i = deg; i >= 0; i--) r = (uint8_t)(gf_mul(g, r, x) ^ p[i]);
    return r;
}

/* ------------------------------------------------------- Reed-Solomon */

void sqqr_rs_encode(const uint8_t *data, int n, int nsym, uint8_t *ecc) {
    Gf g;
    gf_init(&g);
    /* Generator prod_{i<nsym} (x - a^i), gen[0] = highest degree (always 1). */
    uint8_t gen[256] = {1};
    int glen = 1;
    for (int i = 0; i < nsym; i++) {
        uint8_t r = g.exp[i];
        gen[glen] = 0;
        for (int j = glen; j > 0; j--) gen[j] ^= gf_mul(&g, gen[j - 1], r);
        glen++;
    }
    memset(ecc, 0, (size_t)nsym);
    for (int i = 0; i < n; i++) {
        uint8_t f = (uint8_t)(data[i] ^ ecc[0]);
        memmove(ecc, ecc + 1, (size_t)nsym - 1);
        ecc[nsym - 1] = 0;
        for (int j = 0; j < nsym; j++) ecc[j] ^= gf_mul(&g, gen[j + 1], f);
    }
}

int sqqr_rs_decode(uint8_t *cw, int n, int nsym) {
    if (n <= 0 || n > 255 || nsym <= 0 || nsym >= n) return -1;
    Gf g;
    gf_init(&g);

    /* Syndromes S_j = c(a^j); cw[0] is the x^(n-1) coefficient. */
    uint8_t s[256];
    int nonzero = 0;
    for (int j = 0; j < nsym; j++) {
        uint8_t x = g.exp[j], r = 0;
        for (int i = 0; i < n; i++) r = (uint8_t)(gf_mul(&g, r, x) ^ cw[i]);
        s[j] = r;
        nonzero |= r;
    }
    if (!nonzero) return 0;

    /* Berlekamp-Massey: error locator lambda (lambda[0] = 1, low degree first). */
    uint8_t lam[256] = {1}, prev[256] = {1}, tmp[256];
    int L = 0, m = 1;
    uint8_t b = 1;
    for (int k = 0; k < nsym; k++) {
        uint8_t d = s[k];
        for (int i = 1; i <= L; i++) d ^= gf_mul(&g, lam[i], s[k - i]);
        if (!d) {
            m++;
            continue;
        }
        uint8_t coef = gf_div(&g, d, b);
        memcpy(tmp, lam, sizeof tmp);
        for (int i = 0; i + m < 256; i++) lam[i + m] ^= gf_mul(&g, coef, prev[i]);
        if (2 * L <= k) {
            L = k + 1 - L;
            memcpy(prev, tmp, sizeof prev);
            b = d;
            m = 1;
        } else {
            m++;
        }
    }
    if (2 * L > nsym) return -1;

    /* Chien search: lambda(a^-i) == 0 means an error at degree i. */
    int pos[128], np = 0;
    for (int i = 0; i < n; i++) {
        if (poly_eval_lo(&g, lam, L, gf_pow_a(&g, -i)) == 0) {
            if (np >= 128) return -1;
            pos[np++] = i;
        }
    }
    if (np != L) return -1;

    /* Omega = S(x) * lambda(x) mod x^nsym. */
    uint8_t om[256] = {0};
    for (int i = 0; i < nsym; i++) {
        uint8_t v = 0;
        for (int j = 0; j <= i && j <= L; j++) v ^= gf_mul(&g, lam[j], s[i - j]);
        om[i] = v;
    }

    /* Forney (first root a^0): e = X * Omega(X^-1) / lambda'(X^-1). */
    for (int k = 0; k < np; k++) {
        uint8_t X = gf_pow_a(&g, pos[k]), Xi = gf_pow_a(&g, -pos[k]);
        uint8_t num = gf_mul(&g, X, poly_eval_lo(&g, om, nsym - 1, Xi));
        uint8_t den = 0;
        for (int i = 1; i <= L; i += 2) { /* formal derivative: odd terms only */
            uint8_t t = lam[i];
            for (int e = 0; e < i - 1; e++) t = gf_mul(&g, t, Xi);
            den ^= t;
        }
        if (!den) return -1;
        cw[n - 1 - pos[k]] ^= gf_div(&g, num, den);
    }

    /* Re-check: a miscorrection leaves nonzero syndromes. */
    for (int j = 0; j < nsym; j++) {
        uint8_t x = g.exp[j], r = 0;
        for (int i = 0; i < n; i++) r = (uint8_t)(gf_mul(&g, r, x) ^ cw[i]);
        if (r) return -1;
    }
    return np;
}

/* ------------------------------------------------- format / version */

static uint32_t bch_rem(uint32_t v, uint32_t gen, int gen_deg) {
    for (int i = 31; i >= gen_deg; i--)
        if (v & (1u << i)) v ^= gen << (i - gen_deg);
    return v;
}

static int popcount32(uint32_t v) {
    int c = 0;
    while (v) {
        v &= v - 1;
        c++;
    }
    return c;
}

int sqqr_decode_format_bits(uint32_t word) {
    int best = -1, best_d = 4;
    for (uint32_t d = 0; d < 32; d++) {
        uint32_t code = ((d << 10) | bch_rem(d << 10, 0x537, 10)) ^ 0x5412;
        int dist = popcount32((code ^ word) & 0x7FFF);
        if (dist < best_d) {
            best_d = dist;
            best = (int)d;
        }
    }
    return best;
}

int sqqr_decode_version_bits(uint32_t word) {
    int best = -1, best_d = 4;
    for (uint32_t v = 7; v <= 40; v++) {
        uint32_t code = (v << 12) | bch_rem(v << 12, 0x1F25, 12);
        int dist = popcount32((code ^ word) & 0x3FFFF);
        if (dist < best_d) {
            best_d = dist;
            best = (int)v;
        }
    }
    return best;
}

/* ------------------------------------------------------ block table */

/* ISO/IEC 18004 error-correction blocks, per version, for L, M, Q, H:
 * {EC codewords per block, group-1 blocks, data cw each, group-2 blocks, data cw each}. */
static const uint8_t EC_BLOCKS[40][4][5] = {
    /*  1 */ {{7, 1, 19, 0, 0}, {10, 1, 16, 0, 0}, {13, 1, 13, 0, 0}, {17, 1, 9, 0, 0}},
    /*  2 */ {{10, 1, 34, 0, 0}, {16, 1, 28, 0, 0}, {22, 1, 22, 0, 0}, {28, 1, 16, 0, 0}},
    /*  3 */ {{15, 1, 55, 0, 0}, {26, 1, 44, 0, 0}, {18, 2, 17, 0, 0}, {22, 2, 13, 0, 0}},
    /*  4 */ {{20, 1, 80, 0, 0}, {18, 2, 32, 0, 0}, {26, 2, 24, 0, 0}, {16, 4, 9, 0, 0}},
    /*  5 */ {{26, 1, 108, 0, 0}, {24, 2, 43, 0, 0}, {18, 2, 15, 2, 16}, {22, 2, 11, 2, 12}},
    /*  6 */ {{18, 2, 68, 0, 0}, {16, 4, 27, 0, 0}, {24, 4, 19, 0, 0}, {28, 4, 15, 0, 0}},
    /*  7 */ {{20, 2, 78, 0, 0}, {18, 4, 31, 0, 0}, {18, 2, 14, 4, 15}, {26, 4, 13, 1, 14}},
    /*  8 */ {{24, 2, 97, 0, 0}, {22, 2, 38, 2, 39}, {22, 4, 18, 2, 19}, {26, 4, 14, 2, 15}},
    /*  9 */ {{30, 2, 116, 0, 0}, {22, 3, 36, 2, 37}, {20, 4, 16, 4, 17}, {24, 4, 12, 4, 13}},
    /* 10 */ {{18, 2, 68, 2, 69}, {26, 4, 43, 1, 44}, {24, 6, 19, 2, 20}, {28, 6, 15, 2, 16}},
    /* 11 */ {{20, 4, 81, 0, 0}, {30, 1, 50, 4, 51}, {28, 4, 22, 4, 23}, {24, 3, 12, 8, 13}},
    /* 12 */ {{24, 2, 92, 2, 93}, {22, 6, 36, 2, 37}, {26, 4, 20, 6, 21}, {28, 7, 14, 4, 15}},
    /* 13 */ {{26, 4, 107, 0, 0}, {22, 8, 37, 1, 38}, {24, 8, 20, 4, 21}, {22, 12, 11, 4, 12}},
    /* 14 */ {{30, 3, 115, 1, 116}, {24, 4, 40, 5, 41}, {20, 11, 16, 5, 17}, {24, 11, 12, 5, 13}},
    /* 15 */ {{22, 5, 87, 1, 88}, {24, 5, 41, 5, 42}, {30, 5, 24, 7, 25}, {24, 11, 12, 7, 13}},
    /* 16 */ {{24, 5, 98, 1, 99}, {28, 7, 45, 3, 46}, {24, 15, 19, 2, 20}, {30, 3, 15, 13, 16}},
    /* 17 */ {{28, 1, 107, 5, 108}, {28, 10, 46, 1, 47}, {28, 1, 22, 15, 23}, {28, 2, 14, 17, 15}},
    /* 18 */ {{30, 5, 120, 1, 121}, {26, 9, 43, 4, 44}, {28, 17, 22, 1, 23}, {28, 2, 14, 19, 15}},
    /* 19 */ {{28, 3, 113, 4, 114}, {26, 3, 44, 11, 45}, {26, 17, 21, 4, 22}, {26, 9, 13, 16, 14}},
    /* 20 */ {{28, 3, 107, 5, 108}, {26, 3, 41, 13, 42}, {30, 15, 24, 5, 25}, {28, 15, 15, 10, 16}},
    /* 21 */ {{28, 4, 116, 4, 117}, {26, 17, 42, 0, 0}, {28, 17, 22, 6, 23}, {30, 19, 16, 6, 17}},
    /* 22 */ {{28, 2, 111, 7, 112}, {28, 17, 46, 0, 0}, {30, 7, 24, 16, 25}, {24, 34, 13, 0, 0}},
    /* 23 */ {{30, 4, 121, 5, 122}, {28, 4, 47, 14, 48}, {30, 11, 24, 14, 25}, {30, 16, 15, 14, 16}},
    /* 24 */ {{30, 6, 117, 4, 118}, {28, 6, 45, 14, 46}, {30, 11, 24, 16, 25}, {30, 30, 16, 2, 17}},
    /* 25 */ {{26, 8, 106, 4, 107}, {28, 8, 47, 13, 48}, {30, 7, 24, 22, 25}, {30, 22, 15, 13, 16}},
    /* 26 */ {{28, 10, 114, 2, 115}, {28, 19, 46, 4, 47}, {28, 28, 22, 6, 23}, {30, 33, 16, 4, 17}},
    /* 27 */ {{30, 8, 122, 4, 123}, {28, 22, 45, 3, 46}, {30, 8, 23, 26, 24}, {30, 12, 15, 28, 16}},
    /* 28 */ {{30, 3, 117, 10, 118}, {28, 3, 45, 23, 46}, {30, 4, 24, 31, 25}, {30, 11, 15, 31, 16}},
    /* 29 */ {{30, 7, 116, 7, 117}, {28, 21, 45, 7, 46}, {30, 1, 23, 37, 24}, {30, 19, 15, 26, 16}},
    /* 30 */ {{30, 5, 115, 10, 116}, {28, 19, 47, 10, 48}, {30, 15, 24, 25, 25}, {30, 23, 15, 25, 16}},
    /* 31 */ {{30, 13, 115, 3, 116}, {28, 2, 46, 29, 47}, {30, 42, 24, 1, 25}, {30, 23, 15, 28, 16}},
    /* 32 */ {{30, 17, 115, 0, 0}, {28, 10, 46, 23, 47}, {30, 10, 24, 35, 25}, {30, 19, 15, 35, 16}},
    /* 33 */ {{30, 17, 115, 1, 116}, {28, 14, 46, 21, 47}, {30, 29, 24, 19, 25}, {30, 11, 15, 46, 16}},
    /* 34 */ {{30, 13, 115, 6, 116}, {28, 14, 46, 23, 47}, {30, 44, 24, 7, 25}, {30, 59, 16, 1, 17}},
    /* 35 */ {{30, 12, 121, 7, 122}, {28, 12, 47, 26, 48}, {30, 39, 24, 14, 25}, {30, 22, 15, 41, 16}},
    /* 36 */ {{30, 6, 121, 14, 122}, {28, 6, 47, 34, 48}, {30, 46, 24, 10, 25}, {30, 2, 15, 64, 16}},
    /* 37 */ {{30, 17, 122, 4, 123}, {28, 29, 46, 14, 47}, {30, 49, 24, 10, 25}, {30, 24, 15, 46, 16}},
    /* 38 */ {{30, 4, 122, 18, 123}, {28, 13, 46, 32, 47}, {30, 48, 24, 14, 25}, {30, 42, 15, 32, 16}},
    /* 39 */ {{30, 20, 117, 4, 118}, {28, 40, 47, 7, 48}, {30, 43, 24, 22, 25}, {30, 10, 15, 67, 16}},
    /* 40 */ {{30, 19, 118, 6, 119}, {28, 18, 47, 31, 48}, {30, 34, 24, 34, 25}, {30, 20, 15, 61, 16}},
};

int sqqr_blocks(int version, SqqrEcLevel level, SqqrBlocks *out) {
    if (version < 1 || version > 40 || (int)level < 0 || (int)level > 3 || !out) return -1;
    const uint8_t *e = EC_BLOCKS[version - 1][level];
    out->ec_per_block = e[0];
    out->g1_blocks = e[1];
    out->g1_data = e[2];
    out->g2_blocks = e[3];
    out->g2_data = e[4];
    out->total = e[1] * (e[2] + e[0]) + e[3] * (e[4] + e[0]);
    return 0;
}

int sqqr_alignment_positions(int version, int pos[7]) {
    if (version < 2 || version > 40) return 0;
    int n = version / 7 + 2, last = 4 * version + 10;
    int step = version == 32 ? 26 : ((4 * version + 2 * n + 1) / (2 * n - 2)) * 2;
    pos[0] = 6;
    for (int i = n - 1; i >= 1; i--) pos[i] = last - (n - 1 - i) * step;
    return n;
}

/* ------------------------------------------------------- grid layout */

typedef struct {
    int size, version;
    uint8_t fn[SQQR_MAX_SIZE * SQQR_MAX_SIZE]; /* 1 = function module (not data) */
} Layout;

static void mark(Layout *l, int r0, int c0, int h, int w) {
    for (int r = r0; r < r0 + h; r++)
        for (int c = c0; c < c0 + w; c++)
            if (r >= 0 && c >= 0 && r < l->size && c < l->size) l->fn[r * l->size + c] = 1;
}

static void layout_init(Layout *l, int version) {
    int n = 17 + 4 * version;
    l->size = n;
    l->version = version;
    memset(l->fn, 0, sizeof l->fn);
    /* Finder patterns with separators and format areas (9x9, 9x8, 8x9). */
    mark(l, 0, 0, 9, 9);
    mark(l, 0, n - 8, 9, 8);
    mark(l, n - 8, 0, 8, 9);
    /* Timing patterns. */
    mark(l, 6, 0, 1, n);
    mark(l, 0, 6, n, 1);
    /* Alignment patterns, except where they would overlap a finder. */
    int pos[7], k = sqqr_alignment_positions(version, pos);
    for (int i = 0; i < k; i++)
        for (int j = 0; j < k; j++) {
            if ((i == 0 && j == 0) || (i == 0 && j == k - 1) || (i == k - 1 && j == 0)) continue;
            mark(l, pos[i] - 2, pos[j] - 2, 5, 5);
        }
    /* Version information blocks. */
    if (version >= 7) {
        mark(l, 0, n - 11, 6, 3);
        mark(l, n - 11, 0, 3, 6);
    }
    /* The dark module sits inside the bottom-left format area already marked. */
}

static int mask_bit(int mask, int i, int j) {
    switch (mask) {
        case 0: return (i + j) % 2 == 0;
        case 1: return i % 2 == 0;
        case 2: return j % 3 == 0;
        case 3: return (i + j) % 3 == 0;
        case 4: return (i / 2 + j / 3) % 2 == 0;
        case 5: return (i * j) % 2 + (i * j) % 3 == 0;
        case 6: return ((i * j) % 2 + (i * j) % 3) % 2 == 0;
        default: return ((i + j) % 2 + (i * j) % 3) % 2 == 0;
    }
}

#define DARK(g, n, r, c) ((g)[(r) * (n) + (c)] ? 1u : 0u)

static uint32_t read_format_1(const uint8_t *g, int n) {
    uint32_t v = 0;
    for (int c = 0; c <= 5; c++) v = (v << 1) | DARK(g, n, 8, c);
    v = (v << 1) | DARK(g, n, 8, 7);
    v = (v << 1) | DARK(g, n, 8, 8);
    v = (v << 1) | DARK(g, n, 7, 8);
    for (int r = 5; r >= 0; r--) v = (v << 1) | DARK(g, n, r, 8);
    return v;
}

static uint32_t read_format_2(const uint8_t *g, int n) {
    uint32_t v = 0;
    for (int r = n - 1; r >= n - 7; r--) v = (v << 1) | DARK(g, n, r, 8);
    for (int c = n - 8; c < n; c++) v = (v << 1) | DARK(g, n, 8, c);
    return v;
}

static uint32_t read_version_1(const uint8_t *g, int n) { /* top-right block */
    uint32_t v = 0;
    for (int i = 17; i >= 0; i--) v = (v << 1) | DARK(g, n, i / 3, n - 11 + i % 3);
    return v;
}

static uint32_t read_version_2(const uint8_t *g, int n) { /* bottom-left block */
    uint32_t v = 0;
    for (int i = 17; i >= 0; i--) v = (v << 1) | DARK(g, n, n - 11 + i % 3, i / 3);
    return v;
}

/* --------------------------------------------------------- bitstream */

typedef struct {
    const uint8_t *buf;
    size_t len_bits, pos;
} Bits;

static int bits_get(Bits *b, int n, uint32_t *out) {
    if (b->pos + (size_t)n > b->len_bits) return -1;
    uint32_t v = 0;
    for (int i = 0; i < n; i++, b->pos++)
        v = (v << 1) | ((b->buf[b->pos >> 3] >> (7 - (b->pos & 7))) & 1u);
    *out = v;
    return 0;
}

static size_t bits_left(const Bits *b) { return b->len_bits - b->pos; }

static int put(SqqrResult *r, uint8_t c) {
    if (r->len >= SQQR_MAX_PAYLOAD) return -1;
    r->data[r->len++] = c;
    return 0;
}

static const char ALNUM[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

static int count_bits(int mode, int version) {
    int range = version <= 9 ? 0 : version <= 26 ? 1 : 2;
    static const int numeric[3] = {10, 12, 14}, alnum[3] = {9, 11, 13}, byte8[3] = {8, 16, 16},
                     kanji[3] = {8, 10, 12};
    switch (mode) {
        case 1: return numeric[range];
        case 2: return alnum[range];
        case 4: return byte8[range];
        default: return kanji[range];
    }
}

static int parse_stream(Bits *b, int version, SqqrResult *r) {
    for (;;) {
        uint32_t mode = 0;
        if (bits_left(b) < 4) return 0; /* terminator may be truncated */
        bits_get(b, 4, &mode);
        if (mode == 0) return 0;
        uint32_t cnt, v;
        switch (mode) {
            case 1: /* numeric */
                if (bits_get(b, count_bits(1, version), &cnt)) return -1;
                while (cnt >= 3) {
                    if (bits_get(b, 10, &v) || v > 999) return -1;
                    if (put(r, (uint8_t)('0' + v / 100)) || put(r, (uint8_t)('0' + v / 10 % 10)) ||
                        put(r, (uint8_t)('0' + v % 10)))
                        return SQQR_ERR_OVERFLOW;
                    cnt -= 3;
                }
                if (cnt == 2) {
                    if (bits_get(b, 7, &v) || v > 99) return -1;
                    if (put(r, (uint8_t)('0' + v / 10)) || put(r, (uint8_t)('0' + v % 10))) return SQQR_ERR_OVERFLOW;
                } else if (cnt == 1) {
                    if (bits_get(b, 4, &v) || v > 9) return -1;
                    if (put(r, (uint8_t)('0' + v))) return SQQR_ERR_OVERFLOW;
                }
                break;
            case 2: /* alphanumeric */
                if (bits_get(b, count_bits(2, version), &cnt)) return -1;
                while (cnt >= 2) {
                    if (bits_get(b, 11, &v) || v >= 45 * 45) return -1;
                    if (put(r, (uint8_t)ALNUM[v / 45]) || put(r, (uint8_t)ALNUM[v % 45])) return SQQR_ERR_OVERFLOW;
                    cnt -= 2;
                }
                if (cnt == 1) {
                    if (bits_get(b, 6, &v) || v >= 45) return -1;
                    if (put(r, (uint8_t)ALNUM[v])) return SQQR_ERR_OVERFLOW;
                }
                break;
            case 4: /* byte */
                if (bits_get(b, count_bits(4, version), &cnt)) return -1;
                for (uint32_t i = 0; i < cnt; i++) {
                    if (bits_get(b, 8, &v)) return -1;
                    if (put(r, (uint8_t)v)) return SQQR_ERR_OVERFLOW;
                }
                break;
            case 8: /* kanji -> Shift JIS */
                if (bits_get(b, count_bits(8, version), &cnt)) return -1;
                r->has_kanji = 1;
                for (uint32_t i = 0; i < cnt; i++) {
                    if (bits_get(b, 13, &v)) return -1;
                    uint32_t w = ((v / 0xC0) << 8) | (v % 0xC0);
                    w += w < 0x1F00 ? 0x8140 : 0xC140;
                    if (put(r, (uint8_t)(w >> 8)) || put(r, (uint8_t)w)) return SQQR_ERR_OVERFLOW;
                }
                break;
            case 7: { /* ECI */
                uint32_t a, x;
                if (bits_get(b, 8, &a)) return -1;
                if ((a & 0x80) == 0) {
                    x = a;
                } else if ((a & 0xC0) == 0x80) {
                    if (bits_get(b, 8, &x)) return -1;
                    x |= (a & 0x3F) << 8;
                } else if ((a & 0xE0) == 0xC0) {
                    if (bits_get(b, 16, &x)) return -1;
                    x |= (a & 0x1F) << 16;
                } else {
                    return -1;
                }
                if (r->eci < 0) r->eci = (int)x;
                break;
            }
            case 3: /* structured append: sequence + parity, ignored */
                if (bits_get(b, 16, &v)) return -1;
                break;
            case 5: /* FNC1 first position: no payload */
                break;
            case 9: /* FNC1 second position: application indicator */
                if (bits_get(b, 8, &v)) return -1;
                break;
            default:
                return -1;
        }
    }
}

/* ------------------------------------------------------------ decode */

int sqqr_decode_grid(const uint8_t *grid, int size, SqqrResult *out) {
    if (!grid || !out || size < 21 || size > SQQR_MAX_SIZE || size % 4 != 1) return SQQR_ERR_SIZE;
    memset(out, 0, sizeof *out);
    out->eci = -1;
    int version = (size - 17) / 4;

    int f = sqqr_decode_format_bits(read_format_1(grid, size));
    if (f < 0) f = sqqr_decode_format_bits(read_format_2(grid, size));
    if (f < 0) return SQQR_ERR_FORMAT;
    static const SqqrEcLevel LEVEL_FROM_BITS[4] = {SQQR_EC_M, SQQR_EC_L, SQQR_EC_H, SQQR_EC_Q};
    SqqrEcLevel level = LEVEL_FROM_BITS[(f >> 3) & 3];
    int mask = f & 7;

    if (version >= 7) {
        int v = sqqr_decode_version_bits(read_version_1(grid, size));
        if (v < 0) v = sqqr_decode_version_bits(read_version_2(grid, size));
        if (v != version) return SQQR_ERR_VERSION;
    }

    Layout lay;
    layout_init(&lay, version);
    SqqrBlocks bl;
    sqqr_blocks(version, level, &bl);

    /* Read codewords in the zigzag order, unmasking as we go. */
    uint8_t raw[3706];
    memset(raw, 0, sizeof raw);
    size_t nbits = 0, max_bits = (size_t)bl.total * 8;
    int upward = 1;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5; /* skip the vertical timing column */
        for (int k = 0; k < size; k++) {
            int row = upward ? size - 1 - k : k;
            for (int dc = 0; dc < 2; dc++) {
                int col = right - dc;
                if (lay.fn[row * size + col]) continue;
                if (nbits < max_bits) {
                    unsigned bit = DARK(grid, size, row, col) ^ (unsigned)mask_bit(mask, row, col);
                    if (bit) raw[nbits >> 3] |= (uint8_t)(0x80 >> (nbits & 7));
                }
                nbits++;
            }
        }
        upward = !upward;
    }
    if (nbits < max_bits) return SQQR_ERR_DATA;

    /* De-interleave into blocks, correct each, and concatenate the data. */
    int nblocks = bl.g1_blocks + bl.g2_blocks;
    uint8_t blocks[81][256];
    int dlen[81];
    for (int i = 0; i < nblocks; i++) dlen[i] = i < bl.g1_blocks ? bl.g1_data : bl.g2_data;
    int maxd = bl.g2_blocks ? bl.g2_data : bl.g1_data;
    size_t p = 0;
    for (int j = 0; j < maxd; j++)
        for (int i = 0; i < nblocks; i++)
            if (j < dlen[i]) blocks[i][j] = raw[p++];
    for (int j = 0; j < bl.ec_per_block; j++)
        for (int i = 0; i < nblocks; i++) blocks[i][dlen[i] + j] = raw[p++];

    uint8_t data[3706];
    size_t dl = 0;
    for (int i = 0; i < nblocks; i++) {
        int fixed = sqqr_rs_decode(blocks[i], dlen[i] + bl.ec_per_block, bl.ec_per_block);
        if (fixed < 0) return SQQR_ERR_ECC;
        out->corrected += fixed;
        memcpy(data + dl, blocks[i], (size_t)dlen[i]);
        dl += (size_t)dlen[i];
    }

    out->version = version;
    out->ec_level = level;
    out->mask = mask;
    Bits b = {data, dl * 8, 0};
    int rc = parse_stream(&b, version, out);
    if (rc == SQQR_ERR_OVERFLOW) return rc;
    if (rc < 0) return SQQR_ERR_DATA;
    out->data[out->len] = 0;
    return SQQR_OK;
}

const char *sqqr_strerror(int code) {
    switch (code) {
        case SQQR_OK: return "ok";
        case SQQR_ERR_SIZE: return "grid size is not a QR version";
        case SQQR_ERR_FORMAT: return "format information unreadable";
        case SQQR_ERR_VERSION: return "version information unreadable or wrong";
        case SQQR_ERR_ECC: return "too many errors to correct";
        case SQQR_ERR_DATA: return "malformed data";
        case SQQR_ERR_OVERFLOW: return "payload too large";
        case SQQR_ERR_NOT_FOUND: return "no QR symbol found in the image";
        default: return "unknown error";
    }
}
