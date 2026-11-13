/* qrgui: the SquirrelQR small window (S3.4), drawn with libs/sqgui.
 * A toolbar (Select area, Whole screen, Copy), a file row, a preview of
 * the last image with the found symbol outlined, and the decoded text.
 * Screen capture and the clipboard are the caller's: qr_gui_frame
 * returns what was asked for, so the panel draws and tests headless. */
#ifndef QRGUI_H
#define QRGUI_H

#include "qrimage.h"
#include "sqgui.h"
#include "sqqr.h"

enum { QR_GUI_NONE, QR_GUI_SELECT, QR_GUI_SCREEN, QR_GUI_COPY };

typedef struct {
    char path[512];          /* the file row */
    QrImage img;             /* last image, owned (img.gray NULL when none) */
    uint32_t *preview;       /* img as ARGB grey */
    SqqrResult res;
    int found;               /* res holds a decoded symbol */
    char text[4 * SQQR_MAX_PAYLOAD + 1];
    const char *charset;
    char status[256];
    sqg_rect preview_at;     /* where the image went in the last frame */
} QrGui;

void qr_gui_init(QrGui *g);
void qr_gui_free(QrGui *g);

/* Take img (ownership moves to g) from source ("screen", a path) and
 * decode it. Returns 1 if a code was found. */
int qr_gui_set_image(QrGui *g, QrImage *img, const char *source);

/* Load and decode a file. Returns 1 if found, 0 if not, -1 if unreadable. */
int qr_gui_open(QrGui *g, const char *path);

/* Report a failure (capture error, cancelled select) in the status line */
void qr_gui_status(QrGui *g, const char *msg);

/* Draw a frame; returns a QR_GUI_* action for the caller */
int qr_gui_frame(QrGui *g, sqg_ui *ui);

#endif
