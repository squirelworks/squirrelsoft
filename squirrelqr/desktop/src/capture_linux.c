/* Linux screen capture dispatcher: Wayland → xdg-desktop-portal, else X11.
 * Both back ends may be linked; QR_CAPTURE_FRAME still wins for CI. */
#include "qrcapture_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *qr_capture_backend(void) {
#if defined(QR_HAS_PORTAL) && defined(QR_HAS_X11)
    if (getenv("WAYLAND_DISPLAY") && getenv("WAYLAND_DISPLAY")[0]) return qr_capture_backend_portal();
    if (getenv("DISPLAY") && getenv("DISPLAY")[0]) return qr_capture_backend_x11();
    return qr_capture_backend_portal();
#elif defined(QR_HAS_PORTAL)
    return qr_capture_backend_portal();
#elif defined(QR_HAS_X11)
    return qr_capture_backend_x11();
#else
    return "none";
#endif
}

int qr_capture(QrRect r, QrImage *out, char *err, size_t n) {
    if (qr_capture_try_synth(r, out, err, n)) return out->gray ? 0 : -1;
#if defined(QR_HAS_PORTAL) && defined(QR_HAS_X11)
    if (getenv("WAYLAND_DISPLAY") && getenv("WAYLAND_DISPLAY")[0]) return qr_capture_portal(r, out, err, n);
    if (getenv("DISPLAY") && getenv("DISPLAY")[0]) return qr_capture_x11(r, out, err, n);
    return qr_capture_portal(r, out, err, n);
#elif defined(QR_HAS_PORTAL)
    return qr_capture_portal(r, out, err, n);
#elif defined(QR_HAS_X11)
    return qr_capture_x11(r, out, err, n);
#else
    out->w = out->h = 0;
    out->gray = NULL;
    if (err && n) snprintf(err, n, "screen capture isn't available in this build");
    return -1;
#endif
}

int qr_select_rect(QrRect *r, char *err, size_t n) {
    {
        int syn = qr_select_try_synth(r, err, n);
        if (syn <= 0) return syn;
    }
#if defined(QR_HAS_PORTAL) && defined(QR_HAS_X11)
    if (getenv("WAYLAND_DISPLAY") && getenv("WAYLAND_DISPLAY")[0]) return qr_select_rect_portal(r, err, n);
    if (getenv("DISPLAY") && getenv("DISPLAY")[0]) return qr_select_rect_x11(r, err, n);
    return qr_select_rect_portal(r, err, n);
#elif defined(QR_HAS_PORTAL)
    return qr_select_rect_portal(r, err, n);
#elif defined(QR_HAS_X11)
    return qr_select_rect_x11(r, err, n);
#else
    if (err && n) snprintf(err, n, "screen selection isn't available in this build");
    return -1;
#endif
}
