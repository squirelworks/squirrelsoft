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
          "  --mines SIZE     Mines: small (9x9, 10), medium (16x16, 40), large (30x16, 99), or WxHxM\n"
          "  --hearts         Hearts, you and three computer players, to 100\n"
          "  --chess          Chess against the computer (you white)\n"
          "  --level N        Chess: the computer's strength, 1 to 4 (3)\n"
          "  --tiles LAYOUT   Tiles, matching pairs: pyramid, towers or cross\n"
          "  --seed N         the deal (0 to 4294967295; the same number, the same deal)\n"
          "  --do CMD         play: \"draw\", \"move t3 t5\", \"move w f1\", \"smart t2\", \"auto\",\n"
          "                   \"finish\", \"undo\", \"new freecell 7\" (no window then);\n"
          "                   Mines: \"open 4 4\", \"flag 0 1\", \"chord 2 2\", \"new medium 5\";\n"
          "                   Hearts: \"pass QS AH KH\", \"play 2C\", \"next\", \"new 9\";\n"
          "                   Chess: \"move e4\", \"move Nf3\", \"undo\", \"go\", \"new black\", \"fen ...\";\n"
          "                   Tiles: \"match 12 40\", \"hint\", \"undo\", \"shuffle\", \"new cross 3\"\n"
          "  --script FILE    the same, one command a line (# for comments)\n"
          "  --show           print the table as text after the commands\n"
          "  --snapshot FILE  draw one 960x680 frame of the window to FILE (PPM), no window\n"
          "  --gui-frames N   close the window after N frames (testing)\n"
          "  --help           this text\n"
          "Piles: s (stock), w (waste), f1-f4 (home), t1-t8 (columns), c1-c4 (FreeCell's cells).\n"
          "Mines squares are X Y from 0 0 at the top-left.\n",
          f);
}

static Game game; /* about 1 MB with its undo */
static Mines mines;
static Hearts hearts;
static Chess chess;
static Tiles tiles;
static int play_mines, play_hearts, play_chess, play_tiles;
static GamesGui gui;

static int run_command(const char *where, const char *cmd) {
    char msg[120];
    if (play_tiles    ? tiles_command(&tiles, cmd, msg, sizeof msg)
        : play_chess  ? chess_command(&chess, cmd, msg, sizeof msg)
        : play_hearts ? hearts_command(&hearts, cmd, msg, sizeof msg)
        : play_mines  ? mines_command(&mines, cmd, msg, sizeof msg)
                      : game_command(&game, cmd, msg, sizeof msg)) {
        fprintf(stderr, "squirrelgames: %s%s: %s\n", where, cmd, msg);
        return -1;
    }
    if (msg[0]) printf("%s\n", msg); /* what a command answers, as a hint */
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
    int mw = 9, mh = 9, mm = 10, level = 3, layout = TILES_PYRAMID;
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
        } else if (!strcmp(a, "--mines") && more) {
            const char *z = argv[++i];
            int which = !strcmp(z, "small") ? MINES_SMALL : !strcmp(z, "medium") ? MINES_MEDIUM : !strcmp(z, "large") ? MINES_LARGE : -1;
            char x1, x2, extra;
            if (which >= 0) mines_size_of(which, &mw, &mh, &mm);
            else if (sscanf(z, "%d%c%d%c%d%c", &mw, &x1, &mh, &x2, &mm, &extra) != 5 || x1 != 'x' || x2 != 'x') {
                fprintf(stderr, "squirrelgames: --mines %s: small, medium, large, or WxHxM as in 20x12x40\n", z);
                return 2;
            }
            play_mines = 1, play_hearts = play_chess = play_tiles = 0;
        } else if (!strcmp(a, "--hearts")) {
            play_hearts = 1, play_mines = play_chess = play_tiles = 0;
        } else if (!strcmp(a, "--chess")) {
            play_chess = 1, play_mines = play_hearts = play_tiles = 0;
        } else if (!strcmp(a, "--tiles") && more) {
            const char *z = argv[++i];
            layout = !strcmp(z, "pyramid") ? TILES_PYRAMID : !strcmp(z, "towers") ? TILES_TOWERS : !strcmp(z, "cross") ? TILES_CROSS : -1;
            if (layout < 0) {
                fprintf(stderr, "squirrelgames: --tiles %s: pyramid, towers or cross\n", z);
                return 2;
            }
            play_tiles = 1, play_mines = play_hearts = play_chess = 0;
        } else if (!strcmp(a, "--level") && more) {
            char *end;
            level = (int)strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end || level < 1 || level > 4) {
                fprintf(stderr, "squirrelgames: --level %s: 1 to 4\n", argv[i]);
                return 2;
            }
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
    if (mines_new(&mines, mw, mh, mm, (uint32_t)seed)) {
        fprintf(stderr, "squirrelgames: a field is 2x2 to %dx%d, with room for a clear first square\n", MINES_MAX_W, MINES_MAX_H);
        return 2;
    }
    hearts_new(&hearts, (uint32_t)seed);
    chess_new(&chess, (uint32_t)seed);
    chess.level = level;
    tiles_new(&tiles, layout, (uint32_t)seed);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") || !strcmp(argv[i], "--snapshot") || !strcmp(argv[i], "--gui-frames") ||
            !strcmp(argv[i], "--mines") || !strcmp(argv[i], "--level") || !strcmp(argv[i], "--tiles"))
            i++;
        else if (!strcmp(argv[i], "--do") && run_command("", argv[++i])) return 1;
        else if (!strcmp(argv[i], "--script") && run_script(argv[++i])) return 1;
    }
    if (show) {
        static char text[8192];
        if (play_tiles) {
            tiles_text(&tiles, text, sizeof text);
        } else if (play_chess) {
            chess_text(&chess, text, sizeof text);
        } else if (play_hearts) {
            hearts_text(&hearts, text, sizeof text);
        } else if (play_mines) {
            static const char *const STATE[3] = {"playing", "won", "lost"};
            printf("Mines %d x %d, %d mines  field %lu  left %d  %s\n", mines.w, mines.h, mines.mines,
                   (unsigned long)mines.seed, mines_left(&mines), STATE[mines.state]);
            mines_text(&mines, text, sizeof text);
        } else {
            game_text(&game.s, text, sizeof text);
        }
        fputs(text, stdout);
    }
    games_gui_init(&gui, &game, &mines, &hearts, &chess, &tiles);
    if (play_hearts) games_gui_show(&gui, GAMES_HEARTS);
    if (play_chess) games_gui_show(&gui, GAMES_CHESS);
    if (play_tiles) games_gui_show(&gui, GAMES_TILES);
    if (play_mines) games_gui_show(&gui, GAMES_MINES);
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
