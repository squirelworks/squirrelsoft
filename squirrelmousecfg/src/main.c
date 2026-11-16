#include "ms_btn.h"
#include "ms_cfg.h"
#include "ms_dpi.h"
#include "ms_iface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *default_iface(void)
{
    return "fixtures/mouse/interface.ismhid";
}

static int load_iface(const char *path, MsIface *m)
{
    char err[MS_MSG_LEN];
    if (ms_iface_load(path, m, err, sizeof(err)) != 0) {
        fprintf(stderr, "load: %s\n", err);
        return 1;
    }
    return 0;
}

static int cmd_show(const char *iface_path)
{
    MsIface m;
    char dpi[512], btn[512];
    if (load_iface(iface_path, &m) != 0)
        return 1;
    if (ms_dpi_print(&m, dpi, sizeof(dpi)) != 0 || ms_btn_print(&m, btn, sizeof(btn)) != 0)
        return 1;
    fputs(dpi, stdout);
    fputs(btn, stdout);
    return 0;
}

static int cmd_roundtrip(MsIface *m, const char *cfg_path)
{
    MsCfg cfg, cfg2;
    char err[MS_MSG_LEN];
    if (ms_cfg_from_iface(m, &cfg) != 0 || ms_cfg_write(cfg_path, &cfg) != 0) {
        fprintf(stderr, "cfg-write failed\n");
        return 1;
    }
    if (ms_cfg_read(cfg_path, &cfg2, err, sizeof(err)) != 0) {
        fprintf(stderr, "cfg-read: %s\n", err);
        return 1;
    }
    if (!ms_cfg_matches_iface(&cfg2, m)) {
        fprintf(stderr, "round-trip mismatch\n");
        return 1;
    }
    printf("cfg round-trip ok (dpi=%d buttons=%d)\n", cfg2.active_dpi, cfg2.nmap);
    return 0;
}

int main(int argc, char **argv)
{
    const char *iface = default_iface();
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s show|dpi-set|button-map|cfg-write|cfg-read|demo ...\n",
                argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "show") == 0) {
        if (argc >= 4 && strcmp(argv[2], "--iface") == 0)
            iface = argv[3];
        return cmd_show(iface);
    }
    if (strcmp(argv[1], "dpi-set") == 0) {
        MsIface m;
        int cpi;
        if (argc < 3) {
            fprintf(stderr, "usage: %s dpi-set CPI [--iface FILE]\n", argv[0]);
            return 2;
        }
        cpi = atoi(argv[2]);
        if (argc >= 5 && strcmp(argv[3], "--iface") == 0)
            iface = argv[4];
        if (load_iface(iface, &m) != 0)
            return 1;
        if (ms_dpi_set(&m, cpi) != 0) {
            fprintf(stderr, "dpi-set failed\n");
            return 1;
        }
        printf("active_dpi=%d steps=%d\n", m.active_dpi, m.ndpi);
        return 0;
    }
    if (strcmp(argv[1], "button-map") == 0) {
        MsIface m;
        if (argc < 4) {
            fprintf(stderr, "usage: %s button-map PHYS ACT [--iface FILE]\n", argv[0]);
            return 2;
        }
        if (argc >= 6 && strcmp(argv[4], "--iface") == 0)
            iface = argv[5];
        if (load_iface(iface, &m) != 0)
            return 1;
        if (ms_btn_remap(&m, argv[2], argv[3]) != 0) {
            fprintf(stderr, "button-map failed\n");
            return 1;
        }
        printf("%s -> %s\n", argv[2], argv[3]);
        return 0;
    }
    if (strcmp(argv[1], "cfg-write") == 0) {
        MsIface m;
        MsCfg cfg;
        const char *out = argc >= 3 ? argv[2] : "build/out.sqms";
        if (argc >= 5 && strcmp(argv[2], "--iface") == 0) {
            iface = argv[3];
            out = argv[4];
        }
        if (load_iface(iface, &m) != 0)
            return 1;
        if (ms_cfg_from_iface(&m, &cfg) != 0 || ms_cfg_write(out, &cfg) != 0)
            return 1;
        printf("wrote %s\n", out);
        return 0;
    }
    if (strcmp(argv[1], "cfg-read") == 0) {
        MsCfg cfg;
        char err[MS_MSG_LEN];
        const char *in = argc >= 3 ? argv[2] : "build/out.sqms";
        if (ms_cfg_read(in, &cfg, err, sizeof(err)) != 0) {
            fprintf(stderr, "cfg-read: %s\n", err);
            return 1;
        }
        printf("read %s dpi=%d buttons=%d profile=%s\n", in, cfg.active_dpi, cfg.nmap,
               cfg.dpi_profile);
        return 0;
    }
    if (strcmp(argv[1], "demo") == 0) {
        MsIface m;
        if (cmd_show(iface) != 0)
            return 1;
        if (load_iface(iface, &m) != 0)
            return 1;
        if (ms_dpi_set(&m, 1600) != 0 || ms_btn_remap(&m, "side1", "forward") != 0) {
            fprintf(stderr, "demo edit failed\n");
            return 1;
        }
        printf("demo edit active_dpi=%d side1=%s\n", m.active_dpi, ms_btn_action(&m, "side1"));
        return cmd_roundtrip(&m, "build/demo.sqms");
    }
    fprintf(stderr, "unknown command\n");
    return 2;
}
