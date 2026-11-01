#include "sqs_dxf.h"
#include "sqstation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_point(const char *s, SqsPoint *p) {
    return sscanf(s, "%lf,%lf,%lf", &p->x, &p->y, &p->z) == 3;
}

static void usage(void) {
    puts("SquirrelStation — MicroStation-class design core");
    puts("usage:");
    puts("  squirrelstation new <file.sqdgn> [title]");
    puts("  squirrelstation line <file> x,y,z x,y,z");
    puts("  squirrelstation arc <file> cx,cy,cz radius start_deg sweep_deg");
    puts("  squirrelstation text <file> x,y,z <string>");
    puts("  squirrelstation level <file> <name> [color_hex]");
    puts("  squirrelstation list <file>");
    puts("  squirrelstation snap <file> x,y,z [tol]");
    puts("  squirrelstation import-dxf <out.sqdgn> <in.dxf>   # MicroStation bridge");
    puts("  squirrelstation export-dxf <in.sqdgn> <out.dxf>");
    puts("  squirrelstation probe <file>                     # detect dxf/dgn/sqdgn");
    puts("");
    puts("MicroStation .dgn: export DXF from MicroStation, then import-dxf.");
}

static int load(SqsDesign *d, const char *path) {
    if (sqs_read_sqdgn(d, path) != 0) {
        fprintf(stderr, "failed to read %s\n", path);
        return -1;
    }
    return 0;
}

static int save(const SqsDesign *d, const char *path) {
    if (sqs_write_sqdgn(d, path) != 0) {
        fprintf(stderr, "failed to write %s\n", path);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0) {
        usage();
        return 0;
    }
    if (strcmp(cmd, "new") == 0) {
        if (argc < 3) {
            usage();
            return 1;
        }
        SqsDesign d;
        sqs_design_init(&d, argc >= 4 ? argv[3] : "untitled");
        return save(&d, argv[2]) == 0 ? 0 : 1;
    }

    if (strcmp(cmd, "probe") == 0) {
        if (argc < 3) {
            usage();
            return 1;
        }
        const char *path = argv[2];
        if (sqs_file_looks_like_dxf(path)) {
            puts("format=dxf (ASCII) — use: squirrelstation import-dxf out.sqdgn this.dxf");
            return 0;
        }
        if (sqs_file_looks_like_dgn(path)) {
            puts("format=dgn (binary MicroStation) — not readable directly.");
            puts("In MicroStation: File → Export → DXF, then import-dxf.");
            return 2;
        }
        FILE *f = fopen(path, "r");
        if (!f) {
            fprintf(stderr, "cannot open %s\n", path);
            return 1;
        }
        char line[64];
        if (fgets(line, sizeof(line), f) && strncmp(line, "SQDGN", 5) == 0) {
            puts("format=sqdgn");
            fclose(f);
            return 0;
        }
        fclose(f);
        puts("format=unknown");
        return 1;
    }

    if (strcmp(cmd, "import-dxf") == 0) {
        if (argc < 4) {
            usage();
            return 1;
        }
        SqsDesign d;
        sqs_design_init(&d, "from-dxf");
        SqsDxfStats st;
        int rc = sqs_import_dxf(&d, argv[3], &st);
        if (rc != 0) return 1;
        if (save(&d, argv[2]) != 0) return 1;
        printf("imported → %s\n", argv[2]);
        printf("  layers=%d lines=%d arcs=%d circles=%d texts=%d pline_segs=%d skipped=%d\n",
               st.layers, st.lines, st.arcs, st.circles, st.texts, st.pline_segs, st.skipped);
        return 0;
    }

    if (strcmp(cmd, "export-dxf") == 0) {
        if (argc < 4) {
            usage();
            return 1;
        }
        SqsDesign d;
        if (load(&d, argv[2]) != 0) return 1;
        if (sqs_export_dxf(&d, argv[3]) != 0) {
            fprintf(stderr, "export failed\n");
            return 1;
        }
        printf("exported %s → %s (%d elements)\n", argv[2], argv[3], d.element_count);
        return 0;
    }

    if (argc < 3) {
        usage();
        return 1;
    }
    SqsDesign d;
    if (load(&d, argv[2]) != 0) return 1;
    int level = d.levels[0].id;

    if (strcmp(cmd, "line") == 0) {
        SqsPoint a, b;
        if (argc < 5 || !parse_point(argv[3], &a) || !parse_point(argv[4], &b)) {
            usage();
            return 1;
        }
        int id = sqs_add_line(&d, level, a, b);
        printf("line id=%d\n", id);
        return save(&d, argv[2]) == 0 ? 0 : 1;
    }
    if (strcmp(cmd, "arc") == 0) {
        SqsPoint c;
        double r, s, w;
        if (argc < 7 || !parse_point(argv[3], &c)) {
            usage();
            return 1;
        }
        r = atof(argv[4]);
        s = atof(argv[5]);
        w = atof(argv[6]);
        int id = sqs_add_arc(&d, level, c, r, s, w);
        printf("arc id=%d\n", id);
        return save(&d, argv[2]) == 0 ? 0 : 1;
    }
    if (strcmp(cmd, "text") == 0) {
        SqsPoint p;
        if (argc < 5 || !parse_point(argv[3], &p)) {
            usage();
            return 1;
        }
        int id = sqs_add_text(&d, level, p, argv[4]);
        printf("text id=%d\n", id);
        return save(&d, argv[2]) == 0 ? 0 : 1;
    }
    if (strcmp(cmd, "level") == 0) {
        if (argc < 4) {
            usage();
            return 1;
        }
        uint32_t color = 0x00FF00;
        if (argc >= 5) color = (uint32_t)strtoul(argv[4], NULL, 0);
        int id = sqs_add_level(&d, argv[3], color);
        printf("level id=%d\n", id);
        return save(&d, argv[2]) == 0 ? 0 : 1;
    }
    if (strcmp(cmd, "list") == 0) {
        printf("design: %s  elements=%d levels=%d cells=%d\n", d.title, d.element_count,
               d.level_count, d.cell_count);
        for (int i = 0; i < d.level_count; i++)
            printf("  level %d %s vis=%d\n", d.levels[i].id, d.levels[i].name,
                   d.levels[i].visible);
        for (int i = 0; i < d.element_count; i++) {
            const SqsElement *e = &d.elements[i];
            if (e->type == SQS_LINE)
                printf("  LINE %d L%d (%.3f,%.3f,%.3f)-(%.3f,%.3f,%.3f)\n", e->id,
                       e->level_id, e->a.x, e->a.y, e->a.z, e->b.x, e->b.y, e->b.z);
            else if (e->type == SQS_ARC)
                printf("  ARC %d L%d c=(%.3f,%.3f,%.3f) r=%.3f\n", e->id, e->level_id,
                       e->a.x, e->a.y, e->a.z, e->radius);
            else if (e->type == SQS_TEXT)
                printf("  TEXT %d L%d (%.3f,%.3f,%.3f) \"%s\"\n", e->id, e->level_id,
                       e->a.x, e->a.y, e->a.z, e->text);
        }
        return 0;
    }
    if (strcmp(cmd, "snap") == 0) {
        SqsPoint c;
        double tol = 1.0;
        if (argc < 4 || !parse_point(argv[3], &c)) {
            usage();
            return 1;
        }
        if (argc >= 5) tol = atof(argv[4]);
        SqsSnapResult s = sqs_accu_snap(&d, c, tol);
        const char *k = "none";
        if (s.kind == SQS_SNAP_ENDPOINT) k = "endpoint";
        else if (s.kind == SQS_SNAP_MIDPOINT) k = "midpoint";
        else if (s.kind == SQS_SNAP_CENTER) k = "center";
        else if (s.kind == SQS_SNAP_INTERSECTION) k = "intersection";
        printf("snap=%s elem=%d dist=%.6f -> %.6f,%.6f,%.6f\n", k, s.element_id,
               s.distance, s.p.x, s.p.y, s.p.z);
        return 0;
    }
    usage();
    return 1;
}
