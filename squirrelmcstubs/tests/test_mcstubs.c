#include "mc_catalog.h"
#include "mc_display.h"
#include "mc_sms.h"
#include "mc_stub.h"

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
    long n;
    char *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    b = malloc((size_t)n + 1);
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
    char *ct = slurp("fixtures/message-catalog.yaml");
    char *st = slurp("fixtures/safeing-display.yaml");
    McCatalog cat;
    McSmsDisplay sms;
    McStubTickResult mr;
    McSmsTickResult sr;
    McDisplayTickResult dr;
    char err[160];

    expect(ct && st, "fixtures present");
    if (!ct || !st) return 1;

    expect(mc_catalog_parse(&cat, ct, err, sizeof err) == 0, err);
    expect(cat.ntopics >= 11, "topic count from avionics-bus + sms");
    expect(mc_catalog_find(&cat, "mc.stores_config") >= 0, "mc.stores_config");
    expect(mc_catalog_find(&cat, "sms.safe_arm_discrete") >= 0, "sms.safe_arm_discrete");
    expect(mc_catalog_find(&cat, "mc.stores_release_cmd") >= 0, "release topic name listed");
    expect(mc_catalog_has_required(&cat, err, sizeof err) == 0, err);

    expect(mc_sms_parse_display(&sms, st, err, sizeof err) == 0, err);
    expect(sms.nstations == 9, "nine stations");
    expect(sms.safeing.live_fuzing_in_repo == 0, "no live fuzing");
    expect(strcmp(sms.safeing.safe_arm_discrete, "safe") == 0, "safe_arm display");

    mc_stub_tick(&cat, 1, &mr);
    expect(mr.ok == 1, mr.summary);
    mc_sms_tick(&sms, 1, &sr);
    expect(sr.ok == 1, sr.summary);
    mc_display_tick(&cat, &sms, 1, &dr);
    expect(dr.ok == 1, dr.summary);

    {
        McStubTickResult mr2;
        mc_stub_tick(&cat, 1, &mr2);
        expect(strcmp(mr.summary, mr2.summary) == 0, "MC tick deterministic");
    }

    /* Weapons boundary: live_fuzing_in_repo true must be rejected. */
    {
        const char *bad =
            "station_loadout_flags:\n"
            "  STA-1: empty\n"
            "safeing:\n"
            "  live_fuzing_in_repo: true\n"
            "  safe_arm_discrete: safe\n";
        McSmsDisplay bad_sms;
        expect(mc_sms_parse_display(&bad_sms, bad, err, sizeof err) != 0,
               "reject live_fuzing_in_repo true");
        expect(strstr(err, "live_fuzing") != NULL, "fuzing reject message");
    }

    /* Display-only loadout enum - reject inventing weapon state words. */
    {
        const char *bad =
            "station_loadout_flags:\n"
            "  STA-1: armed\n"
            "safeing:\n"
            "  live_fuzing_in_repo: false\n";
        McSmsDisplay bad_sms;
        expect(mc_sms_parse_display(&bad_sms, bad, err, sizeof err) != 0,
               "reject non-display loadout flag");
    }

    free(ct);
    free(st);
    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    puts("test_mcstubs ok");
    return 0;
}
