#define _POSIX_C_SOURCE 200809L
#include "ib_hid.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *ib_hid_kind_name(IbHidReportKind k) {
    switch (k) {
    case IB_HID_KIND_BOOT_KEYBOARD:
        return "boot_keyboard";
    case IB_HID_KIND_BOOT_MOUSE:
        return "boot_mouse";
    default:
        return "unknown";
    }
}

static int starts_with(const char *s, const char *p) {
    return strncmp(s, p, strlen(p)) == 0;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex_bytes(const char *hex, unsigned char *out, int cap, int *nout) {
    *nout = 0;
    while (*hex) {
        while (*hex == ' ' || *hex == ':') hex++;
        if (!*hex) break;
        int hi = hex_nibble(*hex++);
        if (hi < 0 || !*hex) return -1;
        int lo = hex_nibble(*hex++);
        if (lo < 0) return -1;
        if (*nout >= cap) return -1;
        out[(*nout)++] = (unsigned char)((hi << 4) | lo);
    }
    return 0;
}

static int format_hex(const unsigned char *data, int len, char *out, int out_cap) {
    int used = 0, i;
    for (i = 0; i < len; i++) {
        int w = snprintf(out + used, out_cap - used, "%02x", data[i]);
        if (w < 0 || used + w >= out_cap) return -1;
        used += w;
    }
    return used;
}

int ib_hid_encode_hello(char *out, int out_cap) {
    if (!out || out_cap < 8) return -1;
    return snprintf(out, out_cap, "HELLO SoftISMBench/1.0");
}

int ib_hid_encode_hello_ack(char *out, int out_cap, const char *board_id) {
    if (!out || out_cap < 16 || !board_id) return -1;
    return snprintf(out, out_cap, "HELLO_ACK %s protocol=%s", board_id, IB_HID_PROTO);
}

int ib_hid_encode_report(char *out, int out_cap, const IbHidReport *r) {
    if (!out || out_cap < 24 || !r) return -1;
    char hex[32];
    if (format_hex(r->data, r->len, hex, sizeof hex) < 0) return -1;
    return snprintf(out, out_cap, "REPORT kind=%s hex=%s", ib_hid_kind_name(r->kind), hex);
}

int ib_hid_encode_bye(char *out, int out_cap) {
    if (!out || out_cap < 4) return -1;
    return snprintf(out, out_cap, "BYE");
}

int ib_hid_parse_line(const char *line, IbHidReport *rep_out, int *is_hello, int *is_ack,
                      int *is_bye, char *board_id, int board_cap) {
    if (is_hello) *is_hello = 0;
    if (is_ack) *is_ack = 0;
    if (is_bye) *is_bye = 0;
    if (!line) return -1;
    while (*line == ' ' || *line == '\t') line++;
    if (!*line || *line == '#') return 0;
    if (starts_with(line, "HELLO_ACK")) {
        if (is_ack) *is_ack = 1;
        if (board_id && board_cap > 0) {
            char id[64], proto[16];
            if (sscanf(line, "HELLO_ACK %63s protocol=%15s", id, proto) >= 1)
                snprintf(board_id, board_cap, "%s", id);
        }
        return 0;
    }
    if (starts_with(line, "HELLO")) {
        if (is_hello) *is_hello = 1;
        return 0;
    }
    if (starts_with(line, "BYE")) {
        if (is_bye) *is_bye = 1;
        return 0;
    }
    if (starts_with(line, "REPORT")) {
        if (!rep_out) return -1;
        memset(rep_out, 0, sizeof *rep_out);
        char kind[32], hex[64];
        if (sscanf(line, "REPORT kind=%31s hex=%63s", kind, hex) != 2) return -1;
        if (strcmp(kind, "boot_keyboard") == 0) {
            rep_out->kind = IB_HID_KIND_BOOT_KEYBOARD;
            rep_out->len = 8;
        } else if (strcmp(kind, "boot_mouse") == 0) {
            rep_out->kind = IB_HID_KIND_BOOT_MOUSE;
            rep_out->len = 4;
        } else
            return -1;
        int n = 0;
        if (parse_hex_bytes(hex, rep_out->data, 8, &n) != 0 || n != rep_out->len) return -1;
        return 0;
    }
    return -1;
}

int ib_hid_session_from_text(IbHidSession *sess, const char *text, char *err, int err_cap) {
    if (!sess || !text) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(sess, 0, sizeof *sess);
    snprintf(sess->protocol, sizeof sess->protocol, "%s", IB_HID_PROTO);
    snprintf(sess->board_id, sizeof sess->board_id, "ISM-HID-MOCK");

    char *buf = strdup(text);
    if (!buf) {
        if (err && err_cap) snprintf(err, err_cap, "oom");
        return -1;
    }
    char *save = NULL;
    char *line = strtok_r(buf, "\n", &save);
    int saw_hdr = 0, saw_ack = 0, saw_bye = 0, line_no = 0;
    while (line) {
        line_no++;
        char *t = line;
        while (*t == ' ' || *t == '\t') t++;
        size_t L = strlen(t);
        while (L > 0 && (t[L - 1] == '\r' || t[L - 1] == ' ')) t[--L] = 0;
        if (!*t || *t == '#') {
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        if (!saw_hdr) {
            if (!starts_with(t, "SQHID1")) {
                if (err && err_cap)
                    snprintf(err, err_cap, "line %d: need SQHID1 Soft mock header", line_no);
                free(buf);
                return -1;
            }
            saw_hdr = 1;
            line = strtok_r(NULL, "\n", &save);
            continue;
        }
        IbHidReport rep;
        int is_hello = 0, is_ack = 0, is_bye = 0;
        char board[64];
        board[0] = 0;
        if (ib_hid_parse_line(t, &rep, &is_hello, &is_ack, &is_bye, board, sizeof board) !=
            0) {
            if (err && err_cap) snprintf(err, err_cap, "line %d: bad frame", line_no);
            free(buf);
            return -1;
        }
        if (is_ack) {
            saw_ack = 1;
            if (board[0]) snprintf(sess->board_id, sizeof sess->board_id, "%s", board);
        } else if (is_bye) {
            saw_bye = 1;
        } else if (is_hello) {
            /* host hello */
        } else if (rep.len > 0) {
            if (sess->nreports >= IB_HID_MAX_REPORTS) {
                if (err && err_cap) snprintf(err, err_cap, "too many reports");
                free(buf);
                return -1;
            }
            sess->reports[sess->nreports++] = rep;
        }
        line = strtok_r(NULL, "\n", &save);
    }
    free(buf);
    if (!saw_hdr || !saw_ack || !saw_bye || sess->nreports < 2) {
        if (err && err_cap)
            snprintf(err, err_cap, "incomplete SQHID1 session (ack=%d bye=%d n=%d)", saw_ack,
                     saw_bye, sess->nreports);
        return -1;
    }
    sess->connected = 1;
    snprintf(sess->detail, sizeof sess->detail, "fixture session board=%s reports=%d",
             sess->board_id, sess->nreports);
    return 0;
}

int ib_hid_session_from_mock(IbHidSession *sess, const IbKeyboardIface *kbd,
                             const IbMouseIface *mouse, char *err, int err_cap) {
    if (!sess || !kbd || !mouse) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    memset(sess, 0, sizeof *sess);
    snprintf(sess->protocol, sizeof sess->protocol, "%s", IB_HID_PROTO);
    snprintf(sess->board_id, sizeof sess->board_id, "ISM-HID-MOCK");

    IbKeyMatrix mx;
    ib_keyboard_matrix_clear(&mx);
    /* Prefer key "A" if mapped; else first key. */
    const char *key = "A";
    int ki;
    for (ki = 0; ki < kbd->nkeys; ki++)
        if (strcmp(kbd->key_name[ki], "A") == 0) break;
    if (ki >= kbd->nkeys) key = kbd->key_name[0];

    if (ib_keyboard_press(&mx, kbd, key) != 0) {
        if (err && err_cap) snprintf(err, err_cap, "cannot press %s", key);
        return -1;
    }
    {
        IbHidReport *r = &sess->reports[sess->nreports++];
        r->kind = IB_HID_KIND_BOOT_KEYBOARD;
        r->len = 8;
        ib_keyboard_boot_report(kbd, &mx, r->data);
    }
    ib_keyboard_release(&mx, kbd, key);
    {
        IbHidReport *r = &sess->reports[sess->nreports++];
        r->kind = IB_HID_KIND_BOOT_KEYBOARD;
        r->len = 8;
        ib_keyboard_boot_report(kbd, &mx, r->data);
    }

    IbMouseSample s;
    memset(&s, 0, sizeof s);
    ib_mouse_sensor_step(mouse, 10, -5, &s);
    s.buttons = 1;
    {
        IbHidReport *r = &sess->reports[sess->nreports++];
        r->kind = IB_HID_KIND_BOOT_MOUSE;
        r->len = 4;
        ib_mouse_boot_report(&s, r->data);
    }
    memset(&s, 0, sizeof s);
    {
        IbHidReport *r = &sess->reports[sess->nreports++];
        r->kind = IB_HID_KIND_BOOT_MOUSE;
        r->len = 4;
        ib_mouse_boot_report(&s, r->data);
    }

    sess->connected = 1;
    snprintf(sess->detail, sizeof sess->detail,
             "Soft mock HID session board=%s reports=%d (kbd+mouse boot)", sess->board_id,
             sess->nreports);
    return 0;
}

static int looks_like_live_hid(const char *ep) {
    if (!ep) return 0;
    if (starts_with(ep, "/dev/hidraw")) return 1;
    if (starts_with(ep, "/dev/usb")) return 1;
    if (starts_with(ep, "hidraw:")) return 1;
    if (starts_with(ep, "usbhid:")) return 1;
    return 0;
}

static int read_whole(const char *path, char **out, char *err, int err_cap) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err && err_cap) snprintf(err, err_cap, "cannot open %s", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        if (err && err_cap) snprintf(err, err_cap, "seek %s", path);
        return -1;
    }
    long n = ftell(f);
    if (n < 0 || n > 1 << 20) {
        fclose(f);
        if (err && err_cap) snprintf(err, err_cap, "bad size %s", path);
        return -1;
    }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        if (err && err_cap) snprintf(err, err_cap, "oom");
        return -1;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        if (err && err_cap) snprintf(err, err_cap, "read %s", path);
        return -1;
    }
    buf[n] = 0;
    fclose(f);
    *out = buf;
    return 0;
}

static int load_iface_pair(IbKeyboardIface *kbd, IbMouseIface *mouse, char *err,
                           int err_cap) {
    char *kt = NULL, *mt = NULL;
    if (read_whole("fixtures/keyboard-60/interface.ismhid", &kt, err, err_cap) != 0)
        return -1;
    if (ib_keyboard_parse(kbd, kt, err, err_cap) != 0) {
        free(kt);
        return -1;
    }
    free(kt);
    if (read_whole("fixtures/mouse/interface.ismhid", &mt, err, err_cap) != 0) return -1;
    if (ib_mouse_parse(mouse, mt, err, err_cap) != 0) {
        free(mt);
        return -1;
    }
    free(mt);
    return 0;
}

int ib_hid_connect(const char *endpoint, IbHidSession *sess, char *err, int err_cap) {
    if (!endpoint || !sess) {
        if (err && err_cap) snprintf(err, err_cap, "null args");
        return -1;
    }
    if (looks_like_live_hid(endpoint)) {
        if (err && err_cap)
            snprintf(err, err_cap,
                     "live USB HID (%s) deferred until Eric freezes Soft/host path; use "
                     "`hid-test mock` or fixtures/hid/*.sqhid (S8.2 Soft path)",
                     endpoint);
        return -1;
    }
    if (strcmp(endpoint, "mock") == 0 || starts_with(endpoint, "mock:")) {
        IbKeyboardIface kbd;
        IbMouseIface mouse;
        if (load_iface_pair(&kbd, &mouse, err, err_cap) != 0) return -1;
        return ib_hid_session_from_mock(sess, &kbd, &mouse, err, err_cap);
    }
    char *text = NULL;
    if (read_whole(endpoint, &text, err, err_cap) != 0) return -1;
    int rc = ib_hid_session_from_text(sess, text, err, err_cap);
    free(text);
    return rc;
}

int ib_hid_selftest(const IbHidSession *sess, IbHidTestResult *res) {
    memset(res, 0, sizeof *res);
    res->ok = 1;
    if (!sess || !sess->connected || sess->nreports < 2) {
        res->nfail++;
        res->ok = 0;
        snprintf(res->summary, sizeof res->summary, "hid selftest: empty session → FAIL");
        return 0;
    }
    int i;
    int saw_kbd = 0, saw_mouse = 0;
    for (i = 0; i < sess->nreports; i++) {
        const IbHidReport *r = &sess->reports[i];
        char line[IB_HID_LINE];
        IbHidReport back;
        int is_hello = 0, is_ack = 0, is_bye = 0;
        if (ib_hid_encode_report(line, sizeof line, r) < 0) {
            res->nfail++;
            res->ok = 0;
            continue;
        }
        if (ib_hid_parse_line(line, &back, &is_hello, &is_ack, &is_bye, NULL, 0) != 0 ||
            back.kind != r->kind || back.len != r->len ||
            memcmp(back.data, r->data, (size_t)r->len) != 0) {
            res->nfail++;
            res->ok = 0;
            continue;
        }
        res->npass++;
        if (r->kind == IB_HID_KIND_BOOT_KEYBOARD) {
            saw_kbd = 1;
            /* Soft mock first kbd report should carry at least one keycode. */
            if (i == 0) {
                int nonzero = 0, j;
                for (j = 2; j < 8; j++)
                    if (r->data[j]) nonzero = 1;
                if (!nonzero) {
                    res->nfail++;
                    res->ok = 0;
                } else
                    res->npass++;
            }
        } else if (r->kind == IB_HID_KIND_BOOT_MOUSE) {
            saw_mouse = 1;
        }
    }
    if (saw_kbd && saw_mouse)
        res->npass++;
    else {
        res->nfail++;
        res->ok = 0;
    }

    /* Framing helpers smoke. */
    {
        char line[IB_HID_LINE];
        if (ib_hid_encode_hello(line, sizeof line) < 0 ||
            ib_hid_encode_hello_ack(line, sizeof line, sess->board_id) < 0 ||
            ib_hid_encode_bye(line, sizeof line) < 0) {
            res->nfail++;
            res->ok = 0;
        } else
            res->npass++;
    }

    snprintf(res->summary, sizeof res->summary,
             "hid selftest board=%s protocol=%s reports=%d pass=%d fail=%d → %s",
             sess->board_id, sess->protocol, sess->nreports, res->npass, res->nfail,
             res->ok ? "PASS" : "FAIL");
    return 0;
}
