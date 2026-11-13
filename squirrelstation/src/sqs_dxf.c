#include "sqs_dxf.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int code;
    char val[256];
} DxfPair;

static void trim(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = 0;
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static int read_pair(FILE *f, DxfPair *p) {
    char a[256], b[256];
    if (!fgets(a, sizeof(a), f)) return 0;
    if (!fgets(b, sizeof(b), f)) return 0;
    trim(a);
    trim(b);
    p->code = atoi(a);
    snprintf(p->val, sizeof(p->val), "%s", b);
    return 1;
}

static int level_id_by_name(SqsDesign *d, const char *name) {
    if (!name || !name[0]) name = "0";
    for (int i = 0; i < d->level_count; i++)
        if (strcmp(d->levels[i].name, name) == 0) return d->levels[i].id;
    return sqs_add_level(d, name, 0xFFFFFF);
}

static int eqi(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

int sqs_file_looks_like_dgn(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char buf[8];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n < 4) return 0;
    /* DGN V8 OLE/compound often starts with D0 CF 11 E0 (OLE compound doc). */
    if (buf[0] == 0xD0 && buf[1] == 0xCF && buf[2] == 0x11 && buf[3] == 0xE0) return 1;
    /* DGN V7 typically begins with element type/level words; common signature
       scan: many V7 files have 0x08/0x09 style first bytes — weak heuristic:
       reject if text ASCII DXF, accept binary with high NULs in first 64. */
    f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char probe[64];
    n = fread(probe, 1, sizeof(probe), f);
    fclose(f);
    if (n < 16) return 0;
    int nul = 0, printable = 0;
    for (size_t i = 0; i < n; i++) {
        if (probe[i] == 0) nul++;
        if (probe[i] >= 32 && probe[i] < 127) printable++;
    }
    /* Binary-ish and not starting with "0\nSECTION" style */
    if (nul >= 4 && printable < (int)n / 2) {
        /* exclude ELF/PE */
        if (probe[0] == 0x7f && probe[1] == 'E') return 0;
        return 1;
    }
    return 0;
}

int sqs_file_looks_like_dxf(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[128];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    /* Skip UTF-8 BOM */
    char *p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF)
        p += 3;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    /* Typical start: "0\nSECTION" or "999\ncomment" */
    if (strncmp(p, "0", 1) == 0 || strncmp(p, "999", 3) == 0) {
        if (strstr(buf, "SECTION") || strstr(buf, "ENTITIES") || strstr(buf, "HEADER"))
            return 1;
        /* still might be DXF with only group codes early */
        return strchr(p, '\n') != NULL;
    }
    return 0;
}

static void ensure_default_level(SqsDesign *d) {
    if (d->level_count == 0) sqs_design_init(d, d->title[0] ? d->title : "dxf");
}

int sqs_import_dxf(SqsDesign *d, const char *path, SqsDxfStats *stats_out) {
    SqsDxfStats st;
    memset(&st, 0, sizeof(st));
    if (stats_out) *stats_out = st;

    if (sqs_file_looks_like_dgn(path) && !sqs_file_looks_like_dxf(path)) {
        fprintf(stderr,
                "error: '%s' looks like binary MicroStation DGN.\n"
                "SquirrelStation does not read proprietary .dgn.\n"
                "In MicroStation: File → Export → DXF/DWG → save .dxf, then:\n"
                "  squirrelstation import-dxf <out.sqdgn> <file.dxf>\n",
                path);
        return -2;
    }

    FILE *f = fopen(path, "r");
    if (!f) return -1;
    ensure_default_level(d);

    DxfPair pair;
    char entity[64] = "";
    char layer[SQS_MAX_NAME] = "0";
    double x = 0, y = 0, z = 0, x1 = 0, y1 = 0, z1 = 0;
    double radius = 0, a0 = 0, a1 = 0;
    char text[SQS_MAX_NAME] = "";
    int in_entities = 0;
    int in_layer_table = 0;
    int collecting = 0;

    /* LWPOLYLINE state */
    int lw_n = 0;
    int lw_closed = 0;
    SqsPoint lw_pts[256];
    int lw_count = 0;
    double lw_bulge = 0; /* ignored for now except as skip */

    while (read_pair(f, &pair)) {
        if (pair.code == 0) {
            /* finish previous entity */
            if (collecting && in_entities) {
                int lid = level_id_by_name(d, layer);
                if (eqi(entity, "LINE")) {
                    SqsPoint a = {x, y, z}, b = {x1, y1, z1};
                    if (sqs_add_line(d, lid, a, b) > 0) st.lines++;
                    else st.skipped++;
                } else if (eqi(entity, "CIRCLE")) {
                    SqsPoint c = {x, y, z};
                    if (sqs_add_arc(d, lid, c, radius, 0, 360) > 0) {
                        st.circles++;
                        st.arcs++;
                    } else
                        st.skipped++;
                } else if (eqi(entity, "ARC")) {
                    SqsPoint c = {x, y, z};
                    double sweep = a1 - a0;
                    while (sweep <= 0) sweep += 360.0;
                    while (sweep > 360) sweep -= 360.0;
                    if (sqs_add_arc(d, lid, c, radius, a0, sweep) > 0) st.arcs++;
                    else st.skipped++;
                } else if (eqi(entity, "TEXT") || eqi(entity, "MTEXT")) {
                    SqsPoint p = {x, y, z};
                    if (sqs_add_text(d, lid, p, text) > 0) st.texts++;
                    else st.skipped++;
                } else if (eqi(entity, "LWPOLYLINE")) {
                    if (lw_closed && lw_count >= 2) {
                        if (lw_count < 256) lw_pts[lw_count++] = lw_pts[0];
                    }
                    for (int i = 1; i < lw_count; i++) {
                        if (sqs_add_line(d, lid, lw_pts[i - 1], lw_pts[i]) > 0) {
                            st.lines++;
                            st.pline_segs++;
                        } else
                            st.skipped++;
                    }
                } else if (entity[0] && !eqi(entity, "LAYER") && !eqi(entity, "VERTEX") &&
                           !eqi(entity, "SEQEND") && !eqi(entity, "POLYLINE")) {
                    st.skipped++;
                }
            }

            collecting = 0;
            lw_count = 0;
            lw_n = 0;
            lw_closed = 0;
            lw_bulge = 0;
            radius = 0;
            a0 = a1 = 0;
            text[0] = 0;
            layer[0] = '0';
            layer[1] = 0;
            x = y = z = x1 = y1 = z1 = 0;

            snprintf(entity, sizeof(entity), "%.63s", pair.val);
            if (eqi(entity, "SECTION")) {
                /* next 2 = name */
            } else if (eqi(entity, "ENDSEC")) {
                in_entities = 0;
                in_layer_table = 0;
            } else if (eqi(entity, "EOF")) {
                break;
            } else if (eqi(entity, "TABLE")) {
                /* wait for 2 = table name */
            } else if (eqi(entity, "ENDTAB")) {
                in_layer_table = 0;
            } else if (eqi(entity, "LAYER") && in_layer_table) {
                collecting = 1; /* layer table entry uses code 2 for name */
            } else if (in_entities && (eqi(entity, "LINE") || eqi(entity, "CIRCLE") ||
                                       eqi(entity, "ARC") || eqi(entity, "TEXT") ||
                                       eqi(entity, "MTEXT") || eqi(entity, "LWPOLYLINE") ||
                                       eqi(entity, "POLYLINE") || eqi(entity, "POINT"))) {
                collecting = 1;
            }
            continue;
        }

        if (pair.code == 2) {
            if (eqi(entity, "SECTION")) {
                if (eqi(pair.val, "ENTITIES")) in_entities = 1;
            } else if (eqi(entity, "TABLE") && eqi(pair.val, "LAYER")) {
                in_layer_table = 1;
            } else if (collecting && in_layer_table && eqi(entity, "LAYER")) {
                level_id_by_name(d, pair.val);
                st.layers++;
            }
        }

        if (!collecting) continue;

        if (pair.code == 8) {
            snprintf(layer, sizeof(layer), "%.63s", pair.val);
        } else if (pair.code == 10) {
            x = atof(pair.val);
            if (eqi(entity, "LWPOLYLINE") && lw_count < 256) {
                lw_pts[lw_count].x = x;
                lw_pts[lw_count].y = 0;
                lw_pts[lw_count].z = 0;
            }
        } else if (pair.code == 20) {
            y = atof(pair.val);
            if (eqi(entity, "LWPOLYLINE") && lw_count < 256) {
                lw_pts[lw_count].y = y;
                lw_count++;
            }
        } else if (pair.code == 30)
            z = atof(pair.val);
        else if (pair.code == 11)
            x1 = atof(pair.val);
        else if (pair.code == 21)
            y1 = atof(pair.val);
        else if (pair.code == 31)
            z1 = atof(pair.val);
        else if (pair.code == 40)
            radius = atof(pair.val);
        else if (pair.code == 50)
            a0 = atof(pair.val);
        else if (pair.code == 51)
            a1 = atof(pair.val);
        else if (pair.code == 1 || pair.code == 3)
            snprintf(text, sizeof(text), "%.63s", pair.val);
        else if (pair.code == 70 && eqi(entity, "LWPOLYLINE"))
            lw_closed = (atoi(pair.val) & 1) != 0;
        else if (pair.code == 90 && eqi(entity, "LWPOLYLINE"))
            lw_n = atoi(pair.val);
        else if (pair.code == 42)
            lw_bulge = atof(pair.val);
        (void)lw_n;
        (void)lw_bulge;
    }

    /* Flush final entity if the file ended without a trailing type-0 marker. */
    if (collecting && in_entities) {
        int lid = level_id_by_name(d, layer);
        if (eqi(entity, "LINE")) {
            SqsPoint a = {x, y, z}, b = {x1, y1, z1};
            if (sqs_add_line(d, lid, a, b) > 0) st.lines++;
        } else if (eqi(entity, "CIRCLE")) {
            SqsPoint c = {x, y, z};
            if (sqs_add_arc(d, lid, c, radius, 0, 360) > 0) {
                st.circles++;
                st.arcs++;
            }
        } else if (eqi(entity, "ARC")) {
            SqsPoint c = {x, y, z};
            double sweep = a1 - a0;
            while (sweep <= 0) sweep += 360.0;
            if (sqs_add_arc(d, lid, c, radius, a0, sweep) > 0) st.arcs++;
        } else if (eqi(entity, "TEXT") || eqi(entity, "MTEXT")) {
            SqsPoint p = {x, y, z};
            if (sqs_add_text(d, lid, p, text) > 0) st.texts++;
        } else if (eqi(entity, "LWPOLYLINE")) {
            if (lw_closed && lw_count >= 2 && lw_count < 256) lw_pts[lw_count++] = lw_pts[0];
            for (int i = 1; i < lw_count; i++) {
                if (sqs_add_line(d, lid, lw_pts[i - 1], lw_pts[i]) > 0) {
                    st.lines++;
                    st.pline_segs++;
                }
            }
        }
    }

    fclose(f);
    if (stats_out) *stats_out = st;
    return 0;
}

static const char *level_name(const SqsDesign *d, int id) {
    for (int i = 0; i < d->level_count; i++)
        if (d->levels[i].id == id) return d->levels[i].name;
    return "0";
}

/* the nearest of the seven basic DXF colours to 0xRRGGBB */
static int aci(uint32_t rgb) {
    static const uint32_t BASIC[8] = {0, 0xff0000, 0xffff00, 0x00ff00, 0x00ffff, 0x0000ff, 0xff00ff, 0xffffff};
    int best = 7;
    long bd = -1;
    for (int k = 1; k <= 7; k++) {
        long dr = (long)((rgb >> 16) & 255) - (long)((BASIC[k] >> 16) & 255);
        long dg = (long)((rgb >> 8) & 255) - (long)((BASIC[k] >> 8) & 255);
        long db = (long)(rgb & 255) - (long)(BASIC[k] & 255);
        long dd = dr * dr + dg * dg + db * db;
        if (bd < 0 || dd < bd) bd = dd, best = k;
    }
    return best;
}

typedef struct {
    FILE *f;
    const SqsDesign *d;
} Out;

static void face3d(FILE *f, const char *lay, SqsPoint a, SqsPoint b, SqsPoint c, SqsPoint d) {
    fprintf(f, "0\n3DFACE\n8\n%s\n", lay);
    fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n", a.x, a.y, a.z);
    fprintf(f, "11\n%.15g\n21\n%.15g\n31\n%.15g\n", b.x, b.y, b.z);
    fprintf(f, "12\n%.15g\n22\n%.15g\n32\n%.15g\n", c.x, c.y, c.z);
    fprintf(f, "13\n%.15g\n23\n%.15g\n33\n%.15g\n", d.x, d.y, d.z);
}

static void polyline(FILE *f, const char *lay, const SqsPoint *p, int n, int closed) {
    /* a 3D polyline keeps z on every vertex */
    fprintf(f, "0\nPOLYLINE\n8\n%s\n66\n1\n10\n0\n20\n0\n30\n0\n70\n%d\n", lay, 8 | (closed ? 1 : 0));
    for (int i = 0; i < n; i++)
        fprintf(f, "0\nVERTEX\n8\n%s\n10\n%.15g\n20\n%.15g\n30\n%.15g\n70\n32\n", lay, p[i].x, p[i].y, p[i].z);
    fprintf(f, "0\nSEQEND\n8\n%s\n", lay);
}

static void write_el(const SqsElement *e, const SqsLevel *lv, int ref, void *ctx) {
    (void)ref;
    Out *o = ctx;
    FILE *f = o->f;
    const char *lay = lv ? lv->name : level_name(o->d, e->level_id);
    switch (e->type) {
    case SQS_LINE:
        fprintf(f, "0\nLINE\n8\n%s\n", lay);
        fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n", e->a.x, e->a.y, e->a.z);
        fprintf(f, "11\n%.15g\n21\n%.15g\n31\n%.15g\n", e->b.x, e->b.y, e->b.z);
        break;
    case SQS_ARC:
        if (fabs(e->sweep_deg) >= 359.999) {
            fprintf(f, "0\nCIRCLE\n8\n%s\n", lay);
            fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n40\n%.15g\n", e->a.x, e->a.y, e->a.z, e->radius);
        } else {
            /* DXF arcs run counter-clockwise from 50 to 51 */
            double s = e->sweep_deg >= 0 ? e->start_deg : e->start_deg + e->sweep_deg;
            double end = s + fabs(e->sweep_deg);
            fprintf(f, "0\nARC\n8\n%s\n", lay);
            fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n40\n%.15g\n", e->a.x, e->a.y, e->a.z, e->radius);
            fprintf(f, "50\n%.15g\n51\n%.15g\n", s, end);
        }
        break;
    case SQS_TEXT:
        fprintf(f, "0\nTEXT\n8\n%s\n", lay);
        fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n", e->a.x, e->a.y, e->a.z);
        fprintf(f, "40\n1.0\n1\n%s\n", e->text);
        break;
    case SQS_PLINE:
    case SQS_SHAPE:
        polyline(f, lay, e->pts, e->npts, e->type == SQS_SHAPE);
        break;
    case SQS_CURVE: {
        /* R12 has no spline: the curve as a fine polyline through its points */
        enum { STEPS = 8 };
        SqsPoint p[(SQS_MAX_PTS - 1) * STEPS + 1];
        int n = 0, steps = STEPS * (e->npts - 1);
        for (int k = 0; k <= steps; k++) p[n++] = sqs_curve_at(e, (double)k / steps);
        polyline(f, lay, p, n, 0);
        break;
    }
    case SQS_MESH:
        for (int k = 0; k < e->nfaces; k++)
            face3d(f, lay, e->pts[e->faces[k][0]], e->pts[e->faces[k][1]], e->pts[e->faces[k][2]],
                   e->pts[e->faces[k][3]]);
        break;
    case SQS_SOLID: {
        SqsPoint p[64];
        uint8_t fc[64][4];
        int np, nf = sqs_solid_mesh(e, p, 64, &np, fc, 64);
        for (int k = 0; k < nf; k++) face3d(f, lay, p[fc[k][0]], p[fc[k][1]], p[fc[k][2]], p[fc[k][3]]);
        break;
    }
    default:
        break;
    }
}

/* Levels as layers with their colours (negative when hidden or frozen), then
 * every shown element with cells expanded: lines, arcs and circles, text,
 * 3D polylines for polylines, shapes and curves, and 3DFACEs for meshes and
 * solids. R12 (AC1009) ASCII DXF. */
int sqs_export_dxf(const SqsDesign *d, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "0\nSECTION\n2\nHEADER\n");
    fprintf(f, "9\n$ACADVER\n1\nAC1009\n");
    fprintf(f, "9\n$INSUNITS\n70\n6\n"); /* meters hint */
    fprintf(f, "0\nENDSEC\n");

    fprintf(f, "0\nSECTION\n2\nTABLES\n");
    fprintf(f, "0\nTABLE\n2\nLAYER\n70\n%d\n", d->level_count > 0 ? d->level_count : 1);
    if (d->level_count == 0) fprintf(f, "0\nLAYER\n2\n0\n70\n0\n62\n7\n6\nCONTINUOUS\n");
    for (int i = 0; i < d->level_count; i++) {
        const SqsLevel *lv = &d->levels[i];
        int c = aci(lv->color);
        fprintf(f, "0\nLAYER\n2\n%s\n70\n%d\n62\n%d\n6\nCONTINUOUS\n", lv->name, lv->frozen ? 1 : 0,
                lv->visible && !lv->frozen ? c : -c);
    }
    fprintf(f, "0\nENDTAB\n0\nENDSEC\n");

    fprintf(f, "0\nSECTION\n2\nENTITIES\n");
    Out o = {f, d};
    /* hidden levels still go out (the layer is off), so walk a copy with every level shown */
    static SqsDesign all;
    all = *d;
    for (int i = 0; i < all.level_count; i++) all.levels[i].visible = 1, all.levels[i].frozen = 0;
    sqs_visit(&all, NULL, 0, write_el, &o);
    fprintf(f, "0\nENDSEC\n0\nEOF\n");
    int bad = ferror(f);
    bad |= fclose(f) != 0;
    return bad ? -1 : 0;
}
