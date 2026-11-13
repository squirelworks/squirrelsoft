/* SquirrelPaint: a paint program on libs/sqgui. Original code. */
#include "paintgui.h"
#include "paintio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
    fputs("usage: squirrelpaint [options] [FILE]\n"
          "  FILE, --open FILE  open a BMP or PPM (PGM too)\n"
          "  --new WxH        start with a white picture (640x480 unless one is opened)\n"
          "  --do CMD         draw: \"color ff0000\", \"size 6\", \"line 10 10 200 80\",\n"
          "                   \"rect 20 20 120 90 square\", \"fill on\", \"ellipse ...\",\n"
          "                   \"brush X Y X Y ...\", \"fill X Y\", \"pick X Y\", \"undo\",\n"
          "                   \"select 0 0 99 99\", \"copy\", \"paste 10 10\", \"rotate 90\",\n"
          "                   \"flip h\", \"resize 320 240 smooth\", \"crop\", \"canvas W H\"\n"
          "  --script FILE    the same, one command a line (# for comments)\n"
          "  --save FILE      write the picture (.bmp, or .ppm / .pnm) after the commands\n"
          "  --snapshot FILE  draw one 960x680 frame of the window to FILE (PPM), no window\n"
          "  --gui-frames N   close the window after N frames (testing)\n"
          "  --help           this text\n",
          f);
}

static PaintDoc doc;      /* about 30 MB: the picture, the press copy, undo and fill */
static PaintGui gui;
static uint8_t file_buf[16 << 20];
static uint32_t file_px[PAINT_MAX_W * PAINT_MAX_H];

static int open_file(PaintGui *g, const char *path, char *msg, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(msg, cap, "cannot read it");
        return -1;
    }
    size_t n = fread(file_buf, 1, sizeof file_buf, f);
    int big = n == sizeof file_buf && fgetc(f) != EOF;
    fclose(f);
    if (big) {
        snprintf(msg, cap, "too big a file");
        return -1;
    }
    int w, h;
    if (paint_read_image(file_buf, n, file_px, sizeof file_px / sizeof file_px[0], &w, &h, msg, cap)) return -1;
    if (w > PAINT_MAX_W || h > PAINT_MAX_H || paint_load(g->doc, file_px, w, h)) {
        snprintf(msg, cap, "%d x %d is bigger than %d x %d", w, h, PAINT_MAX_W, PAINT_MAX_H);
        return -1;
    }
    snprintf(g->path, sizeof g->path, "%s", path);
    return 0;
}

static int save_file(PaintGui *g, const char *path, char *msg, size_t cap) {
    PaintDoc *d = g->doc;
    paint_drop(d); /* a pasted or moved selection still floating goes in too */
    size_t n = paint_write_image(paint_kind_of_name(path), d->px, d->w, d->h, file_buf, sizeof file_buf);
    if (n > sizeof file_buf) {
        snprintf(msg, cap, "too big to write");
        return -1;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        snprintf(msg, cap, "cannot write it");
        return -1;
    }
    int ok = fwrite(file_buf, 1, n, f) == n;
    if (fclose(f) || !ok) {
        snprintf(msg, cap, "could not write all of it");
        return -1;
    }
    if (path != g->path) snprintf(g->path, sizeof g->path, "%s", path);
    return 0;
}

static int run_command(const char *where, const char *cmd) {
    char msg[120];
    if (paint_command(&doc, cmd, msg, sizeof msg)) {
        fprintf(stderr, "squirrelpaint: %s%s: %s\n", where, cmd, msg);
        return -1;
    }
    return 0;
}

static int run_script(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "squirrelpaint: cannot read %s\n", path);
        return -1;
    }
    char line[1024], where[300];
    int no = 0, rc = 0;
    while (fgets(line, sizeof line, f)) {
        no++;
        line[strcspn(line, "\r\n")] = 0;
        const char *p = line + strspn(line, " \t");
        if (!*p || *p == '#') continue;
        snprintf(where, sizeof where, "%.200s:%d: ", path, no);
        if (run_command(where, p)) {
            rc = -1;
            break;
        }
    }
    fclose(f);
    return rc;
}

int main(int argc, char **argv) {
    const char *open = NULL, *save = NULL, *snapshot = NULL;
    int nw = 640, nh = 480, sized = 0, frames = 0;
    /* first the picture: --new or a file */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            return 0;
        } else if (!strcmp(a, "--new") && more) {
            char x;
            if (sscanf(argv[++i], "%d%c%d", &nw, &x, &nh) != 3 || (x != 'x' && x != 'X')) {
                fprintf(stderr, "squirrelpaint: --new %s: expected WxH, as in 640x480\n", argv[i]);
                return 2;
            }
            sized = 1;
        } else if (!strcmp(a, "--open") && more) {
            open = argv[++i];
        } else if ((!strcmp(a, "--do") || !strcmp(a, "--script") || !strcmp(a, "--save") ||
                    !strcmp(a, "--snapshot") || !strcmp(a, "--gui-frames")) &&
                   more) {
            i++;
        } else if (a[0] != '-' && !open) {
            open = a;
        } else {
            usage(stderr);
            return 2;
        }
    }
    if (paint_new(&doc, nw, nh)) {
        fprintf(stderr, "squirrelpaint: a picture is 1x1 to %dx%d\n", PAINT_MAX_W, PAINT_MAX_H);
        return 2;
    }
    paint_gui_init(&gui, &doc);
    gui.open_file = open_file;
    gui.save_file = save_file;
    if (open && !sized) {
        char msg[120];
        if (open_file(&gui, open, msg, sizeof msg)) {
            fprintf(stderr, "squirrelpaint: %s: %s\n", open, msg);
            return 1;
        }
    } else if (open) {
        snprintf(gui.path, sizeof gui.path, "%s", open); /* a new picture, to be saved there */
    }
    /* then the drawing, in order */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--new") || !strcmp(a, "--open")) {
            i++;
        } else if (!strcmp(a, "--do")) {
            if (run_command("", argv[++i])) return 1;
        } else if (!strcmp(a, "--script")) {
            if (run_script(argv[++i])) return 1;
        } else if (!strcmp(a, "--save")) {
            save = argv[++i];
        } else if (!strcmp(a, "--snapshot")) {
            snapshot = argv[++i];
        } else if (!strcmp(a, "--gui-frames")) {
            frames = atoi(argv[++i]);
        }
    }
    if (save) {
        char msg[120];
        if (save_file(&gui, save, msg, sizeof msg)) {
            fprintf(stderr, "squirrelpaint: %s: %s\n", save, msg);
            return 1;
        }
        doc.changed = 0;
    }
    if (snapshot) {
        enum { W = 960, H = 680 };
        static uint32_t px[W * H];
        sqg_canvas cv;
        sqg_canvas_init(&cv, px, W, H, W);
        sqg_ui ui;
        sqg_ui_init(&ui, NULL, 1);
        sqg_input in = {0};
        in.mx = in.my = -1;
        sqg_begin(&ui, &cv, &in);
        paint_gui_frame(&gui, &ui);
        sqg_end(&ui);
        if (sqg_write_ppm(&cv, snapshot)) {
            fprintf(stderr, "squirrelpaint: could not write %s\n", snapshot);
            return 1;
        }
        printf("%d x %d\n", doc.w, doc.h);
        return 0;
    }
    if (save) return 0; /* drawn and saved: no window wanted */
    return paint_gui_run(&gui, frames);
}
