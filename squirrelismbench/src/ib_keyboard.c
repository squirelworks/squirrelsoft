#include "ib_keyboard.h"

#include <stdio.h>
#include <string.h>

/* Minimal HID usage IDs for a few keys (boot protocol). */
static int hid_usage(const char *name) {
    if (strcmp(name, "A") == 0) return 0x04;
    if (strcmp(name, "S") == 0) return 0x16;
    if (strcmp(name, "D") == 0) return 0x07;
    if (strcmp(name, "F") == 0) return 0x09;
    if (strcmp(name, "SPC") == 0) return 0x2c;
    if (strcmp(name, "ENT") == 0) return 0x28;
    if (strcmp(name, "ESC") == 0) return 0x29;
    return 0;
}

static int find_key(const IbKeyboardIface *k, const char *name) {
    int i;
    for (i = 0; i < k->nkeys; i++)
        if (strcmp(k->key_name[i], name) == 0) return i;
    return -1;
}

void ib_keyboard_matrix_clear(IbKeyMatrix *mx) { memset(mx, 0, sizeof *mx); }

int ib_keyboard_press(IbKeyMatrix *mx, const IbKeyboardIface *k, const char *key_name) {
    int i = find_key(k, key_name);
    if (i < 0) return -1;
    int r = k->key_row[i], c = k->key_col[i];
    if (r < 0 || c < 0 || r >= k->rows || c >= k->cols) return -1;
    mx->matrix[r][c] = 1;
    return 0;
}

int ib_keyboard_release(IbKeyMatrix *mx, const IbKeyboardIface *k, const char *key_name) {
    int i = find_key(k, key_name);
    if (i < 0) return -1;
    int r = k->key_row[i], c = k->key_col[i];
    if (r < 0 || c < 0 || r >= k->rows || c >= k->cols) return -1;
    mx->matrix[r][c] = 0;
    return 0;
}

int ib_keyboard_matrix_selftest(const IbKeyboardIface *k, IbKeyTestResult *res) {
    memset(res, 0, sizeof *res);
    res->ok = 1;
    IbKeyMatrix mx;
    int i;
    for (i = 0; i < k->nkeys; i++) {
        ib_keyboard_matrix_clear(&mx);
        if (ib_keyboard_press(&mx, k, k->key_name[i]) != 0) {
            res->nfail++;
            res->ok = 0;
            continue;
        }
        int r = k->key_row[i], c = k->key_col[i];
        if (!mx.matrix[r][c]) {
            res->nfail++;
            res->ok = 0;
            continue;
        }
        /* Exactly one cell pressed */
        int count = 0, rr, cc;
        for (rr = 0; rr < k->rows; rr++)
            for (cc = 0; cc < k->cols; cc++)
                if (mx.matrix[rr][cc]) count++;
        if (count != 1) {
            res->nfail++;
            res->ok = 0;
            continue;
        }
        ib_keyboard_release(&mx, k, k->key_name[i]);
        if (mx.matrix[r][c]) {
            res->nfail++;
            res->ok = 0;
            continue;
        }
        res->npress++;
    }
    snprintf(res->summary, sizeof res->summary,
             "keyboard matrix selftest layout=%s keys=%d pressed_ok=%d fail=%d → %s",
             k->layout, k->nkeys, res->npress, res->nfail, res->ok ? "PASS" : "FAIL");
    return 0;
}

int ib_keyboard_boot_report(const IbKeyboardIface *k, const IbKeyMatrix *mx,
                            unsigned char out[8]) {
    memset(out, 0, 8);
    int slot = 2; /* bytes 2..7 are keycodes */
    int i;
    for (i = 0; i < k->nkeys && slot < 8; i++) {
        int r = k->key_row[i], c = k->key_col[i];
        if (r < 0 || c < 0 || r >= k->rows || c >= k->cols) continue;
        if (!mx->matrix[r][c]) continue;
        int u = hid_usage(k->key_name[i]);
        if (!u) continue;
        out[slot++] = (unsigned char)u;
    }
    return slot - 2;
}
