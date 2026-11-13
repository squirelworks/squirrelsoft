/* gamesgui: the SquirrelGames window panel (see gamesgui.h). Original code,
 * the card faces and backs drawn here. */
#include "gamesgui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FELT 0xFF1E6B3Cu
#define FELT_DARK 0xFF14512Cu
#define INK_RED 0xFFC81E1Eu
#define INK_BLACK 0xFF141414u

void games_gui_init(GamesGui *gg, Game *game, Mines *mines, Hearts *hearts, Chess *chess) {
    memset(gg, 0, sizeof *gg);
    gg->chess = chess;
    gg->chess_sel = -1;
    gg->game = game;
    gg->mines = mines;
    gg->hearts = hearts;
    gg->kind = game->s.kind;
    gg->draw3 = game->s.draw == 3;
    gg->auto_home = 1;
    snprintf(gg->seed_text, sizeof gg->seed_text, "%lu", (unsigned long)game->s.seed);
    gg->last_frame = -100;
    snprintf(gg->status, sizeof gg->status, "Drag a card to move it; double click or right click sends it home");
}

/* ---- the suits, 9x9, drawn for this ---- */

static const char *const SUIT_ART[4][9] = {
    {"...###...", "..#####..", "..#####..", "#..###..#", "#########", "#########", "#..#.#..#", "....#....",
     "...###..."}, /* clubs */
    {"....#....", "...###...", "..#####..", ".#######.", "#########", ".#######.", "..#####..", "...###...",
     "....#...."}, /* diamonds */
    {".##...##.", "####.####", "#########", "#########", ".#######.", "..#####..", "...###...", "....#....",
     "........."}, /* hearts */
    {"....#....", "...###...", "..#####..", ".#######.", "#########", "#########", ".##.#.##.", "....#....",
     "...###..."}, /* spades */
};

static void draw_suit(sqg_canvas *cv, int x, int y, int suit, int scale, uint32_t c) {
    for (int j = 0; j < 9; j++)
        for (int i = 0; i < 9; i++)
            if (SUIT_ART[suit][j][i] == '#') sqg_fill(cv, sqg_rect_make(x + i * scale, y + j * scale, scale, scale), c);
}

/* a card's outline with its corners cut round */
static void card_shape(sqg_canvas *cv, int x, int y, uint32_t fill, uint32_t edge) {
    int w = GAMES_CARD_W, h = GAMES_CARD_H;
    sqg_fill(cv, sqg_rect_make(x + 2, y, w - 4, h), fill);
    sqg_fill(cv, sqg_rect_make(x, y + 2, w, h - 4), fill);
    sqg_fill(cv, sqg_rect_make(x + 1, y + 1, w - 2, h - 2), fill);
    sqg_hline(cv, x + 2, x + w - 3, y, edge);
    sqg_hline(cv, x + 2, x + w - 3, y + h - 1, edge);
    sqg_vline(cv, x, y + 2, y + h - 3, edge);
    sqg_vline(cv, x + w - 1, y + 2, y + h - 3, edge);
    sqg_pixel(cv, x + 1, y + 1, edge);
    sqg_pixel(cv, x + w - 2, y + 1, edge);
    sqg_pixel(cv, x + 1, y + h - 2, edge);
    sqg_pixel(cv, x + w - 2, y + h - 2, edge);
}

void games_gui_draw_card(sqg_canvas *cv, int x, int y, uint8_t card) {
    int w = GAMES_CARD_W, h = GAMES_CARD_H;
    if (!(card & CARD_UP)) { /* the back: a lattice in two blues */
        card_shape(cv, x, y, 0xFFF4F4F4u, 0xFF303030u);
        sqg_rect in = sqg_rect_make(x + 5, y + 5, w - 10, h - 10);
        sqg_fill(cv, in, 0xFF1F4E9Au);
        for (int j = 0; j < in.h; j++)
            for (int i = 0; i < in.w; i++)
                if ((i + j) % 8 == 0 || (i - j + 800) % 8 == 0) sqg_pixel(cv, in.x + i, in.y + j, 0xFF6F9BE0u);
        sqg_frame(cv, sqg_rect_inset(in, -1), 0xFF16386Eu);
        return;
    }
    card_shape(cv, x, y, 0xFFFFFFFFu, 0xFF303030u);
    int rank = CARD_RANK(card), suit = CARD_SUIT(card);
    uint32_t ink = CARD_RED(card) ? INK_RED : INK_BLACK;
    static const char *const NAMES[14] = {"?", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
    const char *nm = NAMES[rank <= 13 ? rank : 0];
    /* the corners: rank, and the suit under it; the bottom one too */
    sqg_text(cv, x + 5, y + 5, nm, ink, 2);
    draw_suit(cv, x + 5, y + 20, suit, 1, ink);
    int tw = sqg_text_width(nm, 2);
    sqg_text(cv, x + w - 4 - tw, y + h - 5 - 14, nm, ink, 2);
    draw_suit(cv, x + w - 14, y + h - 32, suit, 1, ink);
    /* the middle: a big suit for pips, a big letter for the court cards */
    if (rank >= 11) {
        sqg_frame(cv, sqg_rect_make(x + 18, y + 22, w - 36, h - 44), ink);
        int lw = sqg_text_width(nm, 5);
        sqg_text(cv, x + (w - lw) / 2 + 2, y + 34, nm, ink, 5);
        draw_suit(cv, x + (w - 9) / 2, y + h - 38, suit, 1, ink);
    } else {
        int s = rank == 1 ? 4 : 3;
        draw_suit(cv, x + (w - 9 * s) / 2, y + (h - 9 * s) / 2, suit, s, ink);
    }
}

/* ---- layout ---- */

static int columns_shown(const GamesGui *gg) { return game_columns(&gg->game->s); }

static int col_x(const GamesGui *gg, int col) {
    int n = columns_shown(gg), step = GAMES_CARD_W + 24, total = n * step - 24;
    return gg->board.x + (gg->board.w - total) / 2 + col * step;
}

/* the top row: Klondike's stock, waste, then foundations at 3..6;
 * FreeCell's cells 0..3, then foundations 4..7 */
static int top_col(const GamesGui *gg, int pile) {
    int fc = gg->game->s.kind == GAME_FREECELL;
    if (pile == PILE_STOCK) return 0;
    if (pile == PILE_WASTE) return 1;
    if (pile >= PILE_FOUND && pile < PILE_FOUND + 4) return (fc ? 4 : 3) + pile - PILE_FOUND;
    return pile - PILE_CELL;
}

static int is_column(int pile) { return pile >= PILE_TAB && pile < PILE_TAB + 8; }

/* the gaps between a column's cards: smaller when it would run off the bottom */
static void column_steps(const GamesGui *gg, int pile, int *down, int *up) {
    const CardPile *p = &gg->game->s.pile[pile];
    int nd = 0, nu = 0;
    for (int i = 0; i + 1 < p->n; i++) {
        if (p->c[i] & CARD_UP) nu++;
        else nd++;
    }
    *down = 8;
    *up = 30;
    int room = gg->board.y + gg->board.h - 8 - (gg->board.y + 16 + GAMES_CARD_H + 20) - GAMES_CARD_H;
    while ((nd * *down + nu * *up) > room && *up > 10) (*up)--;
    while ((nd * *down + nu * *up) > room && *down > 3) (*down)--;
}

sqg_rect games_gui_card_rect(const GamesGui *gg, int pile, int index) {
    const CardPile *p = &gg->game->s.pile[pile];
    int top_y = gg->board.y + 16;
    if (!is_column(pile)) {
        int x = col_x(gg, top_col(gg, pile)), y = top_y;
        if (pile == PILE_WASTE && index >= 0) { /* the last three fanned out */
            int first = p->n > 3 ? p->n - 3 : 0, k = index - first;
            if (gg->game->s.draw == 3 && k > 0) x += 18 * (k > 2 ? 2 : k);
        }
        return sqg_rect_make(x, y, GAMES_CARD_W, GAMES_CARD_H);
    }
    int down, up, y = top_y + GAMES_CARD_H + 20;
    column_steps(gg, pile, &down, &up);
    for (int i = 0; i < index && i < p->n; i++) y += p->c[i] & CARD_UP ? up : down;
    return sqg_rect_make(col_x(gg, pile - PILE_TAB), y, GAMES_CARD_W, GAMES_CARD_H);
}

static int pile_shown(const GamesGui *gg, int pile) {
    const GameState *s = &gg->game->s;
    if (pile == PILE_STOCK || pile == PILE_WASTE) return s->kind == GAME_KLONDIKE;
    if (pile >= PILE_CELL) return s->kind == GAME_FREECELL;
    if (is_column(pile)) return pile - PILE_TAB < game_columns(s);
    return 1;
}

int games_gui_hit(const GamesGui *gg, int x, int y, int *pile, int *index) {
    for (int p = 0; p < PILE_COUNT; p++) {
        if (!pile_shown(gg, p)) continue;
        int n = gg->game->s.pile[p].n;
        for (int i = n - 1; i >= 0; i--) { /* the top card first: it lies over the rest */
            if (!is_column(p) && i < n - 1) break;
            if (sqg_rect_contains(games_gui_card_rect(gg, p, i), x, y)) {
                *pile = p;
                *index = i;
                return 0;
            }
        }
        if (!n && sqg_rect_contains(games_gui_card_rect(gg, p, 0), x, y)) {
            *pile = p;
            *index = -1;
            return 0;
        }
    }
    return -1;
}

/* where dropped cards go: the shown pile whose place, or column, is under
 * the middle of the dragged card's top */
static int drop_target(const GamesGui *gg, int x, int y) {
    for (int p = 0; p < PILE_COUNT; p++) {
        if (!pile_shown(gg, p)) continue;
        sqg_rect r = games_gui_card_rect(gg, p, 0);
        if (is_column(p)) {
            int n = gg->game->s.pile[p].n;
            sqg_rect last = games_gui_card_rect(gg, p, n ? n - 1 : 0);
            r.h = last.y + last.h - r.y;
        }
        if (sqg_rect_contains(sqg_rect_inset(r, -10), x, y)) return p;
    }
    return -1;
}

/* ---- play ---- */

static void say(GamesGui *gg, const char *s) { snprintf(gg->status, sizeof gg->status, "%s", s); }

static void after_move(GamesGui *gg) {
    Game *g = gg->game;
    if (gg->auto_home) {
        if (game_can_finish(&g->s)) game_auto(g, 1);
        else game_auto(g, 0);
    }
    if (game_won(&g->s)) snprintf(gg->status, sizeof gg->status, "Won in %d moves!", g->s.moves);
}

static uint32_t current_seed(const GamesGui *gg) {
    return gg->kind == GAMES_MINES    ? gg->mines->seed
           : gg->kind == GAMES_HEARTS ? gg->hearts->seed
           : gg->kind == GAMES_CHESS  ? gg->chess->seed
                                      : gg->game->s.seed;
}

static void hearts_deal(GamesGui *gg, uint32_t seed);
static void chess_start(GamesGui *gg, uint32_t seed);
static void chess_said(GamesGui *gg);

static void deal(GamesGui *gg, uint32_t seed) {
    if (gg->kind == GAMES_HEARTS) {
        hearts_deal(gg, seed);
        return;
    }
    if (gg->kind == GAMES_CHESS) {
        chess_start(gg, seed);
        return;
    }
    if (gg->kind == GAMES_MINES) {
        int w, h, n;
        mines_size_of(gg->mines_size, &w, &h, &n);
        mines_new(gg->mines, w, h, n, seed);
        snprintf(gg->seed_text, sizeof gg->seed_text, "%lu", (unsigned long)seed);
        snprintf(gg->status, sizeof gg->status, "Mines, field %lu: the first square is always clear", (unsigned long)seed);
        return;
    }
    game_new(gg->game, gg->kind, gg->draw3 ? 3 : 1, seed);
    snprintf(gg->seed_text, sizeof gg->seed_text, "%lu", (unsigned long)seed);
    gg->dragging = gg->pending = 0;
    snprintf(gg->status, sizeof gg->status, "%s, deal %lu", gg->kind == GAME_FREECELL ? "FreeCell" : "Klondike",
             (unsigned long)seed);
    after_move(gg);
}

static void smart(GamesGui *gg, int pile, int index) {
    const CardPile *p = &gg->game->s.pile[pile];
    int count = index >= 0 ? p->n - index : 0;
    if (count < 1) return;
    if (game_smart(gg->game, pile, count)) say(gg, "Nowhere for that to go");
    else {
        say(gg, "");
        after_move(gg);
    }
}

static void do_undo(GamesGui *gg) {
    gg->dragging = gg->pending = 0;
    say(gg, game_undo(gg->game) ? "Nothing to undo" : "Undone");
}

/* the pointer on the table */
static void table_input(GamesGui *gg, sqg_ui *ui) {
    const sqg_input *in = &ui->in;
    Game *g = gg->game;
    int over = sqg_rect_contains(gg->board, in->mx, in->my) && !ui->active;
    if (gg->dragging) {
        if (!in->down) {
            int x = in->mx - gg->grab_x + GAMES_CARD_W / 2, y = in->my - gg->grab_y + 12;
            int to = drop_target(gg, x, y);
            gg->dragging = 0;
            if (to >= 0 && to != gg->from) {
                if (game_move(g, gg->from, gg->count, to)) {
                    int most = game_most(&g->s, to);
                    if (g->s.kind == GAME_FREECELL && gg->count > most)
                        snprintf(gg->status, sizeof gg->status, "Only %d cards can move together now", most);
                    else
                        say(gg, "That cannot go there");
                } else {
                    say(gg, "");
                    after_move(gg);
                }
            }
        }
        return;
    }
    if (gg->pending) { /* pressed on a card: a drag once it moves a little, else a click */
        if (!in->down) {
            gg->pending = 0;
        } else if (abs(in->mx - gg->press_x) + abs(in->my - gg->press_y) > 3) {
            gg->pending = 0;
            gg->dragging = 1;
        }
        return;
    }
    if (!over) return;
    int pile, index;
    if (in->pressed2) {
        if (!games_gui_hit(gg, in->mx, in->my, &pile, &index) && index >= 0) smart(gg, pile, index);
        else if (game_auto(g, game_can_finish(&g->s))) after_move(gg);
        return;
    }
    if (!in->pressed) return;
    if (games_gui_hit(gg, in->mx, in->my, &pile, &index)) return;
    int twice = gg->frame - gg->last_frame <= 25 && pile == gg->last_pile && index == gg->last_index;
    gg->last_frame = gg->frame;
    gg->last_pile = pile;
    gg->last_index = index;
    if (pile == PILE_STOCK) {
        if (game_draw(g)) say(gg, "No cards left to turn");
        else say(gg, "");
        gg->last_frame = -100;
        return;
    }
    if (index < 0) return;
    const CardPile *p = &g->s.pile[pile];
    if (twice) {
        gg->last_frame = -100;
        smart(gg, pile, index);
        return;
    }
    int count = p->n - index;
    if (!(p->c[index] & CARD_UP) || (is_column(pile) ? count > game_run(&g->s, pile) : count != 1)) return;
    if (pile >= PILE_FOUND && pile < PILE_FOUND + 4 && g->s.kind == GAME_FREECELL) return; /* home is home */
    sqg_rect r = games_gui_card_rect(gg, pile, index);
    gg->pending = 1;
    gg->from = pile;
    gg->count = count;
    gg->grab_x = in->mx - r.x;
    gg->grab_y = in->my - r.y;
    gg->press_x = in->mx;
    gg->press_y = in->my;
}

/* ---- drawing ---- */

static void draw_place(sqg_canvas *cv, sqg_rect r, const char *label) {
    sqg_fill(cv, sqg_rect_inset(r, 1), FELT_DARK);
    sqg_frame(cv, r, 0xFF3F8E5Au);
    if (label) {
        int w = sqg_text_width(label, 3);
        sqg_text(cv, r.x + (r.w - w) / 2, r.y + (r.h - 21) / 2, label, 0xFF3F8E5Au, 3);
    }
}

static void draw_table(GamesGui *gg, sqg_ui *ui) {
    sqg_canvas *cv = ui->cv;
    const GameState *s = &gg->game->s;
    sqg_fill(cv, gg->board, FELT);
    sqg_rect old = sqg_clip(cv, gg->board);
    for (int p = 0; p < PILE_COUNT; p++) {
        if (!pile_shown(gg, p)) continue;
        const CardPile *pl = &s->pile[p];
        int hide = gg->dragging && p == gg->from ? gg->count : 0;
        int shown = pl->n - hide;
        const char *label = p == PILE_STOCK ? "O" : p >= PILE_FOUND && p < PILE_FOUND + 4 ? "A" : NULL;
        draw_place(cv, games_gui_card_rect(gg, p, 0), label);
        int fan = p == PILE_WASTE && s->draw == 3 ? 3 : 1; /* draw 3: the last three fanned */
        int first = is_column(p) ? 0 : shown > fan ? shown - fan : 0;
        for (int i = first; i < shown; i++) {
            sqg_rect r = games_gui_card_rect(gg, p, i);
            games_gui_draw_card(cv, r.x, r.y, pl->c[i]);
        }
        if (p == PILE_STOCK && pl->n) { /* how many are left */
            sqg_rect r = games_gui_card_rect(gg, p, 0);
            char t[8];
            snprintf(t, sizeof t, "%d", pl->n);
            sqg_text(cv, r.x + 4, r.y + r.h + 3, t, 0xFFE0F0E0u, 1);
        }
    }
    if (gg->dragging) {
        const CardPile *pl = &s->pile[gg->from];
        int x = ui->in.mx - gg->grab_x, y = ui->in.my - gg->grab_y;
        for (int i = pl->n - gg->count; i < pl->n; i++, y += 30) games_gui_draw_card(cv, x, y, pl->c[i]);
    }
    if (game_won(s)) {
        const char *t = "You won!";
        int w = sqg_text_width(t, 6);
        sqg_rect b = sqg_rect_make(gg->board.x + (gg->board.w - w) / 2 - 20, gg->board.y + gg->board.h / 2 - 40, w + 40, 80);
        sqg_blend(cv, b, 0xC0000000u);
        sqg_text(cv, b.x + 20, b.y + 19, t, 0xFFFFE070u, 6);
    }
    cv->clip = old;
}

/* ---- mines ---- */

static const char *const MINE_ART[9] = {"....#....", ".#.###.#.", "..#####..", ".##.####.", "#########",
                                        ".#######.", "..#####..", ".#.###.#.", "....#...."};
static const char *const FLAG_ART[9] = {"...##....", "...####..", "...######", "...####..", "...##....",
                                        "...#.....", "...#.....", "..####...", ".######.."};

static void art(sqg_canvas *cv, const char *const *a, int x, int y, int scale, uint32_t c, uint32_t pole) {
    for (int j = 0; j < 9; j++)
        for (int i = 0; i < 9; i++)
            if (a[j][i] == '#')
                sqg_fill(cv, sqg_rect_make(x + i * scale, y + j * scale, scale, scale), a == FLAG_ART && (j >= 5 || i == 3) ? pole : c);
}

static sqg_rect field_rect(const GamesGui *gg) {
    const Mines *m = gg->mines;
    int w = m->w * GAMES_SQUARE, h = m->h * GAMES_SQUARE;
    return sqg_rect_make(gg->board.x + (gg->board.w - w) / 2, gg->board.y + 56 + (gg->board.h - 56 - h) / 3, w, h);
}

sqg_rect games_gui_square_rect(const GamesGui *gg, int x, int y) {
    sqg_rect f = field_rect(gg);
    return sqg_rect_make(f.x + x * GAMES_SQUARE, f.y + y * GAMES_SQUARE, GAMES_SQUARE, GAMES_SQUARE);
}

int games_gui_square(const GamesGui *gg, int sx, int sy, int *x, int *y) {
    sqg_rect f = field_rect(gg);
    if (!sqg_rect_contains(f, sx, sy)) return -1;
    *x = (sx - f.x) / GAMES_SQUARE;
    *y = (sy - f.y) / GAMES_SQUARE;
    return 0;
}

static void mines_said(GamesGui *gg) {
    const Mines *m = gg->mines;
    if (m->state == MINES_WON) snprintf(gg->status, sizeof gg->status, "Cleared in %ld seconds!", m->ticks / 1000);
    else if (m->state == MINES_LOST) say(gg, "Boom. New for another field, or Restart for this one");
    else say(gg, "");
}

static void mines_input(GamesGui *gg, sqg_ui *ui) {
    const sqg_input *in = &ui->in;
    Mines *m = gg->mines;
    if (m->state == MINES_PLAYING && m->laid && gg->ms > gg->last_ms) {
        long dt = gg->ms - gg->last_ms;
        m->ticks += dt > 1000 ? 1000 : dt; /* (a long pause, as when the window was away, counts a second) */
    }
    if (ui->active || m->state != MINES_PLAYING) return;
    int x, y;
    if (!(in->pressed || in->pressed2) || games_gui_square(gg, in->mx, in->my, &x, &y)) return;
    uint8_t s = mines_at(m, x, y);
    if (in->pressed2) mines_mark(m, x, y, 1);
    else if ((s & MINE_OPEN) && (s & 15)) mines_chord(m, x, y);
    else mines_open(m, x, y);
    mines_said(gg);
}

static void draw_mines(GamesGui *gg, sqg_ui *ui) {
    sqg_canvas *cv = ui->cv;
    const Mines *m = gg->mines;
    sqg_fill(cv, gg->board, 0xFF5A6470u);
    sqg_rect f = field_rect(gg);
    /* the counters: mines less flags, and the seconds */
    char t[32];
    snprintf(t, sizeof t, "%03d", mines_left(m) < -99 ? -99 : mines_left(m));
    sqg_rect c = sqg_rect_make(f.x, f.y - 46, 80, 36);
    sqg_fill(cv, c, 0xFF101010u);
    sqg_text(cv, c.x + 6, c.y + 4, t, 0xFFFF4030u, 4);
    long secs = m->ticks / 1000;
    snprintf(t, sizeof t, "%03ld", secs > 999 ? 999 : secs);
    c.x = f.x + f.w - 80;
    sqg_fill(cv, c, 0xFF101010u);
    sqg_text(cv, c.x + 6, c.y + 4, t, 0xFFFF4030u, 4);
    const char *face = m->state == MINES_WON ? "Cleared" : m->state == MINES_LOST ? "Boom" : "";
    if (*face) {
        int w = sqg_text_width(face, 3);
        sqg_text(cv, f.x + (f.w - w) / 2, f.y - 40, face, m->state == MINES_WON ? 0xFFFFE070u : 0xFFFF6050u, 3);
    }
    static const uint32_t NUM[9] = {0, 0xFF1E3CD2u, 0xFF0E7A1Eu, 0xFFD21E1Eu, 0xFF14147Au,
                                    0xFF7A1414u, 0xFF0E7A7Au, 0xFF141414u, 0xFF6E6E6Eu};
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) {
            sqg_rect r = games_gui_square_rect(gg, x, y);
            uint8_t s = mines_at(m, x, y);
            int lost = m->state == MINES_LOST, mine = s & MINE_HERE;
            if (s & MINE_OPEN || (lost && mine && !(s & MINE_FLAG))) {
                int boom = lost && x == m->boom_x && y == m->boom_y;
                sqg_fill(cv, r, boom ? 0xFFE03020u : 0xFFD8D8D8u);
                sqg_hline(cv, r.x, r.x + r.w - 1, r.y + r.h - 1, 0xFF9A9A9Au);
                sqg_vline(cv, r.x + r.w - 1, r.y, r.y + r.h - 1, 0xFF9A9A9Au);
                if (mine) art(cv, MINE_ART, r.x + 3, r.y + 3, 2, 0xFF141414u, 0);
                else if (s & 15) {
                    char d[2] = {(char)('0' + (s & 15)), 0};
                    sqg_text(cv, r.x + 7, r.y + 5, d, NUM[s & 15], 2);
                }
                continue;
            }
            sqg_fill(cv, r, 0xFFBEBEBEu); /* raised: light above and left, dark below and right */
            sqg_fill(cv, sqg_rect_make(r.x, r.y, r.w, 2), 0xFFF4F4F4u);
            sqg_fill(cv, sqg_rect_make(r.x, r.y, 2, r.h), 0xFFF4F4F4u);
            sqg_fill(cv, sqg_rect_make(r.x, r.y + r.h - 2, r.w, 2), 0xFF7A7A7Au);
            sqg_fill(cv, sqg_rect_make(r.x + r.w - 2, r.y, 2, r.h), 0xFF7A7A7Au);
            if (s & MINE_FLAG) {
                art(cv, FLAG_ART, r.x + 3, r.y + 3, 2, 0xFFD21E1Eu, 0xFF141414u);
                if (lost && !mine) { /* a flag where there was none: crossed */
                    sqg_line(cv, r.x + 3, r.y + 3, r.x + r.w - 4, r.y + r.h - 4, 0xFF141414u);
                    sqg_line(cv, r.x + r.w - 4, r.y + 3, r.x + 3, r.y + r.h - 4, 0xFF141414u);
                }
            } else if (s & MINE_ASK) {
                sqg_text(cv, r.x + 7, r.y + 5, "?", 0xFF141414u, 2);
            }
        }
}

/* ---- hearts ---- */

#define HAND_STEP 34
#define RAISE 22

static int hearts_cx(const GamesGui *gg) { return gg->board.x + gg->board.w / 2; }
static int hearts_cy(const GamesGui *gg) { return gg->board.y + gg->board.h / 2 - 50; }

sqg_rect games_gui_hand_rect(const GamesGui *gg, int i) {
    const Hearts *h = gg->hearts;
    int n = h->n[0], total = GAMES_CARD_W + (n > 0 ? n - 1 : 0) * HAND_STEP;
    int x = hearts_cx(gg) - total / 2 + i * HAND_STEP, y = gg->board.y + gg->board.h - GAMES_CARD_H - 12;
    for (int k = 0; k < gg->nchosen && i >= 0 && i < n; k++)
        if (gg->chosen[k] == h->hand[0][i]) y -= RAISE;
    return sqg_rect_make(x, y, GAMES_CARD_W, GAMES_CARD_H);
}

int games_gui_hand_hit(const GamesGui *gg, int x, int y) {
    for (int i = gg->hearts->n[0] - 1; i >= 0; i--) {
        sqg_rect r = games_gui_hand_rect(gg, i);
        if (sqg_rect_contains(r, x, y)) return i;
    }
    return -1;
}

sqg_rect games_gui_hearts_button(const GamesGui *gg) {
    return sqg_rect_make(hearts_cx(gg) - 70, hearts_cy(gg) + 140, 140, 26);
}

/* where each player's card of a trick goes */
static void trick_place(const GamesGui *gg, int p, int *x, int *y) {
    static const int DX[4] = {0, -95, 0, 95}, DY[4] = {65, 0, -65, 0};
    *x = hearts_cx(gg) - GAMES_CARD_W / 2 + DX[p];
    *y = hearts_cy(gg) - GAMES_CARD_H / 2 + DY[p];
}

static const char *const PASS_WORD[4] = {"left", "right", "across", ""};

static void hearts_said(GamesGui *gg) {
    const Hearts *h = gg->hearts;
    if (h->phase == HEARTS_PASSING)
        snprintf(gg->status, sizeof gg->status, "Choose three cards to pass %s", PASS_WORD[hearts_pass_dir(h)]);
    else if (h->phase == HEARTS_PLAYING && h->turn == 0)
        say(gg, h->ntrick ? "Your turn" : h->tricks == 0 ? "Your lead: the two of clubs" : "Your lead");
    else if (h->phase == HEARTS_HAND_OVER || h->phase == HEARTS_GAME_OVER) {
        char t[64] = "";
        if (h->moon >= 0) snprintf(t, sizeof t, "%s shot the moon! ", h->moon ? HEARTS_NAMES[h->moon] : "You");
        if (h->phase == HEARTS_HAND_OVER)
            snprintf(gg->status, sizeof gg->status, "%sYou took %d this hand", t, h->hand_score[0]);
        else {
            int w = hearts_winner(h);
            snprintf(gg->status, sizeof gg->status, "%s%s %s with %d", t, w ? HEARTS_NAMES[w] : "You", w ? "wins" : "win",
                     h->score[w]);
        }
    }
}

static void hearts_deal(GamesGui *gg, uint32_t seed) {
    hearts_new(gg->hearts, seed);
    gg->nchosen = 0;
    gg->next_ms = gg->ms + 400;
    gg->show_until = 0;
    snprintf(gg->seed_text, sizeof gg->seed_text, "%lu", (unsigned long)seed);
    hearts_said(gg);
}

/* a card played, by you or the computer: the trick it takes stays a moment */
static void hearts_played(GamesGui *gg) {
    const Hearts *h = gg->hearts;
    gg->next_ms = gg->ms + 450;
    if (h->ntrick == 0) gg->show_until = gg->next_ms = gg->ms + 1100;
    hearts_said(gg);
}

static void hearts_center_button(GamesGui *gg, sqg_ui *ui) {
    Hearts *h = gg->hearts;
    char label[24];
    if (h->phase == HEARTS_PASSING && gg->nchosen == 3) snprintf(label, sizeof label, "Pass %s", PASS_WORD[hearts_pass_dir(h)]);
    else if (h->phase == HEARTS_HAND_OVER) snprintf(label, sizeof label, "Next hand");
    else if (h->phase == HEARTS_GAME_OVER) snprintf(label, sizeof label, "New game");
    else return;
    if (!sqg_button(ui, sqg_id_str("hearts_go"), games_gui_hearts_button(gg), label)) return;
    if (h->phase == HEARTS_PASSING) {
        int to = hearts_pass_to(h, 0), from = 0;
        for (int p = 1; p < 4; p++)
            if (hearts_pass_to(h, p) == 0) from = p;
        hearts_pass(h, gg->chosen);
        gg->nchosen = 0;
        gg->next_ms = gg->ms + 400;
        char got[3][3];
        for (int i = 0; i < 3; i++) game_card_name((uint8_t)(h->passed[from][i] | CARD_UP), got[i]);
        snprintf(gg->status, sizeof gg->status, "Passed to %s; %s %s %s from %s", HEARTS_NAMES[to], got[0], got[1], got[2],
                 HEARTS_NAMES[from]);
    } else if (h->phase == HEARTS_HAND_OVER) {
        hearts_next_hand(h);
        gg->nchosen = 0;
        gg->next_ms = gg->ms + 400;
        gg->show_until = 0;
        hearts_said(gg);
    } else {
        hearts_deal(gg, h->seed * 2654435761u + 12345u);
    }
}

static void hearts_input(GamesGui *gg, sqg_ui *ui) {
    const sqg_input *in = &ui->in;
    Hearts *h = gg->hearts;
    if (h->phase == HEARTS_PLAYING && h->turn != 0 && gg->ms >= gg->next_ms) {
        hearts_play(h, hearts_ai_card(h));
        hearts_played(gg);
    }
    if (ui->active || !in->pressed) return;
    int i = games_gui_hand_hit(gg, in->mx, in->my);
    if (i < 0) {
        if (sqg_rect_contains(gg->board, in->mx, in->my)) gg->show_until = 0; /* a click moves on from the trick */
        return;
    }
    uint8_t c = h->hand[0][i];
    if (h->phase == HEARTS_PASSING) {
        for (int k = 0; k < gg->nchosen; k++)
            if (gg->chosen[k] == c) {
                gg->chosen[k] = gg->chosen[--gg->nchosen];
                hearts_said(gg);
                return;
            }
        if (gg->nchosen == 3) say(gg, "Three are chosen: click one to put it back");
        else {
            gg->chosen[gg->nchosen++] = c;
            if (gg->nchosen == 3) snprintf(gg->status, sizeof gg->status, "Pass them %s with the button", PASS_WORD[hearts_pass_dir(h)]);
        }
        return;
    }
    if (h->phase != HEARTS_PLAYING || h->turn != 0) return;
    const char *why;
    gg->show_until = 0;
    if (hearts_can_play(h, c, &why)) {
        snprintf(gg->status, sizeof gg->status, "%c%s", why[0] >= 'a' && why[0] <= 'z' ? why[0] - 32 : why[0], why + 1);
        return;
    }
    hearts_play(h, c);
    hearts_played(gg);
}

static void draw_name(sqg_canvas *cv, int x, int y, const Hearts *h, int p, int turn) {
    char t[40];
    snprintf(t, sizeof t, "%s  %d (%d)", HEARTS_NAMES[p], h->taken[p], h->score[p]);
    sqg_text(cv, x, y, t, turn ? 0xFFFFE070u : 0xFFE0F0E0u, 1);
}

static void draw_hearts(GamesGui *gg, sqg_ui *ui) {
    sqg_canvas *cv = ui->cv;
    const Hearts *h = gg->hearts;
    sqg_fill(cv, gg->board, FELT);
    sqg_rect old = sqg_clip(cv, gg->board);
    int playing = h->phase == HEARTS_PLAYING;
    /* the others' hands, face down: West down the left, North along the top, East down the right */
    int bx = gg->board.x, by = gg->board.y, bw = gg->board.w;
    int col_y = hearts_cy(gg) - (GAMES_CARD_H + 12 * 16) / 2;
    for (int i = 0; i < h->n[1]; i++) games_gui_draw_card(cv, bx + 24, col_y + i * 16, 0);
    for (int i = 0; i < h->n[3]; i++) games_gui_draw_card(cv, bx + bw - 24 - GAMES_CARD_W, col_y + i * 16, 0);
    int nw = GAMES_CARD_W + (h->n[2] > 0 ? h->n[2] - 1 : 0) * 16;
    for (int i = 0; i < h->n[2]; i++) games_gui_draw_card(cv, hearts_cx(gg) - nw / 2 + i * 16, by + 12 - 60, 0);
    draw_name(cv, bx + 24, col_y - 14, h, 1, playing && h->turn == 1);
    draw_name(cv, bx + bw - 24 - GAMES_CARD_W, col_y - 14, h, 3, playing && h->turn == 3);
    draw_name(cv, hearts_cx(gg) + nw / 2 + 10, by + 12, h, 2, playing && h->turn == 2);
    sqg_rect r0 = games_gui_hand_rect(gg, 0);
    draw_name(cv, r0.x, r0.y - RAISE - 14, h, 0, playing && h->turn == 0);
    /* your hand */
    for (int i = 0; i < h->n[0]; i++) {
        sqg_rect r = games_gui_hand_rect(gg, i);
        games_gui_draw_card(cv, r.x, r.y, (uint8_t)(h->hand[0][i] | CARD_UP));
        if (playing && h->turn == 0 && hearts_can_play(h, h->hand[0][i], NULL)) /* the cards you may not play: dimmed */
            sqg_blend(cv, sqg_rect_make(r.x, r.y, i + 1 < h->n[0] ? HAND_STEP : r.w, r.h), 0x50000000u);
    }
    /* the trick, or the one just taken */
    const uint8_t *t = h->trick;
    int taken = h->ntrick == 0 && h->last_winner >= 0 && gg->ms < gg->show_until;
    if (taken) t = h->last;
    for (int k = 0; k < 4; k++) {
        int p = (h->leader + k) % 4;
        if (taken) p = k;
        if (!t[p]) continue;
        int x, y;
        trick_place(gg, p, &x, &y);
        games_gui_draw_card(cv, x, y, (uint8_t)(t[p] | CARD_UP));
    }
    if (taken) {
        char s[40];
        snprintf(s, sizeof s, "%s %s", h->last_winner ? HEARTS_NAMES[h->last_winner] : "You", h->last_winner ? "takes it" : "take it");
        int w = sqg_text_width(s, 2);
        sqg_text(cv, hearts_cx(gg) - w / 2, hearts_cy(gg) + GAMES_CARD_H + 30, s, 0xFFFFE070u, 2);
    }
    if (h->phase >= HEARTS_HAND_OVER) { /* the scores */
        sqg_rect b = sqg_rect_make(hearts_cx(gg) - 170, hearts_cy(gg) - 110, 340, 230);
        sqg_blend(cv, b, 0xC0000000u);
        const char *head = h->phase == HEARTS_GAME_OVER ? (hearts_winner(h) ? "Game over" : "You won!") : "Hand over";
        sqg_text(cv, b.x + (b.w - sqg_text_width(head, 3)) / 2, b.y + 12, head, 0xFFFFE070u, 3);
        sqg_text(cv, b.x + 150, b.y + 50, "Hand", 0xFFE0F0E0u, 2);
        sqg_text(cv, b.x + 240, b.y + 50, "Game", 0xFFE0F0E0u, 2);
        for (int p = 0; p < 4; p++) {
            char s[16];
            int y = b.y + 80 + p * 26;
            sqg_text(cv, b.x + 20, y, HEARTS_NAMES[p], 0xFFFFFFFFu, 2);
            snprintf(s, sizeof s, "%d", h->hand_score[p]);
            sqg_text(cv, b.x + 150, y, s, 0xFFFFFFFFu, 2);
            snprintf(s, sizeof s, "%d", h->score[p]);
            sqg_text(cv, b.x + 240, y, s, 0xFFFFFFFFu, 2);
        }
    }
    cv->clip = old;
}

void games_gui_show(GamesGui *gg, int kind) {
    gg->kind = kind;
    gg->dragging = gg->pending = 0;
    snprintf(gg->seed_text, sizeof gg->seed_text, "%lu", (unsigned long)current_seed(gg));
    if (kind == GAMES_CHESS) {
        gg->chess_side = gg->chess->human[0] && gg->chess->human[1] ? 2 : gg->chess->human[1] ? 1 : 0;
        gg->chess_sel = -1;
        gg->next_ms = gg->ms + 300;
        chess_said(gg);
    } else if (kind == GAMES_HEARTS) {
        gg->nchosen = 0;
        gg->next_ms = gg->ms + 400;
        hearts_said(gg);
        if (gg->hearts->phase == HEARTS_PASSING) say(gg, "Choose three cards to pass, then the Pass button");
    } else if (kind == GAMES_MINES) {
        for (int k = 0; k < 3; k++) {
            int w, h, n;
            mines_size_of(k, &w, &h, &n);
            if (w == gg->mines->w && h == gg->mines->h && n == gg->mines->mines) gg->mines_size = k;
        }
        say(gg, "The left button opens a square, the right one flags it");
    }
}

/* ---- chess ---- */

#define CHESS_SQUARE 64

/* the pieces, 16x16, drawn for this: pawn, knight, bishop, rook, queen, king */
static const char *const PIECE_ART[6][16] = {
    {"................", "................", ".......##.......", "......####......", "......####......",
     ".......##.......", "......####......", ".....######.....", "......####......", "......####......",
     ".....######.....", "....########....", "...##########...", "...##########...", "................",
     "................"},
    {"................", ".......#.#......", "......######....", ".....########...", "....##.#######..",
     "...###########..", "...####..#####..", "....##..######..", ".......#######..", "......#######...",
     ".....########...", ".....########...", "....##########..", "...############.", "...############.",
     "................"},
    {"................", ".......##.......", "......####......", ".....######.....", ".....###.##.....",
     ".....##.###.....", ".....######.....", "......####......", ".......##.......", "......####......",
     ".....######.....", "......####......", "....########....", "...##########...", "...##########...",
     "................"},
    {"................", "...##..##..##...", "...##########...", "...##########...", "....########....",
     ".....######.....", ".....######.....", ".....######.....", ".....######.....", ".....######.....",
     "....########....", "...##########...", "..############..", "..############..", "................",
     "................"},
    {"................", "..#....##....#..", "..#...####...#..", "..##..####..##..", "..###.####.###..",
     "...##########...", "...##########...", "....########....", ".....######.....", ".....######.....",
     "....########....", "...##########...", "..############..", "..############..", "................",
     "................"},
    {".......##.......", "......####......", ".......##.......", ".....######.....", "....########....",
     "...##########...", "...##########...", "....########....", ".....######.....", ".....######.....",
     ".....######.....", "....########....", "...##########...", "..############..", "..############..",
     "................"},
};

void games_gui_draw_piece(sqg_canvas *cv, int x, int y, int piece) {
    if (!piece) return;
    const char *const *a = PIECE_ART[(piece < 0 ? -piece : piece) - 1];
    uint32_t fill = piece > 0 ? 0xFFF8F4ECu : 0xFF262626u, edge = piece > 0 ? 0xFF1A1A1Au : 0xFFD8D0C0u;
    for (int pass = 0; pass < 2; pass++)
        for (int j = 0; j < 16; j++)
            for (int i = 0; i < 16; i++)
                if (a[j][i] == '#') {
                    sqg_rect r = sqg_rect_make(x + i * 3, y + j * 3, 3, 3);
                    if (pass == 0) sqg_fill(cv, sqg_rect_inset(r, -2), edge); /* the outline first, */
                    else sqg_fill(cv, r, fill);                               /* then the body over it */
                }
}

static int chess_flipped(const GamesGui *gg) { return gg->chess->human[CHESS_BLACK] && !gg->chess->human[CHESS_WHITE]; }

static sqg_rect chess_board_rect(const GamesGui *gg) {
    int size = 8 * CHESS_SQUARE;
    return sqg_rect_make(gg->board.x + 48, gg->board.y + (gg->board.h - size) / 2, size, size);
}

sqg_rect games_gui_chess_rect(const GamesGui *gg, int sq) {
    sqg_rect b = chess_board_rect(gg);
    int f = sq & 7, r = sq >> 3;
    if (chess_flipped(gg)) f = 7 - f, r = 7 - r;
    return sqg_rect_make(b.x + f * CHESS_SQUARE, b.y + (7 - r) * CHESS_SQUARE, CHESS_SQUARE, CHESS_SQUARE);
}

int games_gui_chess_square(const GamesGui *gg, int x, int y) {
    sqg_rect b = chess_board_rect(gg);
    if (!sqg_rect_contains(b, x, y)) return -1;
    int f = (x - b.x) / CHESS_SQUARE, r = 7 - (y - b.y) / CHESS_SQUARE;
    if (chess_flipped(gg)) f = 7 - f, r = 7 - r;
    return CHESS_SQ(f, r);
}

static void chess_said(GamesGui *gg) {
    const Chess *c = gg->chess;
    int st = chess_state(c), side = c->pos.side, two = c->human[0] && c->human[1];
    static const char *const SIDE[2] = {"White", "Black"};
    if (st == CHESS_CHECKMATE) {
        if (two) snprintf(gg->status, sizeof gg->status, "Checkmate: %s wins", SIDE[!side]);
        else say(gg, c->human[side] ? "Checkmate: the computer wins" : "Checkmate: you win!");
    } else if (st == CHESS_STALEMATE) say(gg, "Stalemate: a draw");
    else if (st == CHESS_DRAW_FIFTY) say(gg, "A draw: fifty moves without a capture or a pawn moving");
    else if (st == CHESS_DRAW_REPEAT) say(gg, "A draw: the same position three times");
    else if (st == CHESS_DRAW_MATERIAL) say(gg, "A draw: too little left to mate");
    else if (!c->human[side]) say(gg, "The computer is thinking");
    else if (two) snprintf(gg->status, sizeof gg->status, "%s to move%s", SIDE[side], chess_in_check(&c->pos) ? ": check!" : "");
    else say(gg, chess_in_check(&c->pos) ? "Check! Your move" : "Your move");
}

static void chess_start(GamesGui *gg, uint32_t seed) {
    Chess *c = gg->chess;
    int level = c->level;
    chess_new(c, seed);
    c->level = level;
    c->human[CHESS_WHITE] = gg->chess_side != 1;
    c->human[CHESS_BLACK] = gg->chess_side != 0;
    gg->chess_sel = -1;
    gg->next_ms = gg->ms + 300;
    chess_said(gg);
}

static void chess_input(GamesGui *gg, sqg_ui *ui) {
    const sqg_input *in = &ui->in;
    Chess *c = gg->chess;
    if (chess_state(c) == CHESS_PLAYING && !c->human[c->pos.side] && gg->ms >= gg->next_ms) {
        chess_make(c, chess_ai(c));
        chess_said(gg);
    }
    if (ui->active || !in->pressed) return;
    int sq = games_gui_chess_square(gg, in->mx, in->my);
    if (sq < 0 || chess_state(c) != CHESS_PLAYING || !c->human[c->pos.side]) return;
    if (gg->chess_sel >= 0) {
        int m = CHESS_MOVE(gg->chess_sel, sq, 0);
        if (!chess_is_legal(&c->pos, m)) m = CHESS_MOVE(gg->chess_sel, sq, CHESS_QUEEN); /* a pawn on the last rank: a queen */
        if (chess_is_legal(&c->pos, m)) {
            chess_make(c, m);
            gg->chess_sel = -1;
            gg->next_ms = gg->ms + 300;
            chess_said(gg);
            return;
        }
    }
    int pc = c->pos.sq[sq];
    if (pc && (pc > 0) == (c->pos.side == CHESS_WHITE) && sq != gg->chess_sel) {
        int m[CHESS_MAX_MOVES], n = chess_moves(&c->pos, m), any = 0;
        for (int i = 0; i < n; i++) any |= CHESS_FROM(m[i]) == sq;
        gg->chess_sel = sq;
        if (!any) say(gg, "That piece has no move");
        else chess_said(gg);
    } else {
        if (gg->chess_sel >= 0 && sq != gg->chess_sel) say(gg, chess_in_check(&c->pos) ? "That does not get out of check" : "That piece cannot go there");
        gg->chess_sel = -1;
    }
}

static void draw_chess(GamesGui *gg, sqg_ui *ui) {
    sqg_canvas *cv = ui->cv;
    const Chess *c = gg->chess;
    const ChessPos *p = &c->pos;
    sqg_fill(cv, gg->board, 0xFF3A3D42u);
    sqg_rect old = sqg_clip(cv, gg->board);
    sqg_rect b = chess_board_rect(gg);
    sqg_frame(cv, sqg_rect_inset(b, -3), 0xFF1A1A1Au);
    int last_from = -1, last_to = -1;
    if (c->n) last_from = CHESS_FROM(c->move[c->n - 1]), last_to = CHESS_TO(c->move[c->n - 1]);
    int check_sq = -1;
    if (chess_in_check(p))
        for (int s = 0; s < 64; s++)
            if (p->sq[s] == (p->side == CHESS_WHITE ? CHESS_KING : -CHESS_KING)) check_sq = s;
    int targets[CHESS_MAX_MOVES], nt = 0;
    if (gg->chess_sel >= 0) {
        int m[CHESS_MAX_MOVES], n = chess_moves(p, m);
        for (int i = 0; i < n; i++)
            if (CHESS_FROM(m[i]) == gg->chess_sel) targets[nt++] = CHESS_TO(m[i]);
    }
    for (int s = 0; s < 64; s++) {
        sqg_rect r = games_gui_chess_rect(gg, s);
        int light = ((s & 7) + (s >> 3)) & 1;
        sqg_fill(cv, r, light ? 0xFFEAD9B5u : 0xFFAE8158u);
        if (s == last_from || s == last_to) sqg_blend(cv, r, 0x60E8D040u);
        if (s == gg->chess_sel) sqg_blend(cv, r, 0x7040A0E0u);
        if (s == check_sq) sqg_blend(cv, r, 0x90E02020u);
        games_gui_draw_piece(cv, r.x + 8, r.y + 8, p->sq[s]);
        for (int k = 0; k < nt; k++)
            if (targets[k] == s) {
                if (p->sq[s]) sqg_frame(cv, sqg_rect_inset(r, 2), 0xFF2060B0u), sqg_frame(cv, sqg_rect_inset(r, 3), 0xFF2060B0u);
                else sqg_blend(cv, sqg_rect_make(r.x + 26, r.y + 26, 12, 12), 0xA02060B0u);
            }
    }
    /* the files and ranks along the edges */
    for (int k = 0; k < 8; k++) {
        sqg_rect r = games_gui_chess_rect(gg, CHESS_SQ(k, 0));
        char t[2] = {(char)('a' + k), 0};
        sqg_text(cv, r.x + 28, b.y + b.h + 8, t, 0xFFD8D8D8u, 1);
        r = games_gui_chess_rect(gg, CHESS_SQ(0, k));
        t[0] = (char)('1' + k);
        sqg_text(cv, b.x - 16, r.y + 28, t, 0xFFD8D8D8u, 1);
    }
    /* the moves, two to a line, the latest in view */
    int mx = b.x + b.w + 40, my = b.y + 24, lines = (b.h - 24) / 14;
    sqg_text(cv, mx, b.y, "Moves", 0xFFFFE070u, 2);
    int off = c->n && c->before[0].side == CHESS_BLACK, total = (c->n + off + 1) / 2;
    int top = total > lines ? total - lines : 0;
    for (int i = 0; i < c->n; i++) {
        int line = (i + off) / 2 - top, col = (i + off) % 2;
        if (line < 0) continue;
        const ChessPos *bp = &c->before[i];
        char san[10], t[16];
        if (col == 0 || i == 0) {
            snprintf(t, sizeof t, "%d.", bp->fullmove);
            sqg_text(cv, mx, my + line * 14, t, 0xFFB0B0B0u, 1);
        }
        chess_san(bp, c->move[i], san);
        sqg_text(cv, mx + (col ? 110 : 40), my + line * 14, san, 0xFFF0F0F0u, 1);
    }
    cv->clip = old;
}

/* ---- the top bar ---- */

static void top_bar(GamesGui *gg, sqg_ui *ui, sqg_rect r) {
    Game *g = gg->game;
    sqg_row row = sqg_row_begin(r, 4);
    static const char *const KINDS[5] = {"Klondike", "FreeCell", "Mines", "Hearts", "Chess"};
    int kind = gg->kind;
    if (sqg_segments(ui, sqg_id_str("kind"), sqg_row_take(&row, 400), KINDS, 5, &kind)) {
        uint32_t seed = current_seed(gg);
        gg->kind = kind;
        deal(gg, seed);
    }
    if (gg->kind == GAME_KLONDIKE &&
        sqg_checkbox(ui, sqg_id_str("draw3"), sqg_row_take(&row, sqg_text_width("Draw 3", 1) + 24), "Draw 3", &gg->draw3))
        deal(gg, g->s.seed);
    if (gg->kind == GAMES_MINES) {
        static const char *const SIZES[3] = {"Small", "Medium", "Large"};
        if (sqg_segments(ui, sqg_id_str("msize"), sqg_row_take(&row, 180), SIZES, 3, &gg->mines_size))
            deal(gg, gg->mines->seed);
    }
    if (gg->kind == GAMES_CHESS) {
        static const char *const SIDES[3] = {"White", "Black", "Both"}, *const LEVELS[4] = {"1", "2", "3", "4"};
        sqg_row_take(&row, 6);
        sqg_label(ui, sqg_row_take(&row, sqg_text_width("You play", 1) + 4), "You play", SQG_ALIGN_LEFT);
        if (sqg_segments(ui, sqg_id_str("cside"), sqg_row_take(&row, 150), SIDES, 3, &gg->chess_side))
            chess_start(gg, gg->chess->seed);
        sqg_row_take(&row, 6);
        sqg_label(ui, sqg_row_take(&row, sqg_text_width("Level", 1) + 4), "Level", SQG_ALIGN_LEFT);
        int lv = gg->chess->level - 1;
        if (sqg_segments(ui, sqg_id_str("clevel"), sqg_row_take(&row, 100), LEVELS, 4, &lv)) gg->chess->level = lv + 1;
        sqg_row_take(&row, 6);
        if (sqg_button(ui, sqg_id_str("new"), sqg_row_take(&row, sqg_button_w(ui, "New")), "New") || ui->in.ctrl_key == 'n')
            chess_start(gg, gg->chess->seed * 2654435761u + 12345u);
        if (sqg_button(ui, sqg_id_str("undo"), sqg_row_take(&row, sqg_button_w(ui, "Undo")), "Undo") || ui->in.ctrl_key == 'z') {
            Chess *c = gg->chess;
            if (chess_undo(c)) say(gg, "Nothing to undo");
            else {
                while (c->n && !c->human[c->pos.side]) chess_undo(c);
                gg->chess_sel = -1;
                gg->next_ms = gg->ms + 300;
                chess_said(gg);
            }
        }
        return;
    }
    sqg_row_take(&row, 6);
    sqg_label(ui, sqg_row_take(&row, sqg_text_width("Deal", 1) + 4), "Deal", SQG_ALIGN_LEFT);
    int typed = sqg_textfield(ui, sqg_id_str("seed"), sqg_row_take(&row, 90), gg->seed_text, sizeof gg->seed_text);
    if (sqg_button(ui, sqg_id_str("play"), sqg_row_take(&row, sqg_button_w(ui, "Play")), "Play") || typed) {
        char *end;
        unsigned long v = strtoul(gg->seed_text, &end, 10);
        if (end == gg->seed_text || *end || v > 0xFFFFFFFFul) say(gg, "A deal is a number from 0 to 4294967295");
        else deal(gg, (uint32_t)v);
    }
    if (sqg_button(ui, sqg_id_str("new"), sqg_row_take(&row, sqg_button_w(ui, "New")), "New") || ui->in.ctrl_key == 'n')
        deal(gg, current_seed(gg) * 2654435761u + 12345u);
    if (sqg_button(ui, sqg_id_str("again"), sqg_row_take(&row, sqg_button_w(ui, "Restart")), "Restart"))
        deal(gg, current_seed(gg));
    if (gg->kind == GAMES_MINES || gg->kind == GAMES_HEARTS) return;
    sqg_row_take(&row, 6);
    if (sqg_button(ui, sqg_id_str("undo"), sqg_row_take(&row, sqg_button_w(ui, "Undo")), "Undo") || ui->in.ctrl_key == 'z')
        do_undo(gg);
    if (sqg_button(ui, sqg_id_str("home"), sqg_row_take(&row, sqg_button_w(ui, "Home")), "Home") || ui->in.ctrl_key == 'a') {
        if (game_auto(g, 1)) after_move(gg);
        else say(gg, "No card can go home");
    }
    sqg_checkbox(ui, sqg_id_str("auto"), sqg_row_take(&row, sqg_text_width("Auto home", 1) + 24), "Auto home", &gg->auto_home);
}

static void status_line(GamesGui *gg, sqg_ui *ui, sqg_rect r) {
    const GameState *s = &gg->game->s;
    char t[96];
    if (gg->kind == GAMES_MINES)
        snprintf(t, sizeof t, "%d x %d, %d mines   Field %lu", gg->mines->w, gg->mines->h, gg->mines->mines,
                 (unsigned long)gg->mines->seed);
    else if (gg->kind == GAMES_CHESS)
        snprintf(t, sizeof t, "Chess   Level %d   Move %d", gg->chess->level, gg->chess->pos.fullmove);
    else if (gg->kind == GAMES_HEARTS)
        snprintf(t, sizeof t, "Game %lu   Hand %d   You %d  West %d  North %d  East %d", (unsigned long)gg->hearts->seed,
                 gg->hearts->hand_no + 1, gg->hearts->score[0], gg->hearts->score[1], gg->hearts->score[2],
                 gg->hearts->score[3]);
    else if (s->kind == GAME_KLONDIKE) snprintf(t, sizeof t, "Score %d   Moves %d", s->score, s->moves);
    else snprintf(t, sizeof t, "Moves %d   Cards that can move together: %d", s->moves, game_most(s, -1));
    sqg_row row = sqg_row_begin(r, 8);
    sqg_label(ui, sqg_row_take(&row, sqg_text_width(t, ui->scale) + 4), t, SQG_ALIGN_LEFT);
    sqg_label(ui, sqg_row_take(&row, 0), gg->status, SQG_ALIGN_RIGHT);
}

void games_gui_frame(GamesGui *gg, sqg_ui *ui) {
    sqg_canvas *cv = ui->cv;
    int lh = sqg_line_h(ui), pad = 6;
    gg->frame++;
    sqg_fill(cv, sqg_rect_make(0, 0, cv->w, cv->h), ui->theme->face);
    int top = pad + lh + pad, status_y = cv->h - pad - lh;
    gg->board = sqg_rect_make(0, top, cv->w, status_y - pad - top);
    top_bar(gg, ui, sqg_rect_make(pad, pad, cv->w - 2 * pad, lh));
    if (gg->kind == GAMES_MINES) {
        mines_input(gg, ui);
        draw_mines(gg, ui);
    } else if (gg->kind == GAMES_CHESS) {
        chess_input(gg, ui);
        draw_chess(gg, ui);
    } else if (gg->kind == GAMES_HEARTS) {
        hearts_input(gg, ui);
        draw_hearts(gg, ui);
        hearts_center_button(gg, ui);
    } else {
        table_input(gg, ui);
        draw_table(gg, ui);
    }
    gg->last_ms = gg->ms;
    status_line(gg, ui, sqg_rect_make(pad, status_y, cv->w - 2 * pad, lh));
}
