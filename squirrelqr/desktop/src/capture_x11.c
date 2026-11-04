/* qrcapture for X11: XGetImage on the root window, and a rubber-band rectangle
 * drawn with an XOR GC while the pointer is grabbed. Original code.
 * Under Wayland this only sees XWayland windows; the portal back end is a
 * later step. */
#include "qrcapture.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static int fail(char *err, size_t n, const char *fmt, ...) {
    if (err && n) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, n, fmt, ap);
        va_end(ap);
    }
    return -1;
}

const char *qr_capture_backend(void) { return "x11"; }

static Display *open_display(char *err, size_t n) {
    Display *d = XOpenDisplay(NULL);
    if (!d) fail(err, n, "cannot open the X display (is DISPLAY set?)");
    return d;
}

typedef struct {
    int shift, bits;
} Field;

static Field field_of(unsigned long mask) {
    Field f = {0, 0};
    if (!mask) return f;
    while (!(mask & 1)) {
        mask >>= 1;
        f.shift++;
    }
    while (mask & 1) {
        mask >>= 1;
        f.bits++;
    }
    return f;
}

static unsigned field_get(unsigned long px, Field f) {
    if (!f.bits) return 0;
    unsigned long v = (px >> f.shift) & ((1ul << f.bits) - 1);
    if (f.bits >= 8) return (unsigned)(v >> (f.bits - 8));
    unsigned long maxv = (1ul << f.bits) - 1;
    return (unsigned)((v * 255 + maxv / 2) / maxv);
}

int qr_capture(QrRect r, QrImage *out, char *err, size_t n) {
    out->w = out->h = 0;
    out->gray = NULL;
    Display *d = open_display(err, n);
    if (!d) return -1;
    Window root = DefaultRootWindow(d);
    XWindowAttributes wa;
    if (!XGetWindowAttributes(d, root, &wa)) {
        XCloseDisplay(d);
        return fail(err, n, "cannot read the root window size");
    }
    if (r.w <= 0 || r.h <= 0) {
        r.x = r.y = 0;
        r.w = wa.width;
        r.h = wa.height;
    }
    if (r.x < 0) {
        r.w += r.x;
        r.x = 0;
    }
    if (r.y < 0) {
        r.h += r.y;
        r.y = 0;
    }
    if (r.x + r.w > wa.width) r.w = wa.width - r.x;
    if (r.y + r.h > wa.height) r.h = wa.height - r.y;
    if (r.w < 1 || r.h < 1) {
        XCloseDisplay(d);
        return fail(err, n, "the rectangle is outside the %dx%d screen", wa.width, wa.height);
    }
    XImage *im = XGetImage(d, root, r.x, r.y, (unsigned)r.w, (unsigned)r.h, AllPlanes, ZPixmap);
    if (!im) {
        XCloseDisplay(d);
        return fail(err, n, "XGetImage failed");
    }
    out->gray = malloc((size_t)r.w * (size_t)r.h);
    if (!out->gray) {
        XDestroyImage(im);
        XCloseDisplay(d);
        return fail(err, n, "out of memory");
    }
    out->w = r.w;
    out->h = r.h;
    unsigned long rm = im->red_mask ? im->red_mask : wa.visual->red_mask;
    unsigned long gm = im->green_mask ? im->green_mask : wa.visual->green_mask;
    unsigned long bm = im->blue_mask ? im->blue_mask : wa.visual->blue_mask;
    Field fr = field_of(rm), fg = field_of(gm), fb = field_of(bm);
    int truecolor = fr.bits && fg.bits && fb.bits;
    XColor *map = NULL;
    if (!truecolor) { /* palette visual: look the colours up once */
        int cells = wa.visual->map_entries > 0 && wa.visual->map_entries <= 4096 ? wa.visual->map_entries : 256;
        map = calloc((size_t)cells, sizeof *map);
        if (map) {
            for (int i = 0; i < cells; i++) map[i].pixel = (unsigned long)i;
            XQueryColors(d, wa.colormap, map, cells);
        }
        for (int y = 0; y < r.h; y++)
            for (int x = 0; x < r.w; x++) {
                unsigned long p = XGetPixel(im, x, y);
                out->gray[(size_t)y * r.w + x] =
                    (map && p < (unsigned long)cells)
                        ? qr_luma(map[p].red >> 8, map[p].green >> 8, map[p].blue >> 8)
                        : (uint8_t)(p ? 255 : 0);
            }
        free(map);
    } else if (im->bits_per_pixel == 32) {
        for (int y = 0; y < r.h; y++) {
            const unsigned char *row = (const unsigned char *)im->data + (size_t)y * im->bytes_per_line;
            for (int x = 0; x < r.w; x++) {
                const unsigned char *b = row + 4 * x;
                unsigned long p = im->byte_order == LSBFirst
                                      ? (unsigned long)b[0] | (unsigned long)b[1] << 8 |
                                            (unsigned long)b[2] << 16 | (unsigned long)b[3] << 24
                                      : (unsigned long)b[3] | (unsigned long)b[2] << 8 |
                                            (unsigned long)b[1] << 16 | (unsigned long)b[0] << 24;
                out->gray[(size_t)y * r.w + x] = qr_luma(field_get(p, fr), field_get(p, fg), field_get(p, fb));
            }
        }
    } else {
        for (int y = 0; y < r.h; y++)
            for (int x = 0; x < r.w; x++) {
                unsigned long p = XGetPixel(im, x, y);
                out->gray[(size_t)y * r.w + x] = qr_luma(field_get(p, fr), field_get(p, fg), field_get(p, fb));
            }
    }
    XDestroyImage(im);
    XCloseDisplay(d);
    return 0;
}

static void outline(Display *d, Window root, GC gc, int x0, int y0, int x1, int y1) {
    int x = x0 < x1 ? x0 : x1, y = y0 < y1 ? y0 : y1;
    unsigned w = (unsigned)abs(x1 - x0), h = (unsigned)abs(y1 - y0);
    XDrawRectangle(d, root, gc, x, y, w, h);
}

int qr_select_rect(QrRect *r, char *err, size_t n) {
    Display *d = open_display(err, n);
    if (!d) return -1;
    Window root = DefaultRootWindow(d);
    int scr = DefaultScreen(d);
    Cursor cur = XCreateFontCursor(d, XC_crosshair);
    if (XGrabPointer(d, root, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
                     GrabModeAsync, None, cur, CurrentTime) != GrabSuccess) {
        XFreeCursor(d, cur);
        XCloseDisplay(d);
        return fail(err, n, "cannot grab the pointer (another program holds it)");
    }
    int have_kbd = XGrabKeyboard(d, root, False, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess;
    XGCValues gv;
    gv.function = GXxor;
    gv.foreground = WhitePixel(d, scr) ^ BlackPixel(d, scr);
    gv.subwindow_mode = IncludeInferiors;
    gv.line_width = 2;
    GC gc = XCreateGC(d, root, GCFunction | GCForeground | GCSubwindowMode | GCLineWidth, &gv);
    int dragging = 0, drawn = 0, done = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0, dx = 0, dy = 0;
    KeyCode esc = XKeysymToKeycode(d, XK_Escape);
    while (!done) {
        XEvent ev;
        XNextEvent(d, &ev);
        switch (ev.type) {
        case ButtonPress:
            if (ev.xbutton.button == Button1) {
                dragging = 1;
                x0 = x1 = ev.xbutton.x_root;
                y0 = y1 = ev.xbutton.y_root;
            } else {
                done = -1;
            }
            break;
        case MotionNotify:
            if (!dragging) break;
            while (XCheckTypedEvent(d, MotionNotify, &ev)) {} /* keep only the latest position */
            if (drawn) outline(d, root, gc, x0, y0, dx, dy); /* XOR again erases */
            dx = ev.xmotion.x_root;
            dy = ev.xmotion.y_root;
            outline(d, root, gc, x0, y0, dx, dy);
            drawn = 1;
            XFlush(d);
            break;
        case ButtonRelease:
            if (dragging && ev.xbutton.button == Button1) {
                x1 = ev.xbutton.x_root;
                y1 = ev.xbutton.y_root;
                done = 1;
            }
            break;
        case KeyPress:
            if (ev.xkey.keycode == esc) done = -1;
            break;
        default: break;
        }
    }
    if (drawn) outline(d, root, gc, x0, y0, dx, dy);
    XUngrabPointer(d, CurrentTime);
    if (have_kbd) XUngrabKeyboard(d, CurrentTime);
    XFreeGC(d, gc);
    XFreeCursor(d, cur);
    XSync(d, False);
    XCloseDisplay(d);
    if (done < 0) return 1;
    r->x = x0 < x1 ? x0 : x1;
    r->y = y0 < y1 ? y0 : y1;
    r->w = abs(x1 - x0);
    r->h = abs(y1 - y0);
    if (r->w < 8 || r->h < 8) return fail(err, n, "the selection is too small");
    return 0;
}
