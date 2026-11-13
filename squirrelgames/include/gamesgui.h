/* gamesgui: the SquirrelGames window panel on libs/sqgui. The game, a new
 * deal, undo and the rest along the top; the table below, with cards drawn
 * here from scratch. A card is dragged to where it goes (with the cards on
 * it, in a column); a double click or the right button sends it to the
 * best place (home first); a click on Klondike's stock turns the next
 * cards. Ctrl+Z undoes, Ctrl+N deals again, Ctrl+A sends home what can go.
 * Mines: the left button opens a square (or, on a number with its flags
 * all placed, the squares around it), the right one flags it, then marks
 * it with a question mark, then clears it.
 * Hearts: a click on a card of your hand chooses it to pass (three, then
 * the Pass button) or plays it; the computer plays West, North and East.
 * Chess: click a piece, then where it goes (a pawn reaching the last rank
 * becomes a queen); the computer plays the other side.
 * Tiles: click two free tiles of a kind to take them away; Hint shows a
 * pair, Shuffle deals the tiles left again.
 * Bridge: calls from the bidding box, then a click on a card of your hand
 * (or of North's, when your side declares) plays it.
 */
#ifndef GAMESGUI_H
#define GAMESGUI_H

#include "bridge.h"
#include "cards.h"
#include "chess.h"
#include "hearts.h"
#include "mines.h"
#include "sqgui.h"
#include "tiles.h"

#define GAMES_CARD_W 80
#define GAMES_CARD_H 110
#define GAMES_MINES 2  /* the third game the window plays, beside GAME_KLONDIKE and GAME_FREECELL */
#define GAMES_HEARTS 3 /* and the fourth */
#define GAMES_CHESS 4  /* and the fifth */
#define GAMES_TILES 5  /* and the sixth */
#define GAMES_BRIDGE 6 /* and the seventh */
#define GAMES_SQUARE 24 /* a square of the mines field */

typedef struct GamesGui GamesGui;

struct GamesGui {
    Game *game;
    Mines *mines;
    Hearts *hearts;
    Chess *chess;
    Tiles *tiles;
    Bridge *bridge;
    int tiles_layout;      /* Tiles: TILES_PYRAMID, ... */
    int tiles_sel;         /* Tiles: the tile picked, or -1 */
    int hint_a, hint_b;    /* Tiles: the pair a hint shows, until hint_until */
    long hint_until;
    int chess_sel;         /* Chess: the square of the piece picked up, or -1 */
    int chess_side;        /* Chess: you play white (0), black (1) or both (2) */
    int kind;              /* the game shown: GAME_KLONDIKE, GAME_FREECELL or GAMES_MINES */
    int mines_size;        /* MINES_SMALL, MEDIUM or LARGE */
    long ms, last_ms;      /* the time now, from the run loop (milliseconds), for the mines clock */
    uint8_t chosen[3];     /* Hearts: the cards chosen to pass */
    int nchosen;
    long next_ms;          /* Hearts: when the computer plays its next card */
    long show_until;       /* Hearts: the trick just taken stays in the middle until then */
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

void games_gui_init(GamesGui *gg, Game *game, Mines *mines, Hearts *hearts, Chess *chess, Tiles *tiles, Bridge *bridge);
/* Show another game as it stands, without dealing: kind is GAME_KLONDIKE,
 * GAME_FREECELL, GAMES_MINES, GAMES_HEARTS, GAMES_CHESS, GAMES_TILES or GAMES_BRIDGE (the window's own Klondike
 * or FreeCell is game's kind) */
void games_gui_show(GamesGui *gg, int kind);
/* The mines square under a window point: 0 with x, y, or -1 */
int games_gui_square(const GamesGui *gg, int sx, int sy, int *x, int *y);
sqg_rect games_gui_square_rect(const GamesGui *gg, int x, int y);
/* Hearts: card i of your hand, and which is under a point (-1 for none) */
sqg_rect games_gui_hand_rect(const GamesGui *gg, int i);
int games_gui_hand_hit(const GamesGui *gg, int x, int y);
/* Chess: a square's place on the board (turned round when you play
 * black), and the square under a point (-1 for none) */
sqg_rect games_gui_chess_rect(const GamesGui *gg, int sq);
int games_gui_chess_square(const GamesGui *gg, int x, int y);
/* Draw a chess piece (chess.h's, white positive) in a 48x48 square at x, y */
void games_gui_draw_piece(sqg_canvas *cv, int x, int y, int piece);
/* Tiles: where tile i is drawn (its face), and the tile on top under a
 * point (-1 for none); one tile's face and side at x, y */
sqg_rect games_gui_tile_rect(const GamesGui *gg, int i);
int games_gui_tile_hit(const GamesGui *gg, int x, int y);
void games_gui_draw_tile(sqg_canvas *cv, int x, int y, int kind);
/* Bridge: where card i of a seat's hand is drawn, the card of a hand you
 * play under a point (its index and seat, or -1), a call's place in the
 * bidding box, and the Next board button */
sqg_rect games_gui_bridge_card_rect(const GamesGui *gg, int seat, int i);
int games_gui_bridge_hit(const GamesGui *gg, int x, int y, int *seat);
sqg_rect games_gui_bid_rect(const GamesGui *gg, int call);
sqg_rect games_gui_bridge_button(const GamesGui *gg);
/* Hearts: the button in the middle (Pass, Next hand, New game) */
sqg_rect games_gui_hearts_button(const GamesGui *gg);

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
