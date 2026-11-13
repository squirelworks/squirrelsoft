/* qrgui: the SquirrelQR small window on libs/sqgui (see qrgui.h).
 * Original code. */
#include "qrgui.h"
#include "qrdecode.h"
#include "qrtext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COL_OUTLINE 0xFF20D060u
#define COL_PREVIEW_BG 0xFF303030u

void qr_gui_init(QrGui *g) {
    memset(g, 0, sizeof *g);
    snprintf(g->status, sizeof g->status, "Select an area, the whole screen, or open a file");
}

static void drop_image(QrGui *g) {
    qr_image_free(&g->img);
    memset(&g->img, 0, sizeof g->img);
    free(g->preview);
    g->preview = NULL;
}

void qr_gui_free(QrGui *g) { drop_image(g); }

void qr_gui_status(QrGui *g, const char *msg) {
    snprintf(g->status, sizeof g->status, "%s", msg);
}

int qr_gui_set_image(QrGui *g, QrImage *img, const char *source) {
    drop_image(g);
    g->img = *img;
    memset(img, 0, sizeof *img);
    size_t n = (size_t)g->img.w * (size_t)g->img.h;
    g->preview = malloc(n * sizeof *g->preview);
    if (g->preview)
        for (size_t i = 0; i < n; i++) {
            uint32_t v = g->img.gray[i];
            g->preview[i] = 0xFF000000u | v << 16 | v << 8 | v;
        }
    int rc = qr_decode_gray(&g->img, &g->res);
    g->found = rc == SQQR_OK;
    if (!g->found) {
        g->text[0] = 0;
        g->charset = NULL;
        snprintf(g->status, sizeof g->status, "%s: %s", source, sqqr_strerror(rc));
        return 0;
    }
    g->charset = qr_payload_text(g->res.data, g->res.len, g->res.eci, g->res.has_kanji, g->text,
                                 sizeof g->text);
    snprintf(g->status, sizeof g->status, "%s: version %d-%c, %s, %zu bytes", source,
             g->res.version, "LMQH"[g->res.ec_level], g->charset, (size_t)g->res.len);
    return 1;
}

int qr_gui_open(QrGui *g, const char *path) {
    QrImage img;
    char err[160];
    if (qr_image_load(path, &img, err, sizeof err)) {
        snprintf(g->status, sizeof g->status, "%s: %s", path, err);
        return -1;
    }
    if (path != g->path) snprintf(g->path, sizeof g->path, "%s", path);
    return qr_gui_set_image(g, &img, path);
}

/* Fit w x h inside r, centred, keeping the aspect ratio */
static sqg_rect fit(sqg_rect r, int w, int h) {
    if (w < 1 || h < 1 || r.w < 1 || r.h < 1) return sqg_rect_make(r.x, r.y, 0, 0);
    int fw = r.w, fh = (int)((long)h * r.w / w);
    if (fh > r.h) {
        fh = r.h;
        fw = (int)((long)w * r.h / h);
    }
    if (fw < 1) fw = 1;
    if (fh < 1) fh = 1;
    return sqg_rect_make(r.x + (r.w - fw) / 2, r.y + (r.h - fh) / 2, fw, fh);
}

static void preview(QrGui *g, sqg_ui *ui, sqg_rect r) {
    sqg_canvas *cv = ui->cv;
    sqg_bevel(cv, r, 1, 0, ui->theme);
    sqg_rect in = sqg_rect_inset(r, 2);
    sqg_fill(cv, in, COL_PREVIEW_BG);
    g->preview_at = sqg_rect_make(in.x, in.y, 0, 0);
    if (!g->preview) {
        const char *hint = "No image yet";
        sqg_text(cv, in.x + (in.w - sqg_text_width(hint, ui->scale)) / 2,
                 in.y + (in.h - sqg_text_height(hint, ui->scale)) / 2, hint, 0xFFA0A0A0u,
                 ui->scale);
        return;
    }
    sqg_rect d = fit(sqg_rect_inset(in, 4), g->img.w, g->img.h);
    g->preview_at = d;
    sqg_blit_scaled(cv, d, g->preview, g->img.w, g->img.h, g->img.w);
    if (!g->found) return;
    sqg_rect old = sqg_clip(cv, in);
    int px[4], py[4];
    for (int i = 0; i < 4; i++) {
        px[i] = d.x + (int)(g->res.corners[2 * i] * d.w / g->img.w);
        py[i] = d.y + (int)(g->res.corners[2 * i + 1] * d.h / g->img.h);
    }
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        for (int t = -1; t <= 1; t++) {
            sqg_line(cv, px[i] + t, py[i], px[j] + t, py[j], COL_OUTLINE);
            sqg_line(cv, px[i], py[i] + t, px[j], py[j] + t, COL_OUTLINE);
        }
    }
    cv->clip = old;
}

int qr_gui_frame(QrGui *g, sqg_ui *ui) {
    sqg_canvas *cv = ui->cv;
    int action = QR_GUI_NONE, lh = sqg_line_h(ui), pad = 6;
    sqg_fill(cv, sqg_rect_make(0, 0, cv->w, cv->h), ui->theme->face);

    sqg_row row = sqg_row_begin(sqg_rect_make(pad, pad, cv->w - 2 * pad, lh), 6);
    if (sqg_button(ui, sqg_id_str("select"), sqg_row_take(&row, sqg_button_w(ui, "Select area")),
                   "Select area"))
        action = QR_GUI_SELECT;
    if (sqg_button(ui, sqg_id_str("screen"),
                   sqg_row_take(&row, sqg_button_w(ui, "Whole screen")), "Whole screen"))
        action = QR_GUI_SCREEN;
    if (sqg_button(ui, sqg_id_str("copy"), sqg_row_take(&row, sqg_button_w(ui, "Copy text")),
                   "Copy text") &&
        g->found)
        action = QR_GUI_COPY;

    int y = pad + lh + pad;
    sqg_row frow = sqg_row_begin(sqg_rect_make(pad, y, cv->w - 2 * pad, lh), 6);
    sqg_label(ui, sqg_row_take(&frow, sqg_text_width("File", ui->scale) + 4), "File",
              SQG_ALIGN_LEFT);
    sqg_rect open_r = sqg_rect_make(0, 0, 0, 0);
    int fw = frow.area.x + frow.area.w - frow.x - sqg_button_w(ui, "Open") - 6;
    int enter = sqg_textfield(ui, sqg_id_str("path"), sqg_row_take(&frow, fw), g->path,
                              sizeof g->path);
    open_r = sqg_row_take(&frow, 0);
    if ((sqg_button(ui, sqg_id_str("open"), open_r, "Open") || enter) && g->path[0])
        qr_gui_open(g, g->path);

    /* status at the bottom, the text above it, the preview takes the rest */
    int status_y = cv->h - pad - lh;
    int text_h = 4 * (lh - 4) + 8;
    int text_y = status_y - pad - text_h;
    y += lh + pad;
    if (text_y - pad - y >= 24)
        preview(g, ui, sqg_rect_make(pad, y, cv->w - 2 * pad, text_y - pad - y));
    else
        text_y = y;
    sqg_textbox(ui, sqg_rect_make(pad, text_y, cv->w - 2 * pad, status_y - pad - text_y),
                g->found ? g->text : "");
    sqg_label(ui, sqg_rect_make(pad, status_y, cv->w - 2 * pad, lh), g->status, SQG_ALIGN_LEFT);
    return action;
}
