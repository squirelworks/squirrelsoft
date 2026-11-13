/* gamesgui: the SquirrelGames window panel on libs/sqgui. The game, a new
 * deal, undo and the rest along the top; the table below, with cards drawn
 * here from scratch. A card is dragged to where it goes (with the cards on
 * it, in a column); a double click or the right button sends it to the
 * best place (home first); a click on Klondike's stock turns the next
 * cards. Ctrl+Z undoes, Ctrl+N deals again, Ctrl+A sends home what can go.
 */
#ifndef GAMESGUI_H
#define GAMESGUI_H

#include "cards.h"
#include "sqgui.h"

#define GAMES_CARD_W 80
#define GAMES_CARD_H 110

typedef struct GamesGui GamesGui;

struct GamesGui {
    Game *game;
    int kind;              /* the game the next deal is: GAME_* */
    int draw3;             /* Klondike: three cards at a time */
    int auto_home;         /* cards go home by themselves when nothing needs them */
    char seed_text[12];    /* the deal to play, typed */
    /* the cards being dragged: count of them from the top of from */
    int dragging, from, count, grab_x, grab_y;
    int press_pile, press_index; /* where the button went down, for a click */
    int frame, last_frame, last_pile, last_index; /* for a double click */
    int pending;           /* a press on a card, not yet moved far enough to be a drag */
    int press_x, press_y;
    sqg_rect board;        /* where the table was last drawn */
    char status[160];
};

void games_gui_init(GamesGui *gg, Game *game);

/* Where card index of a pile is (index pile->n: where the next would go;
 * an empty pile: its place) and which card is under a point: 0 with the
 * pile and index (-1 index: the empty place), or -1 for none */
sqg_rect games_gui_card_rect(const GamesGui *gg, int pile, int index);
int games_gui_hit(const GamesGui *gg, int x, int y, int *pile, int *index);

/* Draw one card (face up or down) at x, y */
void games_gui_draw_card(sqg_canvas *cv, int x, int y, uint8_t card);

/* Lay out and draw one frame */
void games_gui_frame(GamesGui *gg, sqg_ui *ui);

/* The window loop: games_sdl.c with SDL2, games_nosdl.c without. Stops
 * after max_frames when > 0. Returns 0, or 2 when there is no window. */
int games_gui_run(GamesGui *gg, int max_frames);

#endif
