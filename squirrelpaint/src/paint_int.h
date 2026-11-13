/* paint: what paint.c and paintsel.c share inside the core. Not for users of paint.h. */
#ifndef PAINT_INT_H
#define PAINT_INT_H

#include "paint.h"

/* the changed area: emptied, and grown to take in x0..x1, y0..y1 (clipped) */
void paint__dirty_reset(PaintDoc *d);
void paint__touch(PaintDoc *d, int x0, int y0, int x1, int y1);
/* x0..x1 on row y in c, clipped */
void paint__span(PaintDoc *d, int x0, int x1, int y, uint32_t c);
/* the picture into before, and the changed area emptied */
void paint__save_before(PaintDoc *d);
/* r as it was in before, kept as a step to undo */
void paint__push_step(PaintDoc *d, PaintRect r);
/* the whole picture as it was in before (ow x oh), kept as a step to undo */
void paint__push_whole(PaintDoc *d, int ow, int oh);

/* the command line's pieces: a message of a then b; the next word (its
 * start and length, 0 at the end); a word against a lower-case one; a
 * whole number of at most 100000 (0, or -1) */
void paint__say(char *msg, size_t cap, const char *a, const char *b);
size_t paint__word(const char **p, const char **start);
int paint__is(const char *w, size_t n, const char *s);
int paint__number(const char *w, size_t n, int *v);

/* the select tool (paintsel.c) */
void paint__sel_press(PaintDoc *d, int x, int y);
void paint__sel_drag(PaintDoc *d, int x, int y);
void paint__sel_release(PaintDoc *d, int x, int y);
/* the command words of paintsel.c: 1 when line was one of them (rc set), 0 when not */
int paint__sel_command(PaintDoc *d, const char *word, size_t n, const char *rest, int *rc, char *msg, size_t cap);

#endif
