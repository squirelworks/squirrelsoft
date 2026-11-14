/* sqcard: card reading steps over a FeliCa transport. Original code. */
#include "sqcard.h"

#include <string.h>

#define SYS_TRANSIT 0x0003
#define SYS_COMMON 0xFE00
#define SYS_WILDCARD 0xFFFF
#define SVC_SUICA_HIST 0x090F
#define SVC_SUICA_INFO 0x008B
#define SVC_NANACO_HIST 0x564F
#define SVC_NANACO_BAL 0x5597
#define SVC_EDY_HIST 0x170F
#define SVC_EDY_BAL 0x1317
#define SVC_WAON_BAL 0x6817
#define SVC_WAON_HIST 0x680B
#define HISTORY_BLOCKS 20
#define WAON_HIST_BLOCKS 6
#define READ_CHUNK 4 /* many cards refuse more blocks per command; fall back to 1 */
#define TIMEOUT_MS 100

const char *sqs_brand_id(SqsBrand b) {
    static const char *ids[] = {"suica_family", "nanaco", "edy", "waon", "unknown"};
    return ids[b <= SQS_UNKNOWN ? b : SQS_UNKNOWN];
}

const char *sqs_brand_label(SqsBrand b) {
    static const char *labels[] = {"Suica / PASMO", "nanaco", "楽天Edy", "WAON", "Unknown FeliCa"};
    return labels[b <= SQS_UNKNOWN ? b : SQS_UNKNOWN];
}

const char *sqs_card_label(const SqsCard *card) {
    if (!card) return sqs_brand_label(SQS_UNKNOWN);
    if (card->brand == SQS_SUICA && card->has_issuer)
        return sqf_suica_family_label(card->issuer_008b, SQF_BLOCK_LEN,
                                      card->n_history ? (int)card->history[0].settlement : -1);
    return sqs_brand_label(card->brand);
}

typedef struct {
    const SqsTransport *t;
    uint8_t idm[8];
    int error;
} Session;

static int poll(Session *s, uint16_t system, SqfPolling *out) {
    uint8_t cmd[16], resp[64];
    int n = sqf_polling(cmd, sizeof cmd, system, SQF_RC_SYSTEM_CODE, 0);
    int r = s->t->exchange(s->t->ctx, cmd, (size_t)n, resp, sizeof resp, TIMEOUT_MS);
    if (r < 0) s->error = 1;
    if (r <= 0 || sqf_parse_polling(resp, (size_t)r, out)) return -1;
    memcpy(s->idm, out->idm, 8);
    return 0;
}

/* Read blocks first..first+n-1 of one service. Returns blocks read (stops at
 * the first refusal), writing them to out. */
static size_t read_blocks(Session *s, uint16_t service, uint16_t first, size_t n, uint8_t *out, size_t chunk) {
    size_t got = 0;
    while (got < n && !s->error) {
        size_t want = n - got < chunk ? n - got : chunk;
        uint16_t idx[SQF_MAX_READ_BLOCKS];
        for (size_t i = 0; i < want; i++) idx[i] = (uint16_t)(first + got + i);
        uint8_t cmd[64], resp[300];
        int len = sqf_read_without_encryption(cmd, sizeof cmd, s->idm, service, idx, want);
        int r = len < 0 ? 0 : s->t->exchange(s->t->ctx, cmd, (size_t)len, resp, sizeof resp, TIMEOUT_MS);
        if (r < 0) s->error = 1;
        SqfRead rd;
        if (r <= 0 || sqf_parse_read(resp, (size_t)r, &rd) || rd.n_blocks == 0) {
            if (want > 1) { /* refused as a chunk: go on one block at a time */
                chunk = 1;
                continue;
            }
            break;
        }
        for (size_t i = 0; i < rd.n_blocks && got < n; i++) memcpy(out + SQF_BLOCK_LEN * got++, rd.blocks[i], SQF_BLOCK_LEN);
        if (rd.n_blocks < want) break;
    }
    return got;
}

int sqs_read_card(const SqsTransport *t, SqsCard *card) {
    Session s = {t, {0}, 0};
    SqfPolling p;
    memset(card, 0, sizeof *card);
    card->balance_yen = -1;
    card->brand = SQS_UNKNOWN;
    uint8_t blocks[HISTORY_BLOCKS * SQF_BLOCK_LEN], info[SQF_BLOCK_LEN];

    if (poll(&s, SYS_TRANSIT, &p) == 0) {
        size_t nh = read_blocks(&s, SVC_SUICA_HIST, 0, HISTORY_BLOCKS, blocks, READ_CHUNK);
        size_t ni = read_blocks(&s, SVC_SUICA_INFO, 0, 1, info, 1);
        int32_t bal = sqf_suica_balance(nh ? blocks : NULL, nh ? SQF_BLOCK_LEN : 0, ni ? info : NULL,
                                        ni ? SQF_BLOCK_LEN : 0);
        if (bal >= 0 || nh) {
            card->brand = SQS_SUICA;
            card->system_code = SYS_TRANSIT;
            memcpy(card->idm, p.idm, 8);
            memcpy(card->pmm, p.pmm, 8);
            card->balance_yen = bal;
            card->n_history = sqf_parse_suica_history(blocks, nh, card->history, SQS_MAX_HISTORY);
            if (ni) {
                memcpy(card->issuer_008b, info, SQF_BLOCK_LEN);
                card->has_issuer = 1;
            }
            return 0;
        }
    }
    if (s.error) return -1;
    if (poll(&s, SYS_COMMON, &p) == 0) {
        static const struct {
            SqsBrand brand;
            uint16_t balance, history;
            size_t hist_blocks;
        } purses[] = {{SQS_NANACO, SVC_NANACO_BAL, SVC_NANACO_HIST, HISTORY_BLOCKS},
                      {SQS_EDY, SVC_EDY_BAL, SVC_EDY_HIST, HISTORY_BLOCKS},
                      {SQS_WAON, SVC_WAON_BAL, SVC_WAON_HIST, WAON_HIST_BLOCKS}};
        for (size_t i = 0; i < sizeof purses / sizeof purses[0] && !s.error; i++) {
            if (read_blocks(&s, purses[i].balance, 0, 1, info, 1) != 1) continue;
            card->brand = purses[i].brand;
            card->system_code = SYS_COMMON;
            memcpy(card->idm, p.idm, 8);
            memcpy(card->pmm, p.pmm, 8);
            card->balance_yen = purses[i].brand == SQS_NANACO ? sqf_nanaco_balance(info, SQF_BLOCK_LEN)
                              : purses[i].brand == SQS_EDY    ? sqf_edy_balance(info, SQF_BLOCK_LEN)
                                                              : sqf_waon_balance(info, SQF_BLOCK_LEN);
            if (purses[i].history) {
                size_t nh = read_blocks(&s, purses[i].history, 0, purses[i].hist_blocks, blocks, READ_CHUNK);
                if (purses[i].brand == SQS_NANACO)
                    card->n_history = sqf_parse_nanaco_history(blocks, nh, card->history, SQS_MAX_HISTORY);
                else if (purses[i].brand == SQS_EDY)
                    card->n_history = sqf_parse_edy_history(blocks, nh, card->history, SQS_MAX_HISTORY);
                else
                    card->n_history = sqf_parse_waon_history(blocks, nh, card->history, SQS_MAX_HISTORY);
            }
            return 0;
        }
    }
    if (s.error) return -1;
    if (poll(&s, SYS_WILDCARD, &p) == 0) {
        memcpy(card->idm, p.idm, 8);
        memcpy(card->pmm, p.pmm, 8);
        card->system_code = p.has_request_data ? p.request_data : SYS_WILDCARD;
        return 0;
    }
    return s.error ? -1 : 1;
}
