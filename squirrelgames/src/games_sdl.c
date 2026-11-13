/* SquirrelGames window: libs/sqgui SDL back end around gamesgui. Original code. */
#include "gamesgui.h"

#include <SDL2/SDL.h>
#include <stdio.h>

int games_gui_run(GamesGui *gg, int max_frames) {
    sqg_window *win = sqg_window_open("SquirrelGames", 960, 680);
    if (!win) {
        fprintf(stderr, "squirrelgames: could not open a window: %s\n", SDL_GetError());
        return 2;
    }
    sqg_ui ui;
    sqg_ui_init(&ui, NULL, 1);
    sqg_input in = {0};
    int kind = -2;
    for (int n = 0; (max_frames <= 0 || n < max_frames) && sqg_window_poll(win, &in); n++) {
        gg->ms = (long)SDL_GetTicks();
        sqg_begin(&ui, sqg_window_canvas(win), &in);
        games_gui_frame(gg, &ui);
        sqg_end(&ui);
        int now = gg->kind >= GAMES_MINES ? gg->kind : gg->game->s.kind;
        if (now != kind) {
            kind = now;
            sqg_window_set_title(win, kind == GAMES_BRIDGE    ? "Bridge - SquirrelGames"
                                      : kind == GAMES_TILES   ? "Tiles - SquirrelGames"
                                      : kind == GAMES_CHESS   ? "Chess - SquirrelGames"
                                      : kind == GAMES_HEARTS  ? "Hearts - SquirrelGames"
                                      : kind == GAMES_MINES   ? "Mines - SquirrelGames"
                                      : kind == GAME_FREECELL ? "FreeCell - SquirrelGames"
                                                              : "Klondike - SquirrelGames");
        }
        sqg_window_present(win);
        SDL_Delay(10);
    }
    sqg_window_close(win);
    return 0;
}
