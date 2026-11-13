/* stub: the practice machine and its GDB stub. Original code. */
#include "stub.h"

#include <string.h>

/* The program, assembled at 0x1000:
 *   start:  mov esp, 0x8000 / mov ebx, msg / call print / xor ecx, ecx
 *   again:  inc ecx / mov [counter], ecx / call square / cmp ecx, 0x400 / jnz again
 *           xor ecx, ecx / mov ebx, lap / call print / jmp again
 *   square: mov eax, ecx / imul eax, ecx / mov [result], eax / ret
 *   print:  mov al, [ebx] / cmp al, 0 / jz .done / out 0xe9, al / inc ebx / jmp print
 *   .done:  ret
 *   msg:    "Hello from the practice machine\n"   lap: "1024 squares done\n" */
static const uint8_t PROGRAM[] = {
    0xbc, 0x00, 0x80, 0x00, 0x00, 0xbb, 0x4a, 0x10, 0x00, 0x00, 0xe8, 0x2f, 0x00, 0x00, 0x00, 0x31,
    0xc9, 0x41, 0x89, 0x0d, 0x00, 0x20, 0x00, 0x00, 0xe8, 0x16, 0x00, 0x00, 0x00, 0x81, 0xf9, 0x00,
    0x04, 0x00, 0x00, 0x75, 0xec, 0x31, 0xc9, 0xbb, 0x6b, 0x10, 0x00, 0x00, 0xe8, 0x0d, 0x00, 0x00,
    0x00, 0xeb, 0xde, 0x89, 0xc8, 0x0f, 0xaf, 0xc1, 0xa3, 0x04, 0x20, 0x00, 0x00, 0xc3, 0x8a, 0x03,
    0x3c, 0x00, 0x74, 0x05, 0xe6, 0xe9, 0x43, 0xeb, 0xf5, 0xc3, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20,
    0x66, 0x72, 0x6f, 0x6d, 0x20, 0x74, 0x68, 0x65, 0x20, 0x70, 0x72, 0x61, 0x63, 0x74, 0x69, 0x63,
    0x65, 0x20, 0x6d, 0x61, 0x63, 0x68, 0x69, 0x6e, 0x65, 0x0a, 0x00, 0x31, 0x30, 0x32, 0x34, 0x20,
    0x73, 0x71, 0x75, 0x61, 0x72, 0x65, 0x73, 0x20, 0x64, 0x6f, 0x6e, 0x65, 0x0a, 0x00,
};

static const char SYMBOLS[] = "00001000 T start\n"
                              "00001011 t again\n"
                              "00001033 T square\n"
                              "0000103e T print\n"
                              "00001049 t print.done\n"
                              "0000104a r msg\n"
                              "0000106b r lap\n"
                              "00002000 B counter\n"
                              "00002004 B result\n";

const char *stub_symbols(void) { return SYMBOLS; }

#define EFL_CF 0x001u
#define EFL_ZF 0x040u
#define EFL_SF 0x080u
#define EFL_OF 0x800u

void stub_init(Stub *s) {
    memset(s, 0, sizeof *s);
    memcpy(s->mem + STUB_ENTRY, PROGRAM, sizeof PROGRAM);
    s->reg[DBG_EIP] = STUB_ENTRY;
    s->reg[DBG_EFLAGS] = 0x2;
    s->reg[DBG_CS] = 0x08;
    s->reg[DBG_SS] = s->reg[DBG_DS] = s->reg[DBG_ES] = 0x10;
    s->budget = 50;
    rsp_rx_init(&s->rx);
}

/* ---- the machine ---- */

static uint8_t rd8(Stub *s, uint32_t a) { return s->mem[a & (STUB_MEM - 1)]; }
static uint32_t rd32(Stub *s, uint32_t a) {
    return (uint32_t)rd8(s, a) | (uint32_t)rd8(s, a + 1) << 8 | (uint32_t)rd8(s, a + 2) << 16 | (uint32_t)rd8(s, a + 3) << 24;
}
static void wr32(Stub *s, uint32_t a, uint32_t v) {
    for (int i = 0; i < 4; i++) s->mem[(a + (uint32_t)i) & (STUB_MEM - 1)] = (uint8_t)(v >> 8 * i);
}

static void flags_of(Stub *s, uint32_t r, int carry) {
    uint32_t f = s->reg[DBG_EFLAGS] & ~(EFL_CF | EFL_ZF | EFL_SF | EFL_OF);
    if (!r) f |= EFL_ZF;
    if (r & 0x80000000u) f |= EFL_SF;
    if (carry) f |= EFL_CF;
    s->reg[DBG_EFLAGS] = f;
}

/* r/m for mod 0-2 without a SIB; the address, moving *ip past it. -1 for a register form. */
static int ea(Stub *s, uint32_t *ip, uint8_t m, uint32_t *addr) {
    int mod = m >> 6, rm = m & 7;
    if (mod == 3 || rm == 4) return -1;
    if (mod == 0 && rm == 5) {
        *addr = rd32(s, *ip);
        *ip += 4;
        return 0;
    }
    uint32_t a = s->reg[rm];
    if (mod == 1) a += (uint32_t)(int32_t)(int8_t)rd8(s, (*ip)++);
    else if (mod == 2) {
        a += rd32(s, *ip);
        *ip += 4;
    }
    *addr = a;
    return 0;
}

static void console(Stub *s, uint8_t c);

/* One instruction: 0, or the signal it stops with (4 for one it does not know) */
static int exec1(Stub *s) {
    uint32_t ip = s->reg[DBG_EIP];
    uint8_t op = rd8(s, ip++), m;
    uint32_t a, v;
    int cond;
    s->executed++;
    if (op >= 0xB8 && op <= 0xBF) {
        s->reg[op - 0xB8] = rd32(s, ip);
        ip += 4;
    } else if (op >= 0x40 && op <= 0x47) {
        v = ++s->reg[op - 0x40];
        flags_of(s, v, (int)(s->reg[DBG_EFLAGS] & EFL_CF));
    } else if (op >= 0x48 && op <= 0x4F) {
        v = --s->reg[op - 0x48];
        flags_of(s, v, (int)(s->reg[DBG_EFLAGS] & EFL_CF));
    } else if (op >= 0x50 && op <= 0x57) {
        v = s->reg[op - 0x50];
        s->reg[DBG_ESP] -= 4;
        wr32(s, s->reg[DBG_ESP], v);
    } else if (op >= 0x58 && op <= 0x5F) {
        s->reg[op - 0x58] = rd32(s, s->reg[DBG_ESP]);
        s->reg[DBG_ESP] += 4;
    } else
        switch (op) {
        case 0x90: break;
        case 0xF4: ip--; break; /* hlt: waits here for ever */
        case 0xCC: s->reg[DBG_EIP] = ip; return 5;
        case 0x31: /* xor r/m32, r32 (registers) */
        case 0x89: /* mov r/m32, r32 */
        case 0x8B: /* mov r32, r/m32 */
        case 0x8A: /* mov r8, r/m8 (al..bl) */
            m = rd8(s, ip++);
            if (ea(s, &ip, m, &a) < 0) {
                if ((m >> 6) != 3) return 4;
                int dst = op == 0x8B || op == 0x8A ? m >> 3 & 7 : m & 7, src = op == 0x8B || op == 0x8A ? m & 7 : m >> 3 & 7;
                if (op == 0x31) {
                    s->reg[dst] ^= s->reg[src];
                    flags_of(s, s->reg[dst], 0);
                } else if (op == 0x8A) {
                    if (dst > 3 || src > 3) return 4;
                    s->reg[dst] = (s->reg[dst] & ~0xFFu) | (s->reg[src] & 0xFF);
                } else
                    s->reg[dst] = s->reg[src];
            } else if (op == 0x89)
                wr32(s, a, s->reg[m >> 3 & 7]);
            else if (op == 0x8B)
                s->reg[m >> 3 & 7] = rd32(s, a);
            else if (op == 0x8A && (m >> 3 & 7) < 4)
                s->reg[m >> 3 & 7] = (s->reg[m >> 3 & 7] & ~0xFFu) | rd8(s, a);
            else
                return 4;
            break;
        case 0x81: /* add, sub, cmp r32, imm32 */
        case 0x83: /* and imm8 */
            m = rd8(s, ip++);
            if ((m >> 6) != 3) return 4;
            if (op == 0x81) {
                v = rd32(s, ip);
                ip += 4;
            } else
                v = (uint32_t)(int32_t)(int8_t)rd8(s, ip++);
            {
                uint32_t *r = &s->reg[m & 7], x = *r;
                switch (m >> 3 & 7) {
                case 0: *r = x + v; flags_of(s, *r, *r < x); break;
                case 5: *r = x - v; flags_of(s, *r, x < v); break;
                case 7: flags_of(s, x - v, x < v); break;
                default: return 4;
                }
            }
            break;
        case 0x3C: /* cmp al, imm8 */
            v = rd8(s, ip++);
            a = s->reg[DBG_EAX] & 0xFF;
            flags_of(s, (a - v) << 24, a < v);
            break;
        case 0x0F:
            if (rd8(s, ip) != 0xAF || (rd8(s, ip + 1) >> 6) != 3) return 4;
            m = rd8(s, ip + 1);
            ip += 2;
            s->reg[m >> 3 & 7] *= s->reg[m & 7];
            break;
        case 0xA3:
            wr32(s, rd32(s, ip), s->reg[DBG_EAX]);
            ip += 4;
            break;
        case 0xA1:
            s->reg[DBG_EAX] = rd32(s, rd32(s, ip));
            ip += 4;
            break;
        case 0xE6:
            if (rd8(s, ip++) == 0xE9) console(s, (uint8_t)s->reg[DBG_EAX]);
            break;
        case 0xE8:
            v = rd32(s, ip);
            ip += 4;
            s->reg[DBG_ESP] -= 4;
            wr32(s, s->reg[DBG_ESP], ip);
            ip += v;
            break;
        case 0xC3:
            ip = rd32(s, s->reg[DBG_ESP]);
            s->reg[DBG_ESP] += 4;
            break;
        case 0xEB:
            v = (uint32_t)(int32_t)(int8_t)rd8(s, ip++);
            ip += v;
            break;
        case 0x74:
        case 0x75:
            v = (uint32_t)(int32_t)(int8_t)rd8(s, ip++);
            cond = (s->reg[DBG_EFLAGS] & EFL_ZF) != 0;
            if (op == 0x75) cond = !cond;
            if (cond) ip += v;
            break;
        default:
            s->executed--;
            return 4;
        }
    s->reg[DBG_EIP] = ip & (STUB_MEM - 1);
    return 0;
}

/* ---- the stub's side of the protocol ---- */

static void queue(Stub *s, const void *p, size_t n) {
    if (s->outpos == s->nout) s->outpos = s->nout = 0;
    if (s->nout + n > sizeof s->out) return; /* the debugger is not reading: drop */
    memcpy(s->out + s->nout, p, n);
    s->nout += n;
}

/* An answer packet, its runs compressed when rle is set */
static void reply(Stub *s, const char *payload, size_t n) {
    static char f[2 * RSP_MAX + 8];
    static char z[RSP_MAX + 1];
    if (s->rle) { /* runs of 4 or more: the byte, '*', and the count + 29 (never '#', '$', '+' or '-') */
        size_t k = 0;
        for (size_t i = 0; i < n;) {
            size_t j = i + 1;
            while (j < n && payload[j] == payload[i] && j - i < 97) j++;
            size_t run = j - i - 1;
            z[k++] = payload[i];
            while (run == 6 || run == 7) { /* 6 + 29 = '#', 7 + 29 = '$': send one plainly */
                z[k++] = payload[i];
                run--;
            }
            if (run >= 3) {
                z[k++] = '*';
                z[k++] = (char)(run + 29);
            } else
                while (run--) z[k++] = payload[i];
            i = j;
        }
        payload = z;
        n = k;
        /* rsp_frame would escape the '*': frame it here */
        size_t o = 0;
        uint8_t sum = 0;
        f[o++] = '$';
        for (size_t i = 0; i < n; i++) {
            f[o++] = payload[i];
            sum = (uint8_t)(sum + (uint8_t)payload[i]);
        }
        f[o++] = '#';
        f[o++] = "0123456789abcdef"[sum >> 4];
        f[o++] = "0123456789abcdef"[sum & 15];
        queue(s, f, o);
        return;
    }
    size_t len = rsp_frame(payload, n, f, sizeof f);
    queue(s, f, len);
}
static void replys(Stub *s, const char *p) { reply(s, p, strlen(p)); }

static void console(Stub *s, uint8_t c) {
    s->con[s->ncon++] = (char)c;
    if (c == '\n' || s->ncon == sizeof s->con) {
        char p[1 + 2 * sizeof s->con];
        p[0] = 'O';
        rsp_hex((const uint8_t *)s->con, s->ncon, p + 1);
        reply(s, p, 1 + 2 * s->ncon);
        s->ncon = 0;
    }
}

static void stop_reply(Stub *s, int sig) {
    char p[4] = {'T', "0123456789abcdef"[sig >> 4], "0123456789abcdef"[sig & 15], 0};
    replys(s, p);
}

static int is_bp(const Stub *s, uint32_t a) {
    for (int i = 0; i < s->nbp; i++)
        if (s->bp[i] == a) return 1;
    return 0;
}

static void hex32le(char *h, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    rsp_hex(b, 4, h);
}
static void hex32be(char *h, uint32_t v) { /* as text: 8 digits */
    for (int i = 7; i >= 0; i--) {
        h[i] = "0123456789abcdef"[v & 15];
        v >>= 4;
    }
}

static int same(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return 0;
    return 1;
}
static int begins(const char *p, size_t n, const char *w) {
    size_t k = strlen(w);
    return n >= k && same(p, w, k);
}

/* QEMU's "info registers", shortened to the lines SquirrelDebug reads */
static void monitor_regs(Stub *s) {
    static const char *const N[8] = {"EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP", "ESP"};
    static const int IX[8] = {DBG_EAX, DBG_EBX, DBG_ECX, DBG_EDX, DBG_ESI, DBG_EDI, DBG_EBP, DBG_ESP};
    char t[400], p[1 + 800];
    size_t k = 0;
    for (int i = 0; i < 8; i++) {
        memcpy(t + k, N[i], 3);
        t[k + 3] = '=';
        hex32be(t + k + 4, s->reg[IX[i]]);
        k += 12;
        t[k++] = i % 4 == 3 ? '\n' : ' ';
    }
    memcpy(t + k, "EIP=", 4);
    hex32be(t + k + 4, s->reg[DBG_EIP]);
    k += 12;
    memcpy(t + k, " EFL=", 5);
    hex32be(t + k + 5, s->reg[DBG_EFLAGS]);
    k += 13;
    const char *cs = "\nCS =0008 00000000 ffffffff 00cf9a00 DPL=0 CS32 [-R-]\n"
                     "CR0=00000011 CR2=00000000 CR3=00000000 CR4=00000000\n";
    memcpy(t + k, cs, strlen(cs));
    k += strlen(cs);
    p[0] = 'O';
    rsp_hex((const uint8_t *)t, k, p + 1);
    reply(s, p, 1 + 2 * k);
    replys(s, "OK");
}

static void packet(Stub *s, const char *p, size_t n) {
    char buf[2 * 4096 + 16];
    const char *q = p + 1;
    uint32_t a, len;
    s->packets++;
    if (begins(p, n, "qSupported")) {
        replys(s, "PacketSize=1000;QStartNoAckMode+;swbreak+");
    } else if (begins(p, n, "QStartNoAckMode")) {
        replys(s, "OK");
        s->noack = 1;
    } else if (n == 1 && p[0] == '?') {
        stop_reply(s, 5);
    } else if (n == 1 && p[0] == 'g') {
        for (int i = 0; i < DBG_NREGS; i++) hex32le(buf + 8 * i, s->reg[i]);
        memset(buf + 8 * DBG_NREGS, '0', 64); /* more after them, as QEMU sends */
        reply(s, buf, 8 * DBG_NREGS + 64);
    } else if (p[0] == 'G' && n >= 1 + 8 * DBG_NREGS) {
        for (int i = 0; i < DBG_NREGS; i++) {
            uint8_t b[4];
            rsp_unhex(p + 1 + 8 * i, 8, b, 4);
            s->reg[i] = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
        }
        replys(s, "OK");
    } else if (p[0] == 'P' && s->no_p) {
        reply(s, "", 0);
    } else if (p[0] == 'P') {
        uint8_t b[4];
        if (rsp_hexnum(&q, &a) || *q != '=' || a >= DBG_NREGS || rsp_unhex(q + 1, 8, b, 4) != 4) replys(s, "E01");
        else {
            s->reg[a] = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
            replys(s, "OK");
        }
    } else if (p[0] == 'm') {
        if (rsp_hexnum(&q, &a) || *q++ != ',' || rsp_hexnum(&q, &len) || len > 4096) replys(s, "E01");
        else if (a >= STUB_MEM) replys(s, "E14");
        else {
            if (a + len > STUB_MEM) len = STUB_MEM - a;
            rsp_hex(s->mem + a, len, buf);
            reply(s, buf, 2 * len);
        }
    } else if (p[0] == 'M') {
        if (rsp_hexnum(&q, &a) || *q++ != ',' || rsp_hexnum(&q, &len) || *q++ != ':' || a + len > STUB_MEM ||
            rsp_unhex(q, (size_t)(p + n - q), s->mem + a, len) != len)
            replys(s, "E01");
        else
            replys(s, "OK");
    } else if ((p[0] == 'Z' || p[0] == 'z') && n > 2 && p[1] == '0') {
        q = p + 3;
        if (rsp_hexnum(&q, &a)) replys(s, "E01");
        else if (p[0] == 'Z') {
            if (!is_bp(s, a) && s->nbp < 64) s->bp[s->nbp++] = a;
            replys(s, "OK");
        } else {
            for (int i = 0; i < s->nbp; i++)
                if (s->bp[i] == a) s->bp[i] = s->bp[--s->nbp];
            replys(s, "OK");
        }
    } else if (n == 1 && p[0] == 's') {
        int sig = exec1(s);
        stop_reply(s, sig ? sig : 5);
    } else if (n == 1 && p[0] == 'c') {
        s->running = 1;
    } else if (n == 1 && p[0] == 'D') {
        replys(s, "OK");
        s->nbp = 0;
        s->running = 1;
    } else if (n == 1 && p[0] == 'k') {
        s->gone = 1;
    } else if (begins(p, n, "qRcmd,")) {
        uint8_t c[256];
        size_t k = rsp_unhex(p + 6, n - 6, c, sizeof c - 1);
        c[k] = 0;
        if (k == 14 && same(c, "info registers", 14)) monitor_regs(s);
        else {
            const char *t = "unknown command\n";
            char o[64];
            o[0] = 'O';
            rsp_hex((const uint8_t *)t, strlen(t), o + 1);
            reply(s, o, 1 + 2 * strlen(t));
            replys(s, "OK");
        }
    } else
        reply(s, "", 0); /* not supported */
}

static void run(Stub *s) {
    for (long i = 0; i < s->budget && s->running; i++) {
        uint32_t pc = s->reg[DBG_EIP];
        if (is_bp(s, pc)) { /* even where it was resumed, as QEMU does: the debugger steps off one first */
            s->running = 0;
            stop_reply(s, 5);
            break;
        }
        int sig = exec1(s);
        if (sig) {
            s->running = 0;
            stop_reply(s, sig);
        }
    }
}

static long st_write(void *ctx, const void *p, size_t n) {
    Stub *s = ctx;
    const uint8_t *b = p;
    if (s->gone) return -1;
    for (size_t i = 0; i < n; i++) {
        int e = rsp_rx_byte(&s->rx, b[i]);
        if (e == RSP_INTR && s->running) {
            s->running = 0;
            stop_reply(s, 2);
        } else if (e == RSP_BADSUM) {
            if (!s->noack) queue(s, "-", 1);
        } else if (e == RSP_PACKET) {
            if (s->nak > 0) {
                s->nak--;
                queue(s, "-", 1);
                continue;
            }
            if (!s->noack) queue(s, "+", 1);
            packet(s, (const char *)s->rx.buf, s->rx.n);
        }
    }
    return (long)n;
}

static long st_read(void *ctx, void *p, size_t n, int ms) {
    Stub *s = ctx;
    (void)ms;
    if (s->gone && s->outpos == s->nout) return -1;
    if (s->running && s->outpos == s->nout) run(s);
    size_t k = s->nout - s->outpos;
    if (k > n) k = n;
    memcpy(p, s->out + s->outpos, k);
    s->outpos += k;
    return (long)k;
}

DbgIo stub_io(Stub *s) {
    DbgIo io = {s, st_write, st_read};
    return io;
}
