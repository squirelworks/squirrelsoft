/* RC-S380 USB transport over libusb-1.0. Original code. Speaks the Port-100
 * bulk protocol framed in pasori_frame.c: write a command frame, read ACK,
 * then read the data response. Initialises initiator mode for 212 kbps FeliCa
 * (InSetRF + InSetProtocol) before exchanging card frames through InCommRF. */
#include "sqpasori.h"

#include <libusb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <poll.h>
static void sleep_ms(int ms) {
    if (ms > 0) (void)poll(NULL, 0, ms);
}
#endif

#define USB_TIMEOUT_MS 1000
#define FRAME_CAP 512

struct SqsPasori {
    libusb_context *ctx;
    libusb_device_handle *dev;
    uint8_t ep_out, ep_in;
    int iface;
    char name[64];
    uint8_t fw_minor, fw_major;
};

/* Public initiator protocol defaults for FeliCa (key, value) pairs published
 * for the Port-100: initial guard 0x18, add/check CRC on, etc. */
static const uint8_t PROTOCOL_DEFAULTS[] = {
    0x00, 0x18, 0x01, 0x01, 0x02, 0x01, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0x06, 0x00, 0x07, 0x08,
    0x08, 0x00, 0x09, 0x00, 0x0A, 0x00, 0x0B, 0x00, 0x0C, 0x00, 0x0E, 0x04, 0x0F, 0x00, 0x10, 0x00,
    0x11, 0x00, 0x12, 0x00, 0x13, 0x06};
static const uint8_t RF_212F[] = {0x01, 0x01, 0x0F, 0x01};
static const uint8_t PROTOCOL_GUARD[] = {0x00, 0x18};

static int fail(char *err, size_t n, const char *fmt, ...) {
    if (err && n) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, n, fmt, ap);
        va_end(ap);
    }
    return -1;
}

static int is_pasori_pid(uint16_t pid) {
    return pid == SQS_PASORI_PID_S380 || pid == SQS_PASORI_PID_S380_S;
}

static int ascii_ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}

static int reader_matches(const char *name, const char *reader) {
    if (!reader || !*reader) return 1;
    if (strstr(name, reader)) return 1;
    return ascii_ieq(reader, "RC-S380") || ascii_ieq(reader, "PaSoRi") || ascii_ieq(reader, "pasori") ||
           ascii_ieq(reader, "usb");
}

static int find_bulk_eps(libusb_device_handle *dev, uint8_t *ep_out, uint8_t *ep_in, int *iface) {
    struct libusb_config_descriptor *cfg = NULL;
    if (libusb_get_active_config_descriptor(libusb_get_device(dev), &cfg) != 0 || !cfg) return -1;
    int found = -1;
    for (int i = 0; i < cfg->bNumInterfaces && found < 0; i++) {
        const struct libusb_interface *itf = &cfg->interface[i];
        for (int a = 0; a < itf->num_altsetting; a++) {
            const struct libusb_interface_descriptor *id = &itf->altsetting[a];
            uint8_t out = 0, in = 0;
            for (int e = 0; e < id->bNumEndpoints; e++) {
                const struct libusb_endpoint_descriptor *ep = &id->endpoint[e];
                if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK)
                    continue;
                if (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) in = ep->bEndpointAddress;
                else out = ep->bEndpointAddress;
            }
            if (out && in) {
                *ep_out = out;
                *ep_in = in;
                *iface = id->bInterfaceNumber;
                found = 0;
                break;
            }
        }
    }
    libusb_free_config_descriptor(cfg);
    return found;
}

static int usb_write(SqsPasori *p, const uint8_t *buf, int len) {
    int transferred = 0;
    /* libusb takes a non-const buffer; the transfer only reads it. */
    int rc = libusb_bulk_transfer(p->dev, p->ep_out, (unsigned char *)(void *)buf, len, &transferred,
                                  USB_TIMEOUT_MS);
    return rc == 0 && transferred == len ? 0 : -1;
}

static int usb_read(SqsPasori *p, uint8_t *buf, int cap, int timeout_ms) {
    int transferred = 0;
    int rc = libusb_bulk_transfer(p->dev, p->ep_in, buf, cap, &transferred,
                                  timeout_ms > 0 ? (unsigned)timeout_ms : USB_TIMEOUT_MS);
    if (rc == LIBUSB_ERROR_TIMEOUT) return 0;
    if (rc != 0 || transferred <= 0) return -1;
    return transferred;
}

/* Send one chipset command; fill rsp with the reply body after D7 (cmd+1).
 * Returns body length, 0 on empty, -1 on transport/protocol error. */
static int chip_cmd(SqsPasori *p, uint8_t cmd, const uint8_t *args, size_t narg, uint8_t *rsp,
                    size_t cap, int read_timeout_ms) {
    uint8_t payload[300], frame[FRAME_CAP], ackbuf[64], data[FRAME_CAP];
    int plen = sqs_pasori_cmd_build(cmd, args, narg, payload, sizeof payload);
    if (plen < 0) return -1;
    int flen = sqs_pasori_frame_wrap(payload, (size_t)plen, frame, sizeof frame);
    if (flen < 0) return -1;
    if (usb_write(p, frame, flen) != 0) return -1;

    int n = usb_read(p, ackbuf, (int)sizeof ackbuf, USB_TIMEOUT_MS);
    if (n <= 0) return -1;
    size_t ignore = 0;
    if (sqs_pasori_frame_unwrap(ackbuf, (size_t)n, NULL, 0, &ignore) != 0) {
        /* Some firmwares skip ACK and go straight to data; try parsing as data. */
        uint8_t body[300];
        size_t blen = 0;
        if (sqs_pasori_frame_unwrap(ackbuf, (size_t)n, body, sizeof body, &blen) != 1) return -1;
        if (blen < 2 || body[0] != 0xD7 || body[1] != (uint8_t)(cmd + 1)) return -1;
        size_t copy = blen - 2;
        if (copy > cap) return -1;
        memcpy(rsp, body + 2, copy);
        return (int)copy;
    }

    int wait = read_timeout_ms > USB_TIMEOUT_MS ? read_timeout_ms : USB_TIMEOUT_MS;
    n = usb_read(p, data, (int)sizeof data, wait);
    if (n <= 0) return -1;
    uint8_t body[300];
    size_t blen = 0;
    if (sqs_pasori_frame_unwrap(data, (size_t)n, body, sizeof body, &blen) != 1) return -1;
    if (blen < 2 || body[0] != 0xD7 || body[1] != (uint8_t)(cmd + 1)) return -1;
    size_t copy = blen - 2;
    if (copy > cap) return -1;
    memcpy(rsp, body + 2, copy);
    return (int)copy;
}

static int status_ok(const uint8_t *rsp, int n) { return n >= 1 && rsp[0] == 0x00; }

static int init_initiator(SqsPasori *p, char *err, size_t n) {
    uint8_t rsp[64];
    int r;
    /* Clear any half-finished exchange. */
    usb_write(p, SQS_PASORI_ACK, (int)sizeof SQS_PASORI_ACK);

    const uint8_t type1 = 0x01;
    r = chip_cmd(p, SQS_PASORI_CMD_SET_COMMAND_TYPE, &type1, 1, rsp, sizeof rsp, USB_TIMEOUT_MS);
    if (r < 0 || !status_ok(rsp, r)) return fail(err, n, "RC-S380 SetCommandType failed");

    r = chip_cmd(p, SQS_PASORI_CMD_GET_FIRMWARE_VERSION, NULL, 0, rsp, sizeof rsp, USB_TIMEOUT_MS);
    if (r >= 2) {
        p->fw_minor = rsp[0];
        p->fw_major = rsp[1];
    }

    r = chip_cmd(p, SQS_PASORI_CMD_GET_PD_DATA_VERSION, NULL, 0, rsp, sizeof rsp, USB_TIMEOUT_MS);
    (void)r;

    const uint8_t off = 0x00;
    r = chip_cmd(p, SQS_PASORI_CMD_SWITCH_RF, &off, 1, rsp, sizeof rsp, USB_TIMEOUT_MS);
    if (r < 0 || !status_ok(rsp, r)) return fail(err, n, "RC-S380 SwitchRF off failed");

    r = chip_cmd(p, SQS_PASORI_CMD_IN_SET_RF, RF_212F, sizeof RF_212F, rsp, sizeof rsp, USB_TIMEOUT_MS);
    if (r < 0 || !status_ok(rsp, r)) return fail(err, n, "RC-S380 InSetRF (212F) failed");

    r = chip_cmd(p, SQS_PASORI_CMD_IN_SET_PROTOCOL, PROTOCOL_DEFAULTS, sizeof PROTOCOL_DEFAULTS, rsp,
                 sizeof rsp, USB_TIMEOUT_MS);
    if (r < 0 || !status_ok(rsp, r)) return fail(err, n, "RC-S380 InSetProtocol failed");

    r = chip_cmd(p, SQS_PASORI_CMD_IN_SET_PROTOCOL, PROTOCOL_GUARD, sizeof PROTOCOL_GUARD, rsp,
                 sizeof rsp, USB_TIMEOUT_MS);
    if (r < 0 || !status_ok(rsp, r)) return fail(err, n, "RC-S380 InSetProtocol guard failed");
    return 0;
}

static int card_present(SqsPasori *p) {
    /* Wildcard Polling; a mute field returns timeout status → 0. */
    uint8_t poll[6] = {0x06, 0x00, 0xFF, 0xFF, 0x00, 0x00};
    uint8_t body[16], cmd_args[16], rsp[64], frame[64];
    int blen = sqs_pasori_incomm_body(50, poll, sizeof poll, body, sizeof body);
    if (blen < 0) return 0;
    int r = chip_cmd(p, SQS_PASORI_CMD_IN_COMM_RF, body, (size_t)blen, rsp, sizeof rsp, 500);
    if (r < 0) return 0;
    int fl = sqs_pasori_incomm_unwrap(rsp, (size_t)r, frame, sizeof frame);
    (void)cmd_args;
    return fl > 0;
}

int sqs_pasori_list(char *buf, size_t cap, char *err, size_t n) {
    if (!buf || cap < 2) return fail(err, n, "list buffer too small");
    buf[0] = buf[1] = 0;
    libusb_context *ctx = NULL;
    if (libusb_init(&ctx) != 0) return fail(err, n, "libusb_init failed");
    libusb_device **list = NULL;
    ssize_t cnt = libusb_get_device_list(ctx, &list);
    if (cnt < 0) {
        libusb_exit(ctx);
        return fail(err, n, "cannot enumerate USB devices");
    }
    size_t o = 0;
    int found = 0;
    for (ssize_t i = 0; i < cnt; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
        if (desc.idVendor != SQS_PASORI_VID || !is_pasori_pid(desc.idProduct)) continue;
        char name[64];
        snprintf(name, sizeof name, "usb:RC-S380 (%04x:%04x)", desc.idVendor, desc.idProduct);
        size_t need = strlen(name) + 1;
        if (o + need + 1 > cap) {
            libusb_free_device_list(list, 1);
            libusb_exit(ctx);
            return fail(err, n, "list buffer too small for RC-S380");
        }
        memcpy(buf + o, name, need);
        o += need;
        found++;
    }
    buf[o] = 0;
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return found;
}

SqsPasori *sqs_pasori_open(const char *reader, int wait_ms, int *no_card, char *err, size_t n) {
    *no_card = 0;
    SqsPasori *p = calloc(1, sizeof *p);
    if (!p) {
        fail(err, n, "out of memory");
        return NULL;
    }
    if (libusb_init(&p->ctx) != 0) {
        fail(err, n, "libusb_init failed");
        free(p);
        return NULL;
    }
    libusb_device **list = NULL;
    ssize_t cnt = libusb_get_device_list(p->ctx, &list);
    if (cnt < 0) {
        fail(err, n, "cannot enumerate USB devices");
        goto fail_ctx;
    }
    libusb_device *pick = NULL;
    for (ssize_t i = 0; i < cnt; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
        if (desc.idVendor != SQS_PASORI_VID || !is_pasori_pid(desc.idProduct)) continue;
        char name[64];
        snprintf(name, sizeof name, "usb:RC-S380 (%04x:%04x)", desc.idVendor, desc.idProduct);
        if (!reader_matches(name, reader)) continue;
        pick = list[i];
        snprintf(p->name, sizeof p->name, "%s", name);
        break;
    }
    if (!pick) {
        fail(err, n,
             reader && *reader ? "no RC-S380 matches \"%s\"" : "no RC-S380 USB reader is connected",
             reader);
        libusb_free_device_list(list, 1);
        goto fail_ctx;
    }
    int rc = libusb_open(pick, &p->dev);
    libusb_free_device_list(list, 1);
    list = NULL;
    if (rc != 0 || !p->dev) {
        fail(err, n, "cannot open RC-S380 (permission? try udev rules or sudo): %s",
             libusb_strerror(rc));
        goto fail_ctx;
    }
    libusb_set_auto_detach_kernel_driver(p->dev, 1);
    if (libusb_set_configuration(p->dev, 1) != 0) {
        /* Already configured is fine on some hosts. */
    }
    if (find_bulk_eps(p->dev, &p->ep_out, &p->ep_in, &p->iface) != 0) {
        fail(err, n, "RC-S380 has no bulk endpoints");
        goto fail_dev;
    }
    if (libusb_claim_interface(p->dev, p->iface) != 0) {
        fail(err, n, "cannot claim RC-S380 interface (in use by another process?)");
        goto fail_dev;
    }
    if (init_initiator(p, err, n) != 0) goto fail_iface;

    int waited = 0;
    for (;;) {
        if (card_present(p)) return p;
        if (waited >= wait_ms) {
            *no_card = 1;
            fail(err, n, "no card on the RC-S380");
            goto fail_iface;
        }
        int step = wait_ms - waited < 200 ? wait_ms - waited : 200;
        sleep_ms(step);
        waited += step;
    }

fail_iface:
    libusb_release_interface(p->dev, p->iface);
fail_dev:
    libusb_close(p->dev);
fail_ctx:
    libusb_exit(p->ctx);
    free(p);
    return NULL;
}

void sqs_pasori_close(SqsPasori *p) {
    if (!p) return;
    uint8_t rsp[16];
    const uint8_t off = 0x00;
    chip_cmd(p, SQS_PASORI_CMD_SWITCH_RF, &off, 1, rsp, sizeof rsp, USB_TIMEOUT_MS);
    libusb_release_interface(p->dev, p->iface);
    libusb_close(p->dev);
    libusb_exit(p->ctx);
    free(p);
}

const char *sqs_pasori_reader_name(const SqsPasori *p) { return p ? p->name : ""; }

static int pasori_exchange(void *ctx, const uint8_t *cmd, size_t len, uint8_t *resp, size_t cap,
                           int timeout_ms) {
    SqsPasori *p = ctx;
    uint8_t body[300], rsp[300];
    int blen = sqs_pasori_incomm_body(timeout_ms > 0 ? timeout_ms : 100, cmd, len, body, sizeof body);
    if (blen < 0) return -1;
    int read_ms = timeout_ms > 0 ? timeout_ms + 200 : USB_TIMEOUT_MS;
    int r = chip_cmd(p, SQS_PASORI_CMD_IN_COMM_RF, body, (size_t)blen, rsp, sizeof rsp, read_ms);
    if (r < 0) return -1;
    return sqs_pasori_incomm_unwrap(rsp, (size_t)r, resp, cap);
}

SqsTransport sqs_pasori_transport(SqsPasori *p) {
    SqsTransport t = {pasori_exchange, p};
    return t;
}
