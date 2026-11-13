/* SquirrelStation built without SDL2: no window. */
#include "sqs_gui.h"

#include <stdio.h>

int sqs_gui_run(SqsGui *g, int max_frames) {
    (void)g;
    (void)max_frames;
    fprintf(stderr, "squirrelstation: built without SDL2, so there is no window (try gui DESIGN --snapshot out.ppm)\n");
    return 2;
}
