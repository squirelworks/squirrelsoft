/* SquirrelShipBench — Soft test harness for Victor warship / bomber stubs. */
#define _POSIX_C_SOURCE 200809L
#include "sb_aero.h"
#include "sb_board.h"
#include "sb_hydro.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
    fputs(
        "usage: squirrelshipbench <command> [options]\n"
        "\n"
        "  help\n"
        "  hydro-test [--iface FILE]     warship hydrostatics + Simpson I_T\n"
        "  board-test [--iface FILE]     warship scale-model control channels\n"
        "  heatflux-test [--iface FILE]  hypersonic bomber teaching heat flux\n"
        "  demo                          all against CI fixtures\n"
        "\n"
        "Soft test software only. Weapons / fire-control / seekers are out of\n"
        "scope. Hydrostatics uses Soft Simpson (NR Ch. 4.1 class). Fixtures\n"
        "mirror ISM warship/ and hypersonic-bomber/ interfaces.\n",
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

static int cmd_hydro(int argc, char **argv) {
    const char *path = "fixtures/warship/hydrostatics.ismws";
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--iface") == 0) {
            path = opt_val(argc, argv, &i);
            if (!path) return 2;
        } else return 2;
    }
    char *text = NULL, err[SB_MSG_LEN];
    if (read_file(path, &text) != 0) {
        fprintf(stderr, "squirrelshipbench: cannot read %s\n", path);
        return 1;
    }
    SbHydroCase c;
    if (sb_hydro_parse(&c, text, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelshipbench: %s\n", err);
        free(text);
        return 1;
    }
    free(text);
    SbHydroResult res;
    sb_hydro_selftest(&c, &res);
    puts(res.summary);
    return res.ok ? 0 : 1;
}

static int cmd_board(int argc, char **argv) {
    const char *path = "fixtures/warship/control-board.ismws";
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--iface") == 0) {
            path = opt_val(argc, argv, &i);
            if (!path) return 2;
        } else return 2;
    }
    char *text = NULL, err[SB_MSG_LEN];
    if (read_file(path, &text) != 0) {
        fprintf(stderr, "squirrelshipbench: cannot read %s\n", path);
        return 1;
    }
    SbBoardIface b;
    if (sb_board_parse(&b, text, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelshipbench: %s\n", err);
        free(text);
        return 1;
    }
    free(text);
    SbBoardResult res;
    sb_board_selftest(&b, &res);
    puts(res.summary);
    return res.ok ? 0 : 1;
}

static int cmd_heat(int argc, char **argv) {
    const char *path = "fixtures/bomber/heat-flux.ismhb";
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--iface") == 0) {
            path = opt_val(argc, argv, &i);
            if (!path) return 2;
        } else return 2;
    }
    char *text = NULL, err[SB_MSG_LEN];
    if (read_file(path, &text) != 0) {
        fprintf(stderr, "squirrelshipbench: cannot read %s\n", path);
        return 1;
    }
    SbHeatFluxCase c;
    if (sb_aero_parse(&c, text, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelshipbench: %s\n", err);
        free(text);
        return 1;
    }
    free(text);
    SbAeroResult res;
    sb_aero_selftest(&c, &res);
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
    if (strcmp(cmd, "hydro-test") == 0) return cmd_hydro(argc - 2, argv + 2);
    if (strcmp(cmd, "board-test") == 0) return cmd_board(argc - 2, argv + 2);
    if (strcmp(cmd, "heatflux-test") == 0) return cmd_heat(argc - 2, argv + 2);
    if (strcmp(cmd, "demo") == 0) {
        int a = cmd_hydro(0, NULL);
        int b = cmd_board(0, NULL);
        int c = cmd_heat(0, NULL);
        if (a || b || c) return 1;
        puts("squirrelshipbench demo ok");
        return 0;
    }
    fprintf(stderr, "squirrelshipbench: unknown command '%s'\n", cmd);
    usage(stderr);
    return 2;
}
