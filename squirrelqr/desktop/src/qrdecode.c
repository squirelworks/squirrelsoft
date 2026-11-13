/* qrdecode: sqqr on a grayscale image (see qrdecode.h). */
#include "qrdecode.h"

#include <stdlib.h>

/* Try the image as is, then 2x larger: small on-screen codes at 2 px/module
 * decode more reliably once each module covers several pixels. */
int qr_decode_gray(const QrImage *img, SqqrResult *res) {
    size_t need = SQQR_IMAGE_WORK(img->w, img->h);
    void *work = malloc(need);
    if (!work) return SQQR_ERR_SIZE;
    int rc = sqqr_decode_image(img->gray, img->w, img->h, img->w, work, need, res);
    free(work);
    if (rc == SQQR_OK || (long)img->w * img->h > 1200L * 1200L) return rc;
    int w2 = img->w * 2, h2 = img->h * 2;
    uint8_t *big = malloc((size_t)w2 * h2);
    need = SQQR_IMAGE_WORK(w2, h2);
    work = malloc(need);
    if (big && work) {
        for (int y = 0; y < h2; y++)
            for (int x = 0; x < w2; x++) big[(size_t)y * w2 + x] = img->gray[(size_t)(y / 2) * img->w + x / 2];
        int rc2 = sqqr_decode_image(big, w2, h2, w2, work, need, res);
        if (rc2 == SQQR_OK) {
            for (int i = 0; i < 8; i++) res->corners[i] /= 2;
            rc = rc2;
        }
    }
    free(big);
    free(work);
    return rc;
}
