/* calcgui: the SquirrelCalc window panel (libs/sqgui): four rows graphed
 * together, each a function of x ("x^2" or "y = x^2"), a curve
 * "(cos(t), sin(t))", a polar curve "r = 1 + cos(t)" (t from tmin to tmax:
 * 0 to 2 pi unless defined), an equation in x and y "x^2 + y^2 = 25", or an
 * inequality "y < sin(x)", "x^2 + y^2 >= 4" (shaded; < and > dash the edge).
 * Variables defined in the calculator line are used by the graphs (a = 2,
 * then y1 = a sin(x)) and get sliders. Roots, crossings, the derivative of
 * y1, trace, integrals, a table, pan and zoom. Drawn the same on the desktop and off screen in tests. */
#ifndef CALCGUI_H
#define CALCGUI_H

#include "calcgeom.h"
#include "calcgraph.h"
#include "calcnote.h"
#include "calcsheet.h"
#include "calcstats.h"

#define CALC_N 4      /* functions */
#define CALC_VARS 8   /* user variables, ans included */
#define CALC_ROOTS 16 /* roots kept per function */
#define CALC_PAIRS 6  /* pairs of functions, for crossings */
#define CALC_CROSS 16 /* crossings kept */
#define CALC_SLIDERS 4 /* variables shown with a slider */
#define CALC_LISTS 4   /* named lists of numbers */

enum { CALC_PLOT_HIST = 1, CALC_PLOT_BOX = 2 };

/* y = f(x), (x(t), y(t)), r = f(t), an equation in x and y (x^2 + y^2 = 25),
 * an inequality (y < x^2, x^2 + y^2 >= 4) */
enum { CALC_FUNC, CALC_PARAM, CALC_POLAR, CALC_IMPLICIT, CALC_INEQ };

typedef struct {
    char fx[CALC_N][160];      /* what is typed for y1..y4 */
    char seen[CALC_N][160];    /* the text last compiled */
    char src[CALC_N][400];     /* what was compiled: seen with cells and sheet functions worked out */
    SqmProgram prog[CALC_N];  /* f(x), or x(t) for a curve */
    SqmProgram prog2[CALC_N]; /* y(t) for a curve */
    int kind[CALC_N];         /* CALC_FUNC .. CALC_INEQ */
    int body[CALC_N];         /* where f(x) starts in fx: after "y =" */
    int strict[CALC_N];       /* an inequality with < or >: its edge is dashed */
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
    /* lists, "xs = {1, 2, 3}", and the points and fit from plot / linreg */
    char lname[CALC_LISTS][SQM_NAME_LEN];
    double lval[CALC_LISTS][CALC_LIST_MAX];
    int llen[CALC_LISTS];
    int nlists;
    int fit_on;    /* 0, 1 points only, 2 points and the fitted curve; Reset clears */
    double fit_x[CALC_LIST_MAX], fit_y[CALC_LIST_MAX]; /* the points */
    int fit_n;
    int fit_view;       /* move the view onto the points at the next frame */
    CalcFit fit;
    /* hist L [width] and box L: drawn on the graph until Reset */
    int stat_plot; /* 0, CALC_PLOT_HIST or CALC_PLOT_BOX */
    double stat_v[CALC_LIST_MAX];
    int stat_n;
    CalcStats stat;
    double hist_start, hist_width;
    int hist_count[CALC_LIST_MAX];
    int hist_bins;
    /* the spreadsheet, shown in place of the graph */
    CalcSheet sheet;
    int show_sheet;
    int sel_r, sel_c;               /* the selected cell */
    char cell_line[CALC_CELL_LEN];  /* what the edit line holds */
    int cell_r, cell_c;             /* which cell cell_line was loaded from */
    int sheet_top;                  /* the first row shown */
    int sheet_seen_r;               /* the selected row the view last followed */
    /* geometry on the graph: A = (1, 2), c = circle(A, 3), ... */
    CalcGeom geom;
    int drag_geom, hot_geom; /* the point being dragged, and the one under the pointer; -1 for none */
    sqg_rect sheet_at;              /* the grid in the last frame: a 30-pixel row-number
                                       column, then 8 columns; 16-pixel rows under a header */
    /* the notebook, shown in place of the graph */
    CalcNote note;
    int show_notes;
    sqg_rect note_at[CALC_NOTE_MAX]; /* where each cell was drawn in the last frame (h 0: not shown) */
    char *echo;      /* while set, results go here (echo_cap bytes) instead of the history */
    size_t echo_cap;
    char status[200];
} CalcGui;

void calc_gui_init(CalcGui *g);

/* Set yi (0..3) and compile it now (the frame also notices typing). */
void calc_gui_set(CalcGui *g, int i, const char *text);

/* Run a calculator line: "2+3", "a = 2" (defines a for the graphs too),
 * "ans*2", or a CAS command: "expand (x+1)^2", "collect x a x + b x",
 * "d/dx x^3", "solve x^2 - x = 1" (for x, or "... for a"), or
 * "integrate y1 from 0 to 2" (any expression of x; it also shades the area),
 * "integrate x e^x" (the symbolic integral), "factor x^3 - x", "apart 1/(x^2 - 1)".
 * Lists: "xs = {1, 2, 3}", "stats xs", "plot xs ys", and the fits "linreg xs ys",
 * "quadreg xs ys" and "expreg xs ys" (drawn with the points), "hist xs [width]"
 * and "box xs". Wherever a list goes, a sheet range does too (A1:A10, B:B).
 * The sheet: "B2 = 5" or "B2 = A1 * 2" (a formula) sets a cell, "fill B2:B10"
 * copies B2 down the range with its cell names moved, and expressions may
 * name cells and use sum(A1:A5), mean(xs), normalcdf(-1, 1) and the other
 * calcsheet.h functions. Geometry: "A = (1, 2)", "s = segment(A, B)",
 * "c = circle(A, 3)", "M = midpoint(A, B)", "I = intersect(s, c)" and the
 * rest of calcgeom.h, drawn on the graph (free points can be dragged);
 * measurements such as distance(A, B) or area(p) in any expression;
 * "delete NAME" and "geometry" (a list). "y1 = sin(x)" sets a row (y1..y4).
 * With the notebook shown, a line run here is also added to it. Adds the result to the history. Returns 0, or -1 on
 * an error (also in the history). */
int calc_gui_eval(CalcGui *g, const char *line);

/* Set a cell ("B2") as typed in the sheet and recalculate; 0, or -1 for a
 * bad cell name or text too long */
int calc_gui_cell(CalcGui *g, const char *ref, const char *text);
/* Recalculate the sheet and recompile the rows that use it (after loading
 * a CSV into g->sheet, say) */
void calc_gui_recalc(CalcGui *g);

/* Forget the variables, lists, shapes, fits and plots, as at the start;
 * the rows, the sheet, the view and the notebook stay. */
void calc_gui_forget(CalcGui *g);

/* Notebooks (calcnote.h, calcnote.c). Run: forget, then every math cell in
 * order, each one's result kept with it; returns the cells with an error.
 * A math cell is any calculator line, and "y1 = sin(x)" sets a row.
 *
 * The notebook file keeps a whole session, one line each:
 *   SquirrelCalc notebook 1      first
 *   y1: sin(x)                   a row as typed (y1 to y4)
 *   view: -10 10 -6 6            xmin xmax ymin ymax
 *   cell B2: =A1*2               a sheet cell as typed
 *   text: Some notes             a text cell ("# Title" is a heading)
 *   math: a = 2                  a math cell
 * with blank lines and # comments left out. Save returns the length the
 * whole text needs, like snprintf. Load checks every line first and changes
 * nothing on an error (-1, "line 3: why" in msg); else it replaces the
 * rows, sheet, view and notebook, runs it, and says how it went in msg.
 * Text is the notebook as it reads, results under their cells, for a
 * terminal. */
int calc_gui_notebook_run(CalcGui *g);
size_t calc_gui_notebook_save(const CalcGui *g, char *out, size_t cap);
int calc_gui_notebook_load(CalcGui *g, const char *text, char *msg, size_t msgcap);
size_t calc_gui_notebook_text(const CalcGui *g, char *out, size_t cap);

/* Value of a user variable, or NAN */
double calc_gui_var(const CalcGui *g, const char *name);

/* Lay out and draw one frame. */
void calc_gui_frame(CalcGui *g, sqg_ui *ui);

/* Window loop: calc_sdl.c with SDL2, calc_nosdl.c without. Stops after
 * max_frames when > 0. Returns 0, or 2 when there is no window. */
int calc_gui_run(CalcGui *g, int max_frames);

#endif
