/* bridge: SquirrelGames' contract bridge, as rules with no drawing, and
 * computer players. You sit South; the computer plays your partner North
 * and the opponents West and East. Each board: a deal, an auction, the
 * play of thirteen tricks with the declarer's partner as dummy (when your
 * side declares you play both of your side's hands), and duplicate
 * scoring, with the dealer and vulnerability going round as boards do.
 *
 * Freestanding C11: no allocation, no stdio, nothing from libc but
 * memcpy, memmove, memset and strlen. Cards are cards.h's bytes.
 */
#ifndef BRIDGE_H
#define BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#include "cards.h"

/* the seats, clockwise */
enum { BRIDGE_NORTH, BRIDGE_EAST, BRIDGE_SOUTH, BRIDGE_WEST };
/* the strains, lowest first */
enum { BRIDGE_CLUBS, BRIDGE_DIAMONDS, BRIDGE_HEARTS, BRIDGE_SPADES, BRIDGE_NOTRUMP };

/* A call: BRIDGE_PASS, BRIDGE_DOUBLE, BRIDGE_REDOUBLE, or a bid */
#define BRIDGE_PASS 0
#define BRIDGE_DOUBLE 1
#define BRIDGE_REDOUBLE 2
#define BRIDGE_BID(level, strain) (5 + ((level) - 1) * 5 + (strain))
#define BRIDGE_LEVEL(call) (((call) - 5) / 5 + 1)
#define BRIDGE_STRAIN(call) (((call) - 5) % 5)

enum { BRIDGE_BIDDING, BRIDGE_PLAYING, BRIDGE_DONE };

/* vulnerable sides */
#define BRIDGE_VUL_NS 1
#define BRIDGE_VUL_EW 2

#define BRIDGE_CALLS 96

typedef struct {
    uint32_t seed;           /* the game: each board's deal comes from it and the board's number */
    int board;               /* from 1 */
    int dealer, vul;         /* from the board's number */
    uint8_t hand[4][13];     /* the cards left in each hand, sorted (spades, hearts, diamonds, clubs; high first) */
    int n[4];
    uint8_t dealt[4][13];    /* the hands as dealt */
    int phase;               /* BRIDGE_* */
    int turn;                /* whose call, or whose card (dummy's when dummy plays) */
    uint8_t calls[BRIDGE_CALLS];
    int ncalls;
    /* the contract, once the auction is over: declarer -1 when all passed */
    int level, strain, doubled, declarer;
    int leader;              /* who led this trick */
    uint8_t trick[4];        /* by seat, 0 not yet */
    int ntrick;
    uint8_t last[4];         /* the last trick, by seat */
    int last_winner;         /* -1 before the first */
    int tricks;              /* played */
    int won[2];              /* tricks taken: [0] North-South, [1] East-West */
    int result;              /* the board's score for North-South (minus: to East-West) */
    long total;              /* North-South's running total */
} Bridge;

extern const char *const BRIDGE_SEATS[4]; /* "North", "East", "South", "West" */

/* A new game: board 1 dealt */
void bridge_new(Bridge *b, uint32_t seed);
/* After BRIDGE_DONE: the next board. 0, or -1 */
int bridge_next(Bridge *b);

int bridge_hcp(const Bridge *b, int seat); /* high card points: 4 3 2 1 for A K Q J, of the dealt hand */

/* Whether the player whose turn it is may make call: 0, or -1 with why */
int bridge_can_call(const Bridge *b, int call, const char **why);
int bridge_call(Bridge *b, int call);
/* Whether the card may be played now from the hand whose turn it is: 0, or -1 with why */
int bridge_can_play(const Bridge *b, uint8_t card, const char **why);
int bridge_play(Bridge *b, uint8_t card);
int bridge_dummy(const Bridge *b); /* dummy's seat in the play, -1 before */
/* Whether you (South) act for the seat whose turn it is: your own, and
 * dummy or declarer when your side declares */
int bridge_yours(const Bridge *b);

/* The computer's call or card for whoever's turn it is */
int bridge_ai_call(const Bridge *b);
uint8_t bridge_ai_card(const Bridge *b);
/* The computer acts until it is your turn or the board is done; returns how often */
int bridge_run(Bridge *b);

/* The duplicate score for the declaring side: contract level and strain,
 * doubled 0, 1 or 2, vulnerable, the tricks declarer took */
int bridge_score(int level, int strain, int doubled, int vulnerable, int tricks);

/* "1C", "3NT", "Pass", "X", "XX"; at most 5 bytes and the 0. A call from
 * text the same way (any case, "p" for pass, "N" for NT), or -1. */
void bridge_call_name(int call, char out[6]);
int bridge_call_of(const char *w, size_t n);

/* The table as text: the board, the auction, the contract and play, your
 * hand and the dummy's once it is down */
size_t bridge_text(const Bridge *b, char *out, size_t cap);

/* One line of play (the computer acts afterwards up to your turn):
 *   new [SEED]   bid CALL   play CARD   next
 * 0, or -1 with why in msg. */
int bridge_command(Bridge *b, const char *line, char *msg, size_t cap);

#endif
