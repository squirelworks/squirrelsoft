/* calcsheet: the SquirrelCalc spreadsheet, and the functions over ranges,
 * lists and distributions that the calculator line, the graphs and the
 * cells share.
 *
 * A sheet has columns A to H and rows 1 to 50. A cell holds a number, a
 * text label, or a formula starting with "=": any SquirrelCalc expression
 * that may also name cells (B2, absolute $B$2), the calculator's variables,
 * and the functions below. Cells are recalculated in dependency order, and
 * a cycle is an error in every cell on it rather than a hang.
 *
 * Functions, on top of those sqmath knows:
 *   sum mean median sd var count min max q1 q3   over a range (A1:B5, or
 *       A:A for a whole column), a list name, or several values: sum(1, 2, A1);
 *       min and max of two plain values stay sqmath's, so min(x, 0) graphs
 *   normalpdf(x [, mu, sd])    written out as an expression: it graphs
 *   normalcdf(lo, hi [, mu, sd])  invnorm(p [, mu, sd])
 *   binompdf(n, p, k)  binomcdf(n, p, k)  poissonpdf(l, k)  poissoncdf(l, k)
 *   ncr(n, k)  npr(n, k)
 * These take numbers (they are worked out before sqmath sees the text).
 *
 * Plain C11, no allocation.
 */
#ifndef CALCSHEET_H
#define CALCSHEET_H

#include <stddef.h>

#define CALC_SHEET_COLS 8  /* A..H */
#define CALC_SHEET_ROWS 50
#define CALC_CELL_LEN 64   /* what is typed in a cell, NUL included */

enum { CALC_CELL_EMPTY, CALC_CELL_NUMBER, CALC_CELL_TEXT, CALC_CELL_FORMULA };

typedef struct {
    char text[CALC_SHEET_ROWS][CALC_SHEET_COLS][CALC_CELL_LEN];
    double val[CALC_SHEET_ROWS][CALC_SHEET_COLS]; /* NaN for empty, text and errors */
    unsigned char kind[CALC_SHEET_ROWS][CALC_SHEET_COLS];
    unsigned char bad[CALC_SHEET_ROWS][CALC_SHEET_COLS];  /* 1: the formula has an error */
    unsigned char mark[CALC_SHEET_ROWS][CALC_SHEET_COLS]; /* recalculation state */
    char err[CALC_SHEET_ROWS][CALC_SHEET_COLS][40];       /* why, for a bad formula */
} CalcSheet;

/* What names mean outside the sheet: the calculator's variables, and its
 * lists (list copies up to cap values of the named list into out and
 * returns how many, or -1 when there is no such list). Any part may be
 * NULL / 0. */
typedef struct {
    const char *const *names;
    const double *values;
    int n;
    int (*list)(const void *ctx, const char *name, double *out, int cap);
    const void *ctx;
} CalcEnv;

void calc_sheet_init(CalcSheet *s);
/* Put text in a cell (0-based row and column) as typed: "" empties it, a
 * number is a number, "=..." a formula, anything else a label. Call
 * calc_sheet_recalc afterwards. Returns 0, or -1 off the sheet or too long. */
int calc_sheet_set(CalcSheet *s, int row, int col, const char *text);
void calc_sheet_recalc(CalcSheet *s, const CalcEnv *env);

/* "B12" or "$B$12" at the start of text: the bytes it takes (0 when it is
 * not a cell on the sheet), the 0-based row and column, and which parts
 * are absolute (bit 1 the column, bit 2 the row; abs may be NULL). A cell
 * name must not run on into a longer name ("B12x"). */
int calc_cell_parse(const char *text, int *row, int *col, int *abs);
/* "B12" */
void calc_cell_name(int row, int col, char *out, size_t cap);

/* The numbers of a range ("A1:B5", "B:B", "C3" alone), row by row; empty
 * and text cells and errors are left out. Writes up to cap values and
 * returns how many there are, or -1 when range is not a range. */
int calc_sheet_values(const CalcSheet *s, const char *range, double *out, int cap);

/* Copy cell (r0, c0) to (r1, c1), moving its relative cell names by the
 * same rows and columns ("=A1*2" copied one row down is "=A2*2"; $A$1
 * stays). Returns 0, or -1 when a name would fall off the sheet (the copy
 * then says #REF and is an error) or a cell is off the sheet. */
int calc_sheet_copy(CalcSheet *s, int r0, int c0, int r1, int c1);
/* The same move on a formula's text; -1 and "#REF" for a name off the sheet. */
int calc_shift_refs(const char *in, int drow, int dcol, char *out, size_t cap);

/* CSV: load replaces the sheet with the fields of text (commas, quotes
 * with "" inside, \n or \r\n rows); fields past H or row 50 are left out.
 * Returns the cells set, or -1 when something was left out (the rest is
 * still loaded). save writes the used rectangle: values (formulas as their
 * results) or, with formulas set, the text as typed. Returns the length
 * the whole text needs, like snprintf. */
int calc_sheet_load_csv(CalcSheet *s, const char *csv);
size_t calc_sheet_save_csv(const CalcSheet *s, int formulas, char *out, size_t cap);

/* Rewrite an expression for sqmath: cell names become their values (s may
 * be NULL: no cells), and the functions above are worked out or written
 * out. Returns 0, or -1 with a message in msg. */
int calc_expand(const char *in, char *out, size_t cap, const CalcSheet *s, const CalcEnv *env, char *msg,
                size_t msgcap);

/* 1 when text uses a cell name or one of the functions above (so it needs
 * calc_expand before sqmath can read it) */
int calc_needs_expand(const char *text);

#endif
