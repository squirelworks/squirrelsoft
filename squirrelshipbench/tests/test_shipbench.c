#include "sb_aero.h"
#include "sb_board.h"
#include "sb_hydro.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *b = malloc((size_t)n + 1);
    if (!b) {
        fclose(f);
        return NULL;
    }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        fclose(f);
        return NULL;
    }
    b[n] = 0;
    fclose(f);
    return b;
}

int main(void) {
    char *h = slurp("fixtures/warship/hydrostatics.ismws");
    char *b = slurp("fixtures/warship/control-board.ismws");
    char *a = slurp("fixtures/bomber/heat-flux.ismhb");
    expect(h && b && a, "fixtures present");
    if (!h || !b || !a) return 1;

    char err[128];
    SbHydroCase hc;
    expect(sb_hydro_parse(&hc, h, err, sizeof err) == 0, err);
    SbHydroResult hr;
    sb_hydro_selftest(&hc, &hr);
    expect(hr.ok == 1, hr.summary);
    expect(strstr(hr.summary, "NR Ch.4.1") != NULL, "Simpson cite");

    SbBoardIface bi;
    expect(sb_board_parse(&bi, b, err, sizeof err) == 0, err);
    SbBoardResult br;
    sb_board_selftest(&bi, &br);
    expect(br.ok == 1, br.summary);

    /* Negative: weapon role must fail */
    snprintf(bi.ch_role[0], sizeof bi.ch_role[0], "weapon_stub");
    sb_board_selftest(&bi, &br);
    expect(br.ok == 0, "weapon role rejected");

    SbHeatFluxCase ac;
    expect(sb_aero_parse(&ac, a, err, sizeof err) == 0, err);
    SbAeroResult ar;
    sb_aero_selftest(&ac, &ar);
    expect(ar.ok == 1, ar.summary);

    free(h);
    free(b);
    free(a);
    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    puts("test_shipbench ok");
    return 0;
}
