/*
 * NSPredicate format strings: parsed once (with the varargs substituted) into a
 * small tree and evaluated against objects. Covers comparisons (==, !=, <, <=,
 * >, >=), BEGINSWITH / ENDSWITH / CONTAINS / LIKE / MATCHES / IN / BETWEEN with
 * [c] [d] options, AND / OR / NOT, ANY / ALL / NONE over collections, SELF, key
 * paths (with @count), literals and %@ %K %d %i %ld %u %f %s substitutions.
 */
#include "../internal.h"
#include <ctype.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>

enum { N_TRUE, N_FALSE, N_AND, N_OR, N_NOT, N_CMP };
enum { E_SELF, E_KEYPATH, E_CONST, E_LIST };
enum { OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE, OP_BEGINS, OP_ENDS, OP_CONTAINS, OP_LIKE, OP_MATCHES, OP_IN, OP_BETWEEN };
enum { Q_NONE, Q_ANY, Q_ALL, Q_NONEOF };

struct pexpr { int kind; id value; struct pexpr **items; int nitems; };   /* value: keypath string or constant */
struct pnode { int kind; struct pnode *a, *b; int op, opts, quant; struct pexpr *l, *r; };

struct parser { const char *s; size_t p; const uint64_t *args; int argi; id argarray; int err; };

static void skip_ws(struct parser *P) { while (P->s[P->p] && isspace((unsigned char)P->s[P->p])) P->p++; }
/* case-insensitive keyword at the cursor, not followed by an identifier character */
static int kw(struct parser *P, const char *k) {
    skip_ws(P); size_t n = strlen(k);
    if (strncasecmp(P->s + P->p, k, n)) return 0;
    char c = P->s[P->p + n];
    if (isalnum((unsigned char)k[n - 1]) && (isalnum((unsigned char)c) || c == '_')) return 0;
    P->p += n; return 1;
}
static int sym(struct parser *P, const char *k) { skip_ws(P); size_t n = strlen(k); if (strncmp(P->s + P->p, k, n)) return 0; P->p += n; return 1; }

static id next_arg_obj(struct parser *P) {
    if (P->argarray) { id a = (size_t)P->argi < rt_array_count(P->argarray) ? rt_array_at(P->argarray, (size_t)P->argi) : NULL; P->argi++; return a; }
    return P->args ? (id)(uintptr_t)P->args[P->argi++] : NULL;
}
static uint64_t next_arg_word(struct parser *P) {
    if (P->argarray) { id a = next_arg_obj(P); return a ? (uint64_t)RT_SEND0(long long, a, "longLongValue") : 0; }
    return P->args ? P->args[P->argi++] : 0;
}
static double next_arg_double(struct parser *P) {
    if (P->argarray) { id a = next_arg_obj(P); return a ? RT_SEND0(double, a, "doubleValue") : 0; }
    double d = 0; if (P->args) memcpy(&d, &P->args[P->argi++], 8); return d;
}
static id num_bool(int v) { return RT_SEND1(id, (id)rt_class_named("NSNumber"), "numberWithBool:", long, v ? 1 : 0); }

static struct pexpr *mk_expr(int kind, id v) { struct pexpr *e = rt_xcalloc(1, sizeof *e); e->kind = kind; e->value = objc_retain(v); return e; }
static struct pnode *parse_or(struct parser *P);

static struct pexpr *parse_operand(struct parser *P) {
    skip_ws(P);
    const char *s = P->s; char c = s[P->p];
    if (c == '%') {
        P->p++;
        if (s[P->p] == '@') { P->p++; return mk_expr(E_CONST, next_arg_obj(P)); }
        if (s[P->p] == 'K') { P->p++; return mk_expr(E_KEYPATH, next_arg_obj(P)); }
        int lng = 0; while (s[P->p] == 'l' || s[P->p] == 'q' || s[P->p] == 'z') { lng = 1; P->p++; }
        (void)lng;
        char f = s[P->p++];
        if (f == 'd' || f == 'i') return mk_expr(E_CONST, rt_num_int((long long)(int64_t)next_arg_word(P)));
        if (f == 'u' || f == 'x') return mk_expr(E_CONST, rt_num_int((long long)next_arg_word(P)));
        if (f == 'f' || f == 'g' || f == 'e') return mk_expr(E_CONST, rt_num_dbl(next_arg_double(P)));
        if (f == 's') { const char *cs = (const char *)(uintptr_t)next_arg_word(P); return mk_expr(E_CONST, rt_str(cs ? cs : "")); }
        if (f == 'c') return mk_expr(E_CONST, rt_num_int((long long)(char)next_arg_word(P)));
        P->err = 1; return mk_expr(E_CONST, NULL);
    }
    if (c == '\'' || c == '"') {
        char q = c; size_t st = ++P->p; char *buf = rt_xmalloc(strlen(s) + 1); size_t o = 0;
        while (s[P->p] && s[P->p] != q) { if (s[P->p] == '\\' && s[P->p + 1]) P->p++; buf[o++] = s[P->p++]; }
        (void)st; buf[o] = 0; if (s[P->p] == q) P->p++;
        struct pexpr *e = mk_expr(E_CONST, rt_str(buf)); free(buf); return e;
    }
    if (c == '{') {
        P->p++; struct pexpr *e = mk_expr(E_LIST, NULL);
        skip_ws(P);
        if (!sym(P, "}")) for (;;) {
            struct pexpr *it = parse_operand(P);
            e->items = realloc(e->items, sizeof *e->items * (size_t)(e->nitems + 1)); e->items[e->nitems++] = it;
            if (sym(P, ",")) continue;
            if (!sym(P, "}")) P->err = 1;
            break;
        }
        return e;
    }
    if (isdigit((unsigned char)c) || ((c == '-' || c == '+' || c == '.') && isdigit((unsigned char)s[P->p + 1]))) {
        char *end; double d = strtod(s + P->p, &end);
        int isint = 1; for (const char *q = s + P->p; q < end; q++) if (*q == '.' || *q == 'e' || *q == 'E') isint = 0;
        P->p = (size_t)(end - s);
        return mk_expr(E_CONST, isint ? rt_num_int((long long)d) : rt_num_dbl(d));
    }
    if (kw(P, "SELF")) {
        /* SELF.key... is a key path on the object itself */
        if (s[P->p] == '.') { P->p++; goto keypath; }
        return mk_expr(E_SELF, NULL);
    }
    if (kw(P, "YES") || kw(P, "TRUE")) return mk_expr(E_CONST, num_bool(1));
    if (kw(P, "NO") || kw(P, "FALSE")) return mk_expr(E_CONST, num_bool(0));
    if (kw(P, "NIL") || kw(P, "NULL")) return mk_expr(E_CONST, NULL);
keypath:
    skip_ws(P);
    {
        size_t st = P->p;
        if (s[P->p] == '#') { P->p++; st = P->p; }   /* #reservedWord escape */
        while (s[P->p] && (isalnum((unsigned char)s[P->p]) || s[P->p] == '_' || s[P->p] == '.' || s[P->p] == '@' || s[P->p] == '$')) P->p++;
        if (P->p == st) { P->err = 1; return mk_expr(E_CONST, NULL); }
        char *k = strndup(s + st, P->p - st);
        struct pexpr *e = mk_expr(E_KEYPATH, rt_str(k)); free(k); return e;
    }
}
static int parse_opts(struct parser *P) {
    int o = 0; skip_ws(P);
    if (P->s[P->p] == '[') {
        P->p++;
        while (P->s[P->p] && P->s[P->p] != ']') { char c = (char)tolower((unsigned char)P->s[P->p++]); if (c == 'c') o |= 1; else if (c == 'd') o |= 2; else if (c == 'n') o |= 4; }
        if (P->s[P->p] == ']') P->p++;
    }
    return o;
}
static struct pnode *parse_cmp(struct parser *P) {
    struct pnode *n = rt_xcalloc(1, sizeof *n); n->kind = N_CMP;
    if (kw(P, "ANY") || kw(P, "SOME")) n->quant = Q_ANY;
    else if (kw(P, "ALL")) n->quant = Q_ALL;
    else if (kw(P, "NONE")) n->quant = Q_NONEOF;
    n->l = parse_operand(P);
    skip_ws(P);
    static const struct { const char *t; int op; } ops[] = {
        { "==", OP_EQ }, { "!=", OP_NE }, { "<>", OP_NE }, { "<=", OP_LE }, { "=<", OP_LE }, { ">=", OP_GE }, { "=>", OP_GE },
        { "=", OP_EQ }, { "<", OP_LT }, { ">", OP_GT } };
    int found = 0;
    for (size_t i = 0; i < sizeof ops / sizeof *ops; i++) if (sym(P, ops[i].t)) { n->op = ops[i].op; found = 1; break; }
    if (!found) {
        static const struct { const char *t; int op; } words[] = {
            { "BEGINSWITH", OP_BEGINS }, { "ENDSWITH", OP_ENDS }, { "CONTAINS", OP_CONTAINS }, { "LIKE", OP_LIKE },
            { "MATCHES", OP_MATCHES }, { "IN", OP_IN }, { "BETWEEN", OP_BETWEEN } };
        for (size_t i = 0; i < sizeof words / sizeof *words; i++) if (kw(P, words[i].t)) { n->op = words[i].op; found = 1; break; }
    }
    if (!found) { P->err = 1; return n; }
    n->opts = parse_opts(P);
    n->r = parse_operand(P);
    return n;
}
static struct pnode *parse_unary(struct parser *P) {
    skip_ws(P);
    if (kw(P, "NOT") || sym(P, "!")) { struct pnode *n = rt_xcalloc(1, sizeof *n); n->kind = N_NOT; n->a = parse_unary(P); return n; }
    if (kw(P, "TRUEPREDICATE")) { struct pnode *n = rt_xcalloc(1, sizeof *n); n->kind = N_TRUE; return n; }
    if (kw(P, "FALSEPREDICATE")) { struct pnode *n = rt_xcalloc(1, sizeof *n); n->kind = N_FALSE; return n; }
    if (sym(P, "(")) { struct pnode *n = parse_or(P); if (!sym(P, ")")) P->err = 1; return n; }
    return parse_cmp(P);
}
static struct pnode *parse_and(struct parser *P) {
    struct pnode *l = parse_unary(P);
    while (!P->err && (kw(P, "AND") || sym(P, "&&"))) { struct pnode *n = rt_xcalloc(1, sizeof *n); n->kind = N_AND; n->a = l; n->b = parse_unary(P); l = n; }
    return l;
}
static struct pnode *parse_or(struct parser *P) {
    struct pnode *l = parse_and(P);
    while (!P->err && (kw(P, "OR") || sym(P, "||"))) { struct pnode *n = rt_xcalloc(1, sizeof *n); n->kind = N_OR; n->a = l; n->b = parse_and(P); l = n; }
    return l;
}

/* ---------------------------------------------------------------- evaluation */
static int is_kind(id o, const char *cls) { Class c = (Class)rt_class_named(cls); return o && c && rt_is_kind_of(o, c); }
static int is_collection(id o) { return is_kind(o, "NSArray") || is_kind(o, "NSSet") || is_kind(o, "NSOrderedSet"); }
static id to_array(id o) {
    if (!o) return NULL;
    if (is_kind(o, "NSArray")) return o;
    if (is_kind(o, "NSSet") || is_kind(o, "NSDictionary")) return RT_SEND0(id, o, is_kind(o, "NSDictionary") ? "allValues" : "allObjects");
    return NULL;
}
static id keypath_value(id obj, id path) {
    if (!obj || !path) return NULL;
    const char *p = rt_str_utf8(path);
    if (!strcmp(p, "@count")) { id a = to_array(obj); return rt_num_int(a ? (long long)rt_array_count(a) : 0); }
    const char *at = strstr(p, ".@count");
    if (at) {
        char *head = strndup(p, (size_t)(at - p));
        id v = RT_SEND1(id, obj, "valueForKeyPath:", id, rt_str(head)); free(head);
        id a = to_array(v); return rt_num_int(a ? (long long)rt_array_count(a) : (v && rt_responds(v, rt_sel("count")) ? RT_SEND0(long long, v, "count") : 0));
    }
    return RT_SEND1(id, obj, "valueForKeyPath:", id, path);
}
static id eval_expr(struct pexpr *e, id obj) {
    switch (e->kind) {
    case E_SELF: return obj;
    case E_KEYPATH: return keypath_value(obj, e->value);
    case E_LIST: {
        id a = RT_SEND0(id, (id)rt_class_named("NSMutableArray"), "array");
        for (int i = 0; i < e->nitems; i++) { id v = eval_expr(e->items[i], obj); if (v) RT_SEND1(void, a, "addObject:", id, v); }
        return a;
    }
    default: return e->value;
    }
}
/* strings folded per [c] (case) and [d] (diacritics: Latin-1 accents dropped) */
static char *fold(id s, int opts) {
    const char *u = s ? rt_str_utf8(s) : "";
    char *r = rt_xstrdup(u ? u : "");
    if (opts & 1) for (char *p = r; *p; p++) *p = (char)tolower((unsigned char)*p);
    if (opts & 2) {
        /* collapse the common two-byte Latin-1 letters with accents to their base letter */
        static const char base[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
        char *o = r;
        for (unsigned char *p = (unsigned char *)r; *p; p++) {
            if (*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) { char b = base[p[1] - 0x80]; *o++ = (opts & 1) ? (char)tolower((unsigned char)b) : b; p++; }
            else *o++ = (char)*p;
        }
        *o = 0;
    }
    return r;
}
static int like_match(const char *pat, const char *s) {
    for (; *pat; pat++, s++) {
        if (*pat == '*') { pat++; if (!*pat) return 1; for (; *s; s++) if (like_match(pat, s)) return 1; return like_match(pat, s); }
        if (!*s) return 0;
        if (*pat == '\\' && pat[1]) pat++;
        else if (*pat == '?') continue;
        if (*pat != *s) return 0;
    }
    return !*s;
}
static int is_number(id o) { return is_kind(o, "NSNumber"); }
static int is_string(id o) { return is_kind(o, "NSString"); }
static int compare_values(id a, id b, int opts, int *ok) {
    *ok = 1;
    if (is_number(a) && is_number(b)) { double x = RT_SEND0(double, a, "doubleValue"), y = RT_SEND0(double, b, "doubleValue"); return x < y ? -1 : x > y; }
    if (is_string(a) && is_string(b)) { char *x = fold(a, opts), *y = fold(b, opts); int c = strcmp(x, y); free(x); free(y); return c < 0 ? -1 : c > 0; }
    if (a && b && rt_responds(a, rt_sel("compare:"))) return (int)RT_SEND1(long, a, "compare:", id, b);
    *ok = 0; return 0;
}
static int values_equal(id a, id b, int opts) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (is_string(a) && is_string(b) && opts) { int ok; return compare_values(a, b, opts, &ok) == 0; }
    if (is_number(a) && is_number(b)) return RT_SEND0(double, a, "doubleValue") == RT_SEND0(double, b, "doubleValue");
    return (RT_SEND1(long, a, "isEqual:", id, b) & 0xff) != 0;
}
static int eval_op(int op, int opts, id l, id r) {
    int ok, c;
    switch (op) {
    case OP_EQ: return values_equal(l, r, opts);
    case OP_NE: return !values_equal(l, r, opts);
    case OP_LT: c = compare_values(l, r, opts, &ok); return ok && c < 0;
    case OP_LE: c = compare_values(l, r, opts, &ok); return ok && c <= 0;
    case OP_GT: c = compare_values(l, r, opts, &ok); return ok && c > 0;
    case OP_GE: c = compare_values(l, r, opts, &ok); return ok && c >= 0;
    case OP_BEGINS: case OP_ENDS: case OP_LIKE: case OP_MATCHES: {
        if (!is_string(l) || !is_string(r)) return 0;
        char *x = fold(l, opts), *y = fold(r, opts); int res = 0;
        size_t nx = strlen(x), ny = strlen(y);
        if (op == OP_BEGINS) res = nx >= ny && !strncmp(x, y, ny);
        else if (op == OP_ENDS) res = nx >= ny && !strcmp(x + nx - ny, y);
        else if (op == OP_LIKE) res = like_match(y, x);
        else {
            regex_t re; char *pat = rt_xmalloc(ny + 3); snprintf(pat, ny + 3, "^%s$", y);
            if (!regcomp(&re, pat, REG_EXTENDED | REG_NOSUB | ((opts & 1) ? REG_ICASE : 0))) { res = !regexec(&re, x, 0, NULL, 0); regfree(&re); }
            free(pat);
        }
        free(x); free(y); return res;
    }
    case OP_CONTAINS:
        if (is_string(l) && is_string(r)) { char *x = fold(l, opts), *y = fold(r, opts); int res = strstr(x, y) != NULL; free(x); free(y); return res; }
        if (l && is_collection(l)) { id a = to_array(l); for (size_t i = 0; a && i < rt_array_count(a); i++) if (values_equal(rt_array_at(a, i), r, opts)) return 1; return 0; }
        return 0;
    case OP_IN:
        if (is_string(l) && is_string(r)) return eval_op(OP_CONTAINS, opts, r, l);
        if (r && is_kind(r, "NSDictionary")) return RT_SEND1(id, r, "objectForKey:", id, l) != NULL;
        if (r) { id a = to_array(r); for (size_t i = 0; a && i < rt_array_count(a); i++) if (values_equal(l, rt_array_at(a, i), opts)) return 1; }
        return 0;
    case OP_BETWEEN: {
        id a = to_array(r);
        if (!a || rt_array_count(a) < 2) return 0;
        int c1 = compare_values(l, rt_array_at(a, 0), opts, &ok); if (!ok || c1 < 0) return 0;
        int c2 = compare_values(l, rt_array_at(a, 1), opts, &ok); return ok && c2 <= 0;
    }
    }
    return 0;
}
static int eval_node(struct pnode *n, id obj) {
    if (!n) return 1;
    switch (n->kind) {
    case N_TRUE: return 1;
    case N_FALSE: return 0;
    case N_AND: return eval_node(n->a, obj) && eval_node(n->b, obj);
    case N_OR: return eval_node(n->a, obj) || eval_node(n->b, obj);
    case N_NOT: return !eval_node(n->a, obj);
    default: {
        id l = eval_expr(n->l, obj), r = eval_expr(n->r, obj);
        if (n->quant != Q_NONE) {
            id a = to_array(l);
            size_t cnt = a ? rt_array_count(a) : 0, hits = 0;
            for (size_t i = 0; i < cnt; i++) hits += eval_op(n->op, n->opts, rt_array_at(a, i), r) != 0;
            return n->quant == Q_ANY ? hits > 0 : n->quant == Q_ALL ? hits == cnt : hits == 0;
        }
        return eval_op(n->op, n->opts, l, r);
    }
    }
}

/* ---------------------------------------------------------------- glue for NSPredicate */
void *rt_predicate_parse(const char *fmt, const uint64_t *args, id argarray, int *ok) {
    struct parser P = { fmt ? fmt : "", 0, args, 0, argarray, 0 };
    struct pnode *n = parse_or(&P);
    skip_ws(&P);
    if (P.s[P.p]) P.err = 1;
    *ok = !P.err;
    return n;
}
int rt_predicate_eval(void *tree, id obj) { return eval_node((struct pnode *)tree, obj); }
