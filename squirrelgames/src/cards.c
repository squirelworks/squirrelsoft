/* cards: Klondike and FreeCell rules (see cards.h). Original code. */
#include "cards.h"

#include <string.h>

/* ---- deals ---- */

static uint32_t next_random(uint32_t *s) { /* a 32-bit mix of a counter (splitmix style) */
    uint32_t z = (*s += 0x9E3779B9u);
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

static void push(CardPile *p, uint8_t c) { p->c[p->n++] = c; }
static uint8_t top(const CardPile *p) { return p->n ? p->c[p->n - 1] : 0; }

void game_new(Game *g, int kind, int draw, uint32_t seed) {
    GameState *s = &g->s;
    memset(s, 0, sizeof *s);
    s->kind = kind == GAME_FREECELL ? GAME_FREECELL : GAME_KLONDIKE;
    s->draw = draw == 3 ? 3 : 1;
    s->seed = seed;
    g->nundo = 0;
    uint8_t deck[52];
    for (int i = 0; i < 52; i++) deck[i] = CARD(i % 13 + 1, i / 13);
    uint32_t r = seed;
    for (int i = 51; i > 0; i--) { /* Fisher-Yates */
        int j = (int)(next_random(&r) % (uint32_t)(i + 1));
        uint8_t t = deck[i];
        deck[i] = deck[j];
        deck[j] = t;
    }
    int k = 0;
    if (s->kind == GAME_KLONDIKE) {
        for (int col = 0; col < 7; col++)
            for (int i = 0; i <= col; i++) push(&s->pile[PILE_TAB + col], (uint8_t)(deck[k++] | (i == col ? CARD_UP : 0)));
        while (k < 52) push(&s->pile[PILE_STOCK], deck[k++]);
    } else {
        for (; k < 52; k++) push(&s->pile[PILE_TAB + k % 8], (uint8_t)(deck[k] | CARD_UP));
    }
}

int game_columns(const GameState *s) { return s->kind == GAME_FREECELL ? 8 : 7; }

int game_won(const GameState *s) {
    for (int f = 0; f < 4; f++)
        if (s->pile[PILE_FOUND + f].n != 13) return 0;
    return 1;
}

/* ---- the rules ---- */

static int is_found(int p) { return p >= PILE_FOUND && p < PILE_FOUND + 4; }
static int is_tab(const GameState *s, int p) { return p >= PILE_TAB && p < PILE_TAB + game_columns(s); }
static int is_cell(const GameState *s, int p) { return s->kind == GAME_FREECELL && p >= PILE_CELL && p < PILE_CELL + 4; }
static int exists(const GameState *s, int p) {
    if (p == PILE_STOCK || p == PILE_WASTE) return s->kind == GAME_KLONDIKE;
    return is_found(p) || is_tab(s, p) || is_cell(s, p);
}

/* b may lie on a in a column: one rank down, the other colour */
static int builds_on(uint8_t a, uint8_t b) { return CARD_RANK(b) + 1 == CARD_RANK(a) && CARD_RED(a) != CARD_RED(b); }

int game_run(const GameState *s, int pile) {
    if (pile < 0 || pile >= PILE_COUNT) return 0;
    const CardPile *p = &s->pile[pile];
    if (!p->n || !(top(p) & CARD_UP)) return 0;
    int k = 1;
    while (k < p->n && (p->c[p->n - k - 1] & CARD_UP) && builds_on(p->c[p->n - k - 1], p->c[p->n - k])) k++;
    return k;
}

int game_most(const GameState *s, int to) {
    if (s->kind != GAME_FREECELL) return 52;
    int cells = 0, empty = 0;
    for (int i = 0; i < 4; i++) cells += !s->pile[PILE_CELL + i].n;
    for (int i = 0; i < 8; i++) empty += !s->pile[PILE_TAB + i].n && PILE_TAB + i != to;
    return (cells + 1) << empty;
}

int game_can_move(const GameState *s, int from, int count, int to) {
    if (from == to || count < 1 || !exists(s, from) || !exists(s, to)) return -1;
    if (from == PILE_STOCK || to == PILE_STOCK || to == PILE_WASTE) return -1;
    if (is_found(from) && is_found(to)) return -1; /* (an ace from one empty foundation to another) */
    const CardPile *src = &s->pile[from], *dst = &s->pile[to];
    if (count > src->n) return -1;
    if (!is_tab(s, from) && count != 1) return -1;
    if (is_tab(s, from) && count > game_run(s, from)) return -1; /* face up and in order */
    if (!(top(src) & CARD_UP)) return -1;
    uint8_t first = src->c[src->n - count];
    if (is_found(to)) {
        if (count != 1) return -1;
        if (!dst->n) return CARD_RANK(first) == 1 ? 0 : -1;
        return CARD_SUIT(top(dst)) == CARD_SUIT(first) && CARD_RANK(top(dst)) + 1 == CARD_RANK(first) ? 0 : -1;
    }
    if (is_cell(s, to)) return count == 1 && !dst->n ? 0 : -1;
    /* a column */
    if (count > game_most(s, to)) return -1;
    if (!dst->n) return s->kind == GAME_FREECELL || CARD_RANK(first) == 13 ? 0 : -1;
    return (top(dst) & CARD_UP) && builds_on(top(dst), first) ? 0 : -1;
}

static void save(Game *g) {
    if (g->nundo == GAME_UNDO) {
        memmove(&g->undo[0], &g->undo[1], (GAME_UNDO - 1) * sizeof g->undo[0]);
        g->nundo--;
    }
    g->undo[g->nundo++] = g->s;
}

/* the move itself, scored, with no undo state */
static void apply(GameState *s, int from, int count, int to) {
    CardPile *src = &s->pile[from], *dst = &s->pile[to];
    memcpy(dst->c + dst->n, src->c + src->n - count, (size_t)count);
    dst->n = (uint8_t)(dst->n + count);
    src->n = (uint8_t)(src->n - count);
    s->moves++;
    if (s->kind != GAME_KLONDIKE) return;
    if (is_found(to) && !is_found(from)) s->score += 10;
    if (from == PILE_WASTE && is_tab(s, to)) s->score += 5;
    if (is_found(from) && is_tab(s, to)) s->score -= 15;
    if (src->n && !(top(src) & CARD_UP) && is_tab(s, from)) { /* the next card turns up */
        src->c[src->n - 1] |= CARD_UP;
        s->score += 5;
    }
    if (s->score < 0) s->score = 0;
}

int game_move(Game *g, int from, int count, int to) {
    if (game_can_move(&g->s, from, count, to)) return -1;
    save(g);
    apply(&g->s, from, count, to);
    return 0;
}

int game_draw(Game *g) {
    GameState *s = &g->s;
    if (s->kind != GAME_KLONDIKE) return -1;
    CardPile *stock = &s->pile[PILE_STOCK], *waste = &s->pile[PILE_WASTE];
    if (!stock->n && !waste->n) return -1;
    save(g);
    if (!stock->n) { /* the waste turned over: the stock again */
        while (waste->n) push(stock, (uint8_t)(waste->c[--waste->n] & ~CARD_UP));
        s->score -= s->draw == 1 ? 100 : 20;
        if (s->score < 0) s->score = 0;
    } else {
        for (int k = 0; k < s->draw && stock->n; k++) push(waste, (uint8_t)(stock->c[--stock->n] | CARD_UP));
    }
    s->moves++;
    return 0;
}

int game_smart(Game *g, int from, int count) {
    const GameState *s = &g->s;
    if (count == 1)
        for (int f = 0; f < 4; f++)
            if (!game_can_move(s, from, 1, PILE_FOUND + f)) return game_move(g, from, 1, PILE_FOUND + f);
    int cols = game_columns(s);
    for (int pass = 0; pass < 2; pass++) /* columns with cards, then empty ones */
        for (int t = 0; t < cols; t++) {
            int to = PILE_TAB + t;
            if ((s->pile[to].n == 0) != pass) continue;
            /* a whole column onto an empty one changes nothing */
            if (pass && is_tab(s, from) && count == s->pile[from].n) continue;
            if (!game_can_move(s, from, count, to)) return game_move(g, from, count, to);
        }
    if (count == 1 && !is_cell(s, from))
        for (int c = 0; c < 4; c++)
            if (!game_can_move(s, from, 1, PILE_CELL + c)) return game_move(g, from, 1, PILE_CELL + c);
    return -1;
}

/* how high the suit's foundation is (0: none home) */
static int home_height(const GameState *s, int suit) {
    for (int f = 0; f < 4; f++) {
        const CardPile *p = &s->pile[PILE_FOUND + f];
        if (p->n && CARD_SUIT(top(p)) == suit) return CARD_RANK(top(p));
    }
    return 0;
}

/* nothing in play could ever want to go on c any more */
static int safe_home(const GameState *s, uint8_t c) {
    int r = CARD_RANK(c);
    if (r <= 2) return 1;
    int red = CARD_RED(c);
    for (int suit = 0; suit < 4; suit++) {
        int other = suit == SUIT_DIAMONDS || suit == SUIT_HEARTS ? !red : red;
        if (other && home_height(s, suit) < r - 1) return 0;
    }
    return 1;
}

int game_auto(Game *g, int all) {
    GameState before = g->s;
    int moved = 0;
    for (int again = 1; again;) {
        again = 0;
        for (int p = 0; p < PILE_COUNT; p++) {
            if (is_found(p) || p == PILE_STOCK || !exists(&g->s, p) || !g->s.pile[p].n) continue;
            if (!all && !safe_home(&g->s, top(&g->s.pile[p]))) continue;
            for (int f = 0; f < 4; f++)
                if (!game_can_move(&g->s, p, 1, PILE_FOUND + f)) {
                    apply(&g->s, p, 1, PILE_FOUND + f);
                    moved++;
                    again = 1;
                    break;
                }
        }
    }
    if (moved) { /* one step: the state before them all */
        GameState after = g->s;
        g->s = before;
        save(g);
        g->s = after;
    }
    return moved;
}

int game_can_finish(const GameState *s) {
    if (s->kind != GAME_KLONDIKE || s->pile[PILE_STOCK].n || s->pile[PILE_WASTE].n || game_won(s)) return 0;
    for (int t = 0; t < 7; t++) {
        const CardPile *p = &s->pile[PILE_TAB + t];
        for (int i = 0; i < p->n; i++)
            if (!(p->c[i] & CARD_UP)) return 0;
    }
    return 1;
}

int game_can_undo(const Game *g) { return g->nundo > 0; }

int game_undo(Game *g) {
    if (!g->nundo) return -1;
    g->s = g->undo[--g->nundo];
    return 0;
}

/* ---- names and text ---- */

void game_card_name(uint8_t c, char out[3]) {
    static const char RANKS[] = "?A23456789TJQK", SUITS[] = "CDHS";
    if (!(c & CARD_UP)) {
        out[0] = out[1] = '#';
    } else {
        out[0] = RANKS[CARD_RANK(c) <= 13 ? CARD_RANK(c) : 0];
        out[1] = SUITS[CARD_SUIT(c)];
    }
    out[2] = 0;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

int game_pile_of(const char *w, size_t n) {
    if (n == 1 && lower(w[0]) == 's') return PILE_STOCK;
    if (n == 1 && lower(w[0]) == 'w') return PILE_WASTE;
    if (n != 2 || w[1] < '1' || w[1] > '8') return -1;
    int k = w[1] - '1';
    switch (lower(w[0])) {
    case 'f': return k < 4 ? PILE_FOUND + k : -1;
    case 'c': return k < 4 ? PILE_CELL + k : -1;
    case 't': return PILE_TAB + k;
    default: return -1;
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
static void put_int(Text *t, long v) {
    char b[24];
    int k = 0;
    if (v < 0) put(t, "-"), v = -v;
    do b[k++] = (char)('0' + v % 10);
    while (v /= 10);
    char r[24];
    for (int i = 0; i < k; i++) r[i] = b[k - 1 - i];
    r[k] = 0;
    put(t, r);
}
static void put_cards(Text *t, const CardPile *p, int from) {
    for (int i = from; i < p->n; i++) {
        char nm[3];
        game_card_name(p->c[i], nm);
        put(t, " ");
        put(t, nm);
    }
}
static void put_top(Text *t, const CardPile *p) {
    char nm[3] = "--";
    if (p->n) game_card_name(p->c[p->n - 1], nm);
    put(t, " ");
    put(t, nm);
}

size_t game_text(const GameState *s, char *out, size_t cap) {
    Text t = {out, cap, 0};
    if (s->kind == GAME_KLONDIKE) {
        put(&t, s->draw == 3 ? "Klondike, draw 3" : "Klondike, draw 1");
        put(&t, "  seed ");
        put_int(&t, (long)s->seed);
        put(&t, "  score ");
        put_int(&t, s->score);
    } else {
        put(&t, "FreeCell  seed ");
        put_int(&t, (long)s->seed);
    }
    put(&t, "  moves ");
    put_int(&t, s->moves);
    put(&t, game_won(s) ? "  won\n" : "\n");
    if (s->kind == GAME_KLONDIKE) {
        put(&t, "S: ");
        put_int(&t, s->pile[PILE_STOCK].n);
        put(&t, "  W:");
        const CardPile *w = &s->pile[PILE_WASTE];
        put_cards(&t, w, w->n > 3 ? w->n - 3 : 0);
        if (!w->n) put(&t, " --");
    } else {
        put(&t, "C:");
        for (int c = 0; c < 4; c++) put_top(&t, &s->pile[PILE_CELL + c]);
    }
    put(&t, "  F:");
    for (int f = 0; f < 4; f++) put_top(&t, &s->pile[PILE_FOUND + f]);
    put(&t, "\n");
    for (int c = 0; c < game_columns(s); c++) {
        put(&t, "T");
        put_int(&t, c + 1);
        put(&t, ":");
        put_cards(&t, &s->pile[PILE_TAB + c], 0);
        put(&t, "\n");
    }
    if (cap) out[t.len < cap ? t.len : cap - 1] = 0;
    return t.len;
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

int game_command(Game *g, const char *line, char *msg, size_t cap) {
    const char *p = line, *w, *a, *b, *c, *e;
    size_t n = word(&p, &w);
    say(msg, cap, "");
    if (!n) return 0;
    size_t na = word(&p, &a), nb = word(&p, &b), nc = word(&p, &c);
    if (word(&p, &e)) {
        say(msg, cap, "too many words");
        return -1;
    }
    if (is(w, n, "new")) {
        uint32_t seed = g->s.seed + 1;
        int kind = is(a, na, "freecell") ? GAME_FREECELL : GAME_KLONDIKE;
        if (!(is(a, na, "klondike") || is(a, na, "klondike3") || is(a, na, "freecell")) || nc ||
            (nb && number(b, nb, &seed))) {
            say(msg, cap, "new klondike|klondike3|freecell [SEED]");
            return -1;
        }
        game_new(g, kind, is(a, na, "klondike3") ? 3 : 1, seed);
        return 0;
    }
    if (is(w, n, "draw") || is(w, n, "undo") || is(w, n, "auto") || is(w, n, "finish")) {
        if (na) {
            say(msg, cap, "that takes nothing after it");
            return -1;
        }
        if (is(w, n, "draw")) {
            if (game_draw(g)) {
                say(msg, cap, g->s.kind == GAME_KLONDIKE ? "nothing left to draw" : "draw: FreeCell has no stock");
                return -1;
            }
        } else if (is(w, n, "undo")) {
            if (game_undo(g)) {
                say(msg, cap, "nothing to undo");
                return -1;
            }
        } else if (is(w, n, "finish") && !game_can_finish(&g->s) && g->s.kind == GAME_KLONDIKE) {
            say(msg, cap, "finish: only with the stock gone and every card face up");
            return -1;
        } else if (!game_auto(g, is(w, n, "finish"))) {
            say(msg, cap, "no card can go home");
            return -1;
        }
        return 0;
    }
    int smart = is(w, n, "smart");
    if (is(w, n, "move") || smart) {
        int from = game_pile_of(a, na), to = smart ? -2 : game_pile_of(b, nb);
        uint32_t k = 0;
        const char *kw = smart ? b : c;
        size_t kn = smart ? nb : nc;
        if (from < 0 || to == -1 || (kn && (number(kw, kn, &k) || k < 1 || k > 52)) || (smart && nc)) {
            say(msg, cap, smart ? "smart FROM [N]: piles are s, w, f1-f4, t1-t8, c1-c4" : "move FROM TO [N]: piles are s, w, f1-f4, t1-t8, c1-c4");
            return -1;
        }
        int count = (int)k;
        if (!count) count = 1;
        if (!kn && from >= PILE_TAB && from < PILE_TAB + 8 && !smart) { /* the longest run that may go */
            for (int m = game_run(&g->s, from); m >= 1; m--)
                if (!game_can_move(&g->s, from, m, to)) {
                    count = m;
                    break;
                }
        }
        if (!kn && smart && from >= PILE_TAB && from < PILE_TAB + 8) {
            for (int m = game_run(&g->s, from); m >= 1; m--) {
                if (!game_smart(g, from, m)) return 0;
            }
            say(msg, cap, "nowhere for it to go");
            return -1;
        }
        if (smart ? game_smart(g, from, count) : game_move(g, from, count, to)) {
            say(msg, cap, smart ? "nowhere for it to go" : "the rules do not allow that move");
            return -1;
        }
        return 0;
    }
    say(msg, cap, "unknown: try new, draw, move, smart, auto, finish, undo");
    return -1;
}
