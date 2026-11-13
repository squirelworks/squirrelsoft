/* sqgui_demo: every sqgui widget in an SDL2 window (original code).
 *   build/sqgui_demo [--frames N] [--scale S]
 * --frames ends after N frames (CI runs it with SDL_VIDEODRIVER=dummy). */
#include "sqgui.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    int frames = -1, scale = 2;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: sqgui_demo [--frames N] [--scale S]\n");
            return 2;
        }
    }
    sqg_window *win = sqg_window_open("sqgui demo", 640, 420);
    if (!win) {
        fprintf(stderr, "sqgui_demo: no window: %s\n", SDL_GetError());
        return 1;
    }
    sqg_ui ui;
    sqg_ui_init(&ui, NULL, scale);
    sqg_input in;
    memset(&in, 0, sizeof in);
    int hold = 0, autor = 1, speed = 1, sel = 0, top = 0, clicks = 0, n = 0;
    double level = -60;
    char name[64] = "Squirrel";
    const char *speeds[] = { "Slow", "Normal", "Fast" };
    const char *rows[] = { "Shinjuku", "Ikebukuro", "Shibuya", "Ueno", "Akihabara",
                           "Tokyo", "Shinagawa", "Osaki", "Gotanda", "Meguro" };
    while (sqg_window_poll(win, &in) && !ui.quit) {
        sqg_canvas *cv = sqg_window_canvas(win);
        sqg_begin(&ui, cv, &in);
        sqg_fill(cv, sqg_rect_make(0, 0, cv->w, cv->h), ui.theme->face);
        int lh = sqg_line_h(&ui), y = 8;
        char status[96];
        snprintf(status, sizeof status, "clicks %d  level %.0f dB  row %s", clicks, level, rows[sel]);
        sqg_label(&ui, sqg_rect_make(8, y, cv->w - 16, lh), status, SQG_ALIGN_LEFT);
        y += lh + 4;
        sqg_row row = sqg_row_begin(sqg_rect_make(8, y, cv->w - 16, lh), 6);
        if (sqg_button(&ui, 1, sqg_row_take(&row, sqg_button_w(&ui, "Click")), "Click"))
            clicks++;
        sqg_toggle(&ui, 2, sqg_row_take(&row, sqg_button_w(&ui, "Hold")), "Hold", &hold);
        sqg_checkbox(&ui, 3, sqg_row_take(&row, sqg_button_w(&ui, "Auto") + lh), "Auto", &autor);
        if (sqg_button(&ui, 4, sqg_row_take(&row, sqg_button_w(&ui, "Quit")), "Quit"))
            ui.quit = 1;
        y += lh + 6;
        sqg_slider(&ui, 5, sqg_rect_make(8, y, cv->w - 16, lh), &level, -120, 0);
        y += lh + 6;
        sqg_segments(&ui, 6, sqg_rect_make(8, y, cv->w - 16, lh), speeds, 3, &speed);
        y += lh + 6;
        int half = (cv->w - 24) / 2;
        sqg_list(&ui, 10, sqg_rect_make(8, y, half, cv->h - y - 8), rows, 10, &sel, &top);
        sqg_textfield(&ui, 20, sqg_rect_make(16 + half, y, half, lh), name, sizeof name);
        char hello[160];
        snprintf(hello, sizeof hello, "Hello, %s!\nType in the field, pick a row,\ndrag the slider.", name);
        sqg_textbox(&ui, sqg_rect_make(16 + half, y + lh + 6, half, cv->h - y - lh - 14), hello);
        sqg_end(&ui);
        sqg_window_present(win);
        if (frames > 0 && ++n >= frames)
            break;
        SDL_Delay(16);
    }
    sqg_window_close(win);
    if (frames > 0)
        printf("sqgui_demo: %d frames ok\n", n);
    return 0;
}
