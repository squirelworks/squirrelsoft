#include "rm_iface.h"
#include "rm_report.h"
#include "rm_selftest.h"
#include "rm_spd.h"

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
    RmIface m;
    RmTestResult res;
    char err[RM_MSG_LEN];
    char buf[1024];
    char line[256];
    FILE *f;
    const char *iface = "fixtures/memory/interface.ismem";

    fails = 0;
    /* build/ is created by the Makefile before this test runs (Windows
     * mkdir takes one arg only; do not call POSIX mkdir(path, mode)). */

    expect(rm_iface_load(iface, &m, err, (int)sizeof(err)) == 0, "load iface");
    expect(strcmp(m.part, "LY68L6400SLIT") == 0, "part");
    expect(m.spd_i2c_addr == 0x50, "spd addr");
    expect(m.spd_len == 16, "spd len");
    expect(rm_spd_checksum_ok(&m), "spd xor");
    expect(rm_spd_print(&m, buf, (int)sizeof(buf)) == 0, "spd print");
    expect(strstr(buf, "xor=ok") != NULL, "print xor ok");

    expect(rm_selftest_run(&m, &res) == 0, "selftest run");
    expect(res.ok, "selftest pass");
    expect(res.npass > 0, "npass");
    expect(res.nfail == 0, "nfail");
    expect(strstr(res.summary, "PASS") != NULL, "summary PASS");

    expect(rm_report_write("build/test-ramtest-report.txt", &m, &res) == 0,
           "report write");
    f = fopen("build/test-ramtest-report.txt", "rb");
    expect(f != NULL, "report open");
    if (f) {
        expect(fgets(line, (int)sizeof(line), f) != NULL, "report header");
        expect(strstr(line, "SquirrelRamTest report") != NULL, "report magic");
        expect(fgets(line, (int)sizeof(line), f) != NULL, "result line");
        expect(strstr(line, "PASS") != NULL, "result PASS");
        fclose(f);
    }

    /* Soft #203: USB-CDC ISM SPD: framing from live-stub */
    expect(rm_iface_load(iface, &m, err, (int)sizeof(err)) == 0, "reload");
    expect(rm_spd_apply_cdc_path(&m, "fixtures/memory/cdc-spd-stub.sqspd", err,
                                 (int)sizeof(err)) == 0,
           "cdc stub");
    expect(m.spd_len == 16 && rm_spd_checksum_ok(&m), "cdc xor");
    expect(rm_selftest_run(&m, &res) == 0 && res.ok, "cdc selftest");

    /* Bad SPD xor must fail. */
    m.spd[m.spd_len - 1] ^= 0xffu;
    expect(!rm_spd_checksum_ok(&m), "bad xor detected");
    expect(rm_selftest_run(&m, &res) == 0, "selftest bad spd");
    expect(!res.ok, "selftest fail on bad spd");
    expect(strstr(res.summary, "FAIL") != NULL, "summary FAIL");

    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    printf("test_ramtest ok\n");
    return 0;
}
