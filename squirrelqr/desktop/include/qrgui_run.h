/* The SquirrelQR window loop (qrgui_sdl.c with SDL2, qrgui_nosdl.c without). */
#ifndef QRGUI_RUN_H
#define QRGUI_RUN_H

/* Open the window, optionally with first_file loaded; stop after
 * max_frames frames when > 0 (CI). Returns 0, or 2 if no window. */
int qr_gui_run(const char *first_file, int max_frames);

#endif
