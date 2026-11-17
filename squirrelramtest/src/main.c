#include "rm_iface.h"
#include "rm_live.h"
#include "rm_report.h"
#include "rm_selftest.h"
#include "rm_spd.h"

#include <stdio.h>
#include <string.h>

static const char *default_iface(void)
{
    return "fixtures/memory/interface.ismem";
}

static int load_iface(const char *path, RmIface *m)
{
    char err[RM_MSG_LEN];
    if (rm_iface_load(path, m, err, (int)sizeof(err)) != 0) {
        fprintf(stderr, "load: %s\n", err);
        return 1;
    }
    return 0;
}

static int apply_cdc(RmIface *m, const char *spd_cdc)
{
    char err[RM_MSG_LEN];
    if (!spd_cdc)
        return 0;
    if (rm_spd_apply_cdc_path(m, spd_cdc, err, (int)sizeof(err)) != 0) {
        fprintf(stderr, "spd-cdc: %s\n", err);
        return 1;
    }
    printf("spd-cdc: applied %d bytes from %s (xor ok)\n", m->spd_len, spd_cdc);
    return 0;
}

static int cmd_show(const char *iface_path, const char *spd_cdc)
{
    RmIface m;
    char buf[1024];
    if (load_iface(iface_path, &m) != 0)
        return 1;
    if (apply_cdc(&m, spd_cdc) != 0)
        return 1;
    if (rm_spd_print(&m, buf, (int)sizeof(buf)) != 0)
        return 1;
    fputs(buf, stdout);
    return 0;
}

static int cmd_selftest(const char *iface_path, const char *report_path,
                        const char *spd_cdc, const char *live_ep)
{
    RmIface m;
    RmTestResult res;
    if (load_iface(iface_path, &m) != 0)
        return 1;
    if (apply_cdc(&m, spd_cdc) != 0)
        return 1;
    if (live_ep) {
        RmLive live;
        char err[RM_MSG_LEN];
        if (rm_live_open(&live, live_ep, m.pattern_bytes, err,
                         (int)sizeof(err)) != 0) {
            fprintf(stderr, "live-march: %s\n", err);
            return 1;
        }
        rm_live_march(&live, &m, &res);
        puts(res.summary);
        rm_live_close(&live);
    } else {
        if (rm_selftest_run(&m, &res) != 0)
            return 1;
        puts(res.summary);
    }
    if (report_path) {
        if (rm_report_write(report_path, &m, &res) != 0) {
            fprintf(stderr, "report write failed\n");
            return 1;
        }
        printf("wrote %s\n", report_path);
    }
    return res.ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *iface = default_iface();
    const char *spd_cdc = NULL;
    const char *live_ep = NULL;
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s show|selftest|demo [--iface FILE] [--spd-cdc PATH] "
                "[--live-march ENDPOINT] [--report FILE]\n",
                argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "show") == 0) {
        int i;
        for (i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--iface") == 0 && i + 1 < argc)
                iface = argv[++i];
            else if (strcmp(argv[i], "--spd-cdc") == 0 && i + 1 < argc)
                spd_cdc = argv[++i];
        }
        return cmd_show(iface, spd_cdc);
    }
    if (strcmp(argv[1], "selftest") == 0) {
        const char *report = NULL;
        int i;
        for (i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--iface") == 0 && i + 1 < argc) {
                iface = argv[++i];
            } else if (strcmp(argv[i], "--spd-cdc") == 0 && i + 1 < argc) {
                spd_cdc = argv[++i];
            } else if (strcmp(argv[i], "--live-march") == 0 && i + 1 < argc) {
                live_ep = argv[++i];
            } else if (strcmp(argv[i], "--report") == 0 && i + 1 < argc) {
                report = argv[++i];
            }
        }
        if (!report)
            report = "build/ramtest-report.txt";
        return cmd_selftest(iface, report, spd_cdc, live_ep);
    }
    if (strcmp(argv[1], "demo") == 0) {
        if (cmd_show(iface, spd_cdc) != 0)
            return 1;
        return cmd_selftest(iface, "build/demo-ramtest-report.txt", spd_cdc,
                            NULL);
    }
    fprintf(stderr, "unknown command\n");
    return 2;
}
