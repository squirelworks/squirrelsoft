/* hearts: SquirrelGames' Hearts, as rules with no drawing. Four players,
 * you (South, player 0) and three the computer plays: West (1), North (2)
 * and East (3), in that order round the table. Each hand: thirteen cards
 * each, three passed (left, right, across, then none, in turn), then
 * thirteen tricks. The two of clubs leads the first; follow suit if you
 * can; the highest card of the suit led takes the trick and leads the
 * next. Each heart taken is a point and the queen of spades thirteen;
 * all 26 to one player (shooting the moon) gives the other three 26
 * instead. Once a score reaches 100, the lowest score wins.
 *
 * The usual house rules: no hearts and no queen of spades on the first
 * trick (unless a hand holds nothing else), and no heart led until a
 * heart has been played (unless a hand holds nothing else).
 *
 * Freestanding C11: no allocation, no stdio, nothing from libc but
 * memcpy, memmove, memset and strlen. Cards are cards.h's bytes.
 */
#ifndef HEARTS_H
#define HEARTS_H

#include <stddef.h>
#include <stdint.h>

#include "cards.h"

#define HEARTS_END 100 /* the game ends once a score reaches this */

enum { HEARTS_PASS_LEFT, HEARTS_PASS_RIGHT, HEARTS_PASS_ACROSS, HEARTS_PASS_NONE };
enum {
    HEARTS_PASSING,   /* waiting for your three cards */
    HEARTS_PLAYING,   /* tricks: turn says whose card it is */
    HEARTS_HAND_OVER, /* the hand is scored: hearts_next_hand deals the next */
    HEARTS_GAME_OVER
};

typedef struct {
    uint32_t seed;      /* the game: each hand's deal comes from it and the hand's number */
    int hand_no;        /* from 0 */
    int phase;          /* HEARTS_* */
    uint8_t hand[4][13];/* each player's cards, sorted (clubs, diamonds, spades, hearts; low to high) */
    int n[4];
    uint8_t passed[4][3]; /* what each passed this hand, and got: got[p] = passed[from(p)] */
    int turn;           /* whose card it is */
    int leader;         /* who led this trick */
    uint8_t trick[4];   /* this trick's cards by player (0 not yet) */
    int ntrick;         /* cards in it */
    uint8_t last[4];    /* the last trick taken, by player */
    int last_winner;    /* -1 before the first */
    int tricks;         /* tricks taken this hand */
    int broken;         /* a heart has been played */
    uint64_t gone;      /* cards played this hand: bit suit * 13 + rank - 1 */
    int taken[4];       /* points taken this hand */
    int hand_score[4];  /* the last hand's points, after shooting the moon */
    int moon;           /* who shot the moon in the last hand, or -1 */
    int score[4];       /* the game */
} Hearts;

extern const char *const HEARTS_NAMES[4]; /* "You", "West", "North", "East" */

/* A new game from seed: scores 0, the first hand dealt */
void hearts_new(Hearts *h, uint32_t seed);
/* After HEARTS_HAND_OVER: the next hand. 0, or -1 at another time. */
int hearts_next_hand(Hearts *h);
/* HEARTS_PASS_* for this hand */
int hearts_pass_dir(const Hearts *h);
/* who gets player p's passed cards */
int hearts_pass_to(const Hearts *h, int p);

/* Your three cards to pass (the computer chooses the others'): 0, or -1
 * when it is not the time or they are not three of your cards. Then the
 * play starts: the two of clubs' holder leads. */
int hearts_pass(Hearts *h, const uint8_t cards[3]);

/* Whether the player whose turn it is may play card: 0 yes, or -1 with
 * why (a few words, never NULL) in *why when why is not NULL */
int hearts_can_play(const Hearts *h, uint8_t card, const char **why);
/* The card for the player whose turn it is. After the fourth, the trick
 * is taken, and after the thirteenth the hand is scored. 0, or -1. */
int hearts_play(Hearts *h, uint8_t card);

/* The computer's choices: three cards for player p to pass, and a card
 * for whoever's turn it is. */
void hearts_ai_pass(const Hearts *h, int p, uint8_t out[3]);
uint8_t hearts_ai_card(const Hearts *h);
/* The computer plays its players until it is your turn or the hand is
 * over; returns the cards it played */
int hearts_run(Hearts *h);

int hearts_has(const Hearts *h, int p, uint8_t card);
int hearts_points(uint8_t card); /* 1 a heart, 13 the queen of spades */
/* The winner (the lowest score) once the game is over, -1 before; a tie
 * for lowest: the first of them */
int hearts_winner(const Hearts *h);

/* The table as text: scores, the trick, your hand; returns the length it
 * needs (as snprintf) */
size_t hearts_text(const Hearts *h, char *out, size_t cap);

/* One line of play (the computer plays its players afterwards):
 *   new [SEED]   pass C C C   play C   next
 * with cards as in cards.h's names (2C, TD, QS, AH). 0, or -1 with why. */
int hearts_command(Hearts *h, const char *line, char *msg, size_t cap);

#endif
