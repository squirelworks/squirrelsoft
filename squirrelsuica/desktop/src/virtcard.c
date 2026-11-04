/* A FeliCa card simulated from a squirrel-felica-dump/1 JSON. Original code. */
#include "sqcard.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VC_MAX_SYSTEMS 8
#define VC_MAX_SERVICES 64
#define VC_MAX_BLOCKS 64

typedef struct {
    uint16_t code;
    size_t n_blocks;
    uint8_t blocks[VC_MAX_BLOCKS][SQF_BLOCK_LEN];
} VcService;

typedef struct {
    uint16_t code;
    uint8_t idm[8], pmm[8];
    size_t n_services;
    VcService services[VC_MAX_SERVICES];
} VcSystem;

struct SqsVirtualCard {
    size_t n_systems;
    VcSystem systems[VC_MAX_SYSTEMS];
    int current; /* system selected by the last polling, -1 none */
    unsigned commands;
};

static int fail(char *err, size_t n, const char *fmt, ...) {
    if (err && n) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, n, fmt, ap);
        va_end(ap);
    }
    return -1;
}

static int hexval(char c) {
    return (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
}

/* Exactly n bytes of hex. */
static int hex_bytes(const char *s, uint8_t *out, size_t n) {
    if (!s || strlen(s) != 2 * n) return -1;
    for (size_t i = 0; i < n; i++) {
        int a = hexval(s[2 * i]), b = hexval(s[2 * i + 1]);
        if (a < 0 || b < 0) return -1;
        out[i] = (uint8_t)(a << 4 | b);
    }
    return 0;
}

/* "0x090f" or "090f" */
static int code16(const char *s, uint16_t *out) {
    if (!s) return -1;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    uint8_t b[2];
    if (hex_bytes(s, b, 2)) return -1;
    *out = (uint16_t)(b[0] << 8 | b[1]);
    return 0;
}

SqsVirtualCard *sqs_virtual_card_new(const SqjValue *dump, char *err, size_t n) {
    const char *schema = sqj_str(dump, "schema");
    if (!schema || strcmp(schema, "squirrel-felica-dump/1")) {
        fail(err, n, "not a squirrel-felica-dump/1 file");
        return NULL;
    }
    const SqjValue *systems = sqj_get(dump, "systems");
    if (!systems || systems->type != SQJ_ARRAY) {
        fail(err, n, "the dump has no systems array");
        return NULL;
    }
    SqsVirtualCard *vc = calloc(1, sizeof *vc);
    if (!vc) {
        fail(err, n, "out of memory");
        return NULL;
    }
    vc->current = -1;
    uint8_t top_idm[8] = {0}, top_pmm[8] = {0};
    hex_bytes(sqj_str(dump, "idm"), top_idm, 8);
    hex_bytes(sqj_str(dump, "pmm"), top_pmm, 8);
    for (const SqjValue *sy = systems->child; sy && vc->n_systems < VC_MAX_SYSTEMS; sy = sy->next) {
        VcSystem *s = &vc->systems[vc->n_systems];
        if (code16(sqj_str(sy, "system_code"), &s->code)) continue;
        if (hex_bytes(sqj_str(sy, "idm"), s->idm, 8)) memcpy(s->idm, top_idm, 8);
        if (hex_bytes(sqj_str(sy, "pmm"), s->pmm, 8)) memcpy(s->pmm, top_pmm, 8);
        const SqjValue *svcs = sqj_get(sy, "services");
        for (const SqjValue *sv = svcs && svcs->type == SQJ_ARRAY ? svcs->child : NULL;
             sv && s->n_services < VC_MAX_SERVICES; sv = sv->next) {
            VcService *v = &s->services[s->n_services];
            const SqjValue *readable = sqj_get(sv, "readable"), *blocks = sqj_get(sv, "blocks");
            if (code16(sqj_str(sv, "code"), &v->code) || !readable || readable->type != SQJ_TRUE || !blocks ||
                blocks->type != SQJ_ARRAY)
                continue;
            for (const SqjValue *b = blocks->child; b && v->n_blocks < VC_MAX_BLOCKS; b = b->next) {
                if (b->type != SQJ_STRING || hex_bytes(b->string, v->blocks[v->n_blocks], SQF_BLOCK_LEN)) {
                    fail(err, n, "service %04x: a block is not 32 hex digits", v->code);
                    free(vc);
                    return NULL;
                }
                v->n_blocks++;
            }
            s->n_services++;
        }
        vc->n_systems++;
    }
    if (!vc->n_systems) {
        free(vc);
        fail(err, n, "the dump has no readable systems");
        return NULL;
    }
    return vc;
}

void sqs_virtual_card_free(SqsVirtualCard *vc) { free(vc); }

unsigned sqs_virtual_card_commands(const SqsVirtualCard *vc) { return vc->commands; }

static int reply(uint8_t *resp, size_t cap, uint8_t code, const uint8_t idm[8], const uint8_t *data, size_t n) {
    if (cap < 10 + n) return -1;
    resp[0] = (uint8_t)(10 + n);
    resp[1] = code;
    memcpy(resp + 2, idm, 8);
    memcpy(resp + 10, data, n);
    return (int)(10 + n);
}

static int vc_exchange(void *ctx, const uint8_t *cmd, size_t len, uint8_t *resp, size_t cap, int timeout_ms) {
    SqsVirtualCard *vc = ctx;
    (void)timeout_ms;
    vc->commands++;
    if (len < 2 || cmd[0] != len) return 0; /* a card ignores malformed frames */
    if (cmd[1] == SQF_CMD_POLLING && len >= 6) {
        uint16_t want = (uint16_t)(cmd[2] << 8 | cmd[3]);
        for (size_t i = 0; i < vc->n_systems; i++) {
            const VcSystem *s = &vc->systems[i];
            /* FFFF matches any system; FF in either byte is a wildcard too */
            int match = ((want >> 8) == 0xFF || (want >> 8) == (s->code >> 8)) &&
                        ((want & 0xFF) == 0xFF || (want & 0xFF) == (s->code & 0xFF));
            if (!match) continue;
            vc->current = (int)i;
            uint8_t body[18];
            size_t bn = 16;
            memcpy(body, s->idm, 8);
            memcpy(body + 8, s->pmm, 8);
            if (cmd[4] == SQF_RC_SYSTEM_CODE) {
                body[16] = (uint8_t)(s->code >> 8);
                body[17] = (uint8_t)s->code;
                bn = 18;
            }
            if (cap < 2 + bn) return -1;
            resp[0] = (uint8_t)(2 + bn);
            resp[1] = 0x01;
            memcpy(resp + 2, body, bn);
            return (int)(2 + bn);
        }
        return 0;
    }
    if (len < 10) return 0;
    const VcSystem *s = NULL;
    if (vc->current >= 0 && !memcmp(cmd + 2, vc->systems[vc->current].idm, 8)) s = &vc->systems[vc->current];
    for (size_t i = 0; !s && i < vc->n_systems; i++)
        if (!memcmp(cmd + 2, vc->systems[i].idm, 8)) s = &vc->systems[i];
    if (!s) return 0; /* addressed to another card */
    if (cmd[1] == SQF_CMD_READ) {
        static const uint8_t no_service[2] = {0x01, 0xA6}, bad_block[2] = {0x01, 0xA8}, too_many[2] = {0x01, 0xA2};
        if (len < 14 || cmd[10] != 1) return reply(resp, cap, 0x07, s->idm, no_service, 2);
        uint16_t code = (uint16_t)(cmd[11] | cmd[12] << 8);
        size_t nb = cmd[13], off = 14;
        if (nb < 1 || nb > SQF_MAX_READ_BLOCKS) return reply(resp, cap, 0x07, s->idm, too_many, 2);
        const VcService *v = NULL;
        for (size_t i = 0; i < s->n_services; i++)
            if (s->services[i].code == code) v = &s->services[i];
        if (!v) return reply(resp, cap, 0x07, s->idm, no_service, 2);
        uint8_t data[3 + SQF_MAX_READ_BLOCKS * SQF_BLOCK_LEN] = {0, 0, (uint8_t)nb};
        for (size_t i = 0; i < nb; i++) {
            if (off + 2 > len) return 0;
            unsigned idx;
            if (cmd[off] & 0x80) {
                idx = cmd[off + 1];
                off += 2;
            } else {
                if (off + 3 > len) return 0;
                idx = cmd[off + 1] | (unsigned)cmd[off + 2] << 8;
                off += 3;
            }
            if (idx >= v->n_blocks) return reply(resp, cap, 0x07, s->idm, bad_block, 2);
            memcpy(data + 3 + i * SQF_BLOCK_LEN, v->blocks[idx], SQF_BLOCK_LEN);
        }
        return reply(resp, cap, 0x07, s->idm, data, 3 + nb * SQF_BLOCK_LEN);
    }
    if (cmd[1] == SQF_CMD_REQUEST_RESPONSE) {
        static const uint8_t mode = 0;
        return reply(resp, cap, 0x05, s->idm, &mode, 1);
    }
    return 0; /* other commands: no answer, as for an unsupported command */
}

SqsTransport sqs_virtual_transport(SqsVirtualCard *vc) {
    SqsTransport t = {vc_exchange, vc};
    return t;
}
