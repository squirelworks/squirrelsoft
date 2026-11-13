/* SquirrelCalc: the spreadsheet core, its functions, quartiles, histograms
 * and the distributions. */
#include "calcsheet.h"
#include "calcstats.h"
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

static CalcSheet s, t;

/* set by name: "B2" */
static void put(const char *ref, const char *text) {
    int r, c;
    if (!calc_cell_parse(ref, &r, &c, NULL) || calc_sheet_set(&s, r, c, text)) {
        fprintf(stderr, "bad cell %s\n", ref);
        failures++;
    }
}

static double val(const char *ref) {
    int r, c;
    calc_cell_parse(ref, &r, &c, NULL);
    return s.val[r][c];
}

static int bad(const char *ref) {
    int r, c;
    calc_cell_parse(ref, &r, &c, NULL);
    return s.bad[r][c];
}

static const char *err(const char *ref) {
    int r, c;
    calc_cell_parse(ref, &r, &c, NULL);
    return s.err[r][c];
}

/* the calculator's side: a = 2, and the list xs */
static const char *NAMES[] = {"a"};
static const double VALUES[] = {2};
static int list_cb(const void *ctx, const char *name, double *out, int cap) {
    (void)ctx;
    static const double XS[] = {4, 8, 15, 16, 23, 42};
    if (strcmp(name, "xs")) return -1;
    int n = 6 < cap ? 6 : cap;
    memcpy(out, XS, (size_t)n * sizeof *out);
    return n;
}
static const CalcEnv ENV = {NAMES, VALUES, 1, list_cb, NULL};

/* expand then evaluate with a, as the calculator line does */
static double calc(const char *text) {
    char buf[1024], msg[96];
    SqmExpr e;
    double v;
    if (calc_expand(text, buf, sizeof buf, &s, &ENV, msg, sizeof msg)) return NAN;
    if (sqm_parse(buf, &e, NULL) != SQM_OK || sqm_eval(&e, NAMES, VALUES, 1, &v) != SQM_OK) return NAN;
    return v;
}

static const char *calc_err(const char *text) {
    static char msg[96];
    char buf[1024];
    msg[0] = 0;
    if (calc_expand(text, buf, sizeof buf, &s, &ENV, msg, sizeof msg) == 0) return "";
    return msg;
}

static void test_names(void) {
    int r, c, abs;
    CHECK(calc_cell_parse("B12", &r, &c, &abs) == 3 && r == 11 && c == 1 && abs == 0);
    CHECK(calc_cell_parse("$C$3+1", &r, &c, &abs) == 4 && r == 2 && c == 2 && abs == 3);
    CHECK(calc_cell_parse("C$3", &r, &c, &abs) == 3 && abs == 2);
    CHECK(calc_cell_parse("H50", &r, &c, NULL) == 3 && r == 49 && c == 7);
    CHECK(!calc_cell_parse("I1", &r, &c, NULL) && !calc_cell_parse("A0", &r, &c, NULL));
    CHECK(!calc_cell_parse("A51", &r, &c, NULL) && !calc_cell_parse("A100000000000", &r, &c, NULL));
    CHECK(!calc_cell_parse("B12x", &r, &c, NULL) && !calc_cell_parse("b2", &r, &c, NULL));
    char name[16];
    calc_cell_name(49, 7, name, sizeof name);
    CHECK(!strcmp(name, "H50"));
    CHECK(calc_needs_expand("A1 + 1") && calc_needs_expand("sum(xs)") && calc_needs_expand("normalpdf (x)"));
    CHECK(!calc_needs_expand("x^2 + sum") && !calc_needs_expand("2E5 + e") && !calc_needs_expand("ABS(x)"));
}

static void test_cells(void) {
    calc_sheet_init(&s);
    put("A1", "2");
    put("A2", "=A1*3");
    put("A3", " =A2 + A1 ");
    put("B1", "hello");
    put("B2", "1e3");
    put("B3", "  ");
    put("C1", "=B1 + 1");
    put("C2", "=C1 * 2");
    put("C3", "=B3 + a");
    CHECK(s.kind[0][0] == CALC_CELL_NUMBER && s.kind[1][0] == CALC_CELL_FORMULA && s.kind[0][1] == CALC_CELL_TEXT);
    CHECK(s.kind[2][1] == CALC_CELL_EMPTY && s.kind[1][1] == CALC_CELL_NUMBER);
    int r, c;
    calc_cell_parse("A1", &r, &c, NULL);
    CHECK(calc_sheet_set(&s, 50, 0, "1") == -1 && calc_sheet_set(&s, 0, 8, "1") == -1);
    char longt[80];
    memset(longt, '1', sizeof longt - 1);
    longt[79] = 0;
    CHECK(calc_sheet_set(&s, 0, 7, longt) == -1);
    calc_sheet_recalc(&s, &ENV);
    NEAR(val("A2"), 6, 0);
    NEAR(val("A3"), 8, 0);
    NEAR(val("B2"), 1000, 0);
    CHECK(bad("C1") && contains(err("C1"), "B1 is text"));
    CHECK(bad("C2") && contains(err("C2"), "C1 has an error"));
    NEAR(val("C3"), 2, 0); /* an empty cell is 0, and a is the calculator's */
    /* a change flows through on the next recalculation */
    put("A1", "10");
    calc_sheet_recalc(&s, &ENV);
    NEAR(val("A3"), 40, 0);
    /* cycles: every cell on one is an error, and nothing hangs */
    put("D1", "=D2 + 1");
    put("D2", "=D3 + 1");
    put("D3", "=D1 + 1");
    put("D4", "=D4");
    put("D5", "=D1 * 0");
    calc_sheet_recalc(&s, &ENV);
    CHECK(bad("D1") && bad("D2") && bad("D3") && contains(err("D3"), "cycle"));
    CHECK(bad("D4") && contains(err("D4"), "a cycle through D4"));
    CHECK(bad("D5"));
    /* breaking the cycle mends them all */
    put("D3", "5");
    calc_sheet_recalc(&s, &ENV);
    CHECK(!bad("D1") && !bad("D5"));
    NEAR(val("D1"), 7, 0);
    /* formula errors */
    put("E1", "=");
    put("E2", "=2 +");
    put("E3", "=q + 1");
    put("E4", "=1/0");
    calc_sheet_recalc(&s, &ENV);
    CHECK(bad("E1") && bad("E2") && bad("E3") && contains(err("E3"), "define q first"));
    CHECK(!bad("E4") && isinf(val("E4")));
    /* a chain the length of a column */
    calc_sheet_init(&s);
    put("A1", "1");
    char ref[8], f[16];
    for (int k = 2; k <= CALC_SHEET_ROWS; k++) {
        snprintf(ref, sizeof ref, "A%d", k);
        snprintf(f, sizeof f, "=A%d+1", k - 1);
        put(ref, f);
    }
    for (int k = 1; k <= CALC_SHEET_ROWS; k++) { /* and a sum over each of the column above it */
        snprintf(ref, sizeof ref, "B%d", k);
        snprintf(f, sizeof f, "=sum(A1:A%d)", k);
        put(ref, f);
    }
    calc_sheet_recalc(&s, &ENV);
    NEAR(val("A50"), 50, 0);
    NEAR(val("B50"), 1275, 0);
}

static void test_functions(void) {
    calc_sheet_init(&s);
    const char *col[] = {"3", "1", "4", "1", "5", "9", "2", "6"};
    for (int k = 0; k < 8; k++) {
        char ref[8];
        snprintf(ref, sizeof ref, "A%d", k + 1);
        put(ref, col[k]);
    }
    put("B1", "label");
    put("B2", "10");
    put("C1", "=sum(A1:A8)");
    calc_sheet_recalc(&s, &ENV);
    NEAR(val("C1"), 31, 0);
    NEAR(calc("sum(A1:A8)"), 31, 0);
    NEAR(calc("sum(A:A)"), 31, 0);
    NEAR(calc("mean(A1:A8)"), 31.0 / 8, 1e-15);
    NEAR(calc("median(A:A)"), 3.5, 0);
    NEAR(calc("q1(A:A)"), 1.5, 0);
    NEAR(calc("q3(A:A)"), 5.5, 0);
    NEAR(calc("min(A1:A8)"), 1, 0);
    NEAR(calc("max(A:A)"), 9, 0);
    NEAR(calc("count(A1:B8)"), 9, 0);   /* the label is left out */
    NEAR(calc("count(C5:D9)"), 0, 0);
    NEAR(calc("sum(C5:D9)"), 0, 0);
    NEAR(calc("sum(A1:B2)"), 14, 0);
    NEAR(calc("sum(B2:A1)"), 14, 0);    /* corners in any order */
    NEAR(calc("sd(1, 2, 3, 4)"), sqrt(5.0 / 3), 1e-15);
    NEAR(calc("var(1, 2, 3, 4)"), 5.0 / 3, 1e-15);
    NEAR(calc("sum(1, 2, A1:A2, a)"), 9, 0);
    NEAR(calc("mean(xs)"), 18, 0);
    NEAR(calc("max(xs) - min(xs)"), 38, 0);
    NEAR(calc("min(2, a + 1)"), 2, 0);  /* two plain values: sqmath's min */
    NEAR(calc("2 sum(A1:A2) + A3"), 12, 0);
    NEAR(calc("sum(mean(A1:A2), 1)"), 3, 0);
    NEAR(calc("1.5E1 + B2"), 25, 0);    /* the exponent is not a cell */
    CHECK(isnan(calc("A1A2")));
    CHECK(contains(calc_err("A1:A3 + 1"), "a range goes inside"));
    CHECK(contains(calc_err("B1 + 1"), "B1 is text"));
    CHECK(contains(calc_err("mean(C5:C9)"), "no values"));
    CHECK(contains(calc_err("sum(A1:A2"), "is not closed"));
    CHECK(contains(calc_err("sum(1,,2)"), "missing"));
    CHECK(contains(calc_err("normalcdf(0, x)"), "x has no value"));
    CHECK(contains(calc_err("ncr(3)"), "wrong number"));
    CHECK(contains(calc_err("invnorm(1.5)"), "between 0 and 1"));
    CHECK(contains(calc_err("ncr(3, 5)"), "out of its range"));
    /* distributions in expressions */
    NEAR(calc("normalcdf(-1, 1)"), 0.6826894921370859, 1e-15);
    NEAR(calc("normalcdf(-1e99, 0, 100, 15)") / 1.3083924686053004e-11, 1, 1e-9);
    NEAR(calc("normalcdf(100, 1e99, 100, 15)"), 0.5, 1e-15);
    NEAR(calc("invnorm(0.975)"), 1.959963984540054, 1e-12);
    NEAR(calc("invnorm(0.5, 100, 15)"), 100, 1e-12);
    NEAR(calc("binompdf(10, 0.5, 5)"), 0.24609375, 1e-15);
    NEAR(calc("binomcdf(10, 0.5, 5)"), 0.623046875, 1e-14);
    NEAR(calc("poissonpdf(3, 2)"), 0.22404180765538775, 1e-15);
    NEAR(calc("poissoncdf(3, 2)"), 0.42319008112684353, 1e-15);
    NEAR(calc("ncr(52, 5)"), 2598960, 0);
    NEAR(calc("npr(10, 3)"), 720, 0);
    NEAR(calc("normalpdf(0)"), 0.3989422804014327, 1e-15);
    NEAR(calc("normalpdf(130, 100, 15)"), 0.0035993977675458705, 1e-17);
    /* normalpdf is written out: it still has x in it, so it can be graphed */
    char buf[600], msg[96];
    CHECK(calc_expand("y = normalpdf(x, 1, 2)", buf, sizeof buf, NULL, NULL, msg, sizeof msg) == 0);
    CHECK(!strncmp(buf, "y = (exp(", 9));
    SqmExpr e;
    const char *xn[] = {"x"};
    double xv[] = {1}, v = 0;
    CHECK(sqm_parse(buf + 4, &e, NULL) == SQM_OK && sqm_eval(&e, xn, xv, 1, &v) == SQM_OK);
    NEAR(v, 0.3989422804014327 / 2, 1e-15);
    CHECK(calc_expand("normalpdf(x, 1)", buf, sizeof buf, NULL, NULL, msg, sizeof msg) == -1);
    /* with no sheet, cell names are just names */
    CHECK(calc_expand("A1 + 1", buf, sizeof buf, NULL, NULL, msg, sizeof msg) == 0 && !strcmp(buf, "A1 + 1"));
    /* too long for the output */
    char small[3];
    CHECK(calc_expand("sum(1, 2, 3)", small, sizeof small, &s, &ENV, msg, sizeof msg) == -1 &&
          contains(msg, "too long"));
    /* calc_sheet_values: numbers only, row by row */
    double out[16];
    CHECK(calc_sheet_values(&s, "A1:B2", out, 16) == 3 && out[0] == 3 && out[1] == 1 && out[2] == 10);
    CHECK(calc_sheet_values(&s, "A:A", out, 4) == 4 && out[3] == 1);
    CHECK(calc_sheet_values(&s, "C1", out, 16) == 1 && out[0] == 31);
    CHECK(calc_sheet_values(&s, "xs", out, 16) == -1 && calc_sheet_values(&s, "A1:", out, 16) == -1);
    CHECK(calc_sheet_values(&s, "A:Z", out, 16) == -1);
}

static void test_copy_csv(void) {
    char out[128];
    CHECK(calc_shift_refs("=A1+$B$2+B$3+$C4", 1, 1, out, sizeof out) == 0 && !strcmp(out, "=B2+$B$2+C$3+$C5"));
    CHECK(calc_shift_refs("=sum(A1:A3)*2E5+e", 2, 0, out, sizeof out) == 0 && !strcmp(out, "=sum(A3:A5)*2E5+e"));
    CHECK(calc_shift_refs("=mean(A:B)", 0, 1, out, sizeof out) == 0 && !strcmp(out, "=mean(B:C)"));
    CHECK(calc_shift_refs("=A1", -1, 0, out, sizeof out) == -1 && !strcmp(out, "=#REF"));
    CHECK(calc_shift_refs("=H1+1", 0, 1, out, sizeof out) == -1);
    CHECK(calc_shift_refs("=A1B2+x", 1, 0, out, sizeof out) == 0 && !strcmp(out, "=A1B2+x"));
    calc_sheet_init(&s);
    put("A1", "1");
    put("A2", "=A1*2");
    for (int r = 2; r < 10; r++) calc_sheet_copy(&s, 1, 0, r, 0);
    put("B1", "=$A$1 + A1");
    calc_sheet_copy(&s, 0, 1, 0, 2);
    CHECK(!strcmp(s.text[9][0], "=A9*2") && !strcmp(s.text[0][2], "=$A$1 + B1"));
    CHECK(calc_sheet_copy(&s, 1, 0, 0, 0) == -1 && s.kind[0][0] == CALC_CELL_FORMULA); /* =#REF*2 */
    put("A1", "1");
    calc_sheet_recalc(&s, &ENV);
    NEAR(val("A10"), 512, 0);
    NEAR(val("C1"), 3, 0);

    /* CSV in and out */
    const char *csv = "x,y,note\r\n1,2.5,\"a, \"\"quoted\"\" note\"\n2,=A3*B2,\n,,last\n";
    CHECK(calc_sheet_load_csv(&s, csv) == 9);
    calc_sheet_recalc(&s, &ENV);
    CHECK(!strcmp(s.text[1][2], "a, \"quoted\" note") && s.kind[0][0] == CALC_CELL_TEXT);
    NEAR(val("B3"), 5, 0);
    char buf[512];
    size_t n = calc_sheet_save_csv(&s, 0, buf, sizeof buf);
    CHECK(n == strlen(buf));
    CHECK(!strcmp(buf, "x,y,note\n1,2.5,\"a, \"\"quoted\"\" note\"\n2,5,\n,,last\n"));
    calc_sheet_save_csv(&s, 1, buf, sizeof buf);
    CHECK(contains(buf, "2,=A3*B2,\n"));
    CHECK(calc_sheet_load_csv(&t, buf) == 9 && !strcmp(t.text[2][1], "=A3*B2"));
    char tiny[10];
    CHECK(calc_sheet_save_csv(&s, 0, tiny, sizeof tiny) == n && strlen(tiny) == 9);
    /* past H or row 50: loaded as far as it goes */
    CHECK(calc_sheet_load_csv(&s, "1,2,3,4,5,6,7,8,9\n") == -1 && s.kind[0][7] == CALC_CELL_NUMBER);
    char many[600] = "";
    for (int r = 0; r < 52; r++) strcat(many, "1\n");
    CHECK(calc_sheet_load_csv(&s, many) == -1 && s.kind[49][0] == CALC_CELL_NUMBER);
    CHECK(calc_sheet_load_csv(&s, "") == 0 && calc_sheet_save_csv(&s, 0, buf, sizeof buf) == 0);
    put("A1", "=1/0");
    put("B1", "=q");
    calc_sheet_recalc(&s, &ENV);
    calc_sheet_save_csv(&s, 0, buf, sizeof buf);
    CHECK(!strcmp(buf, "undefined,#ERR\n"));
}

static void test_stats_more(void) {
    CalcStats st;
    const double a[] = {1, 2, 3, 4, 5, 6, 7, 8}, b[] = {7, 1, 2, 6, 3, 5, 4}, one[] = {5};
    CHECK(calc_stats(a, 8, &st) == 0);
    NEAR(st.q1, 2.5, 0);
    NEAR(st.q3, 6.5, 0);
    NEAR(st.var, 6, 1e-12);
    NEAR(st.sd_pop, sqrt(5.25), 1e-12);
    CHECK(calc_stats(b, 7, &st) == 0);
    NEAR(st.q1, 2, 0);
    NEAR(st.q3, 6, 0);
    NEAR(st.median, 4, 0);
    CHECK(calc_stats(one, 1, &st) == 0 && st.q1 == 5 && st.q3 == 5 && st.var == 0 && st.sd_pop == 0);

    /* histograms */
    int cnt[16];
    double start = 0, width = 0;
    const double h[] = {1, 2, 2, 3, 3, 3, 4, 4, 5, 9};
    int bins = calc_histogram(h, 10, &start, &width, cnt, 16);
    CHECK(bins == 5 && start == 0 && width == 2);
    CHECK(cnt[0] == 1 && cnt[1] == 5 && cnt[2] == 3 && cnt[3] == 0 && cnt[4] == 1);
    start = 1;
    width = 1;
    bins = calc_histogram(h, 10, &start, &width, cnt, 16);
    CHECK(bins == 8 && cnt[0] == 1 && cnt[1] == 2 && cnt[7] == 1); /* 9 closes the last bin [8, 9] */
    start = 0;
    width = 0;
    const double same[] = {3, 3, 3};
    CHECK(calc_histogram(same, 3, &start, &width, cnt, 16) == 1 && cnt[0] == 3 && width == 1 && start == 3);
    start = 0;
    width = 0.001;
    CHECK(calc_histogram(h, 10, &start, &width, cnt, 4) == 4); /* capped */
    const double nanv[] = {1, NAN};
    CHECK(calc_histogram(nanv, 2, &start, &width, cnt, 4) == -1 && calc_histogram(h, 0, &start, &width, cnt, 4) == -1);
    start = 0;
    width = 0;
    const double big[] = {1003, 1017, 1042, 1088, 1091};
    bins = calc_histogram(big, 5, &start, &width, cnt, 16);
    int total = 0;
    for (int k = 0; k < bins; k++) total += cnt[k];
    CHECK(total == 5 && start <= 1003 && start + bins * width >= 1091 && width == 20);

    /* distributions against known values */
    NEAR(calc_normal_cdf(-1.96, 1.96, 0, 1), 0.9500042097035591, 1e-15);
    NEAR(calc_normal_cdf(8, INFINITY, 0, 1) / 6.22096057427178e-16, 1, 1e-9); /* the upper tail keeps its digits */
    NEAR(calc_normal_cdf(-INFINITY, -8, 0, 1) / 6.22096057427178e-16, 1, 1e-9);
    CHECK(isnan(calc_normal_cdf(0, 1, 0, 0)) && isnan(calc_normal_pdf(0, 0, -1)));
    NEAR(calc_inv_norm(1e-10, 0, 1), -6.361340902404056, 1e-9);
    NEAR(calc_inv_norm(0.841344746068543, 0, 1), 1, 1e-12);
    for (double p = 0.001; p < 1; p += 0.0731)
        NEAR(calc_normal_cdf(-INFINITY, calc_inv_norm(p, 3, 2), 3, 2), p, 1e-14);
    CHECK(isnan(calc_inv_norm(0, 0, 1)) && isnan(calc_inv_norm(1, 0, 1)));
    NEAR(calc_binom_pdf(20, 0.3, 6), 0.19163898275344238, 1e-15);
    NEAR(calc_binom_cdf(20, 0.3, 6), 0.6080098122009233, 1e-14);
    CHECK(calc_binom_pdf(5, 0, 0) == 1 && calc_binom_pdf(5, 1, 5) == 1 && calc_binom_pdf(5, 0.5, 2.5) == 0);
    CHECK(calc_binom_cdf(5, 0.5, -1) == 0 && calc_binom_cdf(5, 0.5, 9) == 1 && isnan(calc_binom_pdf(5, 2, 1)));
    NEAR(calc_poisson_cdf(100, 100), 0.5265621985299985, 1e-13);
    CHECK(calc_poisson_pdf(2, -1) == 0 && isnan(calc_poisson_pdf(0, 1)));
    NEAR(calc_ncr(60, 30), 118264581564861424.0, 1e3);
    CHECK(calc_ncr(5, 0) == 1 && calc_npr(5, 0) == 1 && isnan(calc_ncr(2.5, 1)) && isnan(calc_npr(3, 4)));
}

int main(void) {
    test_names();
    test_cells();
    test_functions();
    test_copy_csv();
    test_stats_more();
    printf("test_sheet: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
