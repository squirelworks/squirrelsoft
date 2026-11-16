#define _POSIX_C_SOURCE 200809L
#include "ib_hid.h"
#include "ib_keyboard.h"
#include "ib_mouse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *kind_name(IbHidDeviceKind k)
{
    return k == IB_HID_DEV_MOUSE ? "mouse" : "keyboard";
}

static int report_len_for(IbHidDeviceKind k)
{
    return k == IB_HID_DEV_MOUSE ? 4 : 8;
}

static int write_header(IbHidTransport *t)
{
    if (!t || !t->fp)
        return -1;
    rewind(t->fp);
    if (fprintf(t->fp,
                "ISM-HID-CAP 1\n"
                "device: %s\n"
                "report_len: %d\n"
                "# Soft capture; one hex report per line (boot protocol)\n",
                kind_name(t->kind), t->report_len) < 0)
        return -1;
    return 0;
}

int ib_hid_open(IbHidTransport *t, const char *spec, IbHidDeviceKind kind, char *err,
                int errlen)
{
    if (!t || !spec) {
        if (err && errlen)
            snprintf(err, (size_t)errlen, "null args");
        return -1;
    }
    memset(t, 0, sizeof *t);
    t->kind = kind;
    t->report_len = report_len_for(kind);

    if (strncmp(spec, "file:", 5) == 0) {
        const char *path = spec + 5;
        if (!*path) {
            if (err && errlen)
                snprintf(err, (size_t)errlen, "file: needs path");
            return -1;
        }
        t->backend = IB_HID_BACKEND_FILE;
        snprintf(t->path, sizeof t->path, "%s", path);
        t->fp = fopen(path, "w+b");
        if (!t->fp) {
            if (err && errlen)
                snprintf(err, (size_t)errlen, "open %s", path);
            return -1;
        }
        if (write_header(t) != 0) {
            fclose(t->fp);
            t->fp = NULL;
            if (err && errlen)
                snprintf(err, (size_t)errlen, "write header %s", path);
            return -1;
        }
        return 0;
    }
    if (strcmp(spec, "live") == 0) {
        t->backend = IB_HID_BACKEND_LIVE;
        t->live_refused = 1;
        snprintf(t->err, sizeof t->err,
                 "live USB HID host path not available in Soft yet "
                 "(S8.2 Soft transport is file: until Soft host USB lands)");
        if (err && errlen)
            snprintf(err, (size_t)errlen, "%s", t->err);
        return 0;
    }
    if (err && errlen)
        snprintf(err, (size_t)errlen, "unknown transport (want file:PATH or live)");
    return -1;
}

void ib_hid_close(IbHidTransport *t)
{
    if (!t)
        return;
    if (t->fp) {
        fclose(t->fp);
        t->fp = NULL;
    }
}

int ib_hid_file_rewind_for_read(IbHidTransport *t)
{
    if (!t || t->backend != IB_HID_BACKEND_FILE || !t->fp)
        return -1;
    if (fflush(t->fp) != 0)
        return -1;
    rewind(t->fp);
    return 0;
}

int ib_hid_write_report(IbHidTransport *t, const unsigned char *rep, int n)
{
    int i;
    if (!t || !rep || n <= 0 || n > IB_HID_REPORT_MAX)
        return -1;
    if (t->backend == IB_HID_BACKEND_LIVE) {
        snprintf(t->err, sizeof t->err, "live USB HID write refused");
        return -1;
    }
    if (!t->fp || n != t->report_len)
        return -1;
    for (i = 0; i < n; i++) {
        if (fprintf(t->fp, "%s%02x", i ? " " : "", rep[i]) < 0)
            return -1;
    }
    if (fprintf(t->fp, "\n") < 0)
        return -1;
    return 0;
}

int ib_hid_read_report(IbHidTransport *t, unsigned char *rep, int nmax, int *nout)
{
    char line[512];
    char *p = NULL;
    int n = 0;
    int got = 0;

    if (!t || !rep || nmax < 1 || !nout)
        return -1;
    *nout = 0;
    if (t->backend == IB_HID_BACKEND_LIVE) {
        snprintf(t->err, sizeof t->err, "live USB HID read refused");
        return -1;
    }
    if (!t->fp)
        return -1;
    while (fgets(line, (int)sizeof(line), t->fp)) {
        p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p || *p == '\n' || *p == '#')
            continue;
        if (strncmp(p, "device:", 6) == 0 || strncmp(p, "report_len:", 11) == 0 ||
            strncmp(p, "ISM-HID-CAP", 11) == 0)
            continue;
        got = 1;
        break;
    }
    if (!got || !p || !*p)
        return -1;
    while (*p && n < nmax) {
        char *end = NULL;
        unsigned long v;
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p || *p == '\n' || *p == '\r')
            break;
        v = strtoul(p, &end, 16);
        if (end == p)
            return -1;
        rep[n++] = (unsigned char)(v & 0xffu);
        p = end;
    }
    if (n != t->report_len)
        return -1;
    *nout = n;
    return 0;
}

int ib_hid_keyboard_path_selftest(const IbKeyboardIface *k, IbHidTransport *t,
                                  IbHidTestResult *res)
{
    IbKeyMatrix mx;
    unsigned char wr[8], rd[8];
    int nout = 0;
    const char *probe = "A";

    memset(res, 0, sizeof *res);
    res->ok = 1;
    if (!k || !t) {
        res->ok = 0;
        res->nfail++;
        snprintf(res->summary, sizeof res->summary, "keyboard-hid null → FAIL");
        return 0;
    }
    if (t->live_refused) {
        snprintf(res->summary, sizeof res->summary,
                 "keyboard-hid live refused (Soft file: path covers CI) → STANDDOWN");
        res->ok = 1;
        return 0;
    }
    if (strcmp(k->hid_report, "boot_keyboard") != 0) {
        res->ok = 0;
        res->nfail++;
        snprintf(res->summary, sizeof res->summary,
                 "keyboard-hid need boot_keyboard iface → FAIL");
        return 0;
    }
    ib_keyboard_matrix_clear(&mx);
    if (ib_keyboard_press(&mx, k, probe) != 0) {
        /* fallback: first mapped key */
        if (k->nkeys < 1 || ib_keyboard_press(&mx, k, k->key_name[0]) != 0) {
            res->ok = 0;
            res->nfail++;
            snprintf(res->summary, sizeof res->summary, "keyboard-hid no probe key → FAIL");
            return 0;
        }
        probe = k->key_name[0];
    }
    if (ib_keyboard_boot_report(k, &mx, wr) < 1) {
        res->ok = 0;
        res->nfail++;
        snprintf(res->summary, sizeof res->summary,
                 "keyboard-hid empty boot report for %s → FAIL", probe);
        return 0;
    }
    if (ib_hid_write_report(t, wr, 8) != 0 || ib_hid_file_rewind_for_read(t) != 0 ||
        ib_hid_read_report(t, rd, 8, &nout) != 0 || nout != 8 ||
        memcmp(wr, rd, 8) != 0) {
        res->ok = 0;
        res->nfail++;
        snprintf(res->summary, sizeof res->summary,
                 "keyboard-hid file: round-trip FAIL key=%s", probe);
        return 0;
    }
    snprintf(res->summary, sizeof res->summary,
             "keyboard-hid path layout=%s key=%s report=boot_keyboard "
             "transport=file → PASS",
             k->layout, probe);
    return 0;
}

int ib_hid_mouse_path_selftest(const IbMouseIface *m, IbHidTransport *t,
                               IbHidTestResult *res)
{
    IbMouseSample s;
    unsigned char wr[4], rd[4];
    int nout = 0;

    memset(res, 0, sizeof *res);
    res->ok = 1;
    if (!m || !t) {
        res->ok = 0;
        res->nfail++;
        snprintf(res->summary, sizeof res->summary, "mouse-hid null → FAIL");
        return 0;
    }
    if (t->live_refused) {
        snprintf(res->summary, sizeof res->summary,
                 "mouse-hid live refused (Soft file: path covers CI) → STANDDOWN");
        res->ok = 1;
        return 0;
    }
    if (strcmp(m->hid_report, "boot_mouse") != 0) {
        res->ok = 0;
        res->nfail++;
        snprintf(res->summary, sizeof res->summary, "mouse-hid need boot_mouse iface → FAIL");
        return 0;
    }
    memset(&s, 0, sizeof s);
    ib_mouse_sensor_step(m, 12, -7, &s);
    s.buttons = 1;
    ib_mouse_boot_report(&s, wr);
    if (ib_hid_write_report(t, wr, 4) != 0 || ib_hid_file_rewind_for_read(t) != 0 ||
        ib_hid_read_report(t, rd, 4, &nout) != 0 || nout != 4 || memcmp(wr, rd, 4) != 0) {
        res->ok = 0;
        res->nfail++;
        snprintf(res->summary, sizeof res->summary, "mouse-hid file: round-trip FAIL");
        return 0;
    }
    snprintf(res->summary, sizeof res->summary,
             "mouse-hid path sensor=%s report=boot_mouse transport=file → PASS",
             m->sensor);
    return 0;
}
