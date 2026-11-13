/* SquirrelQR window: libs/sqgui SDL back end around qrgui, with screen
 * capture through qrcapture and Copy through the SDL clipboard.
 * Original code. */
#include "qrcapture.h"
#include "qrgui.h"
#include "qrgui_run.h"

#include <SDL2/SDL.h>
#include <stdio.h>

int qr_gui_run(const char *first_file, int max_frames) {
    sqg_window *win = sqg_window_open("SquirrelQR", 560, 460);
    if (!win) {
        fprintf(stderr, "squirrelqr: could not open a window: %s\n", SDL_GetError());
        return 2;
    }
    QrGui g;
    qr_gui_init(&g);
    if (first_file) qr_gui_open(&g, first_file);
    sqg_ui ui;
    sqg_ui_init(&ui, NULL, 1);
    sqg_input in = {0};
    for (int n = 0; (max_frames <= 0 || n < max_frames) && sqg_window_poll(win, &in); n++) {
        if (in.key == SQG_KEY_ESCAPE && !ui.focus) break;
        sqg_begin(&ui, sqg_window_canvas(win), &in);
        int act = qr_gui_frame(&g, &ui);
        sqg_end(&ui);
        sqg_window_present(win);

        if (act == QR_GUI_COPY) {
            if (SDL_SetClipboardText(g.text) == 0) qr_gui_status(&g, "Copied to the clipboard");
            else qr_gui_status(&g, SDL_GetError());
        } else if (act == QR_GUI_SELECT || act == QR_GUI_SCREEN) {
            QrRect r = {0, 0, 0, 0};
            char err[160];
            int s = 0;
            if (act == QR_GUI_SELECT) {
                qr_gui_status(&g, "Drag a rectangle around the code (Esc cancels)");
                sqg_begin(&ui, sqg_window_canvas(win), &in);
                qr_gui_frame(&g, &ui);
                sqg_end(&ui);
                sqg_window_present(win);
                s = qr_select_rect(&r, err, sizeof err);
            }
            QrImage img;
            if (s == 1) qr_gui_status(&g, "Cancelled");
            else if (s < 0 || qr_capture(r, &img, err, sizeof err)) qr_gui_status(&g, err);
            else qr_gui_set_image(&g, &img, act == QR_GUI_SELECT ? "area" : "screen");
        }
        SDL_Delay(16);
    }
    qr_gui_free(&g);
    sqg_window_close(win);
    return 0;
}
