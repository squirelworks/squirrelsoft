/* libs/sqgui: a tiny immediate-mode widget layer (original code).
 *
 * Everything draws into a plain 32-bit 0xAARRGGBB pixel buffer (a
 * canvas), so the same code runs in an SDL2 window on the desktop, in a
 * headless test, and later on Squirrel OS/2's wm/gfx surfaces, which use
 * the same pixel layout and the same 5x7 font (font/font5x7.txt).
 *
 * The core (sqgui_draw.c, sqgui_ui.c) is C11 with no OS calls and no
 * allocation. sqgui_sdl.c is an optional window back end.
 *
 * Immediate mode: every frame the program calls sqg_begin(), then one
 * function per widget with that frame's state, then sqg_end(). A widget
 * draws itself and returns what happened to it (clicked, changed). Only
 * the identity of the widget under the mouse button (`active`) and of
 * the one with the keyboard (`focus`) live between frames. */
#ifndef SQGUI_H
#define SQGUI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int x, y, w, h;
} sqg_rect;

typedef struct {
    uint32_t *px;   /* first pixel of row 0 */
    int w, h;
    int stride;     /* pixels per row */
    sqg_rect clip;  /* drawing is limited to this, always inside w x h */
} sqg_canvas;

/* Colours: 0xAARRGGBB, alpha ignored except by sqg_blend() */
#define SQG_RGB(r, g, b) (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* Font: 5x7 glyphs in a 6x9 cell (one column and two rows of spacing),
 * multiplied by an integer scale. */
#define SQG_GLYPH_W 6
#define SQG_GLYPH_H 9

/* Canvas and primitives (sqgui_draw.c) */
void sqg_canvas_init(sqg_canvas *c, uint32_t *px, int w, int h, int stride);
sqg_rect sqg_rect_make(int x, int y, int w, int h);
sqg_rect sqg_rect_intersect(sqg_rect a, sqg_rect b);
int sqg_rect_contains(sqg_rect r, int x, int y);
/* Shrink a rectangle by n pixels on every side */
sqg_rect sqg_rect_inset(sqg_rect r, int n);
/* Set the clip to r (intersected with the canvas); returns the old clip */
sqg_rect sqg_clip(sqg_canvas *c, sqg_rect r);
void sqg_clip_reset(sqg_canvas *c);

void sqg_pixel(sqg_canvas *c, int x, int y, uint32_t argb);
uint32_t sqg_get(const sqg_canvas *c, int x, int y);   /* 0 outside */
void sqg_fill(sqg_canvas *c, sqg_rect r, uint32_t argb);
void sqg_frame(sqg_canvas *c, sqg_rect r, uint32_t argb);
void sqg_hline(sqg_canvas *c, int x0, int x1, int y, uint32_t argb);
void sqg_vline(sqg_canvas *c, int x, int y0, int y1, uint32_t argb);
void sqg_line(sqg_canvas *c, int x0, int y0, int x1, int y1, uint32_t argb);
/* Mix argb over what is there using its alpha (0 = nothing, 255 = opaque) */
void sqg_blend(sqg_canvas *c, sqg_rect r, uint32_t argb);
/* Copy a w x h image (stride in pixels) with its top left at x, y */
void sqg_blit(sqg_canvas *c, int x, int y, const uint32_t *src, int w, int h, int stride);
/* Copy src scaled to fill dst (nearest neighbour) */
void sqg_blit_scaled(sqg_canvas *c, sqg_rect dst, const uint32_t *src, int w, int h, int stride);

/* Text: ASCII 0x20-0x7E; other bytes draw as '?'. '\n' starts a new
 * line. Returns the width drawn of the widest line. */
int sqg_text(sqg_canvas *c, int x, int y, const char *s, uint32_t argb, int scale);
int sqg_text_width(const char *s, int scale);
int sqg_text_height(const char *s, int scale);

/* Theme: a Warp-like grey 3D look by default */
typedef struct {
    uint32_t face, light, shadow, dark, text, text_disabled;
    uint32_t window, window_text, select, select_text, accent;
} sqg_theme;
extern const sqg_theme sqg_theme_warp;

/* Raised or sunken 2-pixel bevel around r, filled with the face colour
 * when fill is set */
void sqg_bevel(sqg_canvas *c, sqg_rect r, int sunken, int fill, const sqg_theme *t);

/* Input for one frame */
enum {
    SQG_KEY_NONE = 0,
    SQG_KEY_ENTER = 1,
    SQG_KEY_BACKSPACE,
    SQG_KEY_DELETE,
    SQG_KEY_LEFT,
    SQG_KEY_RIGHT,
    SQG_KEY_UP,
    SQG_KEY_DOWN,
    SQG_KEY_HOME,
    SQG_KEY_END,
    SQG_KEY_TAB,
    SQG_KEY_ESCAPE
};

typedef struct {
    int mx, my;         /* pointer position in canvas pixels */
    int down;           /* primary button is held */
    int pressed;        /* went down this frame */
    int released;       /* went up this frame */
    int wheel;          /* wheel steps this frame, + is up/away */
    int key;            /* one SQG_KEY_* this frame, or 0 */
    char text[32];      /* UTF-8 typed this frame (ASCII is drawn) */
    int down2;          /* the secondary (right) button is held */
    int pressed2;       /* it went down this frame */
    int released2;      /* it went up this frame */
    int shift, ctrl;    /* held now */
    int ctrl_key;       /* a letter typed with Ctrl this frame ('z'), or 0 */
} sqg_input;

/* Widget identity: any non-zero value unique within a frame */
typedef uint32_t sqg_id;
sqg_id sqg_id_str(const char *s);

typedef struct {
    sqg_canvas *cv;
    const sqg_theme *theme;
    int scale;          /* text scale, 1 or more */
    sqg_input in;
    sqg_id hot;         /* under the pointer this frame */
    sqg_id active;      /* holds the button since it was pressed on it */
    sqg_id focus;       /* takes the keyboard */
    int cursor;         /* text cursor in the focused field */
    int quit;           /* set by the program or the back end */
} sqg_ui;

void sqg_ui_init(sqg_ui *ui, const sqg_theme *theme, int scale);
void sqg_begin(sqg_ui *ui, sqg_canvas *cv, const sqg_input *in);
void sqg_end(sqg_ui *ui);

/* Widgets (sqgui_ui.c) */
enum { SQG_ALIGN_LEFT = 0, SQG_ALIGN_CENTER = 1, SQG_ALIGN_RIGHT = 2 };
void sqg_label(sqg_ui *ui, sqg_rect r, const char *text, int align);
/* Returns 1 on the frame the button is clicked (released over it) */
int sqg_button(sqg_ui *ui, sqg_id id, sqg_rect r, const char *label);
/* A button that stays down while *on; returns 1 when *on changed */
int sqg_toggle(sqg_ui *ui, sqg_id id, sqg_rect r, const char *label, int *on);
/* A check box with its label; returns 1 when *on changed */
int sqg_checkbox(sqg_ui *ui, sqg_id id, sqg_rect r, const char *label, int *on);
/* Horizontal slider for lo..hi; returns 1 when *v changed. The wheel
 * moves it by a twentieth of the range. */
int sqg_slider(sqg_ui *ui, sqg_id id, sqg_rect r, double *v, double lo, double hi);
/* One-of-n row of segments; returns 1 when *sel changed */
int sqg_segments(sqg_ui *ui, sqg_id id, sqg_rect r, const char *const *labels, int n, int *sel);
/* Scrolling list; returns 1 when *sel changed. *top is the first row shown. */
int sqg_list(sqg_ui *ui, sqg_id id, sqg_rect r, const char *const *rows, int n, int *sel, int *top);
/* Single-line text field holding UTF-8 (characters outside the font draw
 * as ?); returns 1 when Enter is pressed in it */
int sqg_textfield(sqg_ui *ui, sqg_id id, sqg_rect r, char *buf, size_t cap);
/* Sunken read-only text area with wrapping; returns the lines used */
int sqg_textbox(sqg_ui *ui, sqg_rect r, const char *text);

/* Layout helper: a row cursor that hands out rectangles left to right */
typedef struct {
    sqg_rect area;
    int x;
    int gap;
} sqg_row;
sqg_row sqg_row_begin(sqg_rect area, int gap);
sqg_rect sqg_row_take(sqg_row *row, int w);        /* w <= 0: the rest */
/* Height of one line of widget at this scale (text plus padding) */
int sqg_line_h(const sqg_ui *ui);
/* Width a button needs for its label */
int sqg_button_w(const sqg_ui *ui, const char *label);

/* Write a canvas as a binary PPM (P6); returns 0 on success */
int sqg_write_ppm(const sqg_canvas *c, const char *path);
/* FNV-1a over the canvas pixels (RGB only), for golden tests */
uint32_t sqg_hash(const sqg_canvas *c);

/* SDL2 window back end (sqgui_sdl.c, built when SDL2 is there) */
typedef struct sqg_window sqg_window;
sqg_window *sqg_window_open(const char *title, int w, int h);
/* Pump events into *in; returns 0 when the window was closed. The
 * canvas follows the window size. */
int sqg_window_poll(sqg_window *win, sqg_input *in);
sqg_canvas *sqg_window_canvas(sqg_window *win);
void sqg_window_present(sqg_window *win);
void sqg_window_set_title(sqg_window *win, const char *title);
void sqg_window_close(sqg_window *win);
/* Last raw key the back end saw this frame (SDL keycode), for shortcuts */
int sqg_window_rawkey(const sqg_window *win);

#ifdef __cplusplus
}
#endif

#endif
