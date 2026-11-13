#include "sqstation.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        failures++;
    }
}

int main(void) {
    static SqsDesign d;
    sqs_design_init(&d, "test");
    expect(d.level_count == 1, "default level");

    SqsPoint a = {0, 0, 0}, b = {10, 0, 0}, c = {5, 5, 0}, e = {5, -5, 0};
    int l1 = sqs_add_line(&d, d.levels[0].id, a, b);
    int l2 = sqs_add_line(&d, d.levels[0].id, c, e);
    expect(l1 > 0 && l2 > 0, "add lines");

    SqsSnapResult mid = sqs_accu_snap(&d, (SqsPoint){5.1, 0.1, 0}, 1.0);
    expect(mid.kind == SQS_SNAP_MIDPOINT, "midpoint snap");
    expect(fabs(mid.p.x - 5.0) < 1e-9, "midpoint x");

    SqsSnapResult hit = sqs_accu_snap(&d, (SqsPoint){5.0, 0.2, 0}, 1.0);
    expect(hit.kind == SQS_SNAP_INTERSECTION || hit.kind == SQS_SNAP_MIDPOINT,
           "intersection or mid near crossing");

    int members[] = {l1, l2};
    expect(sqs_define_cell(&d, "cross", a, members, 2) > 0, "cell");

    const char *path = "/tmp/sqstation_test.sqdgn";
    expect(sqs_write_sqdgn(&d, path) == 0, "write");
    static SqsDesign d2;
    expect(sqs_read_sqdgn(&d2, path) == 0, "read");
    expect(d2.element_count == d.element_count, "roundtrip count");
    expect(strcmp(d2.title, "test") == 0, "title");

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("squirrelstation tests ok");
    return 0;
}
