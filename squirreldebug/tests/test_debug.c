/* SquirrelDebug: the packets, the disassembler, the debugger against the
 * practice machine, and the window driven off screen. Usage: test_debug
 * BUILD_DIR (writes BUILD_DIR/debug.ppm) */
#include "dbggui.h"
#include "rsp.h"
#include "stub.h"
#include "x86dis.h"
#include "x86_vectors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(c) check((c), #c, __LINE__)
static void check(int ok, const char *what, int line) {
    checks++;
    if (!ok) {
        failures++;
        fprintf(stderr, "FAIL line %d: %s\n", line, what);
    }
}

/* ---- packets ---- */

static int feed(RspRx *rx, const char *s, size_t n) { /* the last event */
    int e = RSP_NONE;
    for (size_t i = 0; i < n; i++) {
        int x = rsp_rx_byte(rx, (uint8_t)s[i]);
        if (x != RSP_NONE) e = x;
    }
    return e;
}

static void test_rsp(void) {
    char f[64];
    size_t n = rsp_frame("g", 1, f, sizeof f);
    CHECK(n == 5 && !memcmp(f, "$g#67", 5));
    n = rsp_frame("OK", 2, f, sizeof f);
    CHECK(n == 6 && !memcmp(f, "$OK#9a", 6));
    n = rsp_frame("a$b#c}d*", 8, f, sizeof f); /* the four escaped bytes */
    CHECK(n == 16 && !memcmp(f, "$a}\x04" "b}\x03" "c}]d}\x0a#", 13));
    {
        char small[6];
        CHECK(rsp_frame("abc", 3, small, sizeof small) == 0);
    }
    RspRx rx;
    rsp_rx_init(&rx);
    CHECK(feed(&rx, f, n) == RSP_PACKET && rx.n == 8 && !memcmp(rx.buf, "a$b#c}d*", 8));
    CHECK(feed(&rx, "$OK#9b", 6) == RSP_BADSUM);
    CHECK(feed(&rx, "+", 1) == RSP_ACK && feed(&rx, "-", 1) == RSP_NAK && feed(&rx, "\x03", 1) == RSP_INTR);
    CHECK(feed(&rx, "junk$OK#9a", 10) == RSP_PACKET && rx.n == 2 && !strcmp((char *)rx.buf, "OK"));
    CHECK(feed(&rx, "$O$OK#9a", 8) == RSP_PACKET && rx.n == 2); /* a '$' inside starts again */
    /* run lengths: "0* " is '0' and 3 more ('*' then ' ' = 32 - 29) */
    {
        const char *p = "0* ";
        uint8_t sum = 0;
        for (const char *q = p; *q; q++) sum = (uint8_t)(sum + *q);
        char pk[32];
        snprintf(pk, sizeof pk, "$%s#%02x", p, sum);
        CHECK(feed(&rx, pk, strlen(pk)) == RSP_PACKET && rx.n == 4 && !memcmp(rx.buf, "0000", 4));
    }
    CHECK(feed(&rx, "$ab#zz", 6) == RSP_BADSUM);
    uint8_t b[4];
    char h[9] = {0};
    rsp_hex((const uint8_t *)"\x01\xab\xff\x00", 4, h);
    CHECK(!strcmp(h, "01abff00"));
    CHECK(rsp_unhex("01ABff0", 7, b, 4) == 3 && b[1] == 0xAB && b[2] == 0xFF);
    CHECK(rsp_unhex("0g11", 4, b, 4) == 0);
    const char *p = "7c00,10";
    uint32_t v;
    CHECK(!rsp_hexnum(&p, &v) && v == 0x7c00 && *p == ',');
    p = ",";
    CHECK(rsp_hexnum(&p, &v) == -1);
}

/* ---- the disassembler ---- */

static void test_x86(void) {
    int bad = 0;
    for (size_t i = 0; i < sizeof X86_VECTORS / sizeof X86_VECTORS[0]; i++) {
        const X86Vector *v = &X86_VECTORS[i];
        X86Insn in;
        int len = x86_disasm(v->code, (size_t)v->len, 0x1000, 0, v->bits, &in);
        if (len != v->len || strcmp(in.text, v->text)) {
            fprintf(stderr, "  x86 %d-bit: %s -> %s (%d bytes of %d)\n", v->bits, v->text, in.text, len, v->len);
            bad++;
        }
    }
    CHECK(bad == 0);
    X86Insn in;
    /* targets, flows, the 16-bit wrap and a segment's base */
    x86_disasm((const uint8_t *)"\xe8\x10\x00\x00\x00", 5, 0x1000, 0, 32, &in);
    CHECK(in.flow == X86_FLOW_CALL && in.has_target && in.target == 0x1015 && !strcmp(in.text, "call 0x1015"));
    x86_disasm((const uint8_t *)"\xeb\xfe", 2, 0x7c00, 0, 16, &in);
    CHECK(in.flow == X86_FLOW_JUMP && in.target == 0x7c00 && !strcmp(in.text, "jmp 0x7c00"));
    x86_disasm((const uint8_t *)"\xeb\x10", 2, 0xfff8, 0x10000, 16, &in);
    CHECK(in.target == 0x10000 + 0x000a && !strcmp(in.text, "jmp 0xa"));
    x86_disasm((const uint8_t *)"\x75\xf0", 2, 0x20, 0, 32, &in);
    CHECK(in.flow == X86_FLOW_COND && in.target == 0x12);
    x86_disasm((const uint8_t *)"\xc3", 1, 0, 0, 32, &in);
    CHECK(in.flow == X86_FLOW_RET);
    x86_disasm((const uint8_t *)"\xf4", 1, 0, 0, 32, &in);
    CHECK(in.flow == X86_FLOW_STOP);
    x86_disasm((const uint8_t *)"\xf3\xa4", 2, 0, 0, 32, &in);
    CHECK(in.rep && in.len == 2);
    x86_disasm((const uint8_t *)"\xcd\x10", 2, 0, 0, 16, &in);
    CHECK(in.flow == X86_FLOW_CALL && in.len == 2);
    /* what it does not know, or what is cut short: one byte of db */
    x86_disasm((const uint8_t *)"\x0f\xff", 2, 0, 0, 32, &in);
    CHECK(in.len == 1 && !strcmp(in.text, "db 0xf"));
    x86_disasm((const uint8_t *)"\xb8\x01\x02", 3, 0, 0, 32, &in);
    CHECK(in.len == 1 && !strcmp(in.text, "db 0xb8"));
    x86_disasm((const uint8_t *)"", 0, 0, 0, 32, &in);
    CHECK(in.len == 1);
    /* x87: the right length */
    x86_disasm((const uint8_t *)"\xd9\x45\x08", 3, 0, 0, 32, &in);
    CHECK(in.len == 3 && !strncmp(in.text, "fpu", 3));
    /* fifteen prefixes are too many */
    x86_disasm((const uint8_t *)"\x66\x66\x66\x66\x66\x66\x66\x66\x66\x66\x66\x66\x66\x66\x66\x90", 16, 0, 0, 32, &in);
    CHECK(in.len == 1);
    /* any bytes at all: a length from 1 to 15 and some text */
    uint32_t r = 12345;
    int ok = 1;
    for (int i = 0; i < 200000; i++) {
        uint8_t code[16];
        for (int k = 0; k < 16; k++) {
            r = r * 1103515245u + 12345u;
            code[k] = (uint8_t)(r >> 16);
        }
        int len = x86_disasm(code, (size_t)(r >> 8) % 17, r, 0, i & 1 ? 16 : 32, &in);
        if (len < 1 || len > 15 || !in.text[0] || in.len != len) ok = 0;
    }
    CHECK(ok);
}

/* ---- the debugger against the practice machine ---- */

static Stub st;
static Dbg d;

static void attach(void) {
    stub_init(&st);
    dbg_init(&d, stub_io(&st));
    CHECK(dbg_load_symbols(&d, stub_symbols(), strlen(stub_symbols())) == 9);
    CHECK(dbg_attach(&d) == 0);
}

static char out[16384];
static int cmd(const char *line) {
    int rc = dbg_command(&d, line, out, sizeof out);
    if (rc && getenv("VERBOSE")) fprintf(stderr, "  %s: %s\n", line, out);
    return rc;
}

static void test_dbg(void) {
    uint32_t v, off;
    attach();
    CHECK(d.state == DBG_STOPPED && d.sig == 5 && d.noack && d.monitor == 1);
    CHECK(d.reg[DBG_EIP] == 0x1000 && d.reg[DBG_CS] == 8 && dbg_pc(&d) == 0x1000);
    CHECK(d.bits == 32 && d.cr_known && d.cr0 == 0x11 && d.cs_base == 0);
    CHECK(d.ng == 8 * DBG_NREGS + 64);

    /* symbols and expressions */
    CHECK(!dbg_sym_find(&d, "square", 6, &v) && v == 0x1033);
    CHECK(!strcmp(dbg_sym_at(&d, 0x1035, &off), "square") && off == 2);
    CHECK(dbg_sym_at(&d, 0x0fff, &off) == NULL);
    CHECK(!dbg_eval(&d, "square+4", &v) && v == 0x1037);
    CHECK(!dbg_eval(&d, "0x10 + eip - 1", &v) && v == 0x100f);
    CHECK(!dbg_eval(&d, "7c0:10", &v) && v == 0x7c10);
    CHECK(!dbg_eval(&d, "cs:0", &v) && v == 0x80);
    CHECK(!dbg_eval(&d, "pc", &v) && v == 0x1000);
    CHECK(!dbg_eval(&d, "B800", &v) && v == 0xb800);
    CHECK(dbg_eval(&d, "nosuch", &v) == -1 && dbg_eval(&d, "1+", &v) == -1 && dbg_eval(&d, "123456789", &v) == -1);
    char a[96];
    dbg_addr_text(&d, 0x1012, a, sizeof a);
    CHECK(!strcmp(a, "0x00001012 <again+0x1>"));

    /* registers and memory */
    CHECK(!cmd("regs") && strstr(out, "eip 00001000") && strstr(out, "cr0 00000011") && strstr(out, "32-bit code"));
    uint8_t m[8];
    CHECK(dbg_read(&d, 0x1000, m, 5) == 5 && m[0] == 0xbc);
    CHECK(!cmd("x msg 32") && strstr(out, "Hello from the p") && strstr(out, "0000104a  48 65"));
    CHECK(!cmd("write 0x3000 de ad be ef") && !strcmp(out, "wrote 4 bytes"));
    CHECK(dbg_read(&d, 0x3000, m, 4) == 4 && m[0] == 0xde && m[3] == 0xef);
    CHECK(dbg_read(&d, 0x20000, m, 4) == -1);
    CHECK(cmd("x 0x20000") == -1 && strstr(out, "cannot read"));
    CHECK(!cmd("set eax 0x1234") && d.reg[DBG_EAX] == 0x1234 && st.reg[DBG_EAX] == 0x1234);
    st.no_p = 1; /* the whole set with 'G' */
    CHECK(!cmd("set edx 0xbeef") && d.reg[DBG_EDX] == 0xbeef && st.reg[DBG_EDX] == 0xbeef && st.reg[DBG_EAX] == 0x1234);
    st.no_p = 0;
    CHECK(cmd("set nosuch 1") == -1 && cmd("set eax zz") == -1);

    /* the code */
    CHECK(!cmd("dis start 4"));
    CHECK(strstr(out, ">  00001000  bc 00 80 00 00        mov esp, 0x8000") != NULL);
    CHECK(strstr(out, "call 0x103e  <print>") != NULL);
    CHECK(!cmd("dis pc 3") && !strncmp(out, ">  00001000", 11));

    /* stepping, over and into */
    CHECK(!cmd("step 2") && d.reg[DBG_EIP] == 0x100a && d.reg[DBG_ESP] == 0x8000 && d.reg[DBG_EBX] == 0x104a);
    CHECK(d.prev[DBG_EBX] == 0 && d.reg[DBG_EBX] != d.prev[DBG_EBX]);
    CHECK(!cmd("next") && d.state == DBG_RUNNING); /* over the call to print: runs until it returns */
    CHECK(dbg_wait(&d, 5000) == DBG_STOPPED && d.reg[DBG_EIP] == 0x100f && d.reg[DBG_ESP] == 0x8000);
    CHECK(!d.temp && st.nbp == 0);
    CHECK(strstr(d.out, "Hello from the practice machine\n") != NULL && d.out_total == 32);
    CHECK(!cmd("step") && d.reg[DBG_EIP] == 0x1011 && d.reg[DBG_ECX] == 0);
    CHECK(!cmd("step 3") && d.reg[DBG_EIP] == 0x1033 && d.reg[DBG_ESP] == 0x7ffc); /* into square */
    CHECK(!cmd("next") && d.reg[DBG_EIP] == 0x1035 && d.state == DBG_STOPPED);    /* not a call: one step */

    /* breakpoints */
    CHECK(!cmd("break again") && !strcmp(out, "breakpoint at 0x00001011 <again>") && st.nbp == 1);
    CHECK(!cmd("break again") && d.nbp == 1);
    CHECK(!cmd("continue") && d.state == DBG_RUNNING);
    CHECK(dbg_wait(&d, 5000) == DBG_STOPPED && d.reg[DBG_EIP] == 0x1011 && d.reg[DBG_ECX] == 1);
    CHECK(!cmd("continue") && dbg_wait(&d, 5000) == DBG_STOPPED && d.reg[DBG_ECX] == 2);
    uint8_t c4[4];
    CHECK(dbg_read(&d, 0x2004, c4, 4) == 4 && c4[0] == 4); /* square(2) */
    CHECK(!cmd("breaks") && !strcmp(out, "0x00001011 <again>"));
    CHECK(!cmd("delete again") && d.nbp == 0 && st.nbp == 0);
    CHECK(cmd("delete again") == -1 && strstr(out, "no breakpoint"));
    CHECK(!cmd("until square") && dbg_wait(&d, 5000) == DBG_STOPPED && d.reg[DBG_EIP] == 0x1033 && st.nbp == 0);
    CHECK(!cmd("break 0x1038") && !cmd("break print") && !cmd("delete all") && st.nbp == 0 && d.nbp == 0);

    /* running and stopping */
    CHECK(!cmd("continue") && dbg_poll(&d, 0) == DBG_RUNNING);
    CHECK(cmd("regs") == -1 && strstr(out, "running"));
    CHECK(cmd("step") == -1);
    CHECK(!cmd("stop") && d.state == DBG_STOPPED && d.sig == 2 && strstr(out, "(signal 2)"));
    CHECK(cmd("stop") == -1);
    CHECK(d.reg[DBG_EIP] >= 0x1000 && d.reg[DBG_EIP] < 0x104a);

    /* the monitor, the mode and where */
    CHECK(!cmd("mon info registers") && strstr(out, "CS =0008") && out[strlen(out) - 1] != '\n');
    CHECK(!cmd("mon nosuch") && strstr(out, "unknown command"));
    CHECK(!cmd("mode 16") && d.bits == 16 && !strcmp(out, "16-bit code (set)"));
    CHECK(!cmd("mode auto") && d.bits == 32);
    CHECK(cmd("mode 64") == -1);
    CHECK(!cmd("sym square+3") && !strcmp(out, "0x00001036 <square+0x3>"));
    CHECK(!cmd("sym") && !strcmp(out, "9 symbols"));
    CHECK(!cmd("where") && strstr(out, "stopped at 0x"));
    CHECK(cmd("frobnicate") == -1 && strstr(out, "not a command"));
    CHECK(!cmd("") && !cmd("   "));

    /* an instruction the machine does not know stops it (signal 4) */
    CHECK(!cmd("write 0x3000 0f 0b") && !cmd("set eip 0x3000") && !cmd("step") && d.sig == 4 && d.reg[DBG_EIP] == 0x3000);
    CHECK(!cmd("dis") && strstr(out, "ud2"));
    CHECK(!cmd("set eip start") && d.reg[DBG_EIP] == 0x1000);

    /* detach: the machine runs on; then the link is closed */
    CHECK(!cmd("break square") && !cmd("detach") && d.state == DBG_OFF && st.running && st.nbp == 0);
    CHECK(cmd("regs") == -1 && strstr(out, "not connected"));
    char s[64];
    dbg_status_text(&d, s, sizeof s);
    CHECK(!strcmp(s, "not connected"));

    /* kill */
    attach();
    CHECK(!cmd("kill") && d.state == DBG_OFF && st.gone);

    /* a stub that refuses packets now and then, and one that compresses its answers */
    attach();
    long before = st.packets;
    st.nak = 1;
    d.noack = st.noack = 0; /* acknowledgements back on */
    CHECK(!cmd("x 0x1000 4") && strstr(out, "bc 00 80 00"));
    CHECK(st.packets > before && st.nak == 0);
    st.nak = 9;
    CHECK(cmd("x 0x1000 4") == -1 && strstr(out, "keeps refusing"));
    st.nak = 0;
    CHECK(!cmd("write 0x4000 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 ff"));
    st.rle = 1;
    CHECK(!cmd("x 0x4000 17") && strstr(out, "00004000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00"));
    CHECK(dbg_read(&d, 0x4000, m, 1) == 1 && !cmd("regs") && d.reg[DBG_EIP] == 0x1000);
    {
        uint8_t big[4096];
        CHECK(dbg_read(&d, 0x4000, big, sizeof big) == 4096 && big[16] == 0xff && big[0] == 0);
    }
    st.rle = 0;

    /* why it stopped: W, X and console text, through dbg_request's raw path */
    attach();
    CHECK(d.timeout_ms == 3000);
    CHECK(!cmd("continue"));
    st.running = 0;
    {
        char f[32];
        size_t n = rsp_frame("W03", 3, f, sizeof f);
        memcpy(st.out + st.nout, f, n);
        st.nout += n;
    }
    CHECK(dbg_wait(&d, 1000) == DBG_EXITED && d.code == 3);
    dbg_status_text(&d, s, sizeof s);
    CHECK(!strcmp(s, "the program ended (3)"));
    CHECK(cmd("regs") == -1 && strstr(out, "ended"));

    /* an answer that never comes */
    attach();
    st.gone = 1;
    CHECK(cmd("x 0x1000") == -1 && strstr(out, "gone"));
    CHECK(d.state == DBG_OFF);

    /* symbol lists: nm's lines, with and without a type, and lines to skip */
    attach();
    d.nsym = 0;
    d.npool = 0;
    const char *nm = "00100000 T kmain\n"
                     "         U printf\n"
                     "0x00100040 kputc\n"
                     "00100020 t helper\r\n"
                     "00000005 a ABSOLUTE\n"
                     "garbage line\n"
                     "\n"
                     "00200000 B bss_start";
    CHECK(dbg_load_symbols(&d, nm, strlen(nm)) == 4);
    CHECK(d.sym[0].addr == 0x100000 && d.sym[1].addr == 0x100020 && d.sym[2].addr == 0x100040);
    CHECK(!strcmp(dbg_sym_at(&d, 0x100030, &off), "helper") && off == 0x10);
    CHECK(!dbg_sym_find(&d, "bss_start", 9, &v) && v == 0x200000 && d.sym[3].addr == 0x200000); /* the last line, with no end */
    CHECK(dbg_sym_find(&d, "printf", 6, &v) == -1 && dbg_sym_find(&d, "ABSOLUTE", 8, &v) == -1);
}

/* ---- the window ---- */

enum { W = DBG_GUI_W, H = DBG_GUI_H };
static uint32_t px[W * H];
static sqg_canvas cv;
static sqg_ui ui;
static DbgGui g;

static void frame(sqg_input *in) {
    sqg_begin(&ui, &cv, in);
    dbg_gui_frame(&g, &ui);
    sqg_end(&ui);
}

static void click(int x, int y) {
    sqg_input in = {0};
    in.mx = x;
    in.my = y;
    frame(&in);
    in.down = in.pressed = 1;
    frame(&in);
    in.down = in.pressed = 0;
    in.released = 1;
    frame(&in);
    in.released = 0;
    frame(&in);
}

static void click_rect(sqg_rect r) { click(r.x + r.w / 2, r.y + r.h / 2); }

static void type(const char *s) {
    sqg_input in = {0};
    in.mx = in.my = -1;
    snprintf(in.text, sizeof in.text, "%s", s);
    frame(&in);
    memset(in.text, 0, sizeof in.text);
    in.key = SQG_KEY_ENTER;
    frame(&in);
}

static int row_of(uint32_t addr) {
    for (int i = 0; i < g.nrows; i++)
        if (g.row_addr[i] == addr) return i;
    return -1;
}

static void test_gui(const char *build) {
    attach();
    sqg_canvas_init(&cv, px, W, H, W);
    sqg_ui_init(&ui, NULL, 1);
    dbg_gui_init(&g, &d);
    sqg_input none = {0};
    none.mx = none.my = -1;
    frame(&none);
    CHECK(g.nrows == DBG_GUI_ROWS && row_of(0x1000) == 3); /* the pc a few lines down */
    CHECK(row_of(0x1000) == 3 && strstr(g.row_text[3], "mov esp, 0x8000") != NULL);
    CHECK(g.nmem == 16 * DBG_GUI_MEMROWS);

    /* Step */
    click_rect(dbg_gui_button(&ui, DG_STEP));
    CHECK(d.reg[DBG_EIP] == 0x1005);
    /* Next over the call */
    click_rect(dbg_gui_button(&ui, DG_STEP));
    click_rect(dbg_gui_button(&ui, DG_NEXT));
    for (int i = 0; i < 200 && d.state == DBG_RUNNING; i++) frame(&none);
    CHECK(d.state == DBG_STOPPED && d.reg[DBG_EIP] == 0x100f);
    CHECK(strstr(g.log, "Hello from the practice machine") != NULL);

    /* a click in the margin sets a breakpoint; Continue stops there */
    int r = row_of(0x1033);
    CHECK(r >= 0);
    if (r >= 0) {
        sqg_rect rr = dbg_gui_row(r);
        click(rr.x + 5, rr.y + 5);
        CHECK(dbg_is_break(&d, 0x1033) && st.nbp == 1);
    }
    click_rect(dbg_gui_button(&ui, DG_CONTINUE));
    for (int i = 0; i < 200 && d.state == DBG_RUNNING; i++) frame(&none);
    CHECK(d.state == DBG_STOPPED && d.reg[DBG_EIP] == 0x1033);
    CHECK(row_of(0x1033) >= 1 && row_of(0x1033) < DBG_GUI_ROWS - 3); /* the pc stays in view */
    CHECK(strstr(g.log, "stopped at 0x00001033 <square>") != NULL);
    /* and another click there clears it */
    r = row_of(0x1033);
    if (r >= 0) {
        sqg_rect rr = dbg_gui_row(r);
        click(rr.x + 5, rr.y + 5);
        CHECK(!dbg_is_break(&d, 0x1033) && st.nbp == 0);
    }

    /* pick a line and Run to it */
    r = row_of(0x1038);
    CHECK(r >= 0);
    if (r >= 0) {
        sqg_rect rr = dbg_gui_row(r);
        click(rr.x + 100, rr.y + 5);
        CHECK(g.have_sel && g.sel == 0x1038);
    }
    click_rect(dbg_gui_button(&ui, DG_RUNTO));
    for (int i = 0; i < 200 && d.state == DBG_RUNNING; i++) frame(&none);
    CHECK(d.state == DBG_STOPPED && d.reg[DBG_EIP] == 0x1038 && st.nbp == 0);

    /* Continue, then Stop */
    click_rect(dbg_gui_button(&ui, DG_CONTINUE));
    frame(&none);
    CHECK(d.state == DBG_RUNNING && g.nrows > 0); /* the code stays shown while it runs */
    click_rect(dbg_gui_button(&ui, DG_STOP));
    CHECK(d.state == DBG_STOPPED && d.sig == 2);

    /* the command line */
    sqg_rect cr = dbg_gui_cmd_rect();
    click(cr.x + 10, cr.y + 5);
    type("break print");
    CHECK(dbg_is_break(&d, 0x103e) && strstr(g.log, "> break print") && strstr(g.log, "breakpoint at 0x0000103e <print>"));
    CHECK(g.cmd[0] == 0);
    type("nonsense");
    CHECK(strstr(g.log, "not a command: nonsense") != NULL);

    /* the memory field */
    sqg_rect mr = dbg_gui_mem_rect();
    click(mr.x + 10, mr.y + 5);
    sqg_input in = {0};
    in.mx = in.my = -1;
    in.key = SQG_KEY_BACKSPACE;
    frame(&in);
    type("msg");
    CHECK(g.mem_at == 0x104a && g.nmem > 0 && g.mem[0] == 'H');

    /* the wheel scrolls the code, and the mode can be set */
    uint32_t top = g.top;
    in = (sqg_input){0};
    in.mx = 100;
    in.my = 100;
    in.wheel = -1;
    frame(&in);
    CHECK(g.top > top && !g.follow);
    in.wheel = 1;
    frame(&in);
    CHECK(g.top < g.row_addr[3] || g.top == top);
    click_rect(dbg_gui_button(&ui, DG_STEP));
    CHECK(g.follow);

    /* Ctrl+B on the picked line, Ctrl+S steps */
    uint32_t eip = d.reg[DBG_EIP];
    in = (sqg_input){0};
    in.mx = in.my = -1;
    in.ctrl_key = 's';
    frame(&in);
    CHECK(d.reg[DBG_EIP] != eip);

    /* a picture of it, for a look */
    char path[512];
    snprintf(path, sizeof path, "%s/debug.ppm", build);
    frame(&none);
    CHECK(sqg_write_ppm(&cv, path) == 0);

    /* not connected */
    dbg_kill(&d);
    frame(&none);
    CHECK(d.state == DBG_OFF);
}

int main(int argc, char **argv) {
    const char *build = argc > 1 ? argv[1] : ".";
    test_rsp();
    test_x86();
    test_dbg();
    test_gui(build);
    printf("test_debug: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
