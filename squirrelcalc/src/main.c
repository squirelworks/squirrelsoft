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
          "  --roots          mark the roots\n"
          "  --crossings      mark where the functions cross\n"
          "  --table          show the table of values instead of the graph\n"
          "  --deriv          also graph d/dx y1\n"
          "  --snapshot FILE  draw one 900x600 frame to FILE (PPM) and exit, no window\n"
          "  --gui-frames N   close the window after N frames (testing)\n"
          "  --help           this text\n",
          f);
}

static CalcGui g; /* about 40 KB: not on the stack */

int main(int argc, char **argv) {
    calc_gui_init(&g);
    const char *snapshot = NULL;
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
