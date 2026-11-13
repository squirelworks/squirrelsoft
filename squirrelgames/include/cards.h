/* cards: SquirrelGames' patience games, Klondike and FreeCell, as rules
 * with no drawing. A game is piles of cards; a move takes cards from the
 * top of one pile to another when the rules allow it, and every change is
 * one step to undo.
 *
 * Freestanding C11: no allocation, no stdio, nothing from libc but
 * memcpy, memmove, memset and strlen, so it moves to Squirrel OS/2 as it
 * is. A Game holds its undo states (about 1 MB): keep it static.
 */
#ifndef CARDS_H
#define CARDS_H

#include <stddef.h>
#include <stdint.h>

/* A card is a byte: its rank (1 ace .. 13 king) in the low four bits, its
 * suit above, and CARD_UP when it is face up. 0 is no card. */
enum { SUIT_CLUBS, SUIT_DIAMONDS, SUIT_HEARTS, SUIT_SPADES };
#define CARD(rank, suit) ((uint8_t)((rank) | (suit) << 4))
#define CARD_RANK(c) ((c) & 15)
#define CARD_SUIT(c) (((c) >> 4) & 3)
#define CARD_UP 0x80
#define CARD_RED(c) (CARD_SUIT(c) == SUIT_DIAMONDS || CARD_SUIT(c) == SUIT_HEARTS)

enum { GAME_KLONDIKE, GAME_FREECELL };

/* The piles, the same for both games (each uses the ones it has):
 * Klondike: stock, waste, four foundations, seven tableau columns.
 * FreeCell: four foundations, eight tableau columns, four cells. */
enum {
    PILE_STOCK,
    PILE_WASTE,
    PILE_FOUND,          /* four: PILE_FOUND + 0..3 */
    PILE_TAB = 6,        /* eight: PILE_TAB + 0..7 */
    PILE_CELL = 14,      /* four: PILE_CELL + 0..3 */
    PILE_COUNT = 18
};

typedef struct {
    uint8_t n;
    uint8_t c[52]; /* bottom first */
} CardPile;

typedef struct {
    int kind;      /* GAME_* */
    int draw;      /* Klondike: cards turned from the stock at a time, 1 or 3 */
    uint32_t seed; /* the deal */
    CardPile pile[PILE_COUNT];
    int score;     /* Klondike's: 5 a card to the tableau from the waste, 10 a card home, 5 a card turned up */
    int moves;
} GameState;

#define GAME_UNDO 1000

typedef struct {
    GameState s;
    GameState undo[GAME_UNDO]; /* the states before each step, oldest first */
    int nundo;
} Game;

/* A new deal from seed (the same seed, the same deal). draw: 1 or 3. */
void game_new(Game *g, int kind, int draw, uint32_t seed);
/* Tableau columns: 7 or 8 */
int game_columns(const GameState *s);
int game_won(const GameState *s);

/* Whether count cards from the top of from may go onto to: 0 yes, -1 no */
int game_can_move(const GameState *s, int from, int count, int to);
/* The move, as one step: 0, or -1 when the rules do not allow it. A
 * Klondike column left with a face-down card on top turns it up. */
int game_move(Game *g, int from, int count, int to);
/* Klondike's stock: the next one or three cards to the waste, or the waste
 * back to the stock when it is empty. 0, or -1 with nothing to turn. */
int game_draw(Game *g);
/* count cards from the top of from to the best place for them: home, then
 * a column with cards, then an empty column, then a cell. 0, or -1 for
 * none. */
int game_smart(Game *g, int from, int count);
/* Cards home that nothing else needs any more (both of the other colour's
 * one rank down are home), or with all, every card that can go home, until
 * none can; one step. Returns how many went. */
int game_auto(Game *g, int all);
/* Klondike with the stock and waste empty and every card face up: it can
 * only be won, so game_auto(g, 1) finishes it */
int game_can_finish(const GameState *s);
/* The longest run of ordered cards (descending, the colours alternating,
 * face up) on top of a pile */
int game_run(const GameState *s, int pile);
/* FreeCell: the most cards that may move together onto to: (free cells +
 * 1), doubled for each empty column but to */
int game_most(const GameState *s, int to);
int game_undo(Game *g);
int game_can_undo(const Game *g);

/* "AS", "TD", "QH" (T for ten), "##" face down; 3 bytes with the 0 */
void game_card_name(uint8_t c, char out[3]);
/* A pile from its name: s, w, f1..f4, t1..t8, c1..c4 (any case). -1 when not one */
int game_pile_of(const char *w, size_t n);
/* The board as text, one pile a line; returns the length it needs (as snprintf) */
size_t game_text(const GameState *s, char *out, size_t cap);

/* One line of play, for scripts and tests:
 *   new klondike|klondike3|freecell [SEED]
 *   draw   move FROM TO [N]   smart FROM [N]   auto   finish   undo
 * where a pile is s, w, f1..f4, t1..t8 or c1..c4; move with no N takes
 * the longest run that may go. Returns 0, or -1 with why in msg. */
int game_command(Game *g, const char *line, char *msg, size_t cap);

#endif
