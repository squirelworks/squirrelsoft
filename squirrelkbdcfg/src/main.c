#include "kb_cfg.h"
#include "kb_iface.h"
#include "kb_layout.h"

#include <stdio.h>
#include <string.h>

static const char *default_iface(void)
{
    return "fixtures/keyboard-60/interface.ismhid";
}

static int cmd_show(const char *iface_path)
{
    KbIface k;
    char err[KB_MSG_LEN];
    char buf[4096];
    if (kb_iface_load(iface_path, &k, err, sizeof(err)) != 0) {
        fprintf(stderr, "load: %s\n", err);
        return 1;
    }
    if (kb_layout_print(&k, buf, sizeof(buf)) != 0)
        return 1;
    fputs(buf, stdout);
    return 0;
}

static int cmd_roundtrip(const char *iface_path, const char *cfg_path)
{
    KbIface k;
    KbCfg cfg, cfg2;
    char err[KB_MSG_LEN];
    if (kb_iface_load(iface_path, &k, err, sizeof(err)) != 0) {
        fprintf(stderr, "load: %s\n", err);
        return 1;
    }
    if (kb_cfg_from_iface(&k, &cfg) != 0 || kb_cfg_write(cfg_path, &cfg) != 0) {
        fprintf(stderr, "cfg-write failed\n");
        return 1;
    }
    if (kb_cfg_read(cfg_path, &cfg2, err, sizeof(err)) != 0) {
        fprintf(stderr, "cfg-read: %s\n", err);
        return 1;
    }
    if (!kb_cfg_matches_iface(&cfg2, &k)) {
        fprintf(stderr, "round-trip mismatch\n");
        return 1;
    }
    printf("cfg round-trip ok (%d keys)\n", cfg2.nkeys);
    return 0;
}

int main(int argc, char **argv)
{
    const char *iface = default_iface();
    if (argc < 2) {
        fprintf(stderr, "usage: %s show|cfg-write|cfg-read|demo ...\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "show") == 0) {
        if (argc >= 4 && strcmp(argv[2], "--iface") == 0)
            iface = argv[3];
        return cmd_show(iface);
    }
    if (strcmp(argv[1], "cfg-write") == 0) {
        KbIface k;
        KbCfg cfg;
        char err[KB_MSG_LEN];
        const char *out = argc >= 3 ? argv[2] : "build/out.sqkb";
        if (argc >= 5 && strcmp(argv[2], "--iface") == 0) {
            iface = argv[3];
            out = argv[4];
        }
        if (kb_iface_load(iface, &k, err, sizeof(err)) != 0) {
            fprintf(stderr, "load: %s\n", err);
            return 1;
        }
        if (kb_cfg_from_iface(&k, &cfg) != 0 || kb_cfg_write(out, &cfg) != 0)
            return 1;
        printf("wrote %s\n", out);
        return 0;
    }
    if (strcmp(argv[1], "cfg-read") == 0) {
        KbCfg cfg;
        char err[KB_MSG_LEN];
        const char *in = argc >= 3 ? argv[2] : "build/out.sqkb";
        if (kb_cfg_read(in, &cfg, err, sizeof(err)) != 0) {
            fprintf(stderr, "cfg-read: %s\n", err);
            return 1;
        }
        printf("read %s layout=%s keys=%d\n", in, cfg.layout, cfg.nkeys);
        return 0;
    }
    if (strcmp(argv[1], "demo") == 0) {
        if (cmd_show(iface) != 0)
            return 1;
        return cmd_roundtrip(iface, "build/demo.sqkb");
    }
    fprintf(stderr, "unknown command\n");
    return 2;
}
