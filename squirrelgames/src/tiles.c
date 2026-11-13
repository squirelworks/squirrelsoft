/* tiles: the tile-matching rules and deals (see tiles.h). Original code and layouts. */
#include "tiles.h"

#include <string.h>

const char *const TILES_LAYOUT_NAMES[TILES_LAYOUTS] = {"Pyramid", "Towers", "Cross"};

static uint32_t next_random(uint32_t *s) { /* splitmix style, as cards.c */
    uint32_t z = (*s += 0x9E3779B9u);
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

/* ---- layouts: blocks of tiles, each a layer, a corner in half tiles, columns and rows ---- */

typedef struct {
    int8_t z, x, y, cols, rows;
} Block;

static const Block PYRAMID[] = {{0, 0, 0, 10, 8}, {1, 2, 2, 8, 6}, {2, 6, 4, 4, 4}, {-1, 0, 0, 0, 0}};
static const Block TOWERS[] = {{0, 0, 0, 4, 8},  {0, 16, 0, 4, 8}, {0, 8, 6, 4, 2},  {1, 1, 1, 3, 7}, {1, 17, 1, 3, 7},
                               {2, 2, 2, 2, 6},  {2, 18, 2, 2, 6}, {3, 3, 5, 1, 3},  {3, 19, 5, 1, 3}, {-1, 0, 0, 0, 0}};
static const Block CROSS[] = {{0, 6, 0, 6, 12}, {0, 0, 8, 3, 4},  {0, 18, 8, 3, 4},
                              {1, 8, 2, 4, 10}, {2, 10, 6, 2, 4}, {-1, 0, 0, 0, 0}};
static const Block *const LAYOUTS[TILES_LAYOUTS] = {PYRAMID, TOWERS, CROSS};

static int abs_i(int v) { return v < 0 ? -v : v; }

/* ---- free tiles ---- */

int tiles_free(const Tiles *t, int i) {
    if (i < 0 || i >= t->n || !t->here[i]) return 0;
    int left = 0, right = 0;
    for (int k = 0; k < t->n; k++) {
        if (!t->here[k] || k == i) continue;
        int dx = t->x[k] - t->x[i], dy = t->y[k] - t->y[i];
        if (abs_i(dy) >= 2) continue;
        if (t->z[k] == t->z[i] + 1 && abs_i(dx) < 2) return 0; /* something on top */
        if (t->z[k] == t->z[i]) {
            if (dx == -2) left = 1;
            if (dx == 2) right = 1;
        }
    }
    return !(left && right);
}

/* ---- deals ---- */

/* kinds for the tiles here, by taking them away in random pairs from
 * the full table: -1 when that ran into a corner (try another) */
static int deal_kinds(Tiles *t, uint32_t *r) {
    uint8_t save[TILES_MAX], kinds[TILES_MAX];
    int16_t order[TILES_MAX];
    int idx[TILES_MAX], count = 0;
    memcpy(save, t->here, sizeof save);
    memcpy(kinds, t->kind, sizeof kinds);
    for (int i = 0; i < t->n; i++)
        if (t->here[i]) idx[count++] = i;
    /* the kinds the pairs get: the kinds of the tiles here, two by two */
    uint8_t pairs[TILES_MAX / 2];
    int np = 0;
    {
        uint8_t ks[TILES_MAX];
        int nk = 0;
        for (int k = 0; k < count; k++) ks[nk++] = t->kind[idx[k]];
        for (int a = 0; a < nk; a++) /* sort them so that the same kinds sit together */
            for (int b = a + 1; b < nk; b++)
                if (ks[b] < ks[a]) {
                    uint8_t tmp = ks[a];
                    ks[a] = ks[b], ks[b] = tmp;
                }
        for (int a = 0; a + 1 < nk; a += 2) pairs[np++] = ks[a];
    }
    for (int i = np - 1; i > 0; i--) {
        int j = (int)(next_random(r) % (uint32_t)(i + 1));
        uint8_t tmp = pairs[i];
        pairs[i] = pairs[j], pairs[j] = tmp;
    }
    for (int p = 0; p < np; p++) {
        int fr[TILES_MAX], nf = 0;
        for (int k = 0; k < count; k++)
            if (t->here[idx[k]] && tiles_free(t, idx[k])) fr[nf++] = idx[k];
        if (nf < 2) { /* as it was */
            memcpy(t->here, save, sizeof save);
            memcpy(t->kind, kinds, sizeof kinds);
            return -1;
        }
        int a = (int)(next_random(r) % (uint32_t)nf), b = (int)(next_random(r) % (uint32_t)(nf - 1));
        if (b >= a) b++;
        t->kind[fr[a]] = t->kind[fr[b]] = pairs[p];
        order[2 * p] = (int16_t)fr[a], order[2 * p + 1] = (int16_t)fr[b];
        t->here[fr[a]] = t->here[fr[b]] = 0;
    }
    memcpy(t->here, save, sizeof save);
    memcpy(t->plan, order, sizeof order[0] * (size_t)(2 * np));
    t->nplan = 2 * np;
    return 0;
}

int tiles_new(Tiles *t, int layout, uint32_t seed) {
    if (layout < 0 || layout >= TILES_LAYOUTS) return -1;
    memset(t, 0, sizeof *t);
    t->layout = layout;
    t->seed = seed;
    for (const Block *b = LAYOUTS[layout]; b->z >= 0; b++)
        for (int j = 0; j < b->rows; j++)
            for (int i = 0; i < b->cols && t->n < TILES_MAX; i++) {
                t->x[t->n] = (int8_t)(b->x + 2 * i);
                t->y[t->n] = (int8_t)(b->y + 2 * j);
                t->z[t->n] = b->z;
                t->kind[t->n] = (uint8_t)(t->n / 4 % TILES_KINDS); /* four of each */
                t->here[t->n] = 1;
                t->n++;
            }
    t->left = t->n;
    uint32_t r = seed;
    while (deal_kinds(t, &r)) {}
    return 0;
}

int tiles_shuffle(Tiles *t) {
    if (!t->left) return -1;
    uint32_t r = t->seed ^ (0xA511E9B3u * (uint32_t)(++t->shuffles)) ^ (uint32_t)t->left;
    for (int tries = 0; deal_kinds(t, &r); tries++)
        if (tries > 1000) return -1; /* no way to clear what is left, whatever the kinds */
    return 0;
}

/* ---- play ---- */

int tiles_match(Tiles *t, int i, int j) {
    if (i == j || !tiles_free(t, i) || !tiles_free(t, j) || t->kind[i] != t->kind[j]) return -1;
    t->here[i] = t->here[j] = 0;
    t->left -= 2;
    t->taken[t->ntaken++] = (int16_t)i;
    t->taken[t->ntaken++] = (int16_t)j;
    return 0;
}

int tiles_undo(Tiles *t) {
    if (t->ntaken < 2) return -1;
    t->here[t->taken[--t->ntaken]] = 1;
    t->here[t->taken[--t->ntaken]] = 1;
    t->left += 2;
    return 0;
}

int tiles_hint(const Tiles *t, int *i, int *j) {
    for (int a = 0; a < t->n; a++) {
        if (!tiles_free(t, a)) continue;
        for (int b = a + 1; b < t->n; b++)
            if (t->kind[b] == t->kind[a] && tiles_free(t, b)) {
                *i = a, *j = b;
                return 0;
            }
    }
    return -1;
}

int tiles_moves(const Tiles *t) {
    int per[TILES_KINDS] = {0}, n = 0;
    for (int a = 0; a < t->n; a++)
        if (tiles_free(t, a)) per[t->kind[a]]++;
    for (int k = 0; k < TILES_KINDS; k++) n += per[k] * (per[k] - 1) / 2;
    return n;
}

int tiles_state(const Tiles *t) {
    if (!t->left) return TILES_WON;
    return tiles_moves(t) ? TILES_PLAYING : TILES_STUCK;
}

/* ---- text ---- */

void tiles_kind_name(int kind, char out[3]) {
    static const char *const HONOURS[9] = {"WE", "WS", "WW", "WN", "DR", "DG", "DW", "SU", "MO"};
    if (kind < 0 || kind >= TILES_KINDS) {
        out[0] = out[1] = '?', out[2] = 0;
    } else if (kind < TILES_WINDS) {
        out[0] = "CBN"[kind / 9];
        out[1] = (char)('1' + kind % 9);
        out[2] = 0;
    } else {
        out[0] = HONOURS[kind - TILES_WINDS][0];
        out[1] = HONOURS[kind - TILES_WINDS][1];
        out[2] = 0;
    }
}

typedef struct {
    char *out;
    size_t cap, len;
} Text;

static void put(Text *t, const char *s) {
    for (; *s; s++, t->len++)
        if (t->len + 1 < t->cap) t->out[t->len] = *s;
}

static void put_num(Text *t, unsigned long v) {
    char b[24], r[24];
    int n = 0;
    do b[n++] = (char)('0' + v % 10);
    while (v /= 10);
    for (int i = 0; i < n; i++) r[i] = b[n - 1 - i];
    r[n] = 0;
    put(t, r);
}

size_t tiles_text(const Tiles *t, char *out, size_t cap) {
    static const char *const STATE[3] = {"playing", "cleared", "stuck: shuffle or undo"};
    Text x = {out, cap, 0};
    put(&x, "Tiles ");
    put(&x, TILES_LAYOUT_NAMES[t->layout]);
    put(&x, "  game ");
    put_num(&x, t->seed);
    put(&x, "  left ");
    put_num(&x, (unsigned long)t->left);
    put(&x, "  moves ");
    put_num(&x, (unsigned long)tiles_moves(t));
    put(&x, "  ");
    put(&x, STATE[tiles_state(t)]);
    put(&x, "\nFree");
    for (int i = 0; i < t->n; i++) {
        if (!tiles_free(t, i)) continue;
        char k[3];
        tiles_kind_name(t->kind[i], k);
        put(&x, " ");
        put_num(&x, (unsigned long)i);
        put(&x, ":");
        put(&x, k);
    }
    put(&x, "\n");
    if (cap) out[x.len < cap ? x.len : cap - 1] = 0;
    return x.len;
}

/* ---- commands ---- */

static void say(char *msg, size_t cap, const char *a) {
    if (!cap) return;
    size_t n = 0;
    for (; a[n] && n + 1 < cap; n++) msg[n] = a[n];
    msg[n] = 0;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static size_t word(const char **p, const char **start) {
    while (**p == ' ' || **p == '\t') (*p)++;
    *start = *p;
    while (**p && **p != ' ' && **p != '\t') (*p)++;
    return (size_t)(*p - *start);
}

static int is(const char *w, size_t n, const char *s) {
    size_t k = 0;
    for (; k < n && s[k]; k++)
        if (lower(w[k]) != lower(s[k])) return 0;
    return k == n && !s[k];
}

static int number(const char *w, size_t n, uint32_t *v) {
    if (!n || n > 10) return -1;
    uint64_t x = 0;
    for (size_t i = 0; i < n; i++) {
        if (w[i] < '0' || w[i] > '9') return -1;
        x = x * 10 + (uint64_t)(w[i] - '0');
    }
    if (x > 0xFFFFFFFFu) return -1;
    *v = (uint32_t)x;
    return 0;
}

int tiles_command(Tiles *t, const char *line, char *msg, size_t cap) {
    const char *p = line, *w[4], *e;
    size_t n[4];
    int nw = 0;
    say(msg, cap, "");
    while (nw < 4 && (n[nw] = word(&p, &w[nw]))) nw++;
    if (!nw) return 0;
    if (word(&p, &e)) {
        say(msg, cap, "too many words");
        return -1;
    }
    if (is(w[0], n[0], "new")) {
        int layout = -1;
        uint32_t seed = t->seed + 1;
        for (int k = 0; nw >= 2 && k < TILES_LAYOUTS; k++)
            if (is(w[1], n[1], TILES_LAYOUT_NAMES[k])) layout = k;
        if (layout < 0 || nw > 3 || (nw == 3 && number(w[2], n[2], &seed))) {
            say(msg, cap, "new pyramid|towers|cross [SEED]");
            return -1;
        }
        tiles_new(t, layout, seed);
        return 0;
    }
    if (is(w[0], n[0], "match")) {
        uint32_t a, b;
        if (nw != 3 || number(w[1], n[1], &a) || number(w[2], n[2], &b) || a >= (uint32_t)t->n || b >= (uint32_t)t->n) {
            say(msg, cap, "match I J: two tile numbers, as the free list shows them");
            return -1;
        }
        if (a == b) say(msg, cap, "two different tiles");
        else if (!t->here[a] || !t->here[b]) say(msg, cap, "that tile is gone already");
        else if (!tiles_free(t, (int)a) || !tiles_free(t, (int)b)) say(msg, cap, "that tile is not free");
        else if (t->kind[a] != t->kind[b]) say(msg, cap, "not the same kind");
        else {
            tiles_match(t, (int)a, (int)b);
            return 0;
        }
        return -1;
    }
    if (nw != 1) {
        say(msg, cap, "too many words");
        return -1;
    }
    if (is(w[0], n[0], "hint")) {
        int a, b;
        if (tiles_hint(t, &a, &b)) {
            say(msg, cap, t->left ? "no pair can go: shuffle or undo" : "the table is clear");
            return -1;
        }
        char s[24] = "match ", k[12];
        int len = 6;
        for (int q = 0; q < 2; q++) {
            int v = q ? b : a, nd = 0;
            do k[nd++] = (char)('0' + v % 10);
            while (v /= 10);
            while (nd) s[len++] = k[--nd];
            s[len++] = q ? 0 : ' ';
        }
        say(msg, cap, s);
        return 0;
    }
    if (is(w[0], n[0], "undo")) {
        if (tiles_undo(t)) {
            say(msg, cap, "nothing to undo");
            return -1;
        }
        return 0;
    }
    if (is(w[0], n[0], "shuffle")) {
        if (tiles_shuffle(t)) {
            say(msg, cap, t->left ? "no deal of these could be cleared" : "the table is clear");
            return -1;
        }
        return 0;
    }
    say(msg, cap, "unknown: try new, match, hint, undo, shuffle");
    return -1;
}
