/* Internal helpers for screen-capture back ends. Not for app code. */
#ifndef QRCAPTURE_PRIV_H
#define QRCAPTURE_PRIV_H

#include "qrcapture.h"

/* If QR_CAPTURE_FRAME is set, load that image (optionally crop to r).
 * Returns 1 if the env var handled the call (out already filled or err set),
 * 0 if the caller should do a real capture. */
int qr_capture_try_synth(QrRect r, QrImage *out, char *err, size_t errlen);

/* If QR_CAPTURE_FRAME is set, fill *r with the full synthetic frame and return 0.
 * Returns 1 if cancelled/unavailable (no env), -1 on load error. */
int qr_select_try_synth(QrRect *r, char *err, size_t errlen);

/* Crop full into out for rectangle r (clipped). full is consumed on success
 * only when the crop is a strict subset (full is freed); otherwise out takes
 * full's buffer. Returns 0 or -1. */
int qr_capture_crop(QrImage *full, QrRect r, QrImage *out, char *err, size_t errlen);

#ifdef QR_HAS_X11
const char *qr_capture_backend_x11(void);
int qr_capture_x11(QrRect r, QrImage *out, char *err, size_t errlen);
int qr_select_rect_x11(QrRect *r, char *err, size_t errlen);
#endif

#ifdef QR_HAS_PORTAL
const char *qr_capture_backend_portal(void);
int qr_capture_portal(QrRect r, QrImage *out, char *err, size_t errlen);
int qr_select_rect_portal(QrRect *r, char *err, size_t errlen);
#endif

#endif
