#include "sqs_dxf.h"
#include "sqstation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SqsDesign d;                 /* about a megabyte: not on the stack */
static SqsDesign refd[SQS_MAX_REFS]; /* attached references, for snaps */

static void usage(void) {
    puts("SquirrelStation — MicroStation-class design core");
    puts("usage (points: x,y,z or AccuDraw keyins xy=, dl=dx,dy[,dz], di=dist,angle):");
    puts("  squirrelstation new <file.sqdgn|file.sqdgnb> [title]");
    puts("  squirrelstation line <file> P P");
    puts("  squirrelstation arc <file> CENTRE radius start_deg sweep_deg");
    puts("  squirrelstation text <file> P words...");
    puts("  squirrelstation pline|curve|shape <file> P P P ...");
    puts("  squirrelstation mesh <file> P P P [P] ...           (one face: triangle or quad)");
    puts("  squirrelstation solid <file> box|cylinder|sphere ORIGIN SIZE [rot_deg]");
    puts("                    SIZE: box dx,dy,dz; cylinder r,0,h; sphere r,0,0");
    puts("  squirrelstation level <file> <name> [color_hex | on | off | freeze | thaw]");
    puts("  squirrelstation cell <file> <name> ORIGIN id...     (define from elements)");
    puts("  squirrelstation place <file> <name> P [scale [rot_deg]]");
    puts("  squirrelstation attach <file> <ref.sqdgn> [dx,dy,dz [scale]]");
    puts("  squirrelstation list <file>");
    puts("  squirrelstation measure <file> id               (length, area, volume)");
    puts("  squirrelstation snap <file> P [tol]             (with references)");
    puts("  squirrelstation convert <in> <out>              (.sqdgn <-> .sqdgnb)");
    puts("  squirrelstation import-dxf <out.sqdgn> <in.dxf>   # MicroStation bridge");
    puts("  squirrelstation export-dxf <in.sqdgn> <out.dxf>");
    puts("  squirrelstation probe <file>                     # detect dxf/dgn/sqdgn");
    puts("Drawing commands take --level NAME (default: the first level).");
    puts("");
    puts("MicroStation .dgn: export DXF from MicroStation, then import-dxf.");
}

static int ends_with(const char *s, const char *tail) {
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && strcmp(s + a - b, tail) == 0;
}

static int load(SqsDesign *dd, const char *path) {
    int rc = sqs_read_any(dd, path);
    if (rc != 0) {
        fprintf(stderr, rc == -2 ? "%s is damaged\n" : "failed to read %s\n", path);
        return -1;
    }
    return 0;
}

static int save(const SqsDesign *dd, const char *path) {
    int rc = ends_with(path, ".sqdgnb") ? sqs_write_sqdgnb(dd, path) : sqs_write_sqdgn(dd, path);
    if (rc != 0) {
        fprintf(stderr, "failed to write %s\n", path);
        return -1;
    }
    return 0;
}

/* points in order: the first absolute (or xy=), each later one may be dl= or di= from the one before */
static int points(char **args, int n, SqsPoint *out) {
    SqsPoint last = {0, 0, 0};
    for (int i = 0; i < n; i++) {
        if (sqs_keyin(args[i], last, &out[i])) {
            fprintf(stderr, "bad point: %s\n", args[i]);
            return -1;
        }
        last = out[i];
    }
    return 0;
}

static const char *kind_name(SqsElementType t) {
    switch (t) {
    case SQS_LINE: return "LINE";
    case SQS_ARC: return "ARC";
    case SQS_PLINE: return "PLINE";
    case SQS_TEXT: return "TEXT";
    case SQS_CURVE: return "CURVE";
    case SQS_SHAPE: return "SHAPE";
    case SQS_MESH: return "MESH";
    case SQS_SOLID: return "SOLID";
    case SQS_CELLREF: return "CELL";
    }
    return "?";
}

static const char *snap_name(SqsSnapKind k) {
    switch (k) {
    case SQS_SNAP_ENDPOINT: return "endpoint";
    case SQS_SNAP_MIDPOINT: return "midpoint";
    case SQS_SNAP_CENTER: return "center";
    case SQS_SNAP_INTERSECTION: return "intersection";
    case SQS_SNAP_KEYPOINT: return "keypoint";
    case SQS_SNAP_NEAREST: return "nearest";
    default: return "none";
    }
}

static int done(int id, const char *what, const char *path) {
    if (id < 0) {
        fprintf(stderr, "could not add the %s (bad input, or the design is full)\n", what);
        return 1;
    }
    printf("%s id=%d\n", what, id);
    return save(&d, path) == 0 ? 0 : 1;
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
        sqs_design_init(&d, argc >= 4 ? argv[3] : "untitled");
        return save(&d, argv[2]) == 0 ? 0 : 1;
    }

    if (strcmp(cmd, "probe") == 0) {
        if (argc < 3) {
            usage();
            return 1;
        }
        const char *path = argv[2];
        FILE *f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "cannot open %s\n", path);
            return 1;
        }
        char head[8] = {0};
        size_t got = fread(head, 1, sizeof head, f);
        fclose(f);
        if (got >= 6 && memcmp(head, "SQDGNB", 6) == 0) {
            puts("format=sqdgnb");
            return 0;
        }
        if (got >= 5 && memcmp(head, "SQDGN", 5) == 0) {
            puts("format=sqdgn");
            return 0;
        }
        if (sqs_file_looks_like_dxf(path)) {
            puts("format=dxf (ASCII) — use: squirrelstation import-dxf out.sqdgn this.dxf");
            return 0;
        }
        if (sqs_file_looks_like_dgn(path)) {
            puts("format=dgn (binary MicroStation) — not readable directly.");
            puts("In MicroStation: File → Export → DXF, then import-dxf.");
            return 2;
        }
        puts("format=unknown");
        return 1;
    }

    if (strcmp(cmd, "import-dxf") == 0) {
        if (argc < 4) {
            usage();
            return 1;
        }
        sqs_design_init(&d, "from-dxf");
        SqsDxfStats st;
        int rc = sqs_import_dxf(&d, argv[3], &st);
        if (rc != 0) return 1;
        if (save(&d, argv[2]) != 0) return 1;
        printf("imported → %s\n", argv[2]);
        printf("  layers=%d lines=%d arcs=%d circles=%d texts=%d plines=%d shapes=%d meshes=%d cells=%d "
               "placements=%d skipped=%d\n",
               st.layers, st.lines, st.arcs, st.circles, st.texts, st.plines, st.shapes, st.meshes, st.cells,
               st.placements, st.skipped);
        return 0;
    }

    if (strcmp(cmd, "export-dxf") == 0) {
        if (argc < 4) {
            usage();
            return 1;
        }
        if (load(&d, argv[2]) != 0) return 1;
        if (sqs_export_dxf(&d, argv[3]) != 0) {
            fprintf(stderr, "export failed\n");
            return 1;
        }
        printf("exported %s → %s (%d elements)\n", argv[2], argv[3], d.element_count);
        return 0;
    }

    if (strcmp(cmd, "convert") == 0) {
        if (argc != 4) return usage(), 1;
        if (load(&d, argv[2]) != 0 || save(&d, argv[3]) != 0) return 1;
        printf("converted %s → %s (%d elements)\n", argv[2], argv[3], d.element_count);
        return 0;
    }
    if (argc < 3) {
        usage();
        return 1;
    }
    /* --level NAME, anywhere after the file */
    const char *level_name = NULL;
    for (int i = 3; i + 1 < argc; i++)
        if (strcmp(argv[i], "--level") == 0) {
            level_name = argv[i + 1];
            memmove(&argv[i], &argv[i + 2], (size_t)(argc - i - 2) * sizeof argv[0]);
            argc -= 2;
            break;
        }
    if (load(&d, argv[2]) != 0) return 1;
    int level = d.levels[0].id;
    if (level_name && (level = sqs_level_id(&d, level_name)) < 0) {
        fprintf(stderr, "no level %s\n", level_name);
        return 1;
    }
    char **a = argv + 3;
    int na = argc - 3;
    SqsPoint p[SQS_MAX_PTS + 1];

    if (strcmp(cmd, "line") == 0) {
        if (na != 2 || points(a, 2, p)) return usage(), 1;
        return done(sqs_add_line(&d, level, p[0], p[1]), "line", argv[2]);
    }
    if (strcmp(cmd, "arc") == 0) {
        if (na != 4 || points(a, 1, p)) return usage(), 1;
        return done(sqs_add_arc(&d, level, p[0], atof(a[1]), atof(a[2]), atof(a[3])), "arc", argv[2]);
    }
    if (strcmp(cmd, "text") == 0) {
        if (na < 2 || points(a, 1, p)) return usage(), 1;
        char t[SQS_MAX_NAME] = "";
        for (int i = 1; i < na; i++) {
            size_t at = strlen(t);
            snprintf(t + at, sizeof t - at, "%s%s", i > 1 ? " " : "", a[i]);
        }
        return done(sqs_add_text(&d, level, p[0], t), "text", argv[2]);
    }
    if (strcmp(cmd, "pline") == 0 || strcmp(cmd, "curve") == 0 || strcmp(cmd, "shape") == 0 ||
        strcmp(cmd, "mesh") == 0) {
        if (na < 2 || na > SQS_MAX_PTS || points(a, na, p)) return usage(), 1;
        int id;
        if (cmd[0] == 'p') id = sqs_add_pline(&d, level, p, na);
        else if (cmd[0] == 'c') id = sqs_add_curve(&d, level, p, na);
        else if (cmd[0] == 's') id = sqs_add_shape(&d, level, p, na);
        else {
            uint8_t face[1][4] = {{0, 1, 2, (uint8_t)(na == 4 ? 3 : 2)}};
            id = na == 3 || na == 4 ? sqs_add_mesh(&d, level, p, na, (const uint8_t(*)[4])face, 1) : -1;
        }
        return done(id, cmd, argv[2]);
    }
    if (strcmp(cmd, "solid") == 0) {
        if (na < 3 || na > 4 || points(a + 1, 1, p)) return usage(), 1;
        SqsPoint size;
        if (sqs_keyin(a[2], (SqsPoint){0, 0, 0}, &size)) return usage(), 1;
        SqsSolidKind k = !strcmp(a[0], "box") ? SQS_SOLID_BOX
                         : !strcmp(a[0], "cylinder") ? SQS_SOLID_CYLINDER
                         : !strcmp(a[0], "sphere")   ? SQS_SOLID_SPHERE
                                                     : (SqsSolidKind)0;
        int id = sqs_add_solid(&d, level, k, p[0], size);
        if (id > 0 && na == 4) d.elements[d.element_count - 1].rot_deg = atof(a[3]);
        return done(id, "solid", argv[2]);
    }
    if (strcmp(cmd, "level") == 0) {
        if (na < 1) return usage(), 1;
        if (na == 2 && (!strcmp(a[1], "on") || !strcmp(a[1], "off") || !strcmp(a[1], "freeze") ||
                        !strcmp(a[1], "thaw"))) {
            int id = sqs_level_id(&d, a[0]);
            const SqsLevel *lv = sqs_level(&d, id);
            if (!lv) {
                fprintf(stderr, "no level %s\n", a[0]);
                return 1;
            }
            int vis = lv->visible, frz = lv->frozen;
            if (!strcmp(a[1], "on")) vis = 1;
            else if (!strcmp(a[1], "off")) vis = 0;
            else if (!strcmp(a[1], "freeze")) frz = 1;
            else frz = 0;
            sqs_level_set(&d, a[0], vis, frz);
            printf("level %s %s%s\n", a[0], vis ? "on" : "off", frz ? " frozen" : "");
            return save(&d, argv[2]) == 0 ? 0 : 1;
        }
        uint32_t color = 0x00FF00;
        if (na >= 2) color = (uint32_t)strtoul(a[1], NULL, 0);
        return done(sqs_add_level(&d, a[0], color), "level", argv[2]);
    }
    if (strcmp(cmd, "cell") == 0) {
        if (na < 3 || points(a + 1, 1, p)) return usage(), 1;
        int ids[SQS_MAX_CELL_MEMBERS], n = 0;
        for (int i = 2; i < na && n < SQS_MAX_CELL_MEMBERS; i++) ids[n++] = atoi(a[i]);
        return done(sqs_define_cell(&d, a[0], p[0], ids, n), "cell", argv[2]);
    }
    if (strcmp(cmd, "place") == 0) {
        if (na < 2 || na > 4 || points(a + 1, 1, p)) return usage(), 1;
        double sc = na >= 3 ? atof(a[2]) : 1, rot = na == 4 ? atof(a[3]) : 0;
        return done(sqs_place_cell(&d, level, a[0], p[0], sc, rot), "placed cell", argv[2]);
    }
    if (strcmp(cmd, "attach") == 0) {
        if (na < 1 || na > 3) return usage(), 1;
        SqsPoint off = {0, 0, 0};
        if (na >= 2 && points(a + 1, 1, &off)) return usage(), 1;
        double sc = na == 3 ? atof(a[2]) : 1;
        if (load(&refd[0], a[0]) != 0) return 1; /* it must be readable now */
        int r = sqs_attach(&d, a[0], off, sc);
        if (r < 0) {
            fprintf(stderr, "could not attach %s\n", a[0]);
            return 1;
        }
        printf("reference %d: %s (%d elements)\n", r, a[0], refd[0].element_count);
        return save(&d, argv[2]) == 0 ? 0 : 1;
    }
    if (strcmp(cmd, "list") == 0) {
        printf("design: %s  elements=%d levels=%d cells=%d refs=%d\n", d.title, d.element_count, d.level_count,
               d.cell_count, d.ref_count);
        for (int i = 0; i < d.level_count; i++)
            printf("  level %d %s vis=%d%s\n", d.levels[i].id, d.levels[i].name, d.levels[i].visible,
                   d.levels[i].frozen ? " frozen" : "");
        for (int i = 0; i < d.element_count; i++) {
            const SqsElement *e = &d.elements[i];
            printf("  %s %d L%d (%.3f,%.3f,%.3f)", kind_name(e->type), e->id, e->level_id, e->a.x, e->a.y, e->a.z);
            if (e->type == SQS_LINE) printf("-(%.3f,%.3f,%.3f)", e->b.x, e->b.y, e->b.z);
            else if (e->type == SQS_ARC) printf(" r=%.3f", e->radius);
            else if (e->type == SQS_TEXT) printf(" \"%s\"", e->text);
            else if (e->type == SQS_CELLREF) printf(" %s scale=%g rot=%g", e->text, e->scale, e->rot_deg);
            else if (e->type == SQS_SOLID)
                printf(" %s", e->solid_kind == SQS_SOLID_BOX ? "box" : e->solid_kind == SQS_SOLID_CYLINDER ? "cylinder" : "sphere");
            else printf(" %d points", e->npts);
            if (sqs_in_cell(&d, e->id)) printf(" (cell member)");
            printf("\n");
        }
        for (int i = 0; i < d.cell_count; i++) printf("  cell %s (%d elements)\n", d.cells[i].name, d.cells[i].member_count);
        for (int i = 0; i < d.ref_count; i++)
            printf("  ref %d %s offset %g,%g,%g scale %g\n", i, d.refs[i].path, d.refs[i].offset.x, d.refs[i].offset.y,
                   d.refs[i].offset.z, d.refs[i].scale);
        return 0;
    }
    if (strcmp(cmd, "measure") == 0) {
        const SqsElement *e = na == 1 ? sqs_find(&d, atoi(a[0])) : NULL;
        if (!e) {
            fprintf(stderr, "no element %s\n", na == 1 ? a[0] : "");
            return 1;
        }
        printf("%s %d length=%.6g area=%.6g volume=%.6g\n", kind_name(e->type), e->id, sqs_length(e), sqs_area(e),
               sqs_volume(e));
        return 0;
    }
    if (strcmp(cmd, "snap") == 0) {
        double tol = 1.0;
        if (na < 1 || points(a, 1, p)) return usage(), 1;
        if (na >= 2) tol = atof(a[1]);
        const SqsDesign *refs[SQS_MAX_REFS] = {0};
        for (int i = 0; i < d.ref_count; i++) {
            if (load(&refd[i], d.refs[i].path) == 0) refs[i] = &refd[i];
            else fprintf(stderr, "reference %s not loaded\n", d.refs[i].path);
        }
        SqsSnapResult s = sqs_snap(&d, refs, d.ref_count, p[0], tol);
        if (s.kind == SQS_SNAP_NONE) {
            puts("snap=none");
            return 0;
        }
        printf("snap=%s elem=%d", snap_name(s.kind), s.element_id);
        if (s.ref >= 0) printf(" ref=%d", s.ref);
        printf(" dist=%.6f -> %.6f,%.6f,%.6f\n", s.distance, s.p.x, s.p.y, s.p.z);
        return 0;
    }
    usage();
    return 1;
}
