/* paintgui: the SquirrelPaint window panel (see paintgui.h). Original code. */
#include "paintgui.h"

#include <stdio.h>
#include <string.h>

static const double ZOOMS[] = {0.125, 0.25, 0.5, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32};
#define NZOOM ((int)(sizeof ZOOMS / sizeof ZOOMS[0]))
#define ZOOM_1 3 /* 1:1 */

static const char *const TOOL_NAMES[PAINT_TOOLS] = {"Pencil", "Brush",  "Eraser", "Line",  "Rect",
                                                    "Ellipse", "Fill", "Picker", "Select", "Spray",
                                                    "Polygon", "Curve", "Text"};
static const char TOOL_KEYS[PAINT_TOOLS] = {'p', 'b', 'e', 'l', 'r', 'o', 'f', 'i', 's', 'a', 'g', 'c', 't'};

double paint_gui_scale(int k) { return ZOOMS[k < 0 ? 0 : k >= NZOOM ? NZOOM - 1 : k]; }
int paint_gui_zooms(void) { return NZOOM; }

void paint_gui_init(PaintGui *g, PaintDoc *doc) {
    memset(g, 0, sizeof *g);
    g->doc = doc;
    g->zoom = ZOOM_1;
    snprintf(g->path, sizeof g->path, "untitled.png");
    snprintf(g->new_w, sizeof g->new_w, "%d", doc->w > 0 ? doc->w : 640);
    snprintf(g->new_h, sizeof g->new_h, "%d", doc->h > 0 ? doc->h : 480);
    static const uint32_t pal[PAINT_PALETTE] = {
        0xFF000000u, 0xFFFFFFFFu, 0xFF808080u, 0xFFC0C0C0u, 0xFF800000u, 0xFFFF0000u, 0xFF808000u,
        0xFFFFFF00u, 0xFF008000u, 0xFF00FF00u, 0xFF008080u, 0xFF00FFFFu, 0xFF000080u, 0xFF0000FFu,
        0xFF800080u, 0xFFFF00FFu, 0xFF804000u, 0xFFFF8040u, 0xFF004040u, 0xFF80FF80u, 0xFF0080FFu,
        0xFF80C0FFu, 0xFF4000C0u, 0xFF8080FFu, 0xFFC00040u, 0xFFFF80C0u, 0xFF404040u, 0xFFFFE0C0u};
    memcpy(g->palette, pal, sizeof pal);
    snprintf(g->status, sizeof g->status, "Draw with the left button in the first colour, the right in the second");
}

static int ifloor(double v) {
    int i = (int)v;
    return v < i ? i - 1 : i;
}

void paint_gui_to_picture(const PaintGui *g, int sx, int sy, int *x, int *y) {
    double s = paint_gui_scale(g->zoom);
    *x = ifloor(g->ox + (sx - g->area.x) / s);
    *y = ifloor(g->oy + (sy - g->area.y) / s);
}

void paint_gui_to_screen(const PaintGui *g, double x, double y, int *sx, int *sy) {
    double s = paint_gui_scale(g->zoom);
    double a = g->area.x + (x - g->ox) * s, b = g->area.y + (y - g->oy) * s;
    *sx = (int)(a < 0 ? a - 0.5 : a + 0.5);
    *sy = (int)(b < 0 ? b - 0.5 : b + 0.5);
}

/* keep some of the picture in view */
static void clamp_view(PaintGui *g) {
    double s = paint_gui_scale(g->zoom);
    double vw = g->area.w / s, vh = g->area.h / s, w = g->doc->w, h = g->doc->h;
    /* a picture smaller than the area sits in its middle */
    if (w <= vw) g->ox = (w - vw) / 2;
    else if (g->ox < -vw / 2) g->ox = -vw / 2;
    else if (g->ox > w - vw / 2) g->ox = w - vw / 2;
    if (h <= vh) g->oy = (h - vh) / 2;
    else if (g->oy < -vh / 2) g->oy = -vh / 2;
    else if (g->oy > h - vh / 2) g->oy = h - vh / 2;
}

void paint_gui_fit(PaintGui *g) {
    int k = 0;
    for (int i = 0; i < NZOOM; i++)
        if (g->doc->w * ZOOMS[i] <= g->area.w - 8 && g->doc->h * ZOOMS[i] <= g->area.h - 8 && (i <= ZOOM_1 || g->doc->w * ZOOMS[i] <= 512))
            k = i;
    g->zoom = k;
    g->ox = g->oy = 0;
    clamp_view(g);
    g->view_ready = 1;
}

void paint_gui_zoom(PaintGui *g, int k, int sx, int sy) {
    if (k < 0) k = 0;
    if (k >= NZOOM) k = NZOOM - 1;
    double s0 = paint_gui_scale(g->zoom), s1 = paint_gui_scale(k);
    double px = g->ox + (sx - g->area.x) / s0, py = g->oy + (sy - g->area.y) / s0;
    g->zoom = k;
    g->ox = px - (sx - g->area.x) / s1;
    g->oy = py - (sy - g->area.y) / s1;
    clamp_view(g);
}

static void say(PaintGui *g, const char *s) { snprintf(g->status, sizeof g->status, "%s", s); }

/* ---- the picture ---- */

static void draw_picture(PaintGui *g, sqg_ui *ui) {
    sqg_canvas *cv = ui->cv;
    sqg_rect a = g->area;
    sqg_fill(cv, a, 0xFF707478u);
    const PaintDoc *d = g->doc;
    double s = paint_gui_scale(g->zoom);
    int x0, y0, x1, y1;
    paint_gui_to_screen(g, 0, 0, &x0, &y0);
    paint_gui_to_screen(g, d->w, d->h, &x1, &y1);
    sqg_rect pic = sqg_rect_intersect(sqg_rect_make(x0, y0, x1 - x0, y1 - y0), a);
    sqg_rect old = sqg_clip(cv, a);
    sqg_frame(cv, sqg_rect_make(x0 - 1, y0 - 1, x1 - x0 + 2, y1 - y0 + 2), 0xFF303030u);
    /* each window pixel takes the picture pixel under its middle */
    for (int sy = pic.y; sy < pic.y + pic.h; sy++) {
        int py = ifloor(g->oy + (sy - a.y + 0.5) / s);
        if (py < 0) py = 0;
        if (py >= d->h) py = d->h - 1;
        const uint32_t *row = d->px + (size_t)py * (size_t)d->w;
        uint32_t *out = cv->px + (size_t)sy * (size_t)cv->stride;
        int floats = d->floating && py >= d->sel.y && py < d->sel.y + d->sel.h;
        for (int sx = pic.x; sx < pic.x + pic.w; sx++) {
            int px = ifloor(g->ox + (sx - a.x + 0.5) / s);
            if (px < 0) px = 0;
            if (px >= d->w) px = d->w - 1;
            out[sx] = floats ? paint_shown(d, px, py) : row[px];
        }
    }
    /* the selection: a dashed edge, black and white so it shows on anything */
    if (d->sel.w) {
        int ax, ay, bx, by;
        paint_gui_to_screen(g, d->sel.x, d->sel.y, &ax, &ay);
        paint_gui_to_screen(g, d->sel.x + d->sel.w, d->sel.y + d->sel.h, &bx, &by);
        ax--, ay--;
        for (int x = ax; x <= bx; x++) {
            uint32_t c = ((x - ax) / 4) & 1 ? 0xFFFFFFFFu : 0xFF000000u;
            sqg_pixel(cv, x, ay, c);
            sqg_pixel(cv, x, by, c);
        }
        for (int y = ay; y <= by; y++) {
            uint32_t c = ((y - ay) / 4) & 1 ? 0xFFFFFFFFu : 0xFF000000u;
            sqg_pixel(cv, ax, y, c);
            sqg_pixel(cv, bx, y, c);
        }
    }
    if (s >= 8) { /* the pixel grid, faintly */
        int first = (int)g->ox, last = (int)(g->ox + a.w / s) + 1;
        for (int x = first < 0 ? 0 : first; x <= last && x <= d->w; x++) {
            int sx, sy;
            paint_gui_to_screen(g, x, 0, &sx, &sy);
            for (int y = pic.y; y < pic.y + pic.h; y++)
                if (sx >= pic.x && sx < pic.x + pic.w) sqg_blend(cv, sqg_rect_make(sx, y, 1, 1), 0x30808080u);
        }
        first = (int)g->oy;
        last = (int)(g->oy + a.h / s) + 1;
        for (int y = first < 0 ? 0 : first; y <= last && y <= d->h; y++) {
            int sx, sy;
            paint_gui_to_screen(g, 0, y, &sx, &sy);
            if (sy >= pic.y && sy < pic.y + pic.h) sqg_blend(cv, sqg_rect_make(pic.x, sy, pic.w, 1), 0x30808080u);
        }
    }
    /* the text cursor */
    int tx, ty, th;
    if (!paint_text_caret(d, &tx, &ty, &th)) {
        int sx0, sy0, sx1, sy1;
        paint_gui_to_screen(g, tx, ty, &sx0, &sy0);
        paint_gui_to_screen(g, tx, ty + th, &sx1, &sy1);
        sqg_fill(cv, sqg_rect_make(sx0 - 1, sy0, 2, sy1 - sy0 > 1 ? sy1 - sy0 : 1), 0xFF000000u);
        sqg_fill(cv, sqg_rect_make(sx0 + 1, sy0, 1, sy1 - sy0 > 1 ? sy1 - sy0 : 1), 0xFFFFFFFFu);
    }
    /* the brush's reach under the pointer */
    if (g->mouse_in && d->tool != PAINT_FILL && d->tool != PAINT_PICKER && d->tool != PAINT_SELECT &&
        d->tool != PAINT_TEXT) {
        int size = d->tool == PAINT_PENCIL ? 1 : d->size;
        int bx = g->mouse_x - (size - 1) / 2, by = g->mouse_y - (size - 1) / 2, sx0, sy0, sx1, sy1;
        paint_gui_to_screen(g, bx, by, &sx0, &sy0);
        paint_gui_to_screen(g, bx + size, by + size, &sx1, &sy1);
        sqg_frame(cv, sqg_rect_make(sx0 - 1, sy0 - 1, sx1 - sx0 + 2, sy1 - sy0 + 2), 0xC0FFFFFFu);
        sqg_frame(cv, sqg_rect_make(sx0 - 2, sy0 - 2, sx1 - sx0 + 4, sy1 - sy0 + 4), 0xC0000000u);
    }
    cv->clip = old;
}

/* the pointer on the picture: painting, panning, zooming */
static void picture_input(PaintGui *g, sqg_ui *ui) {
    const sqg_input *in = &ui->in;
    PaintDoc *d = g->doc;
    int over = sqg_rect_contains(g->area, in->mx, in->my) && !ui->active;
    int x, y;
    paint_gui_to_picture(g, in->mx, in->my, &x, &y);
    g->mouse_in = over || g->painting || g->panning;
    g->mouse_x = x;
    g->mouse_y = y;
    if (over && in->wheel) paint_gui_zoom(g, g->zoom + (in->wheel > 0 ? 1 : -1), in->mx, in->my);
    if (g->panning) {
        double s = paint_gui_scale(g->zoom);
        g->ox -= (in->mx - g->pan_mx) / s;
        g->oy -= (in->my - g->pan_my) / s;
        g->pan_mx = in->mx;
        g->pan_my = in->my;
        clamp_view(g);
        if (!in->down) g->panning = 0;
        return;
    }
    if (!g->painting && over && (in->pressed || in->pressed2)) {
        if (in->pressed && in->ctrl) {
            g->panning = 1;
            g->pan_mx = in->mx;
            g->pan_my = in->my;
            return;
        }
        int b = in->pressed ? 1 : 2;
        paint_press(d, x, y, b, in->shift);
        if (d->active || d->sel_mode) g->painting = b;
        return;
    }
    if (g->painting) {
        int held = g->painting == 1 ? in->down : in->down2;
        if (held) {
            paint_drag(d, x, y, in->shift);
            paint_tick(d); /* the spray keeps going while the button is held */
        } else {
            paint_release(d, x, y, in->shift);
            g->painting = 0;
        }
    }
}

/* ---- the panels ---- */

static void swatch(sqg_canvas *cv, sqg_rect r, uint32_t c) {
    sqg_fill(cv, r, c);
    sqg_frame(cv, r, 0xFF000000u);
    sqg_frame(cv, sqg_rect_inset(r, 1), 0xFFFFFFFFu);
}

static void edit(PaintGui *g, int what);
static void image_op(PaintGui *g, int what);

static void tools_panel(PaintGui *g, sqg_ui *ui, sqg_rect r) {
    sqg_canvas *cv = ui->cv;
    PaintDoc *d = g->doc;
    int lh = sqg_line_h(ui), y = r.y, half = (r.w - 4) / 2;
    for (int k = 0; k < PAINT_TOOLS; k++) {
        char id[16];
        snprintf(id, sizeof id, "tool%d", k);
        sqg_rect b = sqg_rect_make(r.x + (k % 2) * (half + 4), y + (k / 2) * (lh + 2), half, lh);
        int on = d->tool == k;
        if (sqg_toggle(ui, sqg_id_str(id), b, TOOL_NAMES[k], &on)) {
            paint_set_tool(d, k);
            g->painting = 0;
        }
    }
    y += (PAINT_TOOLS + 1) / 2 * (lh + 2) + 4;
    /* the selection and the picture: three buttons a row */
    static const char *const ops[] = {"Cut", "Copy", "Paste", "Delete", "Crop", "Invert", "Flip H", "Flip V", "Rot L", "Rot R"};
    static const char keys_of[] = {'x', 'c', 'v', 'd', 'C', 'i', 'h', 'V', 'l', 'r'};
    int third = (r.w - 8) / 3;
    for (int k = 0; k < 10; k++) {
        char id[16];
        snprintf(id, sizeof id, "op%d", k);
        int col = k < 6 ? k % 3 : (k - 6) % 2, row = k < 6 ? k / 3 : 2 + (k - 6) / 2;
        int bw = k < 6 ? third : (r.w - 4) / 2;
        sqg_rect b = sqg_rect_make(r.x + col * (bw + 4), y + row * (lh + 2), bw, lh);
        if (sqg_button(ui, sqg_id_str(id), b, ops[k]) && !g->painting) {
            int c = keys_of[k];
            if (c == 'x' || c == 'c' || c == 'v' || c == 'd') edit(g, c);
            else image_op(g, c == 'C' ? 'c' : c == 'V' ? 'v' : c);
        }
    }
    y += 4 * (lh + 2) + 6;
    char t[32];
    snprintf(t, sizeof t, "Size %d", d->size);
    sqg_label(ui, sqg_rect_make(r.x, y, r.w, lh), t, SQG_ALIGN_LEFT);
    y += lh;
    g->size_v = d->size;
    if (sqg_slider(ui, sqg_id_str("size"), sqg_rect_make(r.x, y, r.w, lh), &g->size_v, 1, PAINT_SIZE_MAX))
        d->size = (int)(g->size_v + 0.5);
    y += lh + 4;
    snprintf(t, sizeof t, "Fill tolerance %d", d->tolerance);
    sqg_label(ui, sqg_rect_make(r.x, y, r.w, lh), t, SQG_ALIGN_LEFT);
    y += lh;
    g->tol_v = d->tolerance;
    if (sqg_slider(ui, sqg_id_str("tol"), sqg_rect_make(r.x, y, r.w, lh), &g->tol_v, 0, 255))
        d->tolerance = (int)(g->tol_v + 0.5);
    y += lh + 4;
    sqg_checkbox(ui, sqg_id_str("filled"), sqg_rect_make(r.x, y, r.w, lh), "Filled shapes", &d->filled);
    y += lh + 8;
    /* the two colours: the first over the second */
    swatch(cv, sqg_rect_make(r.x + 18, y + 14, 30, 30), d->bg);
    swatch(cv, sqg_rect_make(r.x, y, 30, 30), d->fg);
    if (sqg_button(ui, sqg_id_str("swap"), sqg_rect_make(r.x + 56, y + 8, r.w - 56, lh), "Swap")) {
        uint32_t c = d->fg;
        d->fg = d->bg;
        d->bg = c;
    }
    y += 50;
    /* the first colour as RRGGBB to type, and its red, green and blue */
    sqg_id hex_id = sqg_id_str("hex");
    if (ui->focus != hex_id) snprintf(g->hex, sizeof g->hex, "%06X", (unsigned)(d->fg & 0xFFFFFF));
    sqg_label(ui, sqg_rect_make(r.x, y, 10, lh), "#", SQG_ALIGN_LEFT);
    if (sqg_textfield(ui, hex_id, sqg_rect_make(r.x + 10, y, r.w - 10, lh), g->hex, sizeof g->hex)) {
        uint32_t c;
        if (paint_parse_color(g->hex, strlen(g->hex), &c)) say(g, "A colour is six hex digits, as FF8000");
        else d->fg = c;
    }
    y += lh + 2;
    static const char *const RGB[3] = {"R", "G", "B"};
    for (int k = 0; k < 3; k++) {
        char id[8];
        snprintf(id, sizeof id, "rgb%d", k);
        int shift = 16 - 8 * k;
        sqg_label(ui, sqg_rect_make(r.x, y, 10, lh), RGB[k], SQG_ALIGN_LEFT);
        g->rgb_v[k] = (d->fg >> shift) & 0xFF;
        if (sqg_slider(ui, sqg_id_str(id), sqg_rect_make(r.x + 10, y, r.w - 10, lh), &g->rgb_v[k], 0, 255)) {
            uint32_t v = (uint32_t)(g->rgb_v[k] + 0.5);
            d->fg = (d->fg & ~(0xFFu << shift)) | v << shift;
        }
        y += lh + 1;
    }
    y += 3;
    /* the palette: the left button takes the first colour, the right the
     * second; with Shift, the left button puts the first colour there */
    int cols = 4, cw = (r.w - (cols - 1) * 2) / cols;
    for (int k = 0; k < PAINT_PALETTE; k++) {
        sqg_rect c = sqg_rect_make(r.x + (k % cols) * (cw + 2), y + (k / cols) * (cw / 2 + 6), cw, cw / 2 + 4);
        swatch(cv, c, g->palette[k]);
        if (!ui->active && sqg_rect_contains(c, ui->in.mx, ui->in.my)) {
            if (ui->in.pressed && ui->in.shift) {
                g->palette[k] = d->fg;
                say(g, "The colour is in the palette");
            } else if (ui->in.pressed) {
                d->fg = g->palette[k];
            }
            if (ui->in.pressed2) d->bg = g->palette[k];
        }
    }
}

static int parse_int(const char *s, int *v) {
    int x = 0, n = 0;
    while (*s == ' ') s++;
    for (; *s >= '0' && *s <= '9' && n < 6; s++, n++) x = x * 10 + (*s - '0');
    while (*s == ' ') s++;
    if (!n || *s) return -1;
    *v = x;
    return 0;
}

static void do_open(PaintGui *g) {
    char msg[120];
    if (!g->open_file) {
        say(g, "Opening files is not here");
        return;
    }
    paint_cancel(g->doc);
    g->painting = 0;
    if (g->open_file(g, g->path, msg, sizeof msg)) {
        snprintf(g->status, sizeof g->status, "%.60s: %.90s", g->path, msg);
        return;
    }
    g->view_ready = 0;
    snprintf(g->status, sizeof g->status, "Opened %.80s, %d x %d", g->path, g->doc->w, g->doc->h);
}

static void do_save(PaintGui *g) {
    char msg[120];
    if (!g->save_file) {
        say(g, "Saving files is not here");
        return;
    }
    paint_finish(g->doc); /* what is under way, and what floats, go into the file */
    paint_drop(g->doc);
    if (g->save_file(g, g->path, msg, sizeof msg)) {
        snprintf(g->status, sizeof g->status, "%.60s: %.90s", g->path, msg);
        return;
    }
    g->doc->changed = 0;
    snprintf(g->status, sizeof g->status, "Saved %.120s", g->path);
}

/* x cut, c copy, v paste, a select all, d delete */
static void edit(PaintGui *g, int what) {
    PaintDoc *d = g->doc;
    int rc = 0;
    switch (what) {
    case 'x': rc = paint_cut(d); break;
    case 'c': rc = paint_copy(d); break;
    case 'd': rc = paint_delete(d); break;
    case 'a':
        paint_set_tool(d, PAINT_SELECT);
        paint_select_all(d);
        break;
    default: { /* paste at the top-left of what is in view, ready to drag */
        int x, y;
        paint_gui_to_picture(g, g->area.x, g->area.y, &x, &y);
        rc = paint_paste(d, x, y);
        if (!rc) paint_set_tool(d, PAINT_SELECT);
    }
    }
    if (rc) say(g, what == 'v' ? "Nothing to paste" : "Select something first (the Select tool, or Ctrl+A)");
    else say(g, what == 'x' ? "Cut" : what == 'c' ? "Copied" : what == 'd' ? "Deleted" : what == 'a' ? "All selected" : "Pasted: drag it into place");
}

/* the image buttons: on the selection, or the whole picture */
static void image_op(PaintGui *g, int what) {
    PaintDoc *d = g->doc;
    int rc, whole = !d->sel.w;
    switch (what) {
    case 'h': rc = paint_flip(d, 0); break;
    case 'v': rc = paint_flip(d, 1); break;
    case 'l': rc = paint_rotate(d, -1); break;
    case 'r': rc = paint_rotate(d, 1); break;
    case 'i': rc = paint_invert(d); break;
    default: rc = paint_crop(d); whole = 1;
    }
    if (rc) say(g, what == 'c' ? "Select what to keep first" : "That would be too big");
    else if (whole) g->view_ready = what == 'l' || what == 'r' || what == 'c' ? 0 : g->view_ready; /* a new shape: fit it */
}

static void do_undo(PaintGui *g, int redo) {
    if (g->painting) return;
    if ((redo ? paint_redo(g->doc) : paint_undo(g->doc)) < 0) say(g, redo ? "Nothing to redo" : "Nothing to undo");
    else say(g, redo ? "Redone" : "Undone");
}

static void top_bar(PaintGui *g, sqg_ui *ui, sqg_rect r) {
    PaintDoc *d = g->doc;
    sqg_row row = sqg_row_begin(r, 4);
    if (sqg_textfield(ui, sqg_id_str("path"), sqg_row_take(&row, 180), g->path, sizeof g->path)) do_open(g);
    if (sqg_button(ui, sqg_id_str("open"), sqg_row_take(&row, sqg_button_w(ui, "Open")), "Open")) do_open(g);
    if (sqg_button(ui, sqg_id_str("save"), sqg_row_take(&row, sqg_button_w(ui, "Save")), "Save")) do_save(g);
    sqg_row_take(&row, 4);
    sqg_textfield(ui, sqg_id_str("neww"), sqg_row_take(&row, 40), g->new_w, sizeof g->new_w);
    sqg_label(ui, sqg_row_take(&row, sqg_text_width("x", 1)), "x", SQG_ALIGN_CENTER);
    sqg_textfield(ui, sqg_id_str("newh"), sqg_row_take(&row, 40), g->new_h, sizeof g->new_h);
    if (sqg_button(ui, sqg_id_str("resize"), sqg_row_take(&row, sqg_button_w(ui, "Resize")), "Resize")) {
        int w, h, sel = d->sel.w > 0;
        if (g->painting || parse_int(g->new_w, &w) || parse_int(g->new_h, &h) || paint_resize(d, w, h, 1)) {
            snprintf(g->status, sizeof g->status, "Resize: a size from 1 x 1 to %d x %d", PAINT_MAX_W, PAINT_MAX_H);
        } else {
            if (!sel) g->view_ready = 0;
            snprintf(g->status, sizeof g->status, "%s now %d x %d", sel ? "The selection is" : "The picture is", w, h);
        }
    }
    if (sqg_button(ui, sqg_id_str("new"), sqg_row_take(&row, sqg_button_w(ui, "New")), "New")) {
        int w, h;
        paint_cancel(d);
        g->painting = 0;
        uint32_t fg = d->fg, bg = d->bg;
        int tool = d->tool, size = d->size, filled = d->filled, tol = d->tolerance;
        if (parse_int(g->new_w, &w) || parse_int(g->new_h, &h) || paint_new(d, w, h)) {
            snprintf(g->status, sizeof g->status, "New: a size from 1 x 1 to %d x %d", PAINT_MAX_W, PAINT_MAX_H);
        } else {
            d->fg = fg; /* the tools stay as they were */
            d->bg = bg;
            d->tool = tool;
            d->size = size;
            d->filled = filled;
            d->tolerance = tol;
            g->view_ready = 0;
            snprintf(g->status, sizeof g->status, "A new %d x %d picture", w, h);
        }
    }
    sqg_row_take(&row, 4);
    if (sqg_button(ui, sqg_id_str("undo"), sqg_row_take(&row, sqg_button_w(ui, "Undo")), "Undo")) do_undo(g, 0);
    if (sqg_button(ui, sqg_id_str("redo"), sqg_row_take(&row, sqg_button_w(ui, "Redo")), "Redo")) do_undo(g, 1);
    sqg_row_take(&row, 4);
    int cx = g->area.x + g->area.w / 2, cy = g->area.y + g->area.h / 2;
    if (sqg_button(ui, sqg_id_str("zout"), sqg_row_take(&row, sqg_button_w(ui, "-")), "-"))
        paint_gui_zoom(g, g->zoom - 1, cx, cy);
    if (sqg_button(ui, sqg_id_str("zin"), sqg_row_take(&row, sqg_button_w(ui, "+")), "+"))
        paint_gui_zoom(g, g->zoom + 1, cx, cy);
    if (sqg_button(ui, sqg_id_str("z1"), sqg_row_take(&row, sqg_button_w(ui, "1:1")), "1:1"))
        paint_gui_zoom(g, ZOOM_1, cx, cy);
    if (sqg_button(ui, sqg_id_str("fit"), sqg_row_take(&row, sqg_button_w(ui, "Fit")), "Fit")) paint_gui_fit(g);
}

/* keys when no text field has them */
static void keys(PaintGui *g, sqg_ui *ui) {
    const sqg_input *in = &ui->in;
    PaintDoc *d = g->doc;
    if (in->ctrl_key == 'z') do_undo(g, 0);
    if (in->ctrl_key == 'y') do_undo(g, 1);
    if (in->ctrl_key == 's') do_save(g);
    if (in->ctrl_key && strchr("xcva", in->ctrl_key) && !g->painting) edit(g, in->ctrl_key);
    if (d->multi == PAINT_TEXT && !ui->focus && !in->ctrl) { /* typing goes into the text */
        for (const char *t = in->text; *t; t++) paint_text_key(d, (unsigned char)*t);
        if (in->key == SQG_KEY_BACKSPACE) paint_text_key(d, '\b');
        if (in->key == SQG_KEY_ENTER) paint_text_key(d, '\n');
        if (in->key == SQG_KEY_ESCAPE) {
            paint_cancel(d);
            say(g, "Taken back");
        }
        return;
    }
    if (in->key == SQG_KEY_ENTER && d->multi && !d->active && !ui->focus) {
        paint_finish(d); /* the polygon closed, the curve as it is */
        return;
    }
    if (in->key == SQG_KEY_ESCAPE) {
        if (d->active || d->sel_mode || d->multi) {
            paint_cancel(d);
            g->painting = 0;
            say(g, "Taken back");
        } else if (d->sel.w) {
            paint_select_none(d);
        }
    }
    if (ui->focus) return;
    if (in->key == SQG_KEY_DELETE && d->sel.w && !g->painting) edit(g, 'd');
    if (d->sel.w && !g->painting && in->key >= SQG_KEY_LEFT && in->key <= SQG_KEY_DOWN) { /* nudge it a pixel */
        paint_move_selection(d, (in->key == SQG_KEY_RIGHT) - (in->key == SQG_KEY_LEFT),
                             (in->key == SQG_KEY_DOWN) - (in->key == SQG_KEY_UP));
        return;
    }
    double step = 32 / paint_gui_scale(g->zoom);
    if (in->key == SQG_KEY_LEFT) g->ox -= step;
    if (in->key == SQG_KEY_RIGHT) g->ox += step;
    if (in->key == SQG_KEY_UP) g->oy -= step;
    if (in->key == SQG_KEY_DOWN) g->oy += step;
    if (in->key >= SQG_KEY_LEFT && in->key <= SQG_KEY_DOWN) clamp_view(g);
    if (in->text[0] && !in->text[1] && !in->ctrl && !g->painting)
        for (int k = 0; k < PAINT_TOOLS; k++)
            if (in->text[0] == TOOL_KEYS[k] || in->text[0] == TOOL_KEYS[k] - 32) paint_set_tool(d, k);
}

static void status_line(PaintGui *g, sqg_ui *ui, sqg_rect r) {
    const PaintDoc *d = g->doc;
    char pos[48] = "", zoom[24], t[260];
    if (g->mouse_in && g->mouse_x >= 0 && g->mouse_y >= 0 && g->mouse_x < d->w && g->mouse_y < d->h)
        snprintf(pos, sizeof pos, "%d, %d  #%06X", g->mouse_x, g->mouse_y,
                 (unsigned)(paint_get(d, g->mouse_x, g->mouse_y) & 0xFFFFFF));
    double s = paint_gui_scale(g->zoom);
    snprintf(zoom, sizeof zoom, "%d%%", (int)(s * 100 + 0.5));
    char sel[48] = "";
    if (d->sel.w) snprintf(sel, sizeof sel, "  selected %d x %d at %d, %d", d->sel.w, d->sel.h, d->sel.x, d->sel.y);
    snprintf(t, sizeof t, "%d x %d%s  %s  %s%s", d->w, d->h, d->changed ? " *" : "", zoom, pos, sel);
    sqg_row row = sqg_row_begin(r, 8);
    sqg_label(ui, sqg_row_take(&row, sqg_text_width(t, ui->scale) + 4), t, SQG_ALIGN_LEFT);
    sqg_label(ui, sqg_row_take(&row, 0), g->status, SQG_ALIGN_RIGHT);
}

void paint_gui_frame(PaintGui *g, sqg_ui *ui) {
    sqg_canvas *cv = ui->cv;
    int lh = sqg_line_h(ui), pad = 6, left = 132;
    sqg_fill(cv, sqg_rect_make(0, 0, cv->w, cv->h), ui->theme->face);
    int status_y = cv->h - pad - lh, top = pad + lh + pad;
    sqg_rect area = sqg_rect_make(pad + left + pad, top, cv->w - left - 3 * pad, status_y - pad - top);
    if (area.w < 16) area.w = 16;
    if (area.h < 16) area.h = 16;
    if (area.w != g->area.w || area.h != g->area.h) {
        g->area = area;
        if (g->view_ready) clamp_view(g);
    }
    g->area = area;
    if (!g->view_ready) paint_gui_fit(g);
    top_bar(g, ui, sqg_rect_make(pad, pad, cv->w - 2 * pad, lh));
    tools_panel(g, ui, sqg_rect_make(pad, top, left, status_y - pad - top));
    keys(g, ui);
    picture_input(g, ui);
    draw_picture(g, ui);
    status_line(g, ui, sqg_rect_make(pad, status_y, cv->w - 2 * pad, lh));
}
