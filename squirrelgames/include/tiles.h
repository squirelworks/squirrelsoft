/* tiles: SquirrelGames' tile-matching patience (in the Mahjongg family), as
 * rules with no drawing. 144 tiles, four of each of 36 kinds, stacked in
 * layers; take away two of a kind at a time while both are free: nothing
 * on top of them, and nothing beside them on the left or on the right.
 * Clear the table to win.
 *
 * Every deal can be cleared: it is made by taking the tiles away in
 * pairs from the full layout, at random, so that playing the same pairs
 * forwards always works. A shuffle when stuck deals the tiles left again
 * the same way.
 *
 * Freestanding C11: no allocation, no stdio, nothing from libc but
 * memcpy, memmove, memset and strlen.
 */
#ifndef TILES_H
#define TILES_H

#include <stddef.h>
#include <stdint.h>

#define TILES_MAX 144
#define TILES_KINDS 36

/* the kinds: circles 1-9 (0..8), bars 1-9 (9..17), numbers 1-9 (18..26),
 * then the four winds, three dragons, the sun and the moon (27..35) */
enum { TILES_CIRCLES = 0, TILES_BARS = 9, TILES_NUMBERS = 18, TILES_WINDS = 27, TILES_DRAGONS = 31, TILES_SUN = 34, TILES_MOON = 35 };

enum { TILES_PYRAMID, TILES_TOWERS, TILES_CROSS, TILES_LAYOUTS };
/* stuck: no pair can go; dead: and a shuffle found no deal of the tiles
 * left that could be cleared, so only undo helps */
enum { TILES_PLAYING, TILES_WON, TILES_STUCK, TILES_DEAD };

typedef struct {
    int layout;      /* TILES_PYRAMID, ... */
    uint32_t seed;   /* the deal */
    int n;           /* tiles in the layout */
    /* each tile: where (x, y in half tiles, so tiles may sit half over
     * others; z its layer from 0 at the bottom), its kind, whether it is
     * still on the table */
    int8_t x[TILES_MAX], y[TILES_MAX], z[TILES_MAX];
    uint8_t kind[TILES_MAX];
    uint8_t here[TILES_MAX];
    int left;                 /* tiles on the table */
    int16_t taken[TILES_MAX]; /* the tiles taken, in pairs, in order: for undo */
    int ntaken;
    int shuffles;
    int dead;                 /* the last shuffle could not help (until an undo) */
    /* how the deal was made: these pairs, taken in this order, clear it
     * (from the deal or the last shuffle) */
    int16_t plan[TILES_MAX];
    int nplan;
} Tiles;

/* "Pyramid", "Towers", "Cross" */
extern const char *const TILES_LAYOUT_NAMES[TILES_LAYOUTS];

/* A new deal. 0, or -1 for a layout there is not. */
int tiles_new(Tiles *t, int layout, uint32_t seed);
int tiles_free(const Tiles *t, int i);
/* Take away tiles i and j: both free, both here, the same kind. 0, or -1 */
int tiles_match(Tiles *t, int i, int j);
int tiles_undo(Tiles *t);
/* A pair that can go now: 0 with i and j, or -1 for none */
int tiles_hint(const Tiles *t, int *i, int *j);
/* How many pairs could go now */
int tiles_moves(const Tiles *t);
int tiles_state(const Tiles *t); /* TILES_PLAYING, TILES_WON, TILES_STUCK, TILES_DEAD */
/* The tiles left dealt again among their places, so that they can all be
 * cleared. 0, or -1 when there are none or no deal of them could be
 * cleared (as with two tiles one on the other): then they stay as they were. */
int tiles_shuffle(Tiles *t);

/* A kind's short name: C1..C9, B1..B9, N1..N9, WE WS WW WN, DR DG DW, SU, MO */
void tiles_kind_name(int kind, char out[3]);
/* The table as text: the counts, then each free tile as NUMBER:KIND */
size_t tiles_text(const Tiles *t, char *out, size_t cap);

/* One line of play:
 *   new pyramid|towers|cross [SEED]   match I J   hint   undo   shuffle
 * where I and J are tile numbers as tiles_text lists them. 0, or -1 with
 * why in msg (for hint: the pair, in msg). */
int tiles_command(Tiles *t, const char *line, char *msg, size_t cap);

#endif
