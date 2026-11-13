/* paintz: the zlib format (RFC 1950) with deflate (RFC 1951) inside, and
 * the CRC-32 of PNG, for SquirrelPaint's PNG files. Written from the RFCs.
 *
 * Freestanding C11, no allocation: the compressor keeps its tables in
 * static storage, so it is not reentrant.
 */
#ifndef PAINTZ_H
#define PAINTZ_H

#include <stddef.h>
#include <stdint.h>

/* Running checksums: start CRC-32 at 0 and Adler-32 at 1 */
uint32_t paint_crc32(uint32_t crc, const uint8_t *p, size_t n);
uint32_t paint_adler32(uint32_t adler, const uint8_t *p, size_t n);

/* Where compressed bytes come from: p and n to begin with, and when they
 * run out, more (if set) gives the next piece (0 at the end). PNG keeps its
 * stream in several IDAT chunks; this reads across them. */
typedef struct PaintZIn PaintZIn;
struct PaintZIn {
    const uint8_t *p;
    size_t n;
    size_t (*more)(PaintZIn *in, const uint8_t **p);
    void *ctx;
};

enum {
    PAINTZ_BAD = -1,     /* not a zlib or deflate stream, or a broken one */
    PAINTZ_FULL = -2,    /* more comes out than out holds */
    PAINTZ_SHORT = -3,   /* the stream stops early */
    PAINTZ_CHECK = -4,   /* the Adler-32 at the end does not match */
};

/* Unpack a zlib stream into out (cap bytes). Returns the bytes it made, or
 * one of PAINTZ_*. */
long paint_inflate(PaintZIn *in, uint8_t *out, size_t cap);
/* The same for raw deflate data with no zlib wrapping */
long paint_inflate_raw(PaintZIn *in, uint8_t *out, size_t cap);

/* Pack n bytes as a zlib stream into out (cap bytes): LZ77 over a 32 KB
 * window with Huffman codes made for each block (or the fixed ones, or the
 * bytes stored, whichever is shortest). Returns the length, or 0 when it
 * does not fit in cap. */
size_t paint_deflate(const uint8_t *in, size_t n, uint8_t *out, size_t cap);

#endif
