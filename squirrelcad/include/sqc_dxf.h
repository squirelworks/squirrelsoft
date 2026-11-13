#ifndef SQC_DXF_H
#define SQC_DXF_H

#include "sqcad.h"

/*
 * ASCII DXF interchange for AutoCAD workflows.
 * .dwg is proprietary — export DXF from AutoCAD, then import-dxf here.
 *
 * Import reads the LAYER table (colour, off), BLOCKS (kept as blocks, their
 * entities taken out of model space) and ENTITIES: LINE, CIRCLE, ARC, TEXT,
 * MTEXT, LWPOLYLINE, POLYLINE with VERTEX/SEQEND, INSERT, and 3DFACE/SOLID
 * as closed outlines. A polyline longer than SQC_MAX_PLINE becomes chained
 * pieces. Anything else, and anything with a non-numeric coordinate, is
 * counted as skipped.
 */

typedef struct {
    int lines;
    int circles;
    int arcs;
    int texts;
    int plines;
    int layers;
    int skipped;
    int blocks;  /* BLOCK definitions kept (not *Model_Space and the like) */
    int inserts; /* INSERTs of a block that was defined */
    int faces;   /* 3DFACE and SOLID, kept as closed outlines */
} SqcDxfStats;

int sqc_import_dxf(SqcDrawing *dwg, const char *path, SqcDxfStats *stats_out);
int sqc_export_dxf(const SqcDrawing *dwg, const char *path);
int sqc_file_looks_like_dxf(const char *path);
int sqc_file_looks_like_dwg(const char *path);

#endif
