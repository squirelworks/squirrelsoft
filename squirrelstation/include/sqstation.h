/*
 * sqstation: the SquirrelStation design core (original code). A design is
 * levels, an element graph (lines, arcs, polylines, curves, shapes, meshes,
 * solid primitives, text and placed cells), cell definitions and reference
 * attachments to other designs. It snaps like AccuSnap, takes AccuDraw-style
 * keyins, and reads and writes a text .sqdgn and a binary .sqdgnb.
 *
 * Everything is a fixed-size value the caller owns (an SqsDesign is about a
 * megabyte: keep it static or on the heap, not on the stack).
 */
#ifndef SQSTATION_H
#define SQSTATION_H

#include <stddef.h>
#include <stdint.h>

#define SQS_MAX_NAME 64
#define SQS_MAX_LEVELS 64
#define SQS_MAX_ELEMENTS 1024
#define SQS_MAX_CELL_MEMBERS 64
#define SQS_MAX_CELLS 64
#define SQS_MAX_PTS 24   /* vertices of one polyline, curve, shape or mesh */
#define SQS_MAX_FACES 16 /* faces of one mesh */
#define SQS_MAX_REFS 8
#define SQS_MAX_PATH 128

typedef struct {
    double x, y, z;
} SqsPoint;

typedef enum {
    SQS_LINE = 1,
    SQS_ARC = 2,     /* a: centre; radius, start_deg, sweep_deg (360 = circle) */
    SQS_PLINE = 3,   /* pts: open line string */
    SQS_TEXT = 4,    /* a: origin; text */
    SQS_CURVE = 5,   /* pts: a smooth curve through every point (Catmull-Rom) */
    SQS_SHAPE = 6,   /* pts: a closed planar outline */
    SQS_MESH = 7,    /* pts and faces of 3 or 4 vertex indices */
    SQS_SOLID = 8,   /* solid_kind at a with size: box, cylinder or sphere */
    SQS_CELLREF = 9  /* cell placed at a with scale and rot_deg (about z) */
} SqsElementType;

typedef enum { SQS_SOLID_BOX = 1, SQS_SOLID_CYLINDER = 2, SQS_SOLID_SPHERE = 3 } SqsSolidKind;

typedef struct {
    int id;
    char name[SQS_MAX_NAME];
    int visible; /* shown */
    int frozen;  /* neither shown nor snapped to */
    uint32_t color;
} SqsLevel;

typedef struct {
    int id;
    SqsElementType type;
    int level_id;
    SqsPoint a;
    SqsPoint b;       /* line end */
    SqsPoint c;       /* unused, kept for files written before v2 */
    double radius;    /* arc */
    double start_deg; /* arc */
    double sweep_deg; /* arc */
    char text[SQS_MAX_NAME]; /* text, or the cell name of a CELLREF */
    SqsPoint pts[SQS_MAX_PTS];
    int npts;
    uint8_t faces[SQS_MAX_FACES][4]; /* a triangle repeats its last index */
    int nfaces;
    int solid_kind;   /* SqsSolidKind */
    SqsPoint size;    /* box: dx dy dz; cylinder: r, -, h; sphere: r */
    double scale;     /* CELLREF */
    double rot_deg;   /* CELLREF */
} SqsElement;

typedef struct {
    int id;
    char name[SQS_MAX_NAME];
    int member_ids[SQS_MAX_CELL_MEMBERS];
    int member_count;
    SqsPoint origin;
} SqsCell;

/* another design shown (and snapped to) under this one, moved and scaled */
typedef struct {
    char path[SQS_MAX_PATH];
    SqsPoint offset;
    double scale;
    int display;
} SqsRef;

typedef enum {
    SQS_SNAP_NONE = 0,
    SQS_SNAP_ENDPOINT,
    SQS_SNAP_MIDPOINT,
    SQS_SNAP_CENTER,
    SQS_SNAP_INTERSECTION,
    SQS_SNAP_KEYPOINT, /* a curve or shape vertex, a solid or cell origin */
    SQS_SNAP_NEAREST   /* the closest point on an element, when no other point is near */
} SqsSnapKind;

typedef struct {
    SqsSnapKind kind;
    SqsPoint p;
    int element_id; /* in this design, or in the reference (ref >= 0) */
    int ref;        /* -1 for this design, else the reference index */
    double distance;
} SqsSnapResult;

typedef struct {
    char title[SQS_MAX_NAME];
    SqsLevel levels[SQS_MAX_LEVELS];
    int level_count;
    SqsElement elements[SQS_MAX_ELEMENTS];
    int element_count;
    SqsCell cells[SQS_MAX_CELLS];
    int cell_count;
    SqsRef refs[SQS_MAX_REFS];
    int ref_count;
    int next_id;
} SqsDesign;

void sqs_design_init(SqsDesign *d, const char *title);
int sqs_add_level(SqsDesign *d, const char *name, uint32_t color);
const SqsLevel *sqs_level(const SqsDesign *d, int level_id);
int sqs_level_id(const SqsDesign *d, const char *name); /* -1 when there is none */
/* change a level's flags; -1 when there is no such level */
int sqs_level_set(SqsDesign *d, const char *name, int visible, int frozen);

/* Each returns the new element id, or -1 (design full, bad level, bad input). */
int sqs_add_line(SqsDesign *d, int level_id, SqsPoint a, SqsPoint b);
int sqs_add_arc(SqsDesign *d, int level_id, SqsPoint center, double radius, double start_deg, double sweep_deg);
int sqs_add_text(SqsDesign *d, int level_id, SqsPoint p, const char *text);
int sqs_add_pline(SqsDesign *d, int level_id, const SqsPoint *pts, int n);
int sqs_add_curve(SqsDesign *d, int level_id, const SqsPoint *pts, int n);
int sqs_add_shape(SqsDesign *d, int level_id, const SqsPoint *pts, int n);
int sqs_add_mesh(SqsDesign *d, int level_id, const SqsPoint *pts, int n, const uint8_t (*faces)[4], int nf);
int sqs_add_solid(SqsDesign *d, int level_id, SqsSolidKind kind, SqsPoint origin, SqsPoint size);
int sqs_define_cell(SqsDesign *d, const char *name, SqsPoint origin, const int *member_ids, int member_count);
int sqs_place_cell(SqsDesign *d, int level_id, const char *name, SqsPoint at, double scale, double rot_deg);
const SqsCell *sqs_find_cell(const SqsDesign *d, const char *name);
const SqsElement *sqs_find(const SqsDesign *d, int id);
/* attach a design file as a reference; its index, or -1 when full */
int sqs_attach(SqsDesign *d, const char *path, SqsPoint offset, double scale);

/* ---- measuring ---- */
/* a point on a curve element at t in [0, 1] (Catmull-Rom through its points) */
SqsPoint sqs_curve_at(const SqsElement *e, double t);
double sqs_length(const SqsElement *e); /* lines, arcs, polylines, curves, shape perimeters */
double sqs_area(const SqsElement *e);   /* shapes (projected on xy), circles, mesh surface, solid surface */
double sqs_volume(const SqsElement *e); /* solids */
/* A solid as a mesh: up to cap points and fcap faces. Returns the faces. */
int sqs_solid_mesh(const SqsElement *e, SqsPoint *pts, int cap, int *npts, uint8_t (*faces)[4], int fcap);

/* ---- the element graph as drawn ----
 * Every shown element, with cells expanded (members moved, scaled and
 * turned onto the placement) and, when refs is given, every shown element
 * of the attached references (moved and scaled). Hidden or frozen levels
 * are left out. fn gets each element (never a CELLREF), its level, and the
 * reference index (-1 for this design). */
typedef void (*SqsVisit)(const SqsElement *e, const SqsLevel *lv, int ref, void *ctx);
void sqs_visit(const SqsDesign *d, const SqsDesign *const *refs, int nrefs, SqsVisit fn, void *ctx);

/* ---- snaps ---- */
SqsSnapResult sqs_accu_snap(const SqsDesign *d, SqsPoint cursor, double tol);
/* with the references loaded (refs[i] for d->refs[i], NULL when not loaded) */
SqsSnapResult sqs_snap(const SqsDesign *d, const SqsDesign *const *refs, int nrefs, SqsPoint cursor, double tol);

/* ---- keyed input, AccuDraw style ----
 *   xy=x,y[,z]     absolute
 *   dl=dx,dy[,dz]  delta from the last point
 *   di=dist,angle  distance and direction (degrees from east) from the last point
 *   x,y[,z]        absolute
 * Returns 0, or -1 for text that is none of these. */
int sqs_keyin(const char *text, SqsPoint last, SqsPoint *out);

/* ---- files ---- */
int sqs_write_sqdgn(const SqsDesign *d, const char *path);
/* text .sqdgn, version 1 or 2: 0, -1 when unreadable, -2 when damaged */
int sqs_read_sqdgn(SqsDesign *d, const char *path);
/* the binary .sqdgnb sketch: little-endian records, every length checked */
int sqs_write_sqdgnb(const SqsDesign *d, const char *path);
int sqs_read_sqdgnb(SqsDesign *d, const char *path);
/* either, by the first bytes of the file */
int sqs_read_any(SqsDesign *d, const char *path);

double sqs_dist(SqsPoint a, SqsPoint b);
SqsPoint sqs_mid(SqsPoint a, SqsPoint b);

#endif
