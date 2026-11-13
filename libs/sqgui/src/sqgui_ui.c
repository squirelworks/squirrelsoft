/* libs/sqgui: immediate-mode widgets (original code). */
#include "sqgui.h"

#include <string.h>

sqg_id sqg_id_str(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 16777619u;
    }
    return h ? h : 1;
}

void sqg_ui_init(sqg_ui *ui, const sqg_theme *theme, int scale)
{
    memset(ui, 0, sizeof *ui);
    ui->theme = theme ? theme : &sqg_theme_warp;
    ui->scale = scale < 1 ? 1 : scale;
}

void sqg_begin(sqg_ui *ui, sqg_canvas *cv, const sqg_input *in)
{
    ui->cv = cv;
    ui->in = *in;
    ui->hot = 0;
    sqg_clip_reset(cv);
    /* a click on nothing takes the keyboard away */
    if (in->pressed)
        ui->focus = 0;
}

void sqg_end(sqg_ui *ui)
{
    if (!ui->in.down)
        ui->active = 0;
}

int sqg_line_h(const sqg_ui *ui)
{
    return SQG_GLYPH_H * ui->scale + 8;
}

int sqg_button_w(const sqg_ui *ui, const char *label)
{
    return sqg_text_width(label, ui->scale) + 8 + 2 * ui->scale + 4;
}

sqg_row sqg_row_begin(sqg_rect area, int gap)
{
    sqg_row r = { area, area.x, gap };
    return r;
}

sqg_rect sqg_row_take(sqg_row *row, int w)
{
    int right = row->area.x + row->area.w;
    if (w <= 0 || row->x + w > right)
        w = right - row->x;
    if (w < 0)
        w = 0;
    sqg_rect r = sqg_rect_make(row->x, row->area.y, w, row->area.h);
    row->x += w + row->gap;
    return r;
}

/* Track the pointer for a widget: sets hot, takes active on press.
 * Returns 1 when the widget is being pressed (active). */
static int track(sqg_ui *ui, sqg_id id, sqg_rect r)
{
    int over = sqg_rect_contains(r, ui->in.mx, ui->in.my) &&
               sqg_rect_contains(ui->cv->clip, ui->in.mx, ui->in.my);
    if (over)
        ui->hot = id;
    if (over && ui->in.pressed && !ui->active)
        ui->active = id;
    return ui->active == id;
}

static int clicked(sqg_ui *ui, sqg_id id, sqg_rect r)
{
    return ui->active == id && ui->in.released && sqg_rect_contains(r, ui->in.mx, ui->in.my);
}

static void text_in(sqg_ui *ui, sqg_rect r, const char *s, int align, uint32_t col)
{
    int tw = sqg_text_width(s, ui->scale), th = sqg_text_height(s, ui->scale) - 2 * ui->scale;
    int x = r.x + 4;
    if (align == SQG_ALIGN_CENTER)
        x = r.x + (r.w - tw) / 2;
    else if (align == SQG_ALIGN_RIGHT)
        x = r.x + r.w - tw - 4;
    int y = r.y + (r.h - th) / 2;
    sqg_rect old = sqg_clip(ui->cv, sqg_rect_intersect(r, ui->cv->clip));
    sqg_text(ui->cv, x, y, s, col, ui->scale);
    ui->cv->clip = old;
}

void sqg_label(sqg_ui *ui, sqg_rect r, const char *text, int align)
{
    text_in(ui, r, text, align, ui->theme->text);
}

static void button_face(sqg_ui *ui, sqg_rect r, const char *label, int down)
{
    const sqg_theme *t = ui->theme;
    sqg_bevel(ui->cv, r, down, 1, t);
    sqg_rect inner = r;
    if (down) {
        inner.x += 1;
        inner.y += 1;
    }
    text_in(ui, inner, label, SQG_ALIGN_CENTER, t->text);
}

int sqg_button(sqg_ui *ui, sqg_id id, sqg_rect r, const char *label)
{
    int pressing = track(ui, id, r);
    int down = pressing && ui->in.down && sqg_rect_contains(r, ui->in.mx, ui->in.my);
    int hit = clicked(ui, id, r);
    button_face(ui, r, label, down);
    return hit;
}

int sqg_toggle(sqg_ui *ui, sqg_id id, sqg_rect r, const char *label, int *on)
{
    track(ui, id, r);
    int hit = clicked(ui, id, r);
    if (hit)
        *on = !*on;
    button_face(ui, r, label, *on);
    return hit;
}

int sqg_checkbox(sqg_ui *ui, sqg_id id, sqg_rect r, const char *label, int *on)
{
    const sqg_theme *t = ui->theme;
    track(ui, id, r);
    int hit = clicked(ui, id, r);
    if (hit)
        *on = !*on;
    int box = SQG_GLYPH_H * ui->scale;
    if (box > r.h)
        box = r.h;
    sqg_rect b = sqg_rect_make(r.x + 2, r.y + (r.h - box) / 2, box, box);
    sqg_bevel(ui->cv, b, 1, 0, t);
    sqg_fill(ui->cv, sqg_rect_inset(b, 2), t->window);
    if (*on) {
        sqg_rect m = sqg_rect_inset(b, 2 + box / 6);
        sqg_line(ui->cv, m.x, m.y + m.h / 2, m.x + m.w / 3, m.y + m.h - 1, t->window_text);
        sqg_line(ui->cv, m.x + m.w / 3, m.y + m.h - 1, m.x + m.w - 1, m.y, t->window_text);
        sqg_line(ui->cv, m.x + 1, m.y + m.h / 2, m.x + m.w / 3 + 1, m.y + m.h - 1, t->window_text);
    }
    sqg_rect lr = sqg_rect_make(b.x + box + 2, r.y, r.w - box - 4, r.h);
    text_in(ui, lr, label, SQG_ALIGN_LEFT, t->text);
    return hit;
}

int sqg_slider(sqg_ui *ui, sqg_id id, sqg_rect r, double *v, double lo, double hi)
{
    const sqg_theme *t = ui->theme;
    double old = *v;
    int knob = 4 * ui->scale + 4;
    int travel = r.w - knob;
    if (travel < 1)
        travel = 1;
    int over = sqg_rect_contains(r, ui->in.mx, ui->in.my);
    if (track(ui, id, r) && ui->in.down) {
        double f = (double)(ui->in.mx - r.x - knob / 2) / travel;
        if (f < 0)
            f = 0;
        if (f > 1)
            f = 1;
        *v = lo + f * (hi - lo);
    } else if (over && ui->in.wheel && (!ui->active || ui->active == id)) {
        /* not while another widget holds the button (a drag elsewhere) */
        *v += ui->in.wheel * (hi - lo) / 20.0;
    }
    if (lo < hi) {
        if (*v < lo)
            *v = lo;
        if (*v > hi)
            *v = hi;
    }
    double f = hi != lo ? (*v - lo) / (hi - lo) : 0;
    sqg_rect groove = sqg_rect_make(r.x, r.y + r.h / 2 - 3, r.w, 6);
    sqg_bevel(ui->cv, groove, 1, 0, t);
    sqg_fill(ui->cv, sqg_rect_inset(groove, 2), t->accent);
    sqg_rect k = sqg_rect_make(r.x + (int)(f * travel + 0.5), r.y + 1, knob, r.h - 2);
    sqg_bevel(ui->cv, k, 0, 1, t);
    return *v != old;
}

int sqg_segments(sqg_ui *ui, sqg_id id, sqg_rect r, const char *const *labels, int n, int *sel)
{
    if (n <= 0)
        return 0;
    int changed = 0;
    for (int i = 0; i < n; i++) {
        int x0 = r.x + r.w * i / n, x1 = r.x + r.w * (i + 1) / n;
        sqg_rect s = sqg_rect_make(x0, r.y, x1 - x0, r.h);
        int on = *sel == i;
        if (sqg_toggle(ui, id + 1 + (sqg_id)i, s, labels[i], &on) && *sel != i) {
            *sel = i;
            changed = 1;
        }
    }
    /* redraw so the selection shows on the frame it changed */
    if (changed)
        for (int i = 0; i < n; i++) {
            int x0 = r.x + r.w * i / n, x1 = r.x + r.w * (i + 1) / n;
            button_face(ui, sqg_rect_make(x0, r.y, x1 - x0, r.h), labels[i], *sel == i);
        }
    return changed;
}

int sqg_list(sqg_ui *ui, sqg_id id, sqg_rect r, const char *const *rows, int n, int *sel, int *top)
{
    const sqg_theme *t = ui->theme;
    int changed = 0;
    int row_h = SQG_GLYPH_H * ui->scale + 2;
    sqg_rect inner = sqg_rect_inset(r, 2);
    int visible = inner.h / row_h;
    if (visible < 1)
        visible = 1;
    int over = sqg_rect_contains(r, ui->in.mx, ui->in.my);
    track(ui, id, r);
    if (over && ui->in.wheel)
        *top -= ui->in.wheel * 3;
    if (ui->in.pressed && over) {
        int i = *top + (ui->in.my - inner.y) / row_h;
        if (i >= 0 && i < n && i != *sel) {
            *sel = i;
            changed = 1;
        }
        ui->focus = id;
    }
    if (ui->focus == id && n > 0) {
        int s = *sel;
        if (ui->in.key == SQG_KEY_DOWN && s < n - 1)
            s++;
        if (ui->in.key == SQG_KEY_UP && s > 0)
            s--;
        if (ui->in.key == SQG_KEY_HOME)
            s = 0;
        if (ui->in.key == SQG_KEY_END)
            s = n - 1;
        if (s != *sel) {
            *sel = s;
            changed = 1;
            if (s < *top)
                *top = s;
            if (s >= *top + visible)
                *top = s - visible + 1;
        }
    }
    if (*top > n - visible)
        *top = n - visible;
    if (*top < 0)
        *top = 0;

    sqg_bevel(ui->cv, r, 1, 0, t);
    sqg_fill(ui->cv, inner, t->window);
    sqg_rect old = sqg_clip(ui->cv, sqg_rect_intersect(inner, ui->cv->clip));
    for (int i = *top; i < n && i < *top + visible + 1; i++) {
        int y = inner.y + (i - *top) * row_h;
        uint32_t fg = t->window_text;
        if (i == *sel) {
            sqg_fill(ui->cv, sqg_rect_make(inner.x, y, inner.w, row_h), t->select);
            fg = t->select_text;
        }
        sqg_text(ui->cv, inner.x + 3, y + 1, rows[i], fg, ui->scale);
    }
    ui->cv->clip = old;
    return changed;
}

/* UTF-8 helpers for the text field: the buffer holds UTF-8, ui->cursor
 * is a byte offset that always sits on a character boundary. */
static int utf8_len(const unsigned char *p)
{
    unsigned c = p[0];
    int n;
    if (c < 0x80)
        return 1;
    if (c >= 0xC2 && c <= 0xDF)
        n = 2;
    else if (c >= 0xE0 && c <= 0xEF)
        n = 3;
    else if (c >= 0xF0 && c <= 0xF4)
        n = 4;
    else
        return 0;
    for (int i = 1; i < n; i++)
        if ((p[i] & 0xC0) != 0x80)
            return 0;
    /* no overlongs, surrogates or code points past U+10FFFF */
    if ((c == 0xE0 && p[1] < 0xA0) || (c == 0xED && p[1] > 0x9F) ||
        (c == 0xF0 && p[1] < 0x90) || (c == 0xF4 && p[1] > 0x8F))
        return 0;
    return n;
}

static int prev_char(const char *buf, int i)
{
    if (i > 0)
        i--;
    while (i > 0 && ((unsigned char)buf[i] & 0xC0) == 0x80)
        i--;
    return i;
}

static int next_char(const char *buf, int i, int len)
{
    if (i < len)
        i++;
    while (i < len && ((unsigned char)buf[i] & 0xC0) == 0x80)
        i++;
    return i;
}

static int chars_before(const char *buf, int i)
{
    int n = 0;
    for (int k = 0; k < i; k++)
        n += ((unsigned char)buf[k] & 0xC0) != 0x80;
    return n;
}

static int byte_of_char(const char *buf, int len, int ch)
{
    int i = 0;
    while (ch-- > 0 && i < len)
        i = next_char(buf, i, len);
    return i;
}

int sqg_textfield(sqg_ui *ui, sqg_id id, sqg_rect r, char *buf, size_t cap)
{
    const sqg_theme *t = ui->theme;
    int enter = 0;
    int len = (int)strlen(buf);
    int cw = SQG_GLYPH_W * ui->scale;
    if (ui->in.pressed && sqg_rect_contains(r, ui->in.mx, ui->in.my)) {
        ui->focus = id;
        int c = (ui->in.mx - r.x - 4 + cw / 2) / cw;
        ui->cursor = byte_of_char(buf, len, c < 0 ? 0 : c);
    }
    if (ui->focus == id) {
        if (ui->cursor > len)
            ui->cursor = len;
        while (ui->cursor > 0 && ((unsigned char)buf[ui->cursor] & 0xC0) == 0x80)
            ui->cursor--;
        const unsigned char *p = (const unsigned char *)ui->in.text;
        while (*p) {
            int n = utf8_len(p);
            if (n == 0) {           /* stray byte: drop it */
                p++;
                continue;
            }
            if ((n == 1 && (*p < 0x20 || *p == 0x7F)) || (size_t)(len + n) >= cap) {
                p += n;
                continue;
            }
            memmove(buf + ui->cursor + n, buf + ui->cursor, (size_t)(len - ui->cursor + 1));
            memcpy(buf + ui->cursor, p, (size_t)n);
            ui->cursor += n;
            len += n;
            p += n;
        }
        int j;
        switch (ui->in.key) {
        case SQG_KEY_BACKSPACE:
            if (ui->cursor > 0) {
                j = prev_char(buf, ui->cursor);
                memmove(buf + j, buf + ui->cursor, (size_t)(len - ui->cursor + 1));
                len -= ui->cursor - j;
                ui->cursor = j;
            }
            break;
        case SQG_KEY_DELETE:
            if (ui->cursor < len) {
                j = next_char(buf, ui->cursor, len);
                memmove(buf + ui->cursor, buf + j, (size_t)(len - j + 1));
                len -= j - ui->cursor;
            }
            break;
        case SQG_KEY_LEFT:
            ui->cursor = prev_char(buf, ui->cursor);
            break;
        case SQG_KEY_RIGHT:
            ui->cursor = next_char(buf, ui->cursor, len);
            break;
        case SQG_KEY_HOME:
            ui->cursor = 0;
            break;
        case SQG_KEY_END:
            ui->cursor = len;
            break;
        case SQG_KEY_ENTER:
            enter = 1;
            break;
        default:
            break;
        }
    }
    sqg_bevel(ui->cv, r, 1, 0, t);
    sqg_rect inner = sqg_rect_inset(r, 2);
    sqg_fill(ui->cv, inner, t->window);
    sqg_rect old = sqg_clip(ui->cv, sqg_rect_intersect(inner, ui->cv->clip));
    int ty = r.y + (r.h - 7 * ui->scale) / 2;
    /* keep the cursor in view; positions count characters, not bytes */
    int at = ui->focus == id ? chars_before(buf, ui->cursor) : 0;
    int shift = 0, fit = (inner.w - 4) / cw;
    if (ui->focus == id && at > fit)
        shift = at - fit;
    sqg_text(ui->cv, r.x + 4 - shift * cw, ty, buf, t->window_text, ui->scale);
    if (ui->focus == id) {
        int cx = r.x + 4 + (at - shift) * cw - 1;
        sqg_vline(ui->cv, cx, ty - 1, ty + 7 * ui->scale, t->window_text);
    }
    ui->cv->clip = old;
    return enter;
}

int sqg_textbox(sqg_ui *ui, sqg_rect r, const char *text)
{
    const sqg_theme *t = ui->theme;
    sqg_bevel(ui->cv, r, 1, 0, t);
    sqg_rect inner = sqg_rect_inset(r, 2);
    sqg_fill(ui->cv, inner, t->window);
    sqg_rect old = sqg_clip(ui->cv, sqg_rect_intersect(inner, ui->cv->clip));
    int cw = SQG_GLYPH_W * ui->scale, lh = SQG_GLYPH_H * ui->scale;
    int cols = (inner.w - 6) / cw;
    if (cols < 1)
        cols = 1;
    int x = 0, line = 0;
    char one[2] = { 0, 0 };
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if ((*p & 0xC0) == 0x80)
            continue;
        if (*p == '\n' || x >= cols) {
            line++;
            x = 0;
            if (*p == '\n')
                continue;
        }
        one[0] = (char)(*p >= 0x80 ? '?' : *p);
        sqg_text(ui->cv, inner.x + 3 + x * cw, inner.y + 3 + line * lh, one, t->window_text, ui->scale);
        x++;
    }
    ui->cv->clip = old;
    return line + 1;
}
