/* SquirrelPaint window: libs/sqgui SDL back end around paintgui. Original code. */
#include "paintgui.h"

#include <SDL2/SDL.h>
#include <stdio.h>

int paint_gui_run(PaintGui *g, int max_frames) {
    sqg_window *win = sqg_window_open("SquirrelPaint", 960, 680);
    if (!win) {
        fprintf(stderr, "squirrelpaint: could not open a window: %s\n", SDL_GetError());
        return 2;
    }
    sqg_ui ui;
    sqg_ui_init(&ui, NULL, 1);
    sqg_input in = {0};
    int was_changed = -1;
    for (int n = 0; (max_frames <= 0 || n < max_frames) && sqg_window_poll(win, &in); n++) {
        sqg_begin(&ui, sqg_window_canvas(win), &in);
        paint_gui_frame(g, &ui);
        sqg_end(&ui);
        if (g->doc->changed != was_changed) { /* a star in the title while there is something unsaved */
            char title[260];
            snprintf(title, sizeof title, "%s%.200s - SquirrelPaint", g->doc->changed ? "*" : "", g->path);
            sqg_window_set_title(win, title);
            was_changed = g->doc->changed;
        }
        sqg_window_present(win);
        SDL_Delay(10);
    }
    sqg_window_close(win);
    return 0;
}
