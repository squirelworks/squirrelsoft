/* The SquirrelCAD command line: classic drafting verbs typed one line at a
 * time, interactively or from a script (original code).
 *
 *   LINE 0,0 100,0 @0,50 @50<180     a chain of lines
 *   CIRCLE 50,25 10                  centre, radius
 *   ARC 0,0 10 0 90                  centre, radius, start and end angle
 *   PLINE 0,0 10,0 10,10 CLOSE       a polyline, closed or not
 *   TEXT 5,5 Lot 12                  text to the end of the line
 *   LAYER NEW walls 1 | SET walls | ON walls | OFF walls | COLOR walls 3 | LIST
 *   BLOCK door 0,0 4 5 6             a block from entities 4, 5, 6 (taken out of model space)
 *   INSERT door 20,0 [SCALE [ROT]]
 *   ERASE sel | MOVE sel P1 P2 | COPY sel P1 P2 | ROTATE sel BASE DEG | SCALE sel BASE F
 *   LIST [sel]  DIST P1 P2  AREA id  EXTENTS  UNDO
 *   SAVE [path]  OPEN path  NEW [title]  PLOT out.svg [A3] [1:100|FIT] [PORTRAIT]
 *   QUIT
 *
 * Points: x,y (or x,y,z), @dx,dy relative to the last point, d<angle polar
 * from the origin, @d<angle polar from the last point (degrees,
 * counter-clockwise from east). A selection is entity ids, ALL (every
 * model-space entity) or LAST (the last one made). Commands are not case
 * sensitive; # starts a comment.
 */
#ifndef SQC_CMD_H
#define SQC_CMD_H

#include <stddef.h>

#include "sqcad.h"

typedef struct {
    SqcDrawing dwg;
    SqcDrawing undo; /* the drawing before the last change */
    int can_undo;
    SqcPoint last;   /* for @ points */
    int last_id;     /* for LAST */
    int dirty;       /* changed since the last save */
    char path[256];  /* where SAVE writes */
} SqcSession;

void sqc_session_init(SqcSession *s, const char *path);

enum { SQC_CMD_OK = 0, SQC_CMD_ERROR = -1, SQC_CMD_QUIT = 1 };

/* Run one command line. out gets what to show (a result, or the error);
 * returns SQC_CMD_OK, SQC_CMD_ERROR or SQC_CMD_QUIT. */
int sqc_exec(SqcSession *s, const char *line, char *out, size_t cap);

#endif
