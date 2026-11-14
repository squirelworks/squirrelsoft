/* SquirrelCAD command line, editing, plotting and interchange tests. */
#include "sqc_cmd.h"
#include "sqc_dxf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) check((c), #c, __LINE__)
static void check(int ok, const char *what, int line) {
    checks++;
    if (!ok) {
        failures++;
        fprintf(stderr, "FAIL test_cmd.c line %d: %s\n", line, what);
    }
}

static SqcSession S;
static char out[8192];

static int run(const char *line) { return sqc_exec(&S, line, out, sizeof out); }
static int ok(const char *line) {
    int r = run(line);
    if (r != SQC_CMD_OK) fprintf(stderr, "  %s -> %s\n", line, out);
    return r == SQC_CMD_OK;
}
static int err(const char *line, const char *msg) {
    int r = run(line);
    if (r != SQC_CMD_ERROR || !strstr(out, msg)) fprintf(stderr, "  %s -> %d %s\n", line, r, out);
    return r == SQC_CMD_ERROR && strstr(out, msg);
}
static int near(double a, double b) { return fabs(a - b) < 1e-9; }
static SqcEntity *ent(int id) { return sqc_find(&S.dwg, id); }

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    static char buf[1 << 18];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

static void test_drawing(void) {
    sqc_session_init(&S, NULL);
    /* points: absolute, relative, polar */
    CHECK(ok("LINE 0,0 100,0 @0,50 @50<180"));
    CHECK(strcmp(out, "ids=2..4") == 0);
    CHECK(S.dwg.ent_count == 3);
    CHECK(near(ent(3)->p2.x, 100) && near(ent(3)->p2.y, 50));
    CHECK(near(ent(4)->p2.x, 50) && near(ent(4)->p2.y, 50)); /* exactly, quarter turns */
    CHECK(ok("line 10<90 @10<0"));
    CHECK(near(ent(5)->p1.x, 0) && near(ent(5)->p1.y, 10) && near(ent(5)->p2.x, 10));
    CHECK(ok("LINE 0,0,5 1,1,5"));
    CHECK(near(ent(6)->p1.z, 5));
    CHECK(ok("CIRCLE 50,25 10") && strcmp(out, "id=7") == 0);
    CHECK(ok("ARC 0,0 10 0 90"));
    CHECK(ok("PLINE 0,0 10,0 10,10 0,10 CLOSE"));
    SqcEntity *pl = ent(S.last_id);
    CHECK(pl->vert_count == 5 && near(sqc_ent_area(pl), 100) && near(sqc_ent_length(pl), 40));
    CHECK(ok("AREA 9") && strstr(out, "area 100 ") && strstr(out, "perimeter 40"));
    CHECK(ok("AREA 7") && strstr(out, "area 314.159"));
    CHECK(ok("TEXT 5,5 Lot 12, block B   # a comment"));
    CHECK(strcmp(ent(S.last_id)->text, "Lot 12, block B") == 0);
    CHECK(ok("DIST 0,0 3,4") && strstr(out, "dist 5 ") && strstr(out, "angle 53.1301"));
    CHECK(ok("EXTENTS") && strcmp(out, "-10,-10 to 100,50") == 0);
    CHECK(ok("") && ok("   # nothing"));
    /* errors say what is wanted */
    CHECK(err("LINE 0,0", "LINE P1 P2"));
    CHECK(err("LINE 0,0 x,1", "bad point x,1"));
    CHECK(err("LINE 0,0 1,2,3,4", "bad point"));
    CHECK(err("CIRCLE 0,0 -1", "CIRCLE CENTRE RADIUS"));
    CHECK(err("ARC 0,0 1 0", "ARC CENTRE"));
    CHECK(err("PLINE 0,0", "PLINE P1 P2"));
    CHECK(err("PLINE 0,0 1,0 2,0 3,0 4,0 5,0 6,0 7,0 8,0 9,0 10,0 11,0 12,0 13,0 14,0 15,0 CLOSE", "at most 16"));
    CHECK(err("FROB", "unknown command FROB"));
    CHECK(err("AREA 2", "polyline or circle"));
    CHECK(err("TEXT 1,1", "TEXT POINT"));
    CHECK(run("help") == SQC_CMD_OK && strstr(out, "PLOT"));
    CHECK(run("quit") == SQC_CMD_QUIT);
}

static void test_layers(void) {
    sqc_session_init(&S, NULL);
    CHECK(ok("LAYER NEW walls 1"));
    CHECK(ok("LINE 0,0 10,0"));
    const SqcLayer *L = sqc_layer_of(&S.dwg, ent(S.last_id)->layer_id);
    CHECK(L && strcmp(L->name, "walls") == 0 && L->color == 1);
    CHECK(ok("LAYER SET 0") && ok("CIRCLE 0,0 1"));
    CHECK(strcmp(sqc_layer_of(&S.dwg, ent(S.last_id)->layer_id)->name, "0") == 0);
    CHECK(ok("LAYER COLOR walls 5") && sqc_layer_of(&S.dwg, ent(S.last_id - 1)->layer_id)->color == 5);
    CHECK(ok("LAYER OFF walls"));
    CHECK(ok("EXTENTS") && strcmp(out, "-1,-1 to 1,1") == 0); /* the line on walls is hidden */
    CHECK(ok("LAYER LIST") && strstr(out, "walls color 5 off") && strstr(out, "0 (current)"));
    CHECK(ok("LAYER ON walls") && ok("EXTENTS") && strcmp(out, "-1,-1 to 10,1") == 0);
    CHECK(err("LAYER NEW walls", "exists"));
    CHECK(err("LAYER SET roof", "no layer roof"));
    CHECK(err("LAYER NEW x -3", "bad color"));
    CHECK(err("LAYER", "LAYER NEW"));
}

static void test_edit(void) {
    sqc_session_init(&S, NULL);
    ok("LINE 0,0 10,0");  /* 2 */
    ok("CIRCLE 5,5 2");   /* 3 */
    ok("ARC 0,0 3 0 90"); /* 4 */
    CHECK(ok("MOVE 2 3 0,0 @100,50") && strcmp(out, "moved 2") == 0);
    CHECK(near(ent(2)->p1.x, 100) && near(ent(2)->p2.x, 110) && near(ent(2)->p1.y, 50));
    CHECK(near(ent(3)->p1.x, 105) && near(ent(3)->p1.y, 55));
    CHECK(ok("ROTATE 4 0,0 90"));
    CHECK(near(ent(4)->start_ang, 90) && near(ent(4)->end_ang, 180));
    CHECK(ok("ROTATE 2 100,50 90"));
    CHECK(near(ent(2)->p2.x, 100) && near(ent(2)->p2.y, 60)); /* exactly */
    CHECK(ok("SCALE 3 105,55 2.5") && near(ent(3)->radius, 5) && near(ent(3)->p1.x, 105));
    CHECK(ok("COPY 3 0,0 0,-10"));
    CHECK(S.dwg.ent_count == 4 && near(ent(S.last_id)->p1.y, 45) && near(ent(3)->p1.y, 55));
    CHECK(ok("UNDO") && S.dwg.ent_count == 3);
    CHECK(err("UNDO", "nothing to undo"));
    CHECK(ok("ERASE 3 4") && S.dwg.ent_count == 1);
    CHECK(err("ERASE 99", "no entity 99"));
    CHECK(err("MOVE 2 0,0", "MOVE sel FROM TO"));
    CHECK(err("MOVE 0,0 1,1", "select entities"));
    CHECK(err("SCALE 2 0,0 0", "a scale of 0"));
    CHECK(ok("COPY ALL 0,0 @1,0") && S.dwg.ent_count == 2);
    CHECK(ok("LIST") && strstr(out, "2 entities") && strstr(out, "LINE 0 100,50 100,60 len 10"));
    CHECK(ok("LIST 2") && strstr(out, "\n2 LINE"));
}

static void test_blocks(void) {
    sqc_session_init(&S, NULL);
    ok("LINE 0,0 1,0");      /* 2 */
    ok("LINE 1,0 1,2");      /* 3 */
    ok("TEXT 0,1 D");        /* 4 */
    CHECK(ok("BLOCK door 0,0 2 3 4") && strstr(out, "3 entities"));
    CHECK(ok("EXTENTS") && strcmp(out, "empty") == 0); /* the parts left model space */
    CHECK(err("ERASE 2", "inside a block"));
    CHECK(ok("LAYER NEW doors 3"));
    CHECK(ok("INSERT door 10,10 2 90"));
    int ins = S.last_id;
    CHECK(ok("EXTENTS") && strcmp(out, "6,10 to 10,12") == 0);
    CHECK(ok("INSERT door 0,0"));
    CHECK(ok("EXTENTS") && strcmp(out, "0,0 to 10,12") == 0);
    CHECK(err("INSERT window 0,0", "no block window"));
    CHECK(err("BLOCK door 0,0 2", "exists"));
    /* a block of a block */
    CHECK(ok("BLOCK pair 0,0 LAST") && ok("INSERT pair 100,0 1 180"));
    CHECK(ok("EXTENTS") && strcmp(out, "6,-2 to 100,12") == 0);
    CHECK(ok("MOVE ALL 0,0 1,0"));
    CHECK(near(ent(ins)->p1.x, 11));
}

static void test_files(void) {
    const char *path = "build/sqc_test_cmd.sqdwg";
    sqc_session_init(&S, path);
    ok("TITLE Lot_12");
    ok("LAYER NEW fence 4");
    ok("PLINE 0,0 30,0 30,20 0,20 CLOSE");
    ok("TEXT 15,10 Lot 12 (DP 1234)");
    ok("LINE 0,0 1,1");
    ok("BLOCK peg 0,0 LAST");
    ok("INSERT peg 30,20 0.5 45");
    CHECK(ok("SAVE") && strstr(out, "saved") && !S.dirty);
    SqcDrawing back;
    CHECK(sqc_read_sqdwg(&back, path) == 0);
    CHECK(back.ent_count == S.dwg.ent_count && back.block_count == 1 && strcmp(back.title, "Lot_12") == 0);
    int found = 0;
    for (int i = 0; i < back.ent_count; i++) found |= strcmp(back.ents[i].text, "Lot 12 (DP 1234)") == 0;
    CHECK(found); /* text with spaces survives */
    CHECK(sqc_line(&back, (SqcPoint){0, 0, 0}, (SqcPoint){1, 0, 0}) > S.dwg.next_id - 1); /* no reused ids */
    CHECK(ok("OPEN build/sqc_test_cmd.sqdwg") && strstr(out, "4 entities"));
    CHECK(err("OPEN build/no/such.sqdwg", "can't read"));

    /* a damaged file is refused, not overrun */
    FILE *f = fopen("build/sqc_bad.sqdwg", "w");
    fprintf(f, "SQDWG 1\nPLINE 5 1 99 0 0 0\nEND\n");
    fclose(f);
    CHECK(sqc_read_sqdwg(&back, "build/sqc_bad.sqdwg") == -2);
    f = fopen("build/sqc_bad.sqdwg", "w");
    fprintf(f, "SQDWG 1\n");
    for (int i = 0; i < SQC_MAX_ENTS + 5; i++) fprintf(f, "LINE %d 1 0 0 0 1 1 0\n", i + 10);
    fprintf(f, "END\n");
    fclose(f);
    CHECK(sqc_read_sqdwg(&back, "build/sqc_bad.sqdwg") == -2);
    f = fopen("build/sqc_bad.sqdwg", "w");
    fprintf(f, "SQDWG 1\nLINE 7 1 0 0 0 1 1 0\nEND\n");
    fclose(f);
    CHECK(sqc_read_sqdwg(&back, "build/sqc_bad.sqdwg") == 0 && back.layer_count == 1 && back.next_id > 7);

    /* DXF: layers with colours, the block, the insert */
    CHECK(ok("LAYER OFF fence") && ok("EXPORT-DXF build/sqc_test_cmd.dxf"));
    const char *dxf = slurp("build/sqc_test_cmd.dxf");
    CHECK(dxf && strstr(dxf, "0\nLAYER\n2\nfence\n70\n0\n62\n-4\n"));
    CHECK(dxf && strstr(dxf, "2\nBLOCKS\n0\nBLOCK\n8\n0\n2\npeg\n"));
    CHECK(dxf && strstr(dxf, "0\nENDBLK\n"));
    CHECK(dxf && strstr(dxf, "0\nINSERT\n8\nfence\n2\npeg\n10\n30\n20\n20\n"));
    CHECK(dxf && strstr(dxf, "41\n0.5\n42\n0.5\n43\n0.5\n50\n45\n"));
    CHECK(dxf && strstr(dxf, "0\nLWPOLYLINE\n8\nfence\n90\n4\n70\n1\n"));
    /* the block's line is in BLOCKS only */
    const char *ents = dxf ? strstr(dxf, "2\nENTITIES") : NULL;
    CHECK(ents && !strstr(ents, "0\nLINE\n"));
    static SqcDrawing imp;
    memset(&imp, 0, sizeof imp);
    SqcDxfStats st;
    CHECK(sqc_import_dxf(&imp, "build/sqc_test_cmd.dxf", &st) == 0);
    int fence_off = 0;
    for (int i = 0; i < imp.layer_count; i++)
        fence_off |= strcmp(imp.layers[i].name, "fence") == 0 && imp.layers[i].color == 4 && !imp.layers[i].on;
    CHECK(fence_off);
    CHECK(st.plines == 1 && st.texts == 1);
    /* the block comes back as a block, the insert as an insert, and the
       drawing covers the same ground */
    CHECK(st.blocks == 1 && st.inserts == 1 && st.skipped == 0);
    const SqcBlock *peg = sqc_find_block(&imp, "peg");
    CHECK(peg && peg->ent_count == 1 && sqc_in_block(&imp, peg->ent_ids[0]));
    SqcPoint lo1, hi1, lo2, hi2;
    for (int i = 0; i < S.dwg.layer_count; i++) S.dwg.layers[i].on = 1;
    for (int i = 0; i < imp.layer_count; i++) imp.layers[i].on = 1;
    CHECK(sqc_extents(&S.dwg, &lo1, &hi1) && sqc_extents(&imp, &lo2, &hi2));
    CHECK(near(lo1.x, lo2.x) && near(lo1.y, lo2.y) && near(hi1.x, hi2.x) && near(hi1.y, hi2.y));
    remove(path);
}

static void test_plot(void) {
    sqc_session_init(&S, NULL);
    ok("TITLE Site_plan");
    ok("LAYER NEW red 1");
    ok("PLINE 0,0 40,0 40,30 0,30 CLOSE");
    ok("CIRCLE 20,15 5");
    ok("ARC 20,15 8 0 90");
    ok("TEXT 2,2 A & <B>");
    SqcSheet sh;
    CHECK(sqc_sheet("a3", 1, &sh) == 0 && sh.width_mm == 420 && sh.height_mm == 297);
    CHECK(sqc_sheet("A4", 0, &sh) == 0 && sh.width_mm == 210);
    CHECK(sqc_sheet("B5", 1, &sh) == -1);
    CHECK(ok("PLOT build/sqc_plot.svg A3 1:500") && strstr(out, "on A3"));
    const char *svg = slurp("build/sqc_plot.svg");
    CHECK(svg && strstr(svg, "width=\"420mm\" height=\"297mm\""));
    CHECK(svg && strstr(svg, ">Site_plan</text>"));
    CHECK(svg && strstr(svg, "Scale 1:500  Sheet A3"));
    CHECK(svg && strstr(svg, "A &amp; &lt;B&gt;"));
    CHECK(svg && strstr(svg, "stroke=\"#e00000\""));
    /* 1:500 with metres: 2 paper mm per unit, so the 40 m side is 80 mm and the circle r 10 mm */
    CHECK(svg && strstr(svg, "r=\"10.000\""));
    CHECK(svg && strstr(svg, "<path d=\"M "));
    CHECK(ok("PLOT build/sqc_plot.svg A4 PORTRAIT"));
    svg = slurp("build/sqc_plot.svg");
    CHECK(svg && strstr(svg, "width=\"210mm\"") && strstr(svg, "Scale fit (1:"));
    CHECK(err("PLOT build/sqc_plot.svg B5", "unknown sheet B5"));
    CHECK(err("PLOT build/no/such/dir/x.svg", "can't write"));
    /* an empty drawing still plots its sheet */
    sqc_session_init(&S, NULL);
    CHECK(ok("PLOT build/sqc_plot.svg"));
    remove("build/sqc_plot.svg");
}

static void count_visit(const SqcEntity *e, const SqcLayer *L, void *ctx) {
    (void)e;
    (void)L;
    ++*(long *)ctx;
}

/* review notes on the reader and on nested inserts */
static void test_limits(void) {
    static SqcDrawing back;
    /* an id at the top of int would overflow next_id: refused */
    FILE *f = fopen("build/sqc_bad.sqdwg", "w");
    fprintf(f, "SQDWG 1\nLINE 2147483647 1 0 0 0 1 1 0\nEND\n");
    fclose(f);
    CHECK(sqc_read_sqdwg(&back, "build/sqc_bad.sqdwg") == -2);
    f = fopen("build/sqc_bad.sqdwg", "w");
    fprintf(f, "SQDWG 1\nLINE -3 1 0 0 0 1 1 0\nEND\n");
    fclose(f);
    CHECK(sqc_read_sqdwg(&back, "build/sqc_bad.sqdwg") == -2);
    remove("build/sqc_bad.sqdwg");

    /* sixteen inserts of sixteen inserts ... six deep is 16^6 lines: the
       visit stops at its budget instead of running for minutes */
    sqc_session_init(&S, NULL);
    CHECK(ok("LINE 0,0 1,0") && ok("BLOCK b0 0,0 LAST"));
    char cmd[64];
    for (int k = 1; k <= 6; k++) {
        for (int i = 0; i < 16; i++) {
            snprintf(cmd, sizeof cmd, "INSERT b%d %d,0", k - 1, i);
            CHECK(ok(cmd));
        }
        snprintf(cmd, sizeof cmd, "BLOCK b%d 0,0 ALL", k);
        CHECK(ok(cmd));
    }
    CHECK(ok("INSERT b6 0,0"));
    long n = 0;
    sqc_visit(&S.dwg, count_visit, &n);
    CHECK(n > 1000 && n <= 200000);
}

int main(void) {
    test_drawing();
    test_layers();
    test_edit();
    test_blocks();
    test_files();
    test_plot();
    test_limits();
    printf("test_cmd: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
