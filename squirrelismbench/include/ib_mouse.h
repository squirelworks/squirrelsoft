#ifndef IB_MOUSE_H
#define IB_MOUSE_H

#include "ib_iface.h"

typedef struct {
    int dx, dy; /* counts this frame */
    int buttons; /* bit0 L, bit1 R, bit2 M */
    int wheel; /* vertical notches */
} IbMouseSample;

typedef struct {
    int ok;
    int nfail;
    char summary[IB_MSG_LEN];
} IbMouseTestResult;

/* Synthetic sensor: apply CPI scaling stub and clamp. */
void ib_mouse_sensor_step(const IbMouseIface *m, int raw_dx, int raw_dy, IbMouseSample *out);

int ib_mouse_scroll_step(const IbMouseIface *m, int notches, IbMouseSample *out);

/* Self-test: button bits, scroll, and non-zero motion path. */
int ib_mouse_selftest(const IbMouseIface *m, IbMouseTestResult *res);

/* Boot mouse report: buttons, dx, dy, wheel (4 bytes). */
void ib_mouse_boot_report(const IbMouseSample *s, unsigned char out[4]);

#endif
