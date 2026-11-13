#include "sqc_cmd.h"
#include "sqc_dxf.h"
#include "sqc_gui.h"
#include "sqcad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    puts("  run <drawing.sqdwg> [script.scr]  # the command line: LINE, CIRCLE, MOVE, PLOT, ...");
    puts("  plot <drawing.sqdwg> <out.svg> [A3] [1:100|FIT] [PORTRAIT]");
    puts("  gui <drawing.sqdwg|in.dxf> [--snapshot out.ppm] [--frames N]   # the window");
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

static SqcSession session; /* two drawings: not on the stack */

/* Commands from a script, or typed at "Command:". The drawing is opened (or
 * started when the file does not exist yet) and saved when the input ends
 * or at QUIT, if it changed. A failing command in a script stops it. */
static int run(int argc, char **argv) {
    if (argc < 3 || argc > 4) return usage(), 2;
    sqc_session_init(&session, argv[2]);
    FILE *probe = fopen(argv[2], "r");
    if (probe) {
        fclose(probe);
        if (sqc_read_sqdwg(&session.dwg, argv[2])) {
            fprintf(stderr, "cannot read %s\n", argv[2]);
            return 1;
        }
    } else {
        session.dirty = 1;
    }
    FILE *in = stdin;
    if (argc == 4 && !(in = fopen(argv[3], "r"))) {
        fprintf(stderr, "cannot read %s\n", argv[3]);
        return 1;
    }
    int interactive = argc == 3 && isatty(0);
    char line[1024], out[8192];
    int ln = 0, rc = 0;
    for (;;) {
        if (interactive) {
            fputs("Command: ", stdout);
            fflush(stdout);
        }
        if (!fgets(line, sizeof line, in)) break;
        ln++;
        int r = sqc_exec(&session, line, out, sizeof out);
        if (r == SQC_CMD_QUIT) break;
        if (r == SQC_CMD_ERROR) {
            if (interactive) printf("%s\n", out);
            else {
                fprintf(stderr, "%s:%d: %s\n", argc == 4 ? argv[3] : "stdin", ln, out);
                rc = 1;
                break;
            }
        } else if (out[0]) {
            printf("%s\n", out);
        }
    }
    if (in != stdin) fclose(in);
    if (session.dirty && rc == 0) {
        if (sqc_write_sqdwg(&session.dwg, session.path)) {
            fprintf(stderr, "cannot write %s\n", session.path);
            return 1;
        }
        printf("saved %s\n", session.path);
    }
    return rc;
}

static int plot(int argc, char **argv) {
    if (argc < 4) return usage(), 2;
    sqc_session_init(&session, argv[2]);
    if (sqc_read_sqdwg(&session.dwg, argv[2])) {
        fprintf(stderr, "cannot read %s\n", argv[2]);
        return 1;
    }
    char line[1024] = "PLOT", out[256];
    for (int i = 3; i < argc; i++) {
        size_t at = strlen(line);
        snprintf(line + at, sizeof line - at, " %s", argv[i]);
    }
    int r = sqc_exec(&session, line, out, sizeof out);
    printf("%s\n", out);
    return r == SQC_CMD_OK ? 0 : 1;
}

/* The window. A DXF is imported and saves next to it as .sqdwg; a missing
 * file starts a new drawing there. --snapshot draws two frames off screen
 * into a PPM instead of opening a window. */
static int gui(int argc, char **argv) {
    if (argc < 3) return usage(), 2;
    const char *path = argv[2], *snapshot = NULL;
    int frames = 0;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc) snapshot = argv[++i];
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = atoi(argv[++i]);
        else return usage(), 2;
    }
    char save_as[256];
    snprintf(save_as, sizeof save_as, "%s", path);
    size_t n = strlen(save_as);
    int dxf = n > 4 && (strcmp(save_as + n - 4, ".dxf") == 0 || strcmp(save_as + n - 4, ".DXF") == 0);
    if (dxf && n + 2 < sizeof save_as) strcpy(save_as + n - 4, ".sqdwg");
    sqc_session_init(&session, save_as);
    FILE *probe = fopen(path, "r");
    if (probe) {
        fclose(probe);
        int bad = dxf ? sqc_import_dxf(&session.dwg, path, NULL) : sqc_read_sqdwg(&session.dwg, path);
        if (bad) {
            fprintf(stderr, "cannot read %s\n", path);
            return 1;
        }
    }
    static SqcGui g;
    sqc_gui_init(&g, &session);
    if (!snapshot) return sqc_gui_run(&g, frames);
    enum { W = 1000, H = 700 };
    static uint32_t px[W * H];
    sqg_canvas cv;
    sqg_canvas_init(&cv, px, W, H, W);
    sqg_ui ui;
    sqg_ui_init(&ui, NULL, 1);
    sqg_input in = {0};
    in.mx = in.my = -1;
    for (int f = 0; f < 2; f++) {
        sqg_begin(&ui, &cv, &in);
        sqc_gui_frame(&g, &ui);
        sqg_end(&ui);
    }
    if (sqg_write_ppm(&cv, snapshot)) {
        fprintf(stderr, "cannot write %s\n", snapshot);
        return 1;
    }
    printf("wrote %s (%dx%d)\n", snapshot, W, H);
    return 0;
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

    if (strcmp(cmd, "run") == 0 || strcmp(cmd, "RUN") == 0) return run(argc, argv);
    if (strcmp(cmd, "plot") == 0 || strcmp(cmd, "PLOT") == 0) return plot(argc, argv);
    if (strcmp(cmd, "gui") == 0 || strcmp(cmd, "GUI") == 0) return gui(argc, argv);

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
        printf("  layers=%d lines=%d circles=%d arcs=%d texts=%d plines=%d blocks=%d inserts=%d faces=%d "
               "skipped=%d\n",
               st.layers, st.lines, st.circles, st.arcs, st.texts, st.plines, st.blocks, st.inserts, st.faces,
               st.skipped);
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
