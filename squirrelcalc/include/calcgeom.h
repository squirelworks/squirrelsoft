/* calcgeom: SquirrelCalc's geometry on the graph plane. Points, segments,
 * lines, rays, circles and polygons, named and defined from the calculator
 * line; constructions that follow what they are built from (a midpoint
 * moves with its ends, an intersection with its curves); and measurements
 * that read as numbers in any expression.
 *
 * Definitions (NAME = ...):
 *   (x, y) or point(x, y)       a point; x and y may use the calculator's
 *                               variables (A = (a, 2) follows a slider), and
 *                               one with plain numbers can be dragged
 *   segment(A, B)  line(A, B)  ray(A, B)  polygon(A, B, C, ...)
 *   circle(A, r)  circle(A, B) (through B)  circle(A, B, C) (through three)
 *   midpoint(A, B)  midpoint(s)          intersect(f, g [, 2])
 *   perpendicular(l, A)  parallel(l, A)  bisector(A, B)
 * Measurements, in degrees for angles:
 *   distance(A, B)  distance(A, l)  length(s)  slope(l)  radius(c)
 *   area(p)  area(c)  perimeter(p)  perimeter(c)  angle(A, B, C)
 *   xcoord(A)  ycoord(A)
 *
 * Plain C11, no allocation (drawing uses libs/sqgui through calcgraph).
 */
#ifndef CALCGEOM_H
#define CALCGEOM_H

#include <stddef.h>

#include "calcgraph.h"
#include "calcsheet.h"

#define CALC_GEOM_MAX 32  /* objects */
#define CALC_GEOM_NAME 16 /* longest name + 1 */
#define CALC_GEOM_POLY 8  /* corners of a polygon */
#define CALC_GEOM_EXPR 64 /* a coordinate or radius as typed */

enum { CALC_G_POINT, CALC_G_SEGMENT, CALC_G_LINE, CALC_G_RAY, CALC_G_CIRCLE, CALC_G_POLYGON };

typedef struct {
    char name[CALC_GEOM_NAME];
    int kind; /* CALC_G_* */
    int how;  /* how it is made (calcgeom.c) */
    int par[CALC_GEOM_POLY], npar; /* what it is made from: earlier objects */
    char ex[2][CALC_GEOM_EXPR];    /* a point's x and y, or a circle's radius */
    int which;                     /* intersect: the first or second meeting */
    /* worked out by calc_geom_update */
    int ok;          /* 0 when it does not exist now (parallel lines never meet) */
    char why[48];    /* and why */
    double x, y;     /* a point, a circle's centre, the first point of a line */
    double x2, y2;   /* the second point of a segment, line or ray */
    double r;        /* a circle's radius */
} CalcGeomObj;

typedef struct {
    CalcGeomObj o[CALC_GEOM_MAX];
    int n;
} CalcGeom;

void calc_geom_init(CalcGeom *g);

/* NAME = rhs. Returns 0 (defined, and worked out), -1 with why in msg, or
 * -2 when rhs is not geometry at all. A name already there is redefined:
 * in place when something is built from it (which then follows, and it may
 * only be built from objects defined before it), else anew at the end. An
 * object whose parts do not meet now is kept (and -1 says why): it shows
 * again when they move. env and s give the values of names in
 * coordinates (either may be NULL). */
int calc_geom_define(CalcGeom *g, const char *name, const char *rhs, const CalcEnv *env, const CalcSheet *s,
                     char *msg, size_t msgcap);

/* Work every object out again, in order (after a variable changed) */
void calc_geom_update(CalcGeom *g, const CalcEnv *env, const CalcSheet *s);

int calc_geom_find(const CalcGeom *g, const char *name);

/* Remove an object and everything built from it; returns how many went */
int calc_geom_delete(CalcGeom *g, const char *name);

/* "A = (1, 2)", "s = segment(A, B), length 2.83", "I = intersect(l, c): they do not meet" */
void calc_geom_describe(const CalcGeom *g, int i, char *out, size_t cap);

/* Move a point made with plain numbers to (x, y), and what is built from
 * it with it; -1 when it is not one */
int calc_geom_move(CalcGeom *g, int i, double x, double y, const CalcEnv *env, const CalcSheet *s);
/* 1 when object i is a point that calc_geom_move can move */
int calc_geom_draggable(const CalcGeom *g, int i);

/* Rewrite the measurements in an expression as numbers. Returns 0, or -1
 * with why in msg. calc_geom_needs says whether text has any. */
int calc_geom_expand(const CalcGeom *g, const char *in, char *out, size_t cap, char *msg, size_t msgcap);
int calc_geom_needs(const char *text);

/* Draw every object that exists on area r: polygons and circles first,
 * then lines, then points with their names. Returns the objects drawn. */
int calc_geom_draw(sqg_canvas *c, sqg_rect r, const CalcView *v, const CalcGeom *g, int highlight);

/* The draggable point within tol pixels of (px, py) on area r, or -1 */
int calc_geom_hit(const CalcGeom *g, sqg_rect r, const CalcView *v, double px, double py, double tol);

#endif
