/* sqs_gui: the SquirrelStation window panel on libs/sqgui (original code).
 *
 * One view of the design at a time: Top, Front, Right or Iso, with zoom
 * about the pointer, pan by dragging empty space and Fit. Elements are drawn
 * as the core walks them (sqs_visit: levels, cells and references), solids
 * as their edges. Select picks an element (a placed cell as a whole) and
 * shows its type, level and measures; Delete removes it.
 *
 * Line and Circle place elements with the mouse or with keyins. Points snap
 * like AccuSnap (sqs_snap in Top view; ends, middles, centres and vertices
 * on screen in the others). While a tool has a point, the AccuDraw compass
 * sits on it: the pointer indexes to its axes when near one, Ortho locks to
 * the nearer axis, and keyins (dl=, di=, xy=) are taken from that point.
 * The keyin line also takes words: LINE, CIRCLE, SELECT, TOP, FRONT, RIGHT,
 * ISO, FIT, UNDO, SAVE, DELETE, LV=name, QUIT.
 *
 * Drawn the same in an SDL2 window and off screen in tests. */
#ifndef SQS_GUI_H
#define SQS_GUI_H

#include "sqgui.h"
#include "sqstation.h"

enum { SQS_VIEW_TOP, SQS_VIEW_FRONT, SQS_VIEW_RIGHT, SQS_VIEW_ISO, SQS_VIEWS };
enum { SQS_TOOL_SELECT, SQS_TOOL_LINE, SQS_TOOL_CIRCLE, SQS_TOOLS };
/* how the pointer was taken: a snap (an SqsSnapKind), or one of these */
enum { SQS_PICK_FREE = 100, SQS_PICK_INDEX, SQS_PICK_ORTHO };

typedef struct {
    SqsDesign *d;
    const SqsDesign *const *refs; /* loaded references, for drawing and snaps */
    int nrefs;
    char path[256]; /* where Save writes (.sqdgnb binary, else text) */
    int view;
    double cx, cy; /* the view-plane point at the middle of the area */
    double ppu;    /* pixels per design unit */
    int view_ready;
    int tool;
    int level_id; /* where new elements go */
    int snap_on, ortho;
    SqsPoint last; /* the point the tool has (npts 1) */
    int npts;
    int selected;
    sqg_rect area;
    int cursor_in;
    SqsPoint pick; /* where a click would put a point */
    int pick_kind; /* SqsSnapKind or SQS_PICK_* */
    int dragging, last_mx, last_my;
    int dirty, can_undo;
    char keyin[96];
    char out[256]; /* what the last action said */
} SqsGui;

void sqs_gui_init(SqsGui *g, SqsDesign *d, const char *path);
void sqs_gui_frame(SqsGui *g, sqg_ui *ui);
void sqs_gui_fit(SqsGui *g); /* needs the area: after one frame */
void sqs_gui_set_view(SqsGui *g, int view); /* and fits */

/* design <-> window pixels in the current view. to_world puts the point on
 * the view plane through `through` (Iso: the horizontal plane). */
void sqs_gui_to_screen(const SqsGui *g, SqsPoint p, double *x, double *y);
SqsPoint sqs_gui_to_world(const SqsGui *g, double x, double y, SqsPoint through);
/* the point a click at a window point would give, and how (pick_kind) */
int sqs_gui_locate(const SqsGui *g, double sx, double sy, SqsPoint *out);
/* the element (placed cells whole) within tol_px of a window point, or 0 */
int sqs_gui_pick(const SqsGui *g, double sx, double sy, double tol_px);
/* one line about an element: type, id, level, length, area, volume */
void sqs_gui_describe(const SqsGui *g, int id, char *buf, size_t cap);
/* a keyin as typed: a point for the tool, or a word; 0, or -1 (g->out says why) */
int sqs_gui_keyin(SqsGui *g, const char *text);

/* the SDL2 window (sqs_gui_sdl.c), or a message when built without it */
int sqs_gui_run(SqsGui *g, int max_frames);

#endif
