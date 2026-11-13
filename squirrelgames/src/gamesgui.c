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

void games_gui_init(GamesGui *gg, Game *game) {
    memset(gg, 0, sizeof *gg);
    gg->game = game;
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

static void deal(GamesGui *gg, uint32_t seed) {
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

/* ---- the top bar ---- */

static void top_bar(GamesGui *gg, sqg_ui *ui, sqg_rect r) {
    Game *g = gg->game;
    sqg_row row = sqg_row_begin(r, 4);
    static const char *const KINDS[2] = {"Klondike", "FreeCell"};
    int kind = gg->kind;
    if (sqg_segments(ui, sqg_id_str("kind"), sqg_row_take(&row, 160), KINDS, 2, &kind)) {
        gg->kind = kind;
        deal(gg, g->s.seed);
    }
    if (gg->kind == GAME_KLONDIKE &&
        sqg_checkbox(ui, sqg_id_str("draw3"), sqg_row_take(&row, sqg_text_width("Draw 3", 1) + 24), "Draw 3", &gg->draw3))
        deal(gg, g->s.seed);
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
        deal(gg, g->s.seed * 2654435761u + 12345u);
    if (sqg_button(ui, sqg_id_str("again"), sqg_row_take(&row, sqg_button_w(ui, "Restart")), "Restart"))
        deal(gg, g->s.seed);
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
    if (s->kind == GAME_KLONDIKE) snprintf(t, sizeof t, "Score %d   Moves %d", s->score, s->moves);
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
    table_input(gg, ui);
    draw_table(gg, ui);
    status_line(gg, ui, sqg_rect_make(pad, status_y, cv->w - 2 * pad, lh));
}
