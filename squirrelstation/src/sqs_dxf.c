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

/* the seven basic DXF colours (ACI 1..7) as 0xRRGGBB */
static const uint32_t BASIC[8] = {0, 0xff0000, 0xffff00, 0x00ff00, 0x00ffff, 0x0000ff, 0xff00ff, 0xffffff};

#define OBJ_PTS 256 /* vertices of one polyline, or points of one polyface, read */
#define OBJ_FACES 256

/* one DXF object (from its 0 group to the next), gathered before use */
typedef struct {
    char type[32];
    char layer[SQS_MAX_NAME];
    char name[SQS_MAX_NAME]; /* group 2 */
    char text[SQS_MAX_NAME];
    SqsPoint p[4];           /* groups 10..13 */
    double r, a0, a1, sx, rot;
    int has_a1, flags, color, vi[4], bad;
    SqsPoint pts[OBJ_PTS];   /* LWPOLYLINE vertices */
    int npts, over;
} Obj;

typedef struct {
    SqsDesign *d;
    SqsDxfStats *st;
    char section[16];
    int in_layers;
    int in_block, skip_block; /* a BLOCK being read becomes a cell */
    char block[SQS_MAX_NAME];
    SqsPoint base;
    int ids[SQS_MAX_CELL_MEMBERS], nids, lost;
    /* a POLYLINE (or polyface mesh) being read, or a run of 3DFACEs */
    int poly, closed, face_run, over;
    char poly_layer[SQS_MAX_NAME];
    SqsPoint pv[OBJ_PTS];
    int npv;
    int fc[OBJ_FACES][4];
    int nfc;
} Imp;

static int is_finite(double v) { return v == v && v - v == 0; }

static double num(Obj *o, const char *v) {
    double x = atof(v);
    if (!is_finite(x)) {
        o->bad = 1;
        return 0;
    }
    return x;
}

/* a block or cell name as a cell name: no spaces, and short enough */
static void cell_name(char *out, const char *in) {
    snprintf(out, SQS_MAX_NAME, "%.63s", in);
    for (char *p = out; *p; p++)
        if (*p == ' ' || *p == '\t') *p = '_';
}

static void took(Imp *im, int id, int *count) {
    if (id <= 0) {
        im->st->skipped++;
        return;
    }
    (*count)++;
    if (!im->in_block) return;
    if (im->nids < SQS_MAX_CELL_MEMBERS) im->ids[im->nids++] = id;
    else im->lost = 1;
}

/* an outline of any length: a shape or a polyline when it fits, else chained
 * polylines of at most SQS_MAX_PTS vertices */
static void put_poly(Imp *im, int lid, const SqsPoint *v, int n, int closed) {
    SqsDxfStats *st = im->st;
    if (n < 2) {
        st->skipped++;
        return;
    }
    if (closed && n >= 3 && n <= SQS_MAX_PTS) {
        int id = sqs_add_shape(im->d, lid, v, n);
        if (id > 0) {
            took(im, id, &st->shapes);
            return;
        }
    } else if (!closed && n <= SQS_MAX_PTS) {
        took(im, sqs_add_pline(im->d, lid, v, n), &st->plines);
        return;
    }
    static SqsPoint tmp[OBJ_PTS + 1];
    memcpy(tmp, v, (size_t)n * sizeof v[0]);
    if (closed) tmp[n++] = v[0];
    for (int at = 0; at < n - 1;) {
        int k = n - at < SQS_MAX_PTS ? n - at : SQS_MAX_PTS;
        took(im, sqs_add_pline(im->d, lid, tmp + at, k), &st->plines);
        at += k - 1;
    }
}

/* faces over points as meshes of at most SQS_MAX_FACES faces and
 * SQS_MAX_PTS points each */
static void put_mesh(Imp *im, int lid, const SqsPoint *p, int np, const int (*fc)[4], int nf) {
    int map[OBJ_PTS];
    for (int at = 0; at < nf;) {
        SqsPoint mp[SQS_MAX_PTS];
        uint8_t mf[SQS_MAX_FACES][4];
        int n = 0, k = 0;
        for (int i = 0; i < np; i++) map[i] = -1;
        while (at < nf && k < SQS_MAX_FACES) {
            const int *f = fc[at];
            int ok = 1, need = 0;
            for (int j = 0; j < 4; j++) {
                if (f[j] < 0 || f[j] >= np) ok = 0;
                else if (map[f[j]] < 0) {
                    int seen = 0;
                    for (int q = 0; q < j; q++) seen |= f[q] == f[j];
                    need += !seen;
                }
            }
            if (!ok) {
                im->st->skipped++;
                at++;
                continue;
            }
            if (n + need > SQS_MAX_PTS) break;
            for (int j = 0; j < 4; j++) {
                if (map[f[j]] < 0) map[f[j]] = n, mp[n++] = p[f[j]];
                mf[k][j] = (uint8_t)map[f[j]];
            }
            k++;
            at++;
        }
        if (k > 0) {
            /* a lone triangle has three points: pad so the mesh is valid */
            while (n < 3) mp[n] = mp[n - 1], n++;
            took(im, sqs_add_mesh(im->d, lid, mp, n, (const uint8_t(*)[4])mf, k), &im->st->meshes);
        }
    }
}

/* the end of a POLYLINE, a polyface or a run of 3DFACEs */
static void end_poly(Imp *im) {
    if (!im->poly && !im->face_run) return;
    int lid = level_id_by_name(im->d, im->poly_layer);
    if (im->nfc > 0) put_mesh(im, lid, im->pv, im->npv, (const int(*)[4])im->fc, im->nfc);
    else if (im->poly) put_poly(im, lid, im->pv, im->npv, im->closed);
    if (im->over) im->st->skipped++;
    im->poly = im->face_run = im->over = 0;
    im->npv = im->nfc = 0;
}

/* a point of the polyface or face run, shared when it is already there */
static int vertex(Imp *im, SqsPoint p, int share) {
    if (share)
        for (int i = 0; i < im->npv; i++)
            if (im->pv[i].x == p.x && im->pv[i].y == p.y && im->pv[i].z == p.z) return i;
    if (im->npv >= OBJ_PTS) return -1;
    im->pv[im->npv] = p;
    return im->npv++;
}

static void finish(Imp *im, Obj *o) {
    SqsDesign *d = im->d;
    SqsDxfStats *st = im->st;
    if (!o->type[0]) return;
    /* a run of 3DFACEs ends at anything else, or at another level */
    if (im->face_run && (!eqi(o->type, "3DFACE") || strcmp(o->layer, im->poly_layer) != 0)) end_poly(im);
    if (eqi(o->type, "SECTION")) {
        snprintf(im->section, sizeof im->section, "%.15s", o->name);
        return;
    }
    if (eqi(o->type, "ENDSEC")) {
        end_poly(im);
        im->section[0] = 0;
        im->in_layers = 0;
        return;
    }
    if (eqi(im->section, "TABLES")) {
        if (eqi(o->type, "TABLE")) im->in_layers = eqi(o->name, "LAYER");
        else if (eqi(o->type, "ENDTAB")) im->in_layers = 0;
        else if (im->in_layers && eqi(o->type, "LAYER") && o->name[0]) {
            int known = sqs_level_id(d, o->name) >= 0;
            int id = level_id_by_name(d, o->name);
            if (id < 0) return;
            if (!known) st->layers++;
            for (int i = 0; i < d->level_count; i++)
                if (d->levels[i].id == id) {
                    int c = o->color < 0 ? -o->color : o->color;
                    if (c >= 1 && c <= 7) d->levels[i].color = BASIC[c];
                    /* a negative colour means off; a frozen layer is written off too */
                    d->levels[i].frozen = (o->flags & 1) != 0;
                    d->levels[i].visible = o->color >= 0 || d->levels[i].frozen;
                }
        }
        return;
    }
    int blocks = eqi(im->section, "BLOCKS");
    if (!blocks && !eqi(im->section, "ENTITIES")) return;
    if (blocks && eqi(o->type, "BLOCK")) {
        end_poly(im);
        im->in_block = 1;
        im->nids = im->lost = 0;
        cell_name(im->block, o->name);
        /* *Model_Space, *Paper_Space and anonymous blocks are not cells */
        im->skip_block = o->name[0] == 0 || o->name[0] == '*' || sqs_find_cell(d, im->block) != NULL;
        im->base = o->p[0];
        return;
    }
    if (blocks && eqi(o->type, "ENDBLK")) {
        end_poly(im);
        if (im->in_block && !im->skip_block) {
            if (im->nids > 0 && sqs_define_cell(d, im->block, im->base, im->ids, im->nids) > 0) st->cells++;
            else st->skipped++;
            if (im->lost) st->skipped++;
        }
        im->in_block = 0;
        return;
    }
    if (blocks && !im->in_block) return;
    if (im->in_block && im->skip_block) return;

    if (eqi(o->type, "VERTEX")) {
        if (!im->poly) return;
        if (o->bad) {
            im->over = 1;
            return;
        }
        if (o->flags & 16) return; /* a spline frame point */
        if ((o->flags & 128) && !(o->flags & 64)) {
            /* a polyface face: 1-based point numbers, negative for a hidden edge */
            if (im->nfc >= OBJ_FACES) {
                im->over = 1;
                return;
            }
            int *f = im->fc[im->nfc++];
            for (int j = 0; j < 4; j++) f[j] = abs(o->vi[j]) - 1;
            if (f[3] < 0) f[3] = f[2];
            return;
        }
        if (vertex(im, o->p[0], 0) < 0) im->over = 1;
        return;
    }
    if (eqi(o->type, "SEQEND")) {
        end_poly(im);
        return;
    }
    if (im->poly) end_poly(im); /* a POLYLINE without its SEQEND */
    if (o->bad) {
        st->skipped++;
        return;
    }
    int lid = level_id_by_name(d, o->layer);
    if (lid < 0) {
        st->skipped++;
        return;
    }
    if (eqi(o->type, "LINE")) {
        took(im, sqs_add_line(d, lid, o->p[0], o->p[1]), &st->lines);
    } else if (eqi(o->type, "CIRCLE")) {
        took(im, sqs_add_arc(d, lid, o->p[0], o->r, 0, 360), &st->circles);
    } else if (eqi(o->type, "ARC")) {
        double sweep = (o->has_a1 ? o->a1 : 360) - o->a0;
        sweep = fmod(sweep, 360.0);
        if (sweep <= 0) sweep += 360.0;
        took(im, sqs_add_arc(d, lid, o->p[0], o->r, o->a0, sweep), &st->arcs);
    } else if (eqi(o->type, "TEXT") || eqi(o->type, "MTEXT")) {
        took(im, sqs_add_text(d, lid, o->p[0], o->text), &st->texts);
    } else if (eqi(o->type, "LWPOLYLINE")) {
        put_poly(im, lid, o->pts, o->npts, o->flags & 1);
        if (o->over) st->skipped++;
    } else if (eqi(o->type, "POLYLINE")) {
        if (o->flags & 16) { /* an M x N polygon mesh */
            st->skipped++;
            return;
        }
        im->poly = 1;
        im->closed = o->flags & 1;
        im->npv = im->nfc = im->over = 0;
        snprintf(im->poly_layer, sizeof im->poly_layer, "%s", o->layer);
    } else if (eqi(o->type, "3DFACE")) {
        if (!im->face_run) {
            im->face_run = 1;
            im->npv = im->nfc = im->over = 0;
            snprintf(im->poly_layer, sizeof im->poly_layer, "%s", o->layer);
        }
        int f[4];
        for (int j = 0; j < 4; j++) f[j] = vertex(im, o->p[j], 1);
        if (f[0] < 0 || f[1] < 0 || f[2] < 0 || f[3] < 0 || im->nfc >= OBJ_FACES) {
            end_poly(im); /* full: what is gathered goes out, this face is lost */
            st->skipped++;
            return;
        }
        memcpy(im->fc[im->nfc++], f, sizeof f);
    } else if (eqi(o->type, "INSERT")) {
        char nm[SQS_MAX_NAME];
        cell_name(nm, o->name);
        took(im, sqs_place_cell(d, lid, nm, o->p[0], o->sx, o->rot), &st->placements);
    } else {
        st->skipped++;
    }
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

    static Imp im; /* about 16 KB: off the stack */
    static Obj o;
    memset(&im, 0, sizeof im);
    memset(&o, 0, sizeof o);
    im.d = d;
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
        else if (c == 1 || (c == 3 && eqi(o.type, "MTEXT"))) {
            /* MTEXT comes in pieces: 3s, then the last in 1 */
            size_t n = strlen(o.text);
            if (!eqi(o.type, "MTEXT")) n = 0;
            for (const char *q = v; *q && n + 1 < sizeof o.text; q++) o.text[n++] = *q;
            o.text[n] = 0;
        } else if (c == 10 && eqi(o.type, "LWPOLYLINE")) {
            if (o.npts < OBJ_PTS) o.pts[o.npts++] = (SqsPoint){num(&o, v), 0, 0};
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
        else if (c >= 71 && c <= 74) o.vi[c - 71] = atoi(v);
    }
    finish(&im, &o);
    end_poly(&im);
    fclose(f);
    if (stats_out) *stats_out = st;
    return 0;
}

/* the nearest of the seven basic DXF colours to 0xRRGGBB */
static int aci(uint32_t rgb) {
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

static void polyline(FILE *f, const char *lay, const SqsPoint *p, int n, int closed) {
    /* a 3D polyline keeps z on every vertex */
    fprintf(f, "0\nPOLYLINE\n8\n%s\n66\n1\n10\n0\n20\n0\n30\n0\n70\n%d\n", lay, 8 | (closed ? 1 : 0));
    for (int i = 0; i < n; i++)
        fprintf(f, "0\nVERTEX\n8\n%s\n10\n%.15g\n20\n%.15g\n30\n%.15g\n70\n32\n", lay, p[i].x, p[i].y, p[i].z);
    fprintf(f, "0\nSEQEND\n8\n%s\n", lay);
}

/* a polyface mesh: the points, then each face by 1-based point numbers */
static void polyface(FILE *f, const char *lay, const SqsPoint *p, int np, const uint8_t (*fc)[4], int nf) {
    fprintf(f, "0\nPOLYLINE\n8\n%s\n66\n1\n10\n0\n20\n0\n30\n0\n70\n64\n71\n%d\n72\n%d\n", lay, np, nf);
    for (int i = 0; i < np; i++)
        fprintf(f, "0\nVERTEX\n8\n%s\n10\n%.15g\n20\n%.15g\n30\n%.15g\n70\n192\n", lay, p[i].x, p[i].y, p[i].z);
    for (int k = 0; k < nf; k++) {
        fprintf(f, "0\nVERTEX\n8\n%s\n10\n0\n20\n0\n30\n0\n70\n128\n71\n%d\n72\n%d\n73\n%d\n", lay, fc[k][0] + 1,
                fc[k][1] + 1, fc[k][2] + 1);
        if (fc[k][3] != fc[k][2]) fprintf(f, "74\n%d\n", fc[k][3] + 1);
    }
    fprintf(f, "0\nSEQEND\n8\n%s\n", lay);
}

static void write_el(FILE *f, const SqsDesign *d, const SqsElement *e) {
    const SqsLevel *lv = sqs_level(d, e->level_id);
    const char *lay = lv ? lv->name : "0";
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
        polyface(f, lay, e->pts, e->npts, (const uint8_t(*)[4])e->faces, e->nfaces);
        break;
    case SQS_SOLID: {
        /* R12 has no solids: the solid's surface as a polyface mesh */
        SqsPoint p[64];
        uint8_t fc[64][4];
        int np, nf = sqs_solid_mesh(e, p, 64, &np, fc, 64);
        if (nf > 0) polyface(f, lay, p, np, (const uint8_t(*)[4])fc, nf);
        break;
    }
    case SQS_CELLREF:
        fprintf(f, "0\nINSERT\n8\n%s\n2\n%s\n", lay, e->text);
        fprintf(f, "10\n%.15g\n20\n%.15g\n30\n%.15g\n", e->a.x, e->a.y, e->a.z);
        fprintf(f, "41\n%.15g\n42\n%.15g\n43\n%.15g\n50\n%.15g\n", e->scale, e->scale, e->scale, e->rot_deg);
        break;
    default:
        break;
    }
}

/* Levels as layers with their colours (negative when hidden, flag 1 when
 * frozen), cells as BLOCKS, then the elements: lines, arcs and circles,
 * text, 3D polylines for polylines, shapes and curves, polyface meshes for
 * meshes and solids, and INSERTs for placed cells. R12 (AC1009) ASCII DXF. */
int sqs_export_dxf(const SqsDesign *d, const char *path) {
    /* binary so MinGW/Windows keep canonical LF (text mode would emit CRLF) */
    FILE *f = fopen(path, "wb");
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

    fprintf(f, "0\nSECTION\n2\nBLOCKS\n");
    for (int i = 0; i < d->cell_count; i++) {
        const SqsCell *c = &d->cells[i];
        fprintf(f, "0\nBLOCK\n8\n0\n2\n%s\n70\n0\n10\n%.15g\n20\n%.15g\n30\n%.15g\n3\n%s\n", c->name, c->origin.x,
                c->origin.y, c->origin.z, c->name);
        for (int j = 0; j < c->member_count; j++) {
            const SqsElement *m = sqs_find(d, c->member_ids[j]);
            if (m) write_el(f, d, m);
        }
        fprintf(f, "0\nENDBLK\n8\n0\n");
    }
    fprintf(f, "0\nENDSEC\n");

    fprintf(f, "0\nSECTION\n2\nENTITIES\n");
    for (int i = 0; i < d->element_count; i++)
        if (!sqs_in_cell(d, d->elements[i].id)) write_el(f, d, &d->elements[i]);
    fprintf(f, "0\nENDSEC\n0\nEOF\n");
    int bad = ferror(f);
    bad |= fclose(f) != 0;
    return bad ? -1 : 0;
}
