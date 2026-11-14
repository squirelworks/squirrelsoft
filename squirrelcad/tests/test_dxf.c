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

/* What other programs write: an R12 POLYLINE, a long LWPOLYLINE, 3DFACE,
 * MTEXT in pieces, *Model_Space, a nested block, an insert of a block that
 * isn't there, a bad number and an overlong line. */
static void test_other_writers(void) {
    const char *path = "build/sqc_other.dxf";
    FILE *f = fopen(path, "w");
    fprintf(f, "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n0\nLAYER\n2\nroad\n70\n0\n62\n3\n0\nENDTAB\n0\nENDSEC\n");
    fprintf(f, "0\nSECTION\n2\nBLOCKS\n");
    fprintf(f, "0\nBLOCK\n8\n0\n2\n*Model_Space\n10\n0\n20\n0\n0\nLINE\n8\n0\n10\n9\n20\n9\n11\n9\n21\n8\n0\nENDBLK\n");
    fprintf(f, "0\nBLOCK\n8\n0\n2\npost\n10\n1\n20\n1\n30\n0\n");
    fprintf(f, "0\nCIRCLE\n8\n0\n10\n1\n20\n1\n40\n0.5\n0\nENDBLK\n");
    fprintf(f, "0\nBLOCK\n8\n0\n2\ngate\n10\n0\n20\n0\n0\nINSERT\n8\n0\n2\npost\n10\n0\n20\n0\n");
    fprintf(f, "0\nINSERT\n8\n0\n2\npost\n10\n4\n20\n0\n0\nLINE\n8\n0\n10\n0\n20\n0\n11\n4\n21\n0\n0\nENDBLK\n");
    fprintf(f, "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n");
    fprintf(f, "0\nPOLYLINE\n8\nroad\n66\n1\n70\n1\n");
    fprintf(f, "0\nVERTEX\n8\nroad\n10\n0\n20\n0\n0\nVERTEX\n8\nroad\n10\n10\n20\n0\n0\nVERTEX\n8\nroad\n10\n10\n20\n5\n");
    fprintf(f, "0\nSEQEND\n");
    fprintf(f, "0\nLWPOLYLINE\n8\nroad\n90\n20\n70\n0\n");
    for (int i = 0; i < 20; i++) fprintf(f, "10\n%d\n20\n%d\n", i, i % 2);
    fprintf(f, "0\n3DFACE\n8\n0\n10\n0\n20\n0\n30\n0\n11\n1\n21\n0\n31\n0\n12\n1\n22\n1\n32\n1\n13\n1\n23\n1\n33\n1\n");
    fprintf(f, "0\nMTEXT\n8\n0\n10\n2\n20\n2\n40\n0.25\n3\nNorth \n1\nGate\n");
    fprintf(f, "0\nINSERT\n8\nroad\n2\ngate\n10\n100\n20\n0\n41\n2\n50\n90\n");
    fprintf(f, "0\nINSERT\n8\n0\n2\nnowhere\n10\n0\n20\n0\n");
    fprintf(f, "0\nLINE\n8\n0\n10\nnan\n20\n0\n11\n1\n21\n1\n");
    fprintf(f, "999\n");
    for (int i = 0; i < 600; i++) fputc('x', f);
    fprintf(f, "\n0\nLINE\n8\n0\n10\n0\n20\n0\n11\n0\n21\n7\n");
    fprintf(f, "0\nENDSEC\n0\nEOF\n");
    fclose(f);

    SqcDrawing *d = calloc(1, sizeof *d);
    sqc_drawing_init(d, "other");
    SqcDxfStats st;
    expect(sqc_import_dxf(d, path, &st) == 0, "other: import");
    expect(st.layers == 1 && st.blocks == 2, "other: road layer, post and gate (not *Model_Space)");
    expect(st.inserts == 3, "other: two posts in the gate, one gate placed");
    expect(st.faces == 1 && st.texts == 1, "other: 3DFACE and MTEXT");
    expect(st.skipped == 2, "other: the missing block and the nan line");
    expect(st.lines == 2, "other: the gate's line and the one after the long comment");
    /* the R12 polyline is closed (4 vertices), the 20-vertex one is two pieces */
    expect(st.plines == 1 + 2 + 1, "other: polyline pieces");
    int closed4 = 0, long_ok = 0;
    for (int i = 0; i < d->ent_count; i++) {
        const SqcEntity *e = &d->ents[i];
        if (e->type == SQC_ENT_PLINE && e->vert_count == 4 && e->verts[3].x == 0 && e->verts[2].y == 5) closed4 = 1;
        if (e->type == SQC_ENT_PLINE && e->vert_count == SQC_MAX_PLINE && e->verts[0].x == 0) long_ok = 1;
        if (e->type == SQC_ENT_TEXT) expect(strcmp(e->text, "North Gate") == 0 && e->scale == 0.25, "other: mtext");
    }
    expect(closed4 && long_ok, "other: polyline shapes");
    /* only the gate is left in model space next to the ground-level pieces:
       placed at 100,0, scale 2, turned 90, its posts (r 0.5 -> 1) land on
       100,0 and 100,8 */
    int posts = 0;
    for (int i = 0; i < d->ent_count; i++) {
        const SqcEntity *e = &d->ents[i];
        if (e->type == SQC_ENT_INSERT && !sqc_in_block(d, e->id)) {
            static SqcDrawing only;
            only = *d;
            only.ent_count = 0;
            for (int j = 0; j < d->ent_count; j++)
                if (sqc_in_block(d, d->ents[j].id) || d->ents[j].id == e->id) only.ents[only.ent_count++] = d->ents[j];
            SqcPoint lo, hi;
            expect(sqc_extents(&only, &lo, &hi), "other: extents");
            expect(lo.x > 98.999 && lo.x < 99.001 && hi.x > 100.999 && hi.x < 101.001, "other: gate x");
            expect(lo.y > -1.001 && lo.y < -0.999 && hi.y > 8.999 && hi.y < 9.001, "other: gate y");
            posts++;
        }
    }
    expect(posts == 1, "other: one gate placed");
    const SqcBlock *g = sqc_find_block(d, "gate");
    expect(g && g->ent_count == 3, "other: gate holds two inserts and a line");
    free(d);
    remove(path);
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

    const char *out = "build/sqc_roundtrip.dxf";
    expect(sqc_export_dxf(d, out) == 0, "export");

    SqcDrawing *d2 = (SqcDrawing *)calloc(1, sizeof(SqcDrawing));
    sqc_drawing_init(d2, "rt");
    SqcDxfStats st2;
    expect(sqc_import_dxf(d2, out, &st2) == 0, "reimport");
    expect(st2.lines >= st.lines, "roundtrip lines");
    expect(st2.circles >= 1, "roundtrip circles");

    /* Fake DWG header */
    const char *fake = "build/fake.dwg";
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
    test_other_writers();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("squirrelcad dxf tests ok");
    return 0;
}
