/* x86dis: 16- and 32-bit x86 to NASM-syntax text. Original code, written
 * from the instruction encodings (opcode, ModRM, SIB, displacement,
 * immediate). */
#include "x86dis.h"

#include <string.h>

typedef struct {
    char *p;
    size_t n, cap;
} Sb;

static void sb_c(Sb *b, char c) {
    if (b->n + 1 < b->cap) b->p[b->n++] = c;
    b->p[b->n] = 0;
}
static void sb_s(Sb *b, const char *s) {
    while (*s) sb_c(b, *s++);
}
static void sb_hex(Sb *b, uint32_t v) {
    char t[8];
    int k = 0;
    sb_s(b, "0x");
    do {
        t[k++] = "0123456789abcdef"[v & 15];
        v >>= 4;
    } while (v);
    while (k) sb_c(b, t[--k]);
}
static void sb_disp(Sb *b, int32_t v) { /* "+0x8", "-0x8" */
    if (v < 0) {
        sb_c(b, '-');
        sb_hex(b, (uint32_t)0 - (uint32_t)v);
    } else {
        sb_c(b, '+');
        sb_hex(b, (uint32_t)v);
    }
}

typedef struct {
    const uint8_t *c;
    size_t n, i;
    int bad;
    int bits, osz, asz;
    int seg, segused, rep, repne, repused, lock;
    int mod, reg, rm;
    char mem[64];
    int nops;
    uint32_t ip, base;
    X86Insn *o;
    Sb t;
} Dc;

static uint32_t u8(Dc *d) {
    if (d->i >= d->n) {
        d->bad = 1;
        return 0;
    }
    return d->c[d->i++];
}
static uint32_t u16(Dc *d) {
    uint32_t a = u8(d);
    return a | u8(d) << 8;
}
static uint32_t u32(Dc *d) {
    uint32_t a = u16(d);
    return a | u16(d) << 16;
}

static const char *const R8[8] = {"al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"};
static const char *const R16[8] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};
static const char *const R32[8] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
static const char *const SR[8] = {"es", "cs", "ss", "ds", "fs", "gs", "?", "?"};
static const char *const ALU[8] = {"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"};
static const char *const SH[8] = {"rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar"};
static const char *const G3[8] = {"test", "test", "not", "neg", "mul", "imul", "div", "idiv"};
static const char *const CC[16] = {"o", "no", "b", "ae", "z", "nz", "be", "a", "s", "ns", "p", "np", "l", "ge", "le", "g"};
static const char *const M16[8] = {"bx+si", "bx+di", "bp+si", "bp+di", "si", "di", "bp", "bx"};

static void cc_name(char *m, const char *head, uint32_t op) { /* "j" + "nz" */
    const char *t = CC[op & 15];
    while (*head) *m++ = *head++;
    while (*t) *m++ = *t++;
    *m = 0;
}

static const char *reg(int size, int r) {
    return size == 8 ? R8[r & 7] : size == 16 ? R16[r & 7] : R32[r & 7];
}

static void modrm(Dc *d) {
    uint32_t m = u8(d);
    d->mod = (int)(m >> 6);
    d->reg = (int)(m >> 3 & 7);
    d->rm = (int)(m & 7);
    if (d->mod == 3) return;
    Sb b = {d->mem, 0, sizeof d->mem};
    d->mem[0] = 0;
    sb_c(&b, '[');
    if (d->seg >= 0) {
        sb_s(&b, SR[d->seg]);
        sb_c(&b, ':');
        d->segused = 1;
    }
    if (d->asz == 16) {
        if (d->mod == 0 && d->rm == 6) sb_hex(&b, u16(d));
        else {
            sb_s(&b, M16[d->rm]);
            if (d->mod == 1) {
                int32_t v = (int8_t)u8(d);
                if (v) sb_disp(&b, v);
            } else if (d->mod == 2) {
                int32_t v = (int16_t)u16(d);
                if (v) sb_disp(&b, v);
            }
        }
    } else {
        int base = d->rm, index = -1, scale = 0;
        if (d->rm == 4) {
            uint32_t sib = u8(d);
            scale = (int)(sib >> 6);
            index = (int)(sib >> 3 & 7);
            base = (int)(sib & 7);
            if (index == 4) index = -1;
            if (base == 5 && d->mod == 0) base = -1;
        } else if (d->mod == 0 && d->rm == 5)
            base = -1;
        int32_t disp = 0;
        int hasdisp = 0;
        if (base < 0) {
            disp = (int32_t)u32(d);
            hasdisp = 1;
        } else if (d->mod == 1)
            disp = (int8_t)u8(d);
        else if (d->mod == 2)
            disp = (int32_t)u32(d);
        int any = 0;
        if (base >= 0) {
            sb_s(&b, R32[base]);
            any = 1;
        }
        if (index >= 0) {
            if (any) sb_c(&b, '+');
            sb_s(&b, R32[index]);
            if (scale) {
                sb_c(&b, '*');
                sb_c(&b, (char)('0' + (1 << scale)));
            }
            any = 1;
        }
        if (!any) sb_hex(&b, (uint32_t)disp);
        else if (base < 0 && hasdisp) {
            if (disp) {
                sb_c(&b, '+');
                sb_hex(&b, (uint32_t)disp);
            }
        } else if (disp)
            sb_disp(&b, disp);
    }
    sb_c(&b, ']');
}

static void mn(Dc *d, const char *s) {
    sb_s(&d->t, s);
    d->nops = 0;
}
static void sep(Dc *d) {
    sb_s(&d->t, d->nops++ ? ", " : " ");
}
static void opS(Dc *d, const char *s) {
    sep(d);
    sb_s(&d->t, s);
}
static void opR(Dc *d, int size, int r) { opS(d, reg(size, r)); }
static void opG(Dc *d, int size) { opR(d, size, d->reg); }
static const char *sizename(int size) {
    return size == 8 ? "byte " : size == 16 ? "word " : size == 32 ? "dword " : size == 64 ? "qword " : "";
}
/* The r/m operand: a register, or memory with its size when sized */
static void opE(Dc *d, int size, int sized) {
    if (d->mod == 3) {
        opR(d, size, d->rm);
        return;
    }
    sep(d);
    if (sized) sb_s(&d->t, sizename(size));
    sb_s(&d->t, d->mem);
}
/* Memory only (lea, lgdt...): returns -1 when the r/m names a register */
static int opM(Dc *d) {
    if (d->mod == 3) return -1;
    opE(d, 0, 0);
    return 0;
}
static void opI(Dc *d, uint32_t v) {
    sep(d);
    sb_hex(&d->t, v);
}
static uint32_t immv(Dc *d) { return d->osz == 16 ? u16(d) : u32(d); }
static uint32_t imm8sx(Dc *d) {
    uint32_t v = (uint32_t)(int32_t)(int8_t)u8(d);
    return d->osz == 16 ? v & 0xFFFF : v;
}

/* A relative jump or call of size 8, 16 or 32 */
static void rel(Dc *d, int size, int flow) {
    int32_t r = size == 8 ? (int8_t)u8(d) : size == 16 ? (int16_t)u16(d) : (int32_t)u32(d);
    uint32_t mask = d->osz == 16 ? 0xFFFFu : 0xFFFFFFFFu;
    uint32_t off = (d->ip + (uint32_t)d->i + (uint32_t)r) & mask;
    sep(d);
    sb_hex(&d->t, off);
    d->o->flow = flow;
    d->o->has_target = 1;
    d->o->target = d->base + off;
}

static void far_ptr(Dc *d) { /* Ap: offset, then the selector */
    uint32_t off = immv(d), sel = u16(d);
    sep(d);
    sb_hex(&d->t, sel);
    sb_c(&d->t, ':');
    sb_hex(&d->t, off);
}

static void str_op(Dc *d, const char *name, int size, int cmp) {
    /* movs, cmps and the rest: b, w or d by size; rep, repe, repne */
    char m[12];
    size_t k = 0;
    if (d->asz != d->bits) sb_s(&d->t, d->asz == 16 ? "a16 " : "a32 ");
    if (d->rep || d->repne) {
        sb_s(&d->t, d->repne ? "repne " : cmp ? "repe " : "rep ");
        d->repused = 1;
        d->o->rep = 1;
    }
    while (name[k]) {
        m[k] = name[k];
        k++;
    }
    m[k++] = size == 8 ? 'b' : size == 16 ? 'w' : 'd';
    m[k] = 0;
    mn(d, m);
}

static int unknown(Dc *d) {
    d->bad = 1;
    return -1;
}

/* The 0F page */
static int decode0f(Dc *d) {
    uint32_t op = u8(d);
    int v = d->osz;
    if (op >= 0x80 && op <= 0x8F) {
        char m[8];
        cc_name(m, "j", op);
        mn(d, m);
        rel(d, v, X86_FLOW_COND);
        return 0;
    }
    if (op >= 0x90 && op <= 0x9F) {
        char m[8];
        cc_name(m, "set", op);
        modrm(d);
        mn(d, m);
        opE(d, 8, 1);
        return 0;
    }
    if (op >= 0x40 && op <= 0x4F) {
        char m[8];
        cc_name(m, "cmov", op);
        modrm(d);
        mn(d, m);
        opG(d, v);
        opE(d, v, 0);
        return 0;
    }
    if (op >= 0xC8 && op <= 0xCF) {
        mn(d, "bswap");
        opR(d, 32, (int)(op & 7));
        return 0;
    }
    switch (op) {
    case 0x00: {
        static const char *const G6[8] = {"sldt", "str", "lldt", "ltr", "verr", "verw", 0, 0};
        modrm(d);
        if (!G6[d->reg]) return unknown(d);
        mn(d, G6[d->reg]);
        opE(d, d->reg < 2 && d->mod == 3 ? v : 16, 0); /* sldt and str write a whole register */
        return 0;
    }
    case 0x01: {
        static const char *const G7[8] = {"sgdt", "sidt", "lgdt", "lidt", "smsw", 0, "lmsw", "invlpg"};
        modrm(d);
        if (d->mod == 3) {
            int b = 0xC0 | d->reg << 3 | d->rm;
            const char *m = b == 0xC8 ? "monitor" : b == 0xC9 ? "mwait" : b == 0xD0 ? "xgetbv" : b == 0xD1 ? "xsetbv" : b == 0xF9 ? "rdtscp" : 0;
            if (m) {
                mn(d, m);
                return 0;
            }
            if (d->reg != 4 && d->reg != 6) return unknown(d);
        }
        if (!G7[d->reg]) return unknown(d);
        mn(d, G7[d->reg]);
        if (d->reg == 4) opE(d, d->mod == 3 ? v : 16, 0);
        else if (d->reg == 6) opE(d, 16, 0);
        else opM(d);
        return 0;
    }
    case 0x02: case 0x03:
        modrm(d);
        mn(d, op == 2 ? "lar" : "lsl");
        opG(d, v);
        opE(d, 16, 0);
        return 0;
    case 0x06: mn(d, "clts"); return 0;
    case 0x08: mn(d, "invd"); return 0;
    case 0x09: mn(d, "wbinvd"); return 0;
    case 0x0B: mn(d, "ud2"); d->o->flow = X86_FLOW_STOP; return 0;
    case 0x20: case 0x21: case 0x22: case 0x23: {
        char cr[4] = {op & 1 ? 'd' : 'c', 'r', (char)0, 0};
        modrm(d); /* always a register form, whatever mod says */
        cr[2] = (char)('0' + d->reg);
        mn(d, "mov");
        if (op < 0x22) {
            opR(d, 32, d->rm);
            opS(d, cr);
        } else {
            opS(d, cr);
            opR(d, 32, d->rm);
        }
        return 0;
    }
    case 0x30: mn(d, "wrmsr"); return 0;
    case 0x31: mn(d, "rdtsc"); return 0;
    case 0x32: mn(d, "rdmsr"); return 0;
    case 0x33: mn(d, "rdpmc"); return 0;
    case 0xA0: mn(d, "push"); opS(d, "fs"); return 0;
    case 0xA1: mn(d, "pop"); opS(d, "fs"); return 0;
    case 0xA2: mn(d, "cpuid"); return 0;
    case 0xA8: mn(d, "push"); opS(d, "gs"); return 0;
    case 0xA9: mn(d, "pop"); opS(d, "gs"); return 0;
    case 0xA3: case 0xAB: case 0xB3: case 0xBB:
        modrm(d);
        mn(d, op == 0xA3 ? "bt" : op == 0xAB ? "bts" : op == 0xB3 ? "btr" : "btc");
        opE(d, v, 0);
        opG(d, v);
        return 0;
    case 0xA4: case 0xA5: case 0xAC: case 0xAD:
        modrm(d);
        mn(d, op < 0xA8 ? "shld" : "shrd");
        opE(d, v, 0);
        opG(d, v);
        if (op & 1) opS(d, "cl");
        else opI(d, u8(d));
        return 0;
    case 0xAF:
        modrm(d);
        mn(d, "imul");
        opG(d, v);
        opE(d, v, 0);
        return 0;
    case 0xB0: case 0xB1: case 0xC0: case 0xC1:
        modrm(d);
        mn(d, op < 0xC0 ? "cmpxchg" : "xadd");
        opE(d, op & 1 ? v : 8, 0);
        opG(d, op & 1 ? v : 8);
        return 0;
    case 0xB2: case 0xB4: case 0xB5:
        modrm(d);
        mn(d, op == 0xB2 ? "lss" : op == 0xB4 ? "lfs" : "lgs");
        opG(d, v);
        return opM(d) ? unknown(d) : 0;
    case 0xB6: case 0xB7: case 0xBE: case 0xBF:
        modrm(d);
        mn(d, op < 0xB8 ? "movzx" : "movsx");
        opG(d, v);
        opE(d, op & 1 ? 16 : 8, 1);
        return 0;
    case 0xBA:
        modrm(d);
        if (d->reg < 4) return unknown(d);
        mn(d, d->reg == 4 ? "bt" : d->reg == 5 ? "bts" : d->reg == 6 ? "btr" : "btc");
        opE(d, v, 1);
        opI(d, u8(d));
        return 0;
    case 0xBC: case 0xBD:
        modrm(d);
        mn(d, op == 0xBC ? "bsf" : "bsr");
        opG(d, v);
        opE(d, v, 0);
        return 0;
    case 0xC7:
        modrm(d);
        if (d->reg != 1 || d->mod == 3) return unknown(d);
        mn(d, "cmpxchg8b");
        opE(d, 64, 0);
        return 0;
    }
    return unknown(d);
}

static int decode(Dc *d) {
    uint32_t op;
    for (;;) { /* prefixes */
        if (d->i >= X86_MAXLEN) return unknown(d);
        op = u8(d);
        if (d->bad) return -1;
        switch (op) {
        case 0x26: d->seg = 0; continue;
        case 0x2E: d->seg = 1; continue;
        case 0x36: d->seg = 2; continue;
        case 0x3E: d->seg = 3; continue;
        case 0x64: d->seg = 4; continue;
        case 0x65: d->seg = 5; continue;
        case 0x66: d->osz = d->bits == 16 ? 32 : 16; continue;
        case 0x67: d->asz = d->bits == 16 ? 32 : 16; continue;
        case 0xF0: d->lock = 1; continue;
        case 0xF2: d->repne = 1; d->rep = 0; continue;
        case 0xF3: d->rep = 1; d->repne = 0; continue;
        }
        break;
    }
    int v = d->osz;
    if (op < 0x40 && (op & 7) < 6) { /* add, or, adc, sbb, and, sub, xor, cmp */
        int size = op & 1 ? v : 8;
        mn(d, ALU[op >> 3]);
        switch (op & 7) {
        case 0: case 1: modrm(d); opE(d, size, 0); opG(d, size); break;
        case 2: case 3: modrm(d); opG(d, size); opE(d, size, 0); break;
        case 4: opS(d, "al"); opI(d, u8(d)); break;
        case 5: opR(d, v, 0); opI(d, immv(d)); break;
        }
        return 0;
    }
    if (op >= 0x40 && op <= 0x5F) {
        static const char *const M[4] = {"inc", "dec", "push", "pop"};
        mn(d, M[(op - 0x40) >> 3]);
        opR(d, v, (int)(op & 7));
        return 0;
    }
    if (op >= 0x70 && op <= 0x7F) {
        char m[8];
        cc_name(m, "j", op);
        mn(d, m);
        rel(d, 8, X86_FLOW_COND);
        return 0;
    }
    if (op >= 0x91 && op <= 0x97) {
        mn(d, "xchg");
        opR(d, v, (int)(op & 7));
        opR(d, v, 0);
        return 0;
    }
    if (op >= 0xB0 && op <= 0xB7) {
        mn(d, "mov");
        opR(d, 8, (int)(op & 7));
        opI(d, u8(d));
        return 0;
    }
    if (op >= 0xB8 && op <= 0xBF) {
        mn(d, "mov");
        opR(d, v, (int)(op & 7));
        opI(d, immv(d));
        return 0;
    }
    if (op >= 0xD8 && op <= 0xDF) { /* x87: its length, not its name */
        modrm(d);
        mn(d, "fpu");
        if (d->mod != 3) opE(d, 0, 0);
        return 0;
    }
    switch (op) {
    case 0x06: case 0x0E: case 0x16: case 0x1E:
        mn(d, "push");
        opS(d, SR[op >> 3]);
        return 0;
    case 0x07: case 0x17: case 0x1F:
        mn(d, "pop");
        opS(d, SR[op >> 3]);
        return 0;
    case 0x0F: return decode0f(d);
    case 0x27: mn(d, "daa"); return 0;
    case 0x2F: mn(d, "das"); return 0;
    case 0x37: mn(d, "aaa"); return 0;
    case 0x3F: mn(d, "aas"); return 0;
    case 0x60: mn(d, v == 16 ? "pusha" : "pushad"); return 0;
    case 0x61: mn(d, v == 16 ? "popa" : "popad"); return 0;
    case 0x62:
        modrm(d);
        mn(d, "bound");
        opG(d, v);
        return opM(d) ? unknown(d) : 0;
    case 0x63:
        modrm(d);
        mn(d, "arpl");
        opE(d, 16, 0);
        opG(d, 16);
        return 0;
    case 0x68: mn(d, "push"); opI(d, immv(d)); return 0;
    case 0x6A: mn(d, "push"); opI(d, imm8sx(d)); return 0;
    case 0x69: case 0x6B:
        modrm(d);
        mn(d, "imul");
        opG(d, v);
        opE(d, v, 0);
        opI(d, op == 0x69 ? immv(d) : imm8sx(d));
        return 0;
    case 0x6C: str_op(d, "ins", 8, 0); return 0;
    case 0x6D: str_op(d, "ins", v, 0); return 0;
    case 0x6E: str_op(d, "outs", 8, 0); return 0;
    case 0x6F: str_op(d, "outs", v, 0); return 0;
    case 0x80: case 0x81: case 0x82: case 0x83: {
        int size = op == 0x81 || op == 0x83 ? v : 8;
        modrm(d);
        mn(d, ALU[d->reg]);
        opE(d, size, 1);
        opI(d, op == 0x81 ? immv(d) : op == 0x83 ? imm8sx(d) : u8(d));
        return 0;
    }
    case 0x84: case 0x85: case 0x86: case 0x87:
        modrm(d);
        mn(d, op < 0x86 ? "test" : "xchg");
        opE(d, op & 1 ? v : 8, 0);
        opG(d, op & 1 ? v : 8);
        return 0;
    case 0x88: case 0x89:
        modrm(d);
        mn(d, "mov");
        opE(d, op & 1 ? v : 8, 0);
        opG(d, op & 1 ? v : 8);
        return 0;
    case 0x8A: case 0x8B:
        modrm(d);
        mn(d, "mov");
        opG(d, op & 1 ? v : 8);
        opE(d, op & 1 ? v : 8, 0);
        return 0;
    case 0x8C:
        modrm(d);
        if (d->reg > 5) return unknown(d);
        mn(d, "mov");
        opE(d, d->mod == 3 ? v : 16, 0);
        opS(d, SR[d->reg]);
        return 0;
    case 0x8D:
        modrm(d);
        mn(d, "lea");
        opG(d, v);
        return opM(d) ? unknown(d) : 0;
    case 0x8E:
        modrm(d);
        if (d->reg > 5 || d->reg == 1) return unknown(d);
        mn(d, "mov");
        opS(d, SR[d->reg]);
        opE(d, 16, 0);
        return 0;
    case 0x8F:
        modrm(d);
        if (d->reg) return unknown(d);
        mn(d, "pop");
        opE(d, v, 1);
        return 0;
    case 0x90:
        if (d->rep) {
            d->repused = 1;
            mn(d, "pause");
        } else
            mn(d, "nop");
        return 0;
    case 0x98: mn(d, v == 16 ? "cbw" : "cwde"); return 0;
    case 0x99: mn(d, v == 16 ? "cwd" : "cdq"); return 0;
    case 0x9A:
        mn(d, "call");
        far_ptr(d);
        d->o->flow = X86_FLOW_CALL;
        return 0;
    case 0x9B: mn(d, "wait"); return 0;
    case 0x9C: mn(d, v == 16 ? "pushf" : "pushfd"); return 0;
    case 0x9D: mn(d, v == 16 ? "popf" : "popfd"); return 0;
    case 0x9E: mn(d, "sahf"); return 0;
    case 0x9F: mn(d, "lahf"); return 0;
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: {
        int size = op & 1 ? v : 8;
        Sb b = {d->mem, 0, sizeof d->mem};
        sb_c(&b, '[');
        if (d->seg >= 0) {
            sb_s(&b, SR[d->seg]);
            sb_c(&b, ':');
            d->segused = 1;
        }
        sb_hex(&b, d->asz == 16 ? u16(d) : u32(d));
        sb_c(&b, ']');
        d->mod = 0;
        mn(d, "mov");
        if (op < 0xA2) {
            opR(d, size, 0);
            opE(d, size, 0);
        } else {
            opE(d, size, 0);
            opR(d, size, 0);
        }
        return 0;
    }
    case 0xA4: str_op(d, "movs", 8, 0); return 0;
    case 0xA5: str_op(d, "movs", v, 0); return 0;
    case 0xA6: str_op(d, "cmps", 8, 1); return 0;
    case 0xA7: str_op(d, "cmps", v, 1); return 0;
    case 0xA8: mn(d, "test"); opS(d, "al"); opI(d, u8(d)); return 0;
    case 0xA9: mn(d, "test"); opR(d, v, 0); opI(d, immv(d)); return 0;
    case 0xAA: str_op(d, "stos", 8, 0); return 0;
    case 0xAB: str_op(d, "stos", v, 0); return 0;
    case 0xAC: str_op(d, "lods", 8, 0); return 0;
    case 0xAD: str_op(d, "lods", v, 0); return 0;
    case 0xAE: str_op(d, "scas", 8, 1); return 0;
    case 0xAF: str_op(d, "scas", v, 1); return 0;
    case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
        int size = op & 1 ? v : 8;
        modrm(d);
        mn(d, SH[d->reg]);
        opE(d, size, 1);
        if (op < 0xD0) opI(d, u8(d));
        else opS(d, op < 0xD2 ? "1" : "cl");
        return 0;
    }
    case 0xC2: mn(d, "ret"); opI(d, u16(d)); d->o->flow = X86_FLOW_RET; return 0;
    case 0xC3: mn(d, "ret"); d->o->flow = X86_FLOW_RET; return 0;
    case 0xC4: case 0xC5:
        modrm(d);
        mn(d, op == 0xC4 ? "les" : "lds");
        opG(d, v);
        return opM(d) ? unknown(d) : 0;
    case 0xC6: case 0xC7: {
        int size = op & 1 ? v : 8;
        modrm(d);
        if (d->reg) return unknown(d);
        mn(d, "mov");
        opE(d, size, 1);
        opI(d, size == 8 ? u8(d) : immv(d));
        return 0;
    }
    case 0xC8: {
        uint32_t a = u16(d);
        mn(d, "enter");
        opI(d, a);
        opI(d, u8(d));
        return 0;
    }
    case 0xC9: mn(d, "leave"); return 0;
    case 0xCA: mn(d, "retf"); opI(d, u16(d)); d->o->flow = X86_FLOW_RET; return 0;
    case 0xCB: mn(d, "retf"); d->o->flow = X86_FLOW_RET; return 0;
    case 0xCC: mn(d, "int3"); d->o->flow = X86_FLOW_CALL; return 0;
    case 0xCD: mn(d, "int"); opI(d, u8(d)); d->o->flow = X86_FLOW_CALL; return 0;
    case 0xCE: mn(d, "into"); d->o->flow = X86_FLOW_CALL; return 0;
    case 0xCF: mn(d, v == 16 ? "iret" : "iretd"); d->o->flow = X86_FLOW_RET; return 0;
    case 0xD4: case 0xD5: {
        uint32_t b = u8(d);
        mn(d, op == 0xD4 ? "aam" : "aad");
        if (b != 10) opI(d, b);
        return 0;
    }
    case 0xD6: mn(d, "salc"); return 0;
    case 0xD7: mn(d, "xlatb"); return 0;
    case 0xE0: case 0xE1: case 0xE2: case 0xE3:
        if (d->asz != d->bits && op != 0xE3) sb_s(&d->t, d->asz == 16 ? "a16 " : "a32 ");
        mn(d, op == 0xE0 ? "loopne" : op == 0xE1 ? "loope" : op == 0xE2 ? "loop" : d->asz == 16 ? "jcxz" : "jecxz");
        rel(d, 8, X86_FLOW_COND);
        return 0;
    case 0xE4: mn(d, "in"); opS(d, "al"); opI(d, u8(d)); return 0;
    case 0xE5: mn(d, "in"); opR(d, v, 0); opI(d, u8(d)); return 0;
    case 0xE6: mn(d, "out"); opI(d, u8(d)); opS(d, "al"); return 0;
    case 0xE7: mn(d, "out"); opI(d, u8(d)); opR(d, v, 0); return 0;
    case 0xE8: mn(d, "call"); rel(d, v, X86_FLOW_CALL); return 0;
    case 0xE9: mn(d, "jmp"); rel(d, v, X86_FLOW_JUMP); return 0;
    case 0xEA: mn(d, "jmp"); far_ptr(d); d->o->flow = X86_FLOW_JUMP; return 0;
    case 0xEB: mn(d, "jmp"); rel(d, 8, X86_FLOW_JUMP); return 0;
    case 0xEC: mn(d, "in"); opS(d, "al"); opS(d, "dx"); return 0;
    case 0xED: mn(d, "in"); opR(d, v, 0); opS(d, "dx"); return 0;
    case 0xEE: mn(d, "out"); opS(d, "dx"); opS(d, "al"); return 0;
    case 0xEF: mn(d, "out"); opS(d, "dx"); opR(d, v, 0); return 0;
    case 0xF1: mn(d, "int1"); d->o->flow = X86_FLOW_CALL; return 0;
    case 0xF4: mn(d, "hlt"); d->o->flow = X86_FLOW_STOP; return 0;
    case 0xF5: mn(d, "cmc"); return 0;
    case 0xF6: case 0xF7: {
        int size = op & 1 ? v : 8;
        modrm(d);
        mn(d, G3[d->reg]);
        opE(d, size, 1);
        if (d->reg < 2) opI(d, size == 8 ? u8(d) : immv(d));
        return 0;
    }
    case 0xF8: mn(d, "clc"); return 0;
    case 0xF9: mn(d, "stc"); return 0;
    case 0xFA: mn(d, "cli"); return 0;
    case 0xFB: mn(d, "sti"); return 0;
    case 0xFC: mn(d, "cld"); return 0;
    case 0xFD: mn(d, "std"); return 0;
    case 0xFE:
        modrm(d);
        if (d->reg > 1) return unknown(d);
        mn(d, d->reg ? "dec" : "inc");
        opE(d, 8, 1);
        return 0;
    case 0xFF:
        modrm(d);
        switch (d->reg) {
        case 0: mn(d, "inc"); opE(d, v, 1); return 0;
        case 1: mn(d, "dec"); opE(d, v, 1); return 0;
        case 2: mn(d, "call"); opE(d, v, 1); d->o->flow = X86_FLOW_CALL; return 0;
        case 3: case 5:
            if (d->mod == 3) return unknown(d);
            mn(d, d->reg == 3 ? "call far" : "jmp far");
            opM(d);
            d->o->flow = d->reg == 3 ? X86_FLOW_CALL : X86_FLOW_JUMP;
            return 0;
        case 4: mn(d, "jmp"); opE(d, v, 1); d->o->flow = X86_FLOW_JUMP; return 0;
        case 6: mn(d, "push"); opE(d, v, 1); return 0;
        }
        return unknown(d);
    }
    return unknown(d);
}

int x86_disasm(const uint8_t *code, size_t n, uint32_t ip, uint32_t base, int bits, X86Insn *out) {
    char body[72];
    Dc d;
    memset(&d, 0, sizeof d);
    memset(out, 0, sizeof *out);
    d.c = code;
    d.n = n < X86_MAXLEN ? n : X86_MAXLEN;
    d.bits = d.osz = d.asz = bits == 16 ? 16 : 32;
    d.seg = -1;
    d.ip = ip;
    d.base = base;
    d.o = out;
    d.t.p = body;
    d.t.cap = sizeof body;
    body[0] = 0;
    if (decode(&d) < 0 || d.bad || d.i > X86_MAXLEN) {
        Sb b = {out->text, 0, sizeof out->text};
        memset(out, 0, sizeof *out);
        out->text[0] = 0;
        sb_s(&b, "db ");
        sb_hex(&b, n ? code[0] : 0);
        out->len = 1;
        return 1;
    }
    Sb b = {out->text, 0, sizeof out->text};
    out->text[0] = 0;
    if (d.lock) sb_s(&b, "lock ");
    if ((d.rep || d.repne) && !d.repused) sb_s(&b, d.repne ? "repne " : "rep ");
    if (d.seg >= 0 && !d.segused) {
        sb_s(&b, SR[d.seg]);
        sb_c(&b, ' ');
    }
    sb_s(&b, body);
    out->len = (int)d.i;
    return out->len;
}
