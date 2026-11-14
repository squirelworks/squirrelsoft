/* sqcard: read a Suica-family, nanaco, Edy or WAON card over any FeliCa
 * transport with libs/sqfelica, the same steps as SquirrelSuica's readers:
 * transit first (system 0003: history 090F, attributes 008B), then the common
 * area (system FE00: nanaco, Edy, WAON purses). Read-only, no keys. */
#ifndef SQCARD_H
#define SQCARD_H

#include "sqfelica.h"
#include "sqjson.h"

#include <stddef.h>
#include <stdint.h>

/* Send one FeliCa command frame (with its length byte) and receive the reply
 * frame. Returns the reply length, 0 for no reply (timeout), -1 on a transport
 * error. */
typedef struct {
    int (*exchange)(void *ctx, const uint8_t *cmd, size_t len, uint8_t *resp, size_t cap, int timeout_ms);
    void *ctx;
} SqsTransport;

typedef enum { SQS_SUICA, SQS_NANACO, SQS_EDY, SQS_WAON, SQS_UNKNOWN } SqsBrand;

#define SQS_MAX_HISTORY 32

typedef struct {
    SqsBrand brand;
    uint8_t idm[8], pmm[8];
    uint16_t system_code;
    int32_t balance_yen; /* -1 unknown */
    size_t n_history;
    SqfHistory history[SQS_MAX_HISTORY];
    int has_issuer;              /* 1 when issuer_008b was read */
    uint8_t issuer_008b[SQF_BLOCK_LEN]; /* Suica attributes for family label */
} SqsCard;

const char *sqs_brand_id(SqsBrand b);    /* "suica_family", "nanaco", ... as felica_parse */
const char *sqs_brand_label(SqsBrand b); /* "Suica / PASMO", ... */
/* Issuer-aware label for transit cards (ICOCA, PASMO, …); else sqs_brand_label. */
const char *sqs_card_label(const SqsCard *card);

/* Returns 0 with *card filled, 1 if no card answered, -1 on a transport error. */
int sqs_read_card(const SqsTransport *t, SqsCard *card);

/* ---- a card simulated from a squirrel-felica-dump/1 JSON (SquirrelSuicaAnalysis) ----
 * It answers Polling and Read Without Encryption from the dump's systems,
 * services and blocks, so a saved dump goes through the same reader code. */
typedef struct SqsVirtualCard SqsVirtualCard;
SqsVirtualCard *sqs_virtual_card_new(const SqjValue *dump, char *err, size_t errlen);
void sqs_virtual_card_free(SqsVirtualCard *vc);
SqsTransport sqs_virtual_transport(SqsVirtualCard *vc);
/* Commands the virtual card has answered (for tests). */
unsigned sqs_virtual_card_commands(const SqsVirtualCard *vc);

#endif
