/*
 * Soft S8.2 — Soft mock USB HID path for ISM 60% keyboard / mouse.
 *
 * Soft-owned SQHID1 line framing around boot-protocol reports (8-byte
 * keyboard, 4-byte mouse). CI uses mock:/fixture endpoints; live hidraw
 * stays refused until Eric freezes a Soft/host path. Soft does not design
 * hardware.
 */
#ifndef IB_HID_H
#define IB_HID_H

#include "ib_iface.h"
#include "ib_keyboard.h"
#include "ib_mouse.h"

#define IB_HID_PROTO "SQHID1"
#define IB_HID_MAX_REPORTS 64
#define IB_HID_LINE 160

typedef enum {
    IB_HID_KIND_BOOT_KEYBOARD = 0,
    IB_HID_KIND_BOOT_MOUSE = 1
} IbHidReportKind;

typedef struct {
    IbHidReportKind kind;
    unsigned char data[8];
    int len; /* 8 keyboard / 4 mouse */
} IbHidReport;

typedef struct {
    char board_id[IB_NAME_LEN];
    char protocol[16];
    IbHidReport reports[IB_HID_MAX_REPORTS];
    int nreports;
    int connected;
    char detail[IB_MSG_LEN];
} IbHidSession;

typedef struct {
    int ok;
    int npass;
    int nfail;
    char summary[IB_MSG_LEN];
} IbHidTestResult;

const char *ib_hid_kind_name(IbHidReportKind k);

int ib_hid_encode_hello(char *out, int out_cap);
int ib_hid_encode_hello_ack(char *out, int out_cap, const char *board_id);
int ib_hid_encode_report(char *out, int out_cap, const IbHidReport *r);
int ib_hid_encode_bye(char *out, int out_cap);

/* Parse one SQHID1 body line (HELLO/ACK/REPORT/BYE). Returns 0 on ok. */
int ib_hid_parse_line(const char *line, IbHidReport *rep_out, int *is_hello,
                      int *is_ack, int *is_bye, char *board_id, int board_cap);

int ib_hid_session_from_text(IbHidSession *sess, const char *text, char *err,
                             int err_cap);

/*
 * Soft mock: build a short keyboard+mouse report stream from CI fixtures
 * (press A → report → release; mouse move+click).
 */
int ib_hid_session_from_mock(IbHidSession *sess, const IbKeyboardIface *kbd,
                             const IbMouseIface *mouse, char *err, int err_cap);

/*
 * endpoint:
 *   "mock" / "mock:"* → Soft mock from keyboard+mouse iface fixtures
 *   path to .sqhid    → load Soft transcript
 *   /dev/hidraw* etc. → refuse (live USB deferred)
 */
int ib_hid_connect(const char *endpoint, IbHidSession *sess, char *err, int err_cap);

/* Verify session reports round-trip encode/parse and Soft mock expectations. */
int ib_hid_selftest(const IbHidSession *sess, IbHidTestResult *res);

#endif
