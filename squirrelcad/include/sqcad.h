#ifndef SQCAD_H
#define SQCAD_H

#include <stddef.h>
#include <stdint.h>

#define SQC_MAX_NAME 64
#define SQC_MAX_LAYERS 64
#define SQC_MAX_ENTS 256
#define SQC_MAX_PLINE 16
#define SQC_MAX_BLOCK_ENTS 32

typedef struct {
    double x, y, z;
} SqcPoint;

typedef enum {
    SQC_ENT_LINE = 1,
    SQC_ENT_CIRCLE = 2,
    SQC_ENT_ARC = 3,
    SQC_ENT_PLINE = 4,
    SQC_ENT_TEXT = 5,
    SQC_ENT_INSERT = 6
} SqcEntType;

typedef struct {
    int id;
    char name[SQC_MAX_NAME];
    int on;
    uint32_t color;
} SqcLayer;

typedef struct {
    int id;
    SqcEntType type;
    int layer_id;
    SqcPoint p1;
    SqcPoint p2;
    double radius;
    double start_ang;
    double end_ang;
    SqcPoint verts[SQC_MAX_PLINE];
    int vert_count;
    char text[SQC_MAX_NAME];
    char block_name[SQC_MAX_NAME];
    double scale;
    double rotation_deg;
} SqcEntity;

typedef struct {
    char name[SQC_MAX_NAME];
    int ent_ids[SQC_MAX_BLOCK_ENTS];
    int ent_count;
    SqcPoint base;
} SqcBlock;

typedef struct {
    char title[SQC_MAX_NAME];
    SqcLayer layers[SQC_MAX_LAYERS];
    int layer_count;
    int current_layer;
    SqcEntity ents[SQC_MAX_ENTS];
    int ent_count;
    SqcBlock blocks[128];
    int block_count;
    int next_id;
} SqcDrawing;

void sqc_drawing_init(SqcDrawing *dwg, const char *title);
int sqc_add_layer(SqcDrawing *dwg, const char *name, uint32_t color);
int sqc_set_layer(SqcDrawing *dwg, const char *name);
int sqc_line(SqcDrawing *dwg, SqcPoint a, SqcPoint b);
int sqc_circle(SqcDrawing *dwg, SqcPoint c, double r);
int sqc_arc(SqcDrawing *dwg, SqcPoint c, double r, double start_deg, double end_deg);
int sqc_pline(SqcDrawing *dwg, const SqcPoint *verts, int n);
int sqc_text(SqcDrawing *dwg, SqcPoint p, const char *text);
int sqc_block_def(SqcDrawing *dwg, const char *name, SqcPoint base, const int *ids, int n);
int sqc_insert(SqcDrawing *dwg, const char *block, SqcPoint at, double scale, double rot_deg);
int sqc_write_sqdwg(const SqcDrawing *dwg, const char *path);
int sqc_read_sqdwg(SqcDrawing *dwg, const char *path);
double sqc_ent_length(const SqcEntity *e);

/* ---- editing ---- */
SqcEntity *sqc_find(SqcDrawing *dwg, int id);
const SqcLayer *sqc_layer_of(const SqcDrawing *dwg, int layer_id);
const SqcBlock *sqc_find_block(const SqcDrawing *dwg, const char *name);
/* 1 when entity id belongs to a block definition (not drawn in model space) */
int sqc_in_block(const SqcDrawing *dwg, int id);
/* remove an entity; -1 when there is none, -2 when a block uses it */
int sqc_erase(SqcDrawing *dwg, int id);
/* a copy with a new id; its id, or -1 when full */
int sqc_copy(SqcDrawing *dwg, int id);
/* p' = base + R(rot_deg) * (scale * (p - base)) + move, applied to an entity */
void sqc_transform(SqcEntity *e, SqcPoint base, double scale, double rot_deg, SqcPoint move);
/* the model-space extents of visible entities (blocks expanded); 0 when empty */
int sqc_extents(const SqcDrawing *dwg, SqcPoint *lo, SqcPoint *hi);
/* area enclosed by a polyline (closed or not, shoelace) or a circle */
double sqc_ent_area(const SqcEntity *e);

/* Walk every visible model-space entity with blocks expanded: fn gets each
 * primitive (never an INSERT) already transformed into model space, with
 * the layer of the outermost insert for parts on layer 0. */
typedef void (*SqcVisit)(const SqcEntity *e, const SqcLayer *layer, void *ctx);
void sqc_visit(const SqcDrawing *dwg, SqcVisit fn, void *ctx);

/* ---- paper space ---- */
typedef struct {
    double width_mm, height_mm; /* the sheet */
    double scale;               /* paper mm per model unit; 0 = fit */
    char name[16];              /* "A3", ... */
} SqcSheet;

/* "A4", "A3", "A2", "A1", "A0", "LETTER", "TABLOID", landscape or not.
 * Returns 0, or -1 for an unknown size. */
int sqc_sheet(const char *name, int landscape, SqcSheet *out);
/* Plot model space onto a sheet as SVG: a border, a title block (title,
 * scale, sheet) and the drawing at `scale` (model units are metres: 1:100
 * is 10 paper mm per unit) or fitted. Returns 0, or -1 when the file can't
 * be written. */
int sqc_plot_svg(const SqcDrawing *dwg, const SqcSheet *sheet, const char *path);

#endif
