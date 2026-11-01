#ifndef SQC_DXF_H
#define SQC_DXF_H

#include "sqcad.h"

/*
 * ASCII DXF interchange for AutoCAD workflows.
 * .dwg is proprietary — export DXF from AutoCAD, then import-dxf here.
 */

typedef struct {
    int lines;
    int circles;
    int arcs;
    int texts;
    int plines;
    int layers;
    int skipped;
} SqcDxfStats;

int sqc_import_dxf(SqcDrawing *dwg, const char *path, SqcDxfStats *stats_out);
int sqc_export_dxf(const SqcDrawing *dwg, const char *path);
int sqc_file_looks_like_dxf(const char *path);
int sqc_file_looks_like_dwg(const char *path);

#endif
