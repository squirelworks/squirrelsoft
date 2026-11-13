/* rsp: the GDB remote serial protocol's packets, as QEMU's gdbstub and a
 * serial stub speak them: "$payload#cs" framing with its escapes and run
 * lengths, acknowledgements, and hex. Original code.
 *
 * Freestanding C11: no allocation, no stdio, nothing from libc but
 * memcpy, memmove, memset and strlen.
 */
#ifndef RSP_H
#define RSP_H

#include <stddef.h>
#include <stdint.h>

#define RSP_MAX 8192 /* the largest payload taken in, once decoded */

/* What a byte from the other side completed */
enum {
    RSP_NONE,   /* nothing yet */
    RSP_ACK,    /* '+' */
    RSP_NAK,    /* '-': send the last packet again */
    RSP_PACKET, /* a whole packet with a good checksum, in buf[0..n) */
    RSP_BADSUM, /* a whole packet whose checksum is wrong (answer '-') */
    RSP_INTR    /* 0x03 outside a packet: the other side asks for a stop */
};

typedef struct {
    int state;  /* 0 between packets, 1 in the payload, 2 and 3 the checksum digits */
    int esc;    /* the last byte was '}' */
    int rle;    /* the last byte was '*' */
    uint8_t sum, got;
    int overflow;
    size_t n;
    uint8_t buf[RSP_MAX + 1]; /* the payload, decoded and with a 0 after it */
} RspRx;

void rsp_rx_init(RspRx *rx);
/* Feed one received byte; returns RSP_* */
int rsp_rx_byte(RspRx *rx, uint8_t c);

/* Frame a payload as "$...#cs", escaping '$', '#', '}' and '*'. Returns
 * the length written (no 0 is added), or 0 when it does not fit in cap. */
size_t rsp_frame(const void *payload, size_t n, char *out, size_t cap);

/* Hex: two lower-case digits a byte; rsp_unhex takes either case and
 * returns the bytes made, stopping at the first byte that is not hex */
void rsp_hex(const uint8_t *in, size_t n, char *out);
size_t rsp_unhex(const char *in, size_t nchars, uint8_t *out, size_t cap);
int rsp_hexval(int c); /* 0-15, or -1 */
/* A hex number at *p, moving *p past it; -1 when there is none */
int rsp_hexnum(const char **p, uint32_t *v);

#endif
