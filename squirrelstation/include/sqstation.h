#ifndef SQSTATION_H
#define SQSTATION_H

#include <stddef.h>
#include <stdint.h>

#define SQS_MAX_NAME 64
#define SQS_MAX_LEVELS 64
#define SQS_MAX_ELEMENTS 1024
#define SQS_MAX_CELL_MEMBERS 64

typedef struct {
    double x, y, z;
} SqsPoint;

typedef enum {
    SQS_LINE = 1,
    SQS_ARC = 2,
    SQS_PLINE = 3,
    SQS_TEXT = 4
} SqsElementType;

typedef struct {
    int id;
    char name[SQS_MAX_NAME];
    int visible;
    int frozen;
    uint32_t color;
} SqsLevel;

typedef struct {
    int id;
    SqsElementType type;
    int level_id;
    SqsPoint a;
    SqsPoint b;       /* line end, or arc center for convenience */
    SqsPoint c;       /* optional third point / pline mid */
    double radius;    /* arc */
    double start_deg; /* arc */
    double sweep_deg; /* arc */
    char text[SQS_MAX_NAME];
} SqsElement;

typedef struct {
    int id;
    char name[SQS_MAX_NAME];
    int member_ids[SQS_MAX_CELL_MEMBERS];
    int member_count;
    SqsPoint origin;
} SqsCell;

typedef enum {
    SQS_SNAP_NONE = 0,
    SQS_SNAP_ENDPOINT,
    SQS_SNAP_MIDPOINT,
    SQS_SNAP_CENTER,
    SQS_SNAP_INTERSECTION
} SqsSnapKind;

typedef struct {
    SqsSnapKind kind;
    SqsPoint p;
    int element_id;
    double distance;
} SqsSnapResult;

typedef struct {
    char title[SQS_MAX_NAME];
    SqsLevel levels[SQS_MAX_LEVELS];
    int level_count;
    SqsElement elements[SQS_MAX_ELEMENTS];
    int element_count;
    SqsCell cells[64];
    int cell_count;
    int next_id;
} SqsDesign;

void sqs_design_init(SqsDesign *d, const char *title);
int sqs_add_level(SqsDesign *d, const char *name, uint32_t color);
int sqs_add_line(SqsDesign *d, int level_id, SqsPoint a, SqsPoint b);
int sqs_add_arc(SqsDesign *d, int level_id, SqsPoint center, double radius,
                double start_deg, double sweep_deg);
int sqs_add_text(SqsDesign *d, int level_id, SqsPoint p, const char *text);
int sqs_define_cell(SqsDesign *d, const char *name, SqsPoint origin,
                    const int *member_ids, int member_count);

SqsSnapResult sqs_accu_snap(const SqsDesign *d, SqsPoint cursor, double tol);
int sqs_write_sqdgn(const SqsDesign *d, const char *path);
int sqs_read_sqdgn(SqsDesign *d, const char *path);

double sqs_dist(SqsPoint a, SqsPoint b);
SqsPoint sqs_mid(SqsPoint a, SqsPoint b);

#endif
