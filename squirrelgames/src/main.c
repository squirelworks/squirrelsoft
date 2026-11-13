/* SquirrelGames: patience card games on libs/sqgui. Original code. */
#include "gamesgui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
    fputs("usage: squirrelgames [options]\n"
          "  --klondike       Klondike, a card at a time from the stock (the default)\n"
          "  --klondike3      Klondike, three at a time\n"
          "  --freecell       FreeCell\n"
          "  --seed N         the deal (0 to 4294967295; the same number, the same deal)\n"
          "  --do CMD         play: \"draw\", \"move t3 t5\", \"move w f1\", \"smart t2\", \"auto\",\n"
          "                   \"finish\", \"undo\", \"new freecell 7\" (no window then)\n"
          "  --script FILE    the same, one command a line (# for comments)\n"
          "  --show           print the table as text after the commands\n"
          "  --snapshot FILE  draw one 960x680 frame of the window to FILE (PPM), no window\n"
          "  --gui-frames N   close the window after N frames (testing)\n"
          "  --help           this text\n"
          "Piles: s (stock), w (waste), f1-f4 (home), t1-t8 (columns), c1-c4 (FreeCell's cells).\n",
          f);
}

static Game game; /* about 1 MB with its undo */
static GamesGui gui;

static int run_command(const char *where, const char *cmd) {
    char msg[120];
    if (game_command(&game, cmd, msg, sizeof msg)) {
        fprintf(stderr, "squirrelgames: %s%s: %s\n", where, cmd, msg);
        return -1;
    }
    return 0;
}

static int run_script(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "squirrelgames: cannot read %s\n", path);
        return -1;
    }
    char line[512], where[300];
    int no = 0, rc = 0;
    while (fgets(line, sizeof line, f)) {
        no++;
        line[strcspn(line, "\r\n")] = 0;
        const char *p = line + strspn(line, " \t");
        if (!*p || *p == '#') continue;
        snprintf(where, sizeof where, "%.200s:%d: ", path, no);
        if (run_command(where, p)) {
            rc = -1;
            break;
        }
    }
    fclose(f);
    return rc;
}

int main(int argc, char **argv) {
    int kind = GAME_KLONDIKE, draw = 1, frames = 0, show = 0, batch = 0;
    unsigned long seed = 1;
    const char *snapshot = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            return 0;
        } else if (!strcmp(a, "--klondike")) {
            kind = GAME_KLONDIKE, draw = 1;
        } else if (!strcmp(a, "--klondike3")) {
            kind = GAME_KLONDIKE, draw = 3;
        } else if (!strcmp(a, "--freecell")) {
            kind = GAME_FREECELL;
        } else if (!strcmp(a, "--seed") && more) {
            char *end;
            seed = strtoul(argv[++i], &end, 10);
            if (end == argv[i] || *end || seed > 0xFFFFFFFFul) {
                fprintf(stderr, "squirrelgames: --seed %s: a number from 0 to 4294967295\n", argv[i]);
                return 2;
            }
        } else if ((!strcmp(a, "--do") || !strcmp(a, "--script")) && more) {
            i++;
            batch = 1;
        } else if (!strcmp(a, "--show")) {
            show = batch = 1;
        } else if (!strcmp(a, "--snapshot") && more) {
            snapshot = argv[++i];
        } else if (!strcmp(a, "--gui-frames") && more) {
            frames = atoi(argv[++i]);
        } else {
            usage(stderr);
            return 2;
        }
    }
    game_new(&game, kind, draw, (uint32_t)seed);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") || !strcmp(argv[i], "--snapshot") || !strcmp(argv[i], "--gui-frames")) i++;
        else if (!strcmp(argv[i], "--do") && run_command("", argv[++i])) return 1;
        else if (!strcmp(argv[i], "--script") && run_script(argv[++i])) return 1;
    }
    if (show) {
        static char text[8192];
        game_text(&game.s, text, sizeof text);
        fputs(text, stdout);
    }
    games_gui_init(&gui, &game);
    if (snapshot) {
        enum { W = 960, H = 680 };
        static uint32_t px[W * H];
        sqg_canvas cv;
        sqg_canvas_init(&cv, px, W, H, W);
        sqg_ui ui;
        sqg_ui_init(&ui, NULL, 1);
        sqg_input in = {0};
        in.mx = in.my = -1;
        sqg_begin(&ui, &cv, &in);
        games_gui_frame(&gui, &ui);
        sqg_end(&ui);
        if (sqg_write_ppm(&cv, snapshot)) {
            fprintf(stderr, "squirrelgames: could not write %s\n", snapshot);
            return 1;
        }
        return 0;
    }
    if (batch) return 0;
    return games_gui_run(&gui, frames);
}
