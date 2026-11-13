/* calcnote: SquirrelCalc notebooks (see calcnote.h), and running, saving
 * and loading them with the calculator (calcgui.h). */
#include "calcnote.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calcgui.h"

void calc_note_init(CalcNote *nb) {
    memset(nb, 0, sizeof *nb);
    nb->sel = nb->edit_at = nb->seen_sel = -1;
}

int calc_note_insert(CalcNote *nb, int at, int kind, const char *text) {
    if (!text) text = "";
    if (nb->n >= CALC_NOTE_MAX || (kind != CALC_NOTE_TEXT && kind != CALC_NOTE_MATH) ||
        strlen(text) >= CALC_NOTE_LEN || strchr(text, '\n'))
        return -1;
    if (at < 0) at = 0;
    if (at > nb->n) at = nb->n;
    memmove(&nb->c[at + 1], &nb->c[at], (size_t)(nb->n - at) * sizeof nb->c[0]);
    CalcNoteCell *c = &nb->c[at];
    memset(c, 0, sizeof *c);
    c->kind = kind;
    snprintf(c->text, sizeof c->text, "%s", text);
    nb->n++;
    if (nb->sel >= at) nb->sel++;
    if (nb->edit_at >= at) nb->edit_at++;
    return at;
}

int calc_note_set(CalcNote *nb, int at, const char *text) {
    if (at < 0 || at >= nb->n || !text || strlen(text) >= CALC_NOTE_LEN || strchr(text, '\n')) return -1;
    if (text != nb->c[at].text) snprintf(nb->c[at].text, sizeof nb->c[at].text, "%s", text);
    return 0;
}

int calc_note_set_kind(CalcNote *nb, int at, int kind) {
    if (at < 0 || at >= nb->n || (kind != CALC_NOTE_TEXT && kind != CALC_NOTE_MATH)) return -1;
    nb->c[at].kind = kind;
    nb->c[at].out[0] = 0;
    nb->c[at].bad = 0;
    return 0;
}

/* where index i goes when the cell at a leaves */
static int after_remove(int i, int a) { return i == a ? -1 : i > a ? i - 1 : i; }

int calc_note_remove(CalcNote *nb, int at) {
    if (at < 0 || at >= nb->n) return -1;
    memmove(&nb->c[at], &nb->c[at + 1], (size_t)(nb->n - at - 1) * sizeof nb->c[0]);
    nb->n--;
    nb->edit_at = after_remove(nb->edit_at, at);
    if (nb->sel == at) nb->sel = at < nb->n ? at : nb->n - 1; /* the next cell, or the new last one */
    else nb->sel = after_remove(nb->sel, at);
    if (nb->top >= nb->n) nb->top = nb->n > 0 ? nb->n - 1 : 0;
    return 0;
}

/* where index i goes when the cell at a moves to b */
static int after_move(int i, int a, int b) {
    if (i == a) return b;
    if (a < b && i > a && i <= b) return i - 1;
    if (b < a && i >= b && i < a) return i + 1;
    return i;
}

int calc_note_move(CalcNote *nb, int at, int to) {
    if (at < 0 || at >= nb->n) return -1;
    if (to < 0) to = 0;
    if (to >= nb->n) to = nb->n - 1;
    if (to == at) return at;
    CalcNoteCell keep = nb->c[at];
    if (to > at) memmove(&nb->c[at], &nb->c[at + 1], (size_t)(to - at) * sizeof nb->c[0]);
    else memmove(&nb->c[to + 1], &nb->c[to], (size_t)(at - to) * sizeof nb->c[0]);
    nb->c[to] = keep;
    if (nb->sel >= 0) nb->sel = after_move(nb->sel, at, to);
    if (nb->edit_at >= 0) nb->edit_at = after_move(nb->edit_at, at, to);
    return to;
}

void calc_note_result(const CalcNoteCell *c, char *out, size_t cap) {
    if (!cap) return;
    out[0] = 0;
    if (c->kind != CALC_NOTE_MATH) return;
    const char *p = c->out, *t = c->text;
    t += strspn(t, " ");
    size_t tn = strlen(t);
    while (tn && t[tn - 1] == ' ') tn--;
    if (tn && !strncmp(p, t, tn) && (p[tn] == 0 || p[tn] == '\n' || p[tn] == ' ' || p[tn] == ':')) {
        p += tn;
        if (*p == ':') p++; /* "2 +: why" reads "why" */
        p += strspn(p, " ");
        if (*p == '\n') p++; /* the copy was a line of its own */
    }
    snprintf(out, cap, "%s", p);
}

int calc_note_heading(const CalcNoteCell *c, const char **body) {
    const char *t = c->text + strspn(c->text, " ");
    if (c->kind == CALC_NOTE_TEXT && t[0] == '#') {
        t += strspn(t, "#");
        *body = t + strspn(t, " ");
        return 1;
    }
    *body = c->text;
    return 0;
}

size_t calc_note_wrap(const char *s, int cols) {
    size_t n = strlen(s);
    if (cols < 1) cols = 1;
    const char *nl = strchr(s, '\n');
    size_t line = nl ? (size_t)(nl - s) : n;
    if (line <= (size_t)cols) return nl ? line + 1 : line; /* the newline goes with its line */
    size_t cut = (size_t)cols;
    while (cut > 0 && s[cut] != ' ' && s[cut - 1] != ' ') cut--;
    if (cut == 0) return (size_t)cols; /* one long word: break it */
    while (cut < line && s[cut] == ' ') cut++; /* the spaces go at the end of the line */
    return cut;
}

/* ---- with the calculator ---- */

int calc_gui_notebook_run(CalcGui *g) {
    CalcNote *nb = &g->note;
    calc_gui_forget(g);
    int bad = 0;
    for (int i = 0; i < nb->n; i++) {
        CalcNoteCell *c = &nb->c[i];
        c->out[0] = 0;
        c->bad = 0;
        if (c->kind != CALC_NOTE_MATH || !c->text[strspn(c->text, " ")]) continue;
        g->echo = c->out;
        g->echo_cap = sizeof c->out;
        c->bad = calc_gui_eval(g, c->text) != 0;
        bad += c->bad;
    }
    g->echo = NULL;
    g->echo_cap = 0;
    for (int i = 0; i < CALC_N; i++) calc_gui_set(g, i, g->fx[i]); /* with the variables as they are now */
    return bad;
}

/* appending to a buffer that may be short, counting what the whole needs */
typedef struct {
    char *out;
    size_t cap, len;
} Sink;

static void put(Sink *k, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(Sink *k, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *at = k->len < k->cap ? k->out + k->len : NULL;
    int n = vsnprintf(at, at ? k->cap - k->len : 0, fmt, ap);
    va_end(ap);
    if (n > 0) k->len += (size_t)n;
}

#define NOTEBOOK_HEAD "SquirrelCalc notebook 1"

size_t calc_gui_notebook_save(const CalcGui *g, char *out, size_t cap) {
    Sink k = {out, cap, 0};
    if (cap) out[0] = 0;
    put(&k, "%s\n", NOTEBOOK_HEAD);
    for (int i = 0; i < CALC_N; i++)
        if (g->fx[i][0]) put(&k, "y%d: %s\n", i + 1, g->fx[i]);
    if (g->view_ready)
        put(&k, "view: %.17g %.17g %.17g %.17g\n", g->view.xmin, g->view.xmax, g->view.ymin, g->view.ymax);
    for (int r = 0; r < CALC_SHEET_ROWS; r++)
        for (int c = 0; c < CALC_SHEET_COLS; c++)
            if (g->sheet.text[r][c][0]) {
                char name[16];
                calc_cell_name(r, c, name, sizeof name);
                put(&k, "cell %s: %s\n", name, g->sheet.text[r][c]);
            }
    for (int i = 0; i < g->note.n; i++)
        put(&k, "%s: %s\n", g->note.c[i].kind == CALC_NOTE_MATH ? "math" : "text", g->note.c[i].text);
    return k.len;
}

/* One line of a notebook file. With g NULL it is only checked; cells
 * counts the cells so far. Returns 0, 1 for a line that says nothing
 * (blank, # comment, the head), or -1 with why in msg. */
static int load_line(CalcGui *g, const char *line, size_t len, int *cells, char *msg, size_t msgcap) {
    char buf[CALC_NOTE_LEN + 32];
    while (len && (line[len - 1] == '\r' || line[len - 1] == ' ')) len--;
    size_t lead = 0;
    while (lead < len && line[lead] == ' ') lead++;
    if (lead == len || line[lead] == '#') return 1;
    if (len - lead >= sizeof buf) {
        snprintf(msg, msgcap, "the line is too long");
        return -1;
    }
    memcpy(buf, line + lead, len - lead);
    buf[len - lead] = 0;
    if (!strcmp(buf, NOTEBOOK_HEAD)) return 1;
    char *colon = strchr(buf, ':');
    if (!colon) {
        snprintf(msg, msgcap, "expected text:, math:, y1:, cell B2: or view:");
        return -1;
    }
    *colon = 0;
    char *val = colon + 1;
    if (*val == ' ') val++; /* "math: a = 2": the one space after the colon is the format's */
    if (!strcmp(buf, "text") || !strcmp(buf, "math")) {
        int kind = buf[0] == 'm' ? CALC_NOTE_MATH : CALC_NOTE_TEXT;
        if (strlen(val) >= CALC_NOTE_LEN) {
            snprintf(msg, msgcap, "a cell is at most %d characters", CALC_NOTE_LEN - 1);
            return -1;
        }
        if (++*cells > CALC_NOTE_MAX) {
            snprintf(msg, msgcap, "a notebook has at most %d cells", CALC_NOTE_MAX);
            return -1;
        }
        if (g) calc_note_insert(&g->note, g->note.n, kind, val);
        return 0;
    }
    if (buf[0] == 'y' && buf[1] >= '1' && buf[1] <= '0' + CALC_N && !buf[2]) {
        if (strlen(val) >= sizeof g->fx[0]) {
            snprintf(msg, msgcap, "a row is at most %d characters", (int)sizeof g->fx[0] - 1);
            return -1;
        }
        if (g) snprintf(g->fx[buf[1] - '1'], sizeof g->fx[0], "%s", val);
        return 0;
    }
    if (!strncmp(buf, "cell ", 5)) {
        int r, c, n = calc_cell_parse(buf + 5, &r, &c, NULL);
        if (!n || buf[5 + n] || strlen(val) >= CALC_CELL_LEN) {
            snprintf(msg, msgcap, "cell %.12s: not a cell from A1 to H50, or too long", buf + 5);
            return -1;
        }
        if (g) calc_sheet_set(&g->sheet, r, c, val);
        return 0;
    }
    if (!strcmp(buf, "view")) {
        double v[4];
        char extra[2];
        if (sscanf(val, "%lf %lf %lf %lf %1s", &v[0], &v[1], &v[2], &v[3], extra) != 4 || !isfinite(v[0]) ||
            !isfinite(v[1]) || !isfinite(v[2]) || !isfinite(v[3]) || !(v[1] > v[0]) || !(v[3] > v[2])) {
            snprintf(msg, msgcap, "view: expected xmin xmax ymin ymax");
            return -1;
        }
        if (g) {
            g->view.xmin = v[0];
            g->view.xmax = v[1];
            g->view.ymin = v[2];
            g->view.ymax = v[3];
            g->view_ready = 1; /* x as saved; y is squared to the window's shape */
        }
        return 0;
    }
    snprintf(msg, msgcap, "%.24s: expected text:, math:, y1:, cell B2: or view:", buf);
    return -1;
}

/* the first line that says something must be the head */
static int has_head(const char *p) {
    while (*p) {
        size_t len = strcspn(p, "\n"), lead = strspn(p, " \r");
        if (lead < len && p[lead] != '#') {
            size_t hn = strlen(NOTEBOOK_HEAD), end = len;
            while (end > lead && (p[end - 1] == ' ' || p[end - 1] == '\r')) end--;
            return end - lead == hn && !strncmp(p + lead, NOTEBOOK_HEAD, hn);
        }
        p += len + (p[len] == '\n');
    }
    return 0;
}

int calc_gui_notebook_load(CalcGui *g, const char *text, char *msg, size_t msgcap) {
    char why[96];
    if (!has_head(text)) {
        snprintf(msg, msgcap, "not a SquirrelCalc notebook: it starts \"%s\"", NOTEBOOK_HEAD);
        return -1;
    }
    for (int pass = 0; pass < 2; pass++) { /* check every line, then load */
        if (pass) {
            calc_note_init(&g->note);
            for (int i = 0; i < CALC_N; i++) g->fx[i][0] = 0;
            calc_sheet_init(&g->sheet);
            g->view_ready = 0;
        }
        int lineno = 0, cells = 0;
        for (const char *p = text; *p;) {
            size_t len = strcspn(p, "\n");
            lineno++;
            if (load_line(pass ? g : NULL, p, len, &cells, why, sizeof why) < 0) {
                snprintf(msg, msgcap, "line %d: %s", lineno, why);
                return -1;
            }
            p += len + (p[len] == '\n');
        }
    }
    calc_gui_recalc(g);
    int bad = calc_gui_notebook_run(g);
    g->note.sel = g->note.n ? 0 : -1;
    snprintf(msg, msgcap, "%d cells, %d with an error", g->note.n, bad);
    return 0;
}

size_t calc_gui_notebook_text(const CalcGui *g, char *out, size_t cap) {
    Sink k = {out, cap, 0};
    if (cap) out[0] = 0;
    for (int i = 0; i < g->note.n; i++) {
        const CalcNoteCell *c = &g->note.c[i];
        if (c->kind == CALC_NOTE_TEXT) {
            const char *body;
            if (calc_note_heading(c, &body)) put(&k, "== %s ==\n", body);
            else put(&k, "%s\n", c->text);
            continue;
        }
        char res[CALC_NOTE_OUT];
        calc_note_result(c, res, sizeof res);
        put(&k, "> %s\n", c->text);
        for (const char *p = res; *p;) {
            size_t n = strcspn(p, "\n");
            put(&k, "  %s%.*s\n", c->bad ? "! " : "", (int)n, p);
            p += n + (p[n] == '\n');
        }
    }
    return k.len;
}
