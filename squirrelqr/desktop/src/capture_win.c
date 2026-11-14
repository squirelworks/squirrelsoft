/* qrcapture for Windows: GDI BitBlt of a monitor region, with DXGI Desktop
 * Duplication as a full-desktop fallback. Rubber-band --select via a layered
 * topmost window. Original code. */
#include "qrcapture_priv.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <initguid.h>
#include <dxgi1_2.h>
#include <d3d11.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *qr_capture_backend(void) { return "gdi-dxgi"; }

static int fail(char *err, size_t n, const char *fmt, ...) {
    if (err && n) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, n, fmt, ap);
        va_end(ap);
    }
    return -1;
}

static int bgr_to_gray(const uint8_t *bgra, int w, int h, int stride, int bpp, QrImage *out, char *err,
                       size_t n) {
    out->gray = malloc((size_t)w * (size_t)h);
    if (!out->gray) return fail(err, n, "out of memory");
    out->w = w;
    out->h = h;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = bgra + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            unsigned b, g, r;
            if (bpp == 32) {
                b = row[4 * x + 0];
                g = row[4 * x + 1];
                r = row[4 * x + 2];
            } else {
                b = row[3 * x + 0];
                g = row[3 * x + 1];
                r = row[3 * x + 2];
            }
            out->gray[(size_t)y * w + x] = qr_luma(r, g, b);
        }
    }
    return 0;
}

static int capture_gdi(QrRect r, QrImage *out, char *err, size_t n) {
    int sw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int ox = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int oy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    if (sw < 1 || sh < 1) return fail(err, n, "cannot read the virtual screen size");

    if (r.w <= 0 || r.h <= 0) {
        r.x = ox;
        r.y = oy;
        r.w = sw;
        r.h = sh;
    }
    if (r.x < ox) {
        r.w -= ox - r.x;
        r.x = ox;
    }
    if (r.y < oy) {
        r.h -= oy - r.y;
        r.y = oy;
    }
    if (r.x + r.w > ox + sw) r.w = ox + sw - r.x;
    if (r.y + r.h > oy + sh) r.h = oy + sh - r.y;
    if (r.w < 1 || r.h < 1) return fail(err, n, "the rectangle is outside the %dx%d screen", sw, sh);

    HDC screen = GetDC(NULL);
    if (!screen) return fail(err, n, "GetDC failed");
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, r.w, r.h);
    if (!mem || !bmp) {
        if (bmp) DeleteObject(bmp);
        if (mem) DeleteDC(mem);
        ReleaseDC(NULL, screen);
        return fail(err, n, "CreateCompatibleBitmap failed");
    }
    HGDIOBJ old = SelectObject(mem, bmp);
    if (!BitBlt(mem, 0, 0, r.w, r.h, screen, r.x, r.y, SRCCOPY | CAPTUREBLT)) {
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(NULL, screen);
        return fail(err, n, "BitBlt failed");
    }

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof bmi);
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = r.w;
    bmi.bmiHeader.biHeight = -r.h; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    size_t stride = ((size_t)r.w * 4 + 3) & ~(size_t)3;
    uint8_t *pixels = malloc(stride * (size_t)r.h);
    if (!pixels) {
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(NULL, screen);
        return fail(err, n, "out of memory");
    }
    if (!GetDIBits(mem, bmp, 0, (UINT)r.h, pixels, &bmi, DIB_RGB_COLORS)) {
        free(pixels);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(NULL, screen);
        return fail(err, n, "GetDIBits failed");
    }
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);

    int rc = bgr_to_gray(pixels, r.w, r.h, (int)stride, 32, out, err, n);
    free(pixels);
    return rc;
}

/* DXGI Desktop Duplication of the primary output, then crop to r. */
static int capture_dxgi(QrRect r, QrImage *out, char *err, size_t n) {
    HRESULT hr;
    IDXGIFactory1 *factory = NULL;
    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
    if (FAILED(hr) || !factory) return fail(err, n, "CreateDXGIFactory1 failed");

    IDXGIAdapter1 *adapter = NULL;
    hr = IDXGIFactory1_EnumAdapters1(factory, 0, &adapter);
    if (FAILED(hr) || !adapter) {
        IDXGIFactory1_Release(factory);
        return fail(err, n, "no DXGI adapter");
    }

    D3D_FEATURE_LEVEL level;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *ctx = NULL;
    hr = D3D11CreateDevice((IDXGIAdapter *)adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0,
                           D3D11_SDK_VERSION, &device, &level, &ctx);
    if (FAILED(hr) || !device) {
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory1_Release(factory);
        return fail(err, n, "D3D11CreateDevice failed");
    }

    IDXGIOutput *output = NULL;
    hr = IDXGIAdapter1_EnumOutputs(adapter, 0, &output);
    if (FAILED(hr) || !output) {
        ID3D11DeviceContext_Release(ctx);
        ID3D11Device_Release(device);
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory1_Release(factory);
        return fail(err, n, "no DXGI output");
    }

    IDXGIOutput1 *output1 = NULL;
    hr = IDXGIOutput_QueryInterface(output, &IID_IDXGIOutput1, (void **)&output1);
    if (FAILED(hr) || !output1) {
        IDXGIOutput_Release(output);
        ID3D11DeviceContext_Release(ctx);
        ID3D11Device_Release(device);
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory1_Release(factory);
        return fail(err, n, "IDXGIOutput1 unavailable");
    }

    IDXGIOutputDuplication *dup = NULL;
    hr = IDXGIOutput1_DuplicateOutput(output1, (IUnknown *)device, &dup);
    if (FAILED(hr) || !dup) {
        IDXGIOutput1_Release(output1);
        IDXGIOutput_Release(output);
        ID3D11DeviceContext_Release(ctx);
        ID3D11Device_Release(device);
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory1_Release(factory);
        return fail(err, n, "DuplicateOutput failed (another capture may hold it)");
    }

    DXGI_OUTDUPL_FRAME_INFO info;
    IDXGIResource *res = NULL;
    hr = IDXGIOutputDuplication_AcquireNextFrame(dup, 1000, &info, &res);
    if (FAILED(hr) || !res) {
        IDXGIOutputDuplication_Release(dup);
        IDXGIOutput1_Release(output1);
        IDXGIOutput_Release(output);
        ID3D11DeviceContext_Release(ctx);
        ID3D11Device_Release(device);
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory1_Release(factory);
        return fail(err, n, "AcquireNextFrame failed");
    }

    ID3D11Texture2D *tex = NULL;
    hr = IDXGIResource_QueryInterface(res, &IID_ID3D11Texture2D, (void **)&tex);
    IDXGIResource_Release(res);
    if (FAILED(hr) || !tex) {
        IDXGIOutputDuplication_ReleaseFrame(dup);
        IDXGIOutputDuplication_Release(dup);
        IDXGIOutput1_Release(output1);
        IDXGIOutput_Release(output);
        ID3D11DeviceContext_Release(ctx);
        ID3D11Device_Release(device);
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory1_Release(factory);
        return fail(err, n, "texture QI failed");
    }

    D3D11_TEXTURE2D_DESC desc;
    ID3D11Texture2D_GetDesc(tex, &desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D *staging = NULL;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
    if (FAILED(hr) || !staging) {
        ID3D11Texture2D_Release(tex);
        IDXGIOutputDuplication_ReleaseFrame(dup);
        IDXGIOutputDuplication_Release(dup);
        IDXGIOutput1_Release(output1);
        IDXGIOutput_Release(output);
        ID3D11DeviceContext_Release(ctx);
        ID3D11Device_Release(device);
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory1_Release(factory);
        return fail(err, n, "CreateTexture2D staging failed");
    }
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)tex);
    ID3D11Texture2D_Release(tex);

    D3D11_MAPPED_SUBRESOURCE map;
    hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) {
        ID3D11Texture2D_Release(staging);
        IDXGIOutputDuplication_ReleaseFrame(dup);
        IDXGIOutputDuplication_Release(dup);
        IDXGIOutput1_Release(output1);
        IDXGIOutput_Release(output);
        ID3D11DeviceContext_Release(ctx);
        ID3D11Device_Release(device);
        IDXGIAdapter1_Release(adapter);
        IDXGIFactory1_Release(factory);
        return fail(err, n, "Map staging failed");
    }

    QrImage full;
    full.w = full.h = 0;
    full.gray = NULL;
    int rc = bgr_to_gray((const uint8_t *)map.pData, (int)desc.Width, (int)desc.Height, (int)map.RowPitch, 32,
                         &full, err, n);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    ID3D11Texture2D_Release(staging);
    IDXGIOutputDuplication_ReleaseFrame(dup);
    IDXGIOutputDuplication_Release(dup);
    IDXGIOutput1_Release(output1);
    IDXGIOutput_Release(output);
    ID3D11DeviceContext_Release(ctx);
    ID3D11Device_Release(device);
    IDXGIAdapter1_Release(adapter);
    IDXGIFactory1_Release(factory);
    if (rc != 0) return rc;
    rc = qr_capture_crop(&full, r, out, err, n);
    if (rc != 0) qr_image_free(&full);
    return rc;
}

int qr_capture(QrRect r, QrImage *out, char *err, size_t n) {
    if (qr_capture_try_synth(r, out, err, n)) return out->gray ? 0 : -1;
    out->w = out->h = 0;
    out->gray = NULL;
    if (capture_gdi(r, out, err, n) == 0) return 0;
    char gdi_err[160];
    snprintf(gdi_err, sizeof gdi_err, "%s", err ? err : "");
    if (capture_dxgi(r, out, err, n) == 0) return 0;
    if (err && n && gdi_err[0]) snprintf(err, n, "GDI: %s; DXGI also failed", gdi_err);
    return -1;
}

/* ---- rubber-band select ---- */

typedef struct {
    HWND hwnd;
    int dragging, done;
    int x0, y0, x1, y1;
} SelState;

static LRESULT CALLBACK sel_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SelState *st = (SelState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_LBUTTONDOWN:
        if (st) {
            st->dragging = 1;
            POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
            ClientToScreen(hwnd, &pt);
            st->x0 = st->x1 = pt.x;
            st->y0 = st->y1 = pt.y;
            SetCapture(hwnd);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (st && st->dragging) {
            POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
            ClientToScreen(hwnd, &pt);
            st->x1 = pt.x;
            st->y1 = pt.y;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (st && st->dragging) {
            POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
            ClientToScreen(hwnd, &pt);
            st->x1 = pt.x;
            st->y1 = pt.y;
            st->dragging = 0;
            st->done = 1;
            ReleaseCapture();
            PostQuitMessage(0);
        }
        return 0;
    case WM_RBUTTONDOWN:
        if (st) {
            st->done = -1;
            ReleaseCapture();
            PostQuitMessage(0);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && st) {
            st->done = -1;
            ReleaseCapture();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hwnd, msg, wp, lp);
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH dim = CreateSolidBrush(RGB(40, 40, 40));
        FillRect(hdc, &rc, dim);
        DeleteObject(dim);
        if (st && (st->dragging || st->done == 1)) {
            int x = st->x0 < st->x1 ? st->x0 : st->x1;
            int y = st->y0 < st->y1 ? st->y0 : st->y1;
            int w = abs(st->x1 - st->x0);
            int h = abs(st->y1 - st->y0);
            POINT tl = {x, y};
            ScreenToClient(hwnd, &tl);
            RECT hole = {tl.x, tl.y, tl.x + w, tl.y + h};
            HBRUSH clear = CreateSolidBrush(RGB(200, 200, 200));
            FillRect(hdc, &hole, clear);
            DeleteObject(clear);
            FrameRect(hdc, &hole, GetStockObject(BLACK_BRUSH));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    default: return DefWindowProc(hwnd, msg, wp, lp);
    }
}

int qr_select_rect(QrRect *r, char *err, size_t n) {
    {
        int syn = qr_select_try_synth(r, err, n);
        if (syn <= 0) return syn;
    }
    int sw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int ox = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int oy = GetSystemMetrics(SM_YVIRTUALSCREEN);

    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = sel_proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"SquirrelQrSelect";
    wc.hCursor = LoadCursor(NULL, IDC_CROSS);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName, L"",
                                WS_POPUP, ox, oy, sw, sh, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) return fail(err, n, "cannot create the selection window");
    SetLayeredWindowAttributes(hwnd, 0, 96, LWA_ALPHA);

    SelState st;
    memset(&st, 0, sizeof st);
    st.hwnd = hwnd;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)&st);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    DestroyWindow(hwnd);

    if (st.done < 0) return 1;
    r->x = st.x0 < st.x1 ? st.x0 : st.x1;
    r->y = st.y0 < st.y1 ? st.y0 : st.y1;
    r->w = abs(st.x1 - st.x0);
    r->h = abs(st.y1 - st.y0);
    if (r->w < 8 || r->h < 8) return fail(err, n, "the selection is too small");
    return 0;
}
