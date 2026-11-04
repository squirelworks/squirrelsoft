/* qrimage: load still images as 8-bit grayscale for the sqqr decoder.
 * Formats: Netpbm (PBM/PGM/PPM, ASCII and binary, 8 or 16 bit) and Windows BMP
 * (1/4/8-bit palette, 16/24/32-bit, uncompressed or BI_BITFIELDS). */
#ifndef QRIMAGE_H
#define QRIMAGE_H

#include <stddef.h>
#include <stdint.h>

#define QR_IMAGE_MAX_SIDE 16384
#define QR_IMAGE_MAX_PIXELS (64u * 1024u * 1024u)

typedef struct {
    int w, h;       /* stride is w */
    uint8_t *gray;  /* malloc'd, 0 = black */
} QrImage;

/* Load path into img. Returns 0, or -1 with a message in err. */
int qr_image_load(const char *path, QrImage *img, char *err, size_t errlen);
/* Same, from a buffer in memory. */
int qr_image_decode(const uint8_t *buf, size_t len, QrImage *img, char *err, size_t errlen);
/* Write img as a binary PGM. Returns 0 or -1. */
int qr_image_save_pgm(const char *path, const QrImage *img);
void qr_image_free(QrImage *img);

/* ITU-R BT.601 luma, the same weights the Android path uses. */
static inline uint8_t qr_luma(unsigned r, unsigned g, unsigned b) {
    return (uint8_t)((77u * r + 150u * g + 29u * b + 128u) >> 8);
}

#endif
