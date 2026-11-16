#include "ib_iface.h"
#include "ib_keyboard.h"
#include "ib_mouse.h"

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
    expect(kt && mt, "fixtures present");
    if (!kt || !mt) return 1;
    IbKeyboardIface k;
    IbMouseIface m;
    char err[128];
    expect(ib_keyboard_parse(&k, kt, err, sizeof err) == 0, err);
    expect(k.rows == 5 && k.cols == 14, "60% grid");
    expect(k.nkeys >= 5, "key map");
    IbKeyTestResult kr;
    ib_keyboard_matrix_selftest(&k, &kr);
    expect(kr.ok == 1, kr.summary);
    IbKeyMatrix mx;
    ib_keyboard_matrix_clear(&mx);
    expect(ib_keyboard_press(&mx, &k, "A") == 0, "press A");
    unsigned char rep[8];
    expect(ib_keyboard_boot_report(&k, &mx, rep) >= 1, "boot report");
    expect(ib_mouse_parse(&m, mt, err, sizeof err) == 0, err);
    IbMouseTestResult mr;
    ib_mouse_selftest(&m, &mr);
    expect(mr.ok == 1, mr.summary);
    free(kt);
    free(mt);
    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    puts("test_ismbench ok");
    return 0;
}
