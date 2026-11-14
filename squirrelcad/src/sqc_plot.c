/* Paper space for SquirrelCAD: model space plotted onto a sheet as SVG,
 * with a border and a title block (original code). */
#include "sqcad.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const struct {
    const char *name;
    double w, h; /* portrait, mm */
} SHEETS[] = {
    {"A4", 210, 297}, {"A3", 297, 420}, {"A2", 420, 594}, {"A1", 594, 841},
    {"A0", 841, 1189}, {"LETTER", 215.9, 279.4}, {"TABLOID", 279.4, 431.8},
};

static int eqi(const char *a, const char *b) {
    while (*a && *b && toupper((unsigned char)*a) == toupper((unsigned char)*b)) a++, b++;
    return *a == 0 && *b == 0;
}

int sqc_sheet(const char *name, int landscape, SqcSheet *out) {
    for (size_t i = 0; i < sizeof SHEETS / sizeof SHEETS[0]; i++)
        if (eqi(name, SHEETS[i].name)) {
            memset(out, 0, sizeof *out);
            out->width_mm = landscape ? SHEETS[i].h : SHEETS[i].w;
            out->height_mm = landscape ? SHEETS[i].w : SHEETS[i].h;
            snprintf(out->name, sizeof out->name, "%s", SHEETS[i].name);
            return 0;
        }
    return -1;
}

#define MARGIN 10.0   /* sheet edge to border, mm */
#define TB_W 80.0     /* title block */
#define TB_H 24.0
#define TEXT_MM 2.5   /* drawing text height on paper */

/* AutoCAD-style colour numbers 1..7 (7 prints black on white); larger
 * numbers are 0xRRGGBB */
static void colour(uint32_t c, char *out, size_t cap) {
    static const char *const ACI[8] = {"#000000", "#e00000", "#c0a000", "#00a000",
                                       "#00a0a0", "#0000e0", "#c000c0", "#000000"};
    if (c <= 7) snprintf(out, cap, "%s", ACI[c]);
    else if (c > 255) snprintf(out, cap, "#%06x", (unsigned)(c & 0xffffff));
    else snprintf(out, cap, "#606060");
}

typedef struct {
    FILE *f;
    double s;        /* paper mm per model unit */
    double ox, oy;   /* paper position of model (0, 0) */
    double sheet_h;
} Plot;

static double px(const Plot *p, double x) { return p->ox + x * p->s; }
static double py(const Plot *p, double y) { return p->sheet_h - (p->oy + y * p->s); } /* SVG y runs down */

static void xml_text(FILE *f, const char *s) {
    for (; *s; s++) {
        if (*s == '<') fputs("&lt;", f);
        else if (*s == '>') fputs("&gt;", f);
        else if (*s == '&') fputs("&amp;", f);
        else if (*s == '"') fputs("&quot;", f);
        else fputc(*s, f);
    }
}

static void plot_ent(const SqcEntity *e, const SqcLayer *L, void *ctx) {
    Plot *p = ctx;
    char col[16];
    colour(L ? L->color : 7, col, sizeof col);
    FILE *f = p->f;
    switch (e->type) {
    case SQC_ENT_LINE:
        fprintf(f, "<line x1=\"%.3f\" y1=\"%.3f\" x2=\"%.3f\" y2=\"%.3f\" stroke=\"%s\"/>\n", px(p, e->p1.x),
                py(p, e->p1.y), px(p, e->p2.x), py(p, e->p2.y), col);
        break;
    case SQC_ENT_CIRCLE:
        fprintf(f, "<circle cx=\"%.3f\" cy=\"%.3f\" r=\"%.3f\" stroke=\"%s\"/>\n", px(p, e->p1.x), py(p, e->p1.y),
                e->radius * p->s, col);
        break;
    case SQC_ENT_ARC: {
        /* counter-clockwise from start to end in model space */
        double a0 = e->start_ang * M_PI / 180, a1 = e->end_ang * M_PI / 180;
        double sweep = fmod(e->end_ang - e->start_ang + 720.0, 360.0);
        if (sweep == 0) sweep = 360;
        double r = e->radius * p->s;
        double x0 = px(p, e->p1.x + e->radius * cos(a0)), y0 = py(p, e->p1.y + e->radius * sin(a0));
        double x1 = px(p, e->p1.x + e->radius * cos(a1)), y1 = py(p, e->p1.y + e->radius * sin(a1));
        /* y is flipped, so model counter-clockwise is SVG sweep-flag 0 */
        fprintf(f, "<path d=\"M %.3f %.3f A %.3f %.3f 0 %d 0 %.3f %.3f\" stroke=\"%s\"/>\n", x0, y0, r, r,
                sweep > 180 ? 1 : 0, x1, y1, col);
        break;
    }
    case SQC_ENT_PLINE:
        fprintf(f, "<polyline points=\"");
        for (int i = 0; i < e->vert_count; i++)
            fprintf(f, "%s%.3f,%.3f", i ? " " : "", px(p, e->verts[i].x), py(p, e->verts[i].y));
        fprintf(f, "\" stroke=\"%s\"/>\n", col);
        break;
    case SQC_ENT_TEXT:
        fprintf(f, "<text x=\"%.3f\" y=\"%.3f\" font-size=\"%.2f\" fill=\"%s\" stroke=\"none\"", px(p, e->p1.x),
                py(p, e->p1.y), TEXT_MM * (e->scale > 0 ? e->scale : 1), col);
        if (e->rotation_deg != 0)
            fprintf(f, " transform=\"rotate(%.3f %.3f %.3f)\"", -e->rotation_deg, px(p, e->p1.x), py(p, e->p1.y));
        fprintf(f, ">");
        xml_text(f, e->text);
        fprintf(f, "</text>\n");
        break;
    default:
        break;
    }
}

int sqc_plot_svg(const SqcDrawing *dwg, const SqcSheet *sheet, const char *path) {
    /* binary so MinGW/Windows keep canonical LF */
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    double W = sheet->width_mm, H = sheet->height_mm;
    /* the viewport: inside the border, above the title block's band */
    double vx0 = MARGIN + 5, vy0 = MARGIN + TB_H + 5, vx1 = W - MARGIN - 5, vy1 = H - MARGIN - 5;
    SqcPoint lo, hi;
    int any = sqc_extents(dwg, &lo, &hi);
    Plot p = {f, sheet->scale, 0, 0, H};
    if (!any) lo.x = lo.y = hi.x = hi.y = 0;
    double mw = hi.x - lo.x, mh = hi.y - lo.y;
    if (p.s <= 0) {
        double sx = mw > 0 ? (vx1 - vx0) / mw : 1e30, sy = mh > 0 ? (vy1 - vy0) / mh : 1e30;
        p.s = sx < sy ? sx : sy;
        if (p.s >= 1e30) p.s = 1;
    }
    /* centre the drawing in the viewport */
    p.ox = (vx0 + vx1) / 2 - (lo.x + mw / 2) * p.s;
    p.oy = (vy0 + vy1) / 2 - (lo.y + mh / 2) * p.s;

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f,
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%gmm\" height=\"%gmm\" viewBox=\"0 0 %g %g\">\n",
            W, H, W, H);
    fprintf(f, "<rect width=\"%g\" height=\"%g\" fill=\"#ffffff\"/>\n", W, H);
    fprintf(f, "<g fill=\"none\" stroke=\"#000000\" stroke-width=\"0.5\">\n");
    fprintf(f, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\"/>\n", MARGIN, MARGIN, W - 2 * MARGIN,
            H - 2 * MARGIN);
    double tx = W - MARGIN - TB_W, ty = H - MARGIN - TB_H;
    fprintf(f, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\"/>\n", tx, ty, TB_W, TB_H);
    fprintf(f, "<line x1=\"%g\" y1=\"%g\" x2=\"%g\" y2=\"%g\"/>\n", tx, ty + TB_H / 2, tx + TB_W, ty + TB_H / 2);
    fprintf(f, "</g>\n");
    /* the title block */
    char scale_txt[48];
    double n = 1000.0 / p.s; /* 1:n with model units in metres */
    if (sheet->scale > 0) snprintf(scale_txt, sizeof scale_txt, "1:%g", n);
    else snprintf(scale_txt, sizeof scale_txt, "fit (1:%.4g)", n);
    fprintf(f, "<g font-family=\"sans-serif\" fill=\"#000000\">\n");
    fprintf(f, "<text x=\"%g\" y=\"%g\" font-size=\"5\">", tx + 3, ty + TB_H / 2 - 3);
    xml_text(f, dwg->title);
    fprintf(f, "</text>\n");
    fprintf(f, "<text x=\"%g\" y=\"%g\" font-size=\"3\">Scale %s  Sheet %s  Units m</text>\n", tx + 3, ty + TB_H - 4,
            scale_txt, sheet->name);
    fprintf(f, "</g>\n");
    /* the drawing, clipped to the viewport */
    fprintf(f, "<clipPath id=\"vp\"><rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\"/></clipPath>\n", vx0,
            H - vy1, vx1 - vx0, vy1 - vy0);
    fprintf(f,
            "<g clip-path=\"url(#vp)\" fill=\"none\" stroke-width=\"0.25\" font-family=\"sans-serif\" "
            "stroke-linecap=\"round\">\n");
    sqc_visit(dwg, plot_ent, &p);
    fprintf(f, "</g>\n</svg>\n");
    int bad = ferror(f);
    bad |= fclose(f) != 0;
    return bad ? -1 : 0;
}
