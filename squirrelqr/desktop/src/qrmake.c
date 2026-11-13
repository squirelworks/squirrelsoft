/* qrmake: QR code generation and output (see qrmake.h). Original code. */
#include "qrmake.h"

#include <stdlib.h>
#include <string.h>

int qr_level_parse(const char *s)
{
    if (!s || !s[0] || s[1])
        return -1;
    switch (s[0] | 32) {
    case 'l': return SQQR_EC_L;
    case 'm': return SQQR_EC_M;
    case 'q': return SQQR_EC_Q;
    case 'h': return SQQR_EC_H;
    default: return -1;
    }
}

int qr_make(const char *text, size_t len, SqqrEcLevel level, QrSymbol *s, char *err, size_t errlen)
{
    SqqrEncodeOptions o = {level, 0, -1, -1, SQQR_MODE_AUTO};
    int rc = sqqr_encode((const uint8_t *)text, len, &o, s->grid, sizeof s->grid, &s->info);
    if (rc == SQQR_OK)
        return 0;
    if (rc == SQQR_ERR_OVERFLOW)
        snprintf(err, errlen, "%zu bytes is too much for a QR code at level %c (try a lower level)",
                 len, "LMQH"[level]);
    else
        snprintf(err, errlen, "%s", sqqr_strerror(rc));
    return -1;
}

int qr_symbol_image(const QrSymbol *s, int scale, int border, QrImage *out)
{
    if (scale < 1 || border < 0)
        return -1;
    int n = s->info.size, side = (n + 2 * border) * scale;
    if (side > QR_IMAGE_MAX_SIDE)
        return -1;
    out->w = out->h = side;
    out->gray = malloc((size_t)side * (size_t)side);
    if (!out->gray)
        return -1;
    for (int y = 0; y < side; y++)
        for (int x = 0; x < side; x++) {
            int r = y / scale - border, c = x / scale - border;
            int dark = r >= 0 && c >= 0 && r < n && c < n && s->grid[r * n + c];
            out->gray[(size_t)y * side + x] = dark ? 0 : 255;
        }
    return 0;
}

/* ---- PNG writer: IHDR, one IDAT of stored deflate blocks, IEND ---- */

static uint32_t crc_table[256];

static void crc_init(void)
{
    if (crc_table[1])
        return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}

static uint32_t crc_update(uint32_t c, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

typedef struct {
    uint8_t *buf;
    size_t cap, len;
} Sink;

static void put8(Sink *s, uint8_t b)
{
    if (s->buf && s->len < s->cap)
        s->buf[s->len] = b;
    s->len++;
}

static void put32(Sink *s, uint32_t v)
{
    put8(s, (uint8_t)(v >> 24));
    put8(s, (uint8_t)(v >> 16));
    put8(s, (uint8_t)(v >> 8));
    put8(s, (uint8_t)v);
}

/* CRC over what was written since start (type and data), when it is in buf */
static void put_crc(Sink *s, size_t start)
{
    uint32_t c = 0xFFFFFFFFu;
    if (s->buf && s->len <= s->cap)
        c = crc_update(c, s->buf + start, s->len - start);
    put32(s, c ^ 0xFFFFFFFFu);
}

size_t qr_png_encode_gray(const QrImage *img, uint8_t *buf, size_t cap)
{
    crc_init();
    Sink s = {buf, cap, 0};
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    for (int i = 0; i < 8; i++)
        put8(&s, sig[i]);
    size_t start;
    put32(&s, 13);
    start = s.len;
    put8(&s, 'I'); put8(&s, 'H'); put8(&s, 'D'); put8(&s, 'R');
    put32(&s, (uint32_t)img->w);
    put32(&s, (uint32_t)img->h);
    put8(&s, 8); /* depth */
    put8(&s, 0); /* grayscale */
    put8(&s, 0);
    put8(&s, 0);
    put8(&s, 0);
    put_crc(&s, start);

    /* raw stream: a filter byte (0) before every row */
    size_t raw = (size_t)img->h * ((size_t)img->w + 1);
    size_t blocks = (raw + 65534) / 65535;
    if (blocks == 0)
        blocks = 1;
    size_t zlen = 2 + raw + 5 * blocks + 4;
    put32(&s, (uint32_t)zlen);
    start = s.len;
    put8(&s, 'I'); put8(&s, 'D'); put8(&s, 'A'); put8(&s, 'T');
    put8(&s, 0x78); /* zlib: deflate, 32K window, no dictionary, fastest */
    put8(&s, 0x01);
    uint32_t a = 1, b = 0; /* Adler-32 */
    size_t done = 0;
    for (size_t k = 0; k < blocks; k++) {
        size_t n = raw - done < 65535 ? raw - done : 65535;
        put8(&s, k + 1 == blocks ? 1 : 0); /* BFINAL, BTYPE 00 */
        put8(&s, (uint8_t)n);
        put8(&s, (uint8_t)(n >> 8));
        put8(&s, (uint8_t)~n);
        put8(&s, (uint8_t)(~n >> 8));
        for (size_t i = 0; i < n; i++, done++) {
            size_t row = done / ((size_t)img->w + 1), col = done % ((size_t)img->w + 1);
            uint8_t v = col == 0 ? 0 : img->gray[row * (size_t)img->w + col - 1];
            put8(&s, v);
            a = (a + v) % 65521;
            b = (b + a) % 65521;
        }
    }
    put32(&s, b << 16 | a);
    put_crc(&s, start);
    put32(&s, 0);
    start = s.len;
    put8(&s, 'I'); put8(&s, 'E'); put8(&s, 'N'); put8(&s, 'D');
    put_crc(&s, start);
    return s.len;
}

int qr_png_write_gray(const char *path, const QrImage *img)
{
    size_t n = qr_png_encode_gray(img, NULL, 0);
    uint8_t *buf = malloc(n);
    if (!buf)
        return -1;
    qr_png_encode_gray(img, buf, n);
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(buf, 1, n, f) == n;
    if (f && fclose(f) != 0)
        ok = 0;
    free(buf);
    return ok ? 0 : -1;
}

int qr_write_svg(const char *path, const QrSymbol *s, int border)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    int n = s->info.size, side = n + 2 * border;
    fprintf(f,
            "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" "
            "shape-rendering=\"crispEdges\">\n<rect width=\"%d\" height=\"%d\" fill=\"#fff\"/>\n"
            "<path fill=\"#000\" d=\"",
            side, side, side, side);
    /* one horizontal run per subpath */
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n;) {
            if (!s->grid[r * n + c]) {
                c++;
                continue;
            }
            int c0 = c;
            while (c < n && s->grid[r * n + c])
                c++;
            fprintf(f, "M%d %dh%dv1h-%dz", c0 + border, r + border, c - c0, c - c0);
        }
    fprintf(f, "\"/>\n</svg>\n");
    return fclose(f) == 0 ? 0 : -1;
}

void qr_print_terminal(FILE *f, const QrSymbol *s, int border, int invert)
{
    int n = s->info.size;
    /* a cell is drawn (ink) where the module is light, or dark when inverted */
    for (int r = -border; r < n + border; r += 2) {
        for (int c = -border; c < n + border; c++) {
            int top = r >= 0 && r < n && c >= 0 && c < n && s->grid[r * n + c];
            int bot = r + 1 >= 0 && r + 1 < n && c >= 0 && c < n && s->grid[(r + 1) * n + c];
            if (r + 1 >= n + border)
                bot = !invert; /* past the bottom: no ink */
            int it = invert ? top : !top, ib = invert ? bot : !bot;
            fputs(it && ib ? "\xE2\x96\x88" : it ? "\xE2\x96\x80" : ib ? "\xE2\x96\x84" : " ", f);
        }
        fputc('\n', f);
    }
}

static int ends_with_ci(const char *s, const char *ext)
{
    size_t n = strlen(s), m = strlen(ext);
    if (n < m)
        return 0;
    for (size_t i = 0; i < m; i++)
        if ((s[n - m + i] | 32) != (ext[i] | 32))
            return 0;
    return 1;
}

int qr_save_symbol(const char *path, const QrSymbol *s, int scale, int border, char *err, size_t errlen)
{
    if (ends_with_ci(path, ".svg")) {
        if (qr_write_svg(path, s, border) == 0)
            return 0;
        snprintf(err, errlen, "cannot write %s", path);
        return -1;
    }
    int png = ends_with_ci(path, ".png"), pgm = ends_with_ci(path, ".pgm");
    if (!png && !pgm) {
        snprintf(err, errlen, "%s: use a .png, .svg or .pgm name", path);
        return -1;
    }
    QrImage img;
    if (qr_symbol_image(s, scale, border, &img) != 0) {
        snprintf(err, errlen, "image too large at scale %d", scale);
        return -1;
    }
    int rc = png ? qr_png_write_gray(path, &img) : qr_image_save_pgm(path, &img);
    qr_image_free(&img);
    if (rc != 0)
        snprintf(err, errlen, "cannot write %s", path);
    return rc;
}
