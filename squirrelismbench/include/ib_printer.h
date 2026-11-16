#ifndef IB_PRINTER_H
#define IB_PRINTER_H

#include "ib_iface.h"

typedef struct {
    char driver[32]; /* DRV8833 */
    int vid, pid;
    int ain1, ain2, bin1, bin2, nsleep;
    int head_cs, head_sck, head_mosi;
    int paper_gp;
    int btn_feed, btn_cancel, btn_pwr;
} IbPrinterIface;

typedef struct {
    int ok;
    int npass;
    int nfail;
    char summary[IB_MSG_LEN];
} IbPrinterTestResult;

int ib_printer_parse(IbPrinterIface *p, const char *text, char *err, int err_cap);

/* Synthetic motor / paper / head SPI self-test from Soft fixture (no USB). */
int ib_printer_selftest(const IbPrinterIface *p, IbPrinterTestResult *res);

#endif
