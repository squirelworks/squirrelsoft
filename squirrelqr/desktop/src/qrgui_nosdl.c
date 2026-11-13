/* SquirrelQR built without SDL2: no window. */
#include "qrgui_run.h"

#include <stdio.h>

int qr_gui_run(const char *first_file, int max_frames) {
    (void)first_file;
    (void)max_frames;
    fprintf(stderr, "squirrelqr: built without SDL2, so there is no --gui window\n");
    return 2;
}
