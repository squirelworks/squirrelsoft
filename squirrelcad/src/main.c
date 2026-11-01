#include "sqcad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_xy(const char *s, SqcPoint *p) {
    p->z = 0;
    if (sscanf(s, "%lf,%lf,%lf", &p->x, &p->y, &p->z) >= 2) return 1;
    return 0;
}

static void usage(void) {
    puts("SquirrelCAD — AutoCAD-class drafting core");
    puts("commands: new LINE CIRCLE ARC PLINE TEXT LAYER BLOCK INSERT LIST LEN help");
}

static int load(SqcDrawing *d, const char *path) {
    if (sqc_read_sqdwg(d, path) != 0) {
        fprintf(stderr, "cannot read %s\n", path);
        return -1;
    }
    return 0;
}

static int save(const SqcDrawing *d, const char *path) {
    return sqc_write_sqdwg(d, path);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "help") == 0) {
        usage();
        return 0;
    }

    SqcDrawing *d = (SqcDrawing *)calloc(1, sizeof(SqcDrawing));
    if (!d) return 1;

    if (strcmp(cmd, "new") == 0) {
        if (argc < 3) {
            free(d);
            return 1;
        }
        sqc_drawing_init(d, argc >= 4 ? argv[3] : "Drawing1");
        int rc = save(d, argv[2]) == 0 ? 0 : 1;
        free(d);
        return rc;
    }
    if (argc < 3) {
        usage();
        free(d);
        return 1;
    }
    if (load(d, argv[2]) != 0) {
        free(d);
        return 1;
    }

    int rc = 1;
    if (strcmp(cmd, "LINE") == 0 || strcmp(cmd, "line") == 0) {
        SqcPoint a, b;
        if (argc >= 5 && parse_xy(argv[3], &a) && parse_xy(argv[4], &b)) {
            printf("id=%d\n", sqc_line(d, a, b));
            rc = save(d, argv[2]) == 0 ? 0 : 1;
        }
    } else if (strcmp(cmd, "CIRCLE") == 0 || strcmp(cmd, "circle") == 0) {
        SqcPoint c;
        if (argc >= 5 && parse_xy(argv[3], &c)) {
            printf("id=%d\n", sqc_circle(d, c, atof(argv[4])));
            rc = save(d, argv[2]) == 0 ? 0 : 1;
        }
    } else if (strcmp(cmd, "ARC") == 0 || strcmp(cmd, "arc") == 0) {
        SqcPoint c;
        if (argc >= 7 && parse_xy(argv[3], &c)) {
            printf("id=%d\n", sqc_arc(d, c, atof(argv[4]), atof(argv[5]), atof(argv[6])));
            rc = save(d, argv[2]) == 0 ? 0 : 1;
        }
    } else if (strcmp(cmd, "PLINE") == 0 || strcmp(cmd, "pline") == 0) {
        if (argc >= 5) {
            SqcPoint verts[SQC_MAX_PLINE];
            int n = 0;
            for (int i = 3; i < argc && n < SQC_MAX_PLINE; i++) {
                if (!parse_xy(argv[i], &verts[n])) {
                    n = -1;
                    break;
                }
                n++;
            }
            if (n >= 2) {
                printf("id=%d\n", sqc_pline(d, verts, n));
                rc = save(d, argv[2]) == 0 ? 0 : 1;
            }
        }
    } else if (strcmp(cmd, "TEXT") == 0 || strcmp(cmd, "text") == 0) {
        SqcPoint p;
        if (argc >= 5 && parse_xy(argv[3], &p)) {
            printf("id=%d\n", sqc_text(d, p, argv[4]));
            rc = save(d, argv[2]) == 0 ? 0 : 1;
        }
    } else if (strcmp(cmd, "LAYER") == 0 || strcmp(cmd, "layer") == 0) {
        if (argc >= 4) {
            uint32_t color = 7;
            if (argc >= 5) color = (uint32_t)strtoul(argv[4], NULL, 0);
            int id = sqc_add_layer(d, argv[3], color);
            sqc_set_layer(d, argv[3]);
            printf("layer id=%d current=%s\n", id, argv[3]);
            rc = save(d, argv[2]) == 0 ? 0 : 1;
        }
    } else if (strcmp(cmd, "BLOCK") == 0 || strcmp(cmd, "block") == 0) {
        if (argc >= 6) {
            SqcPoint base;
            if (parse_xy(argv[4], &base)) {
                int ids[SQC_MAX_BLOCK_ENTS];
                int n = 0;
                for (int i = 5; i < argc && n < SQC_MAX_BLOCK_ENTS; i++)
                    ids[n++] = atoi(argv[i]);
                printf("block=%d\n", sqc_block_def(d, argv[3], base, ids, n));
                rc = save(d, argv[2]) == 0 ? 0 : 1;
            }
        }
    } else if (strcmp(cmd, "INSERT") == 0 || strcmp(cmd, "insert") == 0) {
        SqcPoint at;
        if (argc >= 5 && parse_xy(argv[4], &at)) {
            double scale = argc >= 6 ? atof(argv[5]) : 1.0;
            double rot = argc >= 7 ? atof(argv[6]) : 0.0;
            int id = sqc_insert(d, argv[3], at, scale, rot);
            if (id < 0) fprintf(stderr, "unknown block\n");
            else {
                printf("id=%d\n", id);
                rc = save(d, argv[2]) == 0 ? 0 : 1;
            }
        }
    } else if (strcmp(cmd, "LIST") == 0 || strcmp(cmd, "list") == 0) {
        printf("drawing %s ents=%d layers=%d blocks=%d\n", d->title, d->ent_count,
               d->layer_count, d->block_count);
        for (int i = 0; i < d->ent_count; i++) {
            const SqcEntity *e = &d->ents[i];
            printf("  #%d type=%d layer=%d\n", e->id, (int)e->type, e->layer_id);
        }
        rc = 0;
    } else if (strcmp(cmd, "LEN") == 0 || strcmp(cmd, "len") == 0) {
        if (argc >= 4) {
            int id = atoi(argv[3]);
            for (int i = 0; i < d->ent_count; i++)
                if (d->ents[i].id == id) {
                    printf("%.9f\n", sqc_ent_length(&d->ents[i]));
                    rc = 0;
                    break;
                }
        }
    } else {
        usage();
    }

    free(d);
    return rc;
}
