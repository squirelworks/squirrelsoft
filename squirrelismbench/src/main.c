/* SquirrelISMBench — Soft HID / device bench harness. Original code. */
#define _POSIX_C_SOURCE 200809L
#include "ib_iface.h"
#include "ib_keyboard.h"
#include "ib_mouse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
    fputs(
        "usage: squirrelismbench <command> [options]\n"
        "\n"
        "  help\n"
        "  keyboard-test [--iface FILE]   matrix self-test from interface file\n"
        "  mouse-test [--iface FILE]      sensor/scroll/button self-test\n"
        "  demo                           run both against CI fixtures\n"
        "\n"
        "Soft tests Eric's ISM consumer devices. Interfaces are SoT in\n"
        "international-squirrel-machines (keyboard/60-percent, mouse). CI uses\n"
        "fixtures until Eric publishes (ISM #89 / #90). Live USB HID is planned.\n",
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

static int cmd_keyboard(int argc, char **argv) {
    const char *path = "fixtures/keyboard-60/interface.ismhid";
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
    if (strcmp(cmd, "mouse-test") == 0) return cmd_mouse(argc - 2, argv + 2);
    if (strcmp(cmd, "demo") == 0) {
        int a = cmd_keyboard(0, NULL);
        int b = cmd_mouse(0, NULL);
        if (a || b) return 1;
        puts("squirrelismbench demo ok");
        return 0;
    }
    fprintf(stderr, "squirrelismbench: unknown command '%s'\n", cmd);
    usage(stderr);
    return 2;
}
