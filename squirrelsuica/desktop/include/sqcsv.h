/* sqcsv: write a SqsCard (+ optional stations) as CSV for local analysis.
 * Safe default: omit IDm/PMm. Original code. */
#ifndef SQCSV_H
#define SQCSV_H

#include "sqcard.h"
#include "sqfelica.h"

#include <stdio.h>

typedef struct {
    int include_idm; /* 0 = omit IDm/PMm columns (default); 1 = include hex */
} SqsCsvOpts;

/* Write UTF-8 CSV with a header row to *f. Returns 0, or -1 on write error. */
int sqs_csv_write(FILE *f, const SqsCard *card, const SqfStation *stations, size_t n_stations,
                  const SqsCsvOpts *opts);

#endif
