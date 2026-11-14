#include "sqs_dxf.h"
#include "sqstation.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
static void expect(int c, const char *m) {
    if (!c) {
        fprintf(stderr, "FAIL: %s\n", m);
        failures++;
    }
}

/* What other programs write: an R12 POLYLINE, a long LWPOLYLINE, a run of
 * 3DFACEs, MTEXT in pieces, *Model_Space, a block name with a space, a
 * nested block, an insert of a block that isn't there, a bad number and an
 * overlong line. */
static void test_other_writers(void) {
    const char *path = "build/sqs_other.dxf";
    FILE *f = fopen(path, "w");
    fprintf(f, "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n0\nLAYER\n2\nroad\n70\n1\n62\n-3\n0\nENDTAB\n0\nENDSEC\n");
    fprintf(f, "0\nSECTION\n2\nBLOCKS\n");
    fprintf(f, "0\nBLOCK\n8\n0\n2\n*Model_Space\n10\n0\n20\n0\n0\nLINE\n8\n0\n10\n9\n20\n9\n11\n9\n21\n8\n0\nENDBLK\n");
    fprintf(f, "0\nBLOCK\n8\n0\n2\nfence post\n10\n1\n20\n1\n30\n0\n");
    fprintf(f, "0\nCIRCLE\n8\n0\n10\n1\n20\n1\n40\n0.5\n0\nENDBLK\n");
    fprintf(f, "0\nBLOCK\n8\n0\n2\ngate\n10\n0\n20\n0\n0\nINSERT\n8\n0\n2\nfence post\n10\n0\n20\n0\n");
    fprintf(f, "0\nINSERT\n8\n0\n2\nfence post\n10\n4\n20\n0\n0\nLINE\n8\n0\n10\n0\n20\n0\n11\n4\n21\n0\n0\nENDBLK\n");
    fprintf(f, "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n");
    fprintf(f, "0\nPOLYLINE\n8\nroad\n66\n1\n70\n1\n");
    fprintf(f, "0\nVERTEX\n8\nroad\n10\n0\n20\n0\n0\nVERTEX\n8\nroad\n10\n10\n20\n0\n0\nVERTEX\n8\nroad\n10\n10\n20\n5\n");
    fprintf(f, "0\nSEQEND\n");
    fprintf(f, "0\nLWPOLYLINE\n8\nroad\n90\n30\n70\n0\n");
    for (int i = 0; i < 30; i++) fprintf(f, "10\n%d\n20\n%d\n", i, i % 2);
    /* a unit cube's six faces, sharing eight corners */
    static const int C[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
    for (int k = 0; k < 6; k++) {
        fprintf(f, "0\n3DFACE\n8\nbox\n");
        for (int j = 0; j < 4; j++) {
            int v = C[k][j];
            fprintf(f, "1%d\n%d\n2%d\n%d\n3%d\n%d\n", j, (v == 1 || v == 2 || v == 5 || v == 6), j,
                    (v == 2 || v == 3 || v == 6 || v == 7), j, v >= 4);
        }
    }
    fprintf(f, "0\nMTEXT\n8\n0\n10\n2\n20\n2\n3\nNorth \n1\nGate\n");
    fprintf(f, "0\nINSERT\n8\nroad\n2\ngate\n10\n100\n20\n0\n41\n2\n50\n90\n");
    fprintf(f, "0\nINSERT\n8\n0\n2\nnowhere\n10\n0\n20\n0\n");
    fprintf(f, "0\nLINE\n8\n0\n10\nnan\n20\n0\n11\n1\n21\n1\n");
    fprintf(f, "999\n");
    for (int i = 0; i < 600; i++) fputc('x', f);
    fprintf(f, "\n0\nLINE\n8\n0\n10\n0\n20\n0\n11\n0\n21\n7\n");
    fprintf(f, "0\nENDSEC\n0\nEOF\n");
    fclose(f);

    static SqsDesign d;
    sqs_design_init(&d, "other");
    SqsDxfStats st;
    expect(sqs_import_dxf(&d, path, &st) == 0, "other: import");
    expect(st.layers == 1 && st.cells == 2, "other: road layer, fence_post and gate (not *Model_Space)");
    expect(st.placements == 3, "other: two posts in the gate, one gate placed");
    expect(st.texts == 1 && st.circles == 1, "other: MTEXT and the post's circle");
    expect(st.skipped == 2, "other: the missing block and the nan line");
    expect(st.lines == 2, "other: the gate's line and the one after the long comment");
    expect(st.shapes == 1 && st.plines == 2, "other: the closed R12 polyline, the 30-vertex one in two");
    expect(st.meshes == 1, "other: the six faces as one mesh");
    const SqsLevel *road = sqs_level(&d, sqs_level_id(&d, "road"));
    expect(road && road->frozen && road->color == 0x00ff00, "other: road is frozen and green");
    expect(sqs_find_cell(&d, "fence_post") != NULL, "other: the space in a block name");
    for (int i = 0; i < d.element_count; i++) {
        const SqsElement *e = &d.elements[i];
        if (e->type == SQS_TEXT) expect(strcmp(e->text, "North Gate") == 0, "other: mtext");
        if (e->type == SQS_MESH) {
            expect(e->npts == 8 && e->nfaces == 6, "other: cube corners shared");
            expect(fabs(sqs_area(e) - 6) < 1e-12, "other: cube area");
        }
        if (e->type == SQS_SHAPE) expect(e->npts == 3 && e->pts[2].y == 5, "other: shape");
    }
    /* the gate, at 100,0 scale 2 turned 90: its posts (r 0.5 -> 1) on 100,0 and 100,8 */
    sqs_level_set(&d, "road", 1, 0);
    SqsSnapResult s = sqs_accu_snap(&d, (SqsPoint){100.1, 8.1, 0}, 0.5);
    expect(s.kind == SQS_SNAP_CENTER && fabs(s.p.x - 100) < 1e-9 && fabs(s.p.y - 8) < 1e-9, "other: placed post");
    remove(path);
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

    static SqsDesign d;
    sqs_design_init(&d, "t");
    SqsDxfStats st;
    expect(sqs_import_dxf(&d, dxf, &st) == 0, "import");
    expect(st.lines >= 2, "imported lines");
    expect(st.arcs + st.circles >= 1, "imported arcs/circles");
    expect(st.texts >= 1, "imported text");
    expect(d.element_count >= 5, "element count");

    /* level Survey should exist */
    int found_survey = 0;
    for (int i = 0; i < d.level_count; i++)
        if (strcmp(d.levels[i].name, "Survey") == 0) found_survey = 1;
    expect(found_survey, "Survey level");

    const char *out_dxf = "build/sqs_roundtrip.dxf";
    const char *out_sq = "build/sqs_from_dxf.sqdgn";
    expect(sqs_export_dxf(&d, out_dxf) == 0, "export");
    expect(sqs_write_sqdgn(&d, out_sq) == 0, "write sqdgn");

    static SqsDesign d2;
    sqs_design_init(&d2, "rt");
    SqsDxfStats st2;
    expect(sqs_import_dxf(&d2, out_dxf, &st2) == 0, "re-import");
    expect(d2.element_count == d.element_count, "roundtrip size");
    expect(st2.lines == st.lines && st2.plines + st2.shapes == st.plines + st.shapes, "roundtrip lines and polylines");

    /* Fake DGN-ish binary */
    const char *fake = "build/fake.dgn";
    FILE *f = fopen(fake, "wb");
    unsigned char ole[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    fwrite(ole, 1, 8, f);
    fclose(f);
    expect(sqs_file_looks_like_dgn(fake), "ole dgn detect");
    expect(sqs_import_dxf(&d, fake, &st) == -2, "dgn import refused");
    remove(out_dxf);
    remove(out_sq);
    remove(fake);

    test_other_writers();
    if (failures) {
        fprintf(stderr, "%d dxf failure(s)\n", failures);
        return 1;
    }
    puts("squirrelstation dxf tests ok");
    return 0;
}
