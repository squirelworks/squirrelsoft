#ifndef SQS_DXF_H
#define SQS_DXF_H

#include "sqstation.h"

/*
 * ASCII DXF interchange for MicroStation / AutoCAD workflows.
 * MicroStation: File → Export/Import → DXF (preferred bridge to .dgn).
 * Binary .dgn is not parsed — use detect helper + export DXF from MicroStation.
 */

typedef struct {
    int lines;
    int arcs;
    int circles;    /* full 360 degree arcs */
    int texts;      /* TEXT and MTEXT */
    int plines;     /* open polylines, and pieces of ones longer than SQS_MAX_PTS */
    int shapes;     /* closed polylines */
    int meshes;     /* polyface meshes and runs of 3DFACEs on one layer, in pieces that fit */
    int cells;      /* BLOCKs kept as cells (not *Model_Space and the like) */
    int placements; /* INSERTs of a known block */
    int layers;
    int skipped;
} SqsDxfStats;

/* Import ASCII DXF into an existing or empty design (appends geometry).
 * Reads the LAYER table (colour, off, frozen), BLOCKS as cells, and LINE,
 * CIRCLE, ARC, TEXT, MTEXT, LWPOLYLINE, POLYLINE (2D, 3D and polyface),
 * 3DFACE and INSERT. Anything else, or anything with a coordinate that is
 * not a number, is counted as skipped. */
int sqs_import_dxf(SqsDesign *d, const char *path, SqsDxfStats *stats_out);

/* Export as R12 ASCII DXF: layers, cells as BLOCKS, elements, placements as
 * INSERTs; meshes and solids as polyface meshes. */
int sqs_export_dxf(const SqsDesign *d, const char *path);

/* Returns 1 if path looks like binary MicroStation DGN (V7/V8). */
int sqs_file_looks_like_dgn(const char *path);

/* Returns 1 if path looks like ASCII DXF. */
int sqs_file_looks_like_dxf(const char *path);

#endif
