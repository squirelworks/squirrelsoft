/*
 * sqqr: QR Code decoding core (C11, no OS calls, no allocation).
 *
 * This is original code written from the public QR Code specification (ISO/IEC
 * 18004: symbol structure, format/version information, masking, block
 * interleaving, Reed-Solomon over GF(256), data modes). The error-correction
 * block table is the standard's data, cross-checked against an independent
 * encoder by tools/gen_fixtures.py.
 *
 * Two layers: sqqr_decode_grid decodes a sampled module grid (one byte per
 * module, nonzero = dark), and sqqr_decode_image finds a symbol in a grayscale
 * image (finder patterns, alignment pattern, perspective sampling) and feeds it
 * to the grid decoder.
 */
#ifndef SQQR_H
#define SQQR_H

#include <stddef.h>
#include <stdint.h>

#define SQQR_MAX_VERSION 40
#define SQQR_MAX_SIZE (17 + 4 * SQQR_MAX_VERSION) /* 177 modules */
#define SQQR_MAX_PAYLOAD 7090                     /* numeric chars at 40-L; bytes are fewer */

typedef enum { SQQR_EC_L = 0, SQQR_EC_M = 1, SQQR_EC_Q = 2, SQQR_EC_H = 3 } SqqrEcLevel;

enum {
    SQQR_OK = 0,
    SQQR_ERR_SIZE = -1,       /* size is not 21..177 with size % 4 == 1 */
    SQQR_ERR_FORMAT = -2,     /* neither format information copy decodes */
    SQQR_ERR_VERSION = -3,    /* version information doesn't decode or match the size */
    SQQR_ERR_ECC = -4,        /* a block has more errors than Reed-Solomon can fix */
    SQQR_ERR_DATA = -5,       /* the corrected bitstream is malformed */
    SQQR_ERR_OVERFLOW = -6,   /* the payload doesn't fit in SqqrResult */
    SQQR_ERR_NOT_FOUND = -7   /* no three finder patterns form a symbol in the image */
};

typedef struct {
    int version;              /* 1..40 */
    SqqrEcLevel ec_level;
    int mask;                 /* 0..7 */
    int corrected;            /* codewords fixed by Reed-Solomon, all blocks */
    int eci;                  /* first ECI assignment number, or -1 if none */
    int has_kanji;            /* payload contains Kanji-mode text, emitted as Shift JIS */
    double corners[8];        /* sqqr_decode_image only: the symbol's outer corners in the image,
                                 x,y pairs in order top-left, top-right, bottom-right, bottom-left
                                 (symbol orientation); all zero from sqqr_decode_grid */
    size_t len;               /* payload length in bytes */
    uint8_t data[SQQR_MAX_PAYLOAD + 1]; /* payload, NUL-terminated for convenience */
} SqqrResult;

/* Decode a size x size module grid, row-major, grid[r * size + c] != 0 for dark.
 * Returns SQQR_OK or a negative SQQR_ERR_* code. Reentrant; uses about 64 KB of
 * stack for its working buffers. */
int sqqr_decode_grid(const uint8_t *grid, int size, SqqrResult *out);

/* Workspace for sqqr_decode_image on a w x h image: the sampled grid, a binary
 * copy of the image and per-8x8-block statistics. */
#define SQQR_IMAGE_WORK(w, h)                                                         \
    ((size_t)SQQR_MAX_SIZE * SQQR_MAX_SIZE + (size_t)(w) * (size_t)(h) +                \
     3 * (((size_t)(w) + 7) / 8) * (((size_t)(h) + 7) / 8))

/* Find and decode one QR symbol in an 8-bit grayscale image (0 = black), rows
 * stride bytes apart. Handles rotation, moderate perspective, uneven lighting and
 * light-on-dark symbols; mirrored symbols are not tried. work must hold
 * SQQR_IMAGE_WORK(w, h) bytes. Returns SQQR_OK, SQQR_ERR_NOT_FOUND, or the grid
 * decoder's error for the best candidate. Reentrant; no allocation. */
int sqqr_decode_image(const uint8_t *gray, int w, int h, int stride, void *work, size_t work_size,
                      SqqrResult *out);

const char *sqqr_strerror(int code);

/* ---- lower-level pieces, exposed for tests and tools ---- */

/* Reed-Solomon decode in place over GF(256) (poly 0x11D, roots a^0..a^(nsym-1)),
 * cw[0] is the highest-degree coefficient. Returns the number of corrected
 * symbols, or -1 if uncorrectable. n <= 255. */
int sqqr_rs_decode(uint8_t *cw, int n, int nsym);

/* Append nsym Reed-Solomon check symbols to data (n data symbols) into ecc. */
void sqqr_rs_encode(const uint8_t *data, int n, int nsym, uint8_t *ecc);

/* Format information: 15-bit word (masked) -> 5 data bits (EC level bits + mask),
 * or -1 if more than 3 bits are wrong. */
int sqqr_decode_format_bits(uint32_t word);

/* Version information: 18-bit word -> version 7..40, or -1 if more than 3 bits are wrong. */
int sqqr_decode_version_bits(uint32_t word);

/* Codeword layout for a version and level: total codewords, EC codewords per
 * block, and the two block groups. Returns 0, or -1 for an invalid version. */
typedef struct {
    int total;
    int ec_per_block;
    int g1_blocks, g1_data;
    int g2_blocks, g2_data;
} SqqrBlocks;
int sqqr_blocks(int version, SqqrEcLevel level, SqqrBlocks *out);

/* Alignment pattern centre coordinates for a version; returns the count (0..7). */
int sqqr_alignment_positions(int version, int pos[7]);

#endif
