#include "bm_catalog.h"
#include "bm_filter.h"
#include "bm_frame.h"
#include "bm_replay.h"
#include "bm_util.h"

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

int main(void) {
    char *ctext = NULL, *ptext = NULL, err[BM_MSG_LEN];
    BmCatalog cat;
    BmCapture cap, filtered;
    char *replay = NULL;
    BmFilter f;
    const BmTopic *stores;
    int i, opaque_seen = 0, decoded_surface = 0;

    expect(bm_read_file("fixtures/catalog/icd-catalog.ismbus", &ctext) == 0,
           "catalog fixture");
    expect(bm_read_file("fixtures/captures/sample-bus.ismcap", &ptext) == 0,
           "capture fixture");
    if (!ctext || !ptext) return 1;

    expect(bm_catalog_parse(&cat, ctext, err, sizeof err) == 0, err);
    expect(cat.ntopics >= 10, "catalog topic count");
    expect(strcmp(cat.aircraft, "ISM-FJ-1") == 0, "aircraft id");
    stores = bm_catalog_find_topic(&cat, "mc.stores_config");
    expect(stores && stores->opaque == 1, "stores topic opaque");
    expect(bm_catalog_find_topic(&cat, "sms.safe_arm_discrete") != NULL, "sms topic");

    expect(bm_capture_parse(&cap, ptext, err, sizeof err) == 0, err);
    expect(cap.nframes >= 8, "capture frame count");
    expect(bm_capture_decode(&cap, &cat, err, sizeof err) == 0, err);

    for (i = 0; i < cap.nframes; i++) {
        expect(cap.frames[i].known == 1, "every sample frame in catalog");
        if (strcmp(cap.frames[i].topic, "fcc.surface_cmd") == 0) {
            expect(strstr(cap.frames[i].decode, "elev_deg=1.2") != NULL,
                   "surface decode fields");
            decoded_surface = 1;
        }
        if (cap.frames[i].opaque) {
            opaque_seen = 1;
            expect(strstr(cap.frames[i].decode, "OPAQUE") != NULL, "opaque marker");
            /* Must not invent weapon-protocol internals beyond safe display. */
            expect(strstr(cap.frames[i].decode, "fuze") == NULL, "no fuze fields");
            expect(strstr(cap.frames[i].decode, "arming_code") == NULL, "no arming codes");
        }
    }
    expect(decoded_surface, "decoded surface_cmd");
    expect(opaque_seen, "saw opaque stores/SMS frame");

    memset(&f, 0, sizeof f);
    f.node = "MC";
    expect(bm_filter_apply(&f, &cap, &filtered) > 0, "filter node=MC");
    for (i = 0; i < filtered.nframes; i++)
        expect(strcmp(filtered.frames[i].node, "MC") == 0, "filter node match");

    memset(&f, 0, sizeof f);
    f.have_msgid = 1;
    f.msgid = 0x0101;
    expect(bm_filter_apply(&f, &cap, &filtered) == 1, "filter msgid");
    expect(filtered.frames[0].msgid == 0x0101u, "msgid value");

    memset(&f, 0, sizeof f);
    f.topic = "pdu.power_status";
    expect(bm_filter_apply(&f, &cap, &filtered) == 1, "filter topic");

    expect(bm_replay_build(&cap, &replay) == 0, "replay build");
    expect(replay && strstr(replay, "REPLAY v1") != NULL, "replay header");
    expect(strstr(replay, "inject t_ms=") != NULL, "replay inject lines");
    expect(strstr(replay, "topic=fcc.surface_cmd") != NULL, "replay topic");

    free(ctext);
    free(ptext);
    free(replay);
    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    puts("test_busmon ok");
    return 0;
}
