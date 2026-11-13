/* SquirrelDebug built without SDL2: no window. */
#include "dbggui.h"

#include <stdio.h>

int dbg_gui_run(DbgGui *g, int max_frames) {
    (void)g;
    (void)max_frames;
    fprintf(stderr, "squirreldebug: built without SDL2, so there is no window (try --do or --snapshot)\n");
    return 2;
}
