/* dbg: SquirrelDebug's debugger, as a session with a target over the GDB
 * remote serial protocol: QEMU's gdbstub (-gdb tcp::1234), a stub on a
 * serial line, or the practice machine in stub.h. It reads and writes
 * registers and memory, sets breakpoints, steps into or over, runs and
 * stops, tells 16-bit code from 32-bit (from QEMU's monitor when it has
 * one), names addresses from a symbol list, and takes text commands.
 * Original code.
 *
 * The target is i386: the sixteen registers of GDB's i386 layout. Bytes
 * come and go through the two functions in DbgIo, so the session itself
 * knows nothing of sockets or serial ports.
 *
 * Freestanding C11: no allocation, no stdio, nothing from libc but
 * memcpy, memmove, memset and strlen.
 */
#ifndef DBG_H
#define DBG_H

#include <stddef.h>
#include <stdint.h>

#include "rsp.h"
#include "x86dis.h"

typedef struct {
    void *ctx;
    /* bytes written, or -1 */
    long (*write)(void *ctx, const void *p, size_t n);
    /* bytes read; 0 when nothing came within ms; -1 when the link is gone */
    long (*read)(void *ctx, void *p, size_t n, int ms);
} DbgIo;

enum { DBG_OFF, DBG_STOPPED, DBG_RUNNING, DBG_EXITED };

enum {
    DBG_EAX, DBG_ECX, DBG_EDX, DBG_EBX, DBG_ESP, DBG_EBP, DBG_ESI, DBG_EDI,
    DBG_EIP, DBG_EFLAGS, DBG_CS, DBG_SS, DBG_DS, DBG_ES, DBG_FS, DBG_GS,
    DBG_NREGS
};
extern const char *const DBG_REG_NAMES[DBG_NREGS]; /* "eax" ... "gs" */

#define DBG_BPS 64
#define DBG_SYMS 8192
#define DBG_POOL (160 * 1024)
#define DBG_OUT 4096
#define DBG_G 2400

typedef struct {
    uint32_t addr;
    uint32_t name; /* offset into pool */
} DbgSym;

typedef struct {
    DbgIo io;
    RspRx rx;
    uint8_t in[512];
    size_t inpos, inlen;
    int state;              /* DBG_* */
    int sig;                /* the signal of the last stop (5: a trap, a step or a breakpoint) */
    int code;               /* the exit status once DBG_EXITED */
    uint32_t reg[DBG_NREGS];
    uint32_t prev[DBG_NREGS]; /* before the last step or run, to show what changed */
    char g[DBG_G];          /* the last 'g' answer, all of it, for 'G' */
    size_t ng;
    int bits;               /* the code at the pc: 16 or 32 */
    int bits_forced;        /* 0: worked out; 16 or 32: set by "mode" */
    uint32_t cs_base;       /* CS's linear base (from QEMU, or CS*16 in real mode) */
    uint32_t cr0, cr3;
    int cr_known, monitor;  /* the target answers "monitor info registers" like QEMU */
    int noack;              /* QStartNoAckMode taken */
    long timeout_ms;        /* how long to wait for an answer (default 3000) */
    uint32_t bp[DBG_BPS];
    int nbp;
    int temp;               /* a breakpoint set by next or until, taken out at the next stop */
    uint32_t temp_addr;
    unsigned gen;           /* goes up whenever registers or memory may have changed */
    long stops;
    DbgSym sym[DBG_SYMS];
    int nsym;
    char pool[DBG_POOL];
    size_t npool;
    char out[DBG_OUT];      /* what the target printed (its 'O' packets), oldest dropped */
    size_t nout;
    unsigned long out_total; /* bytes ever added to out */
    char err[128];
} Dbg;

void dbg_init(Dbg *d, DbgIo io);
/* Take over a target: what it supports, why it stopped, its registers.
 * 0, or -1 with d->err */
int dbg_attach(Dbg *d);

/* One packet out and its answer back (0-terminated). The answer's length,
 * or -1 with d->err. */
long dbg_request(Dbg *d, const char *payload, size_t n, char *reply, size_t cap);

int dbg_refresh(Dbg *d);                       /* registers, and the mode of the code */
uint32_t dbg_pc(const Dbg *d);                 /* the linear address of the next instruction */
long dbg_read(Dbg *d, uint32_t addr, uint8_t *buf, size_t n);         /* bytes read, or -1 */
int dbg_write(Dbg *d, uint32_t addr, const uint8_t *buf, size_t n);   /* 0, or -1 */
int dbg_set_reg(Dbg *d, int r, uint32_t v);

int dbg_break(Dbg *d, uint32_t addr);   /* 0, or -1 */
int dbg_unbreak(Dbg *d, uint32_t addr);
int dbg_is_break(const Dbg *d, uint32_t addr);

int dbg_step(Dbg *d);                   /* one instruction; waits for it */
int dbg_next(Dbg *d);                   /* over a call, int or rep: runs to the instruction after it */
int dbg_until(Dbg *d, uint32_t addr);   /* runs to addr (or a breakpoint) */
int dbg_continue(Dbg *d);               /* runs; dbg_poll sees it stop */
int dbg_stop(Dbg *d);                   /* stops a running target and waits for it */
/* While running: takes what came within ms; returns d->state */
int dbg_poll(Dbg *d, int ms);
/* Polls until stopped or ms pass; the state */
int dbg_wait(Dbg *d, long ms);
int dbg_detach(Dbg *d);
int dbg_kill(Dbg *d);

/* A QEMU monitor command ("info registers"); its text into out. The length,
 * or -1 */
long dbg_monitor(Dbg *d, const char *cmd, char *out, size_t cap);

/* Symbols: lines of "ADDRESS [TYPE] NAME" as nm prints them. Returns how
 * many were taken. */
int dbg_load_symbols(Dbg *d, const char *text, size_t n);
const char *dbg_sym_at(const Dbg *d, uint32_t addr, uint32_t *off); /* the nearest at or below, within 64K, or NULL */
int dbg_sym_find(const Dbg *d, const char *name, size_t n, uint32_t *addr);

/* An address or value: hex numbers (0x optional), registers, pc, symbols,
 * SEG:OFF (SEG*16+OFF), joined by + and -. 0, or -1. */
int dbg_eval(const Dbg *d, const char *s, uint32_t *v);

/* "0x00007c00 <start+0x4>" */
size_t dbg_addr_text(const Dbg *d, uint32_t addr, char *out, size_t cap);
/* One instruction at addr as a line ("0x00007c00  b8 00 00   mov ax, 0x0").
 * Its length, or 0 when the memory could not be read. */
int dbg_disasm_line(Dbg *d, uint32_t addr, char *out, size_t cap);
/* The same from bytes already read (code[0..n) at addr); in gets the
 * instruction when not NULL */
int dbg_disasm_at(const Dbg *d, uint32_t addr, const uint8_t *code, size_t n, char *out, size_t cap, X86Insn *in);
/* The registers as text, several lines */
size_t dbg_regs_text(const Dbg *d, char *out, size_t cap);
/* "stopped at ...", "running", "exited (0)", "not connected" */
size_t dbg_status_text(Dbg *d, char *out, size_t cap);

/* A command line:
 *   regs   x ADDR [N]   dis [ADDR] [N]   break ADDR   delete ADDR|all
 *   breaks   step [N]   next   until ADDR   continue   stop
 *   set REG VALUE   write ADDR BYTE...   mode 16|32|auto   mon TEXT
 *   sym NAME|ADDR   detach   kill
 * Answers into out. 0, or -1 with why in out. */
int dbg_command(Dbg *d, const char *line, char *out, size_t cap);

#endif
