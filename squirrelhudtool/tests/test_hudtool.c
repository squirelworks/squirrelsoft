#include "ht_export.h"
#include "ht_page.h"

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
    char *hud_txt = slurp("fixtures/pages/demo-hud.sqhud");
    char *mfd_txt = slurp("fixtures/pages/demo-mfd.sqhud");
    HtPage hud, mfd, round;
    char err[HT_MSG_LEN];
    char buf[1 << 16];
    int n, i, saw_station = 0;

    expect(hud_txt && mfd_txt, "fixtures present");
    if (!hud_txt || !mfd_txt) return 1;

    expect(ht_page_parse(&hud, hud_txt, err, (int)sizeof err) == 0, err);
    expect(hud.type == HT_PAGE_HUD, "hud type");
    expect(strcmp(hud.name, "primary-flight") == 0, "hud name");
    expect(hud.nel >= 6, "hud elements");

    for (i = 0; i < hud.nel; i++) {
        if (strcmp(hud.el[i].kind, "stores_station") == 0) {
            saw_station = 1;
            expect(hud.el[i].station >= 1, "station index");
        }
    }
    expect(saw_station, "stores station symbology present");

    expect(ht_page_parse(&mfd, mfd_txt, err, (int)sizeof err) == 0, err);
    expect(mfd.type == HT_PAGE_MFD, "mfd type");
    expect(mfd.nel >= 4, "mfd elements");

    n = ht_export_json(&hud, buf, (int)sizeof buf);
    expect(n > 0, "json export");
    expect(strstr(buf, "\"type\": \"hud\"") != NULL, "json type");
    expect(strstr(buf, "\"format\": \"sqhud\"") != NULL, "json format");
    expect(strstr(buf, "stores_station") != NULL, "json station kind");

    n = ht_export_text(&hud, buf, (int)sizeof buf);
    expect(n > 0, "text export");
    expect(ht_page_parse(&round, buf, err, (int)sizeof err) == 0, err);
    expect(round.nel == hud.nel, "round-trip element count");

    ht_page_new_hud(&hud, "scratch");
    ht_page_new_mfd(&mfd, "scratch-mfd");
    expect(hud.type == HT_PAGE_HUD && hud.nel > 0, "new-hud template");
    expect(mfd.type == HT_PAGE_MFD && mfd.nel > 0, "new-mfd template");

    free(hud_txt);
    free(mfd_txt);
    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    puts("test_hudtool ok");
    return 0;
}
