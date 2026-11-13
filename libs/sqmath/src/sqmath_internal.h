/* sqmath internals shared by the parser, the symbolic and the numeric parts. */
#ifndef SQMATH_INTERNAL_H
#define SQMATH_INTERNAL_H

#include "sqmath.h"

#define SQM_PI 3.14159265358979323846
#define SQM_E 2.71828182845904523536

/* fn(x) or fn(x, y) */
double sqm__apply(int fn, double x, double y);
/* append a node; its index, or -1 when full */
int sqm__node(SqmExpr *e, int kind, int fn, int a, int b, double v);
/* index of a name (added if new); -1 when the table is full, -2 when too long */
int sqm__name(SqmExpr *e, const char *s, size_t len);
int sqm__find_name(const SqmExpr *e, const char *name);
int sqm__uses_index(const SqmExpr *e, int i, int name);
void sqm__clear(SqmExpr *e);

#endif
