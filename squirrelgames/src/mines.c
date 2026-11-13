/* mines: the minesweeper-style game (see mines.h). Original code. */
#include "mines.h"

#include <string.h>

void mines_size_of(int which, int *w, int *h, int *mines) {
    static const int SIZES[3][3] = {{9, 9, 10}, {16, 16, 40}, {30, 16, 99}};
    if (which < 0 || which > 2) which = 0;
    *w = SIZES[which][0];
    *h = SIZES[which][1];
    *mines = SIZES[which][2];
}

int mines_new(Mines *m, int w, int h, int mines, uint32_t seed) {
    if (w < 2 || h < 2 || w > MINES_MAX_W || h > MINES_MAX_H || mines < 1 || mines > w * h - 9) return -1;
    memset(m, 0, sizeof *m);
    m->w = w;
    m->h = h;
    m->mines = mines;
    m->seed = seed;
    return 0;
}

static int on(const Mines *m, int x, int y) { return x >= 0 && y >= 0 && x < m->w && y < m->h; }
static uint8_t *at(Mines *m, int x, int y) { return &m->sq[y * m->w + x]; }
uint8_t mines_at(const Mines *m, int x, int y) { return on(m, x, y) ? m->sq[y * m->w + x] : 0; }

static uint32_t next_random(uint32_t *s) {
    uint32_t z = (*s += 0x9E3779B9u);
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

static int near(int x0, int y0, int x1, int y1) {
    int dx = x0 - x1, dy = y0 - y1;
    return dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1;
}

/* the mines, at random from the seed, none on (fx, fy) or next to it */
static void lay(Mines *m, int fx, int fy) {
    uint32_t r = m->seed;
    int placed = 0, n = m->w * m->h;
    /* squares the first click keeps clear: as many as are on the field */
    int clear = 0;
    for (int y = fy - 1; y <= fy + 1; y++)
        for (int x = fx - 1; x <= fx + 1; x++) clear += on(m, x, y);
    int room = n - clear;
    while (placed < m->mines) {
        int k = (int)(next_random(&r) % (uint32_t)room); /* the k-th free square that may hold one */
        for (int i = 0; i < n; i++) {
            int x = i % m->w, y = i / m->w;
            if ((m->sq[i] & MINE_HERE) || near(x, y, fx, fy)) continue;
            if (k-- == 0) {
                m->sq[i] |= MINE_HERE;
                placed++;
                room--;
                break;
            }
        }
    }
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) {
            int c = 0;
            for (int j = -1; j <= 1; j++)
                for (int i = -1; i <= 1; i++) c += (mines_at(m, x + i, y + j) & MINE_HERE) != 0;
            *at(m, x, y) = (uint8_t)((*at(m, x, y) & ~15) | c);
        }
    m->laid = 1;
}

static void end(Mines *m, int won) {
    m->state = won ? MINES_WON : MINES_LOST;
    for (int i = 0; i < m->w * m->h; i++) {
        if (!(m->sq[i] & MINE_HERE)) continue;
        if (won) m->sq[i] = (uint8_t)((m->sq[i] | MINE_FLAG) & ~MINE_ASK);
    }
    if (won) m->flags = m->mines;
}

/* open one square and, from a square with no mines around, the rest of
 * its clear region (an explicit list, no recursion) */
static int flood(Mines *m, int x, int y) {
    static uint16_t todo[MINES_MAX_W * MINES_MAX_H];
    int top = 0, opened = 0;
    uint8_t *s = at(m, x, y);
    if (*s & (MINE_OPEN | MINE_FLAG)) return 0;
    *s = (uint8_t)((*s | MINE_OPEN) & ~MINE_ASK);
    opened++;
    todo[top++] = (uint16_t)(y * m->w + x);
    while (top) {
        int i = todo[--top], cx = i % m->w, cy = i / m->w;
        if (m->sq[i] & 15) continue;
        for (int j = -1; j <= 1; j++)
            for (int k = -1; k <= 1; k++) {
                int nx = cx + k, ny = cy + j;
                if (!on(m, nx, ny)) continue;
                uint8_t *q = at(m, nx, ny);
                if (*q & (MINE_OPEN | MINE_FLAG | MINE_HERE)) continue;
                *q = (uint8_t)((*q | MINE_OPEN) & ~MINE_ASK);
                opened++;
                todo[top++] = (uint16_t)(ny * m->w + nx);
            }
    }
    m->opened += opened;
    return opened;
}

int mines_open(Mines *m, int x, int y) {
    if (m->state != MINES_PLAYING || !on(m, x, y)) return 0;
    uint8_t *s = at(m, x, y);
    if (*s & (MINE_OPEN | MINE_FLAG)) return 0;
    if (!m->laid) lay(m, x, y);
    if (*s & MINE_HERE) {
        *s |= MINE_OPEN;
        m->boom_x = x;
        m->boom_y = y;
        end(m, 0);
        return -1;
    }
    int n = flood(m, x, y);
    if (m->opened == m->w * m->h - m->mines) end(m, 1);
    return n;
}

int mines_chord(Mines *m, int x, int y) {
    if (m->state != MINES_PLAYING || !on(m, x, y)) return 0;
    uint8_t s = *at(m, x, y);
    if (!(s & MINE_OPEN) || !(s & 15)) return 0;
    int flags = 0;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) flags += (mines_at(m, x + i, y + j) & MINE_FLAG) != 0;
    if (flags != (s & 15)) return 0;
    int total = 0;
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            int r = mines_open(m, x + i, y + j);
            if (r < 0) return -1;
            total += r;
        }
    return total;
}

int mines_mark(Mines *m, int x, int y, int ask) {
    if (m->state != MINES_PLAYING || !on(m, x, y)) return -1;
    uint8_t *s = at(m, x, y);
    if (*s & MINE_OPEN) return -1;
    if (*s & MINE_FLAG) {
        *s = (uint8_t)((*s & ~MINE_FLAG) | (ask ? MINE_ASK : 0));
        m->flags--;
    } else if (*s & MINE_ASK) {
        *s &= (uint8_t)~MINE_ASK;
    } else {
        *s |= MINE_FLAG;
        m->flags++;
    }
    return 0;
}

int mines_left(const Mines *m) { return m->mines - m->flags; }

size_t mines_text(const Mines *m, char *out, size_t cap) {
    size_t len = 0;
    for (int y = 0; y < m->h; y++) {
        for (int x = 0; x <= m->w; x++) {
            char c = '\n';
            if (x < m->w) {
                uint8_t s = mines_at(m, x, y);
                int shown = m->state == MINES_LOST && (s & MINE_HERE);
                if (shown && x == m->boom_x && y == m->boom_y) c = 'X';
                else if (shown && !(s & MINE_FLAG)) c = '*';
                else if (s & MINE_FLAG) c = 'F';
                else if (s & MINE_ASK) c = '?';
                else if (!(s & MINE_OPEN)) c = '.';
                else c = (s & 15) ? (char)('0' + (s & 15)) : ' ';
            }
            if (len + 1 < cap) out[len] = c;
            len++;
        }
    }
    if (cap) out[len < cap ? len : cap - 1] = 0;
    return len;
}

/* ---- commands ---- */

static void say(char *msg, size_t cap, const char *a) {
    if (!cap) return;
    size_t n = 0;
    while (a[n] && n + 1 < cap) {
        msg[n] = a[n];
        n++;
    }
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
        if (lower(w[k]) != s[k]) return 0;
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

int mines_command(Mines *m, const char *line, char *msg, size_t cap) {
    const char *p = line, *w[6], *e;
    size_t n[6];
    int nw = 0;
    say(msg, cap, "");
    while (nw < 6 && (n[nw] = word(&p, &w[nw]))) nw++;
    if (!nw) return 0;
    if (word(&p, &e)) {
        say(msg, cap, "too many words");
        return -1;
    }
    uint32_t v[5] = {0};
    if (is(w[0], n[0], "new")) {
        int which = nw >= 2 ? (is(w[1], n[1], "small") ? 0 : is(w[1], n[1], "medium") ? 1 : is(w[1], n[1], "large") ? 2 : -1) : -1;
        uint32_t seed = m->seed + 1;
        if (which >= 0 && nw <= 3 && (nw < 3 || !number(w[2], n[2], &seed))) {
            int fw, fh, fm;
            mines_size_of(which, &fw, &fh, &fm);
            mines_new(m, fw, fh, fm, seed);
            return 0;
        }
        int ok = nw >= 4 && nw <= 5;
        for (int k = 1; ok && k < nw; k++) ok = !number(w[k], n[k], &v[k]);
        if (ok && v[1] <= 100 && v[2] <= 100 && v[3] <= 10000 &&
            !mines_new(m, (int)v[1], (int)v[2], (int)v[3], nw == 5 ? v[4] : seed))
            return 0;
        say(msg, cap, "new small|medium|large [SEED], or new W H MINES [SEED]: up to 40 x 24, and room for a clear start");
        return -1;
    }
    int open = is(w[0], n[0], "open"), flag = is(w[0], n[0], "flag"), chord = is(w[0], n[0], "chord");
    if (!open && !flag && !chord) {
        say(msg, cap, "unknown: try new, open, flag, chord");
        return -1;
    }
    if (nw != 3 || number(w[1], n[1], &v[1]) || number(w[2], n[2], &v[2]) || v[1] >= (uint32_t)m->w ||
        v[2] >= (uint32_t)m->h) {
        say(msg, cap, open ? "open X Y: a square of the field" : flag ? "flag X Y: a square of the field" : "chord X Y: a square of the field");
        return -1;
    }
    if (m->state != MINES_PLAYING) {
        say(msg, cap, m->state == MINES_WON ? "the game is won: new for another" : "the game is lost: new for another");
        return -1;
    }
    int x = (int)v[1], y = (int)v[2];
    if (flag) {
        if (mines_mark(m, x, y, 0)) {
            say(msg, cap, "that square is open");
            return -1;
        }
        return 0;
    }
    uint8_t s = mines_at(m, x, y);
    if (open && (s & (MINE_OPEN | MINE_FLAG))) {
        say(msg, cap, s & MINE_FLAG ? "that square is flagged" : "that square is open already");
        return -1;
    }
    if (chord && (!(s & MINE_OPEN) || !(s & 15))) {
        say(msg, cap, "a chord goes on an open number");
        return -1;
    }
    if (open) mines_open(m, x, y);
    else mines_chord(m, x, y);
    return 0;
}
