/* SquirrelGames built without SDL2: no window. */
#include "gamesgui.h"

#include <stdio.h>

int games_gui_run(GamesGui *gg, int max_frames) {
    (void)gg;
    (void)max_frames;
    fprintf(stderr, "squirrelgames: built without SDL2, so there is no window (try --show or --snapshot)\n");
    return 2;
}
