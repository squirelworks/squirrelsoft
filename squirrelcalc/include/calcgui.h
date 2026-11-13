/* calcgui: the SquirrelCalc window panel (libs/sqgui): four functions y1..y4
 * graphed together, variables defined in the calculator line and used by the
 * graphs (a = 2, then y1 = a sin(x)), roots, the derivative of y1, pan and
 * zoom. Drawn the same on the desktop and off screen in tests. */
#ifndef CALCGUI_H
#define CALCGUI_H

#include "calcgraph.h"

#define CALC_N 4      /* functions */
#define CALC_VARS 8   /* user variables, ans included */
#define CALC_ROOTS 16 /* roots kept per function */

typedef struct {
    char fx[CALC_N][160];      /* what is typed for y1..y4 */
    char seen[CALC_N][160];    /* the text last compiled */
    SqmProgram prog[CALC_N];
    int ok[CALC_N];
    char err[CALC_N][96];      /* why a function is not drawn, or "" */
    int show_roots, show_deriv;
    SqmProgram dprog;          /* d/dx of y1 */
    int dok;
    char dtext[200];           /* its text */
    char vname[CALC_VARS][SQM_NAME_LEN];
    double vval[CALC_VARS];
    int nvars;
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
    char status[200];
} CalcGui;

void calc_gui_init(CalcGui *g);

/* Set yi (0..3) and compile it now (the frame also notices typing). */
void calc_gui_set(CalcGui *g, int i, const char *text);

/* Run a calculator line: "2+3", "a = 2" (defines a for the graphs too),
 * "ans*2", or a CAS command: "expand (x+1)^2", "collect x a x + b x",
 * "d/dx x^3", "solve x^2 - x = 1" (for x, or "... for a"). Adds the result
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
