/* sqgui stub when SDL2 / SDL_ttf headers are not available. Original code. */
#include "sqgui.h"

#include <stdio.h>

int sqs_gui_available(void) { return 0; }

int sqs_gui_show(const SqsView *view, int autoquit_ms) {
    (void)autoquit_ms;
    fprintf(stderr,
            "GUI: this build has no SDL2/SDL_ttf (install libsdl2-dev libsdl2-ttf-dev and rebuild).\n");
    if (view) {
        fputs("(showing text instead)\n", stderr);
        sqs_view_print_text(stdout, view);
    }
    return -1;
}
