/* SquirrelStation: the element graph, levels, cells, references, snaps,
 * keyins and both file formats. */
#include "sqs_dxf.h"
#include "sqstation.h"

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
        fprintf(stderr, "FAIL test_elements.c line %d: %s\n", line, what);
    }
}
static int near(double a, double b, double tol) { return fabs(a - b) <= tol; }
static int same(SqsPoint a, SqsPoint b) { return sqs_dist(a, b) < 1e-9; }
#define P(x, y, z) ((SqsPoint){x, y, z})
#define PI 3.14159265358979323846

static SqsDesign d, d2, r0;

static void test_elements(void) {
    sqs_design_init(&d, "elements");
    int L = d.levels[0].id;
    SqsPoint sq[4] = {P(0, 0, 0), P(10, 0, 0), P(10, 5, 0), P(0, 5, 0)};
    int shape = sqs_add_shape(&d, L, sq, 4);
    CHECK(shape > 0 && near(sqs_area(sqs_find(&d, shape)), 50, 1e-12) && near(sqs_length(sqs_find(&d, shape)), 30, 1e-12));
    SqsPoint closed[5] = {P(0, 0, 0), P(1, 0, 0), P(1, 1, 0), P(0, 1, 0), P(0, 0, 0)};
    int sh2 = sqs_add_shape(&d, L, closed, 5);
    CHECK(sh2 > 0 && sqs_find(&d, sh2)->npts == 4);
    int pl = sqs_add_pline(&d, L, sq, 3);
    CHECK(pl > 0 && near(sqs_length(sqs_find(&d, pl)), 15, 1e-12) && sqs_area(sqs_find(&d, pl)) == 0);
    SqsPoint cp[3] = {P(0, 0, 0), P(5, 5, 0), P(10, 0, 0)};
    int cv = sqs_add_curve(&d, L, cp, 3);
    const SqsElement *c = sqs_find(&d, cv);
    CHECK(same(sqs_curve_at(c, 0), cp[0]) && same(sqs_curve_at(c, 0.5), cp[1]) && same(sqs_curve_at(c, 1), cp[2]));
    CHECK(sqs_curve_at(c, 0.25).y > 2.5); /* bulges out past the chord */
    double len = sqs_length(c);
    CHECK(len > 2 * sqrt(50) && len < 16);
    SqsPoint two[2] = {P(0, 0, 0), P(3, 4, 0)};
    int cv2 = sqs_add_curve(&d, L, two, 2);
    CHECK(near(sqs_length(sqs_find(&d, cv2)), 5, 1e-9)); /* two points: a straight curve */
    SqsPoint tet[4] = {P(0, 0, 0), P(1, 0, 0), P(0, 1, 0), P(0, 0, 1)};
    const uint8_t faces[4][4] = {{0, 2, 1, 1}, {0, 1, 3, 3}, {0, 3, 2, 2}, {1, 2, 3, 3}};
    int mesh = sqs_add_mesh(&d, L, tet, 4, faces, 4);
    CHECK(mesh > 0 && near(sqs_area(sqs_find(&d, mesh)), 1.5 + sqrt(3) / 2, 1e-12));
    const uint8_t badf[1][4] = {{0, 1, 9, 9}};
    CHECK(sqs_add_mesh(&d, L, tet, 4, badf, 1) == -1);
    int box = sqs_add_solid(&d, L, SQS_SOLID_BOX, P(1, 2, 3), P(2, 3, 4));
    CHECK(near(sqs_volume(sqs_find(&d, box)), 24, 1e-12) && near(sqs_area(sqs_find(&d, box)), 52, 1e-12));
    int cyl = sqs_add_solid(&d, L, SQS_SOLID_CYLINDER, P(0, 0, 0), P(1, 0, 2));
    CHECK(near(sqs_volume(sqs_find(&d, cyl)), 2 * PI, 1e-12));
    int sph = sqs_add_solid(&d, L, SQS_SOLID_SPHERE, P(0, 0, 0), P(3, 0, 0));
    CHECK(near(sqs_volume(sqs_find(&d, sph)), 36 * PI, 1e-9) && near(sqs_area(sqs_find(&d, sph)), 36 * PI, 1e-9));
    CHECK(sqs_add_solid(&d, L, SQS_SOLID_BOX, P(0, 0, 0), P(1, 0, 1)) == -1);
    CHECK(sqs_add_solid(&d, L, (SqsSolidKind)7, P(0, 0, 0), P(1, 1, 1)) == -1);
    /* solids as meshes: a closed box, a cylinder and a sphere whose areas approach the true ones */
    SqsPoint mp[64];
    uint8_t mf[64][4];
    int np, nf = sqs_solid_mesh(sqs_find(&d, box), mp, 64, &np, mf, 64);
    CHECK(nf == 6 && np == 8 && near(sqs_area(&(SqsElement){.type = SQS_MESH}), 0, 0));
    double a = 0;
    for (int i = 0; i < nf; i++) {
        SqsElement t = {0};
        t.type = SQS_MESH;
        memcpy(t.pts, mp, sizeof(SqsPoint) * (size_t)(np < SQS_MAX_PTS ? np : SQS_MAX_PTS));
        t.npts = np;
        memcpy(t.faces[0], mf[i], 4);
        t.nfaces = 1;
        a += sqs_area(&t);
    }
    CHECK(near(a, 52, 1e-9));
    nf = sqs_solid_mesh(sqs_find(&d, cyl), mp, 64, &np, mf, 64);
    CHECK(nf == 36 && np == 26);
    nf = sqs_solid_mesh(sqs_find(&d, sph), mp, 64, &np, mf, 64);
    CHECK(nf == 48 && np == 42);
    int inside = 1;
    for (int i = 0; i < np; i++) inside &= near(sqs_dist(mp[i], P(0, 0, 0)), 3, 1e-9);
    CHECK(inside);
    /* bad input */
    CHECK(sqs_add_line(&d, 9999, P(0, 0, 0), P(1, 0, 0)) == -1);
    CHECK(sqs_add_line(&d, L, P(NAN, 0, 0), P(1, 0, 0)) == -1);
    CHECK(sqs_add_arc(&d, L, P(0, 0, 0), 0, 0, 90) == -1);
    CHECK(sqs_add_pline(&d, L, sq, 1) == -1 && sqs_add_shape(&d, L, sq, 2) == -1);
    SqsPoint many[SQS_MAX_PTS + 1] = {{0}};
    CHECK(sqs_add_pline(&d, L, many, SQS_MAX_PTS + 1) == -1);
    CHECK(sqs_add_level(&d, "Default", 0) == -1); /* names are unique */
}

static void test_cells_levels(void) {
    sqs_design_init(&d, "cells");
    int L = d.levels[0].id;
    int walls = sqs_add_level(&d, "walls", 0xff0000);
    int l1 = sqs_add_line(&d, L, P(0, 0, 0), P(2, 0, 0));
    int a1 = sqs_add_arc(&d, L, P(0, 0, 0), 1, 0, 90);
    int b1 = sqs_add_solid(&d, L, SQS_SOLID_BOX, P(0, 0, 0), P(1, 1, 1));
    int m[] = {l1, a1, b1};
    CHECK(sqs_define_cell(&d, "door", P(0, 0, 0), m, 3) > 0);
    CHECK(sqs_define_cell(&d, "door", P(0, 0, 0), m, 3) == -1);
    /* the members are the definition now, not drawn where they were */
    CHECK(sqs_accu_snap(&d, P(2.1, 0, 0), 0.5).kind == SQS_SNAP_NONE);
    int bad[] = {999};
    CHECK(sqs_define_cell(&d, "ghost", P(0, 0, 0), bad, 1) == -1);
    int pc = sqs_place_cell(&d, walls, "door", P(100, 0, 0), 2, 90);
    CHECK(pc > 0);
    CHECK(sqs_place_cell(&d, walls, "window", P(0, 0, 0), 1, 0) == -1);
    /* the placed line runs from 100,0 up to 100,4; the arc starts at 90 deg; the box is turned */
    SqsSnapResult s = sqs_accu_snap(&d, P(100.1, 3.9, 0), 0.5);
    CHECK(s.kind == SQS_SNAP_ENDPOINT && same(s.p, P(100, 4, 0)) && s.element_id == l1);
    s = sqs_accu_snap(&d, P(98.6, 1.4, 0), 0.5);
    CHECK(s.kind == SQS_SNAP_MIDPOINT && near(s.p.x, 100 - sqrt(2), 1e-9) && near(s.p.y, sqrt(2), 1e-9));
    /* a cell of the cell */
    int m2[] = {pc};
    CHECK(sqs_define_cell(&d, "pair", P(100, 0, 0), m2, 1) > 0);
    CHECK(sqs_place_cell(&d, L, "pair", P(0, 50, 0), 1, 180) > 0);
    s = sqs_accu_snap(&d, P(0, 46, 0), 0.1);
    CHECK(s.kind == SQS_SNAP_ENDPOINT && same(s.p, P(0, 46, 0)));
    /* a cell can't hold itself */
    int m3[] = {pc + 3};
    CHECK(sqs_define_cell(&d, "pair", P(0, 0, 0), m3, 1) == -1);
    /* the door placement is part of "pair" now: drawn only through it */
    CHECK(sqs_in_cell(&d, pc) && !sqs_in_cell(&d, l1 + 1000));
    CHECK(sqs_accu_snap(&d, P(100.1, 3.9, 0), 0.5).kind == SQS_SNAP_NONE);
    /* levels: hidden and frozen are not snapped to */
    CHECK(sqs_level_set(&d, "walls", 0, 0) == 0);
    s = sqs_accu_snap(&d, P(0.1, 46.1, 0), 0.5);
    CHECK(s.kind == SQS_SNAP_NONE);
    sqs_level_set(&d, "walls", 1, 1);
    CHECK(sqs_accu_snap(&d, P(0.1, 46.1, 0), 0.5).kind == SQS_SNAP_NONE);
    sqs_level_set(&d, "walls", 1, 0);
    CHECK(sqs_accu_snap(&d, P(0.1, 46.1, 0), 0.5).kind == SQS_SNAP_ENDPOINT);
    CHECK(sqs_level_set(&d, "roof", 1, 0) == -1);
}

static void test_snaps_keyins(void) {
    sqs_design_init(&d, "snaps");
    int L = d.levels[0].id;
    sqs_add_line(&d, L, P(0, 0, 0), P(10, 0, 0));
    sqs_add_line(&d, L, P(5, 5, 0), P(5, -5, 0));
    SqsSnapResult mid = sqs_accu_snap(&d, P(5.1, 0.1, 0), 1.0);
    CHECK(mid.kind == SQS_SNAP_MIDPOINT && near(mid.p.x, 5, 1e-12) && mid.ref == -1);
    SqsSnapResult near1 = sqs_accu_snap(&d, P(8, 0.3, 0), 1.0);
    CHECK(near1.kind == SQS_SNAP_NEAREST && same(near1.p, P(8, 0, 0)));
    SqsPoint sq[4] = {P(20, 0, 0), P(30, 0, 0), P(30, 10, 0), P(20, 10, 0)};
    sqs_add_shape(&d, L, sq, 4);
    sqs_add_line(&d, L, P(22, -5, 0), P(22, 15, 0));
    SqsSnapResult x = sqs_accu_snap(&d, P(22.2, 10.2, 0), 0.5);
    CHECK(x.kind == SQS_SNAP_INTERSECTION && same(x.p, P(22, 10, 0)));
    SqsSnapResult k = sqs_accu_snap(&d, P(30.1, 9.9, 0), 0.5);
    CHECK(k.kind == SQS_SNAP_KEYPOINT && same(k.p, P(30, 10, 0)));
    sqs_add_arc(&d, L, P(50, 0, 0), 5, 0, 360);
    SqsSnapResult cn = sqs_accu_snap(&d, P(50.2, 0.1, 0), 0.5);
    CHECK(cn.kind == SQS_SNAP_CENTER);
    SqsSnapResult on = sqs_accu_snap(&d, P(50, 5.3, 0), 0.5);
    CHECK(on.kind == SQS_SNAP_NEAREST && same(on.p, P(50, 5, 0)));
    CHECK(sqs_accu_snap(&d, P(500, 500, 0), 1).kind == SQS_SNAP_NONE);
    /* keyins */
    SqsPoint last = {10, 20, 3}, p;
    CHECK(sqs_keyin("xy=1,2", last, &p) == 0 && same(p, P(1, 2, 0)));
    CHECK(sqs_keyin("1,2,3", last, &p) == 0 && same(p, P(1, 2, 3)));
    CHECK(sqs_keyin("dl=5,-5", last, &p) == 0 && same(p, P(15, 15, 3)));
    CHECK(sqs_keyin("DL=0,0,2", last, &p) == 0 && same(p, P(10, 20, 5)));
    CHECK(sqs_keyin("di=10,90", last, &p) == 0 && p.x == 10 && p.y == 30); /* exact */
    CHECK(sqs_keyin("di=2,45", last, &p) == 0 && near(p.x, 10 + sqrt(2), 1e-12));
    CHECK(sqs_keyin("di=1,2,3", last, &p) == -1 && sqs_keyin("xy=1", last, &p) == -1);
    CHECK(sqs_keyin("1,2,3,4", last, &p) == -1 && sqs_keyin("zz=1,2", last, &p) == -1 && sqs_keyin("", last, &p) == -1);
}

static void test_references(void) {
    const char *rp = "/tmp/sqs_ref_test.sqdgn";
    sqs_design_init(&r0, "survey");
    int rl = sqs_add_level(&r0, "control", 0x00ff00);
    sqs_add_line(&r0, rl, P(0, 0, 0), P(10, 0, 0));
    CHECK(sqs_write_sqdgn(&r0, rp) == 0);
    sqs_design_init(&d, "site");
    CHECK(sqs_attach(&d, rp, P(1000, 2000, 0), 2) == 0);
    CHECK(sqs_attach(&d, "", P(0, 0, 0), 1) == -1 && sqs_attach(&d, rp, P(0, 0, 0), 0) == -1);
    const SqsDesign *refs[1] = {&r0};
    SqsSnapResult s = sqs_snap(&d, refs, 1, P(1019.9, 2000.1, 0), 0.5);
    CHECK(s.kind == SQS_SNAP_ENDPOINT && s.ref == 0 && same(s.p, P(1020, 2000, 0)));
    CHECK(sqs_accu_snap(&d, P(1019.9, 2000.1, 0), 0.5).kind == SQS_SNAP_NONE); /* not loaded: not there */
    d.refs[0].display = 0;
    CHECK(sqs_snap(&d, refs, 1, P(1019.9, 2000.1, 0), 0.5).kind == SQS_SNAP_NONE);
    d.refs[0].display = 1;
    sqs_level_set(&r0, "control", 0, 0);
    CHECK(sqs_snap(&d, refs, 1, P(1019.9, 2000.1, 0), 0.5).kind == SQS_SNAP_NONE);
    remove(rp);
}

static double tri_area(SqsPoint a, SqsPoint b, SqsPoint c) {
    double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    double x = uy * vz - uz * vy, y = uz * vx - ux * vz, z = ux * vy - uy * vx;
    return sqrt(x * x + y * y + z * z) / 2;
}

static void build_full(SqsDesign *x) {
    sqs_design_init(x, "Lot 12 site");
    int L = x->levels[0].id;
    int lv = sqs_add_level(x, "survey", 0x0000ff);
    sqs_add_line(x, L, P(0, 0, 0), P(1.5, 2.25, 3.125));
    sqs_add_arc(x, lv, P(1, 1, 0), 2.5, 30, -120);
    sqs_add_text(x, lv, P(1, 2, 0), "Peg 12 (found)");
    SqsPoint sq[4] = {P(0, 0, 0), P(10, 0, 0), P(10, 5, 0.5), P(0, 5, 0)};
    sqs_add_pline(x, L, sq, 4);
    sqs_add_curve(x, L, sq, 4);
    sqs_add_shape(x, lv, sq, 4);
    const uint8_t f[2][4] = {{0, 1, 2, 3}, {0, 2, 3, 3}};
    sqs_add_mesh(x, L, sq, 4, f, 2);
    int b = sqs_add_solid(x, L, SQS_SOLID_CYLINDER, P(5, 5, 0), P(0.5, 0, 3));
    x->elements[x->element_count - 1].rot_deg = 15;
    int m[] = {b};
    sqs_define_cell(x, "pole", P(5, 5, 0), m, 1);
    sqs_place_cell(x, lv, "pole", P(20, 0, 0), 0.5, 45);
    sqs_attach(x, "/tmp/somewhere/control.sqdgn", P(100, 200, 0), 1);
    sqs_level_set(x, "survey", 1, 1);
}

static int designs_equal(const SqsDesign *a, const SqsDesign *b) {
    if (strcmp(a->title, b->title) || a->element_count != b->element_count || a->level_count != b->level_count ||
        a->cell_count != b->cell_count || a->ref_count != b->ref_count || a->next_id != b->next_id)
        return 0;
    for (int i = 0; i < a->element_count; i++) {
        const SqsElement *x = &a->elements[i], *y = &b->elements[i];
        if (x->id != y->id || x->type != y->type || x->level_id != y->level_id || !same(x->a, y->a) ||
            x->radius != y->radius || x->start_deg != y->start_deg || x->sweep_deg != y->sweep_deg ||
            strcmp(x->text, y->text) || x->npts != y->npts || x->nfaces != y->nfaces || x->solid_kind != y->solid_kind ||
            !same(x->size, y->size) || x->scale != y->scale || x->rot_deg != y->rot_deg)
            return 0;
        for (int k = 0; k < x->npts; k++)
            if (!same(x->pts[k], y->pts[k])) return 0;
        if (memcmp(x->faces, y->faces, (size_t)x->nfaces * 4)) return 0;
        if (x->type == SQS_LINE && !same(x->b, y->b)) return 0;
    }
    for (int i = 0; i < a->level_count; i++)
        if (strcmp(a->levels[i].name, b->levels[i].name) || a->levels[i].frozen != b->levels[i].frozen ||
            a->levels[i].color != b->levels[i].color)
            return 0;
    for (int i = 0; i < a->ref_count; i++)
        if (strcmp(a->refs[i].path, b->refs[i].path) || !same(a->refs[i].offset, b->refs[i].offset)) return 0;
    return !strcmp(a->cells[0].name, b->cells[0].name);
}

static void test_files(void) {
    build_full(&d);
    const char *tp = "/tmp/sqs_full.sqdgn", *bp = "/tmp/sqs_full.sqdgnb";
    CHECK(sqs_write_sqdgn(&d, tp) == 0 && sqs_read_sqdgn(&d2, tp) == 0);
    CHECK(designs_equal(&d, &d2));
    CHECK(sqs_write_sqdgnb(&d, bp) == 0 && sqs_read_sqdgnb(&d2, bp) == 0);
    CHECK(designs_equal(&d, &d2));
    CHECK(sqs_read_any(&d2, bp) == 0 && designs_equal(&d, &d2));
    CHECK(sqs_read_any(&d2, tp) == 0 && designs_equal(&d, &d2));
    /* both files were written */
    FILE *f = fopen(tp, "rb");
    fseek(f, 0, SEEK_END);
    long tl = ftell(f);
    fclose(f);
    f = fopen(bp, "rb");
    fseek(f, 0, SEEK_END);
    long bl = ftell(f);
    fclose(f);
    CHECK(tl > 0 && bl > 0);
    /* every cut-short binary is refused, never read past its end */
    static unsigned char buf[1 << 16];
    f = fopen(bp, "rb");
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    int all_refused = 1;
    for (size_t cut = 8; cut < n; cut += 7) {
        f = fopen("/tmp/sqs_cut.sqdgnb", "wb");
        fwrite(buf, 1, cut, f);
        fclose(f);
        all_refused &= sqs_read_sqdgnb(&d2, "/tmp/sqs_cut.sqdgnb") == -2;
    }
    CHECK(all_refused);
    /* a flipped byte in a length or a count is caught */
    int caught = 0, tried = 0;
    for (size_t i = 8; i < n; i += 13) {
        memcpy(buf + (1 << 15), buf, n);
        unsigned char *b = buf + (1 << 15);
        b[i] ^= 0xff;
        f = fopen("/tmp/sqs_cut.sqdgnb", "wb");
        fwrite(b, 1, n, f);
        fclose(f);
        int rc = sqs_read_sqdgnb(&d2, "/tmp/sqs_cut.sqdgnb");
        tried++;
        caught += rc == -2 || rc == 0;
    }
    CHECK(caught == tried); /* either refused or read, never a crash */
    CHECK(sqs_read_sqdgnb(&d2, tp) == -1);
    /* a damaged text file */
    f = fopen("/tmp/sqs_bad.sqdgn", "w");
    fprintf(f, "SQDGN 2\nPLINE 5 1 99 0 0 0\nEND\n");
    fclose(f);
    CHECK(sqs_read_sqdgn(&d2, "/tmp/sqs_bad.sqdgn") == -2);
    f = fopen("/tmp/sqs_bad.sqdgn", "w");
    fprintf(f, "SQDGN 2\nLINE 2147483647 1 0 0 0 1 1 0\nEND\n");
    fclose(f);
    CHECK(sqs_read_sqdgn(&d2, "/tmp/sqs_bad.sqdgn") == -2); /* an id that would run next_id over */
    f = fopen("/tmp/sqs_bad.sqdgn", "w");
    fprintf(f, "SQDGN 2\nMESH 5 1 3 0 0 0 1 0 0 0 1 0 1 0 1 7 7\nEND\n");
    fclose(f);
    CHECK(sqs_read_sqdgn(&d2, "/tmp/sqs_bad.sqdgn") == -2);
    /* a version 1 file still reads */
    f = fopen("/tmp/sqs_v1.sqdgn", "w");
    fprintf(f, "SQDGN 1\nTITLE old\nNEXTID 5\nLEVEL 1 Default 1 0 16777215\nLINE 2 1 0 0 0 1 0 0\n"
               "ARC 3 1 0 0 0 1 0 90\nTEXT 4 1 0 0 0 hello\nCELL 9 c 0 0 0 1 2\nEND\n");
    fclose(f);
    CHECK(sqs_read_sqdgn(&d2, "/tmp/sqs_v1.sqdgn") == 0 && d2.element_count == 3 && d2.cell_count == 1 &&
          d2.next_id == 10);
    /* DXF of everything */
    CHECK(sqs_export_dxf(&d, "/tmp/sqs_full.dxf") == 0);
    f = fopen("/tmp/sqs_full.dxf", "rb");
    static char dx[1 << 20];
    size_t dn = fread(dx, 1, sizeof dx - 1, f);
    dx[dn] = 0;
    fclose(f);
    CHECK(strstr(dx, "0\nLAYER\n2\nsurvey\n70\n1\n62\n-5\n") != NULL);
    CHECK(strstr(dx, "0\nPOLYLINE\n8\nsurvey\n66\n1\n10\n0\n20\n0\n30\n0\n70\n9\n") != NULL); /* the shape */
    CHECK(strstr(dx, "0\nPOLYLINE\n8\nDefault\n66\n1\n10\n0\n20\n0\n30\n0\n70\n64\n71\n4\n72\n2\n") != NULL); /* the mesh */
    CHECK(strstr(dx, "70\n64\n71\n26\n72\n36\n") != NULL); /* the cylinder, once: in its cell */
    CHECK(strstr(dx, "0\nBLOCK\n8\n0\n2\npole\n70\n0\n10\n5\n20\n5\n30\n0\n") != NULL);
    CHECK(strstr(dx, "0\nINSERT\n8\nsurvey\n2\npole\n10\n20\n20\n0\n30\n0\n41\n0.5\n") != NULL);
    CHECK(strstr(dx, "0\nARC\n8\nsurvey\n") && strstr(dx, "50\n-90\n51\n30\n"));
    CHECK(strstr(dx, "1\nPeg 12 (found)\n") != NULL);
    CHECK(strstr(dx, "3DFACE") == NULL);
    /* and back: the cell and its placement, the mesh in one piece, the
       cylinder in pieces that fit, the curve as polylines */
    static SqsDesign back;
    sqs_design_init(&back, "back");
    SqsDxfStats st;
    CHECK(sqs_import_dxf(&back, "/tmp/sqs_full.dxf", &st) == 0);
    CHECK(st.lines == 1 && st.arcs == 1 && st.texts == 1 && st.shapes == 1 && st.skipped == 0);
    CHECK(st.plines == 3); /* the polyline, and the 25-point curve in two */
    CHECK(st.cells == 1 && st.placements == 1 && st.meshes >= 3);
    const SqsCell *pole = sqs_find_cell(&back, "pole");
    CHECK(pole && same(pole->origin, P(5, 5, 0)) && pole->member_count == st.meshes - 1);
    const SqsLevel *sv = sqs_level(&back, sqs_level_id(&back, "survey"));
    CHECK(sv && sv->frozen && sv->color == 0x0000ff);
    const SqsElement *mesh = NULL;
    for (int i = 0; i < back.element_count; i++)
        if (back.elements[i].type == SQS_MESH && !sqs_in_cell(&back, back.elements[i].id)) mesh = &back.elements[i];
    CHECK(mesh && mesh->npts == 4 && mesh->nfaces == 2 && mesh->faces[1][2] == 3 && mesh->faces[1][3] == 3);
    /* every cylinder face came back: the pieces' area adds up */
    double area = 0;
    for (int j = 0; pole && j < pole->member_count; j++) area += sqs_area(sqs_find(&back, pole->member_ids[j]));
    SqsElement cyl = {0};
    cyl.type = SQS_SOLID;
    cyl.solid_kind = SQS_SOLID_CYLINDER;
    cyl.size = P(0.5, 0, 3);
    SqsPoint cp[64];
    uint8_t cf[64][4];
    int cnp, cnf = sqs_solid_mesh(&cyl, cp, 64, &cnp, cf, 64);
    double want = 0;
    for (int k = 0; k < cnf; k++) {
        const uint8_t *q = cf[k];
        want += tri_area(cp[q[0]], cp[q[1]], cp[q[2]]) + tri_area(cp[q[0]], cp[q[2]], cp[q[3]]);
    }
    CHECK(cnp == 26 && cnf == 36 && near(area, want, 1e-9));
    remove("/tmp/sqs_full.dxf");
    remove(tp);
    remove(bp);
}

int main(void) {
    test_elements();
    test_cells_levels();
    test_snaps_keyins();
    test_references();
    test_files();
    printf("test_elements: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
