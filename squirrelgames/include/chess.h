/* chess: SquirrelGames' chess, as rules with no drawing, and a computer
 * player. Every legal move (castling, en passant, promotion), check,
 * checkmate, stalemate, and the draws by the fifty-move rule, threefold
 * repetition and too little material. Moves read and written in standard
 * algebraic notation (Nf3, exd5, O-O, e8=Q) or as squares (e2e4, e7e8q).
 *
 * Freestanding C11: no allocation, no stdio, nothing from libc but
 * memcpy, memmove, memset and strlen.
 */
#ifndef CHESS_H
#define CHESS_H

#include <stddef.h>
#include <stdint.h>

/* A square is rank * 8 + file: a1 is 0, h1 7, a8 56, h8 63. A piece is
 * one of these, positive for White and negative for Black; 0 empty. */
enum { CHESS_PAWN = 1, CHESS_KNIGHT, CHESS_BISHOP, CHESS_ROOK, CHESS_QUEEN, CHESS_KING };
enum { CHESS_WHITE, CHESS_BLACK };
#define CHESS_SQ(file, rank) ((rank) * 8 + (file))

/* castling still allowed */
#define CHESS_WK 1
#define CHESS_WQ 2
#define CHESS_BK 4
#define CHESS_BQ 8

typedef struct {
    int8_t sq[64];
    int side;     /* CHESS_WHITE or CHESS_BLACK to move */
    int castle;   /* CHESS_WK | ... */
    int ep;       /* the square a pawn may take en passant onto, or -1 */
    int halfmove; /* moves since a capture or a pawn moved (the fifty-move rule) */
    int fullmove; /* from 1, up after Black's move */
} ChessPos;

/* A move: from | to << 6 | promotion piece << 12 (0, or CHESS_KNIGHT..CHESS_QUEEN) */
#define CHESS_MOVE(from, to, promo) ((from) | (to) << 6 | (promo) << 12)
#define CHESS_FROM(m) ((m) & 63)
#define CHESS_TO(m) (((m) >> 6) & 63)
#define CHESS_PROMO(m) (((m) >> 12) & 7)
#define CHESS_MAX_MOVES 256

enum { CHESS_PLAYING, CHESS_CHECKMATE, CHESS_STALEMATE, CHESS_DRAW_FIFTY, CHESS_DRAW_REPEAT, CHESS_DRAW_MATERIAL };

#define CHESS_HISTORY 600

typedef struct {
    ChessPos pos;
    ChessPos before[CHESS_HISTORY]; /* the positions before each move, for undo and repetition */
    int move[CHESS_HISTORY];        /* the moves made */
    int n;
    int human[2];  /* which sides a person plays (1) and which the computer (0) */
    int level;     /* the computer's strength, 1 to 4: how many moves it looks ahead */
    uint32_t seed; /* the game: a little variety in the computer's choices */
} Chess;

/* A new game from the usual start: you play white against the computer at level 3 */
void chess_new(Chess *c, uint32_t seed);
/* A position from FEN (the board, side, castling, en passant, and the
 * two counts, which may be left off). 0, or -1 for one that is not a
 * position (each side one king, the side not to move not in check). The
 * history starts again. */
int chess_set_fen(Chess *c, const char *fen);
size_t chess_fen(const ChessPos *p, char *out, size_t cap);

/* Every legal move in the position: returns how many (at most CHESS_MAX_MOVES) */
int chess_moves(const ChessPos *p, int *out);
int chess_is_legal(const ChessPos *p, int move);
/* Whether square sq is attacked by a piece of side by */
int chess_attacked(const ChessPos *p, int sq, int by);
int chess_in_check(const ChessPos *p);
/* The move, which must be legal: 0, or -1 */
int chess_make(Chess *c, int move);
int chess_undo(Chess *c);
int chess_state(const Chess *c); /* CHESS_PLAYING, ... */

/* Standard algebraic notation of a legal move in the position, with + or
 * #: at most 8 bytes and the 0 */
void chess_san(const ChessPos *p, int move, char out[10]);
/* A move from text, SAN or squares; -1 when it is not a legal move */
int chess_parse(const ChessPos *p, const char *text, size_t n);

/* The computer's move for the side to move, at the game's level; -1
 * when there is none */
int chess_ai(const Chess *c);
/* The computer moves while it is its turn and the game goes on; returns
 * the moves it made */
int chess_run(Chess *c);

/* The board as text, White at the bottom, with the moves and the FEN */
size_t chess_text(const Chess *c, char *out, size_t cap);

/* One line of play (the computer replies after each when it is its turn):
 *   new [white|black|both] [SEED]   move M   undo   go   level N   fen FEN
 * new: which side you play (both: two people, no computer); undo takes
 * back to your previous turn; go: the computer moves for the side to
 * move. 0, or -1 with why in msg. */
int chess_command(Chess *c, const char *line, char *msg, size_t cap);

#endif
