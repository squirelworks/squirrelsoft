/* sqc_gui: the SquirrelCAD window panel on libs/sqgui (original code).
 *
 * A drawing area with the classic black background, a toolbar and a
 * command line. Lines, circles and polylines are drawn with the mouse;
 * points snap to endpoints, midpoints and centres (and to a grid), and
 * ortho keeps a line horizontal or vertical from the last point. The wheel
 * zooms about the pointer, dragging empty space pans, Fit zooms to the
 * drawing. A click selects an entity; Erase or Delete removes it. Every
 * change goes through the command line (sqc_exec), so UNDO and SAVE work
 * the same as typed commands, and anything the toolbar lacks can be typed.
 *
 * Drawn the same in an SDL2 window and off screen in tests. */
#ifndef SQC_GUI_H
#define SQC_GUI_H

#include "sqc_cmd.h"
#include "sqgui.h"

enum { SQC_TOOL_SELECT, SQC_TOOL_LINE, SQC_TOOL_CIRCLE, SQC_TOOL_PLINE, SQC_TOOLS };
enum { SQC_SNAP_OFF, SQC_SNAP_END, SQC_SNAP_MID, SQC_SNAP_CEN, SQC_SNAP_GRID };

typedef struct {
    SqcSession *s;
    double cx, cy; /* the drawing point at the middle of the area */
    double ppu;    /* pixels per drawing unit */
    int view_ready;
    int tool;
    int snap_on, grid_on, ortho;
    double grid; /* drawing units */
    SqcPoint pts[SQC_MAX_PLINE]; /* points picked for the tool so far */
    int npts;
    int selected; /* entity id, or 0 */
    sqg_rect area; /* where the drawing was drawn */
    /* the pointer: on the drawing, and where it snaps */
    int cursor_in;
    SqcPoint cursor, pick;
    int snap_kind;
    int dragging, last_mx, last_my;
    char line[160]; /* the command line */
    char out[512];  /* what the last command said */
} SqcGui;

void sqc_gui_init(SqcGui *g, SqcSession *s);
void sqc_gui_frame(SqcGui *g, sqg_ui *ui);
/* zoom to the drawing's extents (needs the area: after one frame) */
void sqc_gui_fit(SqcGui *g);

/* drawing <-> window pixels, for the area last drawn */
void sqc_gui_to_screen(const SqcGui *g, SqcPoint p, double *x, double *y);
SqcPoint sqc_gui_to_world(const SqcGui *g, double x, double y);
/* The snap point within tol_px of a window point: its SQC_SNAP_* kind,
 * or SQC_SNAP_OFF (with *out the point itself, on the grid when it is on). */
int sqc_gui_snap(const SqcGui *g, double sx, double sy, double tol_px, SqcPoint *out);
/* The model-space entity within tol_px of a window point, or 0 */
int sqc_gui_pick(const SqcGui *g, double sx, double sy, double tol_px);
/* Run a command line as typed; its output lands in g->out */
int sqc_gui_exec(SqcGui *g, const char *line);

/* the SDL2 window (sqc_gui_sdl.c), or a message when built without it */
int sqc_gui_run(SqcGui *g, int max_frames);

#endif
