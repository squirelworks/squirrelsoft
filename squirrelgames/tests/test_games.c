/* SquirrelGames tests: the rules of both games, the commands, and the
 * window driven by simulated input. */
#include "cards.h"
#include "gamesgui.h"
#include "mines.h"

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
    int x = 6 + 240 + 4;
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
    games_gui_init(&gg, &g, &mn);
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
    games_gui_init(&gg, &g, &mn);
    gg.ms = 1000;
    frame();
    click(6 + 240 * 5 / 6, 6 + lh / 2); /* Mines */
    CHECK(gg.kind == GAMES_MINES && mn.w == 9 && mn.seed == 42 && strstr(gg.status, "Mines, field 42"));
    click(6 + 240 + 4 + 180 * 5 / 6, 6 + lh / 2); /* Large */
    CHECK(gg.mines_size == MINES_LARGE && mn.w == 30 && mn.mines == 99);
    click(6 + 240 + 4 + 180 / 6, 6 + lh / 2); /* Small again */
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
    int bx = 6 + 240 + 4 + 180 + 4 + 6 + 4 + sqg_text_width("Deal", 1) + 4 + 4 + 90 + 4 + sqg_button_w(&ui, "Play") + 4;
    int newx = bx + sqg_button_w(&ui, "New") / 2;
    int againx = bx + sqg_button_w(&ui, "New") + 4 + sqg_button_w(&ui, "Restart") / 2;
    click(againx, 6 + lh / 2);
    CHECK(mn.seed == 42 && !mn.laid && mn.opened == 0 && mn.state == MINES_PLAYING && mn.ticks == 0);
    click(newx, 6 + lh / 2);
    CHECK(mn.seed != 42 && !mn.laid && mn.w == 9 && gg.kind == GAMES_MINES);
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
    printf("test_games: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
