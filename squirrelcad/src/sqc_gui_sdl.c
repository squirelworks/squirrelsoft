/* SquirrelCAD window: libs/sqgui SDL back end around sqc_gui. Original code. */
#include "sqc_gui.h"

#include <SDL2/SDL.h>
#include <stdio.h>

int sqc_gui_run(SqcGui *g, int max_frames) {
    sqg_window *win = sqg_window_open("SquirrelCAD", 1000, 700);
    if (!win) {
        fprintf(stderr, "squirrelcad: could not open a window: %s\n", SDL_GetError());
        return 2;
    }
    sqg_ui ui;
    sqg_ui_init(&ui, NULL, 1);
    sqg_input in = {0};
    for (int n = 0; (max_frames <= 0 || n < max_frames) && sqg_window_poll(win, &in); n++) {
        sqg_begin(&ui, sqg_window_canvas(win), &in);
        sqc_gui_frame(g, &ui);
        sqg_end(&ui);
        if (ui.quit) break;
        sqg_window_present(win);
        SDL_Delay(16);
    }
    sqg_window_close(win);
    return 0;
}
