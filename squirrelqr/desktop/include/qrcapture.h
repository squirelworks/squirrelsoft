/* qrcapture: grab part of the screen as grayscale. X11 on Linux for now;
 * the Wayland portal, Windows and macOS back ends are later steps of S3.4. */
#ifndef QRCAPTURE_H
#define QRCAPTURE_H

#include "qrimage.h"

typedef struct {
    int x, y, w, h;
} QrRect;

/* Name of the compiled-in back end ("x11" or "none"). */
const char *qr_capture_backend(void);

/* Capture rect r (w or h <= 0 means the whole screen). The rect is clipped to
 * the screen. Returns 0, or -1 with a message in err. */
int qr_capture(QrRect r, QrImage *out, char *err, size_t errlen);

/* Let the user drag a rectangle with the mouse (Esc or right click cancels).
 * Returns 0 with *r set, 1 if cancelled, -1 on error. */
int qr_select_rect(QrRect *r, char *err, size_t errlen);

#endif
