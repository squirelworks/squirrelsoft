/* calcgui: the SquirrelCalc window panel (see calcgui.h). Original code. */
#include "calcgui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint32_t COLORS[CALC_N] = {0xFF2060D0u, 0xFFD03030u, 0xFF20A040u, 0xFF9040C0u};
#define COL_DERIV 0xFF80A8F0u
#define COL_ERR 0xFFB02020u

static SqmExpr scratch, scratch2; /* big: kept off the stack */

void calc_gui_init(CalcGui *g) {
    memset(g, 0, sizeof *g);
    snprintf(g->status, sizeof g->status, "Type a function of x, e.g. x^2 - 2 or a sin(x) after a = 2");
}

double calc_gui_var(const CalcGui *g, const char *name) {
    for (int i = 0; i < g->nvars; i++)
        if (!strcmp(g->vname[i], name)) return g->vval[i];
    return NAN;
}

/* slots: x, then the user variables */
static int slots(const CalcGui *g, const char **names, double *values) {
    names[0] = "x";
    values[0] = 0;
    for (int i = 0; i < g->nvars; i++) {
        names[i + 1] = g->vname[i];
        values[i + 1] = g->vval[i];
    }
    return g->nvars + 1;
}

static void compile_deriv(CalcGui *g) {
    g->dok = 0;
    g->dtext[0] = 0;
    if (!g->ok[0] || sqm_parse(g->fx[0], &scratch, NULL) != SQM_OK) return;
    int rc = sqm_diff(&scratch, "x", &scratch2);
    if (rc != SQM_OK) {
        snprintf(g->dtext, sizeof g->dtext, "d/dx y1: %s", sqm_strerror(rc));
        return;
    }
    char t[180];
    sqm_print(&scratch2, t, sizeof t);
    snprintf(g->dtext, sizeof g->dtext, "y1' = %s", t);
    const char *names[CALC_VARS + 1];
    double values[CALC_VARS + 1];
    int n = slots(g, names, values);
    g->dok = sqm_compile(&scratch2, names, n, &g->dprog) == SQM_OK;
}

static void compile(CalcGui *g, int i) {
    memcpy(g->seen[i], g->fx[i], sizeof g->seen[i]);
    g->ok[i] = 0;
    g->err[i][0] = 0;
    g->nroots[i] = 0;
    const char *p = g->fx[i];
    while (*p == ' ') p++;
    if (*p) {
        SqmError err;
        if (sqm_parse(g->fx[i], &scratch, &err) != SQM_OK) {
            snprintf(g->err[i], sizeof g->err[i], "%s (at %d)", err.msg, err.pos + 1);
        } else {
            for (int k = 0; k < scratch.nnames; k++)
                if (strcmp(scratch.names[k], "x") && isnan(calc_gui_var(g, scratch.names[k]))) {
                    snprintf(g->err[i], sizeof g->err[i], "define %s first: %s = 1", scratch.names[k],
                             scratch.names[k]);
                    break;
                }
            if (!g->err[i][0]) {
                const char *names[CALC_VARS + 1];
                double values[CALC_VARS + 1];
                int n = slots(g, names, values);
                int rc = sqm_compile(&scratch, names, n, &g->prog[i]);
                if (rc == SQM_OK) g->ok[i] = 1;
                else snprintf(g->err[i], sizeof g->err[i], "%s", sqm_strerror(rc));
            }
        }
    }
    if (i == 0) compile_deriv(g);
}

void calc_gui_set(CalcGui *g, int i, const char *text) {
    if (i < 0 || i >= CALC_N) return;
    if (text != g->fx[i]) snprintf(g->fx[i], sizeof g->fx[i], "%s", text ? text : "");
    compile(g, i);
}

static void hist_add(CalcGui *g, const char *line) {
    size_t n = strlen(line), have = strlen(g->hist);
    while (have && have + n + 2 > sizeof g->hist) { /* drop the oldest lines */
        char *nl = strchr(g->hist, '\n');
        if (!nl) {
            g->hist[0] = 0;
            have = 0;
            break;
        }
        memmove(g->hist, nl + 1, strlen(nl + 1) + 1);
        have = strlen(g->hist);
    }
    if (n + 2 > sizeof g->hist) n = sizeof g->hist - 2;
    if (have) g->hist[have++] = '\n';
    memcpy(g->hist + have, line, n);
    g->hist[have + n] = 0;
}

static int is_reserved(const char *name) {
    if (!strcmp(name, "x") || !strcmp(name, "pi") || !strcmp(name, "e")) return 1;
    for (int f = 0; f < SQM_FN_COUNT; f++)
        if (!strcmp(name, sqm_fn_name(f))) return 1;
    return 0;
}

static int set_var(CalcGui *g, const char *name, double v) {
    for (int i = 0; i < g->nvars; i++)
        if (!strcmp(g->vname[i], name)) {
            g->vval[i] = v;
            return 0;
        }
    if (g->nvars >= CALC_VARS) return -1;
    snprintf(g->vname[g->nvars], SQM_NAME_LEN, "%s", name);
    g->vval[g->nvars++] = v;
    return 0;
}

int calc_gui_eval(CalcGui *g, const char *line) {
    char out[400], num[64], name[SQM_NAME_LEN] = "";
    const char *expr = line;
    /* name = expression */
    const char *eq = strchr(line, '=');
    if (eq) {
        const char *a = line, *b = eq;
        while (*a == ' ') a++;
        while (b > a && b[-1] == ' ') b--;
        size_t len = (size_t)(b - a);
        int good = len > 0 && len < SQM_NAME_LEN && ((*a >= 'a' && *a <= 'z') || (*a >= 'A' && *a <= 'Z') || *a == '_');
        for (size_t k = 0; good && k < len; k++)
            good = (a[k] >= 'a' && a[k] <= 'z') || (a[k] >= 'A' && a[k] <= 'Z') || a[k] == '_' ||
                   (a[k] >= '0' && a[k] <= '9');
        if (!good) {
            snprintf(out, sizeof out, "%s: the left of = must be a name", line);
            hist_add(g, out);
            return -1;
        }
        memcpy(name, a, len);
        name[len] = 0;
        if (is_reserved(name) || !strcmp(name, "ans")) {
            snprintf(out, sizeof out, "%s: %s can't be assigned", line, name);
            hist_add(g, out);
            return -1;
        }
        expr = eq + 1;
    }
    SqmError err;
    if (sqm_parse(expr, &scratch, &err) != SQM_OK) {
        snprintf(out, sizeof out, "%s: %s", line, err.msg);
        hist_add(g, out);
        return -1;
    }
    const char *names[CALC_VARS];
    for (int i = 0; i < g->nvars; i++) names[i] = g->vname[i];
    double v;
    if (sqm_eval(&scratch, names, g->vval, g->nvars, &v) != SQM_OK) {
        const char *missing = "?";
        for (int k = 0; k < scratch.nnames; k++)
            if (isnan(calc_gui_var(g, scratch.names[k]))) missing = scratch.names[k];
        snprintf(out, sizeof out, "%s: %s is not defined", line, missing);
        hist_add(g, out);
        return -1;
    }
    sqm_format(v, 12, num, sizeof num);
    if (name[0]) {
        if (set_var(g, name, v)) {
            snprintf(out, sizeof out, "%s: no room for more variables", line);
            hist_add(g, out);
            return -1;
        }
        snprintf(out, sizeof out, "%s = %s", name, num);
        for (int i = 0; i < CALC_N; i++) compile(g, i); /* the graphs may use it */
    } else {
        set_var(g, "ans", v);
        snprintf(out, sizeof out, "%s = %s", line, num);
    }
    hist_add(g, out);
    return 0;
}

/* the current values for the programs: x, then the variables */
static void load_values(const CalcGui *g, double *values) {
    values[0] = 0;
    for (int i = 0; i < g->nvars; i++) values[i + 1] = g->vval[i];
}

static void graph(CalcGui *g, sqg_ui *ui, sqg_rect r) {
    sqg_canvas *cv = ui->cv;
    const sqg_input *in = &ui->in;
    if (!g->view_ready || g->graph_at.w != r.w || g->graph_at.h != r.h) {
        if (!g->view_ready) calc_view_default(&g->view, r.w, r.h);
        else calc_view_square(&g->view, r.w, r.h);
        g->view_ready = 1;
    }
    g->graph_at = r;
    sqg_rect inner = sqg_rect_inset(r, 2);

    /* pointer: wheel zooms about it, a drag pans */
    int inside = sqg_rect_contains(inner, in->mx, in->my);
    g->mouse_in = inside;
    if (inside) {
        g->mouse_x = calc_x_at(&g->view, inner, in->mx + 0.5);
        g->mouse_y = calc_y_at(&g->view, inner, in->my + 0.5);
        if (in->wheel) calc_view_zoom(&g->view, pow(1.25, in->wheel), g->mouse_x, g->mouse_y);
        if (in->pressed && !ui->hot) {
            g->dragging = 1;
            g->last_mx = in->mx;
            g->last_my = in->my;
            ui->focus = 0;
        }
    }
    if (g->dragging) {
        if (in->down) {
            calc_view_pan(&g->view, in->mx - g->last_mx, in->my - g->last_my, inner.w, inner.h);
            g->last_mx = in->mx;
            g->last_my = in->my;
        } else {
            g->dragging = 0;
        }
    }

    sqg_bevel(cv, r, 1, 0, ui->theme);
    calc_draw_axes(cv, inner, &g->view);
    double values[CALC_VARS + 1];
    load_values(g, values);
    if (g->show_deriv && g->dok) calc_plot(cv, inner, &g->view, &g->dprog, values, 0, COL_DERIV);
    for (int i = 0; i < CALC_N; i++) {
        if (!g->ok[i]) continue;
        calc_plot(cv, inner, &g->view, &g->prog[i], values, 0, COLORS[i]);
        g->nroots[i] = 0;
        if (g->show_roots) {
            int n = sqm_roots(&g->prog[i], values, 0, g->view.xmin, g->view.xmax, inner.w, g->roots[i], CALC_ROOTS);
            g->nroots[i] = n < CALC_ROOTS ? n : CALC_ROOTS;
            calc_mark_roots(cv, inner, &g->view, g->roots[i], g->nroots[i], COLORS[i]);
        }
    }
}

void calc_gui_frame(CalcGui *g, sqg_ui *ui) {
    sqg_canvas *cv = ui->cv;
    int lh = sqg_line_h(ui), pad = 6;
    sqg_fill(cv, sqg_rect_make(0, 0, cv->w, cv->h), ui->theme->face);
    for (int i = 0; i < CALC_N; i++)
        if (strcmp(g->fx[i], g->seen[i])) compile(g, i); /* typed into */

    int left_w = cv->w / 3;
    if (left_w < 240) left_w = 240;
    if (left_w > cv->w - 2 * pad - 40) left_w = cv->w - 2 * pad - 40;
    if (left_w < 40) left_w = 40;
    int status_y = cv->h - pad - lh;

    /* functions */
    int y = pad;
    sqg_label(ui, sqg_rect_make(pad, y, left_w, lh), "Functions of x", SQG_ALIGN_LEFT);
    y += lh;
    for (int i = 0; i < CALC_N; i++) {
        char id[16], tag[16];
        snprintf(id, sizeof id, "fx%d", i);
        snprintf(tag, sizeof tag, "y%d =", i + 1);
        sqg_row row = sqg_row_begin(sqg_rect_make(pad, y, left_w, lh), 4);
        sqg_rect sw = sqg_row_take(&row, 8);
        sqg_fill(cv, sqg_rect_make(sw.x, sw.y + lh / 2 - 4, 8, 8), COLORS[i]);
        sqg_label(ui, sqg_row_take(&row, sqg_text_width("y1 =", ui->scale) + 2), tag, SQG_ALIGN_LEFT);
        sqg_textfield(ui, sqg_id_str(id), sqg_row_take(&row, 0), g->fx[i], sizeof g->fx[i]);
        y += lh;
        if (g->err[i][0]) sqg_text(cv, pad + 14, y + 1, g->err[i], COL_ERR, 1);
        y += 12;
    }
    sqg_row trow = sqg_row_begin(sqg_rect_make(pad, y, left_w, lh), 6);
    sqg_toggle(ui, sqg_id_str("roots"), sqg_row_take(&trow, sqg_button_w(ui, "Roots")), "Roots", &g->show_roots);
    sqg_toggle(ui, sqg_id_str("deriv"), sqg_row_take(&trow, sqg_button_w(ui, "d/dx y1")), "d/dx y1",
               &g->show_deriv);
    y += lh + 4;
    if (g->show_deriv && g->dtext[0]) {
        sqg_text(cv, pad, y, g->dtext, g->dok ? 0xFF3050A0u : COL_ERR, 1);
        y += 12;
    }

    /* calculator: history above the input line */
    y += 4;
    sqg_label(ui, sqg_rect_make(pad, y, left_w, lh), "Calculator (a = 2 defines a)", SQG_ALIGN_LEFT);
    y += lh;
    int input_y = status_y - pad - lh;
    if (input_y - y > 20) sqg_textbox(ui, sqg_rect_make(pad, y, left_w, input_y - 4 - y), g->hist);
    sqg_row crow = sqg_row_begin(sqg_rect_make(pad, input_y, left_w, lh), 4);
    int enter = sqg_textfield(ui, sqg_id_str("line"), sqg_row_take(&crow, left_w - sqg_button_w(ui, "=") - 4),
                              g->line, sizeof g->line);
    if ((sqg_button(ui, sqg_id_str("eq"), sqg_row_take(&crow, 0), "=") || enter) && g->line[0]) {
        if (calc_gui_eval(g, g->line) == 0) g->line[0] = 0;
    }

    /* graph toolbar and graph */
    int gx = pad + left_w + pad, gw = cv->w - gx - pad;
    if (gw > 20) {
        sqg_row grow = sqg_row_begin(sqg_rect_make(gx, pad, gw, lh), 4);
        double cx = (g->view.xmin + g->view.xmax) / 2, cy = (g->view.ymin + g->view.ymax) / 2;
        if (sqg_button(ui, sqg_id_str("zout"), sqg_row_take(&grow, sqg_button_w(ui, "-")), "-"))
            calc_view_zoom(&g->view, 1 / 1.5, cx, cy);
        if (sqg_button(ui, sqg_id_str("zin"), sqg_row_take(&grow, sqg_button_w(ui, "+")), "+"))
            calc_view_zoom(&g->view, 1.5, cx, cy);
        if (sqg_button(ui, sqg_id_str("reset"), sqg_row_take(&grow, sqg_button_w(ui, "Reset")), "Reset"))
            g->view_ready = 0;
        if (sqg_button(ui, sqg_id_str("square"), sqg_row_take(&grow, sqg_button_w(ui, "Square")), "Square"))
            calc_view_square(&g->view, g->graph_at.w, g->graph_at.h);
        char pos[80] = "";
        if (g->mouse_in) {
            char a[32], b[32];
            sqm_format(g->mouse_x, 5, a, sizeof a);
            sqm_format(g->mouse_y, 5, b, sizeof b);
            snprintf(pos, sizeof pos, "x = %s  y = %s", a, b);
        }
        sqg_label(ui, sqg_row_take(&grow, 0), pos, SQG_ALIGN_RIGHT);
        int top = pad + lh + 4;
        if (status_y - pad - top > 20) graph(g, ui, sqg_rect_make(gx, top, gw, status_y - pad - top));
    }

    /* status: the roots, or the hint */
    if (g->show_roots) {
        char s[200] = "";
        size_t k = 0;
        for (int i = 0; i < CALC_N && k + 40 < sizeof s; i++) {
            if (!g->ok[i]) continue;
            k += (size_t)snprintf(s + k, sizeof s - k, "%sy%d:", k ? "   " : "", i + 1);
            if (g->nroots[i] == 0) k += (size_t)snprintf(s + k, sizeof s - k, " none in view");
            for (int r = 0; r < g->nroots[i] && r < 4 && k + 20 < sizeof s; r++) {
                char t[32];
                sqm_format(g->roots[i][r], 6, t, sizeof t);
                k += (size_t)snprintf(s + k, sizeof s - k, " %s", t);
            }
            if (g->nroots[i] > 4) k += (size_t)snprintf(s + k, sizeof s - k, " ...");
        }
        snprintf(g->status, sizeof g->status, "Roots  %.190s", s[0] ? s : "(no functions)");
    }
    sqg_label(ui, sqg_rect_make(pad, status_y, cv->w - 2 * pad, lh), g->status, SQG_ALIGN_LEFT);
}
