/* dbg: a debugger session over the GDB remote serial protocol. Original
 * code. */
#include "dbg.h"

#include <string.h>

#include "x86dis.h"

const char *const DBG_REG_NAMES[DBG_NREGS] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
                                              "eip", "eflags", "cs", "ss", "ds", "es", "fs", "gs"};

/* text into a buffer, always 0-terminated */
typedef struct {
    char *p;
    size_t n, cap;
} Tx;

static void tx_c(Tx *t, char c) {
    if (t->n + 1 < t->cap) t->p[t->n++] = c;
    if (t->cap) t->p[t->n] = 0;
}
static void tx_s(Tx *t, const char *s) {
    while (*s) tx_c(t, *s++);
}
static void tx_sn(Tx *t, const char *s, size_t n) {
    for (size_t i = 0; i < n && s[i]; i++) tx_c(t, s[i]);
}
static void tx_hex(Tx *t, uint32_t v, int digits) { /* digits 0: as few as need */
    char b[8];
    int k = 0;
    do {
        b[k++] = "0123456789abcdef"[v & 15];
        v >>= 4;
    } while (v && k < 8);
    while (k < digits) b[k++] = '0';
    while (k) tx_c(t, b[--k]);
}
static void tx_0x(Tx *t, uint32_t v) {
    tx_s(t, "0x");
    tx_hex(t, v, 0);
}
static void tx_dec(Tx *t, long v) {
    char b[24];
    int k = 0;
    unsigned long u = v < 0 ? 0UL - (unsigned long)v : (unsigned long)v;
    if (v < 0) tx_c(t, '-');
    do {
        b[k++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    while (k) tx_c(t, b[--k]);
}
static Tx tx_make(char *p, size_t cap) {
    Tx t = {p, 0, cap};
    if (cap) p[0] = 0;
    return t;
}

static int err(Dbg *d, const char *a, const char *b) {
    Tx t = tx_make(d->err, sizeof d->err);
    tx_s(&t, a);
    if (b) tx_s(&t, b);
    return -1;
}

static int streqn(const char *a, size_t n, const char *b) { /* a[0..n) is b */
    size_t i = 0;
    for (; i < n; i++)
        if (!b[i] || a[i] != b[i]) return 0;
    return b[i] == 0;
}
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int streqn_ci(const char *a, size_t n, const char *b) {
    size_t i = 0;
    for (; i < n; i++)
        if (!b[i] || lower((unsigned char)a[i]) != lower((unsigned char)b[i])) return 0;
    return b[i] == 0;
}
void dbg_init(Dbg *d, DbgIo io) {
    memset(d, 0, sizeof *d);
    d->io = io;
    d->bits = 32;
    d->timeout_ms = 3000;
    rsp_rx_init(&d->rx);
}

/* ---- the link ---- */

static int getbyte(Dbg *d, int ms) { /* a byte, -1 when none came, -2 when the link is gone */
    if (d->inpos < d->inlen) return d->in[d->inpos++];
    long r = d->io.read(d->io.ctx, d->in, sizeof d->in, ms);
    if (r < 0) {
        d->state = DBG_OFF;
        return -2;
    }
    if (r == 0) return -1;
    d->inpos = 0;
    d->inlen = (size_t)r;
    return d->in[d->inpos++];
}

static int put(Dbg *d, const void *p, size_t n) {
    const char *s = p;
    while (n) {
        long w = d->io.write(d->io.ctx, s, n);
        if (w <= 0) {
            d->state = DBG_OFF;
            return err(d, "the link is gone", NULL);
        }
        s += w;
        n -= (size_t)w;
    }
    return 0;
}

static char FRAME[2 * RSP_MAX + 8];
static char R[RSP_MAX + 1]; /* answers */

static int send_packet(Dbg *d, const char *payload, size_t n) {
    size_t len = rsp_frame(payload, n, FRAME, sizeof FRAME);
    if (!len) return err(d, "a packet too long", NULL);
    for (int tries = 0; tries < 4; tries++) {
        if (put(d, FRAME, len)) return -1;
        if (d->noack) return 0;
        for (;;) {
            int c = getbyte(d, (int)d->timeout_ms);
            if (c == -2) return err(d, "the link is gone", NULL);
            if (c == -1) return err(d, "no answer from the target", NULL);
            int e = rsp_rx_byte(&d->rx, (uint8_t)c);
            if (e == RSP_ACK) return 0;
            if (e == RSP_NAK) break; /* again */
        }
    }
    return err(d, "the target keeps refusing a packet", NULL);
}

/* The next packet into R: its length, -1 when none came within ms, -2 when the link is gone */
static long recv_packet(Dbg *d, long ms) {
    for (;;) {
        int c = getbyte(d, (int)ms);
        if (c < 0) return c;
        int e = rsp_rx_byte(&d->rx, (uint8_t)c);
        if (e == RSP_PACKET) {
            if (!d->noack && put(d, "+", 1)) return -2;
            memcpy(R, d->rx.buf, d->rx.n + 1);
            return (long)d->rx.n;
        }
        if (e == RSP_BADSUM && !d->noack && put(d, "-", 1)) return -2;
    }
}

long dbg_request(Dbg *d, const char *payload, size_t n, char *reply, size_t cap) {
    if (send_packet(d, payload, n)) return -1;
    long r = recv_packet(d, d->timeout_ms);
    if (r == -2) return err(d, "the link is gone", NULL);
    if (r < 0) return err(d, "no answer from the target", NULL);
    if (reply && cap) {
        size_t k = (size_t)r < cap - 1 ? (size_t)r : cap - 1;
        memcpy(reply, R, k);
        reply[k] = 0;
    }
    return r;
}

static long req(Dbg *d, const char *payload) { return dbg_request(d, payload, strlen(payload), NULL, 0); }

static int is_error(long n) { return n >= 3 && R[0] == 'E' && rsp_hexval((unsigned char)R[1]) >= 0 && n == 3; }

static void out_add(Dbg *d, const char *s, size_t n) {
    if (n >= DBG_OUT) {
        s += n - (DBG_OUT - 1);
        n = DBG_OUT - 1;
    }
    if (d->nout + n >= DBG_OUT) {
        size_t drop = d->nout + n - (DBG_OUT - 1);
        memmove(d->out, d->out + drop, d->nout - drop);
        d->nout -= drop;
    }
    memcpy(d->out + d->nout, s, n);
    d->nout += n;
    d->out_total += n;
    d->out[d->nout] = 0;
}

/* A stop reply in R: 1 a stop or an end, 0 console output, -1 something else */
static int parse_stop(Dbg *d, long n) {
    const char *p = R + 1;
    uint32_t v = 0;
    if (n <= 0) return -1;
    switch (R[0]) {
    case 'S':
    case 'T':
        rsp_unhex(p, 2, (uint8_t *)&R[0], 1);
        d->sig = (uint8_t)R[0];
        d->state = DBG_STOPPED;
        d->stops++;
        return 1;
    case 'W':
        rsp_hexnum(&p, &v);
        d->code = (int)v;
        d->state = DBG_EXITED;
        return 1;
    case 'X':
        rsp_hexnum(&p, &v);
        d->code = 128 + (int)v;
        d->state = DBG_EXITED;
        return 1;
    case 'O':
        if (n == 2 && R[1] == 'K') return -1;
        {
            uint8_t b[RSP_MAX / 2];
            size_t k = rsp_unhex(p, (size_t)n - 1, b, sizeof b);
            out_add(d, (const char *)b, k);
        }
        return 0;
    }
    return -1;
}

/* ---- registers and the mode ---- */

static uint32_t le32_hex(const char *h) {
    uint8_t b[4] = {0};
    rsp_unhex(h, 8, b, 4);
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static void hex_le32(char *h, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    rsp_hex(b, 4, h);
}

static const char *skip_sp(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static const char *find_text(const char *hay, const char *needle) {
    size_t m = strlen(needle);
    for (; *hay; hay++)
        if (streqn(hay, m, needle)) return hay;
    return NULL;
}

/* QEMU's "info registers": CR0, CR3 and CS's base and flags */
static void parse_qemu_regs(Dbg *d, const char *t) {
    const char *p = find_text(t, "CR0=");
    uint32_t v;
    if (p) {
        p += 4;
        if (!rsp_hexnum(&p, &v)) {
            d->cr0 = v;
            d->cr_known = 1;
        }
    }
    if ((p = find_text(t, "CR3=")) != NULL) {
        p += 4;
        if (!rsp_hexnum(&p, &v)) d->cr3 = v;
    }
    if ((p = find_text(t, "CS =")) != NULL) {
        uint32_t sel, base, limit, flags;
        p = skip_sp(p + 4);
        if (!rsp_hexnum(&p, &sel) && !rsp_hexnum((p = skip_sp(p), &p), &base) &&
            !rsp_hexnum((p = skip_sp(p), &p), &limit) && !rsp_hexnum((p = skip_sp(p), &p), &flags)) {
            d->cs_base = base;
            if (!d->bits_forced) d->bits = (d->cr0 & 1) && (flags & 0x00400000u) ? 32 : 16;
        }
    }
}

int dbg_refresh(Dbg *d) {
    long n = req(d, "g");
    if (n < 0) return -1;
    if (n < 16 * 8 || is_error(n)) return err(d, "the target did not give its registers: ", R);
    if ((size_t)n < sizeof d->g) {
        memcpy(d->g, R, (size_t)n + 1);
        d->ng = (size_t)n;
    } else
        d->ng = 0;
    for (int i = 0; i < DBG_NREGS; i++) d->reg[i] = le32_hex(R + 8 * i);
    d->gen++;
    /* the mode: QEMU's monitor knows CR0 and CS's descriptor */
    if (d->monitor >= 0) {
        static char t[4096];
        long k = dbg_monitor(d, "info registers", t, sizeof t);
        if (k > 0 && find_text(t, "CR0=")) {
            d->monitor = 1;
            parse_qemu_regs(d, t);
            if (d->bits_forced) d->bits = d->bits_forced;
            return 0;
        }
        d->monitor = -1;
    }
    d->cr_known = 0;
    d->bits = d->bits_forced ? d->bits_forced : 32;
    d->cs_base = d->bits == 16 ? d->reg[DBG_CS] << 4 : 0;
    return 0;
}

uint32_t dbg_pc(const Dbg *d) {
    return d->cs_base + (d->bits == 16 ? d->reg[DBG_EIP] & 0xFFFF : d->reg[DBG_EIP]);
}

int dbg_attach(Dbg *d) {
    char sup[512];
    d->state = DBG_STOPPED; /* a stub stops the target when it is taken */
    if (dbg_request(d, "qSupported:swbreak+;hwbreak+", 28, sup, sizeof sup) < 0) {
        d->state = DBG_OFF;
        return -1;
    }
    if (find_text(sup, "QStartNoAckMode+")) {
        long n = req(d, "QStartNoAckMode");
        if (n == 2 && R[0] == 'O' && R[1] == 'K') d->noack = 1;
    }
    long n = req(d, "?");
    if (n < 0 || parse_stop(d, n) != 1) {
        d->state = DBG_OFF;
        return n < 0 ? -1 : err(d, "the target did not say why it stopped", NULL);
    }
    if (d->state == DBG_EXITED) return 0;
    if (dbg_refresh(d)) return -1;
    memcpy(d->prev, d->reg, sizeof d->reg);
    return 0;
}

static int ok(long n) { return n == 2 && R[0] == 'O' && R[1] == 'K'; }

static int need_stopped(Dbg *d) {
    if (d->state == DBG_STOPPED) return 0;
    return err(d, d->state == DBG_RUNNING ? "the target is running (stop it first)"
                  : d->state == DBG_EXITED ? "the program has ended"
                                           : "not connected",
               NULL);
}

int dbg_set_reg(Dbg *d, int r, uint32_t v) {
    char p[32];
    Tx t = tx_make(p, sizeof p);
    if (need_stopped(d)) return -1;
    if (r < 0 || r >= DBG_NREGS) return err(d, "no such register", NULL);
    tx_c(&t, 'P');
    tx_hex(&t, (uint32_t)r, 0);
    tx_c(&t, '=');
    hex_le32(p + t.n, v);
    t.n += 8;
    p[t.n] = 0;
    long n = req(d, p);
    if (n < 0) return -1;
    if (n == 0 && d->ng >= 16 * 8) { /* no 'P': the whole set with 'G' */
        static char g[DBG_G + 2];
        g[0] = 'G';
        memcpy(g + 1, d->g, d->ng);
        hex_le32(g + 1 + 8 * r, v);
        n = dbg_request(d, g, d->ng + 1, NULL, 0);
        if (n < 0) return -1;
    }
    if (!ok(n)) return err(d, "the target would not set the register: ", R);
    return dbg_refresh(d);
}

/* ---- memory ---- */

long dbg_read(Dbg *d, uint32_t addr, uint8_t *buf, size_t n) {
    size_t done = 0;
    if (need_stopped(d)) return -1;
    while (done < n) {
        char p[32];
        size_t chunk = n - done < 1024 ? n - done : 1024;
        Tx t = tx_make(p, sizeof p);
        tx_c(&t, 'm');
        tx_hex(&t, addr + (uint32_t)done, 0);
        tx_c(&t, ',');
        tx_hex(&t, (uint32_t)chunk, 0);
        long r = req(d, p);
        if (r < 0) return -1;
        if (r == 0 || is_error(r)) break;
        size_t got = rsp_unhex(R, (size_t)r, buf + done, chunk);
        if (!got) break;
        done += got;
        if (got < chunk) break;
    }
    if (!done && n) return err(d, "the target cannot read that memory", NULL);
    return (long)done;
}

int dbg_write(Dbg *d, uint32_t addr, const uint8_t *buf, size_t n) {
    static char p[1100];
    size_t done = 0;
    if (need_stopped(d)) return -1;
    while (done < n) {
        size_t chunk = n - done < 512 ? n - done : 512;
        Tx t = tx_make(p, sizeof p);
        tx_c(&t, 'M');
        tx_hex(&t, addr + (uint32_t)done, 0);
        tx_c(&t, ',');
        tx_hex(&t, (uint32_t)chunk, 0);
        tx_c(&t, ':');
        rsp_hex(buf + done, chunk, p + t.n);
        t.n += 2 * chunk;
        long r = dbg_request(d, p, t.n, NULL, 0);
        if (r < 0) return -1;
        if (!ok(r)) return err(d, "the target cannot write that memory", NULL);
        done += chunk;
    }
    d->gen++;
    return 0;
}

/* ---- breakpoints and running ---- */

int dbg_is_break(const Dbg *d, uint32_t addr) {
    for (int i = 0; i < d->nbp; i++)
        if (d->bp[i] == addr) return 1;
    return 0;
}

static int zpacket(Dbg *d, int set, uint32_t addr) {
    char p[32];
    Tx t = tx_make(p, sizeof p);
    tx_s(&t, set ? "Z0," : "z0,");
    tx_hex(&t, addr, 0);
    tx_s(&t, ",1");
    long n = req(d, p);
    if (n < 0) return -1;
    if (n == 0) return err(d, "the target has no breakpoints", NULL);
    if (!ok(n)) return err(d, "the target refused the breakpoint: ", R);
    return 0;
}

int dbg_break(Dbg *d, uint32_t addr) {
    if (need_stopped(d)) return -1;
    if (dbg_is_break(d, addr)) return 0;
    if (d->nbp >= DBG_BPS) return err(d, "too many breakpoints", NULL);
    if (!(d->temp && d->temp_addr == addr) && zpacket(d, 1, addr)) return -1;
    d->bp[d->nbp++] = addr;
    return 0;
}

int dbg_unbreak(Dbg *d, uint32_t addr) {
    if (need_stopped(d)) return -1;
    for (int i = 0; i < d->nbp; i++)
        if (d->bp[i] == addr) {
            if (!(d->temp && d->temp_addr == addr) && zpacket(d, 0, addr)) return -1;
            d->bp[i] = d->bp[--d->nbp];
            return 0;
        }
    return err(d, "no breakpoint there", NULL);
}

static void after_stop(Dbg *d) {
    if (d->state != DBG_STOPPED) {
        d->temp = 0;
        return;
    }
    if (d->temp) {
        d->temp = 0;
        if (!dbg_is_break(d, d->temp_addr)) zpacket(d, 0, d->temp_addr);
    }
    dbg_refresh(d);
}

/* Waits for the stop reply (taking console text on the way); 0, or -1 */
static int wait_reply(Dbg *d, long ms) {
    for (;;) {
        long n = recv_packet(d, ms);
        if (n == -2) return err(d, "the link is gone", NULL);
        if (n < 0) return err(d, "the target did not stop", NULL);
        if (parse_stop(d, n) == 1) return 0;
    }
}

static int wait_stop(Dbg *d, long ms) {
    if (wait_reply(d, ms)) return -1;
    after_stop(d);
    return 0;
}

/* Step or continue. A stub reports a breakpoint at the pc again at once
 * (QEMU does), so one there is lifted for a step first and put back, as
 * gdb does. */
static int resume(Dbg *d, int step) {
    if (need_stopped(d)) return -1;
    uint32_t pc = dbg_pc(d);
    int lifted = dbg_is_break(d, pc) || (d->temp && d->temp_addr == pc);
    memcpy(d->prev, d->reg, sizeof d->reg);
    if (lifted) {
        if (zpacket(d, 0, pc) || send_packet(d, "s", 1)) return -1;
        d->state = DBG_RUNNING;
        int rc = wait_reply(d, d->timeout_ms);
        if (d->state == DBG_STOPPED && zpacket(d, 1, pc)) return -1;
        if (rc) return -1;
        if (step || d->state != DBG_STOPPED || d->sig != 5) {
            after_stop(d);
            return 0;
        }
    } else if (step) {
        if (send_packet(d, "s", 1)) return -1;
        d->state = DBG_RUNNING;
        return wait_stop(d, d->timeout_ms);
    }
    if (send_packet(d, "c", 1)) return -1;
    d->state = DBG_RUNNING;
    d->gen++;
    return 0;
}

int dbg_step(Dbg *d) { return resume(d, 1); }

int dbg_continue(Dbg *d) { return resume(d, 0); }

int dbg_until(Dbg *d, uint32_t addr) {
    if (need_stopped(d)) return -1;
    if (!dbg_is_break(d, addr)) {
        if (zpacket(d, 1, addr)) return -1;
        d->temp = 1;
        d->temp_addr = addr;
    }
    return dbg_continue(d);
}

int dbg_next(Dbg *d) {
    uint8_t code[X86_MAXLEN];
    X86Insn in;
    if (need_stopped(d)) return -1;
    uint32_t pc = dbg_pc(d);
    long n = dbg_read(d, pc, code, sizeof code);
    if (n <= 0) return -1;
    uint32_t ip = d->bits == 16 ? d->reg[DBG_EIP] & 0xFFFF : d->reg[DBG_EIP];
    x86_disasm(code, (size_t)n, ip, d->cs_base, d->bits, &in);
    if (in.flow != X86_FLOW_CALL && !in.rep) return dbg_step(d);
    uint32_t after = ip + (uint32_t)in.len;
    if (d->bits == 16) after &= 0xFFFF;
    return dbg_until(d, d->cs_base + after);
}

int dbg_poll(Dbg *d, int ms) {
    while (d->state == DBG_RUNNING) {
        long n = recv_packet(d, ms);
        if (n < 0) break; /* nothing yet, or the link went (state is then DBG_OFF) */
        if (parse_stop(d, n) == 1) after_stop(d);
        if (d->inpos >= d->inlen) break;
        ms = 0;
    }
    return d->state;
}

int dbg_wait(Dbg *d, long ms) {
    long step = ms < 50 ? ms : 50;
    for (long t = 0; d->state == DBG_RUNNING && t < ms; t += step) dbg_poll(d, (int)step);
    return d->state;
}

int dbg_stop(Dbg *d) {
    if (d->state != DBG_RUNNING) return 0;
    if (put(d, "\x03", 1)) return -1;
    return wait_stop(d, d->timeout_ms);
}

int dbg_detach(Dbg *d) {
    if (d->state == DBG_OFF) return 0;
    if (d->state == DBG_RUNNING && dbg_stop(d)) return -1;
    for (int i = d->nbp - 1; i >= 0; i--) dbg_unbreak(d, d->bp[i]);
    long n = req(d, "D");
    d->state = DBG_OFF;
    return n < 0 ? -1 : 0;
}

int dbg_kill(Dbg *d) {
    if (d->state == DBG_OFF) return 0;
    char f[8];
    size_t len = rsp_frame("k", 1, f, sizeof f);
    put(d, f, len); /* no answer comes: the target is gone */
    d->state = DBG_OFF;
    return 0;
}

long dbg_monitor(Dbg *d, const char *cmd, char *out, size_t cap) {
    static char p[600];
    size_t n = strlen(cmd);
    Tx o = tx_make(out, cap);
    if (need_stopped(d)) return -1;
    if (n > 280) return err(d, "the command is too long", NULL);
    memcpy(p, "qRcmd,", 6);
    rsp_hex((const uint8_t *)cmd, n, p + 6);
    if (send_packet(d, p, 6 + 2 * n)) return -1;
    for (;;) {
        long r = recv_packet(d, d->timeout_ms);
        if (r == -2) return err(d, "the link is gone", NULL);
        if (r < 0) return err(d, "no answer from the target", NULL);
        if (r == 0) return err(d, "the target has no monitor", NULL);
        if (ok(r)) return (long)o.n;
        if (is_error(r)) return err(d, "the monitor refused: ", R);
        if (R[0] == 'O') {
            uint8_t b[RSP_MAX / 2];
            size_t k = rsp_unhex(R + 1, (size_t)r - 1, b, sizeof b);
            tx_sn(&o, (const char *)b, k);
        } else { /* QEMU before 2.x answered in one hex packet */
            uint8_t b[RSP_MAX / 2];
            size_t k = rsp_unhex(R, (size_t)r, b, sizeof b);
            tx_sn(&o, (const char *)b, k);
            return (long)o.n;
        }
    }
}

/* ---- symbols ---- */

static int is_space(int c) { return c == ' ' || c == '\t' || c == '\r'; }

int dbg_load_symbols(Dbg *d, const char *text, size_t n) {
    int took = 0;
    size_t i = 0;
    while (i < n) {
        size_t e = i;
        while (e < n && text[e] != '\n') e++;
        /* one line: text[i..e) */
        size_t k = i;
        uint32_t addr = 0;
        int digits = 0, h;
        while (k < e && is_space((unsigned char)text[k])) k++;
        if (k + 1 < e && text[k] == '0' && (text[k + 1] == 'x' || text[k + 1] == 'X')) k += 2;
        while (k < e && (h = rsp_hexval((unsigned char)text[k])) >= 0) {
            addr = addr << 4 | (uint32_t)h;
            k++;
            digits++;
        }
        if (digits && k < e && is_space((unsigned char)text[k])) {
            while (k < e && is_space((unsigned char)text[k])) k++;
            size_t w = k;
            while (w < e && !is_space((unsigned char)text[w])) w++;
            if (w - k == 1 && w < e) { /* nm's type letter */
                int ty = text[k];
                k = w;
                while (k < e && is_space((unsigned char)text[k])) k++;
                w = k;
                while (w < e && !is_space((unsigned char)text[w])) w++;
                if (ty == 'U' || ty == 'w' || ty == 'v' || ty == 'a' || ty == 'N') w = k; /* no address of code or data */
            }
            size_t len = w - k;
            if (len && len < 120 && d->nsym < DBG_SYMS && d->npool + len + 1 <= DBG_POOL) {
                d->sym[d->nsym].addr = addr;
                d->sym[d->nsym].name = (uint32_t)d->npool;
                memcpy(d->pool + d->npool, text + k, len);
                d->pool[d->npool + len] = 0;
                d->npool += len + 1;
                d->nsym++;
                took++;
            }
        }
        i = e + 1;
    }
    /* by address (a shell sort; the list is loaded once) */
    for (int gap = d->nsym / 2; gap > 0; gap /= 2)
        for (int a = gap; a < d->nsym; a++) {
            DbgSym s = d->sym[a];
            int b = a;
            for (; b >= gap && d->sym[b - gap].addr > s.addr; b -= gap) d->sym[b] = d->sym[b - gap];
            d->sym[b] = s;
        }
    return took;
}

const char *dbg_sym_at(const Dbg *d, uint32_t addr, uint32_t *off) {
    int lo = 0, hi = d->nsym - 1, at = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (d->sym[mid].addr <= addr) {
            at = mid;
            lo = mid + 1;
        } else
            hi = mid - 1;
    }
    if (at < 0 || addr - d->sym[at].addr >= 0x10000) return NULL;
    if (off) *off = addr - d->sym[at].addr;
    return d->pool + d->sym[at].name;
}

int dbg_sym_find(const Dbg *d, const char *name, size_t n, uint32_t *addr) {
    for (int i = 0; i < d->nsym; i++)
        if (streqn(name, n, d->pool + d->sym[i].name)) {
            *addr = d->sym[i].addr;
            return 0;
        }
    return -1;
}

/* ---- expressions ---- */

static int word_char(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
           c == '$' || c == '@' || c == '?';
}

static int reg_named(const Dbg *d, const char *s, size_t n, uint32_t *v) {
    static const char *const R16N[8] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};
    for (int i = 0; i < DBG_NREGS; i++)
        if (streqn_ci(s, n, DBG_REG_NAMES[i])) {
            *v = d->reg[i];
            return 0;
        }
    for (int i = 0; i < 8; i++)
        if (streqn_ci(s, n, R16N[i])) {
            *v = d->reg[i] & 0xFFFF;
            return 0;
        }
    if (streqn_ci(s, n, "pc")) {
        *v = dbg_pc(d);
        return 0;
    }
    if (streqn_ci(s, n, "ip")) {
        *v = d->reg[DBG_EIP] & 0xFFFF;
        return 0;
    }
    if (streqn_ci(s, n, "flags")) {
        *v = d->reg[DBG_EFLAGS];
        return 0;
    }
    return -1;
}

static int word_value(const Dbg *d, const char *s, size_t n, uint32_t *v) {
    if (!n) return -1;
    if (n > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        n -= 2;
    } else {
        if (!reg_named(d, s, n, v)) return 0;
        if (!dbg_sym_find(d, s, n, v)) return 0;
    }
    uint32_t x = 0;
    if (n > 8) return -1;
    for (size_t i = 0; i < n; i++) {
        int h = rsp_hexval((unsigned char)s[i]);
        if (h < 0) return -1;
        x = x << 4 | (uint32_t)h;
    }
    *v = x;
    return 0;
}

int dbg_eval(const Dbg *d, const char *s, uint32_t *v) {
    uint32_t acc = 0;
    int sign = 1, terms = 0;
    for (;;) {
        s = skip_sp(s);
        const char *w = s;
        while (word_char((unsigned char)*s)) s++;
        uint32_t t;
        if (word_value(d, w, (size_t)(s - w), &t)) return -1;
        if (*s == ':') { /* SEG:OFF */
            const char *o = ++s;
            uint32_t off;
            while (word_char((unsigned char)*s)) s++;
            if (word_value(d, o, (size_t)(s - o), &off)) return -1;
            t = (t << 4) + off;
        }
        acc = sign > 0 ? acc + t : acc - t;
        terms++;
        s = skip_sp(s);
        if (*s == '+') sign = 1;
        else if (*s == '-') sign = -1;
        else break;
        s++;
    }
    if (*s || !terms) return -1;
    *v = acc;
    return 0;
}

/* ---- text ---- */

size_t dbg_addr_text(const Dbg *d, uint32_t addr, char *out, size_t cap) {
    Tx t = tx_make(out, cap);
    uint32_t off;
    const char *name = dbg_sym_at(d, addr, &off);
    tx_s(&t, "0x");
    tx_hex(&t, addr, 8);
    if (name) {
        tx_s(&t, " <");
        tx_s(&t, name);
        if (off) {
            tx_c(&t, '+');
            tx_0x(&t, off);
        }
        tx_c(&t, '>');
    }
    return t.n;
}

int dbg_disasm_at(const Dbg *d, uint32_t addr, const uint8_t *code, size_t n, char *out, size_t cap, X86Insn *insn) {
    X86Insn in;
    Tx t = tx_make(out, cap);
    uint32_t ip = addr - d->cs_base;
    if (d->bits == 16) ip &= 0xFFFF;
    x86_disasm(code, n, ip, d->cs_base, d->bits, &in);
    tx_c(&t, d->state == DBG_STOPPED && addr == dbg_pc(d) ? '>' : ' ');
    tx_c(&t, dbg_is_break(d, addr) ? '*' : ' ');
    tx_c(&t, ' ');
    tx_hex(&t, addr, 8);
    tx_s(&t, "  ");
    size_t col = t.n;
    for (int i = 0; i < in.len && i < 7; i++) {
        tx_hex(&t, code[i], 2);
        tx_c(&t, ' ');
    }
    if (in.len > 7) tx_s(&t, ".. ");
    while (t.n < col + 22) tx_c(&t, ' ');
    tx_s(&t, in.text);
    uint32_t off;
    const char *name;
    if (in.has_target && (name = dbg_sym_at(d, in.target, &off)) != NULL) {
        tx_s(&t, "  <");
        tx_s(&t, name);
        if (off) {
            tx_c(&t, '+');
            tx_0x(&t, off);
        }
        tx_c(&t, '>');
    }
    if (insn) *insn = in;
    return in.len;
}

int dbg_disasm_line(Dbg *d, uint32_t addr, char *out, size_t cap) {
    uint8_t code[X86_MAXLEN];
    if (cap) out[0] = 0;
    long n = dbg_read(d, addr, code, sizeof code);
    if (n <= 0) return 0;
    return dbg_disasm_at(d, addr, code, (size_t)n, out, cap, NULL);
}

size_t dbg_regs_text(const Dbg *d, char *out, size_t cap) {
    static const int ORDER[8] = {DBG_EAX, DBG_EBX, DBG_ECX, DBG_EDX, DBG_ESI, DBG_EDI, DBG_EBP, DBG_ESP};
    static const char *const FL[9] = {"CF", "PF", "AF", "ZF", "SF", "TF", "IF", "DF", "OF"};
    static const int FB[9] = {0, 2, 4, 6, 7, 8, 9, 10, 11};
    Tx t = tx_make(out, cap);
    for (int i = 0; i < 8; i++) {
        tx_s(&t, DBG_REG_NAMES[ORDER[i]]);
        tx_c(&t, ' ');
        tx_hex(&t, d->reg[ORDER[i]], 8);
        tx_s(&t, i % 4 == 3 ? "\n" : "  ");
    }
    tx_s(&t, "eip ");
    tx_hex(&t, d->reg[DBG_EIP], 8);
    tx_s(&t, "  eflags ");
    tx_hex(&t, d->reg[DBG_EFLAGS], 8);
    tx_s(&t, " [");
    int any = 0;
    for (int i = 0; i < 9; i++)
        if (d->reg[DBG_EFLAGS] >> FB[i] & 1) {
            if (any++) tx_c(&t, ' ');
            tx_s(&t, FL[i]);
        }
    tx_s(&t, "]\n");
    for (int i = DBG_CS; i <= DBG_GS; i++) {
        tx_s(&t, DBG_REG_NAMES[i]);
        tx_c(&t, ' ');
        tx_hex(&t, d->reg[i], 4);
        tx_s(&t, i == DBG_GS ? "\n" : "  ");
    }
    if (d->cr_known) {
        tx_s(&t, "cr0 ");
        tx_hex(&t, d->cr0, 8);
        tx_s(&t, "  cr3 ");
        tx_hex(&t, d->cr3, 8);
        tx_s(&t, "  ");
    }
    tx_dec(&t, d->bits);
    tx_s(&t, "-bit code");
    if (d->cs_base) {
        tx_s(&t, ", cs base ");
        tx_0x(&t, d->cs_base);
    }
    return t.n;
}

size_t dbg_status_text(Dbg *d, char *out, size_t cap) {
    Tx t = tx_make(out, cap);
    switch (d->state) {
    case DBG_OFF: tx_s(&t, "not connected"); break;
    case DBG_RUNNING: tx_s(&t, "running"); break;
    case DBG_EXITED:
        tx_s(&t, "the program ended (");
        tx_dec(&t, d->code);
        tx_c(&t, ')');
        break;
    default: {
        char a[160];
        dbg_addr_text(d, dbg_pc(d), a, sizeof a);
        tx_s(&t, "stopped at ");
        tx_s(&t, a);
        if (d->sig != 5) {
            tx_s(&t, " (signal ");
            tx_dec(&t, d->sig);
            tx_c(&t, ')');
        }
    }
    }
    return t.n;
}

/* ---- commands ---- */

typedef struct {
    const char *w[24];
    size_t n[24];
    int count;
    const char *rest[24]; /* the line from each word on */
} Words;

static void split(const char *s, Words *w) {
    w->count = 0;
    for (;;) {
        s = skip_sp(s);
        if (!*s || *s == '\n' || w->count == 24) break;
        w->w[w->count] = w->rest[w->count] = s;
        while (*s && !is_space((unsigned char)*s) && *s != '\n') s++;
        w->n[w->count] = (size_t)(s - w->w[w->count]);
        w->count++;
    }
}

static int is_cmd(const Words *w, const char *a, const char *b) {
    return w->count && (streqn_ci(w->w[0], w->n[0], a) || (b && streqn_ci(w->w[0], w->n[0], b)));
}

static int eval_word(const Dbg *d, const Words *w, int i, uint32_t *v) {
    char b[96];
    if (i >= w->count || w->n[i] >= sizeof b) return -1;
    memcpy(b, w->w[i], w->n[i]);
    b[w->n[i]] = 0;
    return dbg_eval(d, b, v);
}

static int count_word(const Words *w, int i, long dflt, long max, long *v) {
    long x = 0;
    if (i >= w->count) {
        *v = dflt;
        return 0;
    }
    for (size_t k = 0; k < w->n[i]; k++) {
        char c = w->w[i][k];
        if (c < '0' || c > '9') return -1;
        x = x * 10 + (c - '0');
        if (x > max) return -1;
    }
    if (x <= 0) return -1;
    *v = x;
    return 0;
}

static int fail(char *out, size_t cap, const char *a, const char *b) {
    Tx t = tx_make(out, cap);
    tx_s(&t, a);
    if (b) tx_s(&t, b);
    return -1;
}
static int fail_d(Dbg *d, char *out, size_t cap) { return fail(out, cap, d->err, NULL); }

static void hexdump(Dbg *d, Tx *t, uint32_t addr, const uint8_t *b, size_t n) {
    (void)d;
    for (size_t row = 0; row < n; row += 16) {
        tx_hex(t, addr + (uint32_t)row, 8);
        tx_s(t, "  ");
        for (size_t i = 0; i < 16; i++) {
            if (row + i < n) {
                tx_hex(t, b[row + i], 2);
                tx_c(t, ' ');
            } else
                tx_s(t, "   ");
            if (i == 7) tx_c(t, ' ');
        }
        tx_c(t, ' ');
        for (size_t i = 0; i < 16 && row + i < n; i++) tx_c(t, b[row + i] >= 0x20 && b[row + i] < 0x7F ? (char)b[row + i] : '.');
        if (row + 16 < n) tx_c(t, '\n');
    }
}

static int reg_index(const char *s, size_t n) {
    for (int i = 0; i < DBG_NREGS; i++)
        if (streqn_ci(s, n, DBG_REG_NAMES[i])) return i;
    if (streqn_ci(s, n, "pc")) return DBG_EIP;
    return -1;
}

int dbg_command(Dbg *d, const char *line, char *out, size_t cap) {
    Words w;
    Tx t = tx_make(out, cap);
    uint32_t a;
    long n;
    split(line, &w);
    if (!w.count) return 0;
    if (is_cmd(&w, "regs", "r")) {
        if (need_stopped(d)) return fail_d(d, out, cap);
        dbg_regs_text(d, out, cap);
        return 0;
    }
    if (is_cmd(&w, "x", "mem")) {
        static uint8_t b[1024];
        if (eval_word(d, &w, 1, &a)) return fail(out, cap, "x ADDRESS [BYTES]: which address?", NULL);
        if (count_word(&w, 2, 64, 1024, &n)) return fail(out, cap, "x: from 1 to 1024 bytes", NULL);
        long got = dbg_read(d, a, b, (size_t)n);
        if (got < 0) return fail_d(d, out, cap);
        hexdump(d, &t, a, b, (size_t)got);
        return 0;
    }
    if (is_cmd(&w, "dis", "u")) {
        if (need_stopped(d)) return fail_d(d, out, cap);
        a = dbg_pc(d);
        int from = 1;
        if (w.count > 1 && !eval_word(d, &w, 1, &a)) from = 2;
        else if (w.count > 2) return fail(out, cap, "dis [ADDRESS] [COUNT]: which address?", NULL);
        if (count_word(&w, from, 10, 200, &n)) return fail(out, cap, "dis: from 1 to 200 instructions", NULL);
        for (long i = 0; i < n; i++) {
            char l[200];
            int len = dbg_disasm_line(d, a, l, sizeof l);
            if (!len) {
                if (!i) return fail_d(d, out, cap);
                break;
            }
            if (i) tx_c(&t, '\n');
            tx_s(&t, l);
            a += (uint32_t)len;
        }
        return 0;
    }
    if (is_cmd(&w, "break", "b")) {
        if (eval_word(d, &w, 1, &a)) return fail(out, cap, "break ADDRESS: which address?", NULL);
        if (dbg_break(d, a)) return fail_d(d, out, cap);
        char s[160];
        dbg_addr_text(d, a, s, sizeof s);
        tx_s(&t, "breakpoint at ");
        tx_s(&t, s);
        return 0;
    }
    if (is_cmd(&w, "delete", "d")) {
        if (w.count > 1 && streqn_ci(w.w[1], w.n[1], "all")) {
            while (d->nbp)
                if (dbg_unbreak(d, d->bp[d->nbp - 1])) return fail_d(d, out, cap);
            tx_s(&t, "no breakpoints");
            return 0;
        }
        if (eval_word(d, &w, 1, &a)) return fail(out, cap, "delete ADDRESS or delete all", NULL);
        if (dbg_unbreak(d, a)) return fail_d(d, out, cap);
        tx_s(&t, "deleted");
        return 0;
    }
    if (is_cmd(&w, "breaks", "info")) {
        if (!d->nbp) tx_s(&t, "no breakpoints");
        for (int i = 0; i < d->nbp; i++) {
            char s[160];
            dbg_addr_text(d, d->bp[i], s, sizeof s);
            if (i) tx_c(&t, '\n');
            tx_s(&t, s);
        }
        return 0;
    }
    if (is_cmd(&w, "step", "s") || is_cmd(&w, "stepi", "si")) {
        if (count_word(&w, 1, 1, 100000, &n)) return fail(out, cap, "step [COUNT]", NULL);
        for (long i = 0; i < n; i++) {
            if (dbg_step(d)) return fail_d(d, out, cap);
            if (d->state != DBG_STOPPED || dbg_is_break(d, dbg_pc(d))) break;
        }
        dbg_status_text(d, out, cap);
        return 0;
    }
    if (is_cmd(&w, "next", "n")) {
        if (dbg_next(d)) return fail_d(d, out, cap);
        dbg_status_text(d, out, cap);
        return 0;
    }
    if (is_cmd(&w, "until", "go")) {
        if (eval_word(d, &w, 1, &a)) return fail(out, cap, "until ADDRESS: which address?", NULL);
        if (dbg_until(d, a)) return fail_d(d, out, cap);
        tx_s(&t, "running");
        return 0;
    }
    if (is_cmd(&w, "continue", "c")) {
        if (dbg_continue(d)) return fail_d(d, out, cap);
        tx_s(&t, "running");
        return 0;
    }
    if (is_cmd(&w, "stop", NULL)) {
        if (d->state != DBG_RUNNING) return fail(out, cap, "the target is not running", NULL);
        if (dbg_stop(d)) return fail_d(d, out, cap);
        dbg_status_text(d, out, cap);
        return 0;
    }
    if (is_cmd(&w, "set", NULL)) {
        int r = w.count > 1 ? reg_index(w.w[1], w.n[1]) : -1;
        if (r < 0) return fail(out, cap, "set REGISTER VALUE: which register?", NULL);
        if (w.count < 3 || dbg_eval(d, skip_sp(w.rest[2]), &a)) return fail(out, cap, "set REGISTER VALUE: what value?", NULL);
        if (dbg_set_reg(d, r, a)) return fail_d(d, out, cap);
        tx_s(&t, DBG_REG_NAMES[r]);
        tx_s(&t, " = ");
        tx_0x(&t, d->reg[r]);
        return 0;
    }
    if (is_cmd(&w, "write", NULL)) {
        uint8_t b[24];
        int k = 0;
        if (eval_word(d, &w, 1, &a)) return fail(out, cap, "write ADDRESS BYTE...: which address?", NULL);
        for (int i = 2; i < w.count; i++) {
            if (k == 24 || w.n[i] > 2) return fail(out, cap, "write: up to 24 bytes, each two hex digits", NULL);
            uint32_t v = 0;
            for (size_t j = 0; j < w.n[i]; j++) {
                int h = rsp_hexval((unsigned char)w.w[i][j]);
                if (h < 0) return fail(out, cap, "write: bytes are hex", NULL);
                v = v << 4 | (uint32_t)h;
            }
            b[k++] = (uint8_t)v;
        }
        if (!k) return fail(out, cap, "write ADDRESS BYTE...: which bytes?", NULL);
        if (dbg_write(d, a, b, (size_t)k)) return fail_d(d, out, cap);
        tx_s(&t, "wrote ");
        tx_dec(&t, k);
        tx_s(&t, k == 1 ? " byte" : " bytes");
        return 0;
    }
    if (is_cmd(&w, "mode", NULL)) {
        if (w.count > 1) {
            if (streqn(w.w[1], w.n[1], "16")) d->bits_forced = 16;
            else if (streqn(w.w[1], w.n[1], "32")) d->bits_forced = 32;
            else if (streqn_ci(w.w[1], w.n[1], "auto")) d->bits_forced = 0;
            else return fail(out, cap, "mode 16, 32 or auto", NULL);
            if (d->state == DBG_STOPPED && dbg_refresh(d)) return fail_d(d, out, cap);
        }
        tx_dec(&t, d->bits);
        tx_s(&t, d->bits_forced ? "-bit code (set)" : "-bit code");
        return 0;
    }
    if (is_cmd(&w, "mon", "monitor")) {
        if (w.count < 2) return fail(out, cap, "mon COMMAND", NULL);
        if (dbg_monitor(d, w.rest[1], out, cap) < 0) return fail_d(d, out, cap);
        size_t k = strlen(out), lead = 0;
        while (k && out[k - 1] == '\n') out[--k] = 0;
        while (lead < k && (out[lead] == '\n' || out[lead] == '\r')) lead++;
        memmove(out, out + lead, k - lead + 1);
        return 0;
    }
    if (is_cmd(&w, "sym", NULL)) {
        if (w.count < 2) {
            tx_dec(&t, d->nsym);
            tx_s(&t, " symbols");
            return 0;
        }
        if (eval_word(d, &w, 1, &a)) return fail(out, cap, "sym NAME or ADDRESS: not known", NULL);
        dbg_addr_text(d, a, out, cap);
        return 0;
    }
    if (is_cmd(&w, "where", "status")) {
        dbg_status_text(d, out, cap);
        return 0;
    }
    if (is_cmd(&w, "detach", NULL)) {
        if (dbg_detach(d)) return fail_d(d, out, cap);
        tx_s(&t, "detached: the target runs on");
        return 0;
    }
    if (is_cmd(&w, "kill", NULL)) {
        dbg_kill(d);
        tx_s(&t, "killed");
        return 0;
    }
    return fail(out, cap, "not a command: ", line);
}
