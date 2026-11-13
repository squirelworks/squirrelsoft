/* paint: the SquirrelPaint core. One picture of up to PAINT_MAX_W x
 * PAINT_MAX_H pixels (0xAARRGGBB, opaque), the tools that draw on it, and
 * undo. A tool is used the way a mouse uses it: press at a point, drag
 * through more, release; a line, rectangle or ellipse is shown as it is
 * dragged and kept on release, and every press-to-release is one step to
 * undo. paint_command runs the same tools from a line of text.
 *
 * Freestanding C11: no allocation, no stdio, nothing from libc but
 * memcpy, memmove and memset, so it moves to Squirrel OS/2 as it is. The
 * PaintDoc is large (the picture, the picture as it was at the press, and
 * the undo store): keep it static.
 */
#ifndef PAINT_H
#define PAINT_H

#include <stddef.h>
#include <stdint.h>

#ifndef PAINT_MAX_W
#define PAINT_MAX_W 1024
#endif
#ifndef PAINT_MAX_H
#define PAINT_MAX_H 1024
#endif
#ifndef PAINT_UNDO_PX
#define PAINT_UNDO_PX (2 * PAINT_MAX_W * PAINT_MAX_H) /* pixels kept for undo */
#endif
#define PAINT_UNDO_STEPS 64
#define PAINT_SIZE_MAX 64 /* the widest brush */

enum {
    PAINT_PENCIL,  /* one pixel wide, whatever the size */
    PAINT_BRUSH,   /* round, size wide */
    PAINT_ERASER,  /* the brush in the background colour */
    PAINT_LINE,
    PAINT_RECT,
    PAINT_ELLIPSE,
    PAINT_FILL,    /* flood fill, within tolerance */
    PAINT_PICKER,  /* takes the colour under it */
    PAINT_SELECT,  /* a rectangle; dragged inside, it moves what it holds */
    PAINT_TOOLS
};

typedef struct {
    int x, y, w, h;
} PaintRect;

typedef struct {
    PaintRect r;  /* where; for a whole picture, 0, 0 and its size */
    size_t at;    /* its pixels in the store */
    size_t cap;   /* the store it holds: its area, or for a whole picture the larger of the two sizes */
    int whole;    /* the whole picture, whose size changed (rotate, resize, crop) */
} PaintStep;

typedef struct {
    int w, h;
    uint32_t px[PAINT_MAX_W * PAINT_MAX_H]; /* row by row, w to a row */
    /* the tools */
    int tool;          /* PAINT_* */
    uint32_t fg, bg;   /* what the primary and secondary button paint */
    int size;          /* brush, eraser and shape outline width, 1..PAINT_SIZE_MAX */
    int filled;        /* rectangles and ellipses: filled with the colour */
    int tolerance;     /* fill: how far (0..255 on each channel) a colour may be from the one clicked */
    /* a press in progress */
    int active;        /* 0, or 1 / 2 for the button that pressed */
    int x0, y0, lx, ly;
    int square;        /* shapes: a square, a circle, a line at a multiple of 45 degrees (Shift) */
    PaintRect dirty;   /* what the press has changed so far */
    uint32_t before[PAINT_MAX_W * PAINT_MAX_H]; /* the picture at the press */
    /* undo: the pixels a step changed, as they were; undoing swaps them in */
    PaintStep step[PAINT_UNDO_STEPS];
    int nsteps, done;  /* steps kept, and how many of them are applied */
    uint32_t store[PAINT_UNDO_PX];
    size_t used;
    /* fill's own: the pixels it has reached, and the ones still to look around */
    uint8_t mark[(PAINT_MAX_W * PAINT_MAX_H + 7) / 8];
    uint32_t todo[PAINT_MAX_W * PAINT_MAX_H]; /* also a scratch picture for the transforms */
    /* the selection: a rectangle of the picture (w 0: none). Floating, its
     * pixels are lifted off the picture into flt (sel.w x sel.h) and drawn
     * over it at sel until they are dropped back; from is where they were
     * lifted from (w 0 for a paste), left in the background colour. The
     * picture as it was at the lift is in before, so the lift, the moves
     * and the drop are one step to undo. */
    PaintRect sel, from;
    int floating;
    uint32_t flt[PAINT_MAX_W * PAINT_MAX_H];
    int sel_mode, grab_x, grab_y; /* the select tool: 1 drawing a rectangle, 2 moving; where it was taken */
    /* the clipboard */
    uint32_t clip[PAINT_MAX_W * PAINT_MAX_H];
    int clip_w, clip_h;
    int changed;       /* drawn on since this was last cleared (by the caller, after a save) */
} PaintDoc;

/* A white w x h picture (at most PAINT_MAX_W x PAINT_MAX_H, at least 1x1),
 * black on white, the brush at 4. Returns 0, or -1 for a size out of range. */
int paint_new(PaintDoc *d, int w, int h);
/* The picture replaced by w x h pixels (from a file); undo starts afresh. */
int paint_load(PaintDoc *d, const uint32_t *px, int w, int h);

uint32_t paint_get(const PaintDoc *d, int x, int y); /* 0 off the picture */

/* The tool at a point of the picture (points off it are fine: drawing is
 * clipped). button is 1 (fg) or 2 (bg); square as for PaintDoc. */
void paint_press(PaintDoc *d, int x, int y, int button, int square);
void paint_drag(PaintDoc *d, int x, int y, int square);
void paint_release(PaintDoc *d, int x, int y, int square);
/* Put the picture back as it was at the press (Escape, say) */
void paint_cancel(PaintDoc *d);

/* 1 when there is a step to undo / to redo; undo and redo return 0, or -1
 * when there is none */
int paint_can_undo(const PaintDoc *d);
int paint_can_redo(const PaintDoc *d);
int paint_undo(PaintDoc *d);
int paint_redo(PaintDoc *d);

/* What shows at a pixel: the floating selection over the picture */
uint32_t paint_shown(const PaintDoc *d, int x, int y);

/* The selection. select: a new rectangle (corners in any order, clipped),
 * after dropping what floats; -1 when nothing of it is on the picture.
 * none drops and forgets it. lift: the selected pixels off the picture to
 * float (a no-op when they float already). move: by dx, dy, lifting first.
 * drop: the floating pixels back onto the picture, ending the step; the
 * rectangle stays selected. Each returns 0, or -1 with no selection. */
int paint_select(PaintDoc *d, int x0, int y0, int x1, int y1);
void paint_select_all(PaintDoc *d);
void paint_select_none(PaintDoc *d);
int paint_lift(PaintDoc *d);
int paint_move_selection(PaintDoc *d, int dx, int dy);
void paint_drop(PaintDoc *d);
/* The clipboard: copy and cut take the selection (cut leaves the
 * background colour); paste floats the clipboard with its top-left at x, y
 * (on the picture); delete fills the selection with the background
 * colour. 0, or -1 with nothing to work on. */
int paint_copy(PaintDoc *d);
int paint_cut(PaintDoc *d);
int paint_paste(PaintDoc *d, int x, int y);
int paint_delete(PaintDoc *d);

/* Transforms of the selection (lifted first) or, with none, the whole
 * picture; each one step to undo. rotate: quarter turns clockwise (1, 2,
 * 3, or -1 for one anticlockwise). resize: to w x h, by the nearest pixel
 * or, smooth, by weighing the four nearest. crop: the picture cut to the
 * selection. canvas: the picture made w x h, kept at the top-left and
 * grown with the background colour. 0, or -1 for a size out of range or
 * (crop) no selection. */
int paint_flip(PaintDoc *d, int vertical);
int paint_rotate(PaintDoc *d, int quarters);
int paint_resize(PaintDoc *d, int w, int h, int smooth);
int paint_crop(PaintDoc *d);
int paint_canvas(PaintDoc *d, int w, int h);
int paint_invert(PaintDoc *d);

/* Drawing in fg, clipped, without undo (the tools use these) */
void paint_dab(PaintDoc *d, int x, int y, int size, uint32_t c);
void paint_line(PaintDoc *d, int x0, int y0, int x1, int y1, int size, uint32_t c);
void paint_rect(PaintDoc *d, int x0, int y0, int x1, int y1, int size, int filled, uint32_t c);
void paint_ellipse(PaintDoc *d, int x0, int y0, int x1, int y1, int size, int filled, uint32_t c);
/* Returns the pixels changed */
long paint_flood(PaintDoc *d, int x, int y, uint32_t c, int tolerance);

/* One line of drawing, for scripts and tests (numbers are whole pixels):
 *   new W H                      a white picture
 *   color RRGGBB   bg RRGGBB     (a leading # is fine)
 *   size N   tolerance N   fill on|off   (fill on: shapes are filled)
 *   pencil|brush|eraser X Y [X Y ...]   a stroke through the points
 *   line|rect|ellipse X0 Y0 X1 Y1 [square]
 *   fill X Y   pick X Y
 *   undo   redo   clear (the background colour all over, as one step)
 *   select X0 Y0 X1 Y1 | select all | select none
 *   copy   cut   paste [X Y]   delete   move DX DY   drop
 *   flip h|v   rotate 90|180|270|-90   resize W H [smooth]   crop
 *   canvas W H   invert
 * Words may be in any case; "right" before the points of a tool uses the
 * background colour, as the secondary button does. Returns 0, or -1 with
 * why in msg. */
int paint_command(PaintDoc *d, const char *line, char *msg, size_t msgcap);

/* "#RRGGBB" or "RRGGBB" (case does not matter) to 0xFFRRGGBB; 0 on success */
int paint_parse_color(const char *s, size_t n, uint32_t *c);

#endif
