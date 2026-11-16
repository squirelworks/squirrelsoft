#include "ms_btn.h"
#include "ms_cfg.h"
#include "ms_dpi.h"
#include "ms_iface.h"

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
    MsIface m;
    MsCfg cfg, cfg2;
    char err[MS_MSG_LEN];
    char buf[1024];
    const char *iface = "fixtures/mouse/interface.ismhid";

    fails = 0;
    /* build/ is created by the Makefile before this test runs (Windows
     * mkdir takes one arg only; do not call POSIX mkdir(path, mode)). */

    expect(ms_iface_load(iface, &m, err, sizeof(err)) == 0, "load iface");
    expect(m.cpi_default == 800, "cpi_default");
    expect(m.ndpi >= 4, "dpi steps");
    expect(ms_dpi_has_step(&m, 800), "has 800");
    expect(m.nmap >= 5, "button map rows");
    expect(ms_btn_action(&m, "left") && strcmp(ms_btn_action(&m, "left"), "left") == 0,
           "left identity");
    expect(ms_btn_action(&m, "side1") && strcmp(ms_btn_action(&m, "side1"), "back") == 0,
           "side1 back");

    expect(ms_dpi_print(&m, buf, sizeof(buf)) == 0, "dpi print");
    expect(strstr(buf, "800") != NULL, "dpi print has 800");
    expect(ms_btn_print(&m, buf, sizeof(buf)) == 0, "btn print");
    expect(strstr(buf, "side2") != NULL, "btn print has side2");

    expect(ms_dpi_set(&m, 1600) == 0, "dpi set 1600");
    expect(m.active_dpi == 1600, "active 1600");
    expect(ms_btn_remap(&m, "side1", "forward") == 0, "remap side1");
    expect(strcmp(ms_btn_action(&m, "side1"), "forward") == 0, "side1 forward");

    expect(ms_cfg_from_iface(&m, &cfg) == 0, "cfg from iface");
    expect(ms_cfg_write("build/test.sqms", &cfg) == 0, "cfg write");
    expect(ms_cfg_read("build/test.sqms", &cfg2, err, sizeof(err)) == 0, "cfg read");
    expect(ms_cfg_matches_iface(&cfg2, &m), "round-trip");

    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    printf("test_mousecfg ok\n");
    return 0;
}
