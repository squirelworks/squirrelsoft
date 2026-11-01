#include "sqs_dxf.h"
#include "sqstation.h"

#include <stdio.h>
#include <string.h>

static int failures;
static void expect(int c, const char *m) {
    if (!c) {
        fprintf(stderr, "FAIL: %s\n", m);
        failures++;
    }
}

int main(void) {
    const char *dxf = "samples/microstation_bridge.dxf";
    /* allow running from build/ or repo root */
    FILE *tryf = fopen(dxf, "r");
    if (!tryf) dxf = "squirrelstation/samples/microstation_bridge.dxf";
    else fclose(tryf);
    tryf = fopen(dxf, "r");
    if (!tryf) dxf = "../samples/microstation_bridge.dxf";
    else fclose(tryf);

    expect(sqs_file_looks_like_dxf(dxf), "sample looks like dxf");

    SqsDesign d;
    sqs_design_init(&d, "t");
    SqsDxfStats st;
    expect(sqs_import_dxf(&d, dxf, &st) == 0, "import");
    expect(st.lines >= 2, "imported lines");
    expect(st.arcs >= 1, "imported arcs/circles");
    expect(st.texts >= 1, "imported text");
    expect(d.element_count >= 5, "element count");

    /* level Survey should exist */
    int found_survey = 0;
    for (int i = 0; i < d.level_count; i++)
        if (strcmp(d.levels[i].name, "Survey") == 0) found_survey = 1;
    expect(found_survey, "Survey level");

    const char *out_dxf = "/tmp/sqs_roundtrip.dxf";
    const char *out_sq = "/tmp/sqs_from_dxf.sqdgn";
    expect(sqs_export_dxf(&d, out_dxf) == 0, "export");
    expect(sqs_write_sqdgn(&d, out_sq) == 0, "write sqdgn");

    SqsDesign d2;
    sqs_design_init(&d2, "rt");
    SqsDxfStats st2;
    expect(sqs_import_dxf(&d2, out_dxf, &st2) == 0, "re-import");
    expect(d2.element_count >= d.element_count - st.pline_segs, "roundtrip size");
    /* pline expands on import; export is lines — count should be stable-ish */
    expect(st2.lines >= st.lines, "roundtrip lines");

    /* Fake DGN-ish binary */
    const char *fake = "/tmp/fake.dgn";
    FILE *f = fopen(fake, "wb");
    unsigned char ole[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    fwrite(ole, 1, 8, f);
    fclose(f);
    expect(sqs_file_looks_like_dgn(fake), "ole dgn detect");
    expect(sqs_import_dxf(&d, fake, &st) == -2, "dgn import refused");

    if (failures) {
        fprintf(stderr, "%d dxf failure(s)\n", failures);
        return 1;
    }
    puts("squirrelstation dxf tests ok");
    return 0;
}
