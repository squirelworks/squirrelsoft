/* bridge: contract bridge rules, scoring and computer players (see bridge.h). Original code. */
#include "bridge.h"

#include <string.h>

const char *const BRIDGE_SEATS[4] = {"North", "East", "South", "West"};

static uint32_t next_random(uint32_t *s) { /* splitmix style, as cards.c */
    uint32_t z = (*s += 0x9E3779B9u);
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

static int value(uint8_t c) { return CARD_RANK(c) == 1 ? 14 : CARD_RANK(c); } /* aces high */
static int suit_of(uint8_t c) { return CARD_SUIT(c); }
static int side_of(int seat) { return seat & 1; } /* 0 North-South, 1 East-West */
static int partner_of(int seat) { return (seat + 2) & 3; }
static int bit(uint8_t c) { return suit_of(c) * 13 + CARD_RANK(c) - 1; }

/* ---- boards ---- */

static void sort_hand(uint8_t *h, int n) { /* spades, hearts, diamonds, clubs; high first */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0; j--) {
            int a = (3 - suit_of(h[j - 1])) * 16 + 14 - value(h[j - 1]), b = (3 - suit_of(h[j])) * 16 + 14 - value(h[j]);
            if (a <= b) break;
            uint8_t t = h[j];
            h[j] = h[j - 1], h[j - 1] = t;
        }
}

static void deal(Bridge *b) {
    /* the dealer and vulnerability go round in sixteens, as boards do */
    static const int VUL[16] = {0, 1, 2, 3, 1, 2, 3, 0, 2, 3, 0, 1, 3, 0, 1, 2};
    int k = (b->board - 1) % 16;
    b->dealer = k % 4;
    b->vul = VUL[k];
    uint8_t deck[52];
    for (int i = 0; i < 52; i++) deck[i] = CARD(i % 13 + 1, i / 13);
    uint32_t r = b->seed ^ ((uint32_t)b->board * 0x3C6EF372u);
    for (int i = 51; i > 0; i--) {
        int j = (int)(next_random(&r) % (uint32_t)(i + 1));
        uint8_t t = deck[i];
        deck[i] = deck[j], deck[j] = t;
    }
    for (int s = 0; s < 4; s++) {
        memcpy(b->hand[s], deck + 13 * s, 13);
        sort_hand(b->hand[s], 13);
        memcpy(b->dealt[s], b->hand[s], 13);
        b->n[s] = 13;
    }
    b->phase = BRIDGE_BIDDING;
    b->turn = b->dealer;
    b->ncalls = 0;
    b->level = b->strain = b->doubled = 0;
    b->declarer = -1;
    b->ntrick = b->tricks = 0;
    b->won[0] = b->won[1] = 0;
    b->last_winner = -1;
    b->result = 0;
    memset(b->trick, 0, 4);
    memset(b->last, 0, 4);
}

void bridge_new(Bridge *b, uint32_t seed) {
    memset(b, 0, sizeof *b);
    b->seed = seed;
    b->board = 1;
    deal(b);
}

int bridge_next(Bridge *b) {
    if (b->phase != BRIDGE_DONE) return -1;
    b->board++;
    deal(b);
    return 0;
}

int bridge_hcp(const Bridge *b, int seat) {
    int p = 0;
    for (int i = 0; i < 13; i++) {
        int v = value(b->dealt[seat][i]);
        p += v > 10 ? v - 10 : 0;
    }
    return p;
}

/* ---- the auction ---- */

static int last_bid_at(const Bridge *b) { /* the index of the last bid, or -1 */
    for (int i = b->ncalls - 1; i >= 0; i--)
        if (b->calls[i] >= 5) return i;
    return -1;
}

static int seat_of_call(const Bridge *b, int i) { return (b->dealer + i) & 3; }

int bridge_can_call(const Bridge *b, int call, const char **why) {
    const char *w = NULL;
    if (b->phase != BRIDGE_BIDDING) w = "the auction is over";
    else if (call < 0 || call > BRIDGE_BID(7, BRIDGE_NOTRUMP)) w = "not a call";
    else if (call >= 5) {
        int lb = last_bid_at(b);
        if (lb >= 0 && call <= b->calls[lb]) w = "a bid must be higher than the last";
    } else if (call != BRIDGE_PASS) {
        int last = -1; /* the last call that is not a pass */
        for (int i = b->ncalls - 1; i >= 0 && last < 0; i--)
            if (b->calls[i] != BRIDGE_PASS) last = i;
        int theirs = last >= 0 && side_of(seat_of_call(b, last)) != side_of(b->turn);
        if (call == BRIDGE_DOUBLE && !(theirs && b->calls[last] >= 5)) w = "a double goes on an opponent's bid";
        if (call == BRIDGE_REDOUBLE && !(theirs && b->calls[last] == BRIDGE_DOUBLE)) w = "a redouble goes on an opponent's double";
    }
    if (why) *why = w ? w : "";
    return w ? -1 : 0;
}

static void score_board(Bridge *b);

int bridge_call(Bridge *b, int call) {
    if (bridge_can_call(b, call, NULL) || b->ncalls >= BRIDGE_CALLS) return -1;
    b->calls[b->ncalls++] = (uint8_t)call;
    b->turn = (b->turn + 1) & 3;
    int lb = last_bid_at(b);
    int passes = 0;
    for (int i = b->ncalls - 1; i >= 0 && b->calls[i] == BRIDGE_PASS; i--) passes++;
    if (lb < 0 && passes == 4) { /* passed out */
        b->phase = BRIDGE_DONE;
        b->declarer = -1;
        b->result = 0;
        return 0;
    }
    if (lb >= 0 && passes == 3) { /* three passes after the last call that was not one */
        int c = b->calls[lb], side = side_of(seat_of_call(b, lb));
        b->level = BRIDGE_LEVEL(c);
        b->strain = BRIDGE_STRAIN(c);
        b->doubled = 0;
        for (int i = lb + 1; i < b->ncalls; i++)
            if (b->calls[i] == BRIDGE_DOUBLE) b->doubled = 1;
            else if (b->calls[i] == BRIDGE_REDOUBLE) b->doubled = 2;
        for (int i = 0; i <= lb; i++) /* the first of the side to name the strain declares */
            if (b->calls[i] >= 5 && BRIDGE_STRAIN(b->calls[i]) == b->strain && side_of(seat_of_call(b, i)) == side) {
                b->declarer = seat_of_call(b, i);
                break;
            }
        b->phase = BRIDGE_PLAYING;
        b->turn = b->leader = (b->declarer + 1) & 3;
    }
    return 0;
}

/* ---- the play ---- */

static int find(const Bridge *b, int s, uint8_t c) {
    for (int i = 0; i < b->n[s]; i++)
        if (b->hand[s][i] == c) return i;
    return -1;
}

static int count_suit(const Bridge *b, int s, int suit) {
    int k = 0;
    for (int i = 0; i < b->n[s]; i++) k += suit_of(b->hand[s][i]) == suit;
    return k;
}

int bridge_dummy(const Bridge *b) { return b->declarer >= 0 && b->phase != BRIDGE_BIDDING ? partner_of(b->declarer) : -1; }

int bridge_yours(const Bridge *b) {
    if (b->phase == BRIDGE_BIDDING) return b->turn == BRIDGE_SOUTH;
    if (b->phase != BRIDGE_PLAYING) return 0;
    if (side_of(b->declarer) == 0) return side_of(b->turn) == 0; /* your side declares: both hands */
    return b->turn == BRIDGE_SOUTH;
}

int bridge_can_play(const Bridge *b, uint8_t card, const char **why) {
    const char *w = NULL;
    card &= (uint8_t)~CARD_UP;
    if (b->phase != BRIDGE_PLAYING) w = b->phase == BRIDGE_BIDDING ? "the auction is not over" : "the board is over";
    else if (find(b, b->turn, card) < 0) w = "that card is not in the hand to play";
    else if (b->ntrick) {
        int led = suit_of(b->trick[b->leader]);
        if (suit_of(card) != led && count_suit(b, b->turn, led)) w = "follow suit";
    }
    if (why) *why = w ? w : "";
    return w ? -1 : 0;
}

/* who wins the trick so far, of the cards played */
static int winning(const Bridge *b) {
    int led = suit_of(b->trick[b->leader]), win = b->leader;
    for (int k = 1; k < 4; k++) {
        int s = (b->leader + k) & 3;
        uint8_t c = b->trick[s], w = b->trick[win];
        if (!c) continue;
        int ct = b->strain < 4 && suit_of(c) == b->strain, wt = b->strain < 4 && suit_of(w) == b->strain;
        if ((ct && !wt) || (suit_of(c) == suit_of(w) && value(c) > value(w) && (ct || suit_of(c) == led))) win = s;
    }
    return win;
}

int bridge_play(Bridge *b, uint8_t card) {
    card &= (uint8_t)~CARD_UP;
    if (bridge_can_play(b, card, NULL)) return -1;
    int s = b->turn, i = find(b, s, card);
    memmove(&b->hand[s][i], &b->hand[s][i + 1], (size_t)(b->n[s] - i - 1));
    b->n[s]--;
    b->trick[s] = card;
    b->ntrick++;
    if (b->ntrick < 4) {
        b->turn = (s + 1) & 3;
        return 0;
    }
    int w = winning(b);
    b->won[side_of(w)]++;
    memcpy(b->last, b->trick, 4);
    memset(b->trick, 0, 4);
    b->last_winner = w;
    b->ntrick = 0;
    b->tricks++;
    b->turn = b->leader = w;
    if (b->tricks == 13) score_board(b);
    return 0;
}

/* ---- scoring ---- */

int bridge_score(int level, int strain, int doubled, int vulnerable, int tricks) {
    int need = level + 6, mult = doubled == 2 ? 4 : doubled == 1 ? 2 : 1;
    if (tricks < need) {
        int down = need - tricks, s = 0;
        for (int k = 1; k <= down; k++) {
            if (!doubled) s += vulnerable ? 100 : 50;
            else if (vulnerable) s += k == 1 ? 200 : 300;
            else s += k == 1 ? 100 : k <= 3 ? 200 : 300;
        }
        return -(doubled == 2 ? 2 * s : s);
    }
    int per = strain <= BRIDGE_DIAMONDS ? 20 : 30;
    int contract = (level * per + (strain == BRIDGE_NOTRUMP ? 10 : 0)) * mult;
    int s = contract;
    s += contract >= 100 ? (vulnerable ? 500 : 300) : 50;
    if (level == 6) s += vulnerable ? 750 : 500;
    if (level == 7) s += vulnerable ? 1500 : 1000;
    int over = tricks - need;
    if (!doubled) s += over * per;
    else s += over * (vulnerable ? 200 : 100) * (doubled == 2 ? 2 : 1);
    if (doubled) s += doubled == 2 ? 100 : 50; /* for making it doubled */
    return s;
}

static void score_board(Bridge *b) {
    int side = side_of(b->declarer), vul = (b->vul >> side) & 1;
    int s = bridge_score(b->level, b->strain, b->doubled, vul, b->won[side]);
    b->result = side == 0 ? s : -s;
    b->total += b->result;
    b->phase = BRIDGE_DONE;
}

/* ---- the computer: calls ---- */

static int length(const Bridge *b, int seat, int suit) {
    int k = 0;
    for (int i = 0; i < 13; i++) k += suit_of(b->dealt[seat][i]) == suit;
    return k;
}

static int balanced(const Bridge *b, int seat) {
    int twos = 0;
    for (int s = 0; s < 4; s++) {
        int l = length(b, seat, s);
        if (l < 2 || l > 5) return 0;
        twos += l == 2;
    }
    return twos <= 1;
}

/* what a seat has shown by its calls: points at least, and length in each suit */
static int shown(const Bridge *b, int seat, int len[4]) {
    int pts = 0, first = 1;
    for (int s = 0; s < 4; s++) len[s] = 0;
    for (int i = 0; i < b->ncalls; i++) {
        int c = b->calls[i];
        if (seat_of_call(b, i) != seat || c < 5) continue;
        int st = BRIDGE_STRAIN(c), lvl = BRIDGE_LEVEL(c);
        if (first) {
            int opened = 1, partner_bid = 0;
            for (int k = 0; k < i; k++)
                if (b->calls[k] >= 5) {
                    opened = 0;
                    if (seat_of_call(b, k) == partner_of(seat)) partner_bid = 1;
                }
            if (opened) pts = st == BRIDGE_NOTRUMP ? (lvl == 1 ? 15 : 20) : 12;
            else pts = partner_bid ? 6 : 9;
            first = 0;
        }
        if (st < 4) len[st] = len[st] ? (len[st] < 7 ? len[st] + 1 : 7) : (st >= BRIDGE_HEARTS ? 5 : 4);
    }
    return pts;
}

static int game_level(int strain) { return strain == BRIDGE_NOTRUMP ? 3 : strain >= BRIDGE_HEARTS ? 4 : 5; }

static int lowest_level(const Bridge *b, int strain) { /* the lowest level a bid in strain may be, or 8 */
    int lb = last_bid_at(b);
    if (lb < 0) return 1;
    for (int lvl = 1; lvl <= 7; lvl++)
        if (BRIDGE_BID(lvl, strain) > b->calls[lb]) return lvl;
    return 8;
}

int bridge_ai_call(const Bridge *b) {
    int me = b->turn, part = partner_of(me), h = bridge_hcp(b, me);
    int len[4], plen[4], mylen[4];
    for (int s = 0; s < 4; s++) len[s] = length(b, me, s);
    int ppts = shown(b, part, plen), mine = shown(b, me, mylen);
    int lb = last_bid_at(b), ours = lb >= 0 && side_of(seat_of_call(b, lb)) == side_of(me);
    int longest = BRIDGE_SPADES;
    for (int s = BRIDGE_SPADES; s >= 0; s--)
        if (len[s] > len[longest]) longest = s;
    if (!ppts && !mine) {
        if (lb < 0) { /* to open */
            if (h < 12 && !(h >= 11 && len[longest] >= 6)) return BRIDGE_PASS;
            if (balanced(b, me) && h >= 15 && h <= 17) return BRIDGE_BID(1, BRIDGE_NOTRUMP);
            if (balanced(b, me) && h >= 20 && h <= 21) return BRIDGE_BID(2, BRIDGE_NOTRUMP);
            int suit = len[BRIDGE_SPADES] >= 5 && len[BRIDGE_SPADES] >= len[BRIDGE_HEARTS] ? BRIDGE_SPADES
                       : len[BRIDGE_HEARTS] >= 5                                          ? BRIDGE_HEARTS
                       : len[BRIDGE_DIAMONDS] > len[BRIDGE_CLUBS] || (len[BRIDGE_DIAMONDS] == len[BRIDGE_CLUBS] && len[BRIDGE_DIAMONDS] >= 4)
                           ? BRIDGE_DIAMONDS
                           : BRIDGE_CLUBS;
            if (len[longest] >= 6 && longest != suit && len[suit] < 5) suit = longest;
            return BRIDGE_BID(1, suit);
        }
        if (ours) return BRIDGE_PASS;
        /* they opened: an overcall with a good suit, or 1NT with a strong even hand */
        if (balanced(b, me) && h >= 15 && h <= 18 && lowest_level(b, BRIDGE_NOTRUMP) == 1) return BRIDGE_BID(1, BRIDGE_NOTRUMP);
        if (h >= 9 && len[longest] >= 5) {
            int lvl = lowest_level(b, longest);
            if (lvl == 1 || (lvl == 2 && h >= 12)) return BRIDGE_BID(lvl, longest);
        }
        return BRIDGE_PASS;
    }
    if (!ppts) return BRIDGE_PASS; /* partner has said nothing: no more alone */
    if (h < 6 && !mine) return BRIDGE_PASS;
    int total = h + ppts;
    /* the strain: a fit of eight, a major first; else partner's no trumps or a long suit of mine; else no trumps */
    int strain = -1;
    for (int s = BRIDGE_SPADES; s >= 0 && strain < 0; s--)
        if (plen[s] && len[s] + plen[s] >= 8 && (s >= BRIDGE_HEARTS || strain < 0)) strain = s;
    if (strain < 0 && len[longest] >= 6) strain = longest;
    if (strain < 0 && !mine && len[longest] >= 4 && lowest_level(b, longest) == 1 && longest != BRIDGE_CLUBS) strain = longest;
    if (strain < 0) strain = BRIDGE_NOTRUMP;
    if (strain < BRIDGE_HEARTS && strain != BRIDGE_NOTRUMP && total < 29) {
        int fit = plen[strain] && len[strain] + plen[strain] >= 8;
        if (total >= 25 || !fit) strain = BRIDGE_NOTRUMP; /* game in no trumps rather than five of a minor */
    }
    int target = total >= 33 ? 6 : total >= 25 ? game_level(strain) : total >= 23 ? (strain == BRIDGE_NOTRUMP ? 2 : 3)
                 : total >= 20                                      ? (strain == BRIDGE_NOTRUMP ? 1 : 2)
                                                                    : 1;
    if (lb >= 0 && ours) { /* our side holds the contract: on only toward the target */
        int c = b->calls[lb];
        if (BRIDGE_LEVEL(c) >= target) return BRIDGE_PASS;
        if (BRIDGE_STRAIN(c) == strain && BRIDGE_LEVEL(c) >= target) return BRIDGE_PASS;
    }
    int lvl = lowest_level(b, strain);
    if (lvl > target) return BRIDGE_PASS;
    if (total >= 25 && lvl < target && (mine || ppts >= 12)) lvl = target; /* a jump to where we belong */
    return BRIDGE_BID(lvl, strain);
}

/* ---- the computer: cards ---- */

/* whether c is the highest card of its suit not yet played, counting cards in the given hand as its own */
static int top_card(const Bridge *b, int seat, uint8_t c, uint64_t gone) {
    for (int v = value(c) + 1; v <= 14; v++) {
        uint8_t h = CARD(v == 14 ? 1 : v, suit_of(c));
        if (!(gone >> bit(h) & 1) && find(b, seat, h) < 0) return 0;
    }
    return 1;
}

static uint64_t played(const Bridge *b) {
    uint64_t g = (uint64_t)-1 >> 12; /* every card, less those still in a hand */
    for (int s = 0; s < 4; s++)
        for (int i = 0; i < b->n[s]; i++) g &= ~((uint64_t)1 << bit(b->hand[s][i]));
    return g;
}

uint8_t bridge_ai_card(const Bridge *b) {
    int me = b->turn, part = partner_of(me), trump = b->strain < 4 ? b->strain : -1;
    uint8_t legal[13];
    int nl = 0;
    for (int i = 0; i < b->n[me]; i++)
        if (!bridge_can_play(b, b->hand[me][i], NULL)) legal[nl++] = b->hand[me][i];
    if (nl <= 1) return nl ? legal[0] : 0;
    uint64_t gone = played(b);
    uint8_t best = 0;
    if (b->ntrick == 0) {
        /* a winner to cash outside trumps (trumps too for the declaring side) */
        int declaring = b->declarer >= 0 && side_of(me) == side_of(b->declarer);
        for (int i = 0; i < nl; i++)
            if (top_card(b, me, legal[i], gone) && (suit_of(legal[i]) != trump || declaring) && b->tricks > 0) return legal[i];
        /* else: partner's suit, or my longest outside trumps; the top of a sequence, else low */
        int suit = -1, plen[4];
        if (b->tricks == 0 && shown(b, part, plen) >= 0)
            for (int s = 0; s < 4; s++)
                if (plen[s] && s != trump && count_suit(b, me, s)) suit = s;
        if (suit < 0)
            for (int s = 3; s >= 0; s--)
                if (s != trump && count_suit(b, me, s) && (suit < 0 || count_suit(b, me, s) > count_suit(b, me, suit))) suit = s;
        if (suit < 0) suit = suit_of(legal[0]);
        uint8_t hi = 0, second = 0, lo = 0;
        for (int i = 0; i < nl; i++) {
            if (suit_of(legal[i]) != suit) continue;
            if (!hi) hi = legal[i];
            else if (!second) second = legal[i];
            lo = legal[i]; /* (the hand is sorted high first) */
        }
        if (second && value(hi) >= 10 && value(second) == value(hi) - 1) return hi;
        if (count_suit(b, me, suit) == 2) return hi;
        return lo;
    }
    int w = winning(b), led = suit_of(b->trick[b->leader]);
    uint8_t wc = b->trick[w];
    int partner_has_it = w == part && (b->ntrick == 3 || top_card(b, part, wc, gone | ((uint64_t)1 << bit(wc))));
    if (suit_of(legal[0]) == led || count_suit(b, me, led)) { /* following suit */
        uint8_t low = legal[nl - 1];
        if (partner_has_it || b->ntrick == 1) return low;
        if (trump >= 0 && suit_of(wc) == trump && led != trump) return low; /* ruffed already */
        for (int i = nl - 1; i >= 0; i--) /* the cheapest that wins */
            if (value(legal[i]) > value(wc)) return legal[i];
        return low;
    }
    /* void in the suit led: ruff when the other side is winning, else throw the least */
    if (!partner_has_it && trump >= 0 && count_suit(b, me, trump)) {
        for (int i = nl - 1; i >= 0; i--) {
            uint8_t c = legal[i];
            if (suit_of(c) == trump && (suit_of(wc) != trump || value(c) > value(wc))) return c;
        }
    }
    int k = 99;
    for (int i = 0; i < nl; i++) {
        uint8_t c = legal[i];
        int cost = value(c) + (suit_of(c) == trump ? 20 : 0) + (top_card(b, me, c, gone) ? 10 : 0);
        if (cost < k) k = cost, best = c;
    }
    return best;
}

int bridge_run(Bridge *b) {
    int k = 0;
    while (b->phase != BRIDGE_DONE && !bridge_yours(b)) {
        if (b->phase == BRIDGE_BIDDING) {
            if (bridge_call(b, bridge_ai_call(b))) bridge_call(b, BRIDGE_PASS);
        } else if (bridge_play(b, bridge_ai_card(b)))
            break;
        k++;
    }
    return k;
}

/* ---- names ---- */

void bridge_call_name(int call, char out[6]) {
    static const char *const ST[5] = {"C", "D", "H", "S", "NT"};
    if (call == BRIDGE_PASS) memcpy(out, "Pass", 5);
    else if (call == BRIDGE_DOUBLE) memcpy(out, "X", 2);
    else if (call == BRIDGE_REDOUBLE) memcpy(out, "XX", 3);
    else {
        out[0] = (char)('0' + BRIDGE_LEVEL(call));
        const char *s = ST[BRIDGE_STRAIN(call)];
        int n = 1;
        while (*s) out[n++] = *s++;
        out[n] = 0;
    }
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int is(const char *w, size_t n, const char *s) {
    size_t k = 0;
    for (; k < n && s[k]; k++)
        if (lower(w[k]) != s[k]) return 0;
    return k == n && !s[k];
}

int bridge_call_of(const char *w, size_t n) {
    if (is(w, n, "pass") || is(w, n, "p")) return BRIDGE_PASS;
    if (is(w, n, "x") || is(w, n, "double")) return BRIDGE_DOUBLE;
    if (is(w, n, "xx") || is(w, n, "redouble")) return BRIDGE_REDOUBLE;
    if (n < 2 || w[0] < '1' || w[0] > '7') return -1;
    int lvl = w[0] - '0', st = -1;
    if (is(w + 1, n - 1, "c")) st = 0;
    else if (is(w + 1, n - 1, "d")) st = 1;
    else if (is(w + 1, n - 1, "h")) st = 2;
    else if (is(w + 1, n - 1, "s")) st = 3;
    else if (is(w + 1, n - 1, "nt") || is(w + 1, n - 1, "n")) st = 4;
    return st < 0 ? -1 : BRIDGE_BID(lvl, st);
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
    char d[24], r[24];
    int n = 0, neg = v < 0;
    unsigned long u = neg ? 0ul - (unsigned long)v : (unsigned long)v;
    do d[n++] = (char)('0' + u % 10);
    while (u /= 10);
    int k = 0;
    if (neg) r[k++] = '-';
    while (n) r[k++] = d[--n];
    r[k] = 0;
    put(t, r);
}

static void put_card(Text *t, uint8_t c) {
    char nm[3] = {"?A23456789TJQK"[CARD_RANK(c) <= 13 ? CARD_RANK(c) : 0], "CDHS"[suit_of(c)], 0};
    put(t, nm);
}

static void put_hand(Text *t, const Bridge *b, int seat) {
    put(t, BRIDGE_SEATS[seat]);
    for (int s = 3; s >= 0; s--) {
        char h[4] = {' ', ' ', "CDHS"[s], 0};
        put(t, h);
        put(t, ":");
        int any = 0;
        for (int i = 0; i < b->n[seat]; i++)
            if (suit_of(b->hand[seat][i]) == s) {
                char r[3] = {' ', "?A23456789TJQK"[CARD_RANK(b->hand[seat][i])], 0};
                put(t, r);
                any = 1;
            }
        if (!any) put(t, " -");
    }
    put(t, "\n");
}

static void put_contract(Text *t, const Bridge *b) {
    char nm[6];
    bridge_call_name(BRIDGE_BID(b->level, b->strain), nm);
    put(t, nm);
    if (b->doubled) put(t, b->doubled == 2 ? "XX" : "X");
    put(t, " by ");
    put(t, BRIDGE_SEATS[b->declarer]);
}

size_t bridge_text(const Bridge *b, char *out, size_t cap) {
    static const char *const VUL[4] = {"none", "North-South", "East-West", "both"};
    Text t = {out, cap, 0};
    put(&t, "Bridge  game ");
    put_num(&t, (long)b->seed);
    put(&t, "  board ");
    put_num(&t, b->board);
    put(&t, "  dealer ");
    put(&t, BRIDGE_SEATS[b->dealer]);
    put(&t, "  vulnerable ");
    put(&t, VUL[b->vul]);
    put(&t, "  total ");
    put_num(&t, b->total);
    put(&t, "\nAuction");
    for (int i = 0; i < b->ncalls; i++) {
        char nm[6];
        bridge_call_name(b->calls[i], nm);
        put(&t, " ");
        put(&t, (const char[2]){BRIDGE_SEATS[seat_of_call(b, i)][0], 0});
        put(&t, ":");
        put(&t, nm);
    }
    put(&t, "\n");
    if (b->phase == BRIDGE_BIDDING) {
        put(&t, b->turn == BRIDGE_SOUTH ? "Your call" : BRIDGE_SEATS[b->turn]);
        if (b->turn != BRIDGE_SOUTH) put(&t, " to call");
        put(&t, "\n");
    } else if (b->declarer < 0) {
        put(&t, "Passed out\n");
    } else {
        put(&t, "Contract ");
        put_contract(&t, b);
        put(&t, "  tricks North-South ");
        put_num(&t, b->won[0]);
        put(&t, " East-West ");
        put_num(&t, b->won[1]);
        if (b->phase == BRIDGE_PLAYING) {
            put(&t, "  ");
            put(&t, BRIDGE_SEATS[b->turn]);
            put(&t, bridge_yours(b) ? " to play (yours)" : " to play");
        } else {
            int side = side_of(b->declarer), need = b->level + 6, got = b->won[side];
            put(&t, got >= need ? "  made" : "  down ");
            if (got < need) put_num(&t, need - got);
            else if (got > need) put(&t, " +"), put_num(&t, got - need);
            put(&t, "  score North-South ");
            put_num(&t, b->result);
        }
        put(&t, "\n");
    }
    if (b->ntrick) {
        put(&t, "Trick");
        for (int k = 0; k < 4; k++) {
            int s = (b->leader + k) & 3;
            if (!b->trick[s]) continue;
            put(&t, " ");
            put(&t, BRIDGE_SEATS[s]);
            put(&t, " ");
            put_card(&t, b->trick[s]);
        }
        put(&t, "\n");
    } else if (b->last_winner >= 0) {
        put(&t, "Last trick");
        for (int s = 0; s < 4; s++) {
            put(&t, " ");
            put(&t, BRIDGE_SEATS[s]);
            put(&t, " ");
            put_card(&t, b->last[s]);
        }
        put(&t, "  to ");
        put(&t, BRIDGE_SEATS[b->last_winner]);
        put(&t, "\n");
    }
    int dummy = bridge_dummy(b), shown_dummy = dummy >= 0 && (b->tricks > 0 || b->ntrick > 0 || b->phase == BRIDGE_DONE);
    int north = b->phase == BRIDGE_PLAYING && b->declarer >= 0 && side_of(b->declarer) == 0;
    if (north || (shown_dummy && dummy == BRIDGE_NORTH)) put_hand(&t, b, BRIDGE_NORTH);
    if (shown_dummy && side_of(dummy) == 1) put_hand(&t, b, dummy);
    put_hand(&t, b, BRIDGE_SOUTH);
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

static size_t word(const char **p, const char **start) {
    while (**p == ' ' || **p == '\t') (*p)++;
    *start = *p;
    while (**p && **p != ' ' && **p != '\t') (*p)++;
    return (size_t)(*p - *start);
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

int bridge_command(Bridge *b, const char *line, char *msg, size_t cap) {
    const char *p = line, *w[3], *e;
    size_t n[3];
    int nw = 0;
    say(msg, cap, "");
    while (nw < 3 && (n[nw] = word(&p, &w[nw]))) nw++;
    if (!nw) return 0;
    if (word(&p, &e) || nw > 2) {
        say(msg, cap, "too many words");
        return -1;
    }
    const char *why;
    if (is(w[0], n[0], "new")) {
        uint32_t seed = b->seed + 1;
        if (nw == 2 && number(w[1], n[1], &seed)) {
            say(msg, cap, "new [SEED]: a number from 0 to 4294967295");
            return -1;
        }
        bridge_new(b, seed);
    } else if (is(w[0], n[0], "bid")) {
        int c = nw == 2 ? bridge_call_of(w[1], n[1]) : -1;
        if (c < 0) {
            say(msg, cap, "bid CALL: as 1H, 3NT, pass, X, XX");
            return -1;
        }
        bridge_run(b);
        if (b->phase != BRIDGE_BIDDING) {
            say(msg, cap, "the auction is over");
            return -1;
        }
        if (bridge_can_call(b, c, &why)) {
            say(msg, cap, why);
            return -1;
        }
        bridge_call(b, c);
    } else if (is(w[0], n[0], "play")) {
        uint8_t c = nw == 2 ? card_of(w[1], n[1]) : 0;
        if (!c) {
            say(msg, cap, "play CARD: as AS, TD, 7C");
            return -1;
        }
        bridge_run(b);
        if (bridge_can_play(b, c, &why)) {
            say(msg, cap, why);
            return -1;
        }
        bridge_play(b, c);
    } else if (is(w[0], n[0], "next")) {
        if (nw != 1 || bridge_next(b)) {
            say(msg, cap, "the board is not over yet");
            return -1;
        }
    } else {
        say(msg, cap, "unknown: try new, bid, play, next");
        return -1;
    }
    bridge_run(b);
    return 0;
}
