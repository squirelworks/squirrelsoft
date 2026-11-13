/* SquirrelCalc built without SDL2: no window. */
#include "calcgui.h"

#include <stdio.h>

int calc_gui_run(CalcGui *g, int max_frames) {
    (void)g;
    (void)max_frames;
    fprintf(stderr, "squirrelcalc: built without SDL2, so there is no window (try --snapshot)\n");
    return 2;
}
