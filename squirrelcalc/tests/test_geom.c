/* SquirrelCalc: geometry objects, constructions, measurements and drawing. */
#include "calcgeom.h"
#include "sqmath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures, checks;
#define CHECK(c) check((c), #c, __LINE__)
#define NEAR(a, b, tol) check(fabs((a) - (b)) <= (tol), #a " ~ " #b, __LINE__)
static void check(int ok, const char *what, int line) {
    checks++;
    if (!ok) {
        failures++;
        fprintf(stderr, "FAIL line %d: %s\n", line, what);
    }
}

static int contains(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

static CalcGeom g;
static char msg[96];
static const char *NAMES[] = {"a"};
static double VALUES[] = {2};
static const CalcEnv ENV = {NAMES, VALUES, 1, NULL, NULL};

static int def(const char *name, const char *rhs) { return calc_geom_define(&g, name, rhs, &ENV, NULL, msg, sizeof msg); }

static const CalcGeomObj *obj(const char *name) {
    int i = calc_geom_find(&g, name);
    return i >= 0 ? &g.o[i] : NULL;
}

static double measure(const char *text) {
    char buf[256];
    if (calc_geom_expand(&g, text, buf, sizeof buf, msg, sizeof msg)) return NAN;
    SqmExpr e;
    double v;
    if (sqm_parse(buf, &e, NULL) != SQM_OK || sqm_eval(&e, NULL, NULL, 0, &v) != SQM_OK) return NAN;
    return v;
}

static void test_define(void) {
    calc_geom_init(&g);
    CHECK(def("A", "(0, 0)") == 0 && obj("A")->kind == CALC_G_POINT);
    CHECK(def("B", " point(4, 0) ") == 0);
    CHECK(def("C", "(a - 1, 3)") == 0); /* a = 2 */
    NEAR(obj("C")->x, 1, 0);
    CHECK(def("s", "segment(A, B)") == 0 && obj("s")->kind == CALC_G_SEGMENT);
    CHECK(def("l", "line(A, C)") == 0 && def("r", "ray(B, C)") == 0);
    CHECK(def("t", "polygon(A, B, C)") == 0 && obj("t")->npar == 3);
    CHECK(def("c", "circle(A, B, C)") == 0);
    NEAR(obj("c")->x, 2, 1e-12);
    NEAR(obj("c")->y, 1, 1e-12);
    NEAR(obj("c")->r, sqrt(5), 1e-12);
    CHECK(def("c2", "circle(A, 2a)") == 0);
    NEAR(obj("c2")->r, 4, 0);
    CHECK(def("c3", "circle(C, B)") == 0);
    NEAR(obj("c3")->r, sqrt(18), 1e-12);
    CHECK(def("M", "midpoint(A, B)") == 0 && obj("M")->x == 2 && obj("M")->y == 0);
    CHECK(def("N", "midpoint(s)") == 0 && obj("N")->x == 2);
    CHECK(def("h", "perpendicular(s, C)") == 0 && def("p", "parallel(s, C)") == 0 && def("b", "bisector(A, B)") == 0);
    CHECK(def("F", "intersect(h, s)") == 0 && obj("F")->x == 1 && obj("F")->y == 0);
    CHECK(def("G", "intersect(b, p)") == 0 && obj("G")->x == 2 && obj("G")->y == 3);
    /* a line and a circle: in order along the line */
    CHECK(def("I1", "intersect(l, c)") == 0 && def("I2", "intersect(l, c, 2)") == 0);
    NEAR(obj("I1")->x, 0, 1e-12);
    NEAR(obj("I2")->x, 1, 1e-12);
    NEAR(obj("I2")->y, 3, 1e-12);
    /* two circles: radius 4 about A and radius sqrt(18) about C */
    CHECK(def("J1", "intersect(c2, c3)") == 0 && def("J2", "intersect(c2, c3, 2)") == 0);
    NEAR(hypot(obj("J1")->x, obj("J1")->y), 4, 1e-12);
    NEAR(hypot(obj("J2")->x - 1, obj("J2")->y - 3), sqrt(18), 1e-12);
    CHECK(fabs(obj("J1")->x - obj("J2")->x) > 0.1);
    /* things that do not exist (yet) */
    CHECK(def("K", "intersect(s, p)") == -1 && contains(msg, "do not meet")); /* parallel */
    CHECK(def("K", "intersect(s, h, 2)") == -1 && contains(msg, "only once"));
    CHECK(def("R1", "(1, 1)") == 0 && def("R2", "(1, 2)") == 0 && def("up", "ray(R1, R2)") == 0);
    CHECK(def("K", "intersect(up, s)") == -1 && contains(msg, "do not meet")); /* the ray points away */
    CHECK(def("K", "intersect(r, s)") == 0 && obj("K")->x == 4); /* a ray meets at its start */
    CHECK(def("K", "circle(A, -1)") == -1 && contains(msg, "not above 0"));
    CHECK(def("K", "circle(A, M, B)") == -1 && contains(msg, "on one line"));
    CHECK(def("K", "segment(A, A)") == -1 && contains(msg, "same point"));
    /* mistakes */
    CHECK(def("K", "segment(A, Q)") == -1 && contains(msg, "Q is not a point"));
    CHECK(def("K", "segment(A)") == -1 && contains(msg, "wrong number"));
    CHECK(def("K", "polygon(A, B)") == -1);
    CHECK(def("K", "perpendicular(A, s)") == -1 && contains(msg, "A is a point"));
    CHECK(def("K", "midpoint(c)") == -1 && contains(msg, "c is a circle"));
    CHECK(def("K", "intersect(l, c, 3)") == -1);
    CHECK(def("K", "(1, q)") == -1 && contains(msg, "define q first"));
    CHECK(def("K", "(1, 2, 3)") == -2 && def("K", "2 + 3") == -2 && def("K", "sin(3)") == -2);
    CHECK(def("K", "segment(A, ") == -2);
    CHECK(calc_geom_find(&g, "K") >= 0 && !obj("K")->ok); /* kept: it may exist once its parts move */
    /* failed definitions of a new name leave an object that does not exist; delete it */
    calc_geom_delete(&g, "K");
    CHECK(calc_geom_find(&g, "K") < 0);
}

static void test_follow(void) {
    /* moving a free point moves what is built from it */
    int b = calc_geom_find(&g, "B");
    CHECK(calc_geom_draggable(&g, b) && !calc_geom_draggable(&g, calc_geom_find(&g, "C")));
    CHECK(!calc_geom_draggable(&g, calc_geom_find(&g, "M")));
    CHECK(calc_geom_move(&g, b, 6, 0, &ENV, NULL) == 0);
    NEAR(obj("M")->x, 3, 0);
    NEAR(measure("length(s)"), 6, 1e-12);
    CHECK(calc_geom_move(&g, calc_geom_find(&g, "M"), 1, 1, &ENV, NULL) == -1);
    /* a variable moves a point that uses it */
    VALUES[0] = 3;
    calc_geom_update(&g, &ENV, NULL);
    NEAR(obj("C")->x, 2, 0);
    NEAR(obj("F")->x, 2, 0);
    /* a redefinition keeps its place: things built on it follow */
    CHECK(def("A", "(0, 1)") == 0);
    NEAR(obj("M")->y, 0.5, 0);
    CHECK(def("A", "midpoint(M, B)") == -1 && contains(msg, "M is defined after this"));
    CHECK(def("A", "midpoint(A, B)") == -1 && contains(msg, "from itself"));
    /* with nothing built on it, a name can be remade from later objects */
    CHECK(def("W", "(9, 9)") == 0 && def("W", "midpoint(M, B)") == 0 && calc_geom_find(&g, "W") == g.n - 1);
    /* something that stops existing comes back */
    CHECK(def("P", "(0, 5)") == 0 && def("Q", "(1, 5)") == 0 && def("u", "line(P, Q)") == 0);
    CHECK(def("X", "intersect(u, c2)") == 0); /* y = 5 across the circle of 6 about (0, 1) */
    NEAR(obj("X")->x, -sqrt(20), 1e-12);
    CHECK(def("P", "(0, 9)") == 0 && def("Q", "(1, 9)") == 0);
    CHECK(!obj("X")->ok && contains(obj("X")->why, "do not meet"));
    CHECK(def("Q", "(1, 5)") == 0 && def("P", "(0, 5)") == 0 && obj("X")->ok);
    /* delete takes what is built on it */
    int before = g.n;
    CHECK(calc_geom_delete(&g, "s") >= 4); /* s, N, h, p, F, G ... */
    CHECK(calc_geom_find(&g, "s") < 0 && calc_geom_find(&g, "F") < 0 && calc_geom_find(&g, "B") >= 0);
    CHECK(g.n < before && calc_geom_delete(&g, "nothing") == 0);
    for (int i = 0; i < g.n; i++)
        for (int k = 0; k < g.o[i].npar; k++) CHECK(g.o[i].par[k] < i);
}

static void test_measure(void) {
    calc_geom_init(&g);
    VALUES[0] = 2;
    def("A", "(0, 0)");
    def("B", "(4, 0)");
    def("C", "(0, 3)");
    def("t", "polygon(A, B, C)");
    def("s", "segment(B, C)");
    def("l", "line(B, C)");
    def("v", "line(A, C)");
    def("c", "circle(A, 2)");
    NEAR(measure("distance(A, B)"), 4, 0);
    NEAR(measure("length(s)"), 5, 1e-12);
    NEAR(measure("area(t)"), 6, 1e-12);
    NEAR(measure("perimeter(t)"), 12, 1e-12);
    NEAR(measure("angle(B, A, C)"), 90, 1e-12);
    NEAR(measure("angle(C, B, A)") + measure("angle(A, C, B)"), 90, 1e-12);
    NEAR(measure("slope(l)"), -0.75, 1e-15);
    CHECK(isinf(measure("slope(v)")));
    NEAR(measure("distance(A, l)"), 2.4, 1e-12);
    NEAR(measure("radius(c)"), 2, 0);
    NEAR(measure("area(c)"), 4 * 3.141592653589793, 1e-12);
    NEAR(measure("perimeter(c)"), 4 * 3.141592653589793, 1e-12);
    NEAR(measure("xcoord(B) + ycoord(C)"), 7, 0);
    NEAR(measure("2 area(t) + length (s)"), 17, 1e-12);
    CHECK(isnan(measure("length(t)")) && contains(msg, "of a segment"));
    CHECK(isnan(measure("distance(A, c)")) && contains(msg, "to a point or a line"));
    CHECK(isnan(measure("angle(A, A, B)")) && contains(msg, "corner"));
    CHECK(isnan(measure("area(Q)")) && contains(msg, "Q is not"));
    CHECK(isnan(measure("area(t")) && contains(msg, "not closed"));
    CHECK(isnan(measure("distance(A)")));
    CHECK(calc_geom_needs("2 area(t)") && calc_geom_needs("slope (l)") && !calc_geom_needs("area + 1"));
    CHECK(!calc_geom_needs("myarea(t)"));
    char buf[64];
    CHECK(calc_geom_expand(&g, "myarea(t)", buf, sizeof buf, msg, sizeof msg) == 0 && !strcmp(buf, "myarea(t)"));
    char small[3];
    CHECK(calc_geom_expand(&g, "area(t)", small, sizeof small, msg, sizeof msg) == -1);
    char out[160];
    calc_geom_describe(&g, calc_geom_find(&g, "l"), out, sizeof out);
    CHECK(!strcmp(out, "l: line y = -0.75 x + 3"));
    calc_geom_describe(&g, calc_geom_find(&g, "v"), out, sizeof out);
    CHECK(!strcmp(out, "v: line x = 0"));
    calc_geom_describe(&g, calc_geom_find(&g, "t"), out, sizeof out);
    CHECK(!strcmp(out, "t: polygon of 3 corners, area 6, perimeter 12"));
    calc_geom_describe(&g, calc_geom_find(&g, "c"), out, sizeof out);
    CHECK(!strcmp(out, "c: circle, centre (0, 0), radius 2"));
    /* full: 32 objects at most */
    char nm[8];
    int ok = 0;
    for (int k = 0; k < 40; k++) {
        snprintf(nm, sizeof nm, "Z%d", k);
        ok += def(nm, "(1, 1)") == 0;
    }
    CHECK(ok == CALC_GEOM_MAX - 8 && contains(msg, "no room"));
}

static void test_draw(void) {
    enum { W = 200, H = 200 };
    static uint32_t px[W * H];
    for (int k = 0; k < W * H; k++) px[k] = 0xFFFFFFFFu;
    sqg_canvas cv;
    sqg_canvas_init(&cv, px, W, H, W);
    CalcView v = {-5, 5, -5, 5};
    sqg_rect r = sqg_rect_make(0, 0, W, H);
    calc_geom_init(&g);
    def("A", "(-2, -2)");
    def("B", "(2, -2)");
    def("C", "(0, 2)");
    def("t", "polygon(A, B, C)");
    def("c", "circle(C, 1)");
    def("l", "line(A, C)");
    CHECK(calc_geom_draw(&cv, r, &v, &g, -1) == 6);
    /* inside the triangle is tinted, outside is white */
    uint32_t in = px[(int)calc_px_y(&v, r, -1) * W + (int)calc_px_x(&v, r, 0)];
    CHECK(in != 0xFFFFFFFFu && (in & 0xFF) > (in >> 16 & 0xFF));
    CHECK(px[(int)calc_px_y(&v, r, -4) * W + (int)calc_px_x(&v, r, 4)] == 0xFFFFFFFFu);
    /* the circle passes through (1, 2) */
    int cx = (int)lround(calc_px_x(&v, r, 1)), cy = (int)lround(calc_px_y(&v, r, 2)), hits = 0;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) hits += px[(cy + dy) * W + cx + dx] == 0xFF00707Au;
    CHECK(hits > 0);
    /* the line runs on past C to the edge */
    int ex = (int)lround(calc_px_x(&v, r, 1.4)), ey = (int)lround(calc_px_y(&v, r, 4.8));
    hits = 0;
    for (int dy = -3; dy <= 3; dy++)
        for (int dx = -3; dx <= 3; dx++)
            if (ey + dy >= 0 && ey + dy < H) hits += px[(ey + dy) * W + ex + dx] == 0xFF00707Au;
    CHECK(hits > 0);
    /* hit test: free points near the pointer */
    CHECK(calc_geom_hit(&g, r, &v, calc_px_x(&v, r, 2) + 3, calc_px_y(&v, r, -2), 7) == calc_geom_find(&g, "B"));
    CHECK(calc_geom_hit(&g, r, &v, calc_px_x(&v, r, 2) + 30, calc_px_y(&v, r, -2), 7) == -1);
    /* far out of view: nothing drawn wrongly, no hang */
    CalcView far = {1e8, 1e8 + 1, 1e8, 1e8 + 1};
    calc_geom_draw(&cv, r, &far, &g, 0);
    CalcView tiny = {-1e-9, 1e-9, -1e-9, 1e-9};
    calc_geom_draw(&cv, r, &tiny, &g, 0);
}

int main(void) {
    test_define();
    test_follow();
    test_measure();
    test_draw();
    printf("test_geom: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
