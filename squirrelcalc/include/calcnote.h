/* calcnote: SquirrelCalc notebooks. A notebook is a list of cells, each a
 * line of notes ("text") or a calculator line ("math"), run top to bottom
 * from a clean start so that every math cell's result sits under it and
 * follows the cells above it: change "a = 2" to "a = 3" and everything
 * after it is worked out again. Running it, and the notebook file that
 * keeps a whole session (the rows, the sheet, the view and the cells),
 * are in calcgui.h, since they need the calculator.
 *
 * Plain C11, no allocation.
 */
#ifndef CALCNOTE_H
#define CALCNOTE_H

#include <stddef.h>

#define CALC_NOTE_MAX 48  /* cells */
#define CALC_NOTE_LEN 200 /* a cell as typed, NUL included */
#define CALC_NOTE_OUT 480 /* what a math cell said, lines split by \n */

enum { CALC_NOTE_TEXT, CALC_NOTE_MATH };

typedef struct {
    int kind;                /* CALC_NOTE_TEXT or CALC_NOTE_MATH */
    char text[CALC_NOTE_LEN];
    char out[CALC_NOTE_OUT]; /* the calculator's lines from the last run */
    int bad;                 /* 1: the last run gave an error */
} CalcNoteCell;

typedef struct {
    CalcNoteCell c[CALC_NOTE_MAX];
    int n;
    /* the window's notebook view */
    int sel;                  /* the selected cell, or -1 */
    int top;                  /* the first cell shown */
    char edit[CALC_NOTE_LEN]; /* the edit line */
    int edit_at;              /* the cell edit was loaded from, or -1 */
    int seen_sel;             /* the selection the view last followed */
} CalcNote;

void calc_note_init(CalcNote *nb);

/* Put a cell before position at (n or more: at the end). Returns its
 * index, or -1 when the notebook is full, the kind is not one of the two
 * or the text is too long. */
int calc_note_insert(CalcNote *nb, int at, int kind, const char *text);
/* Change a cell's text (its result stays until the next run); 0, or -1 */
int calc_note_set(CalcNote *nb, int at, const char *text);
int calc_note_set_kind(CalcNote *nb, int at, int kind);
/* Take a cell out; 0, or -1 when there is no such cell */
int calc_note_remove(CalcNote *nb, int at);
/* Move a cell to position to, the others closing up; returns where it is
 * now, or -1 when there is no such cell */
int calc_note_move(CalcNote *nb, int at, int to);

/* A math cell's result for showing under it: its output without the copy
 * of the cell's own text that the calculator puts first ("2 + 3 = 5"
 * shows as "= 5", "a = 2" as nothing more). */
void calc_note_result(const CalcNoteCell *c, char *out, size_t cap);

/* How a text cell reads: "# Title" is a heading (returns 1, and *body
 * points past "# "), anything else a paragraph (0, *body the text). */
int calc_note_heading(const CalcNoteCell *c, const char **body);

/* The longest start of s, in bytes, that fits in cols columns, breaking
 * after a space when there is one; 0 only for an empty s. */
size_t calc_note_wrap(const char *s, int cols);

#endif
