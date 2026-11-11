/* sqgui: optional SDL2 window for SquirrelSuica balance + history.
 * Builds without SDL2 use a stub that prints a short message and returns. */
#ifndef SQGUI_H
#define SQGUI_H

#include "sqview.h"

/* Show *view in a native window. autoquit_ms > 0 closes after that many
 * milliseconds (for CI / headless smoke). Returns 0 on a clean quit, -1 if the
 * GUI backend is missing or failed to start. */
int sqs_gui_show(const SqsView *view, int autoquit_ms);

/* 1 when this build linked SDL2 + SDL_ttf. */
int sqs_gui_available(void);

#endif
