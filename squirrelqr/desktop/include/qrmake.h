/* qrmake: generate QR codes with sqqr_encode and write them out as PNG,
 * SVG or text for a terminal. Shared by the CLI (--encode) and the window. */
#ifndef QRMAKE_H
#define QRMAKE_H

#include "qrimage.h"
#include "sqqr.h"

#include <stdio.h>

typedef struct {
    SqqrEncodeInfo info;
    uint8_t grid[SQQR_MAX_SIZE * SQQR_MAX_SIZE]; /* info.size squared, 1 = dark */
} QrSymbol;

/* Encode text at a level (automatic version, mode and mask). Returns 0, or
 * -1 with the reason in err. */
int qr_make(const char *text, size_t len, SqqrEcLevel level, QrSymbol *s, char *err, size_t errlen);

/* "L", "M", "Q", "H" (either case) to a level; -1 if not one of them */
int qr_level_parse(const char *s);

/* The symbol as grayscale pixels (0 dark, 255 light): scale px per module
 * and a border of quiet-zone modules around it. Returns 0 or -1. */
int qr_symbol_image(const QrSymbol *s, int scale, int border, QrImage *out);

/* 8-bit grayscale PNG (stored deflate blocks: no compressor needed). */
int qr_png_write_gray(const char *path, const QrImage *img);
/* Same, into memory: returns the length written into buf, or the size
 * needed when it doesn't fit (buf may be NULL). */
size_t qr_png_encode_gray(const QrImage *img, uint8_t *buf, size_t cap);

/* SVG with one path, viewBox in modules, border modules around it */
int qr_write_svg(const char *path, const QrSymbol *s, int border);

/* Two module rows per text line with Unicode half blocks. invert 0 suits a
 * dark terminal (light modules are drawn), 1 a light one. */
void qr_print_terminal(FILE *f, const QrSymbol *s, int border, int invert);

/* Write by the file name's extension: .png, .svg, .pgm. Returns 0, or -1
 * with err set. */
int qr_save_symbol(const char *path, const QrSymbol *s, int scale, int border, char *err, size_t errlen);

#endif
