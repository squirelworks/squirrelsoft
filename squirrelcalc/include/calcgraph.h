/* calcgraph: the SquirrelCalc graph: a view onto the plane, tick spacing,
 * axes and grid, and curves plotted from sqmath programs onto a libs/sqgui
 * canvas. No OS calls, so it ports to Squirrel OS/2 with sqgui. */
#ifndef CALCGRAPH_H
#define CALCGRAPH_H

#include "sqgui.h"
#include "sqmath.h"

typedef struct {
    double xmin, xmax, ymin, ymax;
} CalcView;

/* -10..10 across, the same scale up and down for an area of w x h pixels */
void calc_view_default(CalcView *v, int w, int h);

/* Keep x as it is and set y around its centre so both axes have the same
 * scale on a w x h area. */
void calc_view_square(CalcView *v, int w, int h);

/* Zoom by factor (> 1 zooms in) keeping the point (cx, cy) where it is.
 * Spans stay between 1e-9 and 1e12. */
void calc_view_zoom(CalcView *v, double factor, double cx, double cy);

/* Move the view with a drag of (dx, dy) pixels on a w x h area (the plane
 * follows the pointer). */
void calc_view_pan(CalcView *v, int dx, int dy, int w, int h);

/* Plane to pixel and back for an area (pixel centres; y grows downwards). */
double calc_px_x(const CalcView *v, sqg_rect r, double x);
double calc_px_y(const CalcView *v, sqg_rect r, double y);
double calc_x_at(const CalcView *v, sqg_rect r, double px);
double calc_y_at(const CalcView *v, sqg_rect r, double py);

/* A tick step of 1, 2 or 5 times a power of ten giving at most max_ticks
 * steps over span. */
double calc_nice_step(double span, int max_ticks);

/* Short tick label ("0.5", "-20", "1e6"). */
void calc_tick_label(double v, double step, char *out, size_t cap);

/* Grid lines, both axes and tick labels on area r. */
void calc_draw_axes(sqg_canvas *c, sqg_rect r, const CalcView *v);

/* Plot y = f(x) where values[slot] is x (the other values stay as they are),
 * sampling twice per pixel column. Lines are broken where f is not finite
 * and across jumps that a sample between them shows are not steep slopes
 * (1/x, tan). Returns the number of line segments drawn. */
int calc_plot(sqg_canvas *c, sqg_rect r, const CalcView *v, const SqmProgram *p, double *values, int slot,
              uint32_t color);

/* Mark the points (x, 0) with small rings; returns how many were in view. */
int calc_mark_roots(sqg_canvas *c, sqg_rect r, const CalcView *v, const double *xs, int n, uint32_t color);

/* A curve (x(t), y(t)) for t from t0 to t1 in `steps` pieces: px and py read
 * t from values[slot]. It breaks where a value is not finite or a step jumps
 * across half the view. Returns the segments drawn. */
int calc_plot_curve(sqg_canvas *c, sqg_rect r, const CalcView *v, const SqmProgram *px, const SqmProgram *py,
                    double *values, int slot, double t0, double t1, int steps, uint32_t color);

/* Rings at (xs[i], ys[i]), or on the x axis when ys is NULL; returns how
 * many are in view. */
int calc_mark_points(sqg_canvas *c, sqg_rect r, const CalcView *v, const double *xs, const double *ys, int n,
                     uint32_t color);

/* Shade between the curve and the x axis for x from a to b (either order),
 * blending argb (its alpha says how strongly). Returns the columns shaded. */
int calc_shade(sqg_canvas *c, sqg_rect r, const CalcView *v, const SqmProgram *p, double *values, int slot,
               double a, double b, uint32_t argb);

/* The curve f(x, y) = 0, where values[xslot] and values[yslot] are x and y,
 * by marching squares on a grid of cell pixels (saddles settled by the
 * centre, and no line across a pole where f jumps sign). dashed leaves
 * every other pair of cells out. Returns the pieces drawn. */
int calc_plot_implicit(sqg_canvas *c, sqg_rect r, const CalcView *v, const SqmProgram *p, double *values, int xslot,
                       int yslot, int cell, int dashed, uint32_t color);

/* Blend argb over every cell-pixel block whose centre has f(x, y) < 0.
 * Returns the blocks shaded. */
int calc_shade_region(sqg_canvas *c, sqg_rect r, const CalcView *v, const SqmProgram *p, double *values, int xslot,
                      int yslot, int cell, uint32_t argb);

/* y = f(ctx, x) for a C function (a fitted curve); returns segments drawn */
int calc_plot_fn(sqg_canvas *c, sqg_rect r, const CalcView *v, double (*f)(const void *ctx, double x),
                 const void *ctx, uint32_t color);

/* Histogram bars: bin k spans start + k width to start + (k + 1) width and
 * is counts[k] high, filled with argb (blended) and outlined in edge.
 * Returns the bars at least partly in view. */
int calc_draw_histogram(sqg_canvas *c, sqg_rect r, const CalcView *v, double start, double width, const int *counts,
                        int n, uint32_t argb, uint32_t edge);

/* A box plot along x at height y (h tall in plane units): whiskers from
 * lo to hi, the box from q1 to q3, a bar at the median. Returns 1 when any
 * of it is in view. */
int calc_draw_boxplot(sqg_canvas *c, sqg_rect r, const CalcView *v, double lo, double q1, double med, double q3,
                      double hi, double y, double h, uint32_t argb, uint32_t edge);

#endif
