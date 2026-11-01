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

#endif
