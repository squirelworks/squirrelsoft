/* qrdecode: run sqqr on a grayscale image, retrying at 2x for small
 * on-screen codes. Shared by the CLI and the GUI window. */
#ifndef QRDECODE_H
#define QRDECODE_H

#include "qrimage.h"
#include "sqqr.h"

/* Returns SQQR_OK with res filled (corners in img coordinates), or an
 * SQQR_ERR_* code. */
int qr_decode_gray(const QrImage *img, SqqrResult *res);

#endif
