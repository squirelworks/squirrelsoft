#include "ib_iface.h"
#include "ib_keyboard.h"
#include "ib_memory.h"
#include "ib_mouse.h"
#include "ib_hid.h"
#include "ib_plotter.h"
#include "ib_power.h"
#include "ib_printer.h"
#include "ib_storage.h"

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

    {
        IbHidSession sess;
        IbHidTestResult hr;
        char herr[128];
        expect(ib_hid_connect("mock", &sess, herr, sizeof herr) == 0, herr);
        ib_hid_selftest(&sess, &hr);
        expect(hr.ok == 1, hr.summary);
        expect(ib_hid_connect("/dev/hidraw0", &sess, herr, sizeof herr) == -1, "hid refuse");
        expect(strstr(herr, "deferred") != NULL, herr);
        char *hx = slurp("fixtures/hid/mock-hid-session.sqhid");
        expect(hx != NULL, "hid fixture");
        if (hx) {
            expect(ib_hid_session_from_text(&sess, hx, herr, sizeof herr) == 0, herr);
            ib_hid_selftest(&sess, &hr);
            expect(hr.ok == 1, hr.summary);
            free(hx);
        }
        /* Framing round-trip of a known A-key report. */
        {
            IbHidReport r, back;
            char line[160];
            memset(&r, 0, sizeof r);
            r.kind = IB_HID_KIND_BOOT_KEYBOARD;
            r.len = 8;
            r.data[2] = 0x04;
            expect(ib_hid_encode_report(line, sizeof line, &r) > 0, "enc report");
            expect(ib_hid_parse_line(line, &back, NULL, NULL, NULL, NULL, 0) == 0, "parse");
            expect(back.data[2] == 0x04 && back.len == 8, "A usage");
        }
    }

    {
        char *st = slurp("fixtures/storage/interface.ismstor");
        expect(st != NULL, "storage fixture");
        if (st) {
            IbStorageIface stor;
            char serr[128];
            expect(ib_storage_parse(&stor, st, serr, sizeof serr) == 0, serr);
            expect(strcmp(stor.media, "sd_card") == 0, "sd media");
            expect(stor.block_bytes == 512, "512 block");
            IbStorageTestResult sr;
            ib_storage_selftest(&stor, &sr);
            expect(sr.ok == 1, sr.summary);
            expect(strstr(sr.summary, "NR Ch.7.1") != NULL, "storage NR");
            free(st);
        }
        {
            IbStorageIface bad;
            char berr[128];
            const char *bad_txt =
                "ISM-STOR storage interface 1\n"
                "media: tape\n"
                "part: x\n"
                "block_bytes: 512\n"
                "capacity_kib: 64\n"
                "pattern_bytes: 512\n";
            expect(ib_storage_parse(&bad, bad_txt, berr, sizeof berr) != 0, "reject bad media");
            expect(strstr(berr, "media") != NULL, berr);
        }
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
