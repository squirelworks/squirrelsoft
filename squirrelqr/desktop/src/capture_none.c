/* qrcapture stub for builds without a live screen capture back end.
 * Synthetic frames via QR_CAPTURE_FRAME still work for CI. */
#include "qrcapture_priv.h"

#include <stdio.h>

const char *qr_capture_backend(void) { return "none"; }

int qr_capture(QrRect r, QrImage *out, char *err, size_t n) {
    if (qr_capture_try_synth(r, out, err, n)) return out->gray ? 0 : -1;
    out->w = out->h = 0;
    out->gray = NULL;
    if (err && n)
        snprintf(err, n,
                 "screen capture isn't available in this build; "
                 "set QR_CAPTURE_FRAME for a synthetic frame");
    return -1;
}

int qr_select_rect(QrRect *r, char *err, size_t n) {
    {
        int syn = qr_select_try_synth(r, err, n);
        if (syn <= 0) return syn;
    }
    if (err && n)
        snprintf(err, n,
                 "screen selection isn't available in this build; "
                 "set QR_CAPTURE_FRAME for a synthetic frame");
    return -1;
}
