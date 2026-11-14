/* qrwebcam stub: builds on Windows (mf-stub) and macOS (avf-stub) until PKG
 * brings Media Foundation / AVFoundation capture; also used when Linux has
 * no V4L2 headers. Synthetic frames via @FILE / QR_WEBCAM_FRAME still work
 * so CI and host tests can decode without a camera. */
#include "qrwebcam.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
const char *qr_webcam_backend(void) { return "mf-stub"; }
#elif defined(__APPLE__)
const char *qr_webcam_backend(void) { return "avf-stub"; }
#else
const char *qr_webcam_backend(void) { return "none"; }
#endif

int qr_webcam_list(FILE *out) {
    if (!out) return -1;
    const char *env = getenv("QR_WEBCAM_FRAME");
    if (env && env[0]) {
        fprintf(out, "@%s\tsynthetic (QR_WEBCAM_FRAME)\n", env);
        return 1;
    }
    fprintf(out, "# webcam: %s (live capture not built; use --device @FILE or QR_WEBCAM_FRAME)\n",
            qr_webcam_backend());
    return 0;
}

int qr_webcam_grab(const char *device, QrImage *out, char *err, size_t n) {
    out->w = out->h = 0;
    out->gray = NULL;
    const char *path = NULL;
    if (device && device[0] == '@') {
        if (!device[1]) {
            if (err && n) snprintf(err, n, "--device @FILE needs a path after @");
            return -1;
        }
        path = device + 1;
    } else {
        const char *env = getenv("QR_WEBCAM_FRAME");
        if (env && env[0] && (!device || !device[0])) path = env;
    }
    if (path) return qr_image_load(path, out, err, n);
    if (err && n)
        snprintf(err, n,
                 "webcam live capture isn't available in this build (%s); "
                 "pass --device @FILE.png or set QR_WEBCAM_FRAME for a synthetic frame",
                 qr_webcam_backend());
    return -1;
}
