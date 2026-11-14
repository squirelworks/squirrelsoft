/* qrwebcam for Linux: V4L2 frame grab, plus a synthetic path for CI.
 * Original code. Prefers GREY / YUYV / NV12 / RGB24 / BGR24; MJPEG-only
 * cameras are reported clearly (no JPEG decoder here). */
#include "qrwebcam.h"

#include <linux/videodev2.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

const char *qr_webcam_backend(void) { return "v4l2"; }

static int fail(char *err, size_t n, const char *fmt, ...) {
    if (err && n) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, n, fmt, ap);
        va_end(ap);
    }
    return -1;
}

/* Load an image file as if it were one camera frame (CI / --device @FILE). */
static int grab_file(const char *path, QrImage *out, char *err, size_t n) {
    return qr_image_load(path, out, err, n);
}

static int resolve_device(const char *device, char *buf, size_t n, char *err, size_t en) {
    const char *env = getenv("QR_WEBCAM_FRAME");
    if (device && device[0] == '@') {
        if (!device[1]) return fail(err, en, "--device @FILE needs a path after @");
        if (strlen(device + 1) >= n) return fail(err, en, "device path too long");
        memcpy(buf, device + 1, strlen(device + 1) + 1);
        return 1; /* synthetic file */
    }
    if ((!device || !device[0]) && env && env[0]) {
        if (strlen(env) >= n) return fail(err, en, "QR_WEBCAM_FRAME path too long");
        memcpy(buf, env, strlen(env) + 1);
        return 1;
    }
    const char *d = (device && device[0]) ? device : "/dev/video0";
    if (strlen(d) >= n) return fail(err, en, "device path too long");
    memcpy(buf, d, strlen(d) + 1);
    return 0;
}

static int xioctl(int fd, unsigned long req, void *arg) {
    int r;
    do {
        r = ioctl(fd, req, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

static int is_video_capture(const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof cap);
    int ok = xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0 &&
             ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
              (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE));
    close(fd);
    return ok;
}

int qr_webcam_list(FILE *out) {
    if (!out) return -1;
    const char *env = getenv("QR_WEBCAM_FRAME");
    if (env && env[0]) fprintf(out, "@%s\tsynthetic (QR_WEBCAM_FRAME)\n", env);
    DIR *d = opendir("/dev");
    if (!d) return env && env[0] ? 1 : -1;
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "video", 5) != 0) continue;
        char path[64];
        snprintf(path, sizeof path, "/dev/%s", de->d_name);
        if (!is_video_capture(path)) continue;
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        char name[32] = "?";
        if (fd >= 0) {
            struct v4l2_capability cap;
            memset(&cap, 0, sizeof cap);
            if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
                snprintf(name, sizeof name, "%s", (const char *)cap.card);
                for (char *p = name; *p; p++)
                    if (*p == '\t' || *p == '\n') *p = ' ';
            }
            close(fd);
        }
        fprintf(out, "%s\t%s\n", path, name);
        n++;
    }
    closedir(d);
    if (env && env[0]) n++;
    return n;
}

typedef struct {
    void *start;
    size_t len;
} MapBuf;

static int try_fmt(int fd, uint32_t pix, int w, int h, struct v4l2_format *fmt) {
    memset(fmt, 0, sizeof *fmt);
    fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt->fmt.pix.width = (unsigned)w;
    fmt->fmt.pix.height = (unsigned)h;
    fmt->fmt.pix.pixelformat = pix;
    fmt->fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, fmt) < 0) return -1;
    return fmt->fmt.pix.pixelformat == pix ? 0 : -1;
}

static int pick_format(int fd, struct v4l2_format *fmt) {
    static const uint32_t prefer[] = {
        V4L2_PIX_FMT_GREY, V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_RGB24, V4L2_PIX_FMT_BGR24,
    };
    static const int sizes[][2] = {{640, 480}, {320, 240}, {1280, 720}};
    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++)
        for (size_t p = 0; p < sizeof prefer / sizeof prefer[0]; p++)
            if (try_fmt(fd, prefer[p], sizes[s][0], sizes[s][1], fmt) == 0) return 0;
    /* Accept whatever the driver gives if it is one we can convert. */
    memset(fmt, 0, sizeof *fmt);
    fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_G_FMT, fmt) < 0) return -1;
    uint32_t pix = fmt->fmt.pix.pixelformat;
    for (size_t p = 0; p < sizeof prefer / sizeof prefer[0]; p++)
        if (pix == prefer[p]) return 0;
    return -1;
}

static void yuyv_to_gray(const uint8_t *src, int w, int h, int stride, uint8_t *dst) {
    for (int y = 0; y < h; y++) {
        const uint8_t *row = src + (size_t)y * stride;
        uint8_t *out = dst + (size_t)y * w;
        for (int x = 0; x < w; x++) out[x] = row[x * 2];
    }
}

static void nv12_to_gray(const uint8_t *src, int w, int h, uint8_t *dst) {
    memcpy(dst, src, (size_t)w * (size_t)h); /* Y plane first */
}

static void rgb_to_gray(const uint8_t *src, int w, int h, int stride, int bgr, uint8_t *dst) {
    for (int y = 0; y < h; y++) {
        const uint8_t *row = src + (size_t)y * stride;
        uint8_t *out = dst + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            const uint8_t *p = row + x * 3;
            unsigned r = bgr ? p[2] : p[0], g = p[1], b = bgr ? p[0] : p[2];
            out[x] = qr_luma(r, g, b);
        }
    }
}

static int convert_frame(uint32_t pix, const uint8_t *src, int w, int h, int bytesused, int stride,
                         QrImage *out, char *err, size_t n) {
    out->w = out->h = 0;
    out->gray = NULL;
    if (w < 1 || h < 1 || w > QR_IMAGE_MAX_SIDE || h > QR_IMAGE_MAX_SIDE ||
        (unsigned long)w * (unsigned long)h > QR_IMAGE_MAX_PIXELS)
        return fail(err, n, "unsupported frame size %dx%d", w, h);
    uint8_t *gray = malloc((size_t)w * (size_t)h);
    if (!gray) return fail(err, n, "out of memory");
    if (pix == V4L2_PIX_FMT_GREY) {
        if (bytesused < w * h) {
            free(gray);
            return fail(err, n, "short GREY frame");
        }
        if (stride == w)
            memcpy(gray, src, (size_t)w * h);
        else
            for (int y = 0; y < h; y++) memcpy(gray + (size_t)y * w, src + (size_t)y * stride, (size_t)w);
    } else if (pix == V4L2_PIX_FMT_YUYV) {
        if (stride < w * 2) stride = w * 2;
        if (bytesused < stride * h && bytesused < w * h * 2) {
            free(gray);
            return fail(err, n, "short YUYV frame");
        }
        yuyv_to_gray(src, w, h, stride, gray);
    } else if (pix == V4L2_PIX_FMT_NV12) {
        if (bytesused < w * h) {
            free(gray);
            return fail(err, n, "short NV12 frame");
        }
        nv12_to_gray(src, w, h, gray);
    } else if (pix == V4L2_PIX_FMT_RGB24 || pix == V4L2_PIX_FMT_BGR24) {
        if (stride < w * 3) stride = w * 3;
        if (bytesused < w * h * 3) {
            free(gray);
            return fail(err, n, "short RGB frame");
        }
        rgb_to_gray(src, w, h, stride, pix == V4L2_PIX_FMT_BGR24, gray);
    } else {
        free(gray);
        return fail(err, n, "unsupported pixel format");
    }
    out->gray = gray;
    out->w = w;
    out->h = h;
    return 0;
}

static int grab_mmap(int fd, struct v4l2_format *fmt, QrImage *out, char *err, size_t n) {
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof req);
    req.count = 2;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 1)
        return fail(err, n, "V4L2 mmap buffers: %s", strerror(errno));

    MapBuf *bufs = calloc(req.count, sizeof *bufs);
    if (!bufs) return fail(err, n, "out of memory");
    unsigned nbuf = req.count;
    for (unsigned i = 0; i < nbuf; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            fail(err, n, "VIDIOC_QUERYBUF: %s", strerror(errno));
            goto fail_map;
        }
        bufs[i].len = buf.length;
        bufs[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (bufs[i].start == MAP_FAILED) {
            bufs[i].start = NULL;
            fail(err, n, "mmap: %s", strerror(errno));
            goto fail_map;
        }
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            fail(err, n, "VIDIOC_QBUF: %s", strerror(errno));
            goto fail_map;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        fail(err, n, "VIDIOC_STREAMON: %s", strerror(errno));
        goto fail_map;
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    /* A few frames: some drivers deliver a dark first buffer. */
    int got = -1;
    for (int attempt = 0; attempt < 8; attempt++) {
        if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) continue;
            fail(err, n, "VIDIOC_DQBUF: %s", strerror(errno));
            xioctl(fd, VIDIOC_STREAMOFF, &type);
            goto fail_map;
        }
        int stride = fmt->fmt.pix.bytesperline ? (int)fmt->fmt.pix.bytesperline : (int)fmt->fmt.pix.width;
        got = convert_frame(fmt->fmt.pix.pixelformat, bufs[buf.index].start, (int)fmt->fmt.pix.width,
                            (int)fmt->fmt.pix.height, (int)buf.bytesused, stride, out, err, n);
        xioctl(fd, VIDIOC_QBUF, &buf);
        if (got == 0) break;
    }
    xioctl(fd, VIDIOC_STREAMOFF, &type);

    for (unsigned i = 0; i < nbuf; i++)
        if (bufs[i].start) munmap(bufs[i].start, bufs[i].len);
    free(bufs);
    return got;

fail_map:
    for (unsigned i = 0; i < nbuf; i++)
        if (bufs[i].start) munmap(bufs[i].start, bufs[i].len);
    free(bufs);
    return -1;
}

static int grab_read(int fd, struct v4l2_format *fmt, QrImage *out, char *err, size_t n) {
    size_t cap = fmt->fmt.pix.sizeimage ? fmt->fmt.pix.sizeimage
                                        : (size_t)fmt->fmt.pix.width * fmt->fmt.pix.height * 4;
    if (cap < 4096) cap = 4096;
    uint8_t *buf = malloc(cap);
    if (!buf) return fail(err, n, "out of memory");
    ssize_t got = -1;
    for (int attempt = 0; attempt < 8; attempt++) {
        got = read(fd, buf, cap);
        if (got > 0) break;
        if (got < 0 && errno == EAGAIN) continue;
        free(buf);
        return fail(err, n, "read: %s", strerror(errno));
    }
    if (got <= 0) {
        free(buf);
        return fail(err, n, "no frame from device");
    }
    int stride = fmt->fmt.pix.bytesperline ? (int)fmt->fmt.pix.bytesperline : (int)fmt->fmt.pix.width;
    int rc = convert_frame(fmt->fmt.pix.pixelformat, buf, (int)fmt->fmt.pix.width, (int)fmt->fmt.pix.height,
                           (int)got, stride, out, err, n);
    free(buf);
    return rc;
}

static int grab_v4l2(const char *path, QrImage *out, char *err, size_t n) {
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return fail(err, n, "%s: %s", path, strerror(errno));

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof cap);
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        close(fd);
        return fail(err, n, "%s: not a V4L2 device", path);
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) && !(cap.device_caps & V4L2_CAP_VIDEO_CAPTURE)) {
        close(fd);
        return fail(err, n, "%s: no video capture", path);
    }

    struct v4l2_format fmt;
    if (pick_format(fd, &fmt) < 0) {
        close(fd);
        return fail(err, n,
                    "%s: no GREY/YUYV/NV12/RGB24 format (MJPEG-only cameras need a different device)",
                    path);
    }

    int rc;
    if (cap.capabilities & V4L2_CAP_STREAMING)
        rc = grab_mmap(fd, &fmt, out, err, n);
    else if (cap.capabilities & V4L2_CAP_READWRITE)
        rc = grab_read(fd, &fmt, out, err, n);
    else {
        close(fd);
        return fail(err, n, "%s: neither streaming nor read I/O", path);
    }
    close(fd);
    return rc;
}

int qr_webcam_grab(const char *device, QrImage *out, char *err, size_t n) {
    out->w = out->h = 0;
    out->gray = NULL;
    char path[512];
    int synthetic = resolve_device(device, path, sizeof path, err, n);
    if (synthetic < 0) return -1;
    if (synthetic) return grab_file(path, out, err, n);
    return grab_v4l2(path, out, err, n);
}
