#ifndef IB_HID_H
#define IB_HID_H

#include "ib_iface.h"

#include <stdio.h>

#define IB_HID_REPORT_MAX 64
#define IB_HID_PATH_LEN 256

typedef enum {
    IB_HID_DEV_KEYBOARD = 1,
    IB_HID_DEV_MOUSE = 2
} IbHidDeviceKind;

typedef enum {
    IB_HID_BACKEND_FILE = 1,
    IB_HID_BACKEND_LIVE = 2
} IbHidBackend;

typedef struct {
    IbHidBackend backend;
    IbHidDeviceKind kind;
    int report_len; /* 8 keyboard boot, 4 mouse boot */
    FILE *fp;       /* file: transport only */
    char path[IB_HID_PATH_LEN];
    int live_refused; /* 1 if live USB host path not available yet */
    char err[IB_MSG_LEN];
} IbHidTransport;

typedef struct {
    int ok;
    int nfail;
    char summary[IB_MSG_LEN];
} IbHidTestResult;

/*
 * Open Soft HID transport.
 * spec forms:
 *   file:PATH   — Soft capture stream (CI / operator replay)
 *   live        — real USB HID host path (refused until Soft host USB lands)
 * Returns 0 on success. For live, returns 0 with live_refused=1 and a clear
 * message in t->err (expected Soft stand-down until hardware host path).
 */
int ib_hid_open(IbHidTransport *t, const char *spec, IbHidDeviceKind kind, char *err,
                int errlen);

void ib_hid_close(IbHidTransport *t);

int ib_hid_write_report(IbHidTransport *t, const unsigned char *rep, int n);
int ib_hid_read_report(IbHidTransport *t, unsigned char *rep, int nmax, int *nout);

/* Rewind Soft capture for round-trip verify (file: only). */
int ib_hid_file_rewind_for_read(IbHidTransport *t);

/*
 * S8.2 Soft path: press a fixture key / emit mouse sample, write boot HID
 * report to transport, read back, verify. CI uses file:; live refuses until
 * host USB exists.
 */
int ib_hid_keyboard_path_selftest(const IbKeyboardIface *k, IbHidTransport *t,
                                  IbHidTestResult *res);

int ib_hid_mouse_path_selftest(const IbMouseIface *m, IbHidTransport *t,
                               IbHidTestResult *res);

#endif
