/* qrwebcam: grab one grayscale frame from a camera for sqqr.
 * Linux builds use V4L2 when <linux/videodev2.h> is present. Windows and
 * macOS link a stub (Media Foundation / AVFoundation) until PKG brings
 * those runners. CI never needs a real camera: pass device "@path" or set
 * QR_WEBCAM_FRAME to a PNG/PGM/PPM/BMP with a QR code. */
#ifndef QRWEBCAM_H
#define QRWEBCAM_H

#include "qrimage.h"

#include <stdio.h>

/* Compiled-in back end: "v4l2", "mf-stub", "avf-stub", or "none". */
const char *qr_webcam_backend(void);

/* List capture devices, one per line (V4L2: /dev/videoN and the card name).
 * Returns the count, or -1 on error. Stubs print a short note and return 0. */
int qr_webcam_list(FILE *out);

/* Grab one frame into out (malloc'd gray). device may be NULL (default
 * camera), a path like /dev/video0, or "@FILE" to load FILE as a synthetic
 * frame. When device is NULL/empty and QR_WEBCAM_FRAME is set, that path is
 * used instead (for CI). Returns 0, or -1 with a message in err. */
int qr_webcam_grab(const char *device, QrImage *out, char *err, size_t errlen);

#endif
