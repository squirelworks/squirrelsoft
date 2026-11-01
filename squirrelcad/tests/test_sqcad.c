#include "sqcad.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;
static void expect(int c, const char *m) {
    if (!c) {
        fprintf(stderr, "FAIL: %s\n", m);
        failures++;
    }
}

int main(void) {
    SqcDrawing *d = (SqcDrawing *)calloc(1, sizeof(SqcDrawing));
    if (!d) return 1;
    sqc_drawing_init(d, "t");
    SqcPoint a = {0, 0, 0}, b = {3, 4, 0}, c = {0, 0, 0};
    int lid = sqc_line(d, a, b);
    expect(fabs(sqc_ent_length(&d->ents[0]) - 5.0) < 1e-9, "3-4-5 length");
    expect(sqc_circle(d, c, 2.0) > 0, "circle");
    SqcPoint poly[3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}};
    expect(sqc_pline(d, poly, 3) > 0, "pline");
    int ids[] = {lid};
    expect(sqc_block_def(d, "stick", a, ids, 1) > 0, "block");
    expect(sqc_insert(d, "stick", b, 1.0, 0.0) > 0, "insert");

    const char *path = "/tmp/sqcad_test.sqdwg";
    expect(sqc_write_sqdwg(d, path) == 0, "write");
    SqcDrawing *d2 = (SqcDrawing *)calloc(1, sizeof(SqcDrawing));
    expect(d2 != NULL, "alloc d2");
    expect(sqc_read_sqdwg(d2, path) == 0, "read");
    expect(d2->ent_count == d->ent_count, "roundtrip");

    free(d);
    free(d2);
    if (failures) return 1;
    puts("squirrelcad tests ok");
    return 0;
}
