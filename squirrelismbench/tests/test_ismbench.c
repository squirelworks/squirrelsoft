#include "ib_hid.h"
#include "ib_iface.h"
#include "ib_keyboard.h"
#include "ib_memory.h"
#include "ib_mouse.h"
#include "ib_plotter.h"
#include "ib_power.h"
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
    char *psu = slurp("fixtures/psu/interface.ismpsu");
    char *ups = slurp("fixtures/ups/interface.ismups");
    expect(kt && mt && tt && memt && prt && plt && psu && ups, "fixtures present");
    if (!kt || !mt || !tt || !memt || !prt || !plt || !psu || !ups) return 1;

    IbKeyboardIface k, tw;
    IbMouseIface m;
    IbMemoryIface mem;
    IbPrinterIface printer;
    IbPlotterIface plotter;
    IbPowerIface psu_i, ups_i;
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

    expect(ib_power_parse(&psu_i, psu, err, sizeof err) == 0, err);
    expect(psu_i.kind == IB_POWER_PSU, "psu kind");
    expect(psu_i.nsteps == 5, "psu steps");
    IbPowerTestResult psr;
    ib_power_selftest(&psu_i, &psr);
    expect(psr.ok == 1, psr.summary);
    expect(strstr(psr.summary, "NR Ch.4.1") != NULL, "psu NR cite");
    expect(psr.energy_wh > 0.0, "psu energy");

    expect(ib_power_parse(&ups_i, ups, err, sizeof err) == 0, err);
    expect(ups_i.kind == IB_POWER_UPS, "ups kind");
    expect(ups_i.capacity_wh == 50.0, "ups capacity");
    IbPowerTestResult usr;
    ib_power_selftest(&ups_i, &usr);
    expect(usr.ok == 1, usr.summary);
    expect(usr.energy_wh <= ups_i.capacity_wh + 1e-9, "ups energy under capacity");

    /* Over-nameplate step must fail. */
    {
        IbPowerIface bad = psu_i;
        bad.load_w[bad.nsteps - 1] = bad.p_max_w + 10.0;
        IbPowerTestResult br;
        ib_power_selftest(&bad, &br);
        expect(br.ok == 0, "overcurrent fail");
    }

    /* S8.2 Soft HID path: file: round-trip + live stand-down. */
    {
        IbHidTransport ht, live;
        IbHidTestResult hr;
        char herr[128];
        expect(ib_hid_open(&ht, "file:build/test-kbd.ismhidcap", IB_HID_DEV_KEYBOARD, herr,
                           (int)sizeof(herr)) == 0,
               "hid open kbd file");
        ib_hid_keyboard_path_selftest(&k, &ht, &hr);
        expect(hr.ok == 1, hr.summary);
        expect(strstr(hr.summary, "PASS") != NULL, "kbd hid PASS");
        ib_hid_close(&ht);

        expect(ib_hid_open(&ht, "file:build/test-mouse.ismhidcap", IB_HID_DEV_MOUSE, herr,
                           (int)sizeof(herr)) == 0,
               "hid open mouse file");
        ib_hid_mouse_path_selftest(&m, &ht, &hr);
        expect(hr.ok == 1, hr.summary);
        ib_hid_close(&ht);

        expect(ib_hid_open(&live, "live", IB_HID_DEV_KEYBOARD, herr, (int)sizeof(herr)) == 0,
               "hid open live");
        expect(live.live_refused == 1, "live refused");
        ib_hid_keyboard_path_selftest(&k, &live, &hr);
        expect(hr.ok == 1, "live standdown ok");
        expect(strstr(hr.summary, "STANDDOWN") != NULL, "live standdown text");
        ib_hid_close(&live);
    }

    free(kt);
    free(mt);
    free(tt);
    free(memt);
    free(prt);
    free(plt);
    free(psu);
    free(ups);
    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    puts("test_ismbench ok");
    return 0;
}
