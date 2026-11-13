/* sqqr: QR Code encoder (original code, from ISO/IEC 18004). One segment
 * in the most compact of numeric, alphanumeric or byte mode, an optional
 * ECI header, the smallest version that fits, and the mask with the
 * lowest penalty score. Shares the symbol layout with the decoder. */
#include "sqqr.h"
#include "sqqr_internal.h"

#include <string.h>

typedef struct {
    uint8_t *buf;
    size_t cap_bits, len;
} BitBuf;

static void put_bits(BitBuf *b, uint32_t v, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        if (b->len < b->cap_bits && ((v >> i) & 1))
            b->buf[b->len >> 3] |= (uint8_t)(0x80 >> (b->len & 7));
        b->len++;
    }
}

static int alnum_value(uint8_t c)
{
    const char *p = c ? strchr(SQQR_ALNUM, c) : NULL;
    return p ? (int)(p - SQQR_ALNUM) : -1;
}

int sqqr_pick_mode(const uint8_t *data, size_t len)
{
    int numeric = 1, alnum = 1;
    for (size_t i = 0; i < len; i++) {
        if (data[i] < '0' || data[i] > '9')
            numeric = 0;
        if (alnum_value(data[i]) < 0)
            alnum = 0;
    }
    return numeric ? SQQR_MODE_NUMERIC : alnum ? SQQR_MODE_ALNUM : SQQR_MODE_BYTE;
}

static int eci_bits(int eci)
{
    if (eci < 0)
        return 0;
    return 4 + (eci < 128 ? 8 : eci < 16384 ? 16 : 24);
}

static size_t data_bits(int mode, size_t len)
{
    switch (mode) {
    case SQQR_MODE_NUMERIC: return 10 * (len / 3) + (len % 3 == 2 ? 7 : len % 3 == 1 ? 4 : 0);
    case SQQR_MODE_ALNUM: return 11 * (len / 2) + 6 * (len % 2);
    default: return 8 * len;
    }
}

static int data_codewords(int version, SqqrEcLevel level)
{
    SqqrBlocks b;
    if (sqqr_blocks(version, level, &b) != 0)
        return 0;
    return b.g1_blocks * b.g1_data + b.g2_blocks * b.g2_data;
}

/* ---- penalty score (ISO/IEC 18004 7.8.3) ---- */

static int penalty(const uint8_t *g, int n)
{
    int score = 0;
    /* N1: runs of five or more of one colour, rows and columns */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            int run = 1;
            for (int j = 1; j <= n; j++) {
                int same = j < n && (pass ? g[j * n + i] == g[(j - 1) * n + i]
                                          : g[i * n + j] == g[i * n + j - 1]);
                if (same) {
                    run++;
                } else {
                    if (run >= 5)
                        score += 3 + (run - 5);
                    run = 1;
                }
            }
        }
    /* N2: 2x2 blocks of one colour */
    for (int r = 0; r + 1 < n; r++)
        for (int c = 0; c + 1 < n; c++) {
            uint8_t v = g[r * n + c];
            if (g[r * n + c + 1] == v && g[(r + 1) * n + c] == v && g[(r + 1) * n + c + 1] == v)
                score += 3;
        }
    /* N3: 1:1:3:1:1 finder-like runs with four light modules on a side */
    static const uint8_t pat_a[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
    static const uint8_t pat_b[11] = {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1};
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++)
            for (int j = 0; j + 11 <= n; j++) {
                int a = 1, b = 1;
                for (int k = 0; k < 11; k++) {
                    uint8_t v = pass ? g[(j + k) * n + i] : g[i * n + j + k];
                    if (v != pat_a[k])
                        a = 0;
                    if (v != pat_b[k])
                        b = 0;
                }
                score += 40 * (a + b);
            }
    /* N4: 10 points for every full 5% the dark share is away from 50% */
    long dark = 0, total = (long)n * n;
    for (long i = 0; i < total; i++)
        dark += g[i] != 0;
    long off = dark * 100 - 50 * total; /* in 1/total percent */
    if (off < 0)
        off = -off;
    score += (int)(10 * (off / (5 * total)));
    return score;
}

/* ---- placing modules ---- */

static void set_mod(uint8_t *g, int n, int r, int c, int dark)
{
    if (r >= 0 && c >= 0 && r < n && c < n)
        g[r * n + c] = (uint8_t)(dark ? 1 : 0);
}

static void finder(uint8_t *g, int n, int r0, int c0)
{
    for (int r = -1; r <= 7; r++)
        for (int c = -1; c <= 7; c++) {
            int in = r >= 0 && r <= 6 && c >= 0 && c <= 6;
            int ring = r == 0 || r == 6 || c == 0 || c == 6;
            int core = r >= 2 && r <= 4 && c >= 2 && c <= 4;
            set_mod(g, n, r0 + r, c0 + c, in && (ring || core));
        }
}

static void draw_functions(uint8_t *g, int n, int version)
{
    finder(g, n, 0, 0);
    finder(g, n, 0, n - 7);
    finder(g, n, n - 7, 0);
    for (int i = 8; i < n - 8; i++) {
        set_mod(g, n, 6, i, i % 2 == 0);
        set_mod(g, n, i, 6, i % 2 == 0);
    }
    int pos[7], k = sqqr_alignment_positions(version, pos);
    for (int i = 0; i < k; i++)
        for (int j = 0; j < k; j++) {
            if ((i == 0 && j == 0) || (i == 0 && j == k - 1) || (i == k - 1 && j == 0))
                continue;
            for (int r = -2; r <= 2; r++)
                for (int c = -2; c <= 2; c++) {
                    int m = r < 0 ? -r : r, mc = c < 0 ? -c : c;
                    set_mod(g, n, pos[i] + r, pos[j] + c, (m > mc ? m : mc) != 1);
                }
        }
    if (version >= 7) {
        uint32_t code = ((uint32_t)version << 12) | sqqr_bch_rem((uint32_t)version << 12, 0x1F25, 12);
        for (int i = 0; i < 18; i++) {
            int bit = (code >> i) & 1;
            set_mod(g, n, i / 3, n - 11 + i % 3, bit);
            set_mod(g, n, n - 11 + i % 3, i / 3, bit);
        }
    }
}

static void draw_format(uint8_t *g, int n, SqqrEcLevel level, int mask)
{
    static const uint32_t LEVEL_BITS[4] = {1, 0, 3, 2}; /* L M Q H */
    uint32_t d = LEVEL_BITS[level] << 3 | (uint32_t)mask;
    uint32_t code = ((d << 10) | sqqr_bch_rem(d << 10, 0x537, 10)) ^ 0x5412;
#define FB(i) ((int)((code >> (i)) & 1))
    /* copy 1, around the top-left finder: the decoder reads bit 14 first */
    for (int c = 0; c <= 5; c++)
        set_mod(g, n, 8, c, FB(14 - c));
    set_mod(g, n, 8, 7, FB(8));
    set_mod(g, n, 8, 8, FB(7));
    set_mod(g, n, 7, 8, FB(6));
    for (int r = 5; r >= 0; r--)
        set_mod(g, n, r, 8, FB(r));
    /* copy 2, split between the bottom-left and top-right finders */
    for (int i = 0; i < 7; i++)
        set_mod(g, n, n - 1 - i, 8, FB(14 - i));
    for (int i = 0; i < 8; i++)
        set_mod(g, n, 8, n - 8 + i, FB(7 - i));
#undef FB
    set_mod(g, n, n - 8, 8, 1); /* the dark module */
}

int sqqr_encode(const uint8_t *data, size_t len, const SqqrEncodeOptions *opt, uint8_t *grid,
                size_t grid_cap, SqqrEncodeInfo *info)
{
    SqqrEncodeOptions o = {SQQR_EC_M, 0, -1, -1, SQQR_MODE_AUTO};
    if (opt)
        o = *opt;
    if ((!data && len) || !grid || o.level < SQQR_EC_L || o.level > SQQR_EC_H ||
        o.mask < -1 || o.mask > 7 || o.min_version < 0 || o.min_version > SQQR_MAX_VERSION ||
        o.eci < -1 || o.eci > 999999)
        return SQQR_ERR_SIZE;
    int mode = o.mode == SQQR_MODE_AUTO ? sqqr_pick_mode(data, len) : o.mode;
    if (mode != SQQR_MODE_NUMERIC && mode != SQQR_MODE_ALNUM && mode != SQQR_MODE_BYTE)
        return SQQR_ERR_SIZE;
    if (mode != SQQR_MODE_BYTE && sqqr_pick_mode(data, len) > mode)
        return SQQR_ERR_DATA; /* the data has characters the forced mode can't hold */

    /* smallest version whose capacity holds the segment */
    int version = 0;
    for (int v = o.min_version > 0 ? o.min_version : 1; v <= SQQR_MAX_VERSION; v++) {
        int cb = sqqr_count_bits(mode, v);
        if (len >= (size_t)1 << cb)
            continue;
        size_t need = (size_t)eci_bits(o.eci) + 4 + (size_t)cb + data_bits(mode, len);
        if (need <= (size_t)data_codewords(v, o.level) * 8) {
            version = v;
            break;
        }
    }
    if (!version)
        return SQQR_ERR_OVERFLOW;
    int n = 17 + 4 * version;
    if (grid_cap < (size_t)n * (size_t)n)
        return SQQR_ERR_SIZE;

    /* the data codewords */
    uint8_t cw[3706];
    int ndata = data_codewords(version, o.level);
    memset(cw, 0, sizeof cw);
    BitBuf b = {cw, (size_t)ndata * 8, 0};
    if (o.eci >= 0) {
        put_bits(&b, 7, 4);
        if (o.eci < 128)
            put_bits(&b, (uint32_t)o.eci, 8);
        else if (o.eci < 16384)
            put_bits(&b, 0x8000u | (uint32_t)o.eci, 16);
        else
            put_bits(&b, 0xC00000u | (uint32_t)o.eci, 24);
    }
    put_bits(&b, (uint32_t)mode, 4);
    put_bits(&b, (uint32_t)len, sqqr_count_bits(mode, version));
    if (mode == SQQR_MODE_NUMERIC) {
        for (size_t i = 0; i < len; i += 3) {
            size_t k = len - i < 3 ? len - i : 3;
            uint32_t v = 0;
            for (size_t j = 0; j < k; j++)
                v = v * 10 + (uint32_t)(data[i + j] - '0');
            put_bits(&b, v, k == 3 ? 10 : k == 2 ? 7 : 4);
        }
    } else if (mode == SQQR_MODE_ALNUM) {
        for (size_t i = 0; i < len; i += 2) {
            if (i + 1 < len)
                put_bits(&b, (uint32_t)(alnum_value(data[i]) * 45 + alnum_value(data[i + 1])), 11);
            else
                put_bits(&b, (uint32_t)alnum_value(data[i]), 6);
        }
    } else {
        for (size_t i = 0; i < len; i++)
            put_bits(&b, data[i], 8);
    }
    size_t cap = (size_t)ndata * 8;
    size_t term = cap - b.len < 4 ? cap - b.len : 4;
    put_bits(&b, 0, (int)term);
    b.len = (b.len + 7) & ~(size_t)7;
    for (int i = 0; b.len < cap; i++)
        put_bits(&b, i % 2 ? 0x11 : 0xEC, 8);

    /* blocks, error correction and interleaving */
    SqqrBlocks bl;
    sqqr_blocks(version, o.level, &bl);
    int nblocks = bl.g1_blocks + bl.g2_blocks;
    static const int max_blocks = 81;
    if (nblocks > max_blocks)
        return SQQR_ERR_SIZE;
    uint8_t ecc[81][68];
    int start[81], dlen[81], at = 0;
    for (int i = 0; i < nblocks; i++) {
        dlen[i] = i < bl.g1_blocks ? bl.g1_data : bl.g2_data;
        start[i] = at;
        sqqr_rs_encode(cw + at, dlen[i], bl.ec_per_block, ecc[i]);
        at += dlen[i];
    }
    uint8_t out[3706];
    int p = 0, maxd = bl.g2_blocks ? bl.g2_data : bl.g1_data;
    for (int j = 0; j < maxd; j++)
        for (int i = 0; i < nblocks; i++)
            if (j < dlen[i])
                out[p++] = cw[start[i] + j];
    for (int j = 0; j < bl.ec_per_block; j++)
        for (int i = 0; i < nblocks; i++)
            out[p++] = ecc[i][j];

    /* place the bits in the zigzag order the decoder reads them */
    SqqrLayout lay;
    sqqr_layout_init(&lay, version);
    uint8_t base[SQQR_MAX_SIZE * SQQR_MAX_SIZE];
    memset(base, 0, (size_t)n * n);
    size_t bit = 0, nbits = (size_t)p * 8;
    int upward = 1;
    for (int right = n - 1; right >= 1; right -= 2) {
        if (right == 6)
            right = 5;
        for (int k = 0; k < n; k++) {
            int row = upward ? n - 1 - k : k;
            for (int dc = 0; dc < 2; dc++) {
                int col = right - dc;
                if (lay.fn[row * n + col])
                    continue;
                if (bit < nbits)
                    base[row * n + col] = (out[bit >> 3] >> (7 - (bit & 7))) & 1;
                bit++;
            }
        }
        upward = !upward;
    }

    /* masks: the given one, or the lowest penalty of all eight */
    int best = o.mask, best_score = -1;
    for (int m = o.mask >= 0 ? o.mask : 0; m <= (o.mask >= 0 ? o.mask : 7); m++) {
        for (int i = 0; i < n * n; i++) {
            int r = i / n, c = i % n;
            grid[i] = lay.fn[i] ? 0 : (uint8_t)(base[i] ^ (uint8_t)sqqr_mask_bit(m, r, c));
        }
        draw_functions(grid, n, version);
        draw_format(grid, n, o.level, m);
        if (o.mask >= 0)
            break;
        int s = penalty(grid, n);
        if (best_score < 0 || s < best_score) {
            best_score = s;
            best = m;
        }
    }
    if (o.mask < 0) { /* draw the winner again */
        for (int i = 0; i < n * n; i++) {
            int r = i / n, c = i % n;
            grid[i] = lay.fn[i] ? 0 : (uint8_t)(base[i] ^ (uint8_t)sqqr_mask_bit(best, r, c));
        }
        draw_functions(grid, n, version);
        draw_format(grid, n, o.level, best);
    }
    if (info) {
        info->version = version;
        info->size = n;
        info->ec_level = o.level;
        info->mask = best;
        info->mode = mode;
        info->penalty = o.mask < 0 ? best_score : penalty(grid, n);
    }
    return SQQR_OK;
}
