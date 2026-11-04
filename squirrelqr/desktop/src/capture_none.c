/* qrcapture stub for builds without a screen capture back end. */
#include "qrcapture.h"

#include <stdio.h>

const char *qr_capture_backend(void) { return "none"; }

int qr_capture(QrRect r, QrImage *out, char *err, size_t n) {
    (void)r;
    out->w = out->h = 0;
    out->gray = NULL;
    if (err && n) snprintf(err, n, "screen capture isn't available in this build (X11 only for now)");
    return -1;
}

int qr_select_rect(QrRect *r, char *err, size_t n) {
    (void)r;
    if (err && n) snprintf(err, n, "screen selection isn't available in this build (X11 only for now)");
    return -1;
}
