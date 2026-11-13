/* SquirrelCalc window: libs/sqgui SDL back end around calcgui. Original code. */
#include "calcgui.h"

#include <SDL2/SDL.h>
#include <stdio.h>

int calc_gui_run(CalcGui *g, int max_frames) {
    sqg_window *win = sqg_window_open("SquirrelCalc", 900, 600);
    if (!win) {
        fprintf(stderr, "squirrelcalc: could not open a window: %s\n", SDL_GetError());
        return 2;
    }
    sqg_ui ui;
    sqg_ui_init(&ui, NULL, 1);
    sqg_input in = {0};
    for (int n = 0; (max_frames <= 0 || n < max_frames) && sqg_window_poll(win, &in); n++) {
        if (in.key == SQG_KEY_ESCAPE && !ui.focus) break;
        sqg_begin(&ui, sqg_window_canvas(win), &in);
        calc_gui_frame(g, &ui);
        sqg_end(&ui);
        sqg_window_present(win);
        SDL_Delay(16);
    }
    sqg_window_close(win);
    return 0;
}
