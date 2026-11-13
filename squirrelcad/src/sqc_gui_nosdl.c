/* SquirrelCAD built without SDL2: no window. */
#include "sqc_gui.h"

#include <stdio.h>

int sqc_gui_run(SqcGui *g, int max_frames) {
    (void)g;
    (void)max_frames;
    fprintf(stderr, "squirrelcad: built without SDL2, so there is no window (try gui DRAWING --snapshot out.ppm)\n");
    return 2;
}
