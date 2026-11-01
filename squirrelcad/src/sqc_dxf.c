#include "sqc_dxf.h"

#include <ctype.h>
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

static int eqi(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static void use_layer(SqcDrawing *dwg, const char *name) {
    if (!name || !name[0]) name = "0";
    if (sqc_set_layer(dwg, name) < 0) {
        sqc_add_layer(dwg, name, 7);
        sqc_set_layer(dwg, name);
    }
}

int sqc_file_looks_like_dxf(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[160];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    char *p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF)
        p += 3;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (strncmp(p, "0", 1) == 0 || strncmp(p, "999", 3) == 0) {
        if (strstr(buf, "SECTION") || strstr(buf, "ENTITIES") || strstr(buf, "HEADER"))
            return 1;
        return strchr(p, '\n') != NULL;
    }
    return 0;
}

int sqc_file_looks_like_dwg(const char *path) {
    if (sqc_file_looks_like_dxf(path)) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char ver[8] = {0};
    size_t n = fread(ver, 1, 6, f);
    unsigned char rest[64];
    size_t m = fread(rest, 1, sizeof(rest), f);
    fclose(f);
    if (n < 6) return 0;
    /* AutoCAD DWG begins with ASCII version e.g. AC1015, then binary. */
    if (ver[0] == 'A' && ver[1] == 'C' && ver[2] == '1' && ver[3] == '0') {
        int nul = 0;
        for (size_t i = 0; i < m; i++)
            if (rest[i] == 0) nul++;
        if (nul >= 2) return 1;
        /* even without nuls, AC10xx prefix without DXF markers ⇒ DWG */
        return 1;
    }
    return 0;
}

int sqc_import_dxf(SqcDrawing *dwg, const char *path, SqcDxfStats *stats_out) {
    SqcDxfStats st;
    memset(&st, 0, sizeof(st));
    if (stats_out) *stats_out = st;

    if (sqc_file_looks_like_dwg(path)) {
        fprintf(stderr,
                "error: '%s' looks like binary AutoCAD DWG.\n"
                "SquirrelCAD does not read proprietary .dwg.\n"
                "In AutoCAD: SAVEAS → AutoCAD DXF (*.dxf), then:\n"
                "  squirrelcad IMPORT-DXF <out.sqdwg> <file.dxf>\n",
                path);
        return -2;
    }

    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (dwg->layer_count == 0) sqc_drawing_init(dwg, "from-dxf");

    DxfPair pair;
    char entity[64] = "";
    char layer[SQC_MAX_NAME] = "0";
    double x = 0, y = 0, z = 0, x1 = 0, y1 = 0, z1 = 0;
    double radius = 0, a0 = 0, a1 = 0;
    char text[SQC_MAX_NAME] = "";
    int in_entities = 0;
    int in_layer_table = 0;
    int collecting = 0;

    int lw_n = 0, lw_closed = 0, lw_count = 0;
    SqcPoint lw_pts[SQC_MAX_PLINE];
    double lw_bulge = 0;

    while (read_pair(f, &pair)) {
        if (pair.code == 0) {
            if (collecting && in_entities) {
                use_layer(dwg, layer);
                if (eqi(entity, "LINE")) {
                    SqcPoint a = {x, y, z}, b = {x1, y1, z1};
                    if (sqc_line(dwg, a, b) > 0) st.lines++;
                    else st.skipped++;
                } else if (eqi(entity, "CIRCLE")) {
                    SqcPoint c = {x, y, z};
                    if (sqc_circle(dwg, c, radius) > 0) st.circles++;
                    else st.skipped++;
                } else if (eqi(entity, "ARC")) {
                    SqcPoint c = {x, y, z};
                    if (sqc_arc(dwg, c, radius, a0, a1) > 0) st.arcs++;
                    else st.skipped++;
                } else if (eqi(entity, "TEXT") || eqi(entity, "MTEXT")) {
                    SqcPoint p = {x, y, z};
                    if (sqc_text(dwg, p, text) > 0) st.texts++;
                    else st.skipped++;
                } else if (eqi(entity, "LWPOLYLINE")) {
                    int n = lw_count;
                    if (n > SQC_MAX_PLINE) n = SQC_MAX_PLINE;
                    if (lw_closed && n >= 2 && n < SQC_MAX_PLINE) {
                        lw_pts[n++] = lw_pts[0];
                    }
                    if (n >= 2 && sqc_pline(dwg, lw_pts, n) > 0) st.plines++;
                    else if (n >= 2) {
                        /* overflow: fall back to segments */
                        for (int i = 1; i < n; i++) {
                            if (sqc_line(dwg, lw_pts[i - 1], lw_pts[i]) > 0) st.lines++;
                            else st.skipped++;
                        }
                    } else
                        st.skipped++;
                } else if (entity[0] && !eqi(entity, "LAYER") && !eqi(entity, "VERTEX") &&
                           !eqi(entity, "SEQEND") && !eqi(entity, "POLYLINE") &&
                           !eqi(entity, "INSERT")) {
                    st.skipped++;
                }
            }

            collecting = 0;
            lw_count = lw_n = lw_closed = 0;
            lw_bulge = 0;
            radius = a0 = a1 = 0;
            text[0] = 0;
            snprintf(layer, sizeof(layer), "0");
            x = y = z = x1 = y1 = z1 = 0;
            snprintf(entity, sizeof(entity), "%s", pair.val);

            if (eqi(entity, "SECTION")) {
            } else if (eqi(entity, "ENDSEC")) {
                in_entities = 0;
                in_layer_table = 0;
            } else if (eqi(entity, "EOF")) {
                break;
            } else if (eqi(entity, "ENDTAB")) {
                in_layer_table = 0;
            } else if (eqi(entity, "LAYER") && in_layer_table) {
                collecting = 1;
            } else if (in_entities && (eqi(entity, "LINE") || eqi(entity, "CIRCLE") ||
                                       eqi(entity, "ARC") || eqi(entity, "TEXT") ||
                                       eqi(entity, "MTEXT") || eqi(entity, "LWPOLYLINE") ||
                                       eqi(entity, "POLYLINE") || eqi(entity, "POINT") ||
                                       eqi(entity, "INSERT"))) {
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
                sqc_add_layer(dwg, pair.val, 7);
                st.layers++;
            }
        }

        if (!collecting) continue;

        if (pair.code == 8) {
            snprintf(layer, sizeof(layer), "%s", pair.val);
        } else if (pair.code == 10) {
            x = atof(pair.val);
            if (eqi(entity, "LWPOLYLINE") && lw_count < SQC_MAX_PLINE) {
                lw_pts[lw_count].x = x;
                lw_pts[lw_count].y = 0;
                lw_pts[lw_count].z = 0;
            }
        } else if (pair.code == 20) {
            y = atof(pair.val);
            if (eqi(entity, "LWPOLYLINE") && lw_count < SQC_MAX_PLINE) {
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
            snprintf(text, sizeof(text), "%s", pair.val);
        else if (pair.code == 70 && eqi(entity, "LWPOLYLINE"))
            lw_closed = (atoi(pair.val) & 1) != 0;
        else if (pair.code == 90 && eqi(entity, "LWPOLYLINE"))
            lw_n = atoi(pair.val);
        else if (pair.code == 42)
            lw_bulge = atof(pair.val);
        (void)lw_n;
        (void)lw_bulge;
    }

    if (collecting && in_entities) {
        use_layer(dwg, layer);
        if (eqi(entity, "LINE")) {
            SqcPoint a = {x, y, z}, b = {x1, y1, z1};
            if (sqc_line(dwg, a, b) > 0) st.lines++;
        } else if (eqi(entity, "CIRCLE")) {
            SqcPoint c = {x, y, z};
            if (sqc_circle(dwg, c, radius) > 0) st.circles++;
        } else if (eqi(entity, "ARC")) {
            SqcPoint c = {x, y, z};
            if (sqc_arc(dwg, c, radius, a0, a1) > 0) st.arcs++;
        } else if (eqi(entity, "TEXT") || eqi(entity, "MTEXT")) {
            SqcPoint p = {x, y, z};
            if (sqc_text(dwg, p, text) > 0) st.texts++;
        } else if (eqi(entity, "LWPOLYLINE")) {
            int n = lw_count;
            if (n > SQC_MAX_PLINE) n = SQC_MAX_PLINE;
            if (lw_closed && n >= 2 && n < SQC_MAX_PLINE) lw_pts[n++] = lw_pts[0];
            if (n >= 2 && sqc_pline(dwg, lw_pts, n) > 0) st.plines++;
        }
    }

    fclose(f);
    if (stats_out) *stats_out = st;
    return 0;
}

static const char *layer_name(const SqcDrawing *dwg, int id) {
    for (int i = 0; i < dwg->layer_count; i++)
        if (dwg->layers[i].id == id) return dwg->layers[i].name;
    return "0";
}

int sqc_export_dxf(const SqcDrawing *dwg, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "0\nSECTION\n2\nHEADER\n");
    fprintf(f, "9\n$ACADVER\n1\nAC1009\n");
    fprintf(f, "9\n$INSUNITS\n70\n6\n");
    fprintf(f, "0\nENDSEC\n");

    fprintf(f, "0\nSECTION\n2\nTABLES\n");
    fprintf(f, "0\nTABLE\n2\nLAYER\n70\n%d\n", dwg->layer_count > 0 ? dwg->layer_count : 1);
    if (dwg->layer_count == 0)
        fprintf(f, "0\nLAYER\n2\n0\n70\n0\n62\n7\n6\nCONTINUOUS\n");
    for (int i = 0; i < dwg->layer_count; i++) {
        const SqcLayer *L = &dwg->layers[i];
        fprintf(f, "0\nLAYER\n2\n%s\n70\n0\n62\n7\n6\nCONTINUOUS\n", L->name);
    }
    fprintf(f, "0\nENDTAB\n0\nENDSEC\n");

    fprintf(f, "0\nSECTION\n2\nENTITIES\n");
    for (int i = 0; i < dwg->ent_count; i++) {
        const SqcEntity *e = &dwg->ents[i];
        const char *lay = layer_name(dwg, e->layer_id);
        switch (e->type) {
        case SQC_ENT_LINE:
            fprintf(f, "0\nLINE\n8\n%s\n", lay);
            fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n", e->p1.x, e->p1.y, e->p1.z);
            fprintf(f, "11\n%.15g\n21\n%.15g\n31\n%.15g\n", e->p2.x, e->p2.y, e->p2.z);
            break;
        case SQC_ENT_CIRCLE:
            fprintf(f, "0\nCIRCLE\n8\n%s\n", lay);
            fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n40\n%.15g\n", e->p1.x, e->p1.y,
                    e->p1.z, e->radius);
            break;
        case SQC_ENT_ARC:
            fprintf(f, "0\nARC\n8\n%s\n", lay);
            fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n40\n%.15g\n", e->p1.x, e->p1.y,
                    e->p1.z, e->radius);
            fprintf(f, "50\n%.15g\n51\n%.15g\n", e->start_ang, e->end_ang);
            break;
        case SQC_ENT_PLINE:
            fprintf(f, "0\nLWPOLYLINE\n8\n%s\n90\n%d\n70\n0\n", lay, e->vert_count);
            for (int v = 0; v < e->vert_count; v++)
                fprintf(f, "10\n%.15g\n20\n%.15g\n", e->verts[v].x, e->verts[v].y);
            break;
        case SQC_ENT_TEXT:
            fprintf(f, "0\nTEXT\n8\n%s\n", lay);
            fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n40\n1.0\n1\n%s\n", e->p1.x, e->p1.y,
                    e->p1.z, e->text);
            break;
        case SQC_ENT_INSERT:
            /* exported as point marker text for now */
            fprintf(f, "0\nTEXT\n8\n%s\n", lay);
            fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n40\n1.0\n1\nINSERT:%s\n", e->p1.x,
                    e->p1.y, e->p1.z, e->block_name);
            break;
        }
    }
    fprintf(f, "0\nENDSEC\n0\nEOF\n");
    fclose(f);
    return 0;
}
