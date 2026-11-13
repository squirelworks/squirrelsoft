/* paintio: SquirrelPaint's picture files, read and written in memory.
 *
 * Read: PNG (every colour type and depth, interlaced or not; see-through
 * pixels over white), BMP (the Windows and OS/2 headers; 1, 4 and 8 bits
 * with a palette, 16, 24 and 32 bits, plain or with bit masks; bottom-up
 * or top-down) and the netpbm PPM and PGM (P6, P5, P3, P2; up to 16 bits a
 * sample).
 * Written: 8-bit RGB PNG, 24-bit BMP, and P6 PPM.
 *
 * PNG goes through a static buffer of about 8 MB (paintz.h), so this is
 * not reentrant.
 *
 * Freestanding C11: no allocation, no stdio.
 */
#ifndef PAINTIO_H
#define PAINTIO_H

#include <stddef.h>
#include <stdint.h>

enum { PAINT_FILE_BMP, PAINT_FILE_PPM, PAINT_FILE_PNG };

/* The picture in data (n bytes) into px (0xFFRRGGBB, row by row, at most
 * cap pixels). Returns 0 with its size in w and h, or -1 with why in msg:
 * not a picture this reads, cut short, or bigger than cap. */
int paint_read_image(const uint8_t *data, size_t n, uint32_t *px, size_t cap, int *w, int *h, char *msg,
                     size_t msgcap);

/* The picture as a file of the kind given, into out (cap bytes). Returns
 * the length the whole file needs, as snprintf does: it is all there only
 * when that is at most cap. For PNG, whose length is known only once it is
 * compressed, a too-small cap gives a length over cap that is enough. 0
 * when it cannot be written at all. */
size_t paint_write_image(int kind, const uint32_t *px, int w, int h, uint8_t *out, size_t cap);

/* The kind a file name asks for: .png, .ppm or .pnm, else BMP */
int paint_kind_of_name(const char *name);

#endif
