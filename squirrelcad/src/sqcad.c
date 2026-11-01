#include "sqcad.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void sqc_drawing_init(SqcDrawing *dwg, const char *title) {
    memset(dwg, 0, sizeof(*dwg));
    snprintf(dwg->title, sizeof(dwg->title), "%s", title ? title : "Drawing1");
    dwg->next_id = 1;
    sqc_add_layer(dwg, "0", 7);
    dwg->current_layer = dwg->layers[0].id;
}

int sqc_add_layer(SqcDrawing *dwg, const char *name, uint32_t color) {
    if (dwg->layer_count >= SQC_MAX_LAYERS) return -1;
    for (int i = 0; i < dwg->layer_count; i++)
        if (strcmp(dwg->layers[i].name, name) == 0) return dwg->layers[i].id;
    SqcLayer *L = &dwg->layers[dwg->layer_count++];
    L->id = dwg->next_id++;
    snprintf(L->name, sizeof(L->name), "%s", name);
    L->on = 1;
    L->color = color;
    return L->id;
}

int sqc_set_layer(SqcDrawing *dwg, const char *name) {
    for (int i = 0; i < dwg->layer_count; i++) {
        if (strcmp(dwg->layers[i].name, name) == 0) {
            dwg->current_layer = dwg->layers[i].id;
            return dwg->current_layer;
        }
    }
    return -1;
}

static SqcEntity *push_ent(SqcDrawing *dwg, SqcEntType t) {
    if (dwg->ent_count >= SQC_MAX_ENTS) return NULL;
    SqcEntity *e = &dwg->ents[dwg->ent_count++];
    memset(e, 0, sizeof(*e));
    e->id = dwg->next_id++;
    e->type = t;
    e->layer_id = dwg->current_layer;
    e->scale = 1.0;
    return e;
}

int sqc_line(SqcDrawing *dwg, SqcPoint a, SqcPoint b) {
    SqcEntity *e = push_ent(dwg, SQC_ENT_LINE);
    if (!e) return -1;
    e->p1 = a;
    e->p2 = b;
    return e->id;
}

int sqc_circle(SqcDrawing *dwg, SqcPoint c, double r) {
    SqcEntity *e = push_ent(dwg, SQC_ENT_CIRCLE);
    if (!e) return -1;
    e->p1 = c;
    e->radius = r;
    return e->id;
}

int sqc_arc(SqcDrawing *dwg, SqcPoint c, double r, double start_deg, double end_deg) {
    SqcEntity *e = push_ent(dwg, SQC_ENT_ARC);
    if (!e) return -1;
    e->p1 = c;
    e->radius = r;
    e->start_ang = start_deg;
    e->end_ang = end_deg;
    return e->id;
}

int sqc_pline(SqcDrawing *dwg, const SqcPoint *verts, int n) {
    if (n < 2 || n > SQC_MAX_PLINE) return -1;
    SqcEntity *e = push_ent(dwg, SQC_ENT_PLINE);
    if (!e) return -1;
    e->vert_count = n;
    for (int i = 0; i < n; i++) e->verts[i] = verts[i];
    e->p1 = verts[0];
    e->p2 = verts[n - 1];
    return e->id;
}

int sqc_text(SqcDrawing *dwg, SqcPoint p, const char *text) {
    SqcEntity *e = push_ent(dwg, SQC_ENT_TEXT);
    if (!e) return -1;
    e->p1 = p;
    snprintf(e->text, sizeof(e->text), "%s", text ? text : "");
    return e->id;
}

int sqc_block_def(SqcDrawing *dwg, const char *name, SqcPoint base, const int *ids, int n) {
    if (dwg->block_count >= 128 || n > SQC_MAX_BLOCK_ENTS) return -1;
    SqcBlock *b = &dwg->blocks[dwg->block_count++];
    memset(b, 0, sizeof(*b));
    snprintf(b->name, sizeof(b->name), "%s", name);
    b->base = base;
    b->ent_count = n;
    for (int i = 0; i < n; i++) b->ent_ids[i] = ids[i];
    return dwg->block_count;
}

int sqc_insert(SqcDrawing *dwg, const char *block, SqcPoint at, double scale, double rot_deg) {
    int found = 0;
    for (int i = 0; i < dwg->block_count; i++)
        if (strcmp(dwg->blocks[i].name, block) == 0) found = 1;
    if (!found) return -1;
    SqcEntity *e = push_ent(dwg, SQC_ENT_INSERT);
    if (!e) return -1;
    e->p1 = at;
    e->scale = scale;
    e->rotation_deg = rot_deg;
    snprintf(e->block_name, sizeof(e->block_name), "%s", block);
    return e->id;
}

double sqc_ent_length(const SqcEntity *e) {
    if (e->type == SQC_ENT_LINE) {
        double dx = e->p2.x - e->p1.x, dy = e->p2.y - e->p1.y, dz = e->p2.z - e->p1.z;
        return sqrt(dx * dx + dy * dy + dz * dz);
    }
    if (e->type == SQC_ENT_CIRCLE) return 2.0 * M_PI * e->radius;
    if (e->type == SQC_ENT_ARC) {
        double sweep = fabs(e->end_ang - e->start_ang) * M_PI / 180.0;
        return e->radius * sweep;
    }
    if (e->type == SQC_ENT_PLINE) {
        double len = 0;
        for (int i = 1; i < e->vert_count; i++) {
            double dx = e->verts[i].x - e->verts[i - 1].x;
            double dy = e->verts[i].y - e->verts[i - 1].y;
            double dz = e->verts[i].z - e->verts[i - 1].z;
            len += sqrt(dx * dx + dy * dy + dz * dz);
        }
        return len;
    }
    return 0.0;
}

int sqc_write_sqdwg(const SqcDrawing *dwg, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "SQDWG 1\n");
    fprintf(f, "TITLE %s\n", dwg->title);
    fprintf(f, "CURLAYER %d\n", dwg->current_layer);
    fprintf(f, "NEXTID %d\n", dwg->next_id);
    for (int i = 0; i < dwg->layer_count; i++) {
        const SqcLayer *L = &dwg->layers[i];
        fprintf(f, "LAYER %d %s %d %u\n", L->id, L->name, L->on, L->color);
    }
    for (int i = 0; i < dwg->ent_count; i++) {
        const SqcEntity *e = &dwg->ents[i];
        switch (e->type) {
        case SQC_ENT_LINE:
            fprintf(f, "LINE %d %d %.17g %.17g %.17g %.17g %.17g %.17g\n", e->id,
                    e->layer_id, e->p1.x, e->p1.y, e->p1.z, e->p2.x, e->p2.y, e->p2.z);
            break;
        case SQC_ENT_CIRCLE:
            fprintf(f, "CIRCLE %d %d %.17g %.17g %.17g %.17g\n", e->id, e->layer_id,
                    e->p1.x, e->p1.y, e->p1.z, e->radius);
            break;
        case SQC_ENT_ARC:
            fprintf(f, "ARC %d %d %.17g %.17g %.17g %.17g %.17g %.17g\n", e->id,
                    e->layer_id, e->p1.x, e->p1.y, e->p1.z, e->radius, e->start_ang,
                    e->end_ang);
            break;
        case SQC_ENT_PLINE:
            fprintf(f, "PLINE %d %d %d", e->id, e->layer_id, e->vert_count);
            for (int v = 0; v < e->vert_count; v++)
                fprintf(f, " %.17g %.17g %.17g", e->verts[v].x, e->verts[v].y,
                        e->verts[v].z);
            fprintf(f, "\n");
            break;
        case SQC_ENT_TEXT:
            fprintf(f, "TEXT %d %d %.17g %.17g %.17g %s\n", e->id, e->layer_id, e->p1.x,
                    e->p1.y, e->p1.z, e->text);
            break;
        case SQC_ENT_INSERT:
            fprintf(f, "INSERT %d %d %s %.17g %.17g %.17g %.17g %.17g\n", e->id,
                    e->layer_id, e->block_name, e->p1.x, e->p1.y, e->p1.z, e->scale,
                    e->rotation_deg);
            break;
        }
    }
    for (int i = 0; i < dwg->block_count; i++) {
        const SqcBlock *b = &dwg->blocks[i];
        fprintf(f, "BLOCK %s %.17g %.17g %.17g %d", b->name, b->base.x, b->base.y,
                b->base.z, b->ent_count);
        for (int j = 0; j < b->ent_count; j++) fprintf(f, " %d", b->ent_ids[j]);
        fprintf(f, "\n");
    }
    fprintf(f, "END\n");
    fclose(f);
    return 0;
}

int sqc_read_sqdwg(SqcDrawing *dwg, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    memset(dwg, 0, sizeof(*dwg));
    char line[1024];
    if (!fgets(line, sizeof(line), f) || strncmp(line, "SQDWG", 5) != 0) {
        fclose(f);
        return -1;
    }
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "END", 3) == 0) break;
        if (strncmp(line, "TITLE ", 6) == 0) sscanf(line + 6, "%63s", dwg->title);
        else if (strncmp(line, "CURLAYER ", 9) == 0)
            sscanf(line + 9, "%d", &dwg->current_layer);
        else if (strncmp(line, "NEXTID ", 7) == 0)
            sscanf(line + 7, "%d", &dwg->next_id);
        else if (strncmp(line, "LAYER ", 6) == 0) {
            SqcLayer *L = &dwg->layers[dwg->layer_count++];
            sscanf(line + 6, "%d %63s %d %u", &L->id, L->name, &L->on, &L->color);
        } else if (strncmp(line, "LINE ", 5) == 0) {
            SqcEntity *e = &dwg->ents[dwg->ent_count++];
            memset(e, 0, sizeof(*e));
            e->type = SQC_ENT_LINE;
            e->scale = 1.0;
            sscanf(line + 5, "%d %d %lf %lf %lf %lf %lf %lf", &e->id, &e->layer_id,
                   &e->p1.x, &e->p1.y, &e->p1.z, &e->p2.x, &e->p2.y, &e->p2.z);
        } else if (strncmp(line, "CIRCLE ", 7) == 0) {
            SqcEntity *e = &dwg->ents[dwg->ent_count++];
            memset(e, 0, sizeof(*e));
            e->type = SQC_ENT_CIRCLE;
            e->scale = 1.0;
            sscanf(line + 7, "%d %d %lf %lf %lf %lf", &e->id, &e->layer_id, &e->p1.x,
                   &e->p1.y, &e->p1.z, &e->radius);
        } else if (strncmp(line, "ARC ", 4) == 0) {
            SqcEntity *e = &dwg->ents[dwg->ent_count++];
            memset(e, 0, sizeof(*e));
            e->type = SQC_ENT_ARC;
            e->scale = 1.0;
            sscanf(line + 4, "%d %d %lf %lf %lf %lf %lf %lf", &e->id, &e->layer_id,
                   &e->p1.x, &e->p1.y, &e->p1.z, &e->radius, &e->start_ang, &e->end_ang);
        } else if (strncmp(line, "TEXT ", 5) == 0) {
            SqcEntity *e = &dwg->ents[dwg->ent_count++];
            memset(e, 0, sizeof(*e));
            e->type = SQC_ENT_TEXT;
            e->scale = 1.0;
            sscanf(line + 5, "%d %d %lf %lf %lf %63s", &e->id, &e->layer_id, &e->p1.x,
                   &e->p1.y, &e->p1.z, e->text);
        } else if (strncmp(line, "INSERT ", 7) == 0) {
            SqcEntity *e = &dwg->ents[dwg->ent_count++];
            memset(e, 0, sizeof(*e));
            e->type = SQC_ENT_INSERT;
            sscanf(line + 7, "%d %d %63s %lf %lf %lf %lf %lf", &e->id, &e->layer_id,
                   e->block_name, &e->p1.x, &e->p1.y, &e->p1.z, &e->scale,
                   &e->rotation_deg);
        } else if (strncmp(line, "PLINE ", 6) == 0) {
            SqcEntity *e = &dwg->ents[dwg->ent_count++];
            memset(e, 0, sizeof(*e));
            e->type = SQC_ENT_PLINE;
            e->scale = 1.0;
            int n = 0, consumed = 0;
            char *p = line + 6;
            sscanf(p, "%d %d %d%n", &e->id, &e->layer_id, &n, &consumed);
            p += consumed;
            e->vert_count = n;
            for (int i = 0; i < n && i < SQC_MAX_PLINE; i++) {
                int c2 = 0;
                sscanf(p, " %lf %lf %lf%n", &e->verts[i].x, &e->verts[i].y,
                       &e->verts[i].z, &c2);
                p += c2;
            }
            if (n > 0) {
                e->p1 = e->verts[0];
                e->p2 = e->verts[n - 1];
            }
        } else if (strncmp(line, "BLOCK ", 6) == 0) {
            SqcBlock *b = &dwg->blocks[dwg->block_count++];
            memset(b, 0, sizeof(*b));
            int n = 0, consumed = 0;
            char *p = line + 6;
            sscanf(p, "%63s %lf %lf %lf %d%n", b->name, &b->base.x, &b->base.y,
                   &b->base.z, &n, &consumed);
            p += consumed;
            b->ent_count = n;
            for (int i = 0; i < n && i < SQC_MAX_BLOCK_ENTS; i++) {
                int id = 0, c2 = 0;
                sscanf(p, " %d%n", &id, &c2);
                b->ent_ids[i] = id;
                p += c2;
            }
        }
    }
    fclose(f);
    if (dwg->next_id <= 0) dwg->next_id = 1;
    return 0;
}
