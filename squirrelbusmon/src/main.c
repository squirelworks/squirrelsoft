/* SquirrelBusMon — ISM avionics data-bus analyzer. Original Soft code. */
#define _POSIX_C_SOURCE 200809L
#include "bm_catalog.h"
#include "bm_filter.h"
#include "bm_frame.h"
#include "bm_replay.h"
#include "bm_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *k_default_catalog = "fixtures/catalog/icd-catalog.ismbus";
static const char *k_default_capture = "fixtures/captures/sample-bus.ismcap";

static void usage(FILE *f) {
    fputs(
        "usage: squirrelbusmon <command> [options]\n"
        "\n"
        "  help\n"
        "  decode  [--catalog FILE] [--capture FILE]\n"
        "  filter  [--catalog FILE] [--capture FILE]\n"
        "          [--node NAME] [--msgid ID] [--topic NAME] [--bus NAME]\n"
        "  replay  [--catalog FILE] [--capture FILE] [--out FILE]\n"
        "          [same filter options as filter]\n"
        "  demo                         decode + filter + replay against fixtures\n"
        "\n"
        "Soft sample captures + ICD catalog fixtures only. Protocol SoT:\n"
        "  https://github.com/squirelworks/international-squirrel-machines\n"
        "    /blob/main/fighter-jet/interfaces/avionics-bus.yaml\n"
        "SMS/stores topics are opaque display fields (no weapon-protocol decode).\n"
        "Replay hook: docs/replay-hook.md (Flight Lab / HIL ingest).\n",
        f);
}

static const char *opt_val(int argc, char **argv, int *i) {
    if (*i + 1 >= argc) return NULL;
    (*i)++;
    return argv[*i];
}

typedef struct {
    const char *catalog;
    const char *capture;
    const char *out;
    BmFilter filter;
} BmOpts;

static int parse_opts(int argc, char **argv, BmOpts *o, int allow_out) {
    int i;
    memset(o, 0, sizeof *o);
    o->catalog = k_default_catalog;
    o->capture = k_default_capture;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--catalog") == 0) {
            o->catalog = opt_val(argc, argv, &i);
            if (!o->catalog) return 2;
        } else if (strcmp(argv[i], "--capture") == 0) {
            o->capture = opt_val(argc, argv, &i);
            if (!o->capture) return 2;
        } else if (allow_out && strcmp(argv[i], "--out") == 0) {
            o->out = opt_val(argc, argv, &i);
            if (!o->out) return 2;
        } else if (strcmp(argv[i], "--node") == 0) {
            o->filter.node = opt_val(argc, argv, &i);
            if (!o->filter.node) return 2;
        } else if (strcmp(argv[i], "--topic") == 0) {
            o->filter.topic = opt_val(argc, argv, &i);
            if (!o->filter.topic) return 2;
        } else if (strcmp(argv[i], "--bus") == 0) {
            o->filter.bus = opt_val(argc, argv, &i);
            if (!o->filter.bus) return 2;
        } else if (strcmp(argv[i], "--msgid") == 0) {
            const char *v = opt_val(argc, argv, &i);
            char *end = NULL;
            unsigned long x;
            if (!v) return 2;
            x = strtoul(v, &end, 0);
            if (!end || end == v || *end) return 2;
            o->filter.have_msgid = 1;
            o->filter.msgid = (unsigned)x;
        } else
            return 2;
    }
    return 0;
}

static int load_decoded(const BmOpts *o, BmCatalog *cat, BmCapture *cap, char *err,
                        int err_cap) {
    char *ctext = NULL, *ptext = NULL;
    if (bm_read_file(o->catalog, &ctext) != 0) {
        snprintf(err, err_cap, "cannot read catalog %s", o->catalog);
        return -1;
    }
    if (bm_catalog_parse(cat, ctext, err, err_cap) != 0) {
        free(ctext);
        return -1;
    }
    free(ctext);
    if (bm_read_file(o->capture, &ptext) != 0) {
        snprintf(err, err_cap, "cannot read capture %s", o->capture);
        return -1;
    }
    if (bm_capture_parse(cap, ptext, err, err_cap) != 0) {
        free(ptext);
        return -1;
    }
    free(ptext);
    if (bm_capture_decode(cap, cat, err, err_cap) != 0) return -1;
    return 0;
}

static void print_frames(const BmCapture *cap) {
    int i;
    char line[BM_LINE_LEN + 64];
    for (i = 0; i < cap->nframes; i++) {
        if (bm_frame_format(&cap->frames[i], line, sizeof line) > 0) puts(line);
    }
}

static int cmd_decode(int argc, char **argv) {
    BmOpts o;
    BmCatalog cat;
    BmCapture cap;
    char err[BM_MSG_LEN];
    int rc = parse_opts(argc, argv, &o, 0);
    if (rc) return rc;
    if (load_decoded(&o, &cat, &cap, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelbusmon: %s\n", err);
        return 1;
    }
    printf("catalog aircraft=%s revision=%s topics=%d\n", cat.aircraft, cat.revision,
           cat.ntopics);
    printf("decoded frames=%d\n", cap.nframes);
    print_frames(&cap);
    return 0;
}

static int cmd_filter(int argc, char **argv) {
    BmOpts o;
    BmCatalog cat;
    BmCapture cap, filtered;
    char err[BM_MSG_LEN];
    int n, rc = parse_opts(argc, argv, &o, 0);
    if (rc) return rc;
    if (load_decoded(&o, &cat, &cap, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelbusmon: %s\n", err);
        return 1;
    }
    n = bm_filter_apply(&o.filter, &cap, &filtered);
    if (n < 0) return 1;
    printf("filtered frames=%d\n", n);
    print_frames(&filtered);
    return 0;
}

static int cmd_replay(int argc, char **argv) {
    BmOpts o;
    BmCatalog cat;
    BmCapture cap, filtered;
    char err[BM_MSG_LEN];
    char *replay = NULL;
    int n, rc = parse_opts(argc, argv, &o, 1);
    if (rc) return rc;
    if (load_decoded(&o, &cat, &cap, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelbusmon: %s\n", err);
        return 1;
    }
    n = bm_filter_apply(&o.filter, &cap, &filtered);
    if (n < 0) return 1;
    if (bm_replay_build(&filtered, &replay) != 0) {
        fprintf(stderr, "squirrelbusmon: replay build failed\n");
        return 1;
    }
    if (o.out) {
        FILE *f = fopen(o.out, "wb");
        if (!f) {
            fprintf(stderr, "squirrelbusmon: cannot write %s\n", o.out);
            free(replay);
            return 1;
        }
        fputs(replay, f);
        fclose(f);
        printf("replay wrote %s frames=%d\n", o.out, n);
    } else {
        fputs(replay, stdout);
    }
    free(replay);
    return 0;
}

static int cmd_demo(void) {
    BmOpts o;
    BmCatalog cat;
    BmCapture cap, filtered;
    char err[BM_MSG_LEN];
    char *replay = NULL;
    BmFilter node_mc;

    memset(&o, 0, sizeof o);
    o.catalog = k_default_catalog;
    o.capture = k_default_capture;
    if (load_decoded(&o, &cat, &cap, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelbusmon: %s\n", err);
        return 1;
    }
    printf("demo decode frames=%d catalog_topics=%d\n", cap.nframes, cat.ntopics);
    print_frames(&cap);

    memset(&node_mc, 0, sizeof node_mc);
    node_mc.node = "MC";
    bm_filter_apply(&node_mc, &cap, &filtered);
    printf("demo filter node=MC frames=%d\n", filtered.nframes);
    print_frames(&filtered);

    if (bm_replay_build(&cap, &replay) != 0) return 1;
    if (!strstr(replay, "REPLAY v1") || !strstr(replay, "inject ")) {
        free(replay);
        fprintf(stderr, "squirrelbusmon: bad replay stream\n");
        return 1;
    }
    free(replay);
    puts("squirrelbusmon demo ok");
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
    if (strcmp(argv[1], "decode") == 0) return cmd_decode(argc - 2, argv + 2);
    if (strcmp(argv[1], "filter") == 0) return cmd_filter(argc - 2, argv + 2);
    if (strcmp(argv[1], "replay") == 0) return cmd_replay(argc - 2, argv + 2);
    if (strcmp(argv[1], "demo") == 0) return cmd_demo();
    fprintf(stderr, "squirrelbusmon: unknown command '%s'\n", argv[1]);
    usage(stderr);
    return 2;
}
