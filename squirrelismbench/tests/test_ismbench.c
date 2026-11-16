#include "ib_iface.h"
#include "ib_keyboard.h"
#include "ib_memory.h"
#include "ib_mouse.h"
#include "ib_plotter.h"
#include "ib_printer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *b = malloc((size_t)n + 1);
    if (!b) {
        fclose(f);
        return NULL;
    }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        fclose(f);
        return NULL;
    }
    b[n] = 0;
    fclose(f);
    return b;
}

int main(void) {
    char *kt = slurp("fixtures/keyboard-60/interface.ismhid");
    char *mt = slurp("fixtures/mouse/interface.ismhid");
    char *tt = slurp("fixtures/typewriter/interface.ismhid");
    char *memt = slurp("fixtures/memory/interface.ismem");
    char *prt = slurp("fixtures/printer/interface.ismprt");
    char *plt = slurp("fixtures/plotter/interface.ismplt");
    expect(kt && mt && tt && memt && prt && plt, "fixtures present");
    if (!kt || !mt || !tt || !memt || !prt || !plt) return 1;

    IbKeyboardIface k, tw;
    IbMouseIface m;
    IbMemoryIface mem;
    IbPrinterIface printer;
    IbPlotterIface plotter;
    char err[128];

    expect(ib_keyboard_parse(&k, kt, err, sizeof err) == 0, err);
    IbKeyTestResult kr;
    ib_keyboard_matrix_selftest(&k, &kr);
    expect(kr.ok == 1, kr.summary);

    expect(ib_mouse_parse(&m, mt, err, sizeof err) == 0, err);
    IbMouseTestResult mr;
    ib_mouse_selftest(&m, &mr);
    expect(mr.ok == 1, mr.summary);

    expect(ib_keyboard_parse(&tw, tt, err, sizeof err) == 0, err);
    expect(tw.rows == 4 && tw.cols == 12, "typewriter 4x12");
    IbKeyTestResult tr;
    ib_keyboard_matrix_selftest(&tw, &tr);
    expect(tr.ok == 1, tr.summary);

    expect(ib_memory_parse(&mem, memt, err, sizeof err) == 0, err);
    IbMemTestResult mer;
    ib_memory_selftest(&mem, &mer);
    expect(mer.ok == 1, mer.summary);

    expect(ib_printer_parse(&printer, prt, err, sizeof err) == 0, err);
    expect(printer.pid == 0x000F, "printer pid");
    IbPrinterTestResult pr;
    ib_printer_selftest(&printer, &pr);
    expect(pr.ok == 1, pr.summary);

    expect(ib_plotter_parse(&plotter, plt, err, sizeof err) == 0, err);
    expect(plotter.nwp >= 2, "plotter waypoints");
    IbPlotterTestResult plr;
    ib_plotter_selftest(&plotter, &plr);
    expect(plr.ok == 1, plr.summary);
    expect(strstr(plr.summary, "NR Ch.3.1") != NULL, "plotter NR cite");

    free(kt);
    free(mt);
    free(tt);
    free(memt);
    free(prt);
    free(plt);
    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    puts("test_ismbench ok");
    return 0;
}
