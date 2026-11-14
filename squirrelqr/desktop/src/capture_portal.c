/* qrcapture via xdg-desktop-portal Screenshot over D-Bus (Wayland / GNOME).
 * Original code against the org.freedesktop.portal.Screenshot interface. */
#include "qrcapture_priv.h"

#include <dbus/dbus.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char stash_path[512];
static int stash_valid;

static int fail(char *err, size_t n, const char *fmt, ...) {
    if (err && n) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, n, fmt, ap);
        va_end(ap);
    }
    return -1;
}

const char *qr_capture_backend_portal(void) { return "portal"; }

static void clear_stash(void) {
    if (stash_valid && stash_path[0]) unlink(stash_path);
    stash_path[0] = 0;
    stash_valid = 0;
}

static int uri_to_path(const char *uri, char *out, size_t n) {
    if (!uri || !out || n < 2) return -1;
    if (!strncmp(uri, "file://", 7)) {
        const char *p = uri + 7;
        /* skip empty authority (file:///path) */
        if (p[0] == '/' && p[1] == '/') {
            p += 2;
            while (*p && *p != '/') p++;
        }
        if (!*p) return -1;
        /* percent-decode lightly for spaces */
        size_t j = 0;
        for (size_t i = 0; p[i] && j + 1 < n; i++) {
            if (p[i] == '%' && p[i + 1] && p[i + 2]) {
                unsigned v = 0;
                if (sscanf(p + i + 1, "%2x", &v) == 1) {
                    out[j++] = (char)v;
                    i += 2;
                    continue;
                }
            }
            out[j++] = p[i];
        }
        out[j] = 0;
        return 0;
    }
    if (uri[0] == '/') {
        snprintf(out, n, "%s", uri);
        return 0;
    }
    return -1;
}

/* Block until the portal Request fires Response, or timeout_ms elapses. */
static int wait_request(DBusConnection *conn, const char *request_path, int *response,
                        char *uri, size_t urilen, char *err, size_t errlen, int timeout_ms) {
    *response = -1;
    if (uri && urilen) uri[0] = 0;
    char match[256];
    snprintf(match, sizeof match,
             "type='signal',interface='org.freedesktop.portal.Request',"
             "member='Response',path='%s'",
             request_path);
    dbus_bus_add_match(conn, match, NULL);
    dbus_connection_flush(conn);

    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        dbus_connection_read_write(conn, 200);
        DBusMessage *msg;
        while ((msg = dbus_connection_pop_message(conn)) != NULL) {
            if (dbus_message_is_signal(msg, "org.freedesktop.portal.Request", "Response") &&
                dbus_message_has_path(msg, request_path)) {
                DBusMessageIter it, dict, entry, var;
                unsigned u = 0;
                if (!dbus_message_iter_init(msg, &it) ||
                    dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_UINT32) {
                    dbus_message_unref(msg);
                    return fail(err, errlen, "portal Response had a bad shape");
                }
                dbus_message_iter_get_basic(&it, &u);
                *response = (int)u;
                dbus_message_iter_next(&it);
                if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) {
                    dbus_message_iter_recurse(&it, &dict);
                    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
                        dbus_message_iter_recurse(&dict, &entry);
                        const char *key = NULL;
                        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
                            dbus_message_iter_get_basic(&entry, &key);
                            dbus_message_iter_next(&entry);
                            if (key && !strcmp(key, "uri") &&
                                dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
                                dbus_message_iter_recurse(&entry, &var);
                                if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
                                    const char *uuri = NULL;
                                    dbus_message_iter_get_basic(&var, &uuri);
                                    if (uuri && uri && urilen) snprintf(uri, urilen, "%s", uuri);
                                }
                            }
                        }
                        dbus_message_iter_next(&dict);
                    }
                }
                dbus_message_unref(msg);
                dbus_bus_remove_match(conn, match, NULL);
                return 0;
            }
            dbus_message_unref(msg);
        }
        clock_gettime(CLOCK_MONOTONIC, &t);
        int elapsed =
            (int)((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000);
        if (elapsed >= timeout_ms) {
            dbus_bus_remove_match(conn, match, NULL);
            return fail(err, errlen, "portal Screenshot timed out");
        }
    }
}

static int portal_screenshot(int interactive, char *path, size_t pathlen, char *err, size_t errlen) {
    path[0] = 0;
    DBusError derr;
    dbus_error_init(&derr);
    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SESSION, &derr);
    if (!conn) {
        const char *m = dbus_error_is_set(&derr) ? derr.message : "no session bus";
        fail(err, errlen, "D-Bus session: %s", m);
        dbus_error_free(&derr);
        return -1;
    }

    char token[64];
    snprintf(token, sizeof token, "squirrelqr_%d_%ld", (int)getpid(), (long)time(NULL));
    char request_path[160];
    const char *unique = dbus_bus_get_unique_name(conn);
    /* Request path: /org/freedesktop/portal/desktop/request/SENDER/TOKEN
     * SENDER is the unique name with '.' -> '_'. */
    char sender[96];
    size_t sj = 0;
    if (unique && unique[0] == ':') unique++;
    for (size_t i = 0; unique && unique[i] && sj + 1 < sizeof sender; i++)
        sender[sj++] = unique[i] == '.' ? '_' : unique[i];
    sender[sj] = 0;
    snprintf(request_path, sizeof request_path, "/org/freedesktop/portal/desktop/request/%s/%s",
             sender, token);

    DBusMessage *msg = dbus_message_new_method_call(
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Screenshot", "Screenshot");
    if (!msg) {
        dbus_connection_unref(conn);
        return fail(err, errlen, "out of memory");
    }

    DBusMessageIter args, opts, entry, var;
    dbus_message_iter_init_append(msg, &args);
    const char *parent = "";
    dbus_bool_t inter = interactive ? TRUE : FALSE;
    const char *tok_key = "handle_token";
    const char *inter_key = "interactive";
    const char *modal_key = "modal";
    dbus_bool_t modal = TRUE;
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &parent) ||
        !dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &opts) ||
        !dbus_message_iter_open_container(&opts, DBUS_TYPE_DICT_ENTRY, NULL, &entry) ||
        !dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &tok_key) ||
        !dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var) ||
        !dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &token) ||
        !dbus_message_iter_close_container(&entry, &var) ||
        !dbus_message_iter_close_container(&opts, &entry) ||
        !dbus_message_iter_open_container(&opts, DBUS_TYPE_DICT_ENTRY, NULL, &entry) ||
        !dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &inter_key) ||
        !dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var) ||
        !dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &inter) ||
        !dbus_message_iter_close_container(&entry, &var) ||
        !dbus_message_iter_close_container(&opts, &entry) ||
        !dbus_message_iter_open_container(&opts, DBUS_TYPE_DICT_ENTRY, NULL, &entry) ||
        !dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &modal_key) ||
        !dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var) ||
        !dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &modal) ||
        !dbus_message_iter_close_container(&entry, &var) ||
        !dbus_message_iter_close_container(&opts, &entry) ||
        !dbus_message_iter_close_container(&args, &opts)) {
        dbus_message_unref(msg);
        dbus_connection_unref(conn);
        return fail(err, errlen, "failed to build Screenshot call");
    }

    /* Subscribe to the request path before the call returns. */
    char match[256];
    snprintf(match, sizeof match,
             "type='signal',interface='org.freedesktop.portal.Request',"
             "member='Response',path='%s'",
             request_path);
    dbus_bus_add_match(conn, match, NULL);
    dbus_connection_flush(conn);

    DBusMessage *reply =
        dbus_connection_send_with_reply_and_block(conn, msg, 15000, &derr);
    dbus_message_unref(msg);
    if (!reply) {
        const char *m = dbus_error_is_set(&derr) ? derr.message : "Screenshot call failed";
        fail(err, errlen, "portal Screenshot: %s", m);
        dbus_error_free(&derr);
        dbus_bus_remove_match(conn, match, NULL);
        dbus_connection_unref(conn);
        return -1;
    }

    /* Prefer the path the portal returned (some implementations mint their own token). */
    {
        DBusMessageIter it;
        const char *returned = NULL;
        if (dbus_message_iter_init(reply, &it) &&
            dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_OBJECT_PATH) {
            dbus_message_iter_get_basic(&it, &returned);
            if (returned && returned[0] && strcmp(returned, request_path) != 0) {
                dbus_bus_remove_match(conn, match, NULL);
                snprintf(request_path, sizeof request_path, "%s", returned);
                snprintf(match, sizeof match,
                         "type='signal',interface='org.freedesktop.portal.Request',"
                         "member='Response',path='%s'",
                         request_path);
                dbus_bus_add_match(conn, match, NULL);
                dbus_connection_flush(conn);
            }
        }
    }
    dbus_message_unref(reply);

    int response = -1;
    char uri[512];
    int timeout = interactive ? 120000 : 30000;
    /* wait_request adds its own match; remove the pre-call one first to avoid dup logic */
    dbus_bus_remove_match(conn, match, NULL);
    if (wait_request(conn, request_path, &response, uri, sizeof uri, err, errlen, timeout) != 0) {
        dbus_connection_unref(conn);
        return -1;
    }
    dbus_connection_unref(conn);

    if (response == 1) return fail(err, errlen, "screenshot cancelled");
    if (response != 0) return fail(err, errlen, "portal Screenshot failed (code %d)", response);
    if (!uri[0]) return fail(err, errlen, "portal Screenshot returned no uri");
    if (uri_to_path(uri, path, pathlen) != 0)
        return fail(err, errlen, "cannot parse screenshot uri: %s", uri);
    return 0;
}

static int load_portal_file(const char *path, QrRect r, QrImage *out, char *err, size_t n) {
    QrImage full;
    if (qr_image_load(path, &full, err, n) != 0) return -1;
    return qr_capture_crop(&full, r, out, err, n);
}

int qr_capture_portal(QrRect r, QrImage *out, char *err, size_t n) {
    if (qr_capture_try_synth(r, out, err, n)) return out->gray ? 0 : -1;
    out->w = out->h = 0;
    out->gray = NULL;
    if (stash_valid && stash_path[0]) {
        int rc = load_portal_file(stash_path, r, out, err, n);
        clear_stash();
        return rc;
    }
    char path[512];
    if (portal_screenshot(0, path, sizeof path, err, n) != 0) return -1;
    int rc = load_portal_file(path, r, out, err, n);
    unlink(path);
    return rc;
}

int qr_select_rect_portal(QrRect *r, char *err, size_t n) {
    {
        int syn = qr_select_try_synth(r, err, n);
        if (syn <= 0) return syn;
    }
    clear_stash();
    char path[512];
    if (portal_screenshot(1, path, sizeof path, err, n) != 0) {
        if (err && n && strstr(err, "cancelled")) return 1;
        return -1;
    }
    QrImage full;
    if (qr_image_load(path, &full, err, n) != 0) {
        unlink(path);
        return -1;
    }
    r->x = 0;
    r->y = 0;
    r->w = full.w;
    r->h = full.h;
    qr_image_free(&full);
    if (r->w < 8 || r->h < 8) {
        unlink(path);
        return fail(err, n, "the selection is too small");
    }
    snprintf(stash_path, sizeof stash_path, "%s", path);
    stash_valid = 1;
    return 0;
}

#if !defined(QR_CAPTURE_INTERNAL)
const char *qr_capture_backend(void) { return qr_capture_backend_portal(); }
int qr_capture(QrRect r, QrImage *out, char *err, size_t n) { return qr_capture_portal(r, out, err, n); }
int qr_select_rect(QrRect *r, char *err, size_t n) { return qr_select_rect_portal(r, err, n); }
#endif
