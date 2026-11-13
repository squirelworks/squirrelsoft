/* calcgui: the SquirrelCalc window panel (libs/sqgui): four rows graphed
 * together, each a function of x, a curve "(cos(t), sin(t))" or a polar
 * curve "r = 1 + cos(t)" (t from tmin to tmax: 0 to 2 pi unless defined).
 * Variables defined in the calculator line are used by the graphs (a = 2,
 * then y1 = a sin(x)) and get sliders. Roots, crossings, the derivative of
 * y1, trace, integrals, a table, pan and zoom. Drawn the same on the desktop and off screen in tests. */
#ifndef CALCGUI_H
#define CALCGUI_H

#include "calcgraph.h"

#define CALC_N 4      /* functions */
#define CALC_VARS 8   /* user variables, ans included */
#define CALC_ROOTS 16 /* roots kept per function */
#define CALC_PAIRS 6  /* pairs of functions, for crossings */
#define CALC_CROSS 16 /* crossings kept */
#define CALC_SLIDERS 4 /* variables shown with a slider */

enum { CALC_FUNC, CALC_PARAM, CALC_POLAR }; /* y = f(x), (x(t), y(t)), r = f(t) */

typedef struct {
    char fx[CALC_N][160];      /* what is typed for y1..y4 */
    char seen[CALC_N][160];    /* the text last compiled */
    SqmProgram prog[CALC_N];  /* f(x), or x(t) for a curve */
    SqmProgram prog2[CALC_N]; /* y(t) for a curve */
    int kind[CALC_N];         /* CALC_FUNC, CALC_PARAM or CALC_POLAR */
    int ok[CALC_N];
    char err[CALC_N][96];      /* why a function is not drawn, or "" */
    int show_roots, show_deriv;
    SqmProgram dprog;          /* d/dx of y1 */
    int dok;
    char dtext[200];           /* its text */
    char vname[CALC_VARS][SQM_NAME_LEN];
    double vval[CALC_VARS];
    double vlo[CALC_VARS], vhi[CALC_VARS]; /* slider ranges */
    int nvars;
    sqg_rect slider_at[CALC_SLIDERS];     /* where the sliders were drawn */
    char line[200];            /* the calculator input */
    char hist[1600];           /* past results, newest last */
    CalcView view;
    int view_ready;
    sqg_rect graph_at;         /* where the graph was drawn */
    int dragging, last_mx, last_my;
    int mouse_in;
    double mouse_x, mouse_y;   /* the pointer on the plane */
    double roots[CALC_N][CALC_ROOTS];
    int nroots[CALC_N];
    /* crossings: yi - yj for each pair, and where they meet in view */
    int show_cross;
    SqmProgram cprog[CALC_PAIRS];
    int cok[CALC_PAIRS];
    double cross_x[CALC_CROSS], cross_y[CALC_CROSS];
    int cross_pair[CALC_CROSS];
    int ncross;
    /* trace: a point pinned on a curve by a click, moved with Left/Right */
    int trace_fn; /* -1: none */
    double trace_x;
    int press_mx, press_my;
    /* the area from "integrate ... from a to b", until Reset */
    int area_on, area_fn; /* area_fn: which yi, or -1 for its own expression */
    SqmProgram area_prog;
    double area_a, area_b, area_value;
    /* the table view in place of the graph */
    int show_table;
    char tstart[32], tstep[32];
    int table_top;
    char status[200];
} CalcGui;

void calc_gui_init(CalcGui *g);

/* Set yi (0..3) and compile it now (the frame also notices typing). */
void calc_gui_set(CalcGui *g, int i, const char *text);

/* Run a calculator line: "2+3", "a = 2" (defines a for the graphs too),
 * "ans*2", or a CAS command: "expand (x+1)^2", "collect x a x + b x",
 * "d/dx x^3", "solve x^2 - x = 1" (for x, or "... for a"), or
 * "integrate y1 from 0 to 2" (any expression of x; it also shades the area). Adds the result
 * to the history. Returns 0, or -1 on an error (also in the history). */
int calc_gui_eval(CalcGui *g, const char *line);

/* Value of a user variable, or NAN */
double calc_gui_var(const CalcGui *g, const char *name);

/* Lay out and draw one frame. */
void calc_gui_frame(CalcGui *g, sqg_ui *ui);

/* Window loop: calc_sdl.c with SDL2, calc_nosdl.c without. Stops after
 * max_frames when > 0. Returns 0, or 2 when there is no window. */
int calc_gui_run(CalcGui *g, int max_frames);

#endif
