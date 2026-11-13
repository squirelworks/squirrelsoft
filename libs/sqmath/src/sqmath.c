/* sqmath: parser, evaluator, printer and compiled programs (see sqmath.h). Original code. */
#include "sqmath.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqmath_internal.h"

static const struct {
    const char *name;
    int args;
} FNS[SQM_FN_COUNT] = {
    [SQM_FN_SIN] = {"sin", 1},     [SQM_FN_COS] = {"cos", 1},     [SQM_FN_TAN] = {"tan", 1},
    [SQM_FN_ASIN] = {"asin", 1},   [SQM_FN_ACOS] = {"acos", 1},   [SQM_FN_ATAN] = {"atan", 1},
    [SQM_FN_SINH] = {"sinh", 1},   [SQM_FN_COSH] = {"cosh", 1},   [SQM_FN_TANH] = {"tanh", 1},
    [SQM_FN_EXP] = {"exp", 1},     [SQM_FN_LN] = {"ln", 1},       [SQM_FN_LOG] = {"log", 1},
    [SQM_FN_SQRT] = {"sqrt", 1},   [SQM_FN_CBRT] = {"cbrt", 1},   [SQM_FN_ABS] = {"abs", 1},
    [SQM_FN_FLOOR] = {"floor", 1}, [SQM_FN_CEIL] = {"ceil", 1},   [SQM_FN_ROUND] = {"round", 1},
    [SQM_FN_SIGN] = {"sign", 1},   [SQM_FN_FACT] = {"fact", 1},   [SQM_FN_ATAN2] = {"atan2", 2},
    [SQM_FN_HYPOT] = {"hypot", 2}, [SQM_FN_MIN] = {"min", 2},     [SQM_FN_MAX] = {"max", 2},
    [SQM_FN_MOD] = {"mod", 2},
};

const char *sqm_fn_name(SqmFn fn) { return (unsigned)fn < SQM_FN_COUNT ? FNS[fn].name : "?"; }

int sqm_fn_args(SqmFn fn) { return (unsigned)fn < SQM_FN_COUNT ? FNS[fn].args : 0; }

const char *sqm_strerror(int code) {
    switch (code) {
        case SQM_OK: return "ok";
        case SQM_ERR_SYNTAX: return "syntax error";
        case SQM_ERR_FULL: return "expression too large";
        case SQM_ERR_UNKNOWN: return "unknown name";
        case SQM_ERR_ARGS: return "wrong number of arguments";
        case SQM_ERR_DEPTH: return "nested too deeply";
        case SQM_ERR_DIFF: return "no derivative rule";
        case SQM_ERR_NAME: return "name too long";
        case SQM_ERR_RANGE: return "numbers too big to keep exact";
        case SQM_ERR_SOLVE: return "can't solve this exactly";
        default: return "error";
    }
}

/* ---- function values ---- */

static double round_half_away(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }

double sqm__apply(int fn, double x, double y) {
    switch (fn) {
        case SQM_FN_SIN: return sin(x);
        case SQM_FN_COS: return cos(x);
        case SQM_FN_TAN: return tan(x);
        case SQM_FN_ASIN: return asin(x);
        case SQM_FN_ACOS: return acos(x);
        case SQM_FN_ATAN: return atan(x);
        case SQM_FN_SINH: return sinh(x);
        case SQM_FN_COSH: return cosh(x);
        case SQM_FN_TANH: return tanh(x);
        case SQM_FN_EXP: return exp(x);
        case SQM_FN_LN: return log(x);
        case SQM_FN_LOG: return log10(x);
        case SQM_FN_SQRT: return sqrt(x);
        case SQM_FN_CBRT: return cbrt(x);
        case SQM_FN_ABS: return fabs(x);
        case SQM_FN_FLOOR: return floor(x);
        case SQM_FN_CEIL: return ceil(x);
        case SQM_FN_ROUND: return round_half_away(x);
        case SQM_FN_SIGN: return x > 0 ? 1.0 : x < 0 ? -1.0 : x; /* keeps 0 and NaN */
        case SQM_FN_FACT:
            /* gamma(x+1); negative integers are poles */
            if (x < 0 && x == floor(x)) return NAN;
            return tgamma(x + 1.0);
        case SQM_FN_ATAN2: return atan2(x, y);
        case SQM_FN_HYPOT: return hypot(x, y);
        case SQM_FN_MIN: return x < y ? x : y;
        case SQM_FN_MAX: return x > y ? x : y;
        case SQM_FN_MOD: {
            /* result has the divisor's sign, as calculators do: mod(-1, 3) = 2 */
            double r = fmod(x, y);
            if (r != 0 && ((r < 0) != (y < 0))) r += y;
            return r;
        }
        default: return NAN;
    }
}

/* ---- building nodes ---- */

int sqm__node(SqmExpr *e, int kind, int fn, int a, int b, double v) {
    if (e->n >= SQM_MAX_NODES) return -1;
    SqmNode *nd = &e->node[e->n];
    nd->kind = (uint8_t)kind;
    nd->fn = (uint8_t)fn;
    nd->a = (int16_t)a;
    nd->b = (int16_t)b;
    nd->v = v;
    return e->n++;
}

int sqm__name(SqmExpr *e, const char *s, size_t len) {
    if (len == 0 || len >= SQM_NAME_LEN) return -2;
    for (int i = 0; i < e->nnames; i++)
        if (strlen(e->names[i]) == len && memcmp(e->names[i], s, len) == 0) return i;
    if (e->nnames >= SQM_MAX_NAMES) return -1;
    memcpy(e->names[e->nnames], s, len);
    e->names[e->nnames][len] = 0;
    return e->nnames++;
}

void sqm__clear(SqmExpr *e) {
    e->n = 0;
    e->root = -1;
    e->nnames = 0;
}

/* ---- parser ---- */

typedef struct {
    const char *s;
    int pos;
    int depth;
    SqmExpr *e;
    SqmError *err;
    int code;
} Parser;

static int fail(Parser *p, int code, int pos, const char *msg) {
    if (p->code == SQM_OK) {
        p->code = code;
        if (p->err) {
            p->err->code = code;
            p->err->pos = pos;
            snprintf(p->err->msg, sizeof p->err->msg, "%s", msg);
        }
    }
    return -1;
}

static void skip_ws(Parser *p) {
    while (p->s[p->pos] == ' ' || p->s[p->pos] == '\t' || p->s[p->pos] == '\n' || p->s[p->pos] == '\r') p->pos++;
}

static int peek(Parser *p) {
    skip_ws(p);
    return (unsigned char)p->s[p->pos];
}

static int is_name_start(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_name_char(int c) { return is_name_start(c) || (c >= '0' && c <= '9'); }
static int is_digit(int c) { return c >= '0' && c <= '9'; }

static int mk(Parser *p, int kind, int fn, int a, int b, double v) {
    if (a < 0 && (kind >= SQM_NEG)) return -1;
    if (b < 0 && kind >= SQM_ADD && kind <= SQM_POW) return -1;
    int i = sqm__node(p->e, kind, fn, a, b, v);
    if (i < 0) return fail(p, SQM_ERR_FULL, p->pos, "expression too large");
    return i;
}

static int parse_sum(Parser *p);

static int enter(Parser *p) {
    if (++p->depth > SQM_MAX_DEPTH) return fail(p, SQM_ERR_DEPTH, p->pos, "nested too deeply");
    return 0;
}

static int parse_number(Parser *p) {
    int start = p->pos;
    const char *s = p->s;
    int i = p->pos, digits = 0;
    while (is_digit(s[i])) i++, digits++;
    if (s[i] == '.') {
        i++;
        while (is_digit(s[i])) i++, digits++;
    }
    if (!digits) return fail(p, SQM_ERR_SYNTAX, start, "expected a number");
    if (s[i] == '.') return fail(p, SQM_ERR_SYNTAX, i, "malformed number");
    if (s[i] == 'e' || s[i] == 'E') {
        int j = i + 1;
        if (s[j] == '+' || s[j] == '-') j++;
        if (is_digit(s[j])) {
            while (is_digit(s[j])) j++;
            i = j;
        } /* else "2e" is 2 times the constant e */
    }
    char buf[64];
    int len = i - start;
    if (len >= (int)sizeof buf) return fail(p, SQM_ERR_SYNTAX, start, "number too long");
    memcpy(buf, s + start, (size_t)len);
    buf[len] = 0;
    p->pos = i;
    return mk(p, SQM_NUM, 0, -1, -1, strtod(buf, NULL));
}

static int parse_call(Parser *p, int fn, int at) {
    int args[2] = {-1, -1}, n = 0;
    p->pos++; /* ( */
    if (peek(p) == ')') return fail(p, SQM_ERR_ARGS, p->pos, "missing argument");
    for (;;) {
        int a = parse_sum(p);
        if (a < 0) return -1;
        if (n < 2) args[n] = a;
        n++;
        int c = peek(p);
        if (c == ',') {
            p->pos++;
            continue;
        }
        if (c == ')') {
            p->pos++;
            break;
        }
        return fail(p, SQM_ERR_SYNTAX, p->pos, "expected , or )");
    }
    if (n != FNS[fn].args) {
        char msg[64];
        snprintf(msg, sizeof msg, "%s takes %d argument%s", FNS[fn].name, FNS[fn].args, FNS[fn].args == 1 ? "" : "s");
        return fail(p, SQM_ERR_ARGS, at, msg);
    }
    return mk(p, SQM_FUN, fn, args[0], args[1], 0);
}

static int parse_primary(Parser *p) {
    int c = peek(p);
    int at = p->pos;
    if (is_digit(c) || (c == '.' && is_digit((unsigned char)p->s[p->pos + 1]))) return parse_number(p);
    if (c == '(') {
        p->pos++;
        if (enter(p)) return -1;
        int a = parse_sum(p);
        p->depth--;
        if (a < 0) return -1;
        if (peek(p) != ')') return fail(p, SQM_ERR_SYNTAX, p->pos, "expected )");
        p->pos++;
        return a;
    }
    if (is_name_start(c)) {
        int i = p->pos;
        while (is_name_char((unsigned char)p->s[i])) i++;
        size_t len = (size_t)(i - p->pos);
        const char *name = p->s + p->pos;
        p->pos = i;
        for (int f = 0; f < SQM_FN_COUNT; f++)
            if (strlen(FNS[f].name) == len && memcmp(FNS[f].name, name, len) == 0) {
                if (peek(p) != '(') return fail(p, SQM_ERR_SYNTAX, p->pos, "expected ( after a function name");
                if (enter(p)) return -1;
                int r = parse_call(p, f, at);
                p->depth--;
                return r;
            }
        if (len == 2 && memcmp(name, "pi", 2) == 0) return mk(p, SQM_CONST, 0, 0, -1, SQM_PI);
        if (len == 1 && name[0] == 'e') return mk(p, SQM_CONST, 0, 1, -1, SQM_E);
        int k = sqm__name(p->e, name, len);
        if (k == -2) return fail(p, SQM_ERR_NAME, at, "name too long");
        if (k < 0) return fail(p, SQM_ERR_FULL, at, "too many names");
        return mk(p, SQM_VAR, 0, k, -1, 0);
    }
    if (c == 0) return fail(p, SQM_ERR_SYNTAX, p->pos, "unexpected end");
    return fail(p, SQM_ERR_SYNTAX, p->pos, "unexpected character");
}

static int parse_postfix(Parser *p) {
    int a = parse_primary(p);
    while (a >= 0 && peek(p) == '!') {
        p->pos++;
        a = mk(p, SQM_FUN, SQM_FN_FACT, a, -1, 0);
    }
    return a;
}

static int parse_unary(Parser *p);

static int parse_power(Parser *p) {
    int a = parse_postfix(p);
    if (a < 0) return -1;
    int c = peek(p);
    int star2 = c == '*' && p->s[p->pos + 1] == '*';
    if (c == '^' || star2) {
        p->pos += star2 ? 2 : 1;
        if (enter(p)) return -1;
        int b = parse_unary(p); /* right to left, and 2^-1 */
        p->depth--;
        if (b < 0) return -1;
        return mk(p, SQM_POW, 0, a, b, 0);
    }
    return a;
}

static int parse_unary(Parser *p) {
    int c = peek(p);
    if (c == '-' || c == '+') {
        p->pos++;
        if (enter(p)) return -1;
        int a = parse_unary(p);
        p->depth--;
        if (a < 0) return -1;
        return c == '-' ? mk(p, SQM_NEG, 0, a, -1, 0) : a;
    }
    return parse_power(p);
}

static int parse_product(Parser *p) {
    int a = parse_unary(p);
    while (a >= 0) {
        int c = peek(p);
        int kind;
        if (c == '*' && p->s[p->pos + 1] != '*') {
            p->pos++;
            kind = SQM_MUL;
        } else if (c == '/') {
            p->pos++;
            kind = SQM_DIV;
        } else if (is_name_start(c) || c == '(' || is_digit(c) || c == '.') {
            /* implied multiplication: 2x, 2(x+1), x y, (a)(b), x 2 */
            kind = SQM_MUL;
        } else {
            break;
        }
        int b = parse_unary(p);
        if (b < 0) return -1;
        a = mk(p, kind, 0, a, b, 0);
    }
    return a;
}

static int parse_sum(Parser *p) {
    int a = parse_product(p);
    while (a >= 0) {
        int c = peek(p);
        if (c != '+' && c != '-') break;
        p->pos++;
        int b = parse_product(p);
        if (b < 0) return -1;
        a = mk(p, c == '+' ? SQM_ADD : SQM_SUB, 0, a, b, 0);
    }
    return a;
}

int sqm_parse(const char *text, SqmExpr *out, SqmError *err) {
    Parser p = {text ? text : "", 0, 0, out, err, SQM_OK};
    sqm__clear(out);
    if (err) {
        err->code = SQM_OK;
        err->pos = 0;
        err->msg[0] = 0;
    }
    if (peek(&p) == 0) {
        fail(&p, SQM_ERR_SYNTAX, 0, "empty expression");
        return p.code;
    }
    int r = parse_sum(&p);
    if (r >= 0 && peek(&p) != 0) {
        int c = peek(&p);
        fail(&p, SQM_ERR_SYNTAX, p.pos, c == ')' ? "unmatched )" : "unexpected character");
        r = -1;
    }
    if (r < 0) {
        sqm__clear(out);
        return p.code != SQM_OK ? p.code : SQM_ERR_SYNTAX;
    }
    out->root = r;
    return SQM_OK;
}

/* ---- evaluation ---- */

typedef struct {
    const SqmExpr *e;
    double vals[SQM_MAX_NAMES];
    int have[SQM_MAX_NAMES];
    int missing;
} EvalCtx;

static double eval_node(EvalCtx *c, int i) {
    const SqmNode *n = &c->e->node[i];
    switch (n->kind) {
        case SQM_NUM:
        case SQM_CONST: return n->v;
        case SQM_VAR:
            if (!c->have[n->a]) c->missing = 1;
            return c->vals[n->a];
        case SQM_NEG: return -eval_node(c, n->a);
        case SQM_ADD: return eval_node(c, n->a) + eval_node(c, n->b);
        case SQM_SUB: return eval_node(c, n->a) - eval_node(c, n->b);
        case SQM_MUL: return eval_node(c, n->a) * eval_node(c, n->b);
        case SQM_DIV: return eval_node(c, n->a) / eval_node(c, n->b);
        case SQM_POW: return pow(eval_node(c, n->a), eval_node(c, n->b));
        case SQM_FUN: {
            double x = eval_node(c, n->a);
            double y = FNS[n->fn].args == 2 ? eval_node(c, n->b) : 0;
            return sqm__apply(n->fn, x, y);
        }
        default: return NAN;
    }
}

int sqm_eval(const SqmExpr *e, const char *const *names, const double *values, int n, double *result) {
    EvalCtx c;
    c.e = e;
    c.missing = 0;
    for (int k = 0; k < e->nnames; k++) {
        c.have[k] = 0;
        c.vals[k] = NAN;
        for (int i = 0; i < n; i++)
            if (names && names[i] && strcmp(names[i], e->names[k]) == 0) {
                c.have[k] = 1;
                c.vals[k] = values[i];
            }
    }
    if (e->root < 0) return SQM_ERR_SYNTAX;
    double r = eval_node(&c, e->root);
    if (c.missing) return SQM_ERR_UNKNOWN;
    if (result) *result = r;
    return SQM_OK;
}

static int uses_node(const SqmExpr *e, int i, int name) {
    const SqmNode *n = &e->node[i];
    switch (n->kind) {
        case SQM_NUM:
        case SQM_CONST: return 0;
        case SQM_VAR: return n->a == name;
        case SQM_NEG: return uses_node(e, n->a, name);
        case SQM_FUN:
            return uses_node(e, n->a, name) || (FNS[n->fn].args == 2 && uses_node(e, n->b, name));
        default: return uses_node(e, n->a, name) || uses_node(e, n->b, name);
    }
}

int sqm__uses_index(const SqmExpr *e, int i, int name) { return name >= 0 && uses_node(e, i, name); }

int sqm__find_name(const SqmExpr *e, const char *name) {
    for (int k = 0; k < e->nnames; k++)
        if (strcmp(e->names[k], name) == 0) return k;
    return -1;
}

int sqm_uses(const SqmExpr *e, const char *name) {
    int k = sqm__find_name(e, name);
    return e->root >= 0 && k >= 0 && uses_node(e, e->root, k);
}

/* ---- compiled programs ---- */

enum { OP_K, OP_VAR, OP_NEG, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_FUN };

typedef struct {
    const SqmExpr *e;
    SqmProgram *p;
    int slot_of[SQM_MAX_NAMES];
    int depth, code;
} Compiler;

static void emit(Compiler *c, int op, int arg, double k, int delta) {
    SqmProgram *p = c->p;
    if (p->nops >= SQM_MAX_OPS) {
        c->code = SQM_ERR_FULL;
        return;
    }
    p->op[p->nops] = (uint8_t)op;
    p->arg[p->nops] = (int16_t)arg;
    p->k[p->nops] = k;
    p->nops++;
    c->depth += delta;
    if (c->depth > p->stack) p->stack = c->depth;
}

static void compile_node(Compiler *c, int i) {
    if (c->code) return;
    const SqmNode *n = &c->e->node[i];
    switch (n->kind) {
        case SQM_NUM:
        case SQM_CONST: emit(c, OP_K, 0, n->v, 1); break;
        case SQM_VAR:
            if (c->slot_of[n->a] < 0) {
                c->code = SQM_ERR_UNKNOWN;
                return;
            }
            emit(c, OP_VAR, c->slot_of[n->a], 0, 1);
            break;
        case SQM_NEG:
            compile_node(c, n->a);
            emit(c, OP_NEG, 0, 0, 0);
            break;
        case SQM_FUN:
            compile_node(c, n->a);
            if (FNS[n->fn].args == 2) compile_node(c, n->b);
            emit(c, OP_FUN, n->fn, 0, FNS[n->fn].args == 2 ? -1 : 0);
            break;
        default:
            compile_node(c, n->a);
            compile_node(c, n->b);
            emit(c, OP_ADD + (n->kind - SQM_ADD), 0, 0, -1);
            break;
    }
}

int sqm_compile(const SqmExpr *e, const char *const *slots, int nslots, SqmProgram *p) {
    Compiler c = {e, p, {0}, 0, SQM_OK};
    p->nops = 0;
    p->stack = 0;
    p->nslots = nslots;
    if (e->root < 0) return SQM_ERR_SYNTAX;
    for (int k = 0; k < e->nnames; k++) {
        c.slot_of[k] = -1;
        for (int i = 0; i < nslots; i++)
            if (slots && slots[i] && strcmp(slots[i], e->names[k]) == 0) c.slot_of[k] = i;
    }
    compile_node(&c, e->root);
    if (c.code) {
        p->nops = 0;
        return c.code;
    }
    if (p->stack > SQM_MAX_DEPTH * 2 + 8) {
        p->nops = 0;
        return SQM_ERR_DEPTH;
    }
    return SQM_OK;
}

double sqm_run(const SqmProgram *p, const double *values) {
    double st[SQM_MAX_DEPTH * 2 + 8];
    int sp = 0;
    if (p->nops == 0) return NAN;
    for (int i = 0; i < p->nops; i++) {
        switch (p->op[i]) {
            case OP_K: st[sp++] = p->k[i]; break;
            case OP_VAR: st[sp++] = values[p->arg[i]]; break;
            case OP_NEG: st[sp - 1] = -st[sp - 1]; break;
            case OP_ADD: sp--; st[sp - 1] += st[sp]; break;
            case OP_SUB: sp--; st[sp - 1] -= st[sp]; break;
            case OP_MUL: sp--; st[sp - 1] *= st[sp]; break;
            case OP_DIV: sp--; st[sp - 1] /= st[sp]; break;
            case OP_POW: sp--; st[sp - 1] = pow(st[sp - 1], st[sp]); break;
            case OP_FUN:
                if (FNS[p->arg[i]].args == 2) {
                    sp--;
                    st[sp - 1] = sqm__apply(p->arg[i], st[sp - 1], st[sp]);
                } else {
                    st[sp - 1] = sqm__apply(p->arg[i], st[sp - 1], 0);
                }
                break;
        }
    }
    return st[0];
}

/* ---- printing ---- */

typedef struct {
    char *out;
    size_t cap, len;
} Buf;

static void put(Buf *b, const char *s) {
    for (; *s; s++, b->len++)
        if (b->len + 1 < b->cap) b->out[b->len] = *s;
}

/* binding strength: sum 1, product 2, unary minus 3, power 4, atoms 5 */
static int prec(const SqmExpr *e, int i) {
    const SqmNode *n = &e->node[i];
    switch (n->kind) {
        case SQM_ADD:
        case SQM_SUB: return 1;
        case SQM_MUL:
        case SQM_DIV: return 2;
        case SQM_NEG: return 3;
        case SQM_POW: return 4;
        case SQM_NUM: return n->v < 0 || (n->v == 0 && signbit(n->v)) ? 3 : 5;
        default: return 5;
    }
}

static void print_node(const SqmExpr *e, int i, Buf *b);

static void print_child(const SqmExpr *e, int i, Buf *b, int parens) {
    if (parens) put(b, "(");
    print_node(e, i, b);
    if (parens) put(b, ")");
}

static void print_number(double v, Buf *b) {
    char t[40];
    if (isnan(v)) {
        put(b, "(0/0)");
        return;
    }
    if (isinf(v)) {
        put(b, v > 0 ? "(1/0)" : "(-1/0)");
        return;
    }
    snprintf(t, sizeof t, "%.15g", v);
    if (strtod(t, NULL) != v) snprintf(t, sizeof t, "%.17g", v);
    put(b, t);
}

static void print_node(const SqmExpr *e, int i, Buf *b) {
    const SqmNode *n = &e->node[i];
    int p = prec(e, i);
    switch (n->kind) {
        case SQM_NUM: print_number(n->v, b); break;
        case SQM_CONST: put(b, n->a == 0 ? "pi" : "e"); break;
        case SQM_VAR: put(b, e->names[n->a]); break;
        case SQM_NEG:
            put(b, "-");
            /* -x*y reads back as (-x)*y: the same value, and easier to read
             * than -(x*y); a sum keeps its parentheses */
            print_child(e, n->a, b, prec(e, n->a) < 2);
            break;
        case SQM_FUN:
            if (n->fn == SQM_FN_FACT) {
                print_child(e, n->a, b, prec(e, n->a) < 5);
                put(b, "!");
                break;
            }
            put(b, FNS[n->fn].name);
            put(b, "(");
            print_node(e, n->a, b);
            if (FNS[n->fn].args == 2) {
                put(b, ", ");
                print_node(e, n->b, b);
            }
            put(b, ")");
            break;
        case SQM_POW:
            /* left: anything looser than an atom, or a power (right to left);
             * right: unary minus and atoms bind as the parser reads them */
            print_child(e, n->a, b, prec(e, n->a) <= 4);
            put(b, "^");
            print_child(e, n->b, b, prec(e, n->b) < 3);
            break;
        default: {
            const char *op = n->kind == SQM_ADD ? " + " : n->kind == SQM_SUB ? " - " : n->kind == SQM_MUL ? "*" : "/";
            int pa = prec(e, n->a), pb = prec(e, n->b);
            /* a unary minus on the left of * or / reads back the same: -x*y = (-x)*y */
            print_child(e, n->a, b, pa < p);
            put(b, op);
            /* right side: looser needs parens; the same strength too after - and /
             * (a - (b - c)), but not after + and * of the same kind, which only
             * regroup (a + b + c); a unary minus reads fine after + and -, and
             * gets parens after * and / */
            const SqmNode *rn = &e->node[n->b];
            int rp = pb < p || (pb == p && !((n->kind == SQM_ADD || n->kind == SQM_MUL) && rn->kind == n->kind));
            if (pb == 3) rp = p >= 2;
            print_child(e, n->b, b, rp);
            break;
        }
    }
}

size_t sqm_print(const SqmExpr *e, char *out, size_t cap) {
    Buf b = {out, cap, 0};
    if (e->root >= 0) print_node(e, e->root, &b);
    if (cap) out[b.len < cap ? b.len : cap - 1] = 0;
    return b.len;
}

/* ---- display ---- */

size_t sqm_format(double x, int digits, char *out, size_t cap) {
    char t[64];
    if (digits < 1) digits = 1;
    if (digits > 17) digits = 17;
    if (!isfinite(x)) {
        /* one word for every value that isn't a number: 0/0, sqrt(-1), 1/0, overflow */
        snprintf(t, sizeof t, "undefined");
    } else if (x == 0) {
        snprintf(t, sizeof t, "0");
    } else {
        /* round to the digits first, so 999999.99999999 shows as 1000000 */
        char r[40];
        snprintf(r, sizeof r, "%.*e", digits - 1, x);
        double v = strtod(r, NULL);
        double av = fabs(v);
        if (av >= 1e15 || av < 1e-6) {
            /* mantissa without trailing zeros, then e±N */
            char *ep = strchr(r, 'e');
            int ex = ep ? atoi(ep + 1) : 0;
            if (ep) *ep = 0;
            char *dot = strchr(r, '.');
            if (dot) {
                char *end = r + strlen(r) - 1;
                while (end > dot && *end == '0') *end-- = 0;
                if (end == dot) *end = 0;
            }
            snprintf(t, sizeof t, "%se%d", r, ex);
        } else {
            int intdigits = av >= 1 ? (int)floor(log10(av)) + 1 : 1;
            int dec = digits - intdigits;
            if (av < 1) dec = digits - 1 - (int)floor(log10(av)); /* leading zeros after the point */
            if (dec < 0) dec = 0;
            if (dec > 20) dec = 20;
            snprintf(t, sizeof t, "%.*f", dec, v);
            char *dot = strchr(t, '.');
            if (dot) {
                char *end = t + strlen(t) - 1;
                while (end > dot && *end == '0') *end-- = 0;
                if (end == dot) *end = 0;
            }
            if (strcmp(t, "-0") == 0) snprintf(t, sizeof t, "0");
        }
    }
    size_t n = strlen(t);
    if (cap) {
        size_t m = n < cap ? n : cap - 1;
        memcpy(out, t, m);
        out[m] = 0;
    }
    return n;
}
