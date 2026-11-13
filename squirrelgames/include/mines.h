/* mines: SquirrelGames' minesweeper-style game, as rules with no drawing.
 * A field of hidden squares, some with mines; opening a square shows how
 * many of its eight neighbours hold mines, and opening one with none opens
 * them all. Flag the mines; open every other square to win.
 *
 * Freestanding C11: no allocation, no stdio, nothing from libc but
 * memcpy, memmove, memset and strlen.
 */
#ifndef MINES_H
#define MINES_H

#include <stddef.h>
#include <stdint.h>

#define MINES_MAX_W 40
#define MINES_MAX_H 24

/* a square: these bits, and its count of neighbouring mines in the low four */
#define MINE_HERE 0x10
#define MINE_OPEN 0x20
#define MINE_FLAG 0x40
#define MINE_ASK 0x80 /* a question mark: maybe */

enum { MINES_PLAYING, MINES_WON, MINES_LOST };

typedef struct {
    int w, h, mines;
    uint32_t seed;
    int laid;      /* 0 until the first square opens: the mines go in then, away from it */
    int state;     /* MINES_* */
    int opened;    /* squares opened */
    int flags;
    int boom_x, boom_y; /* the mine that went off */
    long ticks;    /* time played, in whatever the caller counts (the window: milliseconds) */
    uint8_t sq[MINES_MAX_W * MINES_MAX_H];
} Mines;

/* The three usual sizes */
enum { MINES_SMALL, MINES_MEDIUM, MINES_LARGE };
void mines_size_of(int which, int *w, int *h, int *mines);

/* A new hidden field: w x h (up to MINES_MAX_W x MINES_MAX_H, 2x2 at
 * least), mines of them (at least 1, and at most all but nine squares, so
 * the first square and its neighbours can be clear). 0, or -1 out of range. */
int mines_new(Mines *m, int w, int h, int mines, uint32_t seed);

/* Open a square: the first one never holds a mine, nor do its neighbours.
 * A square with no neighbouring mines opens its neighbours too. Opening a
 * mine loses (and shows them all); opening the last safe square wins (and
 * flags them all). Returns the squares it opened (0 for a flagged or open
 * one, or once the game is over), or -1 for a mine. */
int mines_open(Mines *m, int x, int y);
/* On an open number with that many flags around it: open the rest of its
 * neighbours. The same returns as mines_open. */
int mines_chord(Mines *m, int x, int y);
/* Hidden square: none, a flag, a question mark, none again (with ask; or
 * flag and none without). 0, or -1 for an open square or a game over. */
int mines_mark(Mines *m, int x, int y, int ask);
uint8_t mines_at(const Mines *m, int x, int y); /* 0 off the field */
int mines_left(const Mines *m);                 /* mines less flags: what the counter shows */

/* The field as text: a row a line, '.' hidden, 'F' flag, '?' maybe, ' '
 * or 1-8 open, '*' a mine shown after a loss, 'X' the one that went off */
size_t mines_text(const Mines *m, char *out, size_t cap);

/* One line of play:
 *   new small|medium|large [SEED]   new W H MINES [SEED]
 *   open X Y   flag X Y   chord X Y      (from 0, 0 at the top-left)
 * Returns 0, or -1 with why in msg (opening a mine is not an error: the
 * game is lost). */
int mines_command(Mines *m, const char *line, char *msg, size_t cap);

#endif
