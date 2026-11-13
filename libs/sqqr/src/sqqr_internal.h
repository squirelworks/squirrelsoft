/* sqqr internals shared by the decoder (sqqr.c) and the encoder
 * (sqqr_encode.c), so both use one symbol layout. Not a public API. */
#ifndef SQQR_INTERNAL_H
#define SQQR_INTERNAL_H

#include "sqqr.h"

typedef struct {
    int size, version;
    uint8_t fn[SQQR_MAX_SIZE * SQQR_MAX_SIZE]; /* 1 = function module (not data) */
} SqqrLayout;

/* Mark the function modules of a version: finders with separators and
 * format areas, timing, alignment, version blocks (the dark module lies
 * inside a format area). */
void sqqr_layout_init(SqqrLayout *l, int version);
/* Mask pattern 0..7 at row i, column j: 1 where the module is inverted */
int sqqr_mask_bit(int mask, int i, int j);
/* Remainder of v divided by the BCH generator gen of degree gen_deg */
uint32_t sqqr_bch_rem(uint32_t v, uint32_t gen, int gen_deg);
/* Character count bits for mode indicator 1, 2, 4 or 8 at a version */
int sqqr_count_bits(int mode, int version);
/* The 45 alphanumeric-mode characters in value order */
extern const char SQQR_ALNUM[];

#endif
