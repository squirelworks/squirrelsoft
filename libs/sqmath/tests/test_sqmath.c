/* libs/sqmath unit tests. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sqmath.h"

static int checks, failures;

#define CHECK(c) check((c), #c, __LINE__)
static void check(int ok, const char *what, int line) {
    checks++;
    if (!ok) {
        failures++;
        fprintf(stderr, "FAIL line %d: %s\n", line, what);
    }
}

static SqmExpr ex, ex2, ex3;
static SqmProgram prog;
static const char *XY[] = {"x", "y"};

static int near(double a, double b, double tol) {
    if (isnan(a) || isnan(b)) return isnan(a) && isnan(b);
    if (isinf(a) || isinf(b)) return a == b;
    return fabs(a - b) <= tol * (1 + fabs(b));
}

/* value of text at x, y */
static double val(const char *text, double x, double y) {
    double v = NAN, xy[2] = {x, y};
    SqmError err;
    if (sqm_parse(text, &ex, &err) != SQM_OK) {
        fprintf(stderr, "  parse failed: %s: %s at %d\n", text, err.msg, err.pos);
        return NAN;
    }
    if (sqm_eval(&ex, XY, xy, 2, &v) != SQM_OK) return NAN;
    return v;
}

static void test_eval(void) {
    static const struct {
        const char *text;
        double x, want;
    } T[] = {
        {"1 + 2 * 3", 0, 7},
        {"(1 + 2) * 3", 0, 9},
        {"2^3^2", 0, 512},
        {"-2^2", 0, -4},
        {"(-2)^2", 0, 4},
        {"2^-1", 0, 0.5},
        {"2**3", 0, 8},
        {"10 - 4 - 3", 0, 3},
        {"64 / 4 / 2", 0, 8},
        {"2x + 1", 3, 7},
        {"2(x + 1)", 3, 8},
        {"x y", 3, 6},
        {"(x)(x + 1)", 3, 12},
        {"3x^2", 2, 12},
        {"-x^2", 3, -9},
        {"5!", 0, 120},
        {"3!^2", 0, 36},
        {"0!", 0, 1},
        {"2pi", 0, 2 * 3.14159265358979323846},
        {"e", 0, 2.71828182845904523536},
        {"2e", 0, 2 * 2.71828182845904523536},
        {"1e3", 0, 1000},
        {"1.5e-3", 0, 0.0015},
        {".5 + 1.", 0, 1.5},
        {"sin(pi/2)", 0, 1},
        {"cos(0)", 0, 1},
        {"atan(1) * 4", 0, 3.14159265358979323846},
        {"ln(e^2)", 0, 2},
        {"log(1000)", 0, 3},
        {"exp(0)", 0, 1},
        {"sqrt(16) + cbrt(-8)", 0, 2},
        {"abs(-3) + floor(2.7) + ceil(2.1)", 0, 8},
        {"round(2.5) + round(-2.5)", 0, 0},
        {"round(0.49)", 0, 0},
        {"sign(-4) + sign(0) + sign(9)", 0, 0},
        {"atan2(1, -1)", 0, 3 * 3.14159265358979323846 / 4},
        {"hypot(3, 4)", 0, 5},
        {"min(3, -1) + max(3, -1)", 0, 2},
        {"mod(7, 3)", 0, 1},
        {"mod(-1, 3)", 0, 2},
        {"mod(1, -3)", 0, -2},
        {"sinh(0) + cosh(0) + tanh(0)", 0, 1},
        {"asin(1) - acos(0)", 0, 0},
        {"  1 +\t2\n", 0, 3},
        {"+3", 0, 3},
        {"--3", 0, 3},
        {"2 3", 0, 6},
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        double v = val(T[i].text, T[i].x, 2);
        if (!near(v, T[i].want, 1e-12)) fprintf(stderr, "  %s = %.17g, want %.17g\n", T[i].text, v, T[i].want);
        CHECK(near(v, T[i].want, 1e-12));
    }
    CHECK(isnan(val("sqrt(-1)", 0, 0)));
    CHECK(isinf(val("1/0", 0, 0)));
    CHECK(isnan(val("(-1)!", 0, 0)));
    CHECK(near(val("0.5!", 0, 0), sqrt(3.14159265358979323846) / 2, 1e-12));

    /* a variable without a value */
    double v = 7;
    CHECK(sqm_parse("x + z", &ex, NULL) == SQM_OK);
    CHECK(sqm_eval(&ex, XY, (double[]){1, 2}, 2, &v) == SQM_ERR_UNKNOWN && v == 7);
    CHECK(sqm_uses(&ex, "z") && sqm_uses(&ex, "x") && !sqm_uses(&ex, "y"));
    CHECK(sqm_parse("sin(t)", &ex, NULL) == SQM_OK && ex.nnames == 1 && !strcmp(ex.names[0], "t"));
    CHECK(sqm_parse("pi + e", &ex, NULL) == SQM_OK && ex.nnames == 0);
    CHECK(sqm_parse("x_1 + x2 + longer_name", &ex, NULL) == SQM_OK && ex.nnames == 3);
    CHECK(!strcmp(sqm_fn_name(SQM_FN_ATAN2), "atan2") && sqm_fn_args(SQM_FN_ATAN2) == 2);
    CHECK(sqm_fn_args(SQM_FN_SIN) == 1 && !strcmp(sqm_strerror(SQM_ERR_DIFF), "no derivative rule"));
}

static void test_errors(void) {
    static const struct {
        const char *text;
        int code, pos;
    } T[] = {
        {"", SQM_ERR_SYNTAX, 0},
        {"   ", SQM_ERR_SYNTAX, 0},
        {"2 +", SQM_ERR_SYNTAX, 3},
        {"(1 + 2", SQM_ERR_SYNTAX, 6},
        {"1 + 2)", SQM_ERR_SYNTAX, 5},
        {"sin x", SQM_ERR_SYNTAX, 4},
        {"min(1)", SQM_ERR_ARGS, 0},
        {"sin(1, 2)", SQM_ERR_ARGS, 0},
        {"sqrt()", SQM_ERR_ARGS, 5},
        {"1.2.3", SQM_ERR_SYNTAX, 3},
        {"2 @ 3", SQM_ERR_SYNTAX, 2},
        {"*3", SQM_ERR_SYNTAX, 0},
        {"2^", SQM_ERR_SYNTAX, 2},
        {"a_very_long_name_here + 1", SQM_ERR_NAME, 0},
        {"max(1 2)", SQM_ERR_ARGS, 0},
    };
    SqmError err;
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        int rc = sqm_parse(T[i].text, &ex, &err);
        if (rc != T[i].code || err.pos != T[i].pos)
            fprintf(stderr, "  \"%s\": code %d pos %d (%s), want %d at %d\n", T[i].text, rc, err.pos, err.msg,
                    T[i].code, T[i].pos);
        CHECK(rc == T[i].code && err.code == rc && err.pos == T[i].pos && err.msg[0]);
        CHECK(ex.root == -1 && ex.n == 0);
    }
    CHECK(sqm_parse(NULL, &ex, NULL) == SQM_ERR_SYNTAX);

    /* nesting deeper than SQM_MAX_DEPTH, and more nodes than fit */
    static char deep[400], wide[4000];
    for (int i = 0; i < 100; i++) deep[i] = '(';
    deep[100] = '1';
    for (int i = 0; i < 100; i++) deep[101 + i] = ')';
    CHECK(sqm_parse(deep, &ex, &err) == SQM_ERR_DEPTH);
    size_t k = 0;
    for (int i = 0; i < 600; i++) k += (size_t)sprintf(wide + k, "%s1", i ? "+" : "");
    CHECK(sqm_parse(wide, &ex, &err) == SQM_ERR_FULL);
    /* 33 distinct names */
    k = 0;
    for (int i = 0; i < 33; i++) k += (size_t)sprintf(wide + k, "%sv%d", i ? "+" : "", i);
    CHECK(sqm_parse(wide, &ex, &err) == SQM_ERR_FULL);
    for (int i = 0; i < 60; i++) deep[i] = '(';
    deep[60] = '1';
    for (int i = 0; i < 60; i++) deep[61 + i] = ')';
    deep[121] = 0;
    CHECK(sqm_parse(deep, &ex, &err) == SQM_OK);
}

static const char *SAMPLES[] = {
    "x^2 + 3x - 1", "sin(x)/x", "(x + 1)^3 - 2(x - y)", "-x^2 + y", "2^x^2", "(-2)^x", "x - (y - 1)",
    "x/(y/2)", "-(x + y)", "exp(-x^2/2)/sqrt(2pi)", "atan2(y, x) + hypot(x, y)", "x!", "(x + 1)!",
    "-x*y", "x*-y", "x^-y", "ln(abs(x)) + log(y)", "min(x, y) - max(x, 2) + mod(x, y)", "3 - -x",
    "x/y*2", "x - y - 1", "1/(1 + x^2)", "cos(x)^2", "-1", "2 - 3", "x^(1/3)", "e^x - pi",
};

static void test_compile_and_print(void) {
    static const double P[][2] = {{0.7, 1.3}, {2.5, -0.4}, {-1.2, 3.1}, {3, 2}};
    for (size_t i = 0; i < sizeof SAMPLES / sizeof SAMPLES[0]; i++) {
        CHECK(sqm_parse(SAMPLES[i], &ex, NULL) == SQM_OK);
        CHECK(sqm_compile(&ex, XY, 2, &prog) == SQM_OK);
        char text[512];
        size_t len = sqm_print(&ex, text, sizeof text);
        CHECK(len == strlen(text));
        int back = sqm_parse(text, &ex2, NULL);
        if (back != SQM_OK) fprintf(stderr, "  %s printed as %s does not parse\n", SAMPLES[i], text);
        CHECK(back == SQM_OK);
        for (size_t k = 0; k < sizeof P / sizeof P[0]; k++) {
            double a, b;
            CHECK(sqm_eval(&ex, XY, P[k], 2, &a) == SQM_OK);
            double c = sqm_run(&prog, P[k]);
            CHECK(sqm_eval(&ex2, XY, P[k], 2, &b) == SQM_OK);
            if (!near(a, c, 1e-15) || !near(a, b, 1e-12))
                fprintf(stderr, "  %s -> %s at (%g, %g): eval %.17g run %.17g reparsed %.17g\n", SAMPLES[i], text,
                        P[k][0], P[k][1], a, c, b);
            CHECK(near(a, c, 1e-15));
            CHECK(near(a, b, 1e-12));
        }
    }
    /* exact text */
    static const struct {
        const char *in, *out;
    } T[] = {
        {"x^2+3*x-1", "x^2 + 3*x - 1"},
        {"2^3^2", "2^3^2"},
        {"(2^3)^2", "(2^3)^2"},
        {"(-2)^x", "(-2)^x"},
        {"-x^2", "-x^2"},
        {"-(x+1)", "-(x + 1)"},
        {"x-(y-1)", "x - (y - 1)"},
        {"x+(y+1)", "x + y + 1"},
        {"x/(y/2)", "x/(y/2)"},
        {"x^-1", "x^-1"},
        {"(x+1)!", "(x + 1)!"},
        {"atan2(y,x)", "atan2(y, x)"},
        {"2pi e", "2*pi*e"},
        {"0.1", "0.1"},
        {"1e300*10", "1e+300*10"},
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        char text[256];
        CHECK(sqm_parse(T[i].in, &ex, NULL) == SQM_OK);
        sqm_print(&ex, text, sizeof text);
        if (strcmp(text, T[i].out)) fprintf(stderr, "  print %s: %s, want %s\n", T[i].in, text, T[i].out);
        CHECK(!strcmp(text, T[i].out));
    }
    /* a short buffer is cut and terminated; the return value is the full length */
    char small[6];
    CHECK(sqm_parse("x^2 + 3*x - 1", &ex, NULL) == SQM_OK);
    CHECK(sqm_print(&ex, small, sizeof small) == 13 && !strcmp(small, "x^2 +"));

    /* compile needs every name bound */
    CHECK(sqm_parse("x + z", &ex, NULL) == SQM_OK);
    CHECK(sqm_compile(&ex, XY, 2, &prog) == SQM_ERR_UNKNOWN && prog.nops == 0);
    CHECK(isnan(sqm_run(&prog, (double[]){1, 2})));
    const char *xz[] = {"z", "x"};
    CHECK(sqm_compile(&ex, xz, 2, &prog) == SQM_OK && sqm_run(&prog, (double[]){10, 1}) == 11);
}

/* text derivative of in with respect to x, and the d/dx check against a central difference */
static void diff_case(const char *in, const char *want) {
    char text[512];
    CHECK(sqm_parse(in, &ex, NULL) == SQM_OK);
    int rc = sqm_diff(&ex, "x", &ex2);
    CHECK(rc == SQM_OK);
    if (rc) return;
    sqm_print(&ex2, text, sizeof text);
    if (want && strcmp(text, want)) fprintf(stderr, "  d/dx %s = %s, want %s\n", in, text, want);
    if (want) CHECK(!strcmp(text, want));
    static const double X[] = {0.37, 0.81, 1.6, 2.3};
    for (size_t k = 0; k < 4; k++) {
        double h = 1e-6, p[2] = {X[k] + h, 0.6}, m[2] = {X[k] - h, 0.6}, at[2] = {X[k], 0.6};
        double fp = 0, fm = 0, d;
        CHECK(sqm_eval(&ex, XY, p, 2, &fp) == SQM_OK && sqm_eval(&ex, XY, m, 2, &fm) == SQM_OK);
        CHECK(sqm_eval(&ex2, XY, at, 2, &d) == SQM_OK);
        double num = (fp - fm) / (2 * h);
        if (!near(d, num, 1e-5)) fprintf(stderr, "  d/dx %s at %g: %.10g, numeric %.10g\n", in, X[k], d, num);
        CHECK(near(d, num, 1e-5));
    }
}

static void test_diff(void) {
    diff_case("3x^4 - 2x + 7", "12*x^3 - 2");
    diff_case("x^2 sin(x)", "2*x*sin(x) + x^2*cos(x)");
    diff_case("ln(x)/x", "(1 - ln(x))/x^2");
    diff_case("x^x", "x^x*(ln(x) + 1)");
    diff_case("e^(2x)", "2*e^(2*x)");
    diff_case("2^x", "2^x*ln(2)");
    diff_case("sqrt(1 - x^2)", "-x/sqrt(1 - x^2)");
    diff_case("1/x", "-1/x^2");
    diff_case("x/(2 + x)", "2/(2 + x)^2");
    diff_case("(x + 1)(x - 1)", "2*x");
    diff_case("tan(x)", "1/cos(x)^2");
    diff_case("log(x)", "1/(x*ln(10))");
    diff_case("atan2(y, x)", "-y/(x^2 + y^2)");
    diff_case("x*exp(-x)", "exp(-x) - x*exp(-x)");
    diff_case("y^2 + 3", "0");
    diff_case("5", "0");
    diff_case("cos(x)", "-sin(x)");
    /* value checks only */
    diff_case("asin(x/3) + acos(x/4) + atan(x)", NULL);
    diff_case("sinh(x) cosh(x) tanh(x)", NULL);
    diff_case("cbrt(x) + abs(x - 3)", NULL);
    diff_case("hypot(x, y) + exp(sin(x))", NULL);
    diff_case("(x^2 + 1)^3/(x - 4)", NULL);
    diff_case("x^-2 + x^0.5 - 2/x^3", NULL);
    diff_case("floor(x) + round(x) + ceil(x) + sign(x) + x", "1");
    /* no rule for these when they depend on x */
    CHECK(sqm_parse("min(x, 1)", &ex, NULL) == SQM_OK && sqm_diff(&ex, "x", &ex2) == SQM_ERR_DIFF);
    CHECK(ex2.root == -1);
    CHECK(sqm_parse("x!", &ex, NULL) == SQM_OK && sqm_diff(&ex, "x", &ex2) == SQM_ERR_DIFF);
    CHECK(sqm_parse("mod(y, 2) + x", &ex, NULL) == SQM_OK && sqm_diff(&ex, "x", &ex2) == SQM_OK);
    CHECK(sqm_diff(&ex, "x", &ex) == SQM_ERR_SYNTAX); /* not in place */
    /* the second derivative */
    CHECK(sqm_parse("x^3", &ex, NULL) == SQM_OK && sqm_diff(&ex, "x", &ex2) == SQM_OK);
    char text[64];
    CHECK(sqm_diff(&ex2, "x", &ex3) == SQM_OK && sqm_print(&ex3, text, sizeof text) && !strcmp(text, "6*x"));
}

static void test_simplify(void) {
    static const struct {
        const char *in, *out;
    } T[] = {
        {"2x + 3x", "5*x"},
        {"x*x*x", "x^3"},
        {"(x + 1) - (x + 1)", "0"},
        {"6/4 + 0*y", "3/2"},
        {"6/3", "2"},
        {"1/3", "1/3"},
        {"sqrt(4) + sqrt(2)", "2 + sqrt(2)"},
        {"x^1 + y^0", "x + 1"},
        {"--x", "x"},
        {"1*x - 0", "x"},
        {"x + 1 + 2x", "3*x + 1"},
        {"3x - 1 - x", "2*x - 1"},
        {"x - -2y", "x + 2*y"},
        {"-x + 3", "3 - x"},
        {"2x/(4y)", "x/(2*y)"},
        {"(x^2)^3", "x^6"},
        {"x^2 * x^3", "x^5"},
        {"x^3/x", "x^2"},
        {"ln(e)", "1"},
        {"ln(exp(x))", "x"},
        {"a - (b - c)", "a - b + c"},
        {"0/x", "0"},
        {"x/x", "1"},
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        char text[256];
        CHECK(sqm_parse(T[i].in, &ex, NULL) == SQM_OK);
        CHECK(sqm_simplify(&ex, &ex2) == SQM_OK);
        sqm_print(&ex2, text, sizeof text);
        if (strcmp(text, T[i].out)) fprintf(stderr, "  simplify %s: %s, want %s\n", T[i].in, text, T[i].out);
        CHECK(!strcmp(text, T[i].out));
    }
    /* simplifying keeps the value */
    for (size_t i = 0; i < sizeof SAMPLES / sizeof SAMPLES[0]; i++) {
        double a = 0, b = 0, p[2] = {1.7, 0.9};
        CHECK(sqm_parse(SAMPLES[i], &ex, NULL) == SQM_OK && sqm_simplify(&ex, &ex2) == SQM_OK);
        CHECK(sqm_eval(&ex, XY, p, 2, &a) == SQM_OK && sqm_eval(&ex2, XY, p, 2, &b) == SQM_OK);
        if (!near(a, b, 1e-12)) fprintf(stderr, "  simplify %s changed the value: %.17g vs %.17g\n", SAMPLES[i], a, b);
        CHECK(near(a, b, 1e-12));
    }
}

static void test_numeric(void) {
    double v[2] = {0, 0}, r;
    CHECK(sqm_parse("x^2 - 2", &ex, NULL) == SQM_OK && sqm_compile(&ex, XY, 2, &prog) == SQM_OK);
    CHECK(sqm_solve(&prog, v, 0, 0, 2, &r) == SQM_OK && fabs(r - sqrt(2)) < 1e-14);
    CHECK(sqm_solve(&prog, v, 0, 2, 3, &r) == -1);  /* no sign change */
    CHECK(sqm_solve(&prog, v, 5, 0, 2, &r) == -1);  /* bad slot */
    CHECK(sqm_solve(&prog, v, 0, 2, 0, &r) == -1);  /* lo > hi */
    double roots[8];
    CHECK(sqm_roots(&prog, v, 0, -5, 5, 100, roots, 8) == 2);
    CHECK(fabs(roots[0] + sqrt(2)) < 1e-14 && fabs(roots[1] - sqrt(2)) < 1e-14);

    /* exact zeros on the grid count once; poles are not roots */
    CHECK(sqm_parse("sin(x)", &ex, NULL) == SQM_OK && sqm_compile(&ex, XY, 2, &prog) == SQM_OK);
    int n = sqm_roots(&prog, v, 0, -1, 10, 110, roots, 8);
    CHECK(n == 4 && roots[0] == 0 && fabs(roots[3] - 3 * 3.14159265358979323846) < 1e-12);
    CHECK(sqm_parse("tan(x)", &ex, NULL) == SQM_OK && sqm_compile(&ex, XY, 2, &prog) == SQM_OK);
    n = sqm_roots(&prog, v, 0, 0.5, 6, 500, roots, 8);
    CHECK(n == 1 && fabs(roots[0] - 3.14159265358979323846) < 1e-12);
    CHECK(sqm_parse("x - y", &ex, NULL) == SQM_OK && sqm_compile(&ex, XY, 2, &prog) == SQM_OK);
    v[1] = 0.25; /* the other slot keeps its value */
    CHECK(sqm_solve(&prog, v, 0, -1, 1, &r) == SQM_OK && fabs(r - 0.25) < 1e-15);
    /* many roots: the count is the total, cap limits what is written */
    CHECK(sqm_parse("sin(10x)", &ex, NULL) == SQM_OK && sqm_compile(&ex, XY, 2, &prog) == SQM_OK);
    roots[3] = 99;
    CHECK(sqm_roots(&prog, v, 0, 0.05, 10, 4000, roots, 3) == 31 && roots[3] == 99);

    int ok;
    CHECK(sqm_parse("sin(x)", &ex, NULL) == SQM_OK && sqm_compile(&ex, XY, 2, &prog) == SQM_OK);
    CHECK(fabs(sqm_integrate(&prog, v, 0, 0, 3.14159265358979323846, 1e-12, &ok) - 2) < 1e-10 && ok);
    CHECK(sqm_parse("4/(1 + x^2)", &ex, NULL) == SQM_OK && sqm_compile(&ex, XY, 2, &prog) == SQM_OK);
    CHECK(fabs(sqm_integrate(&prog, v, 0, 0, 1, 1e-12, &ok) - 3.14159265358979323846) < 1e-10 && ok);
    CHECK(sqm_parse("exp(-x^2)", &ex, NULL) == SQM_OK && sqm_compile(&ex, XY, 2, &prog) == SQM_OK);
    CHECK(fabs(sqm_integrate(&prog, v, 0, -6, 6, 1e-12, &ok) - sqrt(3.14159265358979323846)) < 1e-10 && ok);
    CHECK(fabs(sqm_integrate(&prog, v, 0, 1, -1, 1e-12, &ok) + 1.4936482656248541) < 1e-10); /* reversed */
    CHECK(sqm_integrate(&prog, v, 0, 2, 2, 1e-12, &ok) == 0 && ok);
    CHECK(sqm_parse("1/x", &ex, NULL) == SQM_OK && sqm_compile(&ex, XY, 2, &prog) == SQM_OK);
    sqm_integrate(&prog, v, 0, 0, 1, 1e-12, &ok);
    CHECK(!ok); /* 1/0 at the end */
    CHECK(isnan(sqm_integrate(&prog, v, 7, 0, 1, 1e-12, &ok)) && !ok);
}

static void test_format(void) {
    static const struct {
        double x;
        int digits;
        const char *want;
    } T[] = {
        {0, 12, "0"},         {-0.0, 12, "0"},      {1, 12, "1"},
        {0.1 + 0.2, 12, "0.3"}, {1.0 / 3, 12, "0.333333333333"}, {-2.5, 12, "-2.5"},
        {123456.789, 12, "123456.789"}, {1e15, 12, "1e15"},   {1.5e-7, 12, "1.5e-7"},
        {6.02214076e23, 6, "6.02214e23"}, {999999.99999999, 6, "1000000"}, {0.000123, 12, "0.000123"},
        {3.14159265358979, 3, "3.14"}, {NAN, 12, "undefined"}, {INFINITY, 12, "undefined"},
        {-INFINITY, 12, "undefined"}, {2.0 / 3, 17, "0.66666666666666663"}, {-1e-300, 12, "-1e-300"},
        {123456789012345.0, 12, "123456789012000"},
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        char b[64];
        size_t n = sqm_format(T[i].x, T[i].digits, b, sizeof b);
        if (strcmp(b, T[i].want)) fprintf(stderr, "  format %.17g/%d: %s, want %s\n", T[i].x, T[i].digits, b, T[i].want);
        CHECK(!strcmp(b, T[i].want) && n == strlen(b));
    }
    char tiny[4];
    CHECK(sqm_format(12345, 12, tiny, sizeof tiny) == 5 && !strcmp(tiny, "123"));
}

/* ---- expansion, collection, solving ---- */

static void expand_case(const char *in, const char *want) {
    char out[1024];
    CHECK(sqm_parse(in, &ex3, NULL) == SQM_OK);
    int rc = sqm_expand(&ex3, &ex2);
    out[0] = 0;
    if (rc == SQM_OK) sqm_print(&ex2, out, sizeof out);
    if (rc != SQM_OK || strcmp(out, want))
        fprintf(stderr, "  expand %s: got \"%s\" (%s), want \"%s\"\n", in, out, sqm_strerror(rc), want);
    CHECK(rc == SQM_OK && !strcmp(out, want));
    /* the same value at a few points */
    static const double P[3][2] = {{0.7, -1.3}, {2.5, 0.4}, {-1.9, 3.1}};
    for (int k = 0; k < 3 && rc == SQM_OK; k++) {
        double a = val(in, P[k][0], P[k][1]), b = val(out, P[k][0], P[k][1]);
        if (!near(a, b, 1e-9)) fprintf(stderr, "  expand %s at %g: %.17g vs %.17g\n", in, P[k][0], a, b);
        CHECK(near(a, b, 1e-9));
    }
}

static void collect_case(const char *in, const char *var, const char *want) {
    char out[1024] = "";
    CHECK(sqm_parse(in, &ex3, NULL) == SQM_OK);
    int rc = sqm_collect(&ex3, var, &ex2);
    if (rc == SQM_OK) sqm_print(&ex2, out, sizeof out);
    if (rc != SQM_OK || strcmp(out, want))
        fprintf(stderr, "  collect %s: got \"%s\" (%s), want \"%s\"\n", in, out, sqm_strerror(rc), want);
    CHECK(rc == SQM_OK && !strcmp(out, want));
}

/* want: the solutions joined with "; ", or "none", or "every" */
static void solve_case(const char *eq, int want_rc, const char *want) {
    SqmSolution sol[8];
    int n = 99;
    char got[1200] = "";
    SqmError err;
    CHECK(sqm_parse_eq(eq, &ex3, &err) == SQM_OK);
    int rc = sqm_solve_for(&ex3, "x", sol, 8, &n);
    if (n < 0) snprintf(got, sizeof got, "every");
    else if (n == 0) snprintf(got, sizeof got, "none");
    for (int k = 0; k < n; k++) {
        size_t len = strlen(got);
        snprintf(got + len, sizeof got - len, "%s%s", k ? "; " : "", sol[k].text);
        /* each solution makes the equation hold */
        if (isfinite(sol[k].value)) {
            double xy[2] = {sol[k].value, 0}, r;
            CHECK(sqm_eval(&ex3, XY, xy, 2, &r) == SQM_OK && fabs(r) < 1e-9);
        }
    }
    if (rc != want_rc || strcmp(got, want))
        fprintf(stderr, "  solve %s: got \"%s\" (%d), want \"%s\" (%d)\n", eq, got, rc, want, want_rc);
    CHECK(rc == want_rc && !strcmp(got, want));
}

static void test_cas(void) {
    expand_case("(x+1)^2", "x^2 + 2*x + 1");
    expand_case("(x - 1)(x + 1)", "x^2 - 1");
    expand_case("(x+y)^3", "x^3 + 3*x^2*y + 3*x*y^2 + y^3");
    expand_case("(x+1)^3 - x^3", "3*x^2 + 3*x + 1");
    expand_case("(2x+3)^5", "32*x^5 + 240*x^4 + 720*x^3 + 1080*x^2 + 810*x + 243");
    expand_case("-(x - 1)^2", "-x^2 + 2*x - 1");
    expand_case("x/2 + x/3", "5*x/6");
    expand_case("0.1x + 0.2x", "3*x/10");
    expand_case("x (x + y) / y", "x^2/y + x");
    expand_case("1/(2x)", "1/(2*x)");
    expand_case("(x+1)/(x-1)", "x/(x - 1) + 1/(x - 1)");
    expand_case("(1+x)^-2", "1/(x + 1)^2");
    expand_case("sin((x+1)^2) + sin(x^2 + 2x + 1)", "2*sin(x^2 + 2*x + 1)");
    expand_case("pi (x + 1)", "pi*x + pi");
    expand_case("sqrt(4) x + atan2(y, x)^2", "atan2(y, x)^2 + 2*x");
    expand_case("2^10 x - 3!", "1024*x - 6");
    expand_case("x - x", "0");
    expand_case("(x^2)^3 x^-6", "1");
    expand_case("e^x e^x", "(e^x)^2");
    /* limits: 62-bit fractions, 64 terms, 8 parts */
    CHECK(sqm_parse("(2x + 1)^64", &ex3, NULL) == SQM_OK && sqm_expand(&ex3, &ex2) == SQM_ERR_RANGE);
    CHECK(sqm_parse("(x + y + x y + 1)^9", &ex3, NULL) == SQM_OK && sqm_expand(&ex3, &ex2) == SQM_ERR_FULL);
    CHECK(sqm_parse("a+b+c+d+f+g+h+k+m", &ex3, NULL) == SQM_OK && sqm_expand(&ex3, &ex2) == SQM_ERR_FULL);
    CHECK(sqm_expand(&ex3, &ex3) == SQM_ERR_SYNTAX);

    collect_case("a x^2 + b x^2 + c x + x + 1", "x", "(a + b)*x^2 + (c + 1)*x + 1");
    collect_case("(x + a)^2", "x", "x^2 + 2*a*x + a^2");
    collect_case("a x + b x - 1/x", "x", "(a + b)*x - 1/x");
    collect_case("y^2 + 2", "x", "y^2 + 2");
    collect_case("x - x", "x", "0");

    solve_case("2x + 3", SQM_OK, "-3/2");
    solve_case("x/3 + 1/2 = 0", SQM_OK, "-3/2");
    solve_case("x^2 = 2", SQM_OK, "-sqrt(2); sqrt(2)");
    solve_case("x^2 - 5x + 6 = 0", SQM_OK, "2; 3");
    solve_case("x^2 - x - 1", SQM_OK, "(1 - sqrt(5))/2; (1 + sqrt(5))/2");
    solve_case("2x^2 = 3", SQM_OK, "-sqrt(6)/2; sqrt(6)/2");
    solve_case("x^2 = 8", SQM_OK, "-2*sqrt(2); 2*sqrt(2)");
    solve_case("4x^2 - 4x + 1", SQM_OK, "1/2");
    solve_case("x^2 + 1 = 0", SQM_OK, "none");
    solve_case("x^3 - 6x^2 + 11x = 6", SQM_OK, "1; 2; 3");
    solve_case("6x^3 - 11x^2 + 6x - 1", SQM_OK, "1/3; 1/2; 1");
    solve_case("x^3 = x", SQM_OK, "-1; 0; 1");
    solve_case("x^4 - 5x^2 + 4", SQM_OK, "-2; -1; 1; 2");
    solve_case("x^5 = x", SQM_OK, "-1; 0; 1");
    solve_case("(x - 1)^2 (x + 2)", SQM_OK, "-2; 1");
    solve_case("x + 1/x = 5/2", SQM_OK, "1/2; 2");
    solve_case("x^3 - 3x^2 + x + 1", SQM_OK, "1 - sqrt(2); 1; 1 + sqrt(2)");
    solve_case("a x + b = 0", SQM_OK, "-b/a");
    solve_case("x - x", SQM_OK, "every");
    solve_case("1 = 2", SQM_OK, "none");
    solve_case("sin(x) = 0", SQM_ERR_SOLVE, "none");
    solve_case("x^3 = 2", SQM_ERR_SOLVE, "none");
    solve_case("x (x^3 - 2)", SQM_ERR_SOLVE, "0");
    {
        SqmSolution sol[2];
        int n;
        CHECK(sqm_parse("a x^2 + b x + c", &ex3, NULL) == SQM_OK);
        CHECK(sqm_solve_for(&ex3, "x", sol, 2, &n) == SQM_OK && n == 2);
        /* the quadratic formula: check it numerically with a = 2, b = -3, c = -5 (roots -1, 5/2) */
        const char *N[] = {"a", "b", "c"};
        double v[3] = {2, -3, -5}, r0 = 0, r1 = 0;
        CHECK(sqm_parse(sol[0].text, &ex2, NULL) == SQM_OK && sqm_eval(&ex2, N, v, 3, &r0) == SQM_OK);
        CHECK(sqm_parse(sol[1].text, &ex2, NULL) == SQM_OK && sqm_eval(&ex2, N, v, 3, &r1) == SQM_OK);
        CHECK((near(r0, -1, 1e-12) && near(r1, 2.5, 1e-12)) || (near(r1, -1, 1e-12) && near(r0, 2.5, 1e-12)));
        CHECK(isnan(sol[0].value));
        /* cap smaller than the count */
        CHECK(sqm_parse("x^4 - 5x^2 + 4", &ex3, NULL) == SQM_OK);
        CHECK(sqm_solve_for(&ex3, "x", sol, 2, &n) == SQM_OK && n == 2);
    }

    /* equations */
    SqmError err;
    double v;
    CHECK(sqm_parse_eq("x^2 = 2x + 1", &ex3, &err) == SQM_OK);
    CHECK(sqm_eval(&ex3, XY, (double[]){3, 0}, 2, &v) == SQM_OK && v == 2);
    CHECK(sqm_parse_eq("y = 2", &ex3, &err) == SQM_OK && sqm_eval(&ex3, XY, (double[]){0, 5}, 2, &v) == SQM_OK && v == 3);
    CHECK(sqm_parse_eq("x + 1", &ex3, &err) == SQM_OK);
    CHECK(sqm_parse_eq("x = 1 = 2", &ex3, &err) == SQM_ERR_SYNTAX && err.pos == 6);
    CHECK(sqm_parse_eq("x = 2 +", &ex3, &err) == SQM_ERR_SYNTAX && err.pos == 7);
    CHECK(sqm_parse_eq("= 2", &ex3, &err) == SQM_ERR_SYNTAX && err.pos == 0);
}

/* random token strings: whatever parses must print, reparse, compile, simplify
 * and differentiate without changing its value (or crashing) */
static unsigned rng = 12345;
static unsigned next(void) {
    rng = rng * 1103515245u + 12345u;
    return (rng >> 16) & 0x7FFF;
}

static int same_value(double a, double b) {
    if (!isfinite(a) || !isfinite(b)) return 1; /* NaN/inf may differ by rounding route */
    return fabs(a - b) <= 1e-9 * (1 + fabs(a));
}

static void test_fuzz(void) {
    static const char *TOK[] = {"x", "y", "2", "0.5", "3", "pi", "e", "+", "-", "*", "/", "^", "(", ")", "(",
                                ")", "!", "sin(", "ln(", "sqrt(", "abs(", "min(", ",", " ", "exp(", "1e2"};
    int parsed = 0, expanded = 0;
    for (int t = 0; t < 20000; t++) {
        char text[200];
        size_t k = 0;
        int len = 1 + (int)(next() % 14);
        for (int i = 0; i < len; i++) {
            const char *tok = TOK[next() % (sizeof TOK / sizeof TOK[0])];
            size_t n = strlen(tok);
            if (k + n + 2 >= sizeof text) break;
            memcpy(text + k, tok, n);
            k += n;
            if (next() % 2) text[k++] = ' ';
        }
        text[k] = 0;
        if (sqm_parse(text, &ex, NULL) != SQM_OK) continue;
        int other = 0; /* "xe" or "pix" is a name of its own */
        for (int n = 0; n < ex.nnames; n++) other |= strcmp(ex.names[n], "x") && strcmp(ex.names[n], "y");
        if (other) continue;
        parsed++;
        double p[2] = {0.83, 1.27}, a, b;
        CHECK(sqm_eval(&ex, XY, p, 2, &a) == SQM_OK);
        CHECK(sqm_compile(&ex, XY, 2, &prog) == SQM_OK);
        double c = sqm_run(&prog, p);
        CHECK(same_value(a, c) || (isnan(a) && isnan(c)));
        char out[2048];
        sqm_print(&ex, out, sizeof out);
        int rc = sqm_parse(out, &ex2, NULL);
        if (rc != SQM_OK) fprintf(stderr, "  fuzz: %s printed as %s does not parse\n", text, out);
        CHECK(rc == SQM_OK);
        if (rc == SQM_OK) {
            CHECK(sqm_eval(&ex2, XY, p, 2, &b) == SQM_OK);
            if (!same_value(a, b)) fprintf(stderr, "  fuzz: %s -> %s: %.17g vs %.17g\n", text, out, a, b);
            CHECK(same_value(a, b));
        }
        rc = sqm_simplify(&ex, &ex2);
        CHECK(rc == SQM_OK || rc == SQM_ERR_FULL);
        if (rc == SQM_OK) {
            CHECK(sqm_eval(&ex2, XY, p, 2, &b) == SQM_OK);
            if (!same_value(a, b)) {
                sqm_print(&ex2, out, sizeof out);
                fprintf(stderr, "  fuzz: simplify %s -> %s: %.17g vs %.17g\n", text, out, a, b);
            }
            CHECK(same_value(a, b));
        }
        rc = sqm_diff(&ex, "x", &ex3);
        CHECK(rc == SQM_OK || rc == SQM_ERR_DIFF || rc == SQM_ERR_FULL);
        rc = sqm_expand(&ex, &ex2);
        CHECK(rc == SQM_OK || rc == SQM_ERR_FULL || rc == SQM_ERR_RANGE || rc == SQM_ERR_DEPTH);
        if (rc == SQM_OK && isfinite(a)) {
            CHECK(sqm_eval(&ex2, XY, p, 2, &b) == SQM_OK);
            /* expanding big powers cancels digits: compare loosely */
            int ok = !isfinite(b) || fabs(a - b) <= 1e-6 * (1 + fabs(a));
            if (!ok) {
                sqm_print(&ex2, out, sizeof out);
                fprintf(stderr, "  fuzz: expand %s -> %s: %.17g vs %.17g\n", text, out, a, b);
            }
            CHECK(ok);
        }
        if (rc == SQM_OK) expanded++;
    }
    CHECK(parsed > 300);
    CHECK(expanded > parsed / 2);
}

/* the printed result of op on text, or the error */
static const char *step3(int op, const char *text, int *rc) {
    static char out[2048];
    SqmError err;
    if (sqm_parse(text, &ex2, &err) != SQM_OK) {
        *rc = err.code;
        return "parse";
    }
    *rc = op == 0 ? sqm_factor(&ex2, "x", &ex3) : op == 1 ? sqm_apart(&ex2, "x", &ex3) : sqm_antideriv(&ex2, "x", &ex3);
    if (*rc) return sqm_strerror(*rc);
    sqm_print(&ex3, out, sizeof out);
    return out;
}

/* a and b agree at a few points (where both are defined) */
static int same_values(const char *a, const char *b) {
    static const double X[] = {-2.3, -0.71, 0.37, 1.91, 3.3, 5.5};
    int seen = 0;
    for (int k = 0; k < 6; k++) {
        double u = val(a, X[k], 0), v = val(b, X[k], 0);
        if (!isfinite(u) || !isfinite(v)) continue;
        seen++;
        if (!near(u, v, 1e-9)) {
            fprintf(stderr, "  %s vs %s at %g: %.17g %.17g\n", a, b, X[k], u, v);
            return 0;
        }
    }
    return seen >= 2;
}

/* d/dx of the integral is the integrand, at a few points */
static int integral_ok(const char *f) {
    int rc;
    char F[2048], dF[2048];
    snprintf(F, sizeof F, "%s", step3(2, f, &rc));
    if (rc) {
        fprintf(stderr, "  integral of %s: %s\n", f, F);
        return 0;
    }
    if (sqm_parse(F, &ex2, NULL) != SQM_OK || sqm_diff(&ex2, "x", &ex3) != SQM_OK) return 0;
    sqm_print(&ex3, dF, sizeof dF);
    int ok = same_values(dF, f);
    if (!ok) fprintf(stderr, "  integral of %s = %s\n", f, F);
    return ok;
}

static void test_step3(void) {
    int rc;
    /* factoring */
    struct {
        const char *in, *out;
    } fac[] = {
        {"x^3 - x", "x*(x - 1)*(x + 1)"},
        {"x^4 - 1", "(x - 1)*(x + 1)*(x^2 + 1)"},
        {"2x^2 - 2", "2*(x - 1)*(x + 1)"},
        {"6x^2 + x - 1", "(3*x - 1)*(2*x + 1)"},
        {"x^5 - 2x^4 + x^3", "x^3*(x - 1)^2"},
        {"(x^2 - 1)/(x^2 + 2x + 1)", "(x - 1)/(x + 1)"},
        {"(x + 1)/(2x^2 - 2)", "1/(2*(x - 1))"},
        {"x^2 - 2", "x^2 - 2"},
        {"x^4 + 4", "(x^2 - 2*x + 2)*(x^2 + 2*x + 2)"},
        {"x^4 + 1", "x^4 + 1"},
        {"x^6 - 1", "(x - 1)*(x + 1)*(x^2 - x + 1)*(x^2 + x + 1)"},
        {"(x - 1)^3 (x^2 + 1)^2", "(x - 1)^3*(x^2 + 1)^2"},
        {"x^2/4 - 1", "1/4*(x - 2)*(x + 2)"},
        {"-x^2 + 1", "-(x - 1)*(x + 1)"},
        {"0", "0"},
        {"7", "7"},
        {"x", "x"},
    };
    for (size_t k = 0; k < sizeof fac / sizeof fac[0]; k++) {
        const char *got = step3(0, fac[k].in, &rc);
        CHECK(rc == SQM_OK && !strcmp(got, fac[k].out));
        if (rc != SQM_OK || strcmp(got, fac[k].out)) fprintf(stderr, "  factor %s: %s\n", fac[k].in, got);
        if (rc == SQM_OK) CHECK(same_values(fac[k].in, got) || !strcmp(fac[k].in, "0") || !strcmp(fac[k].in, "7"));
    }
    CHECK(!strcmp(step3(0, "x^2 + y", &rc), "no exact form found") && rc == SQM_ERR_NOFORM);
    CHECK(step3(0, "sin(x) + 1", &rc) && rc == SQM_ERR_NOFORM);
    CHECK(step3(0, "sqrt(x)", &rc) && rc == SQM_ERR_NOFORM);
    CHECK(sqm_parse("x^2 - 1", &ex2, NULL) == SQM_OK && sqm_factor(&ex2, NULL, &ex3) == SQM_OK);
    CHECK(sqm_factor(&ex2, NULL, &ex2) == SQM_ERR_SYNTAX);

    /* partial fractions */
    struct {
        const char *in, *out;
    } ap[] = {
        {"1/(x^2 - 1)", "1/(2*(x - 1)) - 1/(2*(x + 1))"},
        {"(x^3 + 1)/(x^2 - 1)", "x + 1/(x - 1)"},
        {"1/(x^3 + x)", "1/x - x/(x^2 + 1)"},
        {"(2x + 3)/((x - 1)^2 (x + 2))", "1/(9*(x - 1)) + 5/(3*(x - 1)^2) - 1/(9*(x + 2))"},
        {"1/(x^4 - 1)", "1/(4*(x - 1)) - 1/(4*(x + 1)) - 1/(2*(x^2 + 1))"},
        {"x/(x^2 + x + 1)", "x/(x^2 + x + 1)"},
        {"1/(x^2 - 2)", "1/(x^2 - 2)"},
        {"(x^2 + 1)/(x^2 + 1)^2", "1/(x^2 + 1)"},
        {"x^2 + 1/2", "x^2 + 1/2"},
        {"(x^2 + 3x + 5)/(x + 1)", "x + 2 + 3/(x + 1)"},
    };
    for (size_t k = 0; k < sizeof ap / sizeof ap[0]; k++) {
        const char *got = step3(1, ap[k].in, &rc);
        CHECK(rc == SQM_OK && !strcmp(got, ap[k].out));
        if (rc != SQM_OK || strcmp(got, ap[k].out)) fprintf(stderr, "  apart %s: %s\n", ap[k].in, got);
        if (rc == SQM_OK) CHECK(same_values(ap[k].in, got));
    }
    CHECK(step3(1, "5/(x^3 - 2)", &rc) && rc == SQM_ERR_NOFORM); /* x^3 - 2 does not split */
    CHECK(step3(1, "1/(x + y)", &rc) && rc == SQM_ERR_NOFORM);
    CHECK(step3(1, "e^x/(x + 1)", &rc) && rc == SQM_ERR_NOFORM);

    /* random ratios of products of small factors: apart, then integrate */
    unsigned seed = 12345;
    int done = 0, integrated = 0;
    for (int n = 0; n < 150; n++) {
        char text[512];
        size_t at = 0;
        seed = seed * 1103515245u + 12345u;
        int deg = (int)(seed >> 16) % 4;
        at += (size_t)snprintf(text + at, sizeof text - at, "(");
        for (int k = 0; k <= deg; k++) {
            seed = seed * 1103515245u + 12345u;
            at += (size_t)snprintf(text + at, sizeof text - at, "%s%d*x^%d", k ? " + " : "", (int)((seed >> 16) % 9) - 4, k);
        }
        at += (size_t)snprintf(text + at, sizeof text - at, " + 1)/(");
        seed = seed * 1103515245u + 12345u;
        int nf = 1 + (int)(seed >> 16) % 3;
        for (int k = 0; k < nf; k++) {
            seed = seed * 1103515245u + 12345u;
            int a = 1 + (int)(seed >> 16) % 3, b = (int)(seed >> 20) % 7 - 3, quad = (seed >> 24) % 4 == 0;
            if (quad) at += (size_t)snprintf(text + at, sizeof text - at, "%s(x^2 + %d)", k ? "*" : "", 1 + (b < 0 ? -b : b));
            else at += (size_t)snprintf(text + at, sizeof text - at, "%s(%d*x + %d)", k ? "*" : "", a, b);
        }
        snprintf(text + at, sizeof text - at, ")");
        char got[2048];
        snprintf(got, sizeof got, "%s", step3(1, text, &rc));
        CHECK(rc == SQM_OK || rc == SQM_ERR_RANGE || rc == SQM_ERR_FULL);
        if (rc != SQM_OK) fprintf(stderr, "  apart %s: %s\n", text, got);
        if (rc == SQM_OK) {
            done++;
            CHECK(same_values(text, got));
            if (integral_ok(text)) integrated++;
        }
    }
    CHECK(done > 140);
    CHECK(integrated == done);

    /* integrals: exact text for some, d/dx checks for all */
    struct {
        const char *in, *out;
    } in[] = {
        {"x^2", "x^3/3"},
        {"3x^2 - 2x + 1", "x^3 - x^2 + x"},
        {"1/x", "ln(abs(x))"},
        {"sin(2x + 1)", "-cos(2*x + 1)/2"},
        {"x e^x", "e^x*(x - 1)"},
        {"1/(x^2 + 1)", "atan(x)"},
        {"1/(x^2 - 1)", "ln(abs(x - 1))/2 - ln(abs(x + 1))/2"},
        {"ln(x)", "x*ln(x) - x"},
        {"tan(x)", "-ln(abs(cos(x)))"},
        {"2x cos(x^2)", "sin(x^2)"},
        {"sin(x)^2", "x/2 - sin(2*x)/4"},
        {"e^x sin(x)", "e^x*(sin(x) - cos(x))/2"},
        {"1/sqrt(x)", "2*sqrt(x)"},
        {"2^x", "2^x/ln(2)"},
        {"a x + b", "a*x^2/2 + b*x"},
        {"1/(a x + b)", "ln(abs(a*x + b))/a"},
        {"7", "7*x"},
    };
    for (size_t k = 0; k < sizeof in / sizeof in[0]; k++) {
        const char *got = step3(2, in[k].in, &rc);
        CHECK(rc == SQM_OK && !strcmp(got, in[k].out));
        if (rc != SQM_OK || strcmp(got, in[k].out)) fprintf(stderr, "  integral %s: %s\n", in[k].in, got);
    }
    static const char *const all[] = {
        "x^2", "3x^2 - 2x + 1", "1/x", "sin(2x + 1)", "e^(3x)", "x e^x", "x^2 sin(x)", "x cos(2x)", "2x cos(x^2)",
        "x/(x^2 + 1)", "1/(x^2 + 1)", "1/(x^2 - 1)", "(2x+3)/((x - 1)^2 (x + 2))", "tan(x)", "ln(x)", "x ln(x)",
        "sqrt(2x+1)", "1/sqrt(x)", "3/sqrt(2x + 1)", "2^x", "(x+1)^5", "x (x+1)^2", "cos(x)/sin(x)",
        "1/(x^2 + 2x + 5)", "sin(x)^2", "cos(3x)^2", "sin(x) cos(x)", "exp(-x/2)", "x^3 ln(x)", "x e^(-2x)",
        "x sin(-x)", "1/(x^2 - 2)", "x^2/(x + 1)", "1/(2x)", "e^x sin(x)", "exp(2x) cos(3x)", "x^2 e^x",
        "(x^2+1)/(x^3+3x)", "x e^(x^2)", "cos(x) e^(sin(x))", "ln(x)/x", "x/(x^2+1)^2", "x sqrt(x^2+1)",
        "x/sqrt(1 - x^2)", "cos(x)/sin(x)^2", "x^2 (x^3+1)^4", "sinh(3x)", "cosh(x/2)", "tanh(x)", "atan(x)",
        "asin(x/4)", "acos(x/4)", "log(x)", "cbrt(x)", "abs(x)", "1/(x - 1)^3", "x^(-2)", "5/(3x - 1)^2",
        "1/(x^2 + 4)", "(3x + 1)/(x^2 + 4)", "x^4 ln(x)", "x^5 e^(-x)", "(x^2 - 1) cos(x)", "pi x^2",
        "x (1 + x^2)^(1/3)", "1/(x (x + 1))", "1/(x^2 + x + 1)",
    };
    for (size_t k = 0; k < sizeof all / sizeof all[0]; k++) CHECK(integral_ok(all[k]));
    /* no form */
    CHECK(step3(2, "e^(x^2)", &rc) && rc == SQM_ERR_NOFORM);
    CHECK(step3(2, "sin(x^2)", &rc) && rc == SQM_ERR_NOFORM);
    CHECK(integral_ok("1/(x^2 + 1)^2") && integral_ok("(2x - 1)/(x^2 + x + 3)^3") && integral_ok("x/(x^2 + 4)^2"));
    CHECK(step3(2, "1/(x^2 - 2)^2", &rc) && rc == SQM_ERR_NOFORM); /* irrational real roots, squared */
    CHECK(step3(2, "x^x", &rc) && rc == SQM_ERR_NOFORM);
    CHECK(step3(2, "floor(x)", &rc) && rc == SQM_ERR_NOFORM);
    CHECK(sqm_antideriv(&ex2, NULL, &ex3) == SQM_ERR_SYNTAX && sqm_antideriv(&ex2, "x", &ex2) == SQM_ERR_SYNTAX);
    CHECK(!strcmp(sqm_strerror(SQM_ERR_NOFORM), "no exact form found"));
}

int main(void) {
    test_eval();
    test_errors();
    test_compile_and_print();
    test_diff();
    test_simplify();
    test_numeric();
    test_format();
    test_cas();
    test_fuzz();
    test_step3();
    printf("test_sqmath: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
