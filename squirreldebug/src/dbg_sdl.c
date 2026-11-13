/* SquirrelDebug window: libs/sqgui's SDL back end around dbggui. Original code. */
#include "dbggui.h"

#include <SDL2/SDL.h>
#include <stdio.h>

int dbg_gui_run(DbgGui *g, int max_frames) {
    sqg_window *win = sqg_window_open(g->title[0] ? g->title : "SquirrelDebug", DBG_GUI_W, DBG_GUI_H);
    if (!win) {
        fprintf(stderr, "squirreldebug: could not open a window: %s\n", SDL_GetError());
        return 2;
    }
    sqg_ui ui;
    sqg_ui_init(&ui, NULL, 1);
    sqg_input in = {0};
    for (int n = 0; (max_frames <= 0 || n < max_frames) && sqg_window_poll(win, &in); n++) {
        sqg_begin(&ui, sqg_window_canvas(win), &in);
        dbg_gui_frame(g, &ui);
        sqg_end(&ui);
        sqg_window_present(win);
        SDL_Delay(g->dbg->state == DBG_RUNNING ? 5 : 10);
    }
    sqg_window_close(win);
    return 0;
}
