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

/* one line without its line end, the rest of an overlong one dropped so
 * pairs stay in step */
static int read_line(FILE *f, char *buf, size_t cap) {
    if (!fgets(buf, (int)cap, f)) return 0;
    if (!strchr(buf, '\n')) {
        int c;
        while ((c = fgetc(f)) != EOF && c != '\n') {
        }
    }
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    return 1;
}

static int read_pair(FILE *f, DxfPair *p) {
    char a[256];
    if (!read_line(f, a, sizeof a) || !read_line(f, p->val, sizeof p->val)) return 0;
    p->code = atoi(a);
    if (p->code != 1 && p->code != 3) trim(p->val); /* text keeps its spaces */
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

/* One DXF object (everything from its 0 group to the next), gathered before
 * it is turned into entities. */
#define OBJ_PTS 64
typedef struct {
    char type[32];
    char layer[SQC_MAX_NAME];
    char name[SQC_MAX_NAME]; /* group 2: section, table entry, block or insert's block */
    char text[SQC_MAX_NAME];
    SqcPoint p[4];           /* groups 10..13 / 20..23 / 30..33 */
    double r, a0, a1, sx, rot;
    int has_a1, flags, color, bad;
    SqcPoint pts[OBJ_PTS];   /* LWPOLYLINE vertices */
    int npts, over;
} Obj;

typedef struct {
    SqcDrawing *dwg;
    SqcDxfStats *st;
    char section[16];
    int in_layers;
    /* a BLOCK being read */
    int in_block, skip_block;
    char block[SQC_MAX_NAME];
    SqcPoint base;
    int ids[SQC_MAX_BLOCK_ENTS], nids;
    /* an old-style POLYLINE being read */
    int in_pline, pline_closed, pline_over;
    char pline_layer[SQC_MAX_NAME];
    SqcPoint pv[OBJ_PTS];
    int npv;
} Imp;

static void append(char *dst, size_t cap, const char *src) {
    size_t n = strlen(dst);
    while (n + 1 < cap && *src) dst[n++] = *src++;
    dst[n] = 0;
}

static int is_finite(double v) { return v == v && v - v == 0; }

static double num(Obj *o, const char *v) {
    double x = atof(v);
    if (!is_finite(x)) {
        o->bad = 1;
        return 0;
    }
    return x;
}

/* a new entity: count it, and keep it for the block being read */
static void took(Imp *im, int id, int *count) {
    if (id <= 0) {
        im->st->skipped++;
        return;
    }
    (*count)++;
    if (!im->in_block) return;
    if (im->nids < SQC_MAX_BLOCK_ENTS) im->ids[im->nids++] = id;
    else im->st->skipped++; /* drawn in model space instead; counted as lost from the block */
}

/* a polyline of any length: chained pieces of at most SQC_MAX_PLINE vertices */
static void put_pline(Imp *im, const SqcPoint *v, int n, int closed) {
    SqcPoint tmp[OBJ_PTS + 1];
    if (n < 2) {
        im->st->skipped++;
        return;
    }
    memcpy(tmp, v, (size_t)n * sizeof v[0]);
    if (closed && n >= 3) tmp[n++] = v[0];
    for (int at = 0; at < n - 1;) {
        int k = n - at < SQC_MAX_PLINE ? n - at : SQC_MAX_PLINE;
        took(im, sqc_pline(im->dwg, tmp + at, k), &im->st->plines);
        at += k - 1;
    }
}

static void end_pline(Imp *im) {
    if (!im->in_pline) return;
    use_layer(im->dwg, im->pline_layer);
    put_pline(im, im->pv, im->npv, im->pline_closed);
    if (im->pline_over) im->st->skipped++;
    im->in_pline = 0;
}

static void finish(Imp *im, Obj *o) {
    SqcDrawing *dwg = im->dwg;
    SqcDxfStats *st = im->st;
    if (!o->type[0]) return;
    if (eqi(o->type, "SECTION")) {
        snprintf(im->section, sizeof im->section, "%.15s", o->name);
        return;
    }
    if (eqi(o->type, "ENDSEC")) {
        end_pline(im);
        im->section[0] = 0;
        im->in_layers = 0;
        return;
    }
    if (eqi(im->section, "TABLES")) {
        if (eqi(o->type, "TABLE")) im->in_layers = eqi(o->name, "LAYER");
        else if (eqi(o->type, "ENDTAB")) im->in_layers = 0;
        else if (im->in_layers && eqi(o->type, "LAYER") && o->name[0]) {
            int id = -1;
            for (int i = 0; i < dwg->layer_count; i++)
                if (strcmp(dwg->layers[i].name, o->name) == 0) id = dwg->layers[i].id;
            if (id < 0) {
                id = sqc_add_layer(dwg, o->name, 7);
                if (id > 0) st->layers++;
            }
            for (int i = 0; i < dwg->layer_count; i++)
                if (dwg->layers[i].id == id && o->color != 0) {
                    dwg->layers[i].color = (uint32_t)(o->color < 0 ? -o->color : o->color);
                    dwg->layers[i].on = o->color > 0; /* a negative colour means it is off */
                }
        }
        return;
    }
    int blocks = eqi(im->section, "BLOCKS");
    if (!blocks && !eqi(im->section, "ENTITIES")) return;
    if (blocks && eqi(o->type, "BLOCK")) {
        im->in_block = 1;
        im->nids = 0;
        /* *Model_Space, *Paper_Space and anonymous blocks are not ours to keep */
        im->skip_block = o->name[0] == 0 || o->name[0] == '*' || sqc_find_block(dwg, o->name) != NULL;
        snprintf(im->block, sizeof im->block, "%s", o->name);
        im->base = o->p[0];
        return;
    }
    if (blocks && eqi(o->type, "ENDBLK")) {
        end_pline(im);
        if (im->in_block && !im->skip_block) {
            if (sqc_block_def(dwg, im->block, im->base, im->ids, im->nids) > 0) st->blocks++;
            else st->skipped++;
        }
        im->in_block = 0;
        return;
    }
    if (blocks && !im->in_block) return;
    if (im->in_block && im->skip_block) return;

    if (eqi(o->type, "VERTEX")) {
        if (!im->in_pline || o->bad) return;
        if (o->flags & 16) return; /* a spline frame point */
        if (im->npv < OBJ_PTS) im->pv[im->npv++] = o->p[0];
        else im->pline_over = 1;
        return;
    }
    if (eqi(o->type, "SEQEND")) {
        end_pline(im);
        return;
    }
    end_pline(im); /* a POLYLINE without its SEQEND */
    if (o->bad) {
        st->skipped++;
        return;
    }
    use_layer(dwg, o->layer);
    if (eqi(o->type, "LINE")) {
        took(im, sqc_line(dwg, o->p[0], o->p[1]), &st->lines);
    } else if (eqi(o->type, "CIRCLE")) {
        took(im, o->r > 0 ? sqc_circle(dwg, o->p[0], o->r) : -1, &st->circles);
    } else if (eqi(o->type, "ARC")) {
        took(im, o->r > 0 ? sqc_arc(dwg, o->p[0], o->r, o->a0, o->has_a1 ? o->a1 : 360) : -1, &st->arcs);
    } else if (eqi(o->type, "TEXT") || eqi(o->type, "MTEXT")) {
        int id = sqc_text(dwg, o->p[0], o->text);
        SqcEntity *e = id > 0 ? sqc_find(dwg, id) : NULL;
        if (e) {
            if (o->r > 0) e->scale = o->r; /* the text height */
            e->rotation_deg = o->rot;
        }
        took(im, id, &st->texts);
    } else if (eqi(o->type, "LWPOLYLINE")) {
        put_pline(im, o->pts, o->npts, o->flags & 1);
        if (o->over) st->skipped++;
    } else if (eqi(o->type, "POLYLINE")) {
        if (o->flags & (16 | 64)) { /* polygon meshes and polyface meshes */
            st->skipped++;
            return;
        }
        im->in_pline = 1;
        im->pline_closed = o->flags & 1;
        im->pline_over = 0;
        im->npv = 0;
        snprintf(im->pline_layer, sizeof im->pline_layer, "%s", o->layer);
    } else if (eqi(o->type, "3DFACE") || eqi(o->type, "SOLID")) {
        /* an outline: the fourth corner is left out when it repeats the third */
        SqcPoint v[4] = {o->p[0], o->p[1], o->p[2], o->p[3]};
        if (eqi(o->type, "SOLID")) v[2] = o->p[3], v[3] = o->p[2]; /* SOLID corners go in a Z */
        int n = (v[3].x == v[2].x && v[3].y == v[2].y && v[3].z == v[2].z) ? 3 : 4;
        put_pline(im, v, n, 1);
        st->faces++;
    } else if (eqi(o->type, "INSERT")) {
        took(im, sqc_insert(dwg, o->name, o->p[0], o->sx, o->rot), &st->inserts);
    } else {
        st->skipped++;
    }
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
    int keep_layer = dwg->current_layer;

    static Imp im; /* about 2 KB of polyline: off the stack */
    static Obj o;
    memset(&im, 0, sizeof im);
    memset(&o, 0, sizeof o);
    im.dwg = dwg;
    im.st = &st;
    DxfPair pair;
    while (read_pair(f, &pair)) {
        if (pair.code == 0) {
            finish(&im, &o);
            memset(&o, 0, sizeof o);
            snprintf(o.type, sizeof o.type, "%.31s", pair.val);
            snprintf(o.layer, sizeof o.layer, "0");
            o.sx = 1;
            if (eqi(o.type, "EOF")) break;
            continue;
        }
        const char *v = pair.val;
        int c = pair.code;
        if (c == 8) snprintf(o.layer, sizeof o.layer, "%.63s", v);
        else if (c == 2) snprintf(o.name, sizeof o.name, "%.63s", v);
        else if (c == 1) {
            if (eqi(o.type, "MTEXT") && o.text[0]) append(o.text, sizeof o.text, v);
            else snprintf(o.text, sizeof o.text, "%.63s", v);
        } else if (c == 3 && eqi(o.type, "MTEXT")) append(o.text, sizeof o.text, v);
        else if (c == 10 && eqi(o.type, "LWPOLYLINE")) {
            if (o.npts < OBJ_PTS) o.pts[o.npts++] = (SqcPoint){num(&o, v), 0, 0};
            else o.over = 1;
        } else if (c == 20 && eqi(o.type, "LWPOLYLINE")) {
            if (o.npts > 0 && !o.over) o.pts[o.npts - 1].y = num(&o, v);
        } else if (c >= 10 && c <= 13) o.p[c - 10].x = num(&o, v);
        else if (c >= 20 && c <= 23) o.p[c - 20].y = num(&o, v);
        else if (c >= 30 && c <= 33) o.p[c - 30].z = num(&o, v);
        else if (c == 40) o.r = num(&o, v);
        else if (c == 41) o.sx = num(&o, v);
        else if (c == 50) o.a0 = o.rot = num(&o, v);
        else if (c == 51) o.a1 = num(&o, v), o.has_a1 = 1;
        else if (c == 62) o.color = atoi(v);
        else if (c == 70) o.flags = atoi(v);
    }
    finish(&im, &o);
    end_pline(&im);
    fclose(f);
    dwg->current_layer = keep_layer;
    if (stats_out) *stats_out = st;
    return 0;
}

static const char *layer_name(const SqcDrawing *dwg, int id) {
    for (int i = 0; i < dwg->layer_count; i++)
        if (dwg->layers[i].id == id) return dwg->layers[i].name;
    return "0";
}

static void write_ent(FILE *f, const SqcDrawing *dwg, const SqcEntity *e) {
    const char *lay = layer_name(dwg, e->layer_id);
    switch (e->type) {
    case SQC_ENT_LINE:
        fprintf(f, "0\nLINE\n8\n%s\n", lay);
        fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n", e->p1.x, e->p1.y, e->p1.z);
        fprintf(f, "11\n%.15g\n21\n%.15g\n31\n%.15g\n", e->p2.x, e->p2.y, e->p2.z);
        break;
    case SQC_ENT_CIRCLE:
        fprintf(f, "0\nCIRCLE\n8\n%s\n", lay);
        fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n40\n%.15g\n", e->p1.x, e->p1.y, e->p1.z, e->radius);
        break;
    case SQC_ENT_ARC:
        fprintf(f, "0\nARC\n8\n%s\n", lay);
        fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n40\n%.15g\n", e->p1.x, e->p1.y, e->p1.z, e->radius);
        fprintf(f, "50\n%.15g\n51\n%.15g\n", e->start_ang, e->end_ang);
        break;
    case SQC_ENT_PLINE: {
        /* a last vertex back on the first is written as the closed flag */
        int n = e->vert_count, closed = 0;
        if (n > 2 && e->verts[0].x == e->verts[n - 1].x && e->verts[0].y == e->verts[n - 1].y) closed = 1, n--;
        fprintf(f, "0\nLWPOLYLINE\n8\n%s\n90\n%d\n70\n%d\n", lay, n, closed);
        for (int v = 0; v < n; v++) fprintf(f, "10\n%.15g\n20\n%.15g\n", e->verts[v].x, e->verts[v].y);
        break;
    }
    case SQC_ENT_TEXT:
        fprintf(f, "0\nTEXT\n8\n%s\n", lay);
        fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n40\n%.15g\n1\n%s\n", e->p1.x, e->p1.y, e->p1.z,
                e->scale > 0 ? e->scale : 1.0, e->text);
        if (e->rotation_deg != 0) fprintf(f, "50\n%.15g\n", e->rotation_deg);
        break;
    case SQC_ENT_INSERT:
        fprintf(f, "0\nINSERT\n8\n%s\n2\n%s\n", lay, e->block_name);
        fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n", e->p1.x, e->p1.y, e->p1.z);
        fprintf(f, "41\n%.15g\n42\n%.15g\n43\n%.15g\n50\n%.15g\n", e->scale, e->scale, e->scale, e->rotation_deg);
        break;
    }
}

/* Layers with their colours (negative when off), block definitions, then
 * the model-space entities, in R12 (AC1009) ASCII DXF. */
int sqc_export_dxf(const SqcDrawing *dwg, const char *path) {
    /* binary so MinGW/Windows keep canonical LF (text mode would emit CRLF) */
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    fprintf(f, "0\nSECTION\n2\nHEADER\n");
    fprintf(f, "9\n$ACADVER\n1\nAC1009\n");
    fprintf(f, "9\n$INSUNITS\n70\n6\n");
    fprintf(f, "0\nENDSEC\n");

    fprintf(f, "0\nSECTION\n2\nTABLES\n");
    fprintf(f, "0\nTABLE\n2\nLAYER\n70\n%d\n", dwg->layer_count > 0 ? dwg->layer_count : 1);
    if (dwg->layer_count == 0) fprintf(f, "0\nLAYER\n2\n0\n70\n0\n62\n7\n6\nCONTINUOUS\n");
    for (int i = 0; i < dwg->layer_count; i++) {
        const SqcLayer *L = &dwg->layers[i];
        int aci = L->color >= 1 && L->color <= 255 ? (int)L->color : 7;
        fprintf(f, "0\nLAYER\n2\n%s\n70\n0\n62\n%d\n6\nCONTINUOUS\n", L->name, L->on ? aci : -aci);
    }
    fprintf(f, "0\nENDTAB\n0\nENDSEC\n");

    fprintf(f, "0\nSECTION\n2\nBLOCKS\n");
    for (int i = 0; i < dwg->block_count; i++) {
        const SqcBlock *b = &dwg->blocks[i];
        fprintf(f, "0\nBLOCK\n8\n0\n2\n%s\n70\n0\n10\n%.15g\n20\n%.15g\n30\n%.15g\n3\n%s\n", b->name, b->base.x,
                b->base.y, b->base.z, b->name);
        for (int j = 0; j < b->ent_count; j++)
            for (int k = 0; k < dwg->ent_count; k++)
                if (dwg->ents[k].id == b->ent_ids[j]) write_ent(f, dwg, &dwg->ents[k]);
        fprintf(f, "0\nENDBLK\n8\n0\n");
    }
    fprintf(f, "0\nENDSEC\n");

    fprintf(f, "0\nSECTION\n2\nENTITIES\n");
    for (int i = 0; i < dwg->ent_count; i++)
        if (!sqc_in_block(dwg, dwg->ents[i].id)) write_ent(f, dwg, &dwg->ents[i]);
    fprintf(f, "0\nENDSEC\n0\nEOF\n");
    int bad = ferror(f);
    bad |= fclose(f) != 0;
    return bad ? -1 : 0;
}
