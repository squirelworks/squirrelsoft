#include "sqc_dxf.h"
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
    puts("commands:");
    puts("  new LINE CIRCLE ARC PLINE TEXT LAYER BLOCK INSERT LIST LEN");
    puts("  IMPORT-DXF <out.sqdwg> <in.dxf>   # AutoCAD bridge");
    puts("  EXPORT-DXF <in.sqdwg> <out.dxf>");
    puts("  PROBE <file>                     # detect dxf/dwg/sqdwg");
    puts("  help");
    puts("");
    puts("AutoCAD .dwg: SAVEAS → DXF, then IMPORT-DXF.");
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

    if (strcmp(cmd, "PROBE") == 0 || strcmp(cmd, "probe") == 0) {
        if (argc < 3) {
            usage();
            free(d);
            return 1;
        }
        const char *path = argv[2];
        free(d);
        if (sqc_file_looks_like_dxf(path)) {
            puts("format=dxf (ASCII) — use: squirrelcad IMPORT-DXF out.sqdwg this.dxf");
            return 0;
        }
        if (sqc_file_looks_like_dwg(path)) {
            puts("format=dwg (binary AutoCAD) — not readable directly.");
            puts("In AutoCAD: SAVEAS → AutoCAD DXF (*.dxf), then IMPORT-DXF.");
            return 2;
        }
        FILE *f = fopen(path, "r");
        if (!f) {
            fprintf(stderr, "cannot open %s\n", path);
            return 1;
        }
        char line[64];
        if (fgets(line, sizeof(line), f) && strncmp(line, "SQDWG", 5) == 0) {
            puts("format=sqdwg");
            fclose(f);
            return 0;
        }
        fclose(f);
        puts("format=unknown");
        return 1;
    }

    if (strcmp(cmd, "IMPORT-DXF") == 0 || strcmp(cmd, "import-dxf") == 0) {
        if (argc < 4) {
            usage();
            free(d);
            return 1;
        }
        sqc_drawing_init(d, "from-dxf");
        SqcDxfStats st;
        int irc = sqc_import_dxf(d, argv[3], &st);
        if (irc != 0) {
            free(d);
            return 1;
        }
        if (save(d, argv[2]) != 0) {
            free(d);
            return 1;
        }
        printf("imported → %s\n", argv[2]);
        printf("  layers=%d lines=%d circles=%d arcs=%d texts=%d plines=%d skipped=%d\n",
               st.layers, st.lines, st.circles, st.arcs, st.texts, st.plines, st.skipped);
        free(d);
        return 0;
    }

    if (strcmp(cmd, "EXPORT-DXF") == 0 || strcmp(cmd, "export-dxf") == 0) {
        if (argc < 4) {
            usage();
            free(d);
            return 1;
        }
        if (load(d, argv[2]) != 0) {
            free(d);
            return 1;
        }
        if (sqc_export_dxf(d, argv[3]) != 0) {
            fprintf(stderr, "export failed\n");
            free(d);
            return 1;
        }
        printf("exported %s → %s (%d ents)\n", argv[2], argv[3], d->ent_count);
        free(d);
        return 0;
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
