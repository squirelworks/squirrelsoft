#include "kb_cfg.h"
#include "kb_iface.h"
#include "kb_layout.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

int main(void)
{
    KbIface k;
    KbCfg cfg, cfg2;
    char err[KB_MSG_LEN];
    char buf[2048];
    const char *iface = "fixtures/keyboard-60/interface.ismhid";

    fails = 0;
    expect(kb_iface_load(iface, &k, err, sizeof(err)) == 0, "load iface");
    expect(k.rows == 5 && k.cols == 14, "geometry");
    expect(k.nkeys >= 5, "keys present");
    expect(kb_layout_at(&k, 0, 0) && strcmp(kb_layout_at(&k, 0, 0), "ESC") == 0,
           "ESC at 0,0");
    expect(kb_layout_print(&k, buf, sizeof(buf)) == 0, "print");
    expect(strstr(buf, "ESC") != NULL, "print has ESC");

    expect(kb_cfg_from_iface(&k, &cfg) == 0, "cfg from iface");
    expect(kb_cfg_write("build/test.sqkb", &cfg) == 0, "cfg write");
    expect(kb_cfg_read("build/test.sqkb", &cfg2, err, sizeof(err)) == 0, "cfg read");
    expect(kb_cfg_matches_iface(&cfg2, &k), "round-trip");

    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    printf("test_kbdcfg ok\n");
    return 0;
}
