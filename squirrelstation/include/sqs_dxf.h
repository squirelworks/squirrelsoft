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
    int circles; /* imported as full 360° arcs */
    int texts;
    int pline_segs; /* polyline edges expanded to lines */
    int layers;
    int skipped;
} SqsDxfStats;

/* Import ASCII DXF into an existing or empty design (appends geometry). */
int sqs_import_dxf(SqsDesign *d, const char *path, SqsDxfStats *stats_out);

/* Export design as ASCII DXF R12-ish (ENTITIES + LAYER table). */
int sqs_export_dxf(const SqsDesign *d, const char *path);

/* Returns 1 if path looks like binary MicroStation DGN (V7/V8). */
int sqs_file_looks_like_dgn(const char *path);

/* Returns 1 if path looks like ASCII DXF. */
int sqs_file_looks_like_dxf(const char *path);

#endif
