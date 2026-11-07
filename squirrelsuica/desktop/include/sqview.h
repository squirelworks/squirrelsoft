/* sqview: format a SqsCard (plus optional station table) into display rows
 * shared by the text CLI and the SDL GUI. Original code. */
#ifndef SQVIEW_H
#define SQVIEW_H

#include "sqcard.h"
#include "sqfelica.h"

#include <stddef.h>
#include <stdio.h>

#define SQS_VIEW_ROUTE_MAX 192
#define SQS_VIEW_DETAIL_MAX 160

typedef struct {
    char date[16];
    char kind[24];
    char fare[40];
    char balance[40];
    char route[SQS_VIEW_ROUTE_MAX];
    char detail[SQS_VIEW_DETAIL_MAX];
} SqsViewRow;

typedef struct {
    char brand[64];
    char brand_id[32];
    char idm[20];
    char pmm[20];
    char system[16];
    char balance[48]; /* "Balance ¥1234" or "Balance (unknown)" */
    int32_t balance_yen; /* -1 unknown; mirrors card */
    size_t n_rows;
    SqsViewRow rows[SQS_MAX_HISTORY];
} SqsView;

/* Fill *out from card + station table (rows may be NULL / n=0). */
void sqs_view_build(SqsView *out, const SqsCard *card, const SqfStation *stations, size_t n_stations);

/* Write the same text layout as the CLI (without a trailing JSON mode). */
void sqs_view_print_text(FILE *f, const SqsView *v);

#endif
