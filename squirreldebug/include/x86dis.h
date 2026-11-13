/* x86dis: a disassembler for 16- and 32-bit x86 code, the integer and
 * system instructions an operating system kernel and its boot code use,
 * written out in NASM's syntax ("mov eax, [ebx+esi*4+0x10]"). Original
 * code.
 *
 * x87 instructions come out as "fpu" with their right length; anything
 * else it does not know comes out as "db 0xNN", one byte long.
 *
 * Freestanding C11: no allocation, no stdio, nothing from libc but
 * memcpy, memmove, memset and strlen.
 */
#ifndef X86DIS_H
#define X86DIS_H

#include <stddef.h>
#include <stdint.h>

#define X86_MAXLEN 15

enum {
    X86_FLOW_NONE,
    X86_FLOW_JUMP,  /* jmp */
    X86_FLOW_COND,  /* jcc, loop, jcxz */
    X86_FLOW_CALL,  /* call, int, into */
    X86_FLOW_RET,   /* ret, retf, iret */
    X86_FLOW_STOP   /* hlt, ud2 */
};

typedef struct {
    int len;          /* bytes, 1 to 15 */
    int flow;         /* X86_FLOW_* */
    int has_target;   /* a direct jump or call: target holds where to */
    uint32_t target;  /* base + the target's offset */
    int rep;          /* a string instruction with rep: stepping over it runs the whole repeat */
    char text[72];
} X86Insn;

/* One instruction at code[0..n). ip is its offset in its code segment (what
 * the text shows targets in, wrapping at 64K in 16-bit code) and base the
 * segment's linear base (added to targets in X86Insn.target). bits is 16
 * or 32. Returns the length; when code runs out before the instruction
 * does, a one-byte "db". */
int x86_disasm(const uint8_t *code, size_t n, uint32_t ip, uint32_t base, int bits, X86Insn *out);

#endif
