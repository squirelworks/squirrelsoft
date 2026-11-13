/* paintgui: the SquirrelPaint window panel on libs/sqgui. The tools down
 * the left with the brush size, the fill tolerance, the two colours and
 * the palette; open, save, new, undo, redo and zoom along the top; the
 * picture in the rest. The primary button paints in the first colour and
 * the secondary in the second (the eraser the other way round); Shift
 * holds shapes to squares, circles and lines at 45 degrees; Ctrl and a
 * drag move the view, the wheel zooms about the pointer, and the arrows
 * move the view too (or nudge a selection). Ctrl+Z undoes, Ctrl+Y redoes,
 * Ctrl+S saves, Ctrl+X, C, V and A cut, copy, paste and select all, Delete
 * clears the selection, Escape takes back a stroke in progress or puts the
 * selection down; the letters P B E L R O F I S pick a tool. The edit and
 * image buttons work on the selection, or the whole picture without one.
 * Files go through the open and save hooks, so this file has no stdio.
 */
#ifndef PAINTGUI_H
#define PAINTGUI_H

#include "paint.h"
#include "sqgui.h"

#define PAINT_PALETTE 28

typedef struct PaintGui PaintGui;

struct PaintGui {
    PaintDoc *doc;
    int zoom;                /* an index into the zoom steps (paint_gui_scale) */
    double ox, oy;           /* the picture point at the picture area's top-left */
    int view_ready;          /* 0: fit the picture at the next frame */
    sqg_rect area;           /* where the picture area was last drawn */
    int panning, pan_mx, pan_my;
    int painting;            /* the button holding a stroke, 0 for none */
    int mouse_in, mouse_x, mouse_y; /* the pointer over the picture, in its pixels */
    char path[200];          /* the file for Open and Save */
    char new_w[12], new_h[12]; /* the size for New */
    double size_v, tol_v;    /* the sliders */
    uint32_t palette[PAINT_PALETTE];
    char status[160];
    /* the files: return 0, or -1 with why in msg */
    int (*open_file)(PaintGui *g, const char *path, char *msg, size_t cap);
    int (*save_file)(PaintGui *g, const char *path, char *msg, size_t cap);
    void *ctx;
};

void paint_gui_init(PaintGui *g, PaintDoc *doc);

/* The scale of zoom step k: from 1/8 to 32 */
double paint_gui_scale(int k);
int paint_gui_zooms(void);

/* Fit the whole picture in the area (at most 1:1 for a big one, larger for
 * a small one) */
void paint_gui_fit(PaintGui *g);
/* Zoom to step k keeping the picture point under (sx, sy) where it is */
void paint_gui_zoom(PaintGui *g, int k, int sx, int sy);
/* The picture pixel under a window point, and back (its top-left corner) */
void paint_gui_to_picture(const PaintGui *g, int sx, int sy, int *x, int *y);
void paint_gui_to_screen(const PaintGui *g, double x, double y, int *sx, int *sy);

/* Lay out and draw one frame */
void paint_gui_frame(PaintGui *g, sqg_ui *ui);

/* The window loop: paint_sdl.c with SDL2, paint_nosdl.c without. Stops
 * after max_frames when > 0. Returns 0, or 2 when there is no window. */
int paint_gui_run(PaintGui *g, int max_frames);

#endif
