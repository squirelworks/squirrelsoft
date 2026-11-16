/* SquirrelISMBench — Soft HID / device bench harness. Original code. */
#define _POSIX_C_SOURCE 200809L
#include "ib_iface.h"
#include "ib_keyboard.h"
#include "ib_memory.h"
#include "ib_mouse.h"
#include "ib_hid.h"
#include "ib_plotter.h"
#include "ib_power.h"
#include "ib_printer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
    fputs(
        "usage: squirrelismbench <command> [options]\n"
        "\n"
        "  help\n"
        "  keyboard-test [--iface FILE]   matrix self-test from interface file\n"
        "  typewriter-test [--iface FILE] 4×12 typewriter matrix (ISM #4 / #159)\n"
        "  mouse-test [--iface FILE]      sensor/scroll/button self-test\n"
        "  memory-test [--iface FILE]     RAM SPD + synthetic bank patterns\n"
        "  printer-test [--iface FILE]    printer motor/paper/head self-test\n"
        "  plotter-test [--iface FILE]    plotter path + NR Ch.3.1 lerp\n"
        "  psu-test [--iface FILE]        PSU load steps + telemetry (S8.3)\n"
        "  ups-test [--iface FILE]        UPS load + capacity (S8.3)\n"
        "  hid-test ENDPOINT              Soft S8.2: Soft mock USB HID (SQHID1)\n"
        "                                 ENDPOINT=mock | fixtures/hid/*.sqhid\n"
        "                                 live /dev/hidraw* deferred\n"
        "  demo                           run all against CI fixtures\n"
        "\n"
        "Soft tests Eric's ISM consumer devices. Interfaces are SoT in\n"
        "international-squirrel-machines. CI uses fixtures until SoT lands.\n",
        f);
}

static int read_file(const char *path, char **out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long n = ftell(f);
    if (n < 0 || n > 1 << 20) {
        fclose(f);
        return -1;
    }
    rewind(f);
    char *b = malloc((size_t)n + 1);
    if (!b) {
        fclose(f);
        return -1;
    }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        fclose(f);
        return -1;
    }
    b[n] = 0;
    fclose(f);
    *out = b;
    return 0;
}

static const char *opt_val(int argc, char **argv, int *i) {
    if (*i + 1 >= argc) return NULL;
    (*i)++;
    return argv[*i];
}

static int cmd_keyboard_path(const char *path, int argc, char **argv) {
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--iface") == 0) {
            path = opt_val(argc, argv, &i);
            if (!path) return 2;
        } else return 2;
    }
    char *text = NULL, err[IB_MSG_LEN];
    if (read_file(path, &text) != 0) {
        fprintf(stderr, "squirrelismbench: cannot read %s\n", path);
        return 1;
    }
    IbKeyboardIface k;
    if (ib_keyboard_parse(&k, text, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelismbench: %s\n", err);
        free(text);
        return 1;
    }
    free(text);
    IbKeyTestResult res;
    ib_keyboard_matrix_selftest(&k, &res);
    puts(res.summary);
    return res.ok ? 0 : 1;
}

static int cmd_keyboard(int argc, char **argv) {
    return cmd_keyboard_path("fixtures/keyboard-60/interface.ismhid", argc, argv);
}

static int cmd_typewriter(int argc, char **argv) {
    return cmd_keyboard_path("fixtures/typewriter/interface.ismhid", argc, argv);
}

static int cmd_mouse(int argc, char **argv) {
    const char *path = "fixtures/mouse/interface.ismhid";
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--iface") == 0) {
            path = opt_val(argc, argv, &i);
            if (!path) return 2;
        } else return 2;
    }
    char *text = NULL, err[IB_MSG_LEN];
    if (read_file(path, &text) != 0) {
        fprintf(stderr, "squirrelismbench: cannot read %s\n", path);
        return 1;
    }
    IbMouseIface m;
    if (ib_mouse_parse(&m, text, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelismbench: %s\n", err);
        free(text);
        return 1;
    }
    free(text);
    IbMouseTestResult res;
    ib_mouse_selftest(&m, &res);
    puts(res.summary);
    return res.ok ? 0 : 1;
}

static int cmd_memory(int argc, char **argv) {
    const char *path = "fixtures/memory/interface.ismem";
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--iface") == 0) {
            path = opt_val(argc, argv, &i);
            if (!path) return 2;
        } else return 2;
    }
    char *text = NULL, err[IB_MSG_LEN];
    if (read_file(path, &text) != 0) {
        fprintf(stderr, "squirrelismbench: cannot read %s\n", path);
        return 1;
    }
    IbMemoryIface m;
    if (ib_memory_parse(&m, text, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelismbench: %s\n", err);
        free(text);
        return 1;
    }
    free(text);
    IbMemTestResult res;
    ib_memory_selftest(&m, &res);
    puts(res.summary);
    return res.ok ? 0 : 1;
}

static int cmd_printer(int argc, char **argv) {
    const char *path = "fixtures/printer/interface.ismprt";
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--iface") == 0) {
            path = opt_val(argc, argv, &i);
            if (!path) return 2;
        } else return 2;
    }
    char *text = NULL, err[IB_MSG_LEN];
    if (read_file(path, &text) != 0) {
        fprintf(stderr, "squirrelismbench: cannot read %s\n", path);
        return 1;
    }
    IbPrinterIface p;
    if (ib_printer_parse(&p, text, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelismbench: %s\n", err);
        free(text);
        return 1;
    }
    free(text);
    IbPrinterTestResult res;
    ib_printer_selftest(&p, &res);
    puts(res.summary);
    return res.ok ? 0 : 1;
}

static int cmd_plotter(int argc, char **argv) {
    const char *path = "fixtures/plotter/interface.ismplt";
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--iface") == 0) {
            path = opt_val(argc, argv, &i);
            if (!path) return 2;
        } else return 2;
    }
    char *text = NULL, err[IB_MSG_LEN];
    if (read_file(path, &text) != 0) {
        fprintf(stderr, "squirrelismbench: cannot read %s\n", path);
        return 1;
    }
    IbPlotterIface p;
    if (ib_plotter_parse(&p, text, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelismbench: %s\n", err);
        free(text);
        return 1;
    }
    free(text);
    IbPlotterTestResult res;
    ib_plotter_selftest(&p, &res);
    puts(res.summary);
    return res.ok ? 0 : 1;
}

static int cmd_power(const char *default_path, int argc, char **argv) {
    const char *path = default_path;
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--iface") == 0) {
            path = opt_val(argc, argv, &i);
            if (!path) return 2;
        } else
            return 2;
    }
    char *text = NULL, err[IB_MSG_LEN];
    if (read_file(path, &text) != 0) {
        fprintf(stderr, "squirrelismbench: cannot read %s\n", path);
        return 1;
    }
    IbPowerIface p;
    if (ib_power_parse(&p, text, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelismbench: %s\n", err);
        free(text);
        return 1;
    }
    free(text);
    IbPowerTestResult res;
    ib_power_selftest(&p, &res);
    puts(res.summary);
    return res.ok ? 0 : 1;
}

static int cmd_psu(int argc, char **argv) {
    return cmd_power("fixtures/psu/interface.ismpsu", argc, argv);
}

static int cmd_ups(int argc, char **argv) {
    return cmd_power("fixtures/ups/interface.ismups", argc, argv);
}

static int cmd_hid(int argc, char **argv) {
    if (argc < 1) {
        usage(stderr);
        return 2;
    }
    const char *endpoint = argv[0];
    IbHidSession sess;
    char err[IB_MSG_LEN];
    if (ib_hid_connect(endpoint, &sess, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelismbench: %s\n", err);
        return 1;
    }
    IbHidTestResult res;
    ib_hid_selftest(&sess, &res);
    puts(res.summary);
    char line[IB_HID_LINE];
    ib_hid_encode_hello(line, sizeof line);
    printf("frame: %s\n", line);
    ib_hid_encode_hello_ack(line, sizeof line, sess.board_id);
    printf("frame: %s\n", line);
    return res.ok ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0) {
        usage(stdout);
        return 0;
    }
    if (strcmp(cmd, "keyboard-test") == 0) return cmd_keyboard(argc - 2, argv + 2);
    if (strcmp(cmd, "typewriter-test") == 0) return cmd_typewriter(argc - 2, argv + 2);
    if (strcmp(cmd, "mouse-test") == 0) return cmd_mouse(argc - 2, argv + 2);
    if (strcmp(cmd, "memory-test") == 0) return cmd_memory(argc - 2, argv + 2);
    if (strcmp(cmd, "printer-test") == 0) return cmd_printer(argc - 2, argv + 2);
    if (strcmp(cmd, "plotter-test") == 0) return cmd_plotter(argc - 2, argv + 2);
    if (strcmp(cmd, "psu-test") == 0) return cmd_psu(argc - 2, argv + 2);
    if (strcmp(cmd, "ups-test") == 0) return cmd_ups(argc - 2, argv + 2);
    if (strcmp(cmd, "hid-test") == 0) return cmd_hid(argc - 2, argv + 2);
    if (strcmp(cmd, "demo") == 0) {
        int a = cmd_keyboard(0, NULL);
        int b = cmd_mouse(0, NULL);
        int c = cmd_typewriter(0, NULL);
        int d = cmd_memory(0, NULL);
        int e = cmd_printer(0, NULL);
        int f = cmd_plotter(0, NULL);
        int g = cmd_psu(0, NULL);
        int h = cmd_ups(0, NULL);
        char *hid_argv[] = {"mock"};
        int i = cmd_hid(1, hid_argv);
        if (a || b || c || d || e || f || g || h || i) return 1;
        puts("squirrelismbench demo ok");
        return 0;
    }
    fprintf(stderr, "squirrelismbench: unknown command '%s'\n", cmd);
    usage(stderr);
    return 2;
}
