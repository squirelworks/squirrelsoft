#include "ib_mouse.h"

#include <stdio.h>
#include <string.h>

void ib_mouse_sensor_step(const IbMouseIface *m, int raw_dx, int raw_dy, IbMouseSample *out) {
    memset(out, 0, sizeof *out);
    /* Stub: treat raw as counts at cpi_default; clamp to signed 8-bit boot range. */
    int dx = raw_dx;
    int dy = raw_dy;
    (void)m;
    if (dx > 127) dx = 127;
    if (dx < -127) dx = -127;
    if (dy > 127) dy = 127;
    if (dy < -127) dy = -127;
    out->dx = dx;
    out->dy = dy;
}

int ib_mouse_scroll_step(const IbMouseIface *m, int notches, IbMouseSample *out) {
    if (!out) return -1;
    if (strcmp(m->scroll, "vertical") != 0 && strcmp(m->scroll, "vertical_tilt") != 0)
        return -1;
    out->wheel = notches;
    if (out->wheel > 127) out->wheel = 127;
    if (out->wheel < -127) out->wheel = -127;
    return 0;
}

int ib_mouse_selftest(const IbMouseIface *m, IbMouseTestResult *res) {
    memset(res, 0, sizeof *res);
    res->ok = 1;
    IbMouseSample s;
    ib_mouse_sensor_step(m, 10, -5, &s);
    if (s.dx != 10 || s.dy != -5) {
        res->nfail++;
        res->ok = 0;
    }
    ib_mouse_sensor_step(m, 200, 0, &s);
    if (s.dx != 127) {
        res->nfail++;
        res->ok = 0;
    }
    memset(&s, 0, sizeof s);
    if (ib_mouse_scroll_step(m, 3, &s) != 0 || s.wheel != 3) {
        res->nfail++;
        res->ok = 0;
    }
    s.buttons = 0;
    if (m->buttons >= 1) s.buttons |= 1;
    if (m->buttons >= 2) s.buttons |= 2;
    if (m->buttons >= 3) s.buttons |= 4;
    if (s.buttons == 0) {
        res->nfail++;
        res->ok = 0;
    }
    unsigned char rep[4];
    ib_mouse_boot_report(&s, rep);
    if (rep[0] != (unsigned char)s.buttons) {
        res->nfail++;
        res->ok = 0;
    }
    snprintf(res->summary, sizeof res->summary,
             "mouse selftest sensor=%s buttons=%d scroll=%s fail=%d → %s", m->sensor,
             m->buttons, m->scroll, res->nfail, res->ok ? "PASS" : "FAIL");
    return 0;
}

void ib_mouse_boot_report(const IbMouseSample *s, unsigned char out[4]) {
    out[0] = (unsigned char)(s->buttons & 0x07);
    out[1] = (unsigned char)s->dx;
    out[2] = (unsigned char)s->dy;
    out[3] = (unsigned char)s->wheel;
}
