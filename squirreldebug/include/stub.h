/* stub: the practice machine. A small 32-bit x86 machine (64K of memory,
 * the few instructions its program needs) with a GDB stub in front of
 * it, so SquirrelDebug can be tried and tested with no QEMU and no serial
 * line. Its program prints a greeting to port 0xE9 (which the stub sends
 * as console output, as QEMU's debug console would), then counts and
 * squares numbers for ever. Original code.
 *
 * The stub answers the packets SquirrelDebug sends, including QEMU's
 * "monitor info registers", so the same paths run as against QEMU. For
 * tests it can refuse packets ('-') and compress its answers.
 *
 * Freestanding C11: no allocation, no stdio, nothing from libc but
 * memcpy, memmove, memset and strlen.
 */
#ifndef STUB_H
#define STUB_H

#include <stddef.h>
#include <stdint.h>

#include "dbg.h"
#include "rsp.h"

#define STUB_MEM 0x10000
#define STUB_ENTRY 0x1000

typedef struct {
    uint32_t reg[DBG_NREGS];
    uint8_t mem[STUB_MEM];
    RspRx rx;
    uint8_t out[3 * RSP_MAX];   /* bytes waiting to go to the debugger */
    size_t nout, outpos;
    int running, noack, gone;
    uint32_t bp[64];
    int nbp;
    char con[128];              /* what the program printed, not sent yet */
    size_t ncon;
    long budget;                /* instructions run each time the debugger reads */
    unsigned long executed;
    int nak;                    /* refuse this many packets with '-' (tests) */
    int rle;                    /* compress runs in answers (tests) */
    int no_p;                   /* answer 'P' as not supported, so 'G' is used (tests) */
    long packets;               /* packets taken */
} Stub;

void stub_init(Stub *s);
DbgIo stub_io(Stub *s);
/* The program's symbols, as nm prints them */
const char *stub_symbols(void);

#endif
