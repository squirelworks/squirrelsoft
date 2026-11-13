/* rsp: GDB remote serial protocol packets. Original code. */
#include "rsp.h"

#include <string.h>

void rsp_rx_init(RspRx *rx) { memset(rx, 0, sizeof *rx); }

int rsp_hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void put(RspRx *rx, uint8_t c) {
    if (rx->n < RSP_MAX) rx->buf[rx->n++] = c;
    else rx->overflow = 1;
}

int rsp_rx_byte(RspRx *rx, uint8_t c) {
    switch (rx->state) {
    case 0:
        if (c == '+') return RSP_ACK;
        if (c == '-') return RSP_NAK;
        if (c == 0x03) return RSP_INTR;
        if (c == '$') {
            rx->state = 1;
            rx->n = 0;
            rx->sum = 0;
            rx->esc = rx->rle = rx->overflow = 0;
        }
        return RSP_NONE; /* anything else between packets is noise */
    case 1:
        if (c == '$') { /* a new packet before this one ended: start over */
            rx->n = 0;
            rx->sum = 0;
            rx->esc = rx->rle = rx->overflow = 0;
            return RSP_NONE;
        }
        if (c == '#' && !rx->esc && !rx->rle) {
            rx->state = 2;
            return RSP_NONE;
        }
        rx->sum = (uint8_t)(rx->sum + c);
        if (rx->rle) { /* '*' then a count byte: the byte before, n - 29 more times */
            rx->rle = 0;
            int k = (int)c - 29;
            if (rx->n == 0 || k < 0) rx->overflow = 1;
            else
                for (uint8_t prev = rx->buf[rx->n - 1]; k > 0; k--) put(rx, prev);
        } else if (rx->esc) {
            rx->esc = 0;
            put(rx, (uint8_t)(c ^ 0x20));
        } else if (c == '}')
            rx->esc = 1;
        else if (c == '*')
            rx->rle = 1;
        else
            put(rx, c);
        return RSP_NONE;
    case 2:
    case 3: {
        int v = rsp_hexval(c);
        if (v < 0) { /* not a checksum: drop the packet */
            rx->state = 0;
            return RSP_BADSUM;
        }
        rx->got = (uint8_t)(rx->state == 2 ? v << 4 : rx->got | v);
        if (rx->state == 2) {
            rx->state = 3;
            return RSP_NONE;
        }
        rx->state = 0;
        rx->buf[rx->n] = 0;
        return rx->got == rx->sum && !rx->overflow ? RSP_PACKET : RSP_BADSUM;
    }
    }
    rx->state = 0;
    return RSP_NONE;
}

static const char HEX[] = "0123456789abcdef";

size_t rsp_frame(const void *payload, size_t n, char *out, size_t cap) {
    const uint8_t *p = payload;
    size_t o = 0;
    uint8_t sum = 0;
    if (cap < 4) return 0;
    out[o++] = '$';
    for (size_t i = 0; i < n; i++) {
        uint8_t c = p[i];
        int esc = c == '$' || c == '#' || c == '}' || c == '*';
        if (o + (esc ? 2 : 1) + 3 > cap) return 0;
        if (esc) {
            out[o++] = '}';
            sum = (uint8_t)(sum + '}');
            c ^= 0x20;
        }
        out[o++] = (char)c;
        sum = (uint8_t)(sum + c);
    }
    out[o++] = '#';
    out[o++] = HEX[sum >> 4];
    out[o++] = HEX[sum & 15];
    return o;
}

void rsp_hex(const uint8_t *in, size_t n, char *out) {
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = HEX[in[i] >> 4];
        out[2 * i + 1] = HEX[in[i] & 15];
    }
}

size_t rsp_unhex(const char *in, size_t nchars, uint8_t *out, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; i + 1 < nchars && k < cap; i += 2) {
        int a = rsp_hexval((unsigned char)in[i]), b = rsp_hexval((unsigned char)in[i + 1]);
        if (a < 0 || b < 0) break;
        out[k++] = (uint8_t)(a << 4 | b);
    }
    return k;
}

int rsp_hexnum(const char **p, uint32_t *v) {
    const char *s = *p;
    uint32_t x = 0;
    int d, any = 0;
    while ((d = rsp_hexval((unsigned char)*s)) >= 0) {
        x = x << 4 | (uint32_t)d;
        s++;
        any = 1;
    }
    if (!any) return -1;
    *p = s;
    *v = x;
    return 0;
}
