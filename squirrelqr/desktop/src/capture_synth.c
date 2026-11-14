/* Synthetic screen frames for CI: QR_CAPTURE_FRAME=path (same idea as
 * QR_WEBCAM_FRAME). Original code. */
#include "qrcapture_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int qr_capture_crop(QrImage *full, QrRect r, QrImage *out, char *err, size_t errlen) {
    out->w = out->h = 0;
    out->gray = NULL;
    if (!full || !full->gray || full->w < 1 || full->h < 1) {
        if (err && errlen) snprintf(err, errlen, "empty capture");
        return -1;
    }
    if (r.w <= 0 || r.h <= 0) {
        *out = *full;
        full->gray = NULL;
        full->w = full->h = 0;
        return 0;
    }
    if (r.x < 0) {
        r.w += r.x;
        r.x = 0;
    }
    if (r.y < 0) {
        r.h += r.y;
        r.y = 0;
    }
    if (r.x + r.w > full->w) r.w = full->w - r.x;
    if (r.y + r.h > full->h) r.h = full->h - r.y;
    if (r.w < 1 || r.h < 1) {
        if (err && errlen)
            snprintf(err, errlen, "the rectangle is outside the %dx%d image", full->w, full->h);
        return -1;
    }
    if (r.x == 0 && r.y == 0 && r.w == full->w && r.h == full->h) {
        *out = *full;
        full->gray = NULL;
        full->w = full->h = 0;
        return 0;
    }
    out->gray = malloc((size_t)r.w * (size_t)r.h);
    if (!out->gray) {
        if (err && errlen) snprintf(err, errlen, "out of memory");
        return -1;
    }
    out->w = r.w;
    out->h = r.h;
    for (int y = 0; y < r.h; y++)
        memcpy(out->gray + (size_t)y * r.w, full->gray + (size_t)(r.y + y) * full->w + r.x, (size_t)r.w);
    qr_image_free(full);
    return 0;
}

int qr_capture_try_synth(QrRect r, QrImage *out, char *err, size_t errlen) {
    const char *path = getenv("QR_CAPTURE_FRAME");
    if (!path || !path[0]) return 0;
    QrImage full;
    if (qr_image_load(path, &full, err, errlen) != 0) return 1;
    if (qr_capture_crop(&full, r, out, err, errlen) != 0) {
        qr_image_free(&full);
        return 1;
    }
    return 1;
}

int qr_select_try_synth(QrRect *r, char *err, size_t errlen) {
    const char *path = getenv("QR_CAPTURE_FRAME");
    if (!path || !path[0]) return 1;
    QrImage full;
    if (qr_image_load(path, &full, err, errlen) != 0) return -1;
    r->x = 0;
    r->y = 0;
    r->w = full.w;
    r->h = full.h;
    qr_image_free(&full);
    if (r->w < 8 || r->h < 8) {
        if (err && errlen) snprintf(err, errlen, "the selection is too small");
        return -1;
    }
    return 0;
}
