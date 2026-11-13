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
    PAINT_TOOLS
};

typedef struct {
    int x, y, w, h;
} PaintRect;

typedef struct {
    PaintRect r; /* where */
    size_t at;   /* its pixels in the store */
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
    uint32_t todo[PAINT_MAX_W * PAINT_MAX_H];
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
 * Words may be in any case; "right" before the points of a tool uses the
 * background colour, as the secondary button does. Returns 0, or -1 with
 * why in msg. */
int paint_command(PaintDoc *d, const char *line, char *msg, size_t msgcap);

/* "#RRGGBB" or "RRGGBB" (case does not matter) to 0xFFRRGGBB; 0 on success */
int paint_parse_color(const char *s, size_t n, uint32_t *c);

#endif
