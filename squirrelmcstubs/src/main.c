/* SquirrelMCStubs — Soft mission-computer stubs for fighter ICD.
 * Not flight software. Original Soft code. */
#define _POSIX_C_SOURCE 200809L
#include "mc_catalog.h"
#include "mc_display.h"
#include "mc_sms.h"
#include "mc_stub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
    fputs(
        "usage: squirrelmcstubs <command> [options]\n"
        "\n"
        "  help\n"
        "  catalog [--catalog FILE]     parse message catalog; list topics\n"
        "  mc-tick [--catalog FILE] [--ticks N]\n"
        "                               deterministic MC stub ticks\n"
        "  sms-tick [--safeing FILE] [--ticks N]\n"
        "                               SMC/SMS display-only ticks\n"
        "  display-tick [--catalog FILE] [--safeing FILE] [--ticks N]\n"
        "                               one stores/safeing display page\n"
        "  demo                         all roles against CI fixtures\n"
        "\n"
        "These stubs are Soft test software for Flight Lab / HIL co-check.\n"
        "They are NOT flight software. SMS/stores = display + loadout flags\n"
        "+ safeing display fields only (no guidance/seekers/propulsion/\n"
        "warheads/fuzing/arming codes). Topic names mirror Soft's copy of\n"
        "ISM interface YAML under fixtures/.\n",
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

static int parse_u(const char *s, unsigned *out) {
    char *end = NULL;
    unsigned long v;
    if (!s || !out) return -1;
    v = strtoul(s, &end, 10);
    if (!end || *end || v > 100000u) return -1;
    *out = (unsigned)v;
    return 0;
}

static int load_catalog(const char *path, McCatalog *cat) {
    char *text = NULL, err[MC_MSG_LEN];
    if (read_file(path, &text) != 0) {
        fprintf(stderr, "squirrelmcstubs: cannot read %s\n", path);
        return 1;
    }
    if (mc_catalog_parse(cat, text, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelmcstubs: catalog: %s\n", err);
        free(text);
        return 1;
    }
    free(text);
    if (mc_catalog_has_required(cat, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelmcstubs: catalog: %s\n", err);
        return 1;
    }
    return 0;
}

static int load_sms(const char *path, McSmsDisplay *sms) {
    char *text = NULL, err[MC_SMS_MSG];
    if (read_file(path, &text) != 0) {
        fprintf(stderr, "squirrelmcstubs: cannot read %s\n", path);
        return 1;
    }
    if (mc_sms_parse_display(sms, text, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelmcstubs: sms: %s\n", err);
        free(text);
        return 1;
    }
    free(text);
    return 0;
}

static int cmd_catalog(int argc, char **argv) {
    const char *path = "fixtures/message-catalog.yaml";
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--catalog") == 0) {
            path = opt_val(argc, argv, &i);
            if (!path) return 2;
        } else return 2;
    }
    McCatalog cat;
    int t;
    if (load_catalog(path, &cat) != 0) return 1;
    printf("catalog aircraft=%s topics=%d\n", cat.aircraft_id, cat.ntopics);
    for (t = 0; t < cat.ntopics; t++)
        printf("  %s\n", cat.topics[t].name);
    puts("catalog PASS");
    return 0;
}

static int cmd_mc_tick(int argc, char **argv) {
    const char *path = "fixtures/message-catalog.yaml";
    unsigned ticks = 3, t;
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--catalog") == 0) {
            path = opt_val(argc, argv, &i);
            if (!path) return 2;
        } else if (strcmp(argv[i], "--ticks") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v || parse_u(v, &ticks) != 0 || ticks < 1) return 2;
        } else return 2;
    }
    McCatalog cat;
    if (load_catalog(path, &cat) != 0) return 1;
    for (t = 1; t <= ticks; t++) {
        McStubTickResult res;
        mc_stub_tick(&cat, t, &res);
        puts(res.summary);
        if (!res.ok) return 1;
    }
    return 0;
}

static int cmd_sms_tick(int argc, char **argv) {
    const char *path = "fixtures/safeing-display.yaml";
    unsigned ticks = 3, t;
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--safeing") == 0) {
            path = opt_val(argc, argv, &i);
            if (!path) return 2;
        } else if (strcmp(argv[i], "--ticks") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v || parse_u(v, &ticks) != 0 || ticks < 1) return 2;
        } else return 2;
    }
    McSmsDisplay sms;
    if (load_sms(path, &sms) != 0) return 1;
    for (t = 1; t <= ticks; t++) {
        McSmsTickResult res;
        mc_sms_tick(&sms, t, &res);
        puts(res.summary);
        if (!res.ok) return 1;
    }
    return 0;
}

static int cmd_display_tick(int argc, char **argv) {
    const char *cpath = "fixtures/message-catalog.yaml";
    const char *spath = "fixtures/safeing-display.yaml";
    unsigned ticks = 3, t;
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--catalog") == 0) {
            cpath = opt_val(argc, argv, &i);
            if (!cpath) return 2;
        } else if (strcmp(argv[i], "--safeing") == 0) {
            spath = opt_val(argc, argv, &i);
            if (!spath) return 2;
        } else if (strcmp(argv[i], "--ticks") == 0) {
            const char *v = opt_val(argc, argv, &i);
            if (!v || parse_u(v, &ticks) != 0 || ticks < 1) return 2;
        } else return 2;
    }
    McCatalog cat;
    McSmsDisplay sms;
    if (load_catalog(cpath, &cat) != 0) return 1;
    if (load_sms(spath, &sms) != 0) return 1;
    for (t = 1; t <= ticks; t++) {
        McDisplayTickResult res;
        mc_display_tick(&cat, &sms, t, &res);
        puts(res.summary);
        if (!res.ok) return 1;
    }
    return 0;
}

static int cmd_demo(void) {
    if (cmd_catalog(0, NULL) != 0) return 1;
    if (cmd_mc_tick(0, NULL) != 0) return 1;
    if (cmd_sms_tick(0, NULL) != 0) return 1;
    if (cmd_display_tick(0, NULL) != 0) return 1;
    puts("squirrelmcstubs demo ok");
    return 0;
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
    if (strcmp(cmd, "catalog") == 0) return cmd_catalog(argc - 2, argv + 2);
    if (strcmp(cmd, "mc-tick") == 0) return cmd_mc_tick(argc - 2, argv + 2);
    if (strcmp(cmd, "sms-tick") == 0) return cmd_sms_tick(argc - 2, argv + 2);
    if (strcmp(cmd, "display-tick") == 0) return cmd_display_tick(argc - 2, argv + 2);
    if (strcmp(cmd, "demo") == 0) return cmd_demo();
    fprintf(stderr, "squirrelmcstubs: unknown command '%s'\n", cmd);
    usage(stderr);
    return 2;
}
