/* qrcapture for macOS via ScreenCaptureKit (SCScreenshotManager on 14+,
 * single-frame SCStream on 12.3–13). Rubber-band --select with a borderless
 * Cocoa overlay. Original code. */
#import "qrcapture_priv.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *qr_capture_backend(void) { return "sckit"; }

static int fail(char *err, size_t n, const char *fmt, ...) {
    if (err && n) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, n, fmt, ap);
        va_end(ap);
    }
    return -1;
}

static int cgimage_to_gray(CGImageRef img, QrImage *out, char *err, size_t n) {
    if (!img) return fail(err, n, "empty CGImage");
    size_t w = CGImageGetWidth(img);
    size_t h = CGImageGetHeight(img);
    if (w < 1 || h < 1 || w > QR_IMAGE_MAX_SIDE || h > QR_IMAGE_MAX_SIDE)
        return fail(err, n, "unsupported capture size %zux%zu", w, h);
    out->gray = malloc(w * h);
    if (!out->gray) return fail(err, n, "out of memory");
    out->w = (int)w;
    out->h = (int)h;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    /* BGRA so we can read bytes directly after draw. */
    CGContextRef ctx =
        CGBitmapContextCreate(NULL, w, h, 8, w * 4, cs, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(cs);
    if (!ctx) {
        qr_image_free(out);
        return fail(err, n, "CGBitmapContextCreate failed");
    }
    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
    const uint8_t *px = CGBitmapContextGetData(ctx);
    for (size_t y = 0; y < h; y++)
        for (size_t x = 0; x < w; x++) {
            const uint8_t *p = px + (y * w + x) * 4;
            /* BGRA little-endian */
            out->gray[y * w + x] = qr_luma(p[2], p[1], p[0]);
        }
    CGContextRelease(ctx);
    return 0;
}

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
static int capture_sckit_shot(QrRect r, QrImage *out, char *err, size_t n) API_AVAILABLE(macos(14.0)) {
    __block SCShareableContent *content = nil;
    __block NSError *blockErr = nil;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *c, NSError *e) {
      content = c;
      blockErr = e;
      dispatch_semaphore_signal(sem);
    }];
    if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) != 0)
        return fail(err, n, "ScreenCaptureKit content timed out");
    if (!content || content.displays.count < 1)
        return fail(err, n, "ScreenCaptureKit: %s",
                    blockErr ? [[blockErr localizedDescription] UTF8String] : "no display");

    SCDisplay *display = content.displays[0];
    SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
    SCStreamConfiguration *cfg = [[SCStreamConfiguration alloc] init];
    CGRect src;
    if (r.w > 0 && r.h > 0) {
        src = CGRectMake(r.x, r.y, r.w, r.h);
        /* clip to display */
        CGRect bounds = CGRectMake(0, 0, display.width, display.height);
        src = CGRectIntersection(src, bounds);
        if (CGRectIsEmpty(src) || src.size.width < 1 || src.size.height < 1)
            return fail(err, n, "the rectangle is outside the display");
    } else {
        src = CGRectMake(0, 0, display.width, display.height);
    }
    cfg.sourceRect = src;
    cfg.width = (size_t)src.size.width;
    cfg.height = (size_t)src.size.height;
    cfg.showsCursor = NO;
    cfg.scalesToFit = NO;

    __block CGImageRef shot = NULL;
    __block NSError *capErr = nil;
    dispatch_semaphore_t sem2 = dispatch_semaphore_create(0);
    [SCScreenshotManager captureImageWithFilter:filter
                                  configuration:cfg
                              completionHandler:^(CGImageRef image, NSError *e) {
                                if (image) shot = CGImageRetain(image);
                                capErr = e;
                                dispatch_semaphore_signal(sem2);
                              }];
    if (dispatch_semaphore_wait(sem2, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC)) != 0)
        return fail(err, n, "ScreenCaptureKit screenshot timed out");
    if (!shot)
        return fail(err, n, "ScreenCaptureKit: %s",
                    capErr ? [[capErr localizedDescription] UTF8String] : "no image");
    int rc = cgimage_to_gray(shot, out, err, n);
    CGImageRelease(shot);
    return rc;
}
#endif

/* Fallback for older SDKs / runtimes: CGDisplayCreateImage of the main display. */
static int capture_cgdisplay(QrRect r, QrImage *out, char *err, size_t n) {
    CGDirectDisplayID did = CGMainDisplayID();
    CGImageRef full = CGDisplayCreateImage(did);
    if (!full) return fail(err, n, "CGDisplayCreateImage failed");
    QrImage img;
    if (cgimage_to_gray(full, &img, err, n) != 0) {
        CGImageRelease(full);
        return -1;
    }
    CGImageRelease(full);
    int rc = qr_capture_crop(&img, r, out, err, n);
    if (rc != 0) qr_image_free(&img);
    return rc;
}

int qr_capture(QrRect r, QrImage *out, char *err, size_t n) {
    if (qr_capture_try_synth(r, out, err, n)) return out->gray ? 0 : -1;
    out->w = out->h = 0;
    out->gray = NULL;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
    if (@available(macOS 14.0, *)) return capture_sckit_shot(r, out, err, n);
#endif
    return capture_cgdisplay(r, out, err, n);
}

/* ---- rubber-band select ---- */

@interface QrSelectView : NSView
@property(nonatomic, assign) NSPoint start;
@property(nonatomic, assign) NSPoint current;
@property(nonatomic, assign) BOOL dragging;
@property(nonatomic, assign) int result; /* 0 running, 1 ok, -1 cancel */
@end

@implementation QrSelectView
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    [[NSColor colorWithCalibratedWhite:0.0 alpha:0.35] setFill];
    NSRectFill(self.bounds);
    if (!self.dragging) return;
    CGFloat x = MIN(self.start.x, self.current.x);
    CGFloat y = MIN(self.start.y, self.current.y);
    CGFloat w = fabs(self.current.x - self.start.x);
    CGFloat h = fabs(self.current.y - self.start.y);
    NSRect hole = NSMakeRect(x, y, w, h);
    [[NSColor colorWithCalibratedWhite:1.0 alpha:0.15] setFill];
    NSRectFill(hole);
    [[NSColor whiteColor] setStroke];
    NSFrameRectWithWidth(hole, 2.0);
}
- (void)mouseDown:(NSEvent *)ev {
    self.dragging = YES;
    self.start = self.current = [self convertPoint:ev.locationInWindow fromView:nil];
    [self setNeedsDisplay:YES];
}
- (void)mouseDragged:(NSEvent *)ev {
    if (!self.dragging) return;
    self.current = [self convertPoint:ev.locationInWindow fromView:nil];
    [self setNeedsDisplay:YES];
}
- (void)mouseUp:(NSEvent *)ev {
    if (!self.dragging) return;
    self.current = [self convertPoint:ev.locationInWindow fromView:nil];
    self.dragging = NO;
    self.result = 1;
    [NSApp stop:nil];
}
- (void)rightMouseDown:(NSEvent *)ev {
    (void)ev;
    self.result = -1;
    [NSApp stop:nil];
}
- (void)keyDown:(NSEvent *)ev {
    if (ev.keyCode == 53) { /* Escape */
        self.result = -1;
        [NSApp stop:nil];
    }
}
@end

int qr_select_rect(QrRect *r, char *err, size_t n) {
    {
        int syn = qr_select_try_synth(r, err, n);
        if (syn <= 0) return syn;
    }
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSScreen *screen = [NSScreen mainScreen];
        if (!screen) return fail(err, n, "no main screen");
        NSRect frame = screen.frame;
        NSWindow *win = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:NSWindowStyleMaskBorderless
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [win setLevel:NSScreenSaverWindowLevel];
        [win setOpaque:NO];
        [win setBackgroundColor:[NSColor clearColor]];
        [win setIgnoresMouseEvents:NO];
        [win setAcceptsMouseMovedEvents:YES];
        QrSelectView *view = [[QrSelectView alloc] initWithFrame:frame];
        view.result = 0;
        [win setContentView:view];
        [win makeKeyAndOrderFront:nil];
        [win makeFirstResponder:view];
        [NSApp run];
        int result = view.result;
        NSPoint a = view.start, b = view.current;
        [win orderOut:nil];
        if (result < 0) return 1;
        /* Cocoa y is bottom-up; ScreenCaptureKit / CG use top-left. */
        CGFloat x = MIN(a.x, b.x);
        CGFloat yTop = MAX(a.y, b.y);
        CGFloat w = fabs(b.x - a.x);
        CGFloat h = fabs(b.y - a.y);
        CGFloat flippedY = frame.size.height - yTop;
        r->x = (int)lround(x);
        r->y = (int)lround(flippedY);
        r->w = (int)lround(w);
        r->h = (int)lround(h);
        if (r->w < 8 || r->h < 8) return fail(err, n, "the selection is too small");
        return 0;
    }
}
