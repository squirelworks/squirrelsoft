/* hearts: Hearts rules and the computer's players (see hearts.h). Original code. */
#include "hearts.h"

#include <string.h>

const char *const HEARTS_NAMES[4] = {"You", "West", "North", "East"};

static uint32_t next_random(uint32_t *s) { /* splitmix style, as cards.c */
    uint32_t z = (*s += 0x9E3779B9u);
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

/* ---- cards ---- */

static int value(uint8_t c) { return CARD_RANK(c) == 1 ? 14 : CARD_RANK(c); } /* aces high */
static int suit_of(uint8_t c) { return CARD_SUIT(c); }
static int bit(uint8_t c) { return CARD_SUIT(c) * 13 + CARD_RANK(c) - 1; }
static int sort_key(uint8_t c) { /* clubs, diamonds, spades, hearts: the colours alternate */
    static const int ORDER[4] = {0, 1, 3, 2};
    return ORDER[suit_of(c)] * 16 + value(c);
}
#define QUEEN_SPADES CARD(12, SUIT_SPADES)
#define TWO_CLUBS CARD(2, SUIT_CLUBS)

int hearts_points(uint8_t c) {
    c &= (uint8_t)~CARD_UP;
    return suit_of(c) == SUIT_HEARTS ? 1 : c == QUEEN_SPADES ? 13 : 0;
}

static int find(const Hearts *h, int p, uint8_t c) {
    for (int i = 0; i < h->n[p]; i++)
        if (h->hand[p][i] == c) return i;
    return -1;
}

int hearts_has(const Hearts *h, int p, uint8_t card) {
    return p >= 0 && p < 4 && find(h, p, (uint8_t)(card & ~CARD_UP)) >= 0;
}

static void sort_hand(Hearts *h, int p) {
    for (int i = 1; i < h->n[p]; i++)
        for (int j = i; j > 0 && sort_key(h->hand[p][j]) < sort_key(h->hand[p][j - 1]); j--) {
            uint8_t t = h->hand[p][j];
            h->hand[p][j] = h->hand[p][j - 1];
            h->hand[p][j - 1] = t;
        }
}

static void take_out(Hearts *h, int p, uint8_t c) {
    int i = find(h, p, c);
    if (i < 0) return;
    memmove(&h->hand[p][i], &h->hand[p][i + 1], (size_t)(h->n[p] - i - 1));
    h->n[p]--;
}

static int count_suit(const Hearts *h, int p, int suit) {
    int k = 0;
    for (int i = 0; i < h->n[p]; i++) k += suit_of(h->hand[p][i]) == suit;
    return k;
}

/* ---- hands ---- */

int hearts_pass_dir(const Hearts *h) { return h->hand_no % 4; }

int hearts_pass_to(const Hearts *h, int p) {
    static const int STEP[4] = {1, 3, 2, 0}; /* left is the next round the table */
    return (p + STEP[hearts_pass_dir(h)]) % 4;
}

static void start_play(Hearts *h) {
    h->phase = HEARTS_PLAYING;
    for (int p = 0; p < 4; p++)
        if (find(h, p, TWO_CLUBS) >= 0) h->turn = h->leader = p;
}

static void deal(Hearts *h) {
    uint8_t deck[52];
    for (int i = 0; i < 52; i++) deck[i] = CARD(i % 13 + 1, i / 13);
    uint32_t r = h->seed ^ ((uint32_t)h->hand_no * 0x6A09E667u);
    for (int i = 51; i > 0; i--) {
        int j = (int)(next_random(&r) % (uint32_t)(i + 1));
        uint8_t t = deck[i];
        deck[i] = deck[j];
        deck[j] = t;
    }
    for (int p = 0; p < 4; p++) {
        memcpy(h->hand[p], deck + 13 * p, 13);
        h->n[p] = 13;
        sort_hand(h, p);
        h->taken[p] = 0;
        h->trick[p] = h->last[p] = 0;
    }
    memset(h->passed, 0, sizeof h->passed);
    h->ntrick = h->tricks = h->broken = 0;
    h->gone = 0;
    h->last_winner = -1;
    if (hearts_pass_dir(h) == HEARTS_PASS_NONE) start_play(h);
    else h->phase = HEARTS_PASSING;
}

void hearts_new(Hearts *h, uint32_t seed) {
    memset(h, 0, sizeof *h);
    h->seed = seed;
    h->moon = -1;
    deal(h);
}

int hearts_next_hand(Hearts *h) {
    if (h->phase != HEARTS_HAND_OVER) return -1;
    h->hand_no++;
    deal(h);
    return 0;
}

int hearts_pass(Hearts *h, const uint8_t cards[3]) {
    if (h->phase != HEARTS_PASSING) return -1;
    uint8_t c[3];
    for (int i = 0; i < 3; i++) {
        c[i] = (uint8_t)(cards[i] & ~CARD_UP);
        if (find(h, 0, c[i]) < 0) return -1;
        for (int j = 0; j < i; j++)
            if (c[j] == c[i]) return -1;
    }
    memcpy(h->passed[0], c, 3);
    for (int p = 1; p < 4; p++) hearts_ai_pass(h, p, h->passed[p]);
    for (int p = 0; p < 4; p++)
        for (int i = 0; i < 3; i++) take_out(h, p, h->passed[p][i]);
    for (int p = 0; p < 4; p++) {
        int to = hearts_pass_to(h, p);
        for (int i = 0; i < 3; i++) h->hand[to][h->n[to]++] = h->passed[p][i];
    }
    for (int p = 0; p < 4; p++) sort_hand(h, p);
    start_play(h);
    return 0;
}

/* ---- tricks ---- */

static int only_points(const Hearts *h, int p) {
    for (int i = 0; i < h->n[p]; i++)
        if (!hearts_points(h->hand[p][i])) return 0;
    return 1;
}

int hearts_can_play(const Hearts *h, uint8_t card, const char **why) {
    const char *w = NULL;
    int p = h->turn;
    card &= (uint8_t)~CARD_UP;
    if (h->phase != HEARTS_PLAYING) w = h->phase == HEARTS_PASSING ? "pass three cards first" : "the hand is over";
    else if (find(h, p, card) < 0) w = "that card is not in the hand";
    else if (h->ntrick == 0) { /* a lead */
        if (h->tricks == 0 && card != TWO_CLUBS) w = "the two of clubs leads the first trick";
        else if (suit_of(card) == SUIT_HEARTS && !h->broken && count_suit(h, p, SUIT_HEARTS) < h->n[p])
            w = "hearts are not broken yet";
    } else {
        int led = suit_of(h->trick[h->leader]);
        if (suit_of(card) != led && count_suit(h, p, led)) w = "follow suit";
        else if (h->tricks == 0 && hearts_points(card) && !only_points(h, p)) w = "no points on the first trick";
    }
    if (why) *why = w ? w : "";
    return w ? -1 : 0;
}

static void score_hand(Hearts *h) {
    h->moon = -1;
    for (int p = 0; p < 4; p++)
        if (h->taken[p] == 26) h->moon = p;
    int over = 0;
    for (int p = 0; p < 4; p++) {
        h->hand_score[p] = h->moon < 0 ? h->taken[p] : p == h->moon ? 0 : 26;
        h->score[p] += h->hand_score[p];
        over |= h->score[p] >= HEARTS_END;
    }
    h->phase = over ? HEARTS_GAME_OVER : HEARTS_HAND_OVER;
}

int hearts_play(Hearts *h, uint8_t card) {
    card &= (uint8_t)~CARD_UP;
    if (hearts_can_play(h, card, NULL)) return -1;
    int p = h->turn;
    take_out(h, p, card);
    h->trick[p] = card;
    h->ntrick++;
    h->gone |= (uint64_t)1 << bit(card);
    if (suit_of(card) == SUIT_HEARTS) h->broken = 1;
    if (h->ntrick < 4) {
        h->turn = (p + 1) % 4;
        return 0;
    }
    int led = suit_of(h->trick[h->leader]), win = h->leader, pts = 0;
    for (int q = 0; q < 4; q++) {
        uint8_t c = h->trick[q];
        pts += hearts_points(c);
        if (suit_of(c) == led && value(c) > value(h->trick[win])) win = q;
    }
    h->taken[win] += pts;
    memcpy(h->last, h->trick, 4);
    memset(h->trick, 0, 4);
    h->last_winner = win;
    h->ntrick = 0;
    h->tricks++;
    h->turn = h->leader = win;
    if (h->tricks == 13) score_hand(h);
    return 0;
}

int hearts_winner(const Hearts *h) {
    if (h->phase != HEARTS_GAME_OVER) return -1;
    int w = 0;
    for (int p = 1; p < 4; p++)
        if (h->score[p] < h->score[w]) w = p;
    return w;
}

/* ---- the computer's players ---- */

void hearts_ai_pass(const Hearts *h, int p, uint8_t out[3]) {
    int v[13];
    int spades = count_suit(h, p, SUIT_SPADES);
    for (int i = 0; i < h->n[p]; i++) {
        uint8_t c = h->hand[p][i];
        int s = suit_of(c);
        v[i] = value(c);
        if (c == QUEEN_SPADES) v[i] += spades >= 5 ? -20 : 40; /* kept behind many spades, else passed */
        else if (s == SUIT_SPADES && value(c) > 12 && spades < 5) v[i] += 30;
        if (s == SUIT_HEARTS) v[i] += 4;
        if (s != SUIT_SPADES && count_suit(h, p, s) <= 2) v[i] += 6; /* toward a void */
    }
    int used[13] = {0};
    for (int k = 0; k < 3; k++) {
        int best = -1;
        for (int i = 0; i < h->n[p]; i++)
            if (!used[i] && (best < 0 || v[i] > v[best])) best = i;
        used[best] = 1;
        out[k] = h->hand[p][best];
    }
}

uint8_t hearts_ai_card(const Hearts *h) {
    int p = h->turn;
    uint8_t legal[13];
    int nl = 0;
    for (int i = 0; i < h->n[p]; i++)
        if (!hearts_can_play(h, h->hand[p][i], NULL)) legal[nl++] = h->hand[p][i];
    if (nl <= 1) return nl ? legal[0] : 0;
    int queen_out = !(h->gone >> bit(QUEEN_SPADES) & 1) && find(h, p, QUEEN_SPADES) < 0;
    uint8_t best = 0;
    int bv = 0;
    if (h->ntrick == 0) { /* lead low, not hearts, not the queen; spades low to draw her out */
        for (int i = 0; i < nl; i++) {
            uint8_t c = legal[i];
            int k = value(c) * 4;
            if (suit_of(c) == SUIT_HEARTS) k += 30;
            if (c == QUEEN_SPADES) k += 200;
            if (suit_of(c) == SUIT_SPADES && value(c) > 12 && queen_out) k += 100;
            if (suit_of(c) == SUIT_SPADES && value(c) < 12 && queen_out) k -= 6;
            if (!best || k < bv) best = c, bv = k;
        }
        return best;
    }
    int led = suit_of(h->trick[h->leader]), top = 0, pts = 0;
    for (int q = 0; q < 4; q++)
        if (h->trick[q]) {
            pts += hearts_points(h->trick[q]);
            if (suit_of(h->trick[q]) == led && value(h->trick[q]) > top) top = value(h->trick[q]);
        }
    if (suit_of(legal[0]) == led) { /* following: the highest that stays under, else */
        for (int i = 0; i < nl; i++)
            if (value(legal[i]) < top && (!best || value(legal[i]) > value(best))) best = legal[i];
        if (best) return best;
        int last = h->ntrick == 3;
        for (int i = 0; i < nl; i++) { /* it takes the trick: last, the highest; else the lowest; never the queen if not */
            uint8_t c = legal[i];
            int k = last ? -value(c) : value(c);
            if (c == QUEEN_SPADES) k += 100;
            if (!best || k < bv) best = c, bv = k;
        }
        (void)pts;
        return best;
    }
    /* void: the queen, then the spades over her while she is out, then the highest heart, then the highest */
    for (int i = 0; i < nl; i++) {
        uint8_t c = legal[i];
        int k = value(c);
        if (c == QUEEN_SPADES) k += 300;
        else if (suit_of(c) == SUIT_SPADES && value(c) > 12 && queen_out) k += 200;
        else if (suit_of(c) == SUIT_HEARTS) k += 100;
        if (!best || k > bv) best = c, bv = k;
    }
    return best;
}

int hearts_run(Hearts *h) {
    int k = 0;
    while (h->phase == HEARTS_PLAYING && h->turn != 0 && !hearts_play(h, hearts_ai_card(h))) k++;
    return k;
}

/* ---- text ---- */

typedef struct {
    char *out;
    size_t cap, len;
} Text;

static void put(Text *t, const char *s) {
    for (; *s; s++, t->len++)
        if (t->len + 1 < t->cap) t->out[t->len] = *s;
}

static void put_num(Text *t, long v) {
    char b[24];
    int n = 0, neg = v < 0;
    unsigned long u = neg ? 0ul - (unsigned long)v : (unsigned long)v;
    do b[n++] = (char)('0' + u % 10);
    while (u /= 10);
    if (neg) b[n++] = '-';
    char r[24];
    for (int i = 0; i < n; i++) r[i] = b[n - 1 - i];
    r[n] = 0;
    put(t, r);
}

static void put_card(Text *t, uint8_t c) {
    char nm[3] = {"?A23456789TJQK"[CARD_RANK(c) <= 13 ? CARD_RANK(c) : 0], "CDHS"[CARD_SUIT(c)], 0};
    put(t, nm);
}

static void put_scores(Text *t, const char *what, const int *v) {
    put(t, what);
    for (int p = 0; p < 4; p++) {
        put(t, "  ");
        put(t, HEARTS_NAMES[p]);
        put(t, " ");
        put_num(t, v[p]);
    }
    put(t, "\n");
}

size_t hearts_text(const Hearts *h, char *out, size_t cap) {
    static const char *const DIR[4] = {"pass left", "pass right", "pass across", "no pass"};
    Text t = {out, cap, 0};
    put(&t, "Hearts  game ");
    put_num(&t, (long)h->seed);
    put(&t, "  hand ");
    put_num(&t, h->hand_no + 1);
    put(&t, "  ");
    put(&t, DIR[hearts_pass_dir(h)]);
    put(&t, "  ");
    if (h->phase == HEARTS_PASSING) put(&t, "pass three cards");
    else if (h->phase == HEARTS_PLAYING) {
        put(&t, h->turn ? HEARTS_NAMES[h->turn] : "your turn");
        if (h->turn) put(&t, " to play");
    } else if (h->phase == HEARTS_HAND_OVER) put(&t, "hand over");
    else {
        int w = hearts_winner(h);
        put(&t, "game over: ");
        put(&t, w ? HEARTS_NAMES[w] : "you");
        put(&t, w ? " wins" : " win");
    }
    put(&t, "\n");
    put_scores(&t, "Scores", h->score);
    put_scores(&t, "This hand", h->phase >= HEARTS_HAND_OVER ? h->hand_score : h->taken);
    if (h->phase >= HEARTS_HAND_OVER && h->moon >= 0) {
        put(&t, h->moon ? HEARTS_NAMES[h->moon] : "You");
        put(&t, " shot the moon\n");
    }
    if (h->ntrick) {
        put(&t, "Trick");
        for (int k = 0; k < 4; k++) {
            int q = (h->leader + k) % 4;
            if (!h->trick[q]) continue;
            put(&t, "  ");
            put(&t, HEARTS_NAMES[q]);
            put(&t, " ");
            put_card(&t, h->trick[q]);
        }
        put(&t, "\n");
    } else if (h->last_winner >= 0) {
        put(&t, "Last trick");
        for (int q = 0; q < 4; q++) {
            put(&t, "  ");
            put(&t, HEARTS_NAMES[q]);
            put(&t, " ");
            put_card(&t, h->last[q]);
        }
        put(&t, "  to ");
        put(&t, HEARTS_NAMES[h->last_winner]);
        put(&t, "\n");
    }
    put(&t, "Hand ");
    for (int i = 0; i < h->n[0]; i++) {
        put(&t, " ");
        put_card(&t, h->hand[0][i]);
    }
    put(&t, "\n");
    if (h->phase != HEARTS_PASSING && h->passed[0][0]) {
        int from = 0;
        for (int p = 1; p < 4; p++)
            if (hearts_pass_to(h, p) == 0) from = p;
        put(&t, "Passed");
        for (int i = 0; i < 3; i++) put(&t, " "), put_card(&t, h->passed[0][i]);
        put(&t, " to ");
        put(&t, HEARTS_NAMES[hearts_pass_to(h, 0)]);
        put(&t, "; got");
        for (int i = 0; i < 3; i++) put(&t, " "), put_card(&t, h->passed[from][i]);
        put(&t, " from ");
        put(&t, HEARTS_NAMES[from]);
        put(&t, "\n");
    }
    if (cap) out[t.len < cap ? t.len : cap - 1] = 0;
    return t.len;
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
        if (lower(w[k]) != s[k]) return 0;
    return k == n && !s[k];
}

/* "2C", "TD", "10D", "qs": the card, or 0 */
static uint8_t card_of(const char *w, size_t n) {
    static const char RANKS[] = "a23456789tjqk", SUITS[] = "cdhs";
    int rank = 0;
    size_t k = 0;
    if (n == 3 && w[0] == '1' && w[1] == '0') rank = 10, k = 2;
    else if (n == 2)
        for (int r = 0; r < 13; r++)
            if (lower(w[0]) == RANKS[r]) rank = r + 1, k = 1;
    if (!rank) return 0;
    for (int s = 0; s < 4; s++)
        if (lower(w[k]) == SUITS[s]) return CARD(rank, s);
    return 0;
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

int hearts_command(Hearts *h, const char *line, char *msg, size_t cap) {
    const char *p = line, *w[5], *e;
    size_t n[5];
    int nw = 0;
    say(msg, cap, "");
    while (nw < 5 && (n[nw] = word(&p, &w[nw]))) nw++;
    if (!nw) return 0;
    if (word(&p, &e)) {
        say(msg, cap, "too many words");
        return -1;
    }
    if (is(w[0], n[0], "new")) {
        uint32_t seed = h->seed + 1;
        if (nw > 2 || (nw == 2 && number(w[1], n[1], &seed))) {
            say(msg, cap, "new [SEED]: a number from 0 to 4294967295");
            return -1;
        }
        hearts_new(h, seed);
    } else if (is(w[0], n[0], "pass")) {
        uint8_t c[3];
        int ok = nw == 4;
        for (int i = 0; ok && i < 3; i++) ok = (c[i] = card_of(w[i + 1], n[i + 1])) != 0;
        if (!ok) {
            say(msg, cap, "pass C C C: three cards, as 2C TD QS");
            return -1;
        }
        if (h->phase != HEARTS_PASSING) {
            say(msg, cap, hearts_pass_dir(h) == HEARTS_PASS_NONE ? "no passing this hand" : "the cards are passed already");
            return -1;
        }
        if (hearts_pass(h, c)) {
            say(msg, cap, "three different cards from your hand");
            return -1;
        }
    } else if (is(w[0], n[0], "play")) {
        uint8_t c = nw == 2 ? card_of(w[1], n[1]) : 0;
        const char *why;
        if (!c) {
            say(msg, cap, "play C: a card, as 2C TD QS");
            return -1;
        }
        if (h->phase == HEARTS_PLAYING && h->turn != 0) hearts_run(h);
        if (h->phase == HEARTS_PLAYING && !hearts_has(h, 0, c)) {
            say(msg, cap, "that card is not in your hand");
            return -1;
        }
        if (hearts_can_play(h, c, &why)) {
            say(msg, cap, why);
            return -1;
        }
        hearts_play(h, c);
    } else if (is(w[0], n[0], "next")) {
        if (nw != 1 || hearts_next_hand(h)) {
            say(msg, cap, h->phase == HEARTS_GAME_OVER ? "the game is over: new for another" : "the hand is not over yet");
            return -1;
        }
    } else {
        say(msg, cap, "unknown: try new, pass, play, next");
        return -1;
    }
    hearts_run(h);
    return 0;
}
