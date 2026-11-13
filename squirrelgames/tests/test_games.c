/* SquirrelGames tests: the rules of each game, the commands, and the
 * window driven by simulated input. */
#include "cards.h"
#include "chess.h"
#include "gamesgui.h"
#include "hearts.h"
#include "mines.h"
#include "tiles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(c) check((c), #c, __LINE__)
static void check(int ok, const char *what, int line) {
    checks++;
    if (!ok) {
        failures++;
        printf("FAIL line %d: %s\n", line, what);
    }
}

static Game g;
static Mines mn;
static Hearts hh;
static Chess ch;
static Tiles tt;

/* a pile from names: "AS 2H #KD" (# for face down) */
static void set(int pile, const char *cards) {
    CardPile *p = &g.s.pile[pile];
    p->n = 0;
    for (const char *s = cards; *s;) {
        while (*s == ' ') s++;
        if (!*s) break;
        int down = *s == '#';
        if (down) s++;
        const char *R = "A23456789TJQK", *S = "CDHS";
        int r = (int)(strchr(R, s[0]) - R) + 1, su = (int)(strchr(S, s[1]) - S);
        p->c[p->n++] = (uint8_t)(CARD(r, su) | (down ? 0 : CARD_UP));
        s += 2;
    }
}

static void clear_all(int kind) {
    game_new(&g, kind, 1, 1);
    memset(g.s.pile, 0, sizeof g.s.pile);
}

static int ok(const char *line) {
    char msg[120];
    int rc = game_command(&g, line, msg, sizeof msg);
    if (rc) printf("  %s: %s\n", line, msg);
    return rc == 0;
}
static int fails(const char *line, const char *why) {
    char msg[120];
    int rc = game_command(&g, line, msg, sizeof msg);
    if (rc == 0 || !strstr(msg, why)) {
        printf("  %s: rc %d, said \"%s\"\n", line, rc, msg);
        return 0;
    }
    return 1;
}

static int total_cards(const GameState *s) {
    int n = 0;
    for (int p = 0; p < PILE_COUNT; p++) n += s->pile[p].n;
    return n;
}

static int every_card_once(const GameState *s) {
    int seen[64] = {0};
    for (int p = 0; p < PILE_COUNT; p++)
        for (int i = 0; i < s->pile[p].n; i++) seen[s->pile[p].c[i] & 0x3F]++;
    for (int r = 1; r <= 13; r++)
        for (int su = 0; su < 4; su++)
            if (seen[CARD(r, su)] != 1) return 0;
    return 1;
}

static void test_deals(void) {
    game_new(&g, GAME_KLONDIKE, 1, 42);
    CHECK(every_card_once(&g.s) && total_cards(&g.s) == 52 && g.s.pile[PILE_STOCK].n == 24);
    int tops = 1;
    for (int c = 0; c < 7; c++) {
        const CardPile *p = &g.s.pile[PILE_TAB + c];
        tops &= p->n == c + 1;
        for (int i = 0; i < p->n; i++) tops &= !(p->c[i] & CARD_UP) == (i != c);
    }
    CHECK(tops && !(g.s.pile[PILE_STOCK].c[0] & CARD_UP));
    GameState first = g.s;
    game_new(&g, GAME_KLONDIKE, 1, 42);
    CHECK(!memcmp(&first, &g.s, sizeof first));
    game_new(&g, GAME_KLONDIKE, 1, 43);
    CHECK(memcmp(first.pile, g.s.pile, sizeof first.pile) != 0);
    game_new(&g, GAME_FREECELL, 1, 7);
    CHECK(every_card_once(&g.s) && game_columns(&g.s) == 8);
    CHECK(g.s.pile[PILE_TAB].n == 7 && g.s.pile[PILE_TAB + 3].n == 7 && g.s.pile[PILE_TAB + 4].n == 6 &&
          g.s.pile[PILE_TAB + 7].n == 6 && g.s.pile[PILE_STOCK].n == 0);
    CHECK(game_draw(&g) == -1 && !game_can_undo(&g));
    char nm[3];
    game_card_name(CARD(10, SUIT_HEARTS) | CARD_UP, nm);
    CHECK(!strcmp(nm, "TH"));
    game_card_name(CARD(1, SUIT_SPADES), nm);
    CHECK(!strcmp(nm, "##"));
    CHECK(game_pile_of("T8", 2) == PILE_TAB + 7 && game_pile_of("f4", 2) == PILE_FOUND + 3 && game_pile_of("c2", 2) == PILE_CELL + 1);
    CHECK(game_pile_of("w", 1) == PILE_WASTE && game_pile_of("f5", 2) == -1 && game_pile_of("t9", 2) == -1 && game_pile_of("x1", 2) == -1);
}

static void test_klondike(void) {
    clear_all(GAME_KLONDIKE);
    set(PILE_TAB + 0, "#5C KS");
    set(PILE_TAB + 1, "");
    set(PILE_TAB + 2, "QH JC");
    set(PILE_TAB + 3, "#2D 9D");
    set(PILE_WASTE, "3S TH");
    set(PILE_STOCK, "#4C #4D #4H #4S");
    /* a king to an empty column, and what it turned up */
    CHECK(game_can_move(&g.s, PILE_TAB + 2, 2, PILE_TAB + 1) == -1); /* a queen is no king */
    CHECK(game_move(&g, PILE_TAB, 1, PILE_TAB + 1) == 0 && g.s.pile[PILE_TAB].c[0] == (CARD(5, SUIT_CLUBS) | CARD_UP));
    CHECK(g.s.score == 5 && g.s.moves == 1);
    /* a run onto the king: the queen and the jack */
    CHECK(game_run(&g.s, PILE_TAB + 2) == 2 && game_move(&g, PILE_TAB + 2, 2, PILE_TAB + 1) == 0);
    CHECK(g.s.pile[PILE_TAB + 1].n == 3 && g.s.pile[PILE_TAB + 2].n == 0);
    /* the waste: the ten on the jack (red on black), +5 */
    CHECK(game_move(&g, PILE_WASTE, 1, PILE_TAB + 1) == 0 && g.s.score == 10);
    CHECK(game_move(&g, PILE_WASTE, 1, PILE_TAB + 1) == -1); /* a black three on a red ten: no */
    CHECK(game_move(&g, PILE_TAB + 3, 1, PILE_TAB + 1) == -1 && g.s.pile[PILE_TAB + 1].n == 4); /* 9D on TH: red on red */
    CHECK(g.nundo == 3 && game_undo(&g) == 0 && g.s.pile[PILE_WASTE].n == 2 && g.s.score == 5);
}

static void test_klondike_rules(void) {
    clear_all(GAME_KLONDIKE);
    set(PILE_TAB, "TH");
    set(PILE_TAB + 1, "9D");
    set(PILE_TAB + 2, "9S");
    CHECK(game_can_move(&g.s, PILE_TAB + 1, 1, PILE_TAB) == -1); /* red on red */
    CHECK(game_can_move(&g.s, PILE_TAB + 2, 1, PILE_TAB) == 0);
    CHECK(game_can_move(&g.s, PILE_TAB + 2, 2, PILE_TAB) == -1); /* only one there */
    set(PILE_TAB + 3, "#AS 8H");
    CHECK(game_can_move(&g.s, PILE_TAB + 3, 2, PILE_TAB + 2) == -1); /* a face-down card cannot go */
    CHECK(game_move(&g, PILE_TAB + 3, 1, PILE_TAB + 2) == 0 && g.s.score == 5); /* and the ace turns up */
    /* home: an ace, then the same suit one up */
    CHECK(game_move(&g, PILE_TAB + 3, 1, PILE_FOUND + 2) == 0 && g.s.score == 15);
    set(PILE_WASTE, "2S 3S");
    CHECK(game_can_move(&g.s, PILE_WASTE, 1, PILE_FOUND + 2) == -1);
    CHECK(game_can_move(&g.s, PILE_WASTE, 1, PILE_FOUND + 1) == -1); /* a three on nothing */
    set(PILE_WASTE, "3S 2S");
    CHECK(game_move(&g, PILE_WASTE, 1, PILE_FOUND + 2) == 0 && g.s.score == 25);
    CHECK(game_can_move(&g.s, PILE_FOUND + 2, 1, PILE_FOUND + 0) == -1); /* home to home: no */
    /* from home back down costs 15 */
    set(PILE_TAB + 4, "3H");
    CHECK(game_move(&g, PILE_FOUND + 2, 1, PILE_TAB + 4) == 0 && g.s.score == 10);
    CHECK(game_can_move(&g.s, PILE_STOCK, 1, PILE_TAB) == -1 && game_can_move(&g.s, PILE_TAB, 1, PILE_WASTE) == -1);
    CHECK(game_can_move(&g.s, PILE_TAB, 1, PILE_CELL) == -1); /* Klondike has no cells */
    /* undo puts the score back too */
    int moves = g.s.moves;
    CHECK(game_undo(&g) == 0 && g.s.score == 25 && g.s.moves == moves - 1);
    /* the stock: one at a time, then the waste back for 100 */
    clear_all(GAME_KLONDIKE);
    set(PILE_STOCK, "#AC #2C #3C");
    g.s.score = 150;
    CHECK(game_draw(&g) == 0 && g.s.pile[PILE_WASTE].n == 1 && g.s.pile[PILE_WASTE].c[0] == (CARD(3, SUIT_CLUBS) | CARD_UP));
    game_draw(&g);
    game_draw(&g);
    CHECK(g.s.pile[PILE_STOCK].n == 0 && g.s.pile[PILE_WASTE].n == 3);
    CHECK(game_draw(&g) == 0 && g.s.pile[PILE_STOCK].n == 3 && g.s.pile[PILE_STOCK].c[2] == CARD(3, SUIT_CLUBS) && g.s.score == 50);
    game_draw(&g);
    game_draw(&g);
    game_draw(&g);
    CHECK(game_draw(&g) == 0 && g.s.score == 0); /* never below nothing */
    g.s.draw = 3;
    CHECK(game_draw(&g) == 0 && g.s.pile[PILE_WASTE].n == 3 && g.s.pile[PILE_STOCK].n == 0);
    CHECK(g.s.pile[PILE_WASTE].c[2] == (CARD(1, SUIT_CLUBS) | CARD_UP));
    set(PILE_WASTE, "");
    set(PILE_STOCK, "");
    CHECK(game_draw(&g) == -1);
}

static void test_freecell(void) {
    clear_all(GAME_FREECELL);
    set(PILE_TAB, "KS QH JC TD 9S");
    set(PILE_TAB + 1, "TH");
    set(PILE_TAB + 2, "5C");
    set(PILE_TAB + 3, "6D");
    for (int t = 4; t < 8; t++) set(PILE_TAB + t, "9C");
    /* four free cells: five cards together; no empty columns */
    CHECK(game_most(&g.s, PILE_TAB + 1) == 5 && game_run(&g.s, PILE_TAB) == 5);
    set(PILE_CELL, "AD");
    set(PILE_CELL + 1, "AH");
    CHECK(game_most(&g.s, PILE_TAB + 1) == 3);
    CHECK(game_can_move(&g.s, PILE_TAB, 3, PILE_TAB + 1) == -1); /* JC TD 9S onto TH: the jack does not fit */
    set(PILE_TAB + 1, "QD");
    CHECK(game_can_move(&g.s, PILE_TAB, 3, PILE_TAB + 1) == 0); /* JC on QD */
    set(PILE_CELL + 2, "AC");
    CHECK(game_can_move(&g.s, PILE_TAB, 3, PILE_TAB + 1) == -1 && game_most(&g.s, PILE_TAB + 1) == 2);
    /* an empty column doubles it, but not when it is where they go */
    set(PILE_TAB + 7, "");
    CHECK(game_most(&g.s, PILE_TAB + 1) == 4 && game_most(&g.s, PILE_TAB + 7) == 2);
    CHECK(game_can_move(&g.s, PILE_TAB, 3, PILE_TAB + 1) == 0);
    CHECK(game_can_move(&g.s, PILE_TAB, 3, PILE_TAB + 7) == -1 && game_can_move(&g.s, PILE_TAB, 2, PILE_TAB + 7) == 0);
    CHECK(game_move(&g, PILE_TAB, 2, PILE_TAB + 7) == 0 && g.s.pile[PILE_TAB + 7].n == 2); /* anything to an empty column */
    /* cells: one card, one to a cell */
    CHECK(game_can_move(&g.s, PILE_TAB + 2, 1, PILE_CELL) == -1 && game_move(&g, PILE_TAB + 2, 1, PILE_CELL + 3) == 0);
    CHECK(game_can_move(&g.s, PILE_TAB + 3, 1, PILE_CELL + 3) == -1);
    CHECK(game_can_move(&g.s, PILE_TAB + 7, 2, PILE_CELL) == -1);
    CHECK(game_can_move(&g.s, PILE_WASTE, 1, PILE_TAB) == -1); /* FreeCell has no waste */
    /* home, safely: the aces and twos go; a red three waits for the black twos */
    CHECK(game_auto(&g, 0) == 3 && g.s.pile[PILE_CELL].n == 0 && g.s.pile[PILE_CELL + 2].n == 0);
    int before = g.nundo;
    clear_all(GAME_FREECELL);
    set(PILE_FOUND, "AH 2H");
    set(PILE_FOUND + 1, "AS");
    set(PILE_TAB, "3H");
    set(PILE_TAB + 1, "2S");
    CHECK(game_auto(&g, 0) == 1 && g.s.pile[PILE_TAB].n == 1); /* 2S goes; 3H waits for the black twos */
    set(PILE_FOUND + 2, "AC 2C");
    CHECK(game_auto(&g, 0) == 1 && game_auto(&g, 0) == 0);
    set(PILE_TAB + 2, "5D");
    CHECK(game_auto(&g, 0) == 0 && game_auto(&g, 1) == 0); /* no 4D home */
    (void)before;
}

static void test_smart_and_finish(void) {
    clear_all(GAME_FREECELL);
    set(PILE_TAB, "AS");
    set(PILE_TAB + 1, "9H");
    set(PILE_TAB + 2, "8S");
    set(PILE_TAB + 3, "");
    set(PILE_TAB + 4, "5C KD");
    for (int t = 5; t < 8; t++) set(PILE_TAB + t, "2D");
    CHECK(game_smart(&g, PILE_TAB, 1) == 0 && g.s.pile[PILE_FOUND].n == 1); /* home first */
    CHECK(game_smart(&g, PILE_TAB + 2, 1) == 0 && g.s.pile[PILE_TAB + 1].n == 2); /* then a column with cards */
    CHECK(game_smart(&g, PILE_TAB + 4, 1) == 0 && (g.s.pile[PILE_TAB].n == 1 || g.s.pile[PILE_TAB + 3].n == 1)); /* an empty column */
    set(PILE_TAB + 6, "QS");
    set(PILE_TAB, "KC");
    set(PILE_TAB + 3, "KH");
    set(PILE_TAB + 2, "JD");
    CHECK(game_smart(&g, PILE_TAB + 6, 1) == 0 && g.s.pile[PILE_TAB + 3].n == 2); /* QS on KH */
    set(PILE_TAB + 7, "5C");
    CHECK(game_smart(&g, PILE_TAB + 7, 1) == 0 && g.s.pile[PILE_CELL].n == 1); /* last, a cell */
    /* Klondike: all face up and the stock gone: it can finish */
    clear_all(GAME_KLONDIKE);
    char col[4][80];
    for (int su = 0; su < 4; su++) {
        col[su][0] = 0;
        static const char R[] = "KQJT98765432A";
        const char *S = "CDHS";
        for (int r = 0; r < 13; r++) { /* one suit a column: face up is all finishing needs */
            char nm[4] = {R[r], S[su], ' ', 0};
            strcat(col[su], nm);
        }
        set(PILE_TAB + su, col[su]);
    }
    CHECK(game_can_finish(&g.s));
    g.s.pile[PILE_TAB].c[3] &= (uint8_t)~CARD_UP;
    CHECK(!game_can_finish(&g.s));
    g.s.pile[PILE_TAB].c[3] |= CARD_UP;
    CHECK(game_auto(&g, 1) == 52 && game_won(&g.s) && !game_can_finish(&g.s) && g.nundo == 1);
    CHECK(game_undo(&g) == 0 && !game_won(&g.s) && game_can_finish(&g.s));
}

static void test_commands(void) {
    char text[2048], msg[120];
    CHECK(ok("new klondike 42"));
    size_t n = game_text(&g.s, text, sizeof text);
    CHECK(n == strlen(text) && strstr(text, "Klondike, draw 1  seed 42") && strstr(text, "T5: ## ## ## ## AH"));
    CHECK(game_text(&g.s, text, 10) == n && strlen(text) == 9);
    CHECK(ok("move t5 f1") && g.s.pile[PILE_FOUND].n == 1 && g.s.score == 15); /* home 10, turned up 5 */
    CHECK(fails("move t5 f1", "rules do not allow"));
    CHECK(ok("draw") && g.s.pile[PILE_WASTE].n == 1);
    CHECK(ok("undo") && ok("undo") && g.s.pile[PILE_FOUND].n == 0 && g.s.score == 0);
    CHECK(fails("undo", "nothing to undo"));
    CHECK(fails("move t1", "move FROM TO") && fails("move t1 t9", "move FROM TO") && fails("move t1 t2 0", "move FROM TO"));
    CHECK(fails("smart", "smart FROM") && fails("draw now", "nothing after it") && fails("jump t1", "unknown"));
    CHECK(fails("new chess", "new klondike") && fails("new freecell x", "new klondike") && fails("move a b c d", "too many"));
    CHECK(ok("new klondike3 5") && g.s.draw == 3 && ok("draw") && g.s.pile[PILE_WASTE].n == 3);
    CHECK(ok("new freecell 7") && g.s.kind == GAME_FREECELL && g.s.seed == 7);
    CHECK(fails("draw", "no stock"));
    game_text(&g.s, text, sizeof text);
    CHECK(strstr(text, "FreeCell  seed 7") && strstr(text, "T8: 3H 8C 7S 3S KH 7D") && strstr(text, "C: -- -- -- --"));
    /* the longest run that may go, when no count is given */
    clear_all(GAME_FREECELL);
    set(PILE_TAB, "9H 8S 7D");
    set(PILE_TAB + 1, "TC");
    set(PILE_TAB + 2, "9C");
    CHECK(ok("move t1 t2") && g.s.pile[PILE_TAB + 1].n == 4);
    CHECK(fails("move t2 t3", "rules do not allow"));
    CHECK(fails("move t2 t3 2", "rules do not allow")); /* 8S on 9C: black on black */
    CHECK(ok("move t2 c1 1") && ok("smart c1") && g.s.pile[PILE_TAB + 1].n == 4); /* 7D back on 8S */
    (void)msg;
}

/* random legal play: the cards stay 52, each once; undoing all gives the deal back */
static void test_random_play(void) {
    srand(3);
    for (int round = 0; round < 200; round++) {
        int kind = round % 2 ? GAME_FREECELL : GAME_KLONDIKE;
        game_new(&g, kind, round % 4 == 0 ? 3 : 1, (uint32_t)round * 7919u);
        GameState start = g.s;
        int steps = 0, good = 1;
        for (int k = 0; k < 300; k++) {
            int r = rand() % 10;
            if (r == 0 && kind == GAME_KLONDIKE) steps += !game_draw(&g);
            else if (r == 1) steps += game_auto(&g, rand() % 2) > 0;
            else if (r == 2) steps += !game_smart(&g, PILE_TAB + rand() % 8, 1 + rand() % 3);
            else steps += !game_move(&g, rand() % PILE_COUNT, 1 + rand() % 4, rand() % PILE_COUNT);
            good &= total_cards(&g.s) == 52 && every_card_once(&g.s);
        }
        CHECK(good && g.nundo == steps);
        while (!game_undo(&g)) {}
        CHECK(!memcmp(&start, &g.s, sizeof start));
    }
}

static int count_mines(const Mines *m) {
    int k = 0;
    for (int i = 0; i < m->w * m->h; i++) k += (m->sq[i] & MINE_HERE) != 0;
    return k;
}

/* open every square without a mine, by what the field holds */
static void clear_field(Mines *m) {
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++)
            if (!(mines_at(m, x, y) & MINE_HERE)) mines_open(m, x, y);
}

static int mine_ok(const char *line) {
    char msg[120];
    int rc = mines_command(&mn, line, msg, sizeof msg);
    if (rc) printf("  %s: %s\n", line, msg);
    return rc == 0;
}
static int mine_fails(const char *line, const char *why) {
    char msg[120];
    int rc = mines_command(&mn, line, msg, sizeof msg);
    if (rc == 0 || !strstr(msg, why)) {
        printf("  %s: rc %d, said \"%s\"\n", line, rc, msg);
        return 0;
    }
    return 1;
}

static void test_mines(void) {
    CHECK(mines_new(&mn, 1, 5, 1, 0) == -1 && mines_new(&mn, 41, 5, 1, 0) == -1 && mines_new(&mn, 5, 25, 1, 0) == -1);
    CHECK(mines_new(&mn, 4, 4, 8, 0) == -1 && mines_new(&mn, 4, 4, 7, 0) == 0 && mines_new(&mn, 9, 9, 0, 0) == -1);
    /* the first square and its neighbours are always clear; the count is right; counts are right */
    int good = 1;
    for (int k = 0; k < 300; k++) {
        int w, h, n;
        mines_size_of(k % 3, &w, &h, &n);
        mines_new(&mn, w, h, n, (uint32_t)k);
        int fx = (k * 7) % w, fy = (k * 5) % h;
        good &= mines_open(&mn, fx, fy) > 0 && count_mines(&mn) == n && mn.state != MINES_LOST;
        for (int j = -1; j <= 1; j++)
            for (int i = -1; i <= 1; i++) good &= !(mines_at(&mn, fx + i, fy + j) & MINE_HERE);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int c = 0;
                for (int j = -1; j <= 1; j++)
                    for (int i = -1; i <= 1; i++) c += (mines_at(&mn, x + i, y + j) & MINE_HERE) != 0;
                good &= (mines_at(&mn, x, y) & 15) == c;
                /* an open square with none around has every neighbour open */
                if ((mines_at(&mn, x, y) & MINE_OPEN) && !c)
                    for (int j = -1; j <= 1; j++)
                        for (int i = -1; i <= 1; i++)
                            if (x + i >= 0 && y + j >= 0 && x + i < w && y + j < h) good &= (mines_at(&mn, x + i, y + j) & MINE_OPEN) != 0;
            }
    }
    CHECK(good);
    /* the same seed and first square: the same field */
    mines_new(&mn, 16, 16, 40, 77);
    mines_open(&mn, 3, 3);
    Mines a = mn;
    mines_new(&mn, 16, 16, 40, 77);
    mines_open(&mn, 3, 3);
    CHECK(!memcmp(a.sq, mn.sq, sizeof a.sq));
    /* winning: every clear square open; the mines flagged for you */
    clear_field(&mn);
    CHECK(mn.state == MINES_WON && mn.flags == 40 && mines_left(&mn) == 0 && mn.opened == 256 - 40);
    CHECK(mines_open(&mn, 0, 0) == 0 && mines_mark(&mn, 0, 0, 1) == -1);
    /* losing: a mine; it shows, and then nothing more happens */
    mines_new(&mn, 9, 9, 10, 5);
    mines_open(&mn, 4, 4);
    int mx = -1, my = -1;
    for (int i = 0; i < 81 && mx < 0; i++)
        if (mn.sq[i] & MINE_HERE) mx = i % 9, my = i / 9;
    CHECK(mines_open(&mn, mx, my) == -1 && mn.state == MINES_LOST && mn.boom_x == mx && mn.boom_y == my);
    char text[1024];
    mines_text(&mn, text, sizeof text);
    CHECK(text[my * 10 + mx] == 'X' && strchr(text, '*') && strlen(text) == 90);
    CHECK(mine_fails("open 0 0", "lost"));
    /* flags and question marks */
    mines_new(&mn, 9, 9, 10, 5);
    CHECK(mines_mark(&mn, 2, 2, 1) == 0 && (mines_at(&mn, 2, 2) & MINE_FLAG) && mines_left(&mn) == 9);
    CHECK(mines_open(&mn, 2, 2) == 0 && !mn.laid); /* a flag keeps it shut */
    CHECK(mines_mark(&mn, 2, 2, 1) == 0 && (mines_at(&mn, 2, 2) & MINE_ASK) && mines_left(&mn) == 10);
    CHECK(mines_mark(&mn, 2, 2, 1) == 0 && !(mines_at(&mn, 2, 2) & (MINE_ASK | MINE_FLAG)));
    mines_mark(&mn, 2, 2, 0);
    CHECK(mines_mark(&mn, 2, 2, 0) == 0 && !(mines_at(&mn, 2, 2) & (MINE_ASK | MINE_FLAG))); /* without ?: flag, none */
    mines_mark(&mn, 2, 2, 1);
    mines_mark(&mn, 2, 2, 1);
    CHECK(mines_open(&mn, 2, 2) > 0 && !(mines_at(&mn, 2, 2) & MINE_ASK)); /* a ? opens */
    mines_text(&mn, text, sizeof text);
    CHECK(!strchr(text, '?') && strchr(text, '.'));
    /* chord: a number with its flags placed opens the rest around it */
    int cx = -1, cy = -1;
    for (int y = 0; y < 9 && cx < 0; y++)
        for (int x = 0; x < 9 && cx < 0; x++) {
            uint8_t q = mines_at(&mn, x, y);
            if (!(q & MINE_OPEN) || !(q & 15)) continue;
            int hidden_safe = 0;
            for (int j = -1; j <= 1; j++)
                for (int i = -1; i <= 1; i++) {
                    uint8_t r = mines_at(&mn, x + i, y + j);
                    hidden_safe += x + i >= 0 && y + j >= 0 && x + i < 9 && y + j < 9 && !(r & (MINE_OPEN | MINE_HERE));
                }
            if (hidden_safe) cx = x, cy = y;
        }
    CHECK(cx >= 0);
    CHECK(mines_chord(&mn, cx, cy) == 0); /* no flags yet: nothing */
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++)
            if (mines_at(&mn, cx + i, cy + j) & MINE_HERE && !(mines_at(&mn, cx + i, cy + j) & MINE_FLAG)) mines_mark(&mn, cx + i, cy + j, 0);
    int before = mn.opened;
    CHECK(mines_chord(&mn, cx, cy) > 0 && mn.opened > before && mn.state != MINES_LOST);
    /* a flag in the wrong place: chording sets off the mine it hid */
    mines_new(&mn, 9, 9, 10, 5);
    mines_open(&mn, 4, 4);
    cx = -1;
    for (int y = 0; y < 9 && cx < 0; y++)
        for (int x = 0; x < 9 && cx < 0; x++) {
            uint8_t q = mines_at(&mn, x, y);
            if (!(q & MINE_OPEN) || (q & 15) != 1) continue;
            int safe = -1, mine = -1;
            for (int j = -1; j <= 1; j++)
                for (int i = -1; i <= 1; i++) {
                    int nx = x + i, ny = y + j;
                    if (nx < 0 || ny < 0 || nx > 8 || ny > 8) continue;
                    uint8_t r = mines_at(&mn, nx, ny);
                    if (r & MINE_OPEN) continue;
                    if (r & MINE_HERE) mine = ny * 9 + nx;
                    else safe = ny * 9 + nx;
                }
            if (safe >= 0 && mine >= 0) {
                cx = x, cy = y;
                mines_mark(&mn, safe % 9, safe / 9, 0);
            }
        }
    CHECK(cx >= 0 && mines_chord(&mn, cx, cy) == -1 && mn.state == MINES_LOST);
    mines_text(&mn, text, sizeof text);
    CHECK(strchr(text, 'F') != NULL);
    /* the commands */
    CHECK(mine_ok("new small 9") && mn.w == 9 && mn.mines == 10 && mn.seed == 9);
    CHECK(mine_ok("new 20 10 30 4") && mn.w == 20 && mn.h == 10 && mn.mines == 30);
    CHECK(mine_ok("new large") && mn.w == 30 && mn.seed == 5);
    CHECK(mine_fails("new 50 10 3", "up to 40 x 24") && mine_fails("new 4 4 8", "room for a clear start"));
    CHECK(mine_fails("new huge", "new small") && mine_fails("new 1 2", "new small"));
    CHECK(mine_ok("open 10 5") && mn.laid && mine_fails("open 10 5", "open already"));
    CHECK(mine_fails("open 30 0", "a square of the field") && mine_fails("flag 1", "flag X Y") && mine_fails("open x 1", "open X Y"));
    CHECK(mine_fails("chord 10 5", "on an open number") || (mines_at(&mn, 10, 5) & 15));
    CHECK(mine_fails("dig 1 1", "unknown") && mine_fails("open 1 1 1 1 1 1 1", "too many"));
    CHECK(mine_ok("") );
}

/* ---- the window ---- */

#define W 960
#define H 680
static uint32_t screen[W * H];
static sqg_canvas cv;
static sqg_ui ui;
static sqg_input in;
static GamesGui gg;

static void frame(void) {
    sqg_begin(&ui, &cv, &in);
    games_gui_frame(&gg, &ui);
    sqg_end(&ui);
    in.pressed = in.released = in.pressed2 = in.released2 = 0;
    in.key = in.ctrl_key = 0;
    in.text[0] = 0;
}
static void move(int x, int y) {
    in.mx = x;
    in.my = y;
    frame();
}
static void click(int x, int y) {
    in.mx = x, in.my = y;
    in.down = in.pressed = 1;
    frame();
    in.down = 0, in.released = 1;
    frame();
}
static void middle(int pile, int index, int *x, int *y) {
    sqg_rect r = games_gui_card_rect(&gg, pile, index);
    *x = r.x + r.w / 2;
    *y = r.y + 8;
}
static void drag_card(int pile, int index, int to) {
    int x, y, tx, ty;
    middle(pile, index, &x, &y);
    const CardPile *p = &g.s.pile[to];
    middle(to, p->n ? p->n - 1 : 0, &tx, &ty);
    in.mx = x, in.my = y;
    in.down = in.pressed = 1;
    frame();
    move(x + 10, y + 10);
    move(tx, ty + 10);
    in.down = 0, in.released = 1;
    frame();
}
static int top_x(const char *label) { /* the top bar's buttons, by what the window draws: find the label */
    static const char *const order[] = {"Klondike/FreeCell", "Draw 3", "Deal", "#seed", "Play", "New", "Restart", "Undo", "Home"};
    int x = 6 + 420 + 4;
    int lh = sqg_line_h(&ui);
    (void)lh;
    for (size_t k = 1; k < sizeof order / sizeof order[0]; k++) {
        const char *o = order[k];
        int w = !strcmp(o, "Draw 3") ? (g.s.kind == GAME_KLONDIKE ? sqg_text_width("Draw 3", 1) + 24 : -4)
                : !strcmp(o, "Deal") ? 6 + 4 + sqg_text_width("Deal", 1) + 4
                : !strcmp(o, "#seed") ? 90
                : sqg_button_w(&ui, o);
        if (!strcmp(o, label)) return x + (!strcmp(o, "Deal") ? 10 : 0) + w / 2;
        x += w + 4;
        if (!strcmp(o, "Restart")) x += 6 + 4;
    }
    return -1;
}

static void test_window(const char *dir) {
    sqg_canvas_init(&cv, screen, W, H, W);
    sqg_ui_init(&ui, NULL, 1);
    memset(&in, 0, sizeof in);
    game_new(&g, GAME_KLONDIKE, 1, 42);
    games_gui_init(&gg, &g, &mn, &hh, &ch, &tt);
    frame();
    int lh = sqg_line_h(&ui), x, y;
    /* the cards are drawn: the AH on column 5, face up, white with red */
    sqg_rect r = games_gui_card_rect(&gg, PILE_TAB + 4, 4);
    CHECK(screen[(r.y + 40) * W + r.x + 2] == 0xFFFFFFFFu && screen[(r.y + 60) * W + r.x + 40] == 0xFFC81E1Eu);
    int pile, index;
    CHECK(games_gui_hit(&gg, r.x + 5, r.y + 5, &pile, &index) == 0 && pile == PILE_TAB + 4 && index == 4);
    r = games_gui_card_rect(&gg, PILE_TAB + 6, 0);
    CHECK(games_gui_hit(&gg, r.x + 5, r.y + 3, &pile, &index) == 0 && pile == PILE_TAB + 6 && index == 0);
    CHECK(games_gui_hit(&gg, 3, H / 2, &pile, &index) == -1);
    /* a double click sends the ace home */
    middle(PILE_TAB + 4, 4, &x, &y);
    click(x, y);
    click(x, y);
    CHECK(g.s.pile[PILE_FOUND].n == 2 && g.s.score == 30); /* and the 2H after it, on its own */
    /* the stock turns a card */
    middle(PILE_STOCK, 0, &x, &y);
    click(x, y);
    CHECK(g.s.pile[PILE_WASTE].n == 1 && g.s.pile[PILE_STOCK].n == 23);
    /* drag: 7C (column 2) is no good on 8C; onto a red eight it would be */
    drag_card(PILE_TAB + 1, 1, PILE_TAB + 6);
    CHECK(g.s.pile[PILE_TAB + 1].n == 2 && strstr(gg.status, "cannot go there"));
    /* a drag that lands where a card can go: set one up */
    set(PILE_TAB + 6, "#4D #5D #6D #7D #8D #9D 8H");
    drag_card(PILE_TAB + 1, 1, PILE_TAB + 6);
    CHECK(g.s.pile[PILE_TAB + 6].n == 8 && g.s.pile[PILE_TAB + 1].n == 1 && (g.s.pile[PILE_TAB + 1].c[0] & CARD_UP));
    /* while dragging, the cards follow the pointer */
    middle(PILE_TAB + 6, 7, &x, &y);
    in.mx = x, in.my = y;
    in.down = in.pressed = 1;
    frame();
    move(400, 600);
    CHECK(gg.dragging && screen[(600 - 8 + 40) * W + 400 - 40 + 2] == 0xFFFFFFFFu);
    in.down = 0, in.released = 1;
    frame();
    CHECK(!gg.dragging && g.s.pile[PILE_TAB + 6].n == 8); /* dropped on nothing: back */
    /* the right button: the best place */
    set(PILE_WASTE, "3H");
    middle(PILE_WASTE, 0, &x, &y);
    in.mx = x, in.my = y;
    in.down2 = in.pressed2 = 1;
    frame();
    in.down2 = 0, in.released2 = 1;
    frame();
    CHECK(g.s.pile[PILE_FOUND].n == 3);
    /* Undo, and Ctrl+Z */
    click(top_x("Undo"), 6 + lh / 2);
    CHECK(g.s.pile[PILE_FOUND].n == 2 && strstr(gg.status, "Undone"));
    in.ctrl_key = 'z';
    frame();
    /* FreeCell, a deal by its number */
    click(6 + 120, 6 + lh / 2);
    CHECK(g.s.kind == GAME_FREECELL && gg.kind == GAME_FREECELL);
    snprintf(gg.seed_text, sizeof gg.seed_text, "7");
    click(top_x("Play"), 6 + lh / 2);
    CHECK(g.s.seed == 7 && strstr(gg.status, "FreeCell, deal 7"));
    snprintf(gg.seed_text, sizeof gg.seed_text, "x7");
    click(top_x("Play"), 6 + lh / 2);
    CHECK(g.s.seed == 7 && strstr(gg.status, "A deal is a number"));
    /* to a cell and back by dragging; a run too long for the cells is refused with why */
    drag_card(PILE_TAB + 7, 5, PILE_CELL);
    CHECK(g.s.pile[PILE_CELL].n == 1 && g.s.pile[PILE_TAB + 7].n == 5);
    clear_all(GAME_FREECELL);
    set(PILE_TAB, "KS QH JC TD 9S 8H");
    set(PILE_TAB + 1, "KD");
    for (int t = 2; t < 8; t++) set(PILE_TAB + t, "2C");
    set(PILE_CELL, "3C");
    set(PILE_CELL + 1, "3D");
    set(PILE_CELL + 2, "3H");
    frame();
    drag_card(PILE_TAB, 1, PILE_TAB + 1);
    CHECK(g.s.pile[PILE_TAB + 1].n == 1 && strstr(gg.status, "Only 2 cards can move together now"));
    /* New deals another; Restart the same again */
    game_new(&g, GAME_FREECELL, 1, 7);
    click(top_x("New"), 6 + lh / 2);
    CHECK(g.s.seed != 7);
    uint32_t s = g.s.seed;
    click(top_x("Restart"), 6 + lh / 2);
    CHECK(g.s.seed == s && g.s.moves == 0);
    /* a game one card from won: the last goes and the window says so */
    clear_all(GAME_FREECELL);
    static const char R[] = "A23456789TJQK";
    for (int su = 0; su < 4; su++) {
        char t[64] = "";
        for (int k = 0; k < (su == 3 ? 12 : 13); k++) {
            char nm[4] = {R[k], "CDHS"[su], ' ', 0};
            strcat(t, nm);
        }
        set(PILE_FOUND + su, t);
    }
    set(PILE_TAB, "KS");
    gg.auto_home = 0;
    middle(PILE_TAB, 0, &x, &y);
    click(x, y);
    click(x, y);
    CHECK(game_won(&g.s) && strstr(gg.status, "Won in"));
    frame();
    CHECK(screen[(gg.board.y + gg.board.h / 2) * W + W / 2] != 0xFF1E6B3Cu); /* the banner */
    char path[512];
    snprintf(path, sizeof path, "%s/games.ppm", dir);
    CHECK(sqg_write_ppm(&cv, path) == 0);
}

static void test_window_mines(void) {
    int lh = sqg_line_h(&ui);
    game_new(&g, GAME_KLONDIKE, 1, 42);
    games_gui_init(&gg, &g, &mn, &hh, &ch, &tt);
    gg.ms = 1000;
    frame();
    click(6 + 420 * 5 / 12, 6 + lh / 2); /* Mines */
    CHECK(gg.kind == GAMES_MINES && mn.w == 9 && mn.seed == 42 && strstr(gg.status, "Mines, field 42"));
    click(6 + 420 + 4 + 180 * 5 / 6, 6 + lh / 2); /* Large */
    CHECK(gg.mines_size == MINES_LARGE && mn.w == 30 && mn.mines == 99);
    click(6 + 420 + 4 + 180 / 6, 6 + lh / 2); /* Small again */
    CHECK(mn.w == 9);
    /* a click opens; the clock runs from then */
    sqg_rect r = games_gui_square_rect(&gg, 4, 4);
    int x, y;
    CHECK(games_gui_square(&gg, r.x + 3, r.y + 3, &x, &y) == 0 && x == 4 && y == 4);
    CHECK(games_gui_square(&gg, 2, 2, &x, &y) == -1);
    click(r.x + 5, r.y + 5);
    CHECK(mn.laid && (mines_at(&mn, 4, 4) & MINE_OPEN) && mn.ticks == 0);
    CHECK(screen[(r.y + 1) * W + r.x + 1] == 0xFFD8D8D8u); /* drawn open */
    gg.ms = 3500;
    frame();
    CHECK(mn.ticks == 1000); /* a long gap counts a second at most */
    gg.ms = 3600;
    frame();
    CHECK(mn.ticks == 1100);
    /* the right button flags a hidden square, then ?, then nothing */
    int hx = -1, hy = -1;
    for (int i = 0; i < 81 && hx < 0; i++)
        if (!(mn.sq[i] & MINE_OPEN)) hx = i % 9, hy = i / 9;
    r = games_gui_square_rect(&gg, hx, hy);
    in.mx = r.x + 5, in.my = r.y + 5;
    in.down2 = in.pressed2 = 1;
    frame();
    in.down2 = 0, in.released2 = 1;
    frame();
    CHECK((mines_at(&mn, hx, hy) & MINE_FLAG) && mines_left(&mn) == 9);
    CHECK(screen[(r.y + 2 * 2 + 3) * W + r.x + 3 + 6 * 2] == 0xFFD21E1Eu); /* the flag's red */
    /* clear the field by clicks: won, and the window says so */
    mines_mark(&mn, hx, hy, 0); /* without ask: flag, then none */
    for (int yy = 0; yy < 9; yy++)
        for (int xx = 0; xx < 9; xx++)
            if (!(mines_at(&mn, xx, yy) & (MINE_HERE | MINE_OPEN))) {
                r = games_gui_square_rect(&gg, xx, yy);
                click(r.x + 5, r.y + 5);
            }
    CHECK(mn.state == MINES_WON && strstr(gg.status, "Cleared in 1 seconds"));
    /* Restart: the same field again, hidden; New: another */
    int bx = 6 + 420 + 4 + 180 + 4 + 6 + 4 + sqg_text_width("Deal", 1) + 4 + 4 + 90 + 4 + sqg_button_w(&ui, "Play") + 4;
    int newx = bx + sqg_button_w(&ui, "New") / 2;
    int againx = bx + sqg_button_w(&ui, "New") + 4 + sqg_button_w(&ui, "Restart") / 2;
    click(againx, 6 + lh / 2);
    CHECK(mn.seed == 42 && !mn.laid && mn.opened == 0 && mn.state == MINES_PLAYING && mn.ticks == 0);
    click(newx, 6 + lh / 2);
    CHECK(mn.seed != 42 && !mn.laid && mn.w == 9 && gg.kind == GAMES_MINES);
}

/* ---- hearts ---- */

static uint8_t card_named(const char *s) {
    const char *R = "A23456789TJQK", *S = "CDHS";
    return CARD((int)(strchr(R, s[0]) - R) + 1, (int)(strchr(S, s[1]) - S));
}

/* a player's hand from names, "2C QS AH" */
static void hset(int p, const char *cards) {
    hh.n[p] = 0;
    for (const char *s = cards; *s;) {
        while (*s == ' ') s++;
        if (!*s) break;
        hh.hand[p][hh.n[p]++] = card_named(s);
        s += 2;
    }
}

/* a hand in play with these cards, player turn to lead (tricks taken already) */
static void hplay(int tricks, int turn, const char *p0, const char *p1, const char *p2, const char *p3) {
    hearts_new(&hh, 1);
    hh.phase = HEARTS_PLAYING;
    hh.tricks = tricks;
    hh.turn = hh.leader = turn;
    hset(0, p0), hset(1, p1), hset(2, p2), hset(3, p3);
}

static int hok(const char *line) {
    char msg[120];
    int rc = hearts_command(&hh, line, msg, sizeof msg);
    if (rc) printf("  %s: %s\n", line, msg);
    return rc == 0;
}
static int hfails(const char *line, const char *why) {
    char msg[120];
    int rc = hearts_command(&hh, line, msg, sizeof msg);
    if (rc == 0 || !strstr(msg, why)) {
        printf("  %s: rc %d, said \"%s\"\n", line, rc, msg);
        return 0;
    }
    return 1;
}

static int why_is(uint8_t c, const char *want) {
    const char *why;
    int rc = hearts_can_play(&hh, c, &why);
    if (!want) return rc == 0;
    return rc == -1 && strstr(why, want);
}

static void test_hearts(void) {
    /* deals: 52 cards once, 13 each, sorted; the same seed the same deal */
    int good = 1;
    for (uint32_t seed = 0; seed < 50; seed++) {
        hearts_new(&hh, seed);
        for (int hand = 0; hand < 4; hand++) {
            uint64_t seen = 0;
            for (int p = 0; p < 4; p++) {
                good &= hh.n[p] == 13;
                for (int i = 0; i < 13; i++) {
                    uint8_t c = hh.hand[p][i];
                    int b = CARD_SUIT(c) * 13 + CARD_RANK(c) - 1;
                    good &= !(seen >> b & 1) && CARD_RANK(c) >= 1 && CARD_RANK(c) <= 13;
                    seen |= (uint64_t)1 << b;
                }
            }
            good &= seen == ((uint64_t)1 << 52) - 1;
            hh.phase = HEARTS_HAND_OVER;
            hearts_next_hand(&hh);
        }
    }
    CHECK(good);
    static Hearts a, b;
    hearts_new(&a, 9), hearts_new(&b, 9);
    CHECK(!memcmp(&a, &b, sizeof a));
    hearts_new(&b, 10);
    CHECK(memcmp(a.hand, b.hand, sizeof a.hand) != 0);
    hearts_new(&hh, 7);
    CHECK(hh.hand[0][0] == card_named("4C") && hh.hand[0][12] == card_named("AH")); /* clubs first, hearts last, aces high */
    /* passing: left, right, across, none */
    CHECK(hh.phase == HEARTS_PASSING && hearts_pass_dir(&hh) == HEARTS_PASS_LEFT && hearts_pass_to(&hh, 0) == 1 &&
          hearts_pass_to(&hh, 3) == 0);
    hh.hand_no = 1;
    CHECK(hearts_pass_to(&hh, 0) == 3 && hearts_pass_to(&hh, 1) == 0);
    hh.hand_no = 2;
    CHECK(hearts_pass_to(&hh, 0) == 2 && hearts_pass_to(&hh, 1) == 3);
    hh.hand_no = 3;
    CHECK(hearts_pass_dir(&hh) == HEARTS_PASS_NONE && hearts_pass_to(&hh, 2) == 2);
    hearts_new(&hh, 7);
    uint8_t bad[3] = {card_named("AH"), card_named("AH"), card_named("KS")};
    CHECK(hearts_pass(&hh, bad) == -1);
    bad[1] = card_named("2C"); /* not ours */
    CHECK(hearts_pass(&hh, bad) == -1 && hh.phase == HEARTS_PASSING);
    uint8_t three[3] = {card_named("AH"), card_named("KS"), card_named("KD")};
    uint8_t east[3];
    hearts_ai_pass(&hh, 3, east);
    CHECK(hearts_pass(&hh, three) == 0 && hh.phase == HEARTS_PLAYING);
    CHECK(hh.n[0] == 13 && hh.n[1] == 13 && hh.n[2] == 13 && hh.n[3] == 13);
    CHECK(hearts_has(&hh, 1, three[0]) && hearts_has(&hh, 1, three[1]) && !hearts_has(&hh, 0, three[2]));
    CHECK(hearts_has(&hh, 0, east[0]) && hearts_has(&hh, 0, east[1]) && hearts_has(&hh, 0, east[2]));
    CHECK(hearts_has(&hh, hh.turn, card_named("2C")));
    /* the hand with no pass starts at once */
    hearts_new(&hh, 7);
    for (int k = 0; k < 3; k++) hh.phase = HEARTS_HAND_OVER, hearts_next_hand(&hh);
    CHECK(hh.hand_no == 3 && hh.phase == HEARTS_PLAYING && hearts_has(&hh, hh.turn, card_named("2C")));

    /* the first trick: the two of clubs leads; no points on it unless nothing else */
    hplay(0, 0, "2C 5C QS", "3C 4D AH", "KD 2H 3H", "JH QH KH");
    CHECK(why_is(card_named("5C"), "two of clubs") && why_is(card_named("2C"), NULL));
    CHECK(hearts_play(&hh, card_named("2C")) == 0 && hh.turn == 1);
    CHECK(why_is(card_named("4D"), "follow suit") && why_is(card_named("3C"), NULL));
    hearts_play(&hh, card_named("3C"));
    CHECK(why_is(card_named("2H"), "no points") && why_is(card_named("KD"), NULL));
    hearts_play(&hh, card_named("KD"));
    CHECK(why_is(card_named("QH"), NULL)); /* nothing but hearts: allowed */
    hearts_play(&hh, card_named("QH"));
    CHECK(hh.ntrick == 0 && hh.tricks == 1 && hh.last_winner == 1 && hh.taken[1] == 1 && hh.turn == 1 && hh.broken);
    CHECK(hh.last[0] == card_named("2C") && hh.last[3] == card_named("QH"));
    /* hearts not broken: no heart led unless nothing else */
    hplay(3, 2, "5C", "6C", "2H 3D", "7C");
    CHECK(why_is(card_named("2H"), "not broken") && why_is(card_named("3D"), NULL));
    hplay(3, 2, "5C", "6C", "2H 3H", "7C");
    CHECK(why_is(card_named("2H"), NULL));
    CHECK(why_is(card_named("AS"), "not in the hand"));
    /* the highest of the suit led takes it, an ace high; off suit never */
    hplay(5, 0, "5D", "AS", "KD", "AD");
    hset(1, "AS QS");
    hh.broken = 1;
    hearts_play(&hh, card_named("5D"));
    hearts_play(&hh, card_named("QS"));
    hearts_play(&hh, card_named("KD"));
    hearts_play(&hh, card_named("AD"));
    CHECK(hh.last_winner == 3 && hh.taken[3] == 13 && hh.turn == 3);
    /* the last trick scores the hand; all 26 to one: the moon */
    hplay(12, 1, "2D", "AH", "3D", "4D");
    hh.taken[1] = 25;
    hearts_play(&hh, card_named("AH"));
    hearts_play(&hh, card_named("3D"));
    hearts_play(&hh, card_named("4D"));
    hearts_play(&hh, card_named("2D"));
    CHECK(hh.phase == HEARTS_HAND_OVER && hh.moon == 1 && hh.hand_score[1] == 0 && hh.hand_score[0] == 26 &&
          hh.score[0] == 26 && hh.score[2] == 26 && hh.score[1] == 0);
    CHECK(hearts_play(&hh, card_named("2D")) == -1 && hearts_winner(&hh) == -1);
    CHECK(hearts_next_hand(&hh) == 0 && hh.hand_no == 1 && hh.score[0] == 26 && hh.phase == HEARTS_PASSING);
    CHECK(hearts_next_hand(&hh) == -1);
    /* to 100: the game is over, the lowest wins */
    hplay(12, 0, "AH", "2H", "3H", "4H");
    hh.score[0] = 60, hh.score[1] = 99, hh.score[2] = 30, hh.score[3] = 40;
    hh.taken[2] = 10, hh.taken[1] = 3;
    hearts_play(&hh, card_named("AH")), hearts_play(&hh, card_named("2H"));
    hearts_play(&hh, card_named("3H")), hearts_play(&hh, card_named("4H"));
    CHECK(hh.phase == HEARTS_GAME_OVER && hh.score[0] == 64 && hh.score[2] == 40 && hearts_winner(&hh) == 2);
    CHECK(hearts_next_hand(&hh) == -1);

    /* the computer: rids itself of the queen when it can, and ducks under */
    hplay(4, 0, "5D", "QS 2C 9H", "4D KD", "3D 9D");
    hh.broken = 1;
    hearts_play(&hh, card_named("5D"));
    CHECK(hearts_ai_card(&hh) == card_named("QS"));
    hearts_play(&hh, card_named("QS"));
    CHECK(hearts_ai_card(&hh) == card_named("4D")); /* under the five */
    hearts_play(&hh, card_named("4D"));
    CHECK(hearts_ai_card(&hh) == card_named("3D"));
    hplay(4, 1, "4C", "2H 5C 9C", "KC", "3C");
    hh.broken = 1;
    CHECK(hearts_ai_card(&hh) == card_named("5C")); /* leads low, not a heart */

    /* the commands */
    hearts_new(&hh, 7);
    CHECK(hfails("play 2C", "pass three cards first"));
    CHECK(hfails("pass AH KS", "pass C C C") && hfails("pass AH KS 2C", "from your hand"));
    CHECK(hfails("pass AH KS XX", "pass C C C") && hfails("jump", "unknown") && hfails("next", "not over"));
    CHECK(hfails("play AH KH", "play C") && hfails("new 4294967296", "new [SEED]") && hfails("new 1 2", "new [SEED]"));
    CHECK(hok("pass ah ks kd") && hh.phase == HEARTS_PLAYING && hh.turn == 0 && hh.ntrick == 3);
    CHECK(hfails("pass 4C TC JC", "passed already"));
    CHECK(hfails("play 2C", "not in your hand") && hfails("play 4D", "follow suit"));
    CHECK(hok("play 4C") && hh.tricks >= 1 && (hh.turn == 0 || hh.phase != HEARTS_PLAYING));
    char text[2048];
    hearts_text(&hh, text, sizeof text);
    CHECK(strstr(text, "Hearts  game 7  hand 1  pass left  your turn") && strstr(text, "Passed AH KS KD to West; got") &&
          strstr(text, "\nHand  "));
    CHECK(hearts_text(&hh, text, 8) > 8 && strlen(text) == 7);
    CHECK(hok("new 12") && hh.seed == 12 && hh.hand_no == 0 && hok("new") && hh.seed == 13);
    hh.hand_no = 3;
    hh.phase = HEARTS_PLAYING;
    CHECK(hfails("pass 2C 3C 4C", "no passing"));

    /* the computer plays 200 games through: every card it picks is allowed,
     * the cards stay 52, each hand gives 26 (or 78 for the moon) */
    int legal = 1, sums = 1, ends = 1, cards = 1;
    for (uint32_t seed = 0; seed < 200; seed++) {
        hearts_new(&hh, seed);
        int guard = 0;
        while (hh.phase != HEARTS_GAME_OVER && guard++ < 2000) {
            if (hh.phase == HEARTS_PASSING) {
                uint8_t c[3];
                hearts_ai_pass(&hh, 0, c);
                legal &= hearts_pass(&hh, c) == 0;
            } else if (hh.phase == HEARTS_PLAYING) {
                legal &= hearts_play(&hh, hearts_ai_card(&hh)) == 0;
                int held = hh.ntrick;
                for (int p = 0; p < 4; p++) held += hh.n[p];
                cards &= held + 4 * hh.tricks == 52;
            } else {
                int s = hh.hand_score[0] + hh.hand_score[1] + hh.hand_score[2] + hh.hand_score[3];
                sums &= s == (hh.moon >= 0 ? 78 : 26);
                hearts_next_hand(&hh);
            }
        }
        ends &= hh.phase == HEARTS_GAME_OVER && hearts_winner(&hh) >= 0;
    }
    CHECK(legal && sums && ends && cards);
}

static void test_window_hearts(void) {
    int lh = sqg_line_h(&ui);
    game_new(&g, GAME_KLONDIKE, 1, 5);
    games_gui_init(&gg, &g, &mn, &hh, &ch, &tt);
    gg.ms = 1000;
    frame();
    click(6 + 420 * 7 / 12, 6 + lh / 2); /* Hearts */
    CHECK(gg.kind == GAMES_HEARTS && hh.seed == 5 && hh.phase == HEARTS_PASSING && strstr(gg.status, "pass left"));
    /* three cards chosen, raised; a fourth refused; one put back */
    for (int i = 0; i < 3; i++) {
        sqg_rect r = games_gui_hand_rect(&gg, 12 - i);
        click(r.x + 5, r.y + 30);
    }
    CHECK(gg.nchosen == 3 && games_gui_hand_rect(&gg, 12).y < games_gui_hand_rect(&gg, 0).y);
    CHECK(games_gui_hand_hit(&gg, games_gui_hand_rect(&gg, 0).x + 3, games_gui_hand_rect(&gg, 0).y + 3) == 0);
    sqg_rect r = games_gui_hand_rect(&gg, 0);
    click(r.x + 5, r.y + 40);
    CHECK(gg.nchosen == 3 && strstr(gg.status, "Three are chosen"));
    r = games_gui_hand_rect(&gg, 12);
    click(r.x + r.w - 8, r.y + 10);
    CHECK(gg.nchosen == 2);
    click(r.x + r.w - 8, r.y + 30);
    CHECK(gg.nchosen == 3);
    uint8_t passing[3] = {hh.hand[0][10], hh.hand[0][11], hh.hand[0][12]};
    sqg_rect b = games_gui_hearts_button(&gg);
    click(b.x + b.w / 2, b.y + b.h / 2);
    CHECK(hh.phase == HEARTS_PLAYING && strstr(gg.status, "Passed to West") && hearts_has(&hh, 1, passing[0]) &&
          hearts_has(&hh, 1, passing[2]) && gg.nchosen == 0);
    /* the computer plays at its pace; your cards by clicks, the wrong one refused */
    int refused = 0, clicks = 0, guard = 0, early = 0;
    while (hh.phase == HEARTS_PLAYING && guard++ < 3000) {
        if (hh.turn != 0) {
            int before = hh.ntrick + hh.tricks * 4;
            long due = gg.next_ms;
            frame(); /* no time has passed: not yet, unless it was due */
            if (hh.ntrick + hh.tricks * 4 != before && gg.ms < due) early = 1;
            gg.ms += 500;
            frame();
            continue;
        }
        int pick = -1, wrong = -1;
        for (int i = 0; i < hh.n[0]; i++) {
            if (!hearts_can_play(&hh, hh.hand[0][i], NULL)) {
                if (pick < 0) pick = i;
            } else if (wrong < 0) wrong = i;
        }
        if (wrong >= 0 && !refused) {
            r = games_gui_hand_rect(&gg, wrong);
            click(r.x + 5, r.y + 40);
            refused = hh.turn == 0 && gg.status[0] >= 'A' && gg.status[0] <= 'Z' && !strstr(gg.status, "Your");
        }
        r = games_gui_hand_rect(&gg, pick);
        int had = hh.n[0];
        click(r.x + 5, r.y + 40);
        clicks += hh.n[0] == had - 1;
    }
    CHECK(!early && refused == 1 && clicks == 13 && hh.phase == HEARTS_HAND_OVER && strstr(gg.status, "this hand"));
    /* the scores, then the next hand: passing right */
    b = games_gui_hearts_button(&gg);
    CHECK(screen[(b.y - 60) * W + b.x] != 0xFF1E6B3Cu); /* the scores' box over the felt */
    click(b.x + b.w / 2, b.y + b.h / 2);
    CHECK(hh.hand_no == 1 && hh.phase == HEARTS_PASSING && strstr(gg.status, "pass right"));
    /* New: another game; Restart: this one from the start */
    gg.ms += 10000;
    hh.score[1] = 50;
    int bx = 6 + 420 + 4 + 6 + 4 + sqg_text_width("Deal", 1) + 4 + 4 + 90 + 4 + sqg_button_w(&ui, "Play") + 4;
    click(bx + sqg_button_w(&ui, "New") + 4 + sqg_button_w(&ui, "Restart") / 2, 6 + lh / 2);
    CHECK(hh.seed == 5 && hh.hand_no == 0 && hh.score[1] == 0);
    click(bx + sqg_button_w(&ui, "New") / 2, 6 + lh / 2);
    CHECK(hh.seed != 5 && gg.kind == GAMES_HEARTS);
}

/* ---- chess ---- */

static long perft(const ChessPos *p, int depth) {
    int m[CHESS_MAX_MOVES], n = chess_moves(p, m);
    if (depth == 1) return n;
    long total = 0;
    for (int i = 0; i < n; i++) {
        static Chess step[8];
        step[depth].pos = *p;
        step[depth].n = 0;
        chess_make(&step[depth], m[i]);
        total += perft(&step[depth].pos, depth - 1);
    }
    return total;
}

static int cmove(const char *san) { /* a move by its notation in ch, made */
    int m = chess_parse(&ch.pos, san, strlen(san));
    if (m < 0) {
        printf("  %s: not legal\n", san);
        return 0;
    }
    return chess_make(&ch, m) == 0;
}

static int san_is(const char *move, const char *want) {
    int m = chess_parse(&ch.pos, move, strlen(move));
    char san[10] = "";
    if (m >= 0) chess_san(&ch.pos, m, san);
    if (strcmp(san, want)) printf("  %s: \"%s\", not \"%s\"\n", move, san, want);
    return !strcmp(san, want);
}

static int cok(const char *line) {
    char msg[120];
    int rc = chess_command(&ch, line, msg, sizeof msg);
    if (rc) printf("  %s: %s\n", line, msg);
    return rc == 0;
}
static int cfails(const char *line, const char *why) {
    char msg[120];
    int rc = chess_command(&ch, line, msg, sizeof msg);
    if (rc == 0 || !strstr(msg, why)) {
        printf("  %s: rc %d, said \"%s\"\n", line, rc, msg);
        return 0;
    }
    return 1;
}

static void test_chess(void) {
    /* the moves, counted to a depth from known positions */
    chess_new(&ch, 1);
    CHECK(perft(&ch.pos, 1) == 20 && perft(&ch.pos, 2) == 400 && perft(&ch.pos, 3) == 8902);
    CHECK(!chess_set_fen(&ch, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"));
    CHECK(perft(&ch.pos, 1) == 48 && perft(&ch.pos, 2) == 2039 && perft(&ch.pos, 3) == 97862);
    CHECK(!chess_set_fen(&ch, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"));
    CHECK(perft(&ch.pos, 1) == 14 && perft(&ch.pos, 2) == 191 && perft(&ch.pos, 3) == 2812);
    /* FEN both ways, and what is not a position */
    char fen[100];
    chess_new(&ch, 1);
    chess_fen(&ch.pos, fen, sizeof fen);
    CHECK(!strcmp(fen, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
    CHECK(!chess_set_fen(&ch, "r3k2r/8/8/3pP3/8/8/8/R3K2R w Kq d6 5 30"));
    chess_fen(&ch.pos, fen, sizeof fen);
    CHECK(!strcmp(fen, "r3k2r/8/8/3pP3/8/8/8/R3K2R w Kq d6 5 30") && ch.pos.ep == CHESS_SQ(3, 5));
    CHECK(!chess_set_fen(&ch, "4k3/8/8/8/8/8/8/4K3 b - -") && ch.pos.halfmove == 0 && ch.pos.fullmove == 1);
    CHECK(chess_set_fen(&ch, "4k3/8/8/8/8/8/8/4KK2 w - - 0 1") == -1);   /* two white kings */
    CHECK(chess_set_fen(&ch, "4k3/8/8/8/8/8/4K3 w - - 0 1") == -1);       /* seven ranks */
    CHECK(chess_set_fen(&ch, "4k3/8/8/8/8/8/8/R3K3 w - - 0 1") == 0);
    CHECK(chess_set_fen(&ch, "4k3/8/8/8/8/8/8/4R1K1 w - - 0 1") == -1);   /* black in check, white to move */
    CHECK(chess_set_fen(&ch, "P3k3/8/8/8/8/8/8/4K3 w - - 0 1") == -1);    /* a pawn on the last rank */
    CHECK(chess_set_fen(&ch, "4k3/8/8/8/8/8/8/4K3 x - - 0 1") == -1);
    CHECK(!chess_set_fen(&ch, "4k3/8/8/8/8/8/8/4K3 w KQkq - 0 1") && ch.pos.castle == 0); /* no rooks: no castling */
    /* castling: not out of, through or into check; the rook goes too; the rights go */
    CHECK(!chess_set_fen(&ch, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
    CHECK(san_is("e1g1", "O-O") && san_is("O-O-O", "O-O-O") && san_is("0-0", "O-O"));
    CHECK(cmove("O-O") && ch.pos.sq[CHESS_SQ(5, 0)] == CHESS_ROOK && ch.pos.sq[CHESS_SQ(6, 0)] == CHESS_KING &&
          !ch.pos.sq[CHESS_SQ(7, 0)] && ch.pos.castle == (CHESS_BK | CHESS_BQ));
    CHECK(cmove("Rxa1") && ch.pos.castle == CHESS_BK); /* the rook took a rook: neither has a's castling */
    CHECK(!chess_set_fen(&ch, "r3k2r/8/8/8/8/5r2/8/R3K2R w KQkq - 0 1")); /* f1 attacked: no O-O */
    CHECK(chess_parse(&ch.pos, "O-O", 3) == -1 && chess_parse(&ch.pos, "O-O-O", 5) >= 0);
    CHECK(!chess_set_fen(&ch, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1") && cmove("Kf1") && ch.pos.castle == (CHESS_BK | CHESS_BQ));
    CHECK(!chess_set_fen(&ch, "r3k2r/8/8/8/4r3/8/8/R3K2R w KQkq - 0 1")); /* in check: no castling at all */
    CHECK(chess_parse(&ch.pos, "O-O", 3) == -1 && chess_parse(&ch.pos, "O-O-O", 5) == -1);
    /* en passant: only at once */
    chess_new(&ch, 1);
    CHECK(cmove("e4") && cmove("a6") && cmove("e5") && cmove("d5") && san_is("exd6", "exd6") && san_is("e5d6", "exd6"));
    CHECK(cmove("a3") && cmove("h6") && chess_parse(&ch.pos, "exd6", 4) == -1);
    chess_new(&ch, 1);
    CHECK(cmove("e4") && cmove("a6") && cmove("e5") && cmove("d5") && cmove("exd6") && !ch.pos.sq[CHESS_SQ(3, 4)] &&
          ch.pos.sq[CHESS_SQ(3, 5)] == CHESS_PAWN);
    /* promotion, to any piece */
    CHECK(!chess_set_fen(&ch, "k7/4P3/8/8/8/8/8/4K3 w - - 0 1"));
    CHECK(san_is("e8=Q", "e8=Q+") && san_is("e7e8n", "e8=N") && san_is("e8R", "e8=R+") && chess_parse(&ch.pos, "e8", 2) == -1);
    CHECK(cmove("e7e8n") && ch.pos.sq[CHESS_SQ(4, 7)] == CHESS_KNIGHT);
    /* which piece: by file, by rank */
    CHECK(!chess_set_fen(&ch, "4k3/8/8/8/8/R7/8/R3K1N1 w - - 0 1"));
    CHECK(san_is("R1a2", "R1a2") && san_is("a3a2", "R3a2") && san_is("Ne2", "Ne2"));
    CHECK(!chess_set_fen(&ch, "4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1") && san_is("Nbd2", "Nbd2") && san_is("Nfd2", "Nfd2"));
    CHECK(chess_parse(&ch.pos, "Nd2", 3) == -1 && chess_parse(&ch.pos, "Qd2", 3) == -1 && chess_parse(&ch.pos, "zz", 2) == -1);
    /* checkmate, stalemate and the draws */
    chess_new(&ch, 1);
    CHECK(cmove("f3") && cmove("e5") && cmove("g4") && san_is("Qh4", "Qh4#") && cmove("Qh4"));
    CHECK(chess_state(&ch) == CHESS_CHECKMATE && chess_in_check(&ch.pos) && chess_make(&ch, CHESS_MOVE(8, 16, 0)) == -1);
    CHECK(!chess_set_fen(&ch, "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1") && chess_state(&ch) == CHESS_STALEMATE && !chess_in_check(&ch.pos));
    CHECK(!chess_set_fen(&ch, "4k3/8/8/8/8/8/8/R3K3 w - - 99 80") && chess_state(&ch) == CHESS_PLAYING && cmove("Ra2") &&
          chess_state(&ch) == CHESS_DRAW_FIFTY);
    chess_new(&ch, 1);
    CHECK(cmove("Nf3") && cmove("Nf6") && cmove("Ng1") && cmove("Ng8") && chess_state(&ch) == CHESS_PLAYING);
    CHECK(cmove("Nf3") && cmove("Nf6") && cmove("Ng1") && cmove("Ng8") && chess_state(&ch) == CHESS_DRAW_REPEAT);
    CHECK(!chess_set_fen(&ch, "4k3/8/8/8/8/8/8/2B1K3 w - - 0 1") && chess_state(&ch) == CHESS_DRAW_MATERIAL);
    CHECK(!chess_set_fen(&ch, "4k3/8/8/8/8/8/8/2BBK3 w - - 0 1") && chess_state(&ch) == CHESS_PLAYING);
    /* undo, all the way */
    chess_new(&ch, 1);
    ChessPos start = ch.pos;
    CHECK(cmove("e4") && cmove("e5") && chess_undo(&ch) == 0 && chess_undo(&ch) == 0 && chess_undo(&ch) == -1 &&
          !memcmp(&ch.pos, &start, sizeof start));
    /* the computer: mate in one, a free queen, and no move when there is none */
    CHECK(!chess_set_fen(&ch, "6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1"));
    for (int lv = 1; lv <= 3; lv++) {
        ch.level = lv;
        CHECK(chess_ai(&ch) == CHESS_MOVE(CHESS_SQ(0, 0), CHESS_SQ(0, 7), 0));
    }
    CHECK(!chess_set_fen(&ch, "4k3/8/8/3q4/8/8/3R4/4K3 w - - 0 1"));
    ch.level = 1;
    CHECK(chess_ai(&ch) == CHESS_MOVE(CHESS_SQ(3, 1), CHESS_SQ(3, 4), 0));
    CHECK(!chess_set_fen(&ch, "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1") && chess_ai(&ch) == -1);
    /* the computer against itself: every move legal, games end, undo goes back to the start */
    int legal = 1, back = 1;
    for (uint32_t seed = 1; seed <= 4; seed++) {
        chess_new(&ch, seed);
        ch.level = 1 + (int)(seed % 2);
        ch.human[0] = ch.human[1] = 0;
        for (int ply = 0; ply < 160 && chess_state(&ch) == CHESS_PLAYING; ply++) {
            int m = chess_ai(&ch);
            legal &= chess_is_legal(&ch.pos, m) && chess_make(&ch, m) == 0;
        }
        while (!chess_undo(&ch)) {}
        static Chess start_game;
        chess_new(&start_game, seed);
        back &= !memcmp(&ch.pos, &start_game.pos, sizeof ch.pos);
    }
    CHECK(legal && back);
    /* the same seed, the same game; another seed, another */
    static Chess a, b2;
    chess_new(&a, 5), chess_new(&b2, 5);
    a.human[0] = b2.human[0] = 0;
    a.level = b2.level = 2;
    chess_run(&a), chess_run(&b2);
    CHECK(a.n == b2.n && !memcmp(a.move, b2.move, sizeof a.move[0] * (size_t)a.n));

    /* the commands */
    chess_new(&ch, 1);
    CHECK(cok("move e4") && ch.n == 2 && ch.pos.side == CHESS_WHITE); /* the computer replied */
    CHECK(cfails("move Qh8", "not a legal move") && cfails("move", "move M") && cfails("jump", "unknown"));
    CHECK(cok("undo") && ch.n == 0 && cfails("undo", "nothing to undo"));
    CHECK(cfails("level 5", "1 to 4") && cfails("level x", "1 to 4") && cok("level 1") && ch.level == 1);
    CHECK(cok("new black 9") && ch.seed == 9 && ch.n == 1 && ch.human[1] && !ch.human[0] && ch.level == 1);
    CHECK(cok("new both") && ch.seed == 10 && ch.n == 0 && cok("move e4") && ch.n == 1 && cok("go") && ch.n == 2);
    CHECK(cfails("new purple", "new [white|black|both]"));
    CHECK(cok("fen 6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1") && cok("move Ra8") && chess_state(&ch) == CHESS_CHECKMATE);
    CHECK(cfails("move Kh8", "checkmate") && cfails("go", "checkmate") && cfails("fen 8/8/8 w", "not a position"));
    char text[4096];
    chess_new(&ch, 1);
    CHECK(cok("move e4"));
    chess_text(&ch, text, sizeof text);
    CHECK(strstr(text, "Chess  game 1  level 3  you white  white to move") && strstr(text, "4 | . . . . P . . . |") &&
          strstr(text, "Moves 1. e4 ") && strstr(text, "\nFEN "));
}

static void test_window_chess(void) {
    int lh = sqg_line_h(&ui);
    game_new(&g, GAME_KLONDIKE, 1, 3);
    chess_new(&ch, 3);
    games_gui_init(&gg, &g, &mn, &hh, &ch, &tt);
    gg.ms = 1000;
    frame();
    click(6 + 420 * 9 / 12, 6 + lh / 2); /* Chess */
    CHECK(gg.kind == GAMES_CHESS && ch.n == 0 && ch.human[0] && !ch.human[1] && strstr(gg.status, "Your move"));
    /* a1 at the bottom left; a square from a point */
    sqg_rect a1 = games_gui_chess_rect(&gg, 0), h8 = games_gui_chess_rect(&gg, 63);
    CHECK(a1.x < h8.x && a1.y > h8.y && games_gui_chess_square(&gg, a1.x + 3, a1.y + 3) == 0 &&
          games_gui_chess_square(&gg, 2, 2) == -1);
    /* a piece, then where it goes; the wrong place refused */
    sqg_rect r = games_gui_chess_rect(&gg, CHESS_SQ(4, 1));
    click(r.x + 30, r.y + 30);
    CHECK(gg.chess_sel == CHESS_SQ(4, 1));
    r = games_gui_chess_rect(&gg, CHESS_SQ(4, 4));
    click(r.x + 30, r.y + 30);
    CHECK(gg.chess_sel == -1 && ch.n == 0 && strstr(gg.status, "cannot go there"));
    r = games_gui_chess_rect(&gg, CHESS_SQ(4, 1));
    click(r.x + 30, r.y + 30);
    r = games_gui_chess_rect(&gg, CHESS_SQ(4, 3));
    click(r.x + 30, r.y + 30);
    CHECK(ch.n == 1 && ch.pos.sq[CHESS_SQ(4, 3)] == CHESS_PAWN && strstr(gg.status, "thinking"));
    CHECK(screen[(r.y + 8 + 3 * 12 + 1) * W + r.x + 8 + 3 * 7 + 1] == 0xFFF8F4ECu); /* the white pawn drawn there */
    frame();
    CHECK(ch.n == 1); /* the computer waits a moment */
    gg.ms += 400;
    frame();
    CHECK(ch.n == 2 && ch.pos.side == CHESS_WHITE && strstr(gg.status, "Your move"));
    /* Undo: back to your move; Black: the board turns round and the computer starts */
    int x = 6 + 420 + 4 + 6 + 4 + sqg_text_width("You play", 1) + 4 + 4;
    int levels = x + 150 + 4 + 6 + 4 + sqg_text_width("Level", 1) + 4 + 4;
    int newx = levels + 100 + 4 + 6 + 4;
    int undox = newx + sqg_button_w(&ui, "New") + 4;
    click(undox + 10, 6 + lh / 2);
    CHECK(ch.n == 0 && strstr(gg.status, "Your move"));
    click(levels + 100 / 8, 6 + lh / 2);
    CHECK(ch.level == 1);
    click(x + 150 / 2, 6 + lh / 2); /* Black */
    a1 = games_gui_chess_rect(&gg, 0), h8 = games_gui_chess_rect(&gg, 63);
    CHECK(gg.chess_side == 1 && ch.human[1] && !ch.human[0] && a1.x > h8.x && a1.y < h8.y && ch.level == 1);
    gg.ms += 400;
    frame();
    CHECK(ch.n == 1 && ch.pos.side == CHESS_BLACK);
    click(newx + 10, 6 + lh / 2);
    CHECK(ch.n == 0 && ch.seed != 3 && ch.human[1]);
    /* a pawn to the last rank becomes a queen; checkmate said */
    click(x + 150 * 5 / 6, 6 + lh / 2); /* Both */
    CHECK(ch.human[0] && ch.human[1] && !chess_set_fen(&ch, "6k1/4Pppp/8/8/8/8/8/6K1 w - - 0 1"));
    r = games_gui_chess_rect(&gg, CHESS_SQ(4, 6));
    click(r.x + 30, r.y + 30);
    r = games_gui_chess_rect(&gg, CHESS_SQ(4, 7));
    click(r.x + 30, r.y + 30);
    CHECK(ch.pos.sq[CHESS_SQ(4, 7)] == CHESS_QUEEN && chess_state(&ch) == CHESS_CHECKMATE && strstr(gg.status, "White wins"));
}

/* ---- tiles ---- */

static int tok(const char *line) {
    char msg[120];
    int rc = tiles_command(&tt, line, msg, sizeof msg);
    if (rc) printf("  %s: %s\n", line, msg);
    return rc == 0;
}
static int tfails(const char *line, const char *why) {
    char msg[120];
    int rc = tiles_command(&tt, line, msg, sizeof msg);
    if (rc == 0 || !strstr(msg, why)) {
        printf("  %s: rc %d, said \"%s\"\n", line, rc, msg);
        return 0;
    }
    return 1;
}

/* a free tile and one that is not, of other kinds or the same */
static int find_tile(int want_free, int kind_of, int not_kind) {
    for (int i = 0; i < tt.n; i++)
        if (tt.here[i] && tiles_free(&tt, i) == want_free && (kind_of < 0 || tt.kind[i] == kind_of) &&
            (not_kind < 0 || tt.kind[i] != not_kind))
            return i;
    return -1;
}

static void test_tiles(void) {
    /* every layout: 144 tiles, four of each kind, no two in one place, each
     * upper tile on tiles below; and every deal clears by its plan */
    int whole = 1, clears = 1;
    for (int layout = 0; layout < TILES_LAYOUTS; layout++)
        for (uint32_t seed = 0; seed < 40; seed++) {
            CHECK(tiles_new(&tt, layout, seed) == 0);
            int per[TILES_KINDS] = {0};
            for (int i = 0; i < tt.n; i++) {
                per[tt.kind[i]]++;
                int under = tt.z[i] == 0;
                for (int k = 0; k < tt.n; k++) {
                    if (k == i) continue;
                    int dx = tt.x[k] - tt.x[i], dy = tt.y[k] - tt.y[i];
                    if (tt.z[k] == tt.z[i] && dx > -2 && dx < 2 && dy > -2 && dy < 2) whole = 0;
                    if (tt.z[k] == tt.z[i] - 1 && dx > -2 && dx < 2 && dy > -2 && dy < 2) under = 1;
                }
                whole &= under;
            }
            whole &= tt.n == 144 && tt.left == 144 && tt.nplan == 144;
            for (int k = 0; k < TILES_KINDS; k++) whole &= per[k] == 4;
            for (int p = 0; p + 1 < tt.nplan; p += 2) clears &= tiles_match(&tt, tt.plan[p], tt.plan[p + 1]) == 0;
            clears &= tiles_state(&tt) == TILES_WON && tt.left == 0;
        }
    CHECK(whole && clears);
    CHECK(tiles_new(&tt, 3, 1) == -1 && tiles_new(&tt, -1, 1) == -1);
    /* the same number, the same deal */
    static Tiles a, b;
    tiles_new(&a, TILES_CROSS, 8), tiles_new(&b, TILES_CROSS, 8);
    CHECK(!memcmp(&a, &b, sizeof a));
    tiles_new(&b, TILES_CROSS, 9);
    CHECK(memcmp(a.kind, b.kind, sizeof a.kind) != 0);
    /* free: nothing on top, and one side open */
    tiles_new(&tt, TILES_PYRAMID, 3);
    CHECK(tiles_free(&tt, 0) && tiles_free(&tt, 9) && !tiles_free(&tt, 1)); /* the corners; the next along */
    CHECK(!tiles_free(&tt, 11 + 10)); /* under the second layer */
    int top = tt.n - 1;
    CHECK(tt.z[top] == 2 && tiles_free(&tt, top));
    /* a match: both free, the same kind; then undo */
    int i = find_tile(1, -1, -1), j = -1;
    for (int k = 0; k < tt.n; k++)
        if (k != i && tt.kind[k] == tt.kind[i] && tiles_free(&tt, k)) j = k;
    if (j < 0) { /* no free partner for the first: any free pair */
        CHECK(tiles_hint(&tt, &i, &j) == 0);
    }
    int other = find_tile(1, -1, tt.kind[i]), stuck = find_tile(0, tt.kind[i], -1);
    CHECK(tiles_match(&tt, i, other) == -1 && tiles_match(&tt, i, i) == -1);
    if (stuck >= 0) CHECK(tiles_match(&tt, i, stuck) == -1);
    int moves = tiles_moves(&tt);
    CHECK(moves > 0 && tiles_match(&tt, i, j) == 0 && tt.left == 142 && !tt.here[i] && tiles_match(&tt, i, j) == -1);
    CHECK(tiles_undo(&tt) == 0 && tt.left == 144 && tt.here[i] && tt.here[j] && tiles_moves(&tt) == moves && tiles_undo(&tt) == -1);
    /* stuck: no pair free; a shuffle deals what is left so it clears */
    tiles_new(&tt, TILES_PYRAMID, 3);
    int stuck_seen = 0, shuffled_clears = 1;
    for (uint32_t seed = 0; seed < 30; seed++) {
        tiles_new(&tt, (int)(seed % 3), seed);
        int h1, h2;
        while (tiles_state(&tt) == TILES_PLAYING && !tiles_hint(&tt, &h1, &h2)) tiles_match(&tt, h1, h2);
        if (tiles_state(&tt) != TILES_STUCK) continue;
        stuck_seen++;
        int left = tt.left, per[TILES_KINDS] = {0}, after[TILES_KINDS] = {0};
        for (int k = 0; k < tt.n; k++)
            if (tt.here[k]) per[tt.kind[k]]++;
        CHECK(tiles_hint(&tt, &h1, &h2) == -1 && tiles_moves(&tt) == 0);
        if (tiles_shuffle(&tt)) { /* (a few left that no deal could clear, as two tiles one on the other) */
            shuffled_clears &= tt.left == left && tiles_state(&tt) == TILES_STUCK;
            stuck_seen--;
            continue;
        }
        shuffled_clears &= tt.left == left && tt.nplan == left && tt.shuffles == 1;
        for (int k = 0; k < tt.n; k++)
            if (tt.here[k]) after[tt.kind[k]]++;
        shuffled_clears &= !memcmp(per, after, sizeof per); /* the same tiles, other places */
        for (int p = 0; p + 1 < tt.nplan; p += 2) shuffled_clears &= tiles_match(&tt, tt.plan[p], tt.plan[p + 1]) == 0;
        shuffled_clears &= tiles_state(&tt) == TILES_WON;
    }
    CHECK(stuck_seen > 5 && shuffled_clears);
    CHECK(tiles_shuffle(&tt) == -1); /* nothing left */
    char name[3];
    tiles_kind_name(0, name);
    CHECK(!strcmp(name, "C1"));
    tiles_kind_name(17, name);
    CHECK(!strcmp(name, "B9"));
    tiles_kind_name(TILES_NUMBERS + 4, name);
    CHECK(!strcmp(name, "N5"));
    tiles_kind_name(TILES_DRAGONS + 1, name);
    CHECK(!strcmp(name, "DG"));
    tiles_kind_name(TILES_MOON, name);
    CHECK(!strcmp(name, "MO"));

    /* the commands */
    CHECK(tok("new towers 4") && tt.layout == TILES_TOWERS && tt.seed == 4 && tok("new Cross") && tt.seed == 5);
    CHECK(tfails("new", "new pyramid") && tfails("new hexagon", "new pyramid") && tfails("jump", "unknown"));
    char msg[120];
    CHECK(tiles_command(&tt, "hint", msg, sizeof msg) == 0 && !strncmp(msg, "match ", 6));
    CHECK(tok(msg) && tt.left == 142 && tok("undo") && tt.left == 144 && tfails("undo", "nothing"));
    i = find_tile(1, -1, -1), other = find_tile(1, -1, tt.kind[i]);
    char line[40];
    snprintf(line, sizeof line, "match %d %d", i, other);
    CHECK(tfails(line, "not the same kind"));
    stuck = find_tile(0, -1, -1);
    snprintf(line, sizeof line, "match %d %d", i, stuck);
    CHECK(tfails(line, "not free") && tfails("match 1 1", "two different") && tfails("match 1 999", "match I J"));
    CHECK(tok("shuffle") && tt.shuffles == 1 && tt.left == 144);
    char text[2048];
    tiles_text(&tt, text, sizeof text);
    CHECK(strstr(text, "Tiles Cross  game 5  left 144  moves ") && strstr(text, "\nFree ") && strstr(text, ":"));
}

static void test_window_tiles(void) {
    int lh = sqg_line_h(&ui);
    game_new(&g, GAME_KLONDIKE, 1, 6);
    games_gui_init(&gg, &g, &mn, &hh, &ch, &tt);
    gg.ms = 1000;
    frame();
    click(6 + 420 * 11 / 12, 6 + lh / 2); /* Tiles */
    CHECK(gg.kind == GAMES_TILES && tt.seed == 6 && tt.layout == TILES_PYRAMID && tt.left == 144 && strstr(gg.status, "pairs can go"));
    /* the top tile is the one hit; a tile that is not free says so */
    int top = tt.n - 1;
    sqg_rect r = games_gui_tile_rect(&gg, top);
    CHECK(games_gui_tile_hit(&gg, r.x + 20, r.y + 20) == top && games_gui_tile_hit(&gg, 3, 300) == -1);
    int stuck = find_tile(0, -1, -1);
    r = games_gui_tile_rect(&gg, stuck);
    int hit = games_gui_tile_hit(&gg, r.x + 2, r.y + 2);
    if (hit == stuck) {
        click(r.x + 2, r.y + 2);
        CHECK(gg.tiles_sel == -1 && strstr(gg.status, "not free"));
    }
    /* Hint, then the two clicked: gone */
    int x = 6 + 420 + 4 + 6 + 4 + 180 + 4 + 6 + 4;
    int hintx = x + sqg_button_w(&ui, "New") + 4 + sqg_button_w(&ui, "Restart") + 4 + sqg_button_w(&ui, "Undo") + 4;
    click(hintx + 8, 6 + lh / 2);
    CHECK(gg.hint_a >= 0 && gg.hint_b >= 0 && gg.hint_until > gg.ms);
    int a = gg.hint_a, b = gg.hint_b;
    sqg_rect ra = games_gui_tile_rect(&gg, a), rb = games_gui_tile_rect(&gg, b);
    CHECK(games_gui_tile_hit(&gg, ra.x + 20, ra.y + 20) == a && games_gui_tile_hit(&gg, rb.x + 20, rb.y + 20) == b);
    click(ra.x + 20, ra.y + 20);
    CHECK(gg.tiles_sel == a);
    click(rb.x + 20, rb.y + 20);
    CHECK(!tt.here[a] && !tt.here[b] && tt.left == 142 && gg.tiles_sel == -1);
    /* Undo puts them back; Restart the same deal, New another; a layout */
    click(x + sqg_button_w(&ui, "New") + 4 + sqg_button_w(&ui, "Restart") + 4 + 8, 6 + lh / 2);
    CHECK(tt.left == 144 && tt.here[a]);
    tiles_match(&tt, a, b);
    click(x + sqg_button_w(&ui, "New") + 4 + 8, 6 + lh / 2);
    CHECK(tt.left == 144 && tt.seed == 6);
    click(x + 8, 6 + lh / 2);
    CHECK(tt.seed != 6 && tt.layout == TILES_PYRAMID);
    click(6 + 420 + 4 + 6 + 4 + 180 * 5 / 6, 6 + lh / 2); /* Cross */
    CHECK(tt.layout == TILES_CROSS && gg.tiles_layout == TILES_CROSS && tt.left == 144);
    /* cleared by its plan through the window: said so */
    int by_click = 0, last_click = 0;
    for (int p = 0; p + 1 < tt.nplan; p += 2) {
        ra = games_gui_tile_rect(&gg, tt.plan[p]);
        rb = games_gui_tile_rect(&gg, tt.plan[p + 1]);
        if (games_gui_tile_hit(&gg, ra.x + 20, ra.y + 20) != tt.plan[p] || games_gui_tile_hit(&gg, rb.x + 20, rb.y + 20) != tt.plan[p + 1])
            tiles_match(&tt, tt.plan[p], tt.plan[p + 1]), last_click = 0; /* (one only half in view: by hand) */
        else {
            click(ra.x + 20, ra.y + 20);
            click(rb.x + 20, rb.y + 20);
            by_click++, last_click = 1;
        }
    }
    frame();
    CHECK(tt.left == 0 && tiles_state(&tt) == TILES_WON && by_click > 50 && (!last_click || strstr(gg.status, "Cleared")));
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    test_deals();
    test_klondike();
    test_klondike_rules();
    test_freecell();
    test_smart_and_finish();
    test_commands();
    test_random_play();
    test_mines();
    test_window(dir);
    test_window_mines();
    test_hearts();
    test_window_hearts();
    test_chess();
    test_window_chess();
    test_tiles();
    test_window_tiles();
    printf("test_games: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
