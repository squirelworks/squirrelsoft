/* SquirrelPaint built without SDL2: no window. */
#include "paintgui.h"

#include <stdio.h>

int paint_gui_run(PaintGui *g, int max_frames) {
    (void)g;
    (void)max_frames;
    fprintf(stderr, "squirrelpaint: built without SDL2, so there is no window (try --snapshot)\n");
    return 2;
}
