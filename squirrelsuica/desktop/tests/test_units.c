/* Unit tests for the SquirrelSuica desktop pieces: the JSON reader and the
 * card reader's chunk fallback over a scripted transport. */
#include "sqcard.h"
#include "sqjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);                  \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static SqjValue *parse(const char *s) {
    char err[128];
    return sqj_parse(s, strlen(s), err, sizeof err);
}

static void test_json(void) {
    SqjValue *v = parse(" {\"a\": [1, -2.5e1, true, false, null], \"s\": \"x\\u00e9\\ud83d\\udc3f\\n\\\"\", \"o\": {}} ");
    CHECK(v && v->type == SQJ_OBJECT && v->len == 3);
    const SqjValue *a = sqj_get(v, "a");
    CHECK(a && a->type == SQJ_ARRAY && a->len == 5);
    CHECK(a && a->child->number == 1 && a->child->next->number == -25);
    CHECK(a && a->child->next->next->type == SQJ_TRUE && a->child->next->next->next->next->type == SQJ_NULL);
    CHECK(sqj_str(v, "s") && !strcmp(sqj_str(v, "s"), "x\xC3\xA9\xF0\x9F\x90\xBF\n\""));
    CHECK(sqj_get(v, "o") && sqj_get(v, "o")->type == SQJ_OBJECT && sqj_get(v, "o")->len == 0);
    CHECK(!sqj_get(v, "missing") && !sqj_str(v, "a"));
    sqj_free(v);
    static const char *bad[] = {"", "{", "[1,]", "{\"a\" 1}", "\"\\x\"", "\"\\ud800\"", "tru", "1 2", "[\"a\nb\"]",
                                "{\"a\":1,}", "\"abc"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SqjValue *b = parse(bad[i]);
        if (b) fprintf(stderr, "accepted bad JSON: %s\n", bad[i]);
        CHECK(!b);
        sqj_free(b);
    }
    char deep[200];
    memset(deep, '[', 100);
    memset(deep + 100, ']', 100);
    deep[199] = 0;
    SqjValue *d = sqj_parse(deep, 200 - 1, NULL, 0);
    CHECK(!d); /* deeper than the limit */
    sqj_free(d);
}

/* A card that refuses reads of more than one block, like many real cards. */
typedef struct {
    SqsTransport inner;
    int multi_refused, reads;
} Picky;

static int picky_exchange(void *ctx, const uint8_t *cmd, size_t len, uint8_t *resp, size_t cap, int t) {
    Picky *p = ctx;
    if (len > 13 && cmd[1] == SQF_CMD_READ) {
        p->reads++;
        if (cmd[13] > 1) {
            p->multi_refused++;
            if (cap < 12) return -1;
            resp[0] = 12;
            resp[1] = 0x07;
            memcpy(resp + 2, cmd + 2, 8);
            resp[10] = 0x01;
            resp[11] = 0xA2;
            return 12;
        }
    }
    return p->inner.exchange(p->inner.ctx, cmd, len, resp, cap, t);
}

static int dead_exchange(void *ctx, const uint8_t *cmd, size_t len, uint8_t *resp, size_t cap, int t) {
    (void)ctx, (void)cmd, (void)len, (void)resp, (void)cap, (void)t;
    return -1;
}

static int silent_exchange(void *ctx, const uint8_t *cmd, size_t len, uint8_t *resp, size_t cap, int t) {
    (void)ctx, (void)cmd, (void)len, (void)resp, (void)cap, (void)t;
    return 0;
}

static void test_reader(void) {
    /* 6 history blocks, all with a date */
    char json[4096];
    int o = snprintf(json, sizeof json,
                     "{\"schema\":\"squirrel-felica-dump/1\",\"idm\":\"0101020304050607\",\"pmm\":\"0000000000000000\","
                     "\"systems\":[{\"system_code\":\"0x0003\",\"idm\":\"0101020304050607\",\"pmm\":null,"
                     "\"services\":[{\"code\":\"0x090f\",\"readable\":true,\"blocks\":[");
    for (int i = 0; i < 6; i++) {
        unsigned bal = 500 + 100u * (unsigned)i; /* newest first: each ride cost 100 */
        o += snprintf(json + o, sizeof json - (size_t)o, "%s\"1601000034%02x00000000%02x%02x00000000\"", i ? "," : "",
                      0x5c - i, bal & 0xFF, bal >> 8);
    }
    snprintf(json + o, sizeof json - (size_t)o, "]}]}]}");
    char err[128] = "";
    SqjValue *doc = sqj_parse(json, strlen(json), err, sizeof err);
    CHECK(doc != NULL);
    if (!doc) {
        fprintf(stderr, "%s\n", err);
        return;
    }
    SqsVirtualCard *vc = sqs_virtual_card_new(doc, err, sizeof err);
    sqj_free(doc);
    CHECK(vc != NULL);
    if (!vc) {
        fprintf(stderr, "%s\n", err);
        return;
    }
    Picky p = {sqs_virtual_transport(vc), 0, 0};
    SqsTransport t = {picky_exchange, &p};
    SqsCard card;
    CHECK(sqs_read_card(&t, &card) == 0);
    CHECK(card.brand == SQS_SUICA && card.n_history == 6 && card.balance_yen == 500);
    CHECK(card.history[0].fare_yen == 100 && card.history[4].balance_yen == 900);
    CHECK(p.multi_refused == 1); /* one refused chunk, then single blocks */
    sqs_virtual_card_free(vc);

    SqsTransport dead = {dead_exchange, NULL}, silent = {silent_exchange, NULL};
    CHECK(sqs_read_card(&dead, &card) == -1);
    CHECK(sqs_read_card(&silent, &card) == 1);
}

int main(void) {
    test_json();
    test_reader();
    printf("squirrelsuica desktop units: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
