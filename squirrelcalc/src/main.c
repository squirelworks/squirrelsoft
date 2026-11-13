/* SquirrelCalc: graphing calculator on libs/sqmath and libs/sqgui. Original code. */
#include "calcgui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
    fputs("usage: squirrelcalc [options]\n"
          "  --fn EXPR        graph EXPR as the next of y1..y4: a function of x, a curve\n"
          "                   of t, an equation (x^2 + y^2 = 25) or inequality (y < x)\n"
          "  --let NAME=EXPR  define a variable first (--let a=2)\n"
          "  --line TEXT      run a calculator line first: \"a = 2\", \"solve x^2 = 2\",\n"
          "                   \"expand (x+1)^3\", \"xs = {1, 2, 3}\", \"linreg xs ys\",\n"
          "                   \"5 ft to m\", \"units 5 kg * 9.81 m/s^2\"\n"
          "                   (with --snapshot the history is printed)\n"
          "  --csv FILE       load FILE into the spreadsheet (A1 is its first field)\n"
          "  --cell REF=TEXT  set a cell as typed: --cell A1=5, --cell \"B1==A1*2\"\n"
          "  --sheet          show the spreadsheet instead of the graph\n"
          "  --save-csv FILE  write the sheet's values to FILE as CSV (after the lines)\n"
          "  --save-sheet FILE  the same with formulas as typed\n"
          "  --roots          mark the roots\n"
          "  --crossings      mark where the functions cross\n"
          "  --table          show the table of values instead of the graph\n"
          "  --deriv          also graph d/dx y1\n"
          "  --snapshot FILE  draw one 900x600 frame to FILE (PPM) and exit, no window\n"
          "  --gui-frames N   close the window after N frames (testing)\n"
          "  --help           this text\n",
          f);
}

static CalcGui g; /* about 100 KB: not on the stack */

static char csv_buf[1 << 18];

static int load_csv(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(csv_buf, 1, sizeof csv_buf - 1, f);
    int big = !feof(f);
    fclose(f);
    csv_buf[n] = 0;
    int rc = calc_sheet_load_csv(&g.sheet, csv_buf);
    calc_gui_recalc(&g);
    if (rc < 0 || big) fprintf(stderr, "squirrelcalc: %s: cells past H50 were left out\n", path);
    return 0;
}

static int save_csv(const char *path, int formulas) {
    size_t n = calc_sheet_save_csv(&g.sheet, formulas, csv_buf, sizeof csv_buf);
    if (n >= sizeof csv_buf) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(csv_buf, 1, n, f) == n;
    return fclose(f) == 0 && ok ? 0 : -1;
}

int main(int argc, char **argv) {
    calc_gui_init(&g);
    const char *snapshot = NULL, *save = NULL;
    int save_formulas = 0;
    int frames = 0, nfn = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            return 0;
        } else if (!strcmp(a, "--fn") && more) {
            if (nfn >= CALC_N) {
                fprintf(stderr, "squirrelcalc: at most %d functions\n", CALC_N);
                return 2;
            }
            calc_gui_set(&g, nfn++, argv[++i]);
        } else if (!strcmp(a, "--let") && more) {
            const char *s = argv[++i];
            if (!strchr(s, '=') || calc_gui_eval(&g, s)) {
                fprintf(stderr, "squirrelcalc: --let %s: expected NAME=EXPR with a value\n", s);
                return 1;
            }
        } else if (!strcmp(a, "--line") && more) {
            calc_gui_eval(&g, argv[++i]);
        } else if (!strcmp(a, "--csv") && more) {
            if (load_csv(argv[++i])) {
                fprintf(stderr, "squirrelcalc: cannot read %s\n", argv[i]);
                return 1;
            }
        } else if (!strcmp(a, "--cell") && more) {
            const char *s = argv[++i], *eq = strchr(s, '=');
            char ref[8];
            if (!eq || eq - s >= (long)sizeof ref) {
                fprintf(stderr, "squirrelcalc: --cell %s: expected REF=TEXT\n", s);
                return 1;
            }
            memcpy(ref, s, (size_t)(eq - s));
            ref[eq - s] = 0;
            if (calc_gui_cell(&g, ref, eq + 1)) {
                fprintf(stderr, "squirrelcalc: --cell %s: not a cell from A1 to H50, or too long\n", s);
                return 1;
            }
        } else if (!strcmp(a, "--sheet")) {
            g.show_sheet = 1;
        } else if ((!strcmp(a, "--save-csv") || !strcmp(a, "--save-sheet")) && more) {
            save_formulas = !strcmp(a, "--save-sheet");
            save = argv[++i];
        } else if (!strcmp(a, "--roots")) {
            g.show_roots = 1;
        } else if (!strcmp(a, "--crossings")) {
            g.show_cross = 1;
        } else if (!strcmp(a, "--table")) {
            g.show_table = 1;
        } else if (!strcmp(a, "--deriv")) {
            g.show_deriv = 1;
        } else if (!strcmp(a, "--snapshot") && more) {
            snapshot = argv[++i];
        } else if (!strcmp(a, "--gui-frames") && more) {
            frames = atoi(argv[++i]);
        } else {
            usage(stderr);
            return 2;
        }
    }
    for (int k = 0; k < nfn; k++)
        if (g.err[k][0]) fprintf(stderr, "squirrelcalc: y%d: %s\n", k + 1, g.err[k]);
    if (save && save_csv(save, save_formulas)) {
        fprintf(stderr, "squirrelcalc: could not write %s\n", save);
        return 1;
    }

    if (snapshot) {
        enum { W = 900, H = 600 };
        static uint32_t px[W * H];
        sqg_canvas cv;
        sqg_canvas_init(&cv, px, W, H, W);
        sqg_ui ui;
        sqg_ui_init(&ui, NULL, 1);
        sqg_input in = {0};
        in.mx = in.my = -1;
        for (int k = 0; k < 2; k++) { /* the second frame shows the roots found in the first */
            sqg_begin(&ui, &cv, &in);
            calc_gui_frame(&g, &ui);
            sqg_end(&ui);
        }
        if (sqg_write_ppm(&cv, snapshot)) {
            fprintf(stderr, "squirrelcalc: could not write %s\n", snapshot);
            return 1;
        }
        if (g.hist[0]) printf("%s\n", g.hist);
        if (g.status[0]) printf("%s\n", g.status);
        return 0;
    }
    return calc_gui_run(&g, frames);
}
