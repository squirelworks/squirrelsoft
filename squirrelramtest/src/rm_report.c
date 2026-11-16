#include "rm_report.h"
#include "rm_spd.h"

#include <stdio.h>

int rm_report_write(const char *path, const RmIface *m, const RmTestResult *res)
{
    FILE *f;

    if (!path || !m || !res)
        return -1;
    f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "SquirrelRamTest report 1\n");
    fprintf(f, "result: %s\n", res->ok ? "PASS" : "FAIL");
    fprintf(f, "part: %s\n", m->part);
    fprintf(f, "iface: %s\n", m->path[0] ? m->path : "(memory)");
    fprintf(f, "spd_i2c: 0x%02x\n", m->spd_i2c_addr);
    fprintf(f, "spd_xor: %s\n", rm_spd_checksum_ok(m) ? "ok" : "FAIL");
    fprintf(f, "npass: %d\n", res->npass);
    fprintf(f, "nfail: %d\n", res->nfail);
    fprintf(f, "summary: %s\n", res->summary);
    fprintf(f, "note: fixture mode until live I2C SPD / PSRAM (S8.4b)\n");
    fclose(f);
    return 0;
}
