/* SquirrelDebug: a debugger for Squirrel OS/2 (and other i386 code) over
 * QEMU's gdbstub or a serial line. Original code. */
#define _POSIX_C_SOURCE 200809L
#include "dbggui.h"
#include "link.h"
#include "stub.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
    fputs("usage: squirreldebug [options]\n"
          "  --tcp HOST:PORT     a gdbstub over TCP (QEMU run with -gdb tcp::1234: --tcp :1234)\n"
          "  --serial DEV[@BAUD] a stub on a serial line, 8N1 (115200 when no baud is given)\n"
          "  --qemu \"CMD\"        start QEMU with CMD (\"qemu-system-i386 -fda os.img\"), stopped at its\n"
          "                      first instruction, and debug it; QEMU ends with the debugger\n"
          "  --practice          the built-in practice machine (what runs with no link given)\n"
          "  --symbols FILE      names from FILE (nm's output: ADDRESS TYPE NAME)\n"
          "  --mode 16|32        the code's mode, when QEMU's monitor cannot say\n"
          "  --do CMD            a command (no window then); give several for several:\n"
          "                      regs, x ADDR [N], dis [ADDR] [N], break ADDR, delete ADDR|all, breaks,\n"
          "                      step [N], next, until ADDR, continue, stop, set REG VALUE,\n"
          "                      write ADDR BYTE..., mode 16|32|auto, mon TEXT, sym NAME|ADDR, where,\n"
          "                      detach, kill\n"
          "  --script FILE       the same, one command a line (# for comments)\n"
          "  --wait MS           after continue, next and until, wait this long for a stop (5000)\n"
          "  --snapshot FILE     draw one 960x680 frame of the window to FILE (PPM), no window\n"
          "  --gui-frames N      close the window after N frames (testing)\n"
          "  --help              this text\n"
          "Addresses: hex numbers (0x optional), registers, pc, symbols, SEG:OFF, joined by + and -.\n"
          "Counts are decimal.\n",
          f);
}

static Dbg dbg;
static Stub stub;
static Link link_;
static DbgGui gui;
static long wait_ms = 5000;
static unsigned long out_seen;

static void print_output(void) {
    if (dbg.out_total == out_seen) return;
    unsigned long fresh = dbg.out_total - out_seen;
    size_t n = fresh < dbg.nout ? (size_t)fresh : dbg.nout;
    fwrite(dbg.out + dbg.nout - n, 1, n, stdout);
    out_seen = dbg.out_total;
}

static int word_is(const char *line, const char *w) {
    line += strspn(line, " \t");
    size_t n = strlen(w);
    return !strncmp(line, w, n) && (!line[n] || line[n] == ' ' || line[n] == '\t');
}

static int run_command(const char *where, const char *cmd) {
    static char out[16384];
    int rc = dbg_command(&dbg, cmd, out, sizeof out);
    print_output();
    if (rc) {
        fprintf(stderr, "squirreldebug: %s%s: %s\n", where, cmd, out);
        return -1;
    }
    if (out[0]) printf("%s\n", out);
    if (dbg.state == DBG_RUNNING && (word_is(cmd, "continue") || word_is(cmd, "c") || word_is(cmd, "next") ||
                                     word_is(cmd, "n") || word_is(cmd, "until") || word_is(cmd, "go"))) {
        dbg_wait(&dbg, wait_ms);
        print_output();
        dbg_status_text(&dbg, out, sizeof out);
        printf("%s\n", dbg.state == DBG_RUNNING ? "still running" : out);
    }
    fflush(stdout);
    return 0;
}

static int run_script(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "squirreldebug: cannot read %s\n", path);
        return -1;
    }
    char line[512], where[300];
    int no = 0, rc = 0;
    while (fgets(line, sizeof line, f)) {
        no++;
        line[strcspn(line, "\r\n")] = 0;
        const char *p = line + strspn(line, " \t");
        if (!*p || *p == '#') continue;
        snprintf(where, sizeof where, "%.200s:%d: ", path, no);
        if (run_command(where, p)) {
            rc = -1;
            break;
        }
    }
    fclose(f);
    return rc;
}

static int load_symbols(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "squirreldebug: cannot read %s\n", path);
        return -1;
    }
    static char text[4 << 20];
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    int k = dbg_load_symbols(&dbg, text, n);
    if (k <= 0) {
        fprintf(stderr, "squirreldebug: %s: no symbols in it (nm's output: ADDRESS TYPE NAME)\n", path);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *tcp = NULL, *serial = NULL, *qemu = NULL, *snapshot = NULL;
    int frames = 0, batch = 0, mode = 0;
    signal(SIGPIPE, SIG_IGN);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            return 0;
        } else if (!strcmp(a, "--tcp") && more) {
            tcp = argv[++i], serial = qemu = NULL;
        } else if (!strcmp(a, "--serial") && more) {
            serial = argv[++i], tcp = qemu = NULL;
        } else if (!strcmp(a, "--qemu") && more) {
            qemu = argv[++i], tcp = serial = NULL;
        } else if (!strcmp(a, "--practice")) {
            tcp = serial = qemu = NULL;
        } else if (!strcmp(a, "--symbols") && more) {
            i++;
        } else if (!strcmp(a, "--mode") && more) {
            mode = atoi(argv[++i]);
            if (mode != 16 && mode != 32) {
                fprintf(stderr, "squirreldebug: --mode %s: 16 or 32\n", argv[i]);
                return 2;
            }
        } else if ((!strcmp(a, "--do") || !strcmp(a, "--script")) && more) {
            i++;
            batch = 1;
        } else if (!strcmp(a, "--wait") && more) {
            char *end;
            wait_ms = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end || wait_ms < 0) {
                fprintf(stderr, "squirreldebug: --wait %s: milliseconds\n", argv[i]);
                return 2;
            }
        } else if (!strcmp(a, "--snapshot") && more) {
            snapshot = argv[++i];
        } else if (!strcmp(a, "--gui-frames") && more) {
            frames = atoi(argv[++i]);
        } else {
            usage(stderr);
            return 2;
        }
    }

    char err[256] = "";
    const char *what = "the practice machine";
    link_.fd = -1;
    if (tcp || serial || qemu) {
        int rc;
        if (tcp) rc = link_tcp(&link_, tcp, err, sizeof err), what = tcp;
        else if (qemu) rc = link_launch(&link_, qemu, err, sizeof err), what = "QEMU";
        else {
            char dev[256];
            long baud = 0;
            snprintf(dev, sizeof dev, "%s", serial);
            char *at = strrchr(dev, '@');
            if (at) {
                *at = 0;
                baud = atol(at + 1);
            }
            rc = link_serial(&link_, dev, baud, err, sizeof err);
            what = serial;
        }
        if (rc) {
            fprintf(stderr, "squirreldebug: %s\n", err);
            return 1;
        }
        dbg_init(&dbg, link_io(&link_));
    } else {
        stub_init(&stub);
        dbg_init(&dbg, stub_io(&stub));
        dbg_load_symbols(&dbg, stub_symbols(), strlen(stub_symbols()));
    }
    dbg.bits_forced = mode;
    if (dbg_attach(&dbg)) {
        fprintf(stderr, "squirreldebug: %s: %s\n", what, dbg.err);
        link_close(&link_);
        return 1;
    }
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--symbols") && load_symbols(argv[++i])) {
            link_close(&link_);
            return 1;
        }

    int rc = 0;
    for (int i = 1; i < argc && !rc; i++) {
        if (!strcmp(argv[i], "--tcp") || !strcmp(argv[i], "--serial") || !strcmp(argv[i], "--qemu") ||
            !strcmp(argv[i], "--symbols") || !strcmp(argv[i], "--mode") || !strcmp(argv[i], "--wait") ||
            !strcmp(argv[i], "--snapshot") || !strcmp(argv[i], "--gui-frames"))
            i++;
        else if (!strcmp(argv[i], "--do")) rc = run_command("", argv[++i]) ? 1 : 0;
        else if (!strcmp(argv[i], "--script")) rc = run_script(argv[++i]) ? 1 : 0;
    }

    if (!rc && !batch) {
        dbg_gui_init(&gui, &dbg);
        snprintf(gui.title, sizeof gui.title, "SquirrelDebug - %.60s", what);
        if (snapshot) {
            enum { W = DBG_GUI_W, H = DBG_GUI_H };
            static uint32_t px[W * H];
            sqg_canvas cv;
            sqg_canvas_init(&cv, px, W, H, W);
            sqg_ui ui;
            sqg_ui_init(&ui, NULL, 1);
            sqg_input in = {0};
            in.mx = in.my = -1;
            sqg_begin(&ui, &cv, &in);
            dbg_gui_frame(&gui, &ui);
            sqg_end(&ui);
            if (sqg_write_ppm(&cv, snapshot)) {
                fprintf(stderr, "squirreldebug: could not write %s\n", snapshot);
                rc = 1;
            }
        } else
            rc = dbg_gui_run(&gui, frames);
    }

    /* leave the target as it should be: a QEMU started here ends; another runs on */
    if (dbg.state != DBG_OFF) {
        if (link_.pid) dbg_kill(&dbg);
        else if (tcp || serial) dbg_detach(&dbg);
    }
    link_close(&link_);
    return rc;
}
