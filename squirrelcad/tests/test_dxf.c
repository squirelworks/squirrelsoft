#include "sqc_dxf.h"
#include "sqcad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static void expect(int c, const char *m) {
    if (!c) {
        fprintf(stderr, "FAIL: %s\n", m);
        failures++;
    }
}

static const char *find_sample(void) {
    const char *cands[] = {
        "samples/autocad_bridge.dxf",
        "../samples/autocad_bridge.dxf",
        "squirrelcad/samples/autocad_bridge.dxf",
        NULL};
    for (int i = 0; cands[i]; i++) {
        FILE *f = fopen(cands[i], "r");
        if (f) {
            fclose(f);
            return cands[i];
        }
    }
    return cands[0];
}

int main(void) {
    const char *dxf = find_sample();
    expect(sqc_file_looks_like_dxf(dxf), "sample dxf");

    SqcDrawing *d = (SqcDrawing *)calloc(1, sizeof(SqcDrawing));
    expect(d != NULL, "alloc");
    sqc_drawing_init(d, "t");
    SqcDxfStats st;
    expect(sqc_import_dxf(d, dxf, &st) == 0, "import");
    expect(st.lines >= 2, "lines");
    expect(st.circles >= 1, "circles");
    expect(st.arcs >= 1, "arcs");
    expect(st.texts >= 1, "texts");
    expect(st.plines >= 1, "plines");
    expect(d->ent_count >= 6, "ent count");

    int walls = 0;
    for (int i = 0; i < d->layer_count; i++)
        if (strcmp(d->layers[i].name, "Walls") == 0) walls = 1;
    expect(walls, "Walls layer");

    const char *out = "/tmp/sqc_roundtrip.dxf";
    expect(sqc_export_dxf(d, out) == 0, "export");

    SqcDrawing *d2 = (SqcDrawing *)calloc(1, sizeof(SqcDrawing));
    sqc_drawing_init(d2, "rt");
    SqcDxfStats st2;
    expect(sqc_import_dxf(d2, out, &st2) == 0, "reimport");
    expect(st2.lines >= st.lines, "roundtrip lines");
    expect(st2.circles >= 1, "roundtrip circles");

    /* Fake DWG header */
    const char *fake = "/tmp/fake.dwg";
    FILE *f = fopen(fake, "wb");
    fwrite("AC1015", 1, 6, f);
    unsigned char bin[32];
    memset(bin, 0, sizeof(bin));
    bin[0] = 0x00;
    bin[3] = 0xff;
    fwrite(bin, 1, sizeof(bin), f);
    fclose(f);
    expect(sqc_file_looks_like_dwg(fake), "dwg detect");
    expect(sqc_import_dxf(d, fake, &st) == -2, "dwg refused");

    free(d);
    free(d2);
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("squirrelcad dxf tests ok");
    return 0;
}
