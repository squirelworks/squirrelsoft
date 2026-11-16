/* SquirrelHUDTool — Soft symbology / page layout CLI. Original code. */
#define _POSIX_C_SOURCE 200809L
#include "ht_export.h"
#include "ht_page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OUT_CAP (1 << 16)

static void usage(FILE *f) {
    fputs(
        "usage: squirrelhudtool <command> [options]\n"
        "\n"
        "  help\n"
        "  new-hud [--name NAME] [--out FILE]   author a primary HUD page\n"
        "  new-mfd [--name NAME] [--out FILE]   author an MFD-style page\n"
        "  export --in FILE [--format json|text] [--out FILE]\n"
        "  demo                                author HUD+MFD fixtures and export JSON\n"
        "\n"
        "Soft symbology tooling for ISM fighter HUD/HMI (ISM #82). Display pages\n"
        "only; stores station flags are symbology, not weapons internals.\n"
        "Requirements note:\n"
        "https://github.com/squirelworks/international-squirrel-machines/blob/main/fighter-jet/avionics/hmi-hud.md\n",
        f);
}

static int read_file(const char *path, char **out) {
    FILE *f = fopen(path, "rb");
    long n;
    char *b;
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    n = ftell(f);
    if (n < 0 || n > 1 << 20) {
        fclose(f);
        return -1;
    }
    rewind(f);
    b = malloc((size_t)n + 1);
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

static int write_or_print(const char *path, const char *buf) {
    if (path) {
        if (ht_write_file(path, buf) != 0) {
            fprintf(stderr, "squirrelhudtool: cannot write %s\n", path);
            return 1;
        }
        printf("wrote %s\n", path);
        return 0;
    }
    fputs(buf, stdout);
    return 0;
}

static int cmd_new(int is_mfd, int argc, char **argv) {
    const char *name = NULL;
    const char *out = NULL;
    HtPage page;
    char buf[OUT_CAP];
    int i, n;

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--name") == 0) {
            name = opt_val(argc, argv, &i);
            if (!name) return 2;
        } else if (strcmp(argv[i], "--out") == 0) {
            out = opt_val(argc, argv, &i);
            if (!out) return 2;
        } else {
            return 2;
        }
    }
    if (is_mfd) ht_page_new_mfd(&page, name);
    else ht_page_new_hud(&page, name);
    n = ht_export_text(&page, buf, (int)sizeof buf);
    if (n < 0) {
        fprintf(stderr, "squirrelhudtool: export failed\n");
        return 1;
    }
    return write_or_print(out, buf);
}

static int cmd_export(int argc, char **argv) {
    const char *in = NULL;
    const char *out = NULL;
    const char *fmt = "json";
    char *text = NULL;
    HtPage page;
    char err[HT_MSG_LEN];
    char buf[OUT_CAP];
    int i, n;

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--in") == 0) {
            in = opt_val(argc, argv, &i);
            if (!in) return 2;
        } else if (strcmp(argv[i], "--out") == 0) {
            out = opt_val(argc, argv, &i);
            if (!out) return 2;
        } else if (strcmp(argv[i], "--format") == 0) {
            fmt = opt_val(argc, argv, &i);
            if (!fmt) return 2;
        } else {
            return 2;
        }
    }
    if (!in) {
        fprintf(stderr, "squirrelhudtool: export needs --in FILE\n");
        return 2;
    }
    if (read_file(in, &text) != 0) {
        fprintf(stderr, "squirrelhudtool: cannot read %s\n", in);
        return 1;
    }
    if (ht_page_parse(&page, text, err, (int)sizeof err) != 0) {
        fprintf(stderr, "squirrelhudtool: %s\n", err);
        free(text);
        return 1;
    }
    free(text);
    if (strcmp(fmt, "json") == 0) n = ht_export_json(&page, buf, (int)sizeof buf);
    else if (strcmp(fmt, "text") == 0) n = ht_export_text(&page, buf, (int)sizeof buf);
    else {
        fprintf(stderr, "squirrelhudtool: format must be json or text\n");
        return 2;
    }
    if (n < 0) {
        fprintf(stderr, "squirrelhudtool: export failed\n");
        return 1;
    }
    return write_or_print(out, buf);
}

static int cmd_demo(void) {
    HtPage hud, mfd;
    char buf[OUT_CAP];
    int n;

    ht_page_new_hud(&hud, "primary-flight");
    ht_page_new_mfd(&mfd, "systems-synoptic");

    n = ht_export_json(&hud, buf, (int)sizeof buf);
    if (n < 0) return 1;
    fputs("--- HUD JSON ---\n", stdout);
    fputs(buf, stdout);

    n = ht_export_json(&mfd, buf, (int)sizeof buf);
    if (n < 0) return 1;
    fputs("--- MFD JSON ---\n", stdout);
    fputs(buf, stdout);

    puts("squirrelhudtool demo ok");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        usage(stdout);
        return 0;
    }
    if (strcmp(argv[1], "new-hud") == 0) return cmd_new(0, argc - 2, argv + 2);
    if (strcmp(argv[1], "new-mfd") == 0) return cmd_new(1, argc - 2, argv + 2);
    if (strcmp(argv[1], "export") == 0) return cmd_export(argc - 2, argv + 2);
    if (strcmp(argv[1], "demo") == 0) return cmd_demo();
    fprintf(stderr, "squirrelhudtool: unknown command '%s'\n", argv[1]);
    usage(stderr);
    return 2;
}
