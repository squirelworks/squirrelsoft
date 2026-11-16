#ifndef IB_KEYBOARD_H
#define IB_KEYBOARD_H

#include "ib_iface.h"

typedef struct {
    unsigned char matrix[IB_MAX_ROWS][IB_MAX_COLS]; /* 1 = pressed */
} IbKeyMatrix;

typedef struct {
    int ok;
    int npress;
    int nfail;
    char summary[IB_MSG_LEN];
} IbKeyTestResult;

void ib_keyboard_matrix_clear(IbKeyMatrix *mx);
int ib_keyboard_press(IbKeyMatrix *mx, const IbKeyboardIface *k, const char *key_name);
int ib_keyboard_release(IbKeyMatrix *mx, const IbKeyboardIface *k, const char *key_name);

/* Synthetic matrix walk: press each mapped key, expect one bit, release. */
int ib_keyboard_matrix_selftest(const IbKeyboardIface *k, IbKeyTestResult *res);

/* Build a boot-protocol style report (8 bytes) for currently pressed keys. */
int ib_keyboard_boot_report(const IbKeyboardIface *k, const IbKeyMatrix *mx,
                            unsigned char out[8]);

#endif
