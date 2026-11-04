/* sqpcsc reader access over the PC/SC API (pcsc-lite, macOS PCSC.framework,
 * Windows winscard). Original code. */
#include "sqpcsc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winscard.h>
#else
#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>
#endif
#endif

#ifndef SCARD_E_NO_READERS_AVAILABLE
#define SCARD_E_NO_READERS_AVAILABLE ((LONG)0x8010002E)
#endif

#define EXCHANGE_TIMEOUT_US 100000u /* per FeliCa command; reads answer in a few ms */

struct SqsPcsc {
    SCARDCONTEXT ctx;
    SCARDHANDLE card;
    DWORD protocol;
    char reader[256];
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

static const char *pcsc_msg(LONG rc) {
#ifdef _WIN32
    static char buf[32];
    snprintf(buf, sizeof buf, "0x%08lX", (unsigned long)rc);
    return buf;
#else
    return pcsc_stringify_error(rc);
#endif
}

int sqs_pcsc_list(char *buf, size_t cap, char *err, size_t n) {
    SCARDCONTEXT ctx;
    LONG rc = SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &ctx);
    if (rc != SCARD_S_SUCCESS) return fail(err, n, "PC/SC is not available (%s); is pcscd running?", pcsc_msg(rc));
    DWORD len = (DWORD)cap;
    rc = SCardListReaders(ctx, NULL, buf, &len);
    SCardReleaseContext(ctx);
    if (rc == SCARD_E_NO_READERS_AVAILABLE) {
        if (cap >= 2) buf[0] = buf[1] = 0;
        return 0;
    }
    if (rc != SCARD_S_SUCCESS) return fail(err, n, "cannot list readers: %s", pcsc_msg(rc));
    int count = 0;
    for (const char *p = buf; *p; p += strlen(p) + 1) count++;
    return count;
}

static LONG transmit(SqsPcsc *p, const uint8_t *apdu, size_t len, uint8_t *resp, DWORD *rlen) {
    const SCARD_IO_REQUEST *pci = p->protocol == SCARD_PROTOCOL_T1 ? SCARD_PCI_T1 : SCARD_PCI_T0;
    return SCardTransmit(p->card, pci, apdu, (DWORD)len, NULL, resp, rlen);
}

static int sw_ok(const uint8_t *r, DWORD n) { return n >= 2 && r[n - 2] == 0x90; }

SqsPcsc *sqs_pcsc_open(const char *reader, int wait_ms, int *no_card, char *err, size_t n) {
    *no_card = 0;
    SqsPcsc *p = calloc(1, sizeof *p);
    if (!p) {
        fail(err, n, "out of memory");
        return NULL;
    }
    LONG rc = SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &p->ctx);
    if (rc != SCARD_S_SUCCESS) {
        fail(err, n, "PC/SC is not available (%s); is pcscd running?", pcsc_msg(rc));
        free(p);
        return NULL;
    }
    char names[4096];
    DWORD len = sizeof names;
    rc = SCardListReaders(p->ctx, NULL, names, &len);
    if (rc != SCARD_S_SUCCESS) {
        fail(err, n, rc == SCARD_E_NO_READERS_AVAILABLE ? "no card reader is connected" : "cannot list readers: %s",
             pcsc_msg(rc));
        goto out;
    }
    /* Watch the chosen reader, or all of them. */
    SCARD_READERSTATE st[16];
    int ns = 0;
    for (const char *q = names; *q && ns < 16; q += strlen(q) + 1) {
        if (reader && !strstr(q, reader)) continue;
        memset(&st[ns], 0, sizeof st[ns]);
        st[ns].szReader = q;
        st[ns].dwCurrentState = SCARD_STATE_UNAWARE;
        ns++;
    }
    if (!ns) {
        fail(err, n, "no reader matches \"%s\" (try --list-readers)", reader);
        goto out;
    }
    int waited = 0;
    for (;;) {
        rc = SCardGetStatusChange(p->ctx, 0, st, (DWORD)ns);
        if (rc != SCARD_S_SUCCESS && rc != (LONG)SCARD_E_TIMEOUT) {
            fail(err, n, "reader status: %s", pcsc_msg(rc));
            goto out;
        }
        int found = -1;
        for (int i = 0; i < ns; i++) {
            if ((st[i].dwEventState & SCARD_STATE_PRESENT) && !(st[i].dwEventState & SCARD_STATE_MUTE)) found = i;
            st[i].dwCurrentState = st[i].dwEventState;
        }
        if (found >= 0) {
            snprintf(p->reader, sizeof p->reader, "%s", st[found].szReader);
            break;
        }
        if (waited >= wait_ms) {
            *no_card = 1;
            fail(err, n, "no card on the reader");
            goto out;
        }
        int step = wait_ms - waited < 500 ? wait_ms - waited : 500;
        rc = SCardGetStatusChange(p->ctx, (DWORD)step, st, (DWORD)ns);
        waited += step;
    }
    rc = SCardConnect(p->ctx, p->reader, SCARD_SHARE_SHARED, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &p->card,
                      &p->protocol);
    if (rc != SCARD_S_SUCCESS) {
        fail(err, n, "cannot connect to the card on %s: %s", p->reader, pcsc_msg(rc));
        goto out;
    }
    uint8_t r[64];
    DWORD rl = sizeof r;
    rc = transmit(p, SQS_PCSC_START_SESSION, sizeof SQS_PCSC_START_SESSION, r, &rl);
    if (rc != SCARD_S_SUCCESS || !sw_ok(r, rl)) {
        fail(err, n, "%s does not support PC/SC transparent sessions (start: %s, SW %02X%02X)", p->reader,
             pcsc_msg(rc), rl >= 2 ? r[rl - 2] : 0, rl >= 2 ? r[rl - 1] : 0);
        SCardDisconnect(p->card, SCARD_LEAVE_CARD);
        goto out;
    }
    rl = sizeof r;
    rc = transmit(p, SQS_PCSC_SWITCH_FELICA, sizeof SQS_PCSC_SWITCH_FELICA, r, &rl);
    if (rc != SCARD_S_SUCCESS || !sw_ok(r, rl)) {
        fail(err, n, "%s cannot switch to FeliCa (%s, SW %02X%02X)", p->reader, pcsc_msg(rc),
             rl >= 2 ? r[rl - 2] : 0, rl >= 2 ? r[rl - 1] : 0);
        sqs_pcsc_close(p);
        return NULL;
    }
    return p;
out:
    SCardReleaseContext(p->ctx);
    free(p);
    return NULL;
}

void sqs_pcsc_close(SqsPcsc *p) {
    if (!p) return;
    uint8_t r[64];
    DWORD rl = sizeof r;
    transmit(p, SQS_PCSC_END_SESSION, sizeof SQS_PCSC_END_SESSION, r, &rl);
    SCardDisconnect(p->card, SCARD_LEAVE_CARD);
    SCardReleaseContext(p->ctx);
    free(p);
}

const char *sqs_pcsc_reader_name(const SqsPcsc *p) { return p->reader; }

static int pcsc_exchange(void *ctx, const uint8_t *cmd, size_t len, uint8_t *resp, size_t cap, int timeout_ms) {
    SqsPcsc *p = ctx;
    uint8_t apdu[300], r[300];
    unsigned us = timeout_ms > 0 ? (unsigned)timeout_ms * 1000u : EXCHANGE_TIMEOUT_US;
    int al = sqs_pcsc_wrap(cmd, len, us, apdu, sizeof apdu);
    if (al < 0) return -1;
    DWORD rl = sizeof r;
    LONG rc = transmit(p, apdu, (size_t)al, r, &rl);
    if (rc != SCARD_S_SUCCESS) return -1;
    return sqs_pcsc_unwrap(r, rl, resp, cap);
}

SqsTransport sqs_pcsc_transport(SqsPcsc *p) {
    SqsTransport t = {pcsc_exchange, p};
    return t;
}
