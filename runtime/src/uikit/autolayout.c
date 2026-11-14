/*
 * Auto Layout: NSLayoutConstraint (code, layout anchors and NIBs), the UIView
 * constraint API, and a Cassowary solver (incremental simplex with weighted
 * non-required constraints, after Badros, Borning & Stuckey) that turns the
 * active constraints of a window into frames.
 *
 * Each view taking part gets four variables in window coordinates: left, top,
 * width, height. Views that translate their autoresizing mask (the default for
 * views made in code) are pinned to their current frame relative to their
 * superview; views with translatesAutoresizingMaskIntoConstraints = NO are
 * placed by the constraints, by their intrinsic content size (hugging and
 * compression resistance) and, for whatever stays ambiguous, by very weak
 * "stay where you are" constraints. The window is solved from scratch whenever
 * constraints, the view tree, frames or content change.
 */
#include "uikit_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define V(x) ((struct uiview *)(x))

RT_DEFINE_CLASS(NSLayoutConstraint);
RT_DEFINE_CLASS(NSLayoutAnchor);
RT_DEFINE_CLASS(NSLayoutXAxisAnchor);
RT_DEFINE_CLASS(NSLayoutYAxisAnchor);
RT_DEFINE_CLASS(NSLayoutDimension);
RT_DEFINE_CLASS(_NSLayoutConstraintConstant);
RT_DECLARE_CLASS(UIView);
RT_DECLARE_CLASS(UIWindow);
RT_DECLARE_CLASS(UILabel);

/* ===================================================================== solver */
enum { S_INVALID, S_EXTERNAL, S_SLACK, S_ERROR, S_DUMMY };
typedef struct { int id, type; } Sym;
typedef struct { Sym s; double c; } Cell;
typedef struct { Cell *cells; int n, cap; double constant; } Row;
typedef struct { Sym basic; Row *row; } RowEntry;
struct solver {
    RowEntry *rows; int nrows, caprows;
    Row objective, *artificial;
    int next_id;
    Sym *vars; int nvars;        /* external variable index -> symbol */
};
#define EPS 1e-8
static int near_zero(double v) { return fabs(v) < EPS; }
static Sym sym_new(struct solver *sv, int type) { Sym s = { ++sv->next_id, type }; return s; }
static Row *row_new(double c) { Row *r = calloc(1, sizeof *r); r->constant = c; return r; }
static Row *row_copy(const Row *o) { Row *r = row_new(o->constant); r->n = r->cap = o->n; r->cells = malloc(sizeof(Cell) * (size_t)(o->n ? o->n : 1)); memcpy(r->cells, o->cells, sizeof(Cell) * (size_t)o->n); return r; }
static void row_free(Row *r) { if (r) { free(r->cells); free(r); } }
static int row_find(const Row *r, Sym s) { for (int i = 0; i < r->n; i++) if (r->cells[i].s.id == s.id) return i; return -1; }
static double row_coeff(const Row *r, Sym s) { int i = row_find(r, s); return i < 0 ? 0 : r->cells[i].c; }
static void row_erase(Row *r, int i) { r->cells[i] = r->cells[--r->n]; }
static void row_insert_sym(Row *r, Sym s, double c) {
    int i = row_find(r, s);
    if (i >= 0) { r->cells[i].c += c; if (near_zero(r->cells[i].c)) row_erase(r, i); return; }
    if (near_zero(c)) return;
    if (r->n == r->cap) { r->cap = r->cap ? r->cap * 2 : 8; r->cells = realloc(r->cells, sizeof(Cell) * (size_t)r->cap); }
    r->cells[r->n].s = s; r->cells[r->n++].c = c;
}
static void row_insert_row(Row *r, const Row *o, double c) {
    r->constant += o->constant * c;
    for (int i = 0; i < o->n; i++) row_insert_sym(r, o->cells[i].s, o->cells[i].c * c);
}
static void row_remove(Row *r, Sym s) { int i = row_find(r, s); if (i >= 0) row_erase(r, i); }
static void row_reverse(Row *r) { r->constant = -r->constant; for (int i = 0; i < r->n; i++) r->cells[i].c = -r->cells[i].c; }
static void row_solve_for(Row *r, Sym s) {
    int i = row_find(r, s); double k = -1.0 / r->cells[i].c; row_erase(r, i);
    r->constant *= k; for (int j = 0; j < r->n; j++) r->cells[j].c *= k;
}
static void row_solve_for2(Row *r, Sym lhs, Sym rhs) { row_insert_sym(r, lhs, -1.0); row_solve_for(r, rhs); }
static void row_substitute(Row *r, Sym s, const Row *o) {
    int i = row_find(r, s); if (i < 0) return;
    double c = r->cells[i].c; row_erase(r, i); row_insert_row(r, o, c);
}
static int rows_find(struct solver *sv, Sym s) { for (int i = 0; i < sv->nrows; i++) if (sv->rows[i].basic.id == s.id) return i; return -1; }
static void rows_put(struct solver *sv, Sym s, Row *r) {
    if (sv->nrows == sv->caprows) { sv->caprows = sv->caprows ? sv->caprows * 2 : 64; sv->rows = realloc(sv->rows, sizeof(RowEntry) * (size_t)sv->caprows); }
    sv->rows[sv->nrows].basic = s; sv->rows[sv->nrows++].row = r;
}
static Row *rows_take(struct solver *sv, int i) { Row *r = sv->rows[i].row; sv->rows[i] = sv->rows[--sv->nrows]; return r; }
static void substitute(struct solver *sv, Sym s, const Row *r) {
    for (int i = 0; i < sv->nrows; i++) row_substitute(sv->rows[i].row, s, r);
    row_substitute(&sv->objective, s, r);
    if (sv->artificial) row_substitute(sv->artificial, s, r);
}
static Sym entering_symbol(const Row *obj) {
    Sym none = { 0, S_INVALID };
    for (int i = 0; i < obj->n; i++) if (obj->cells[i].s.type != S_DUMMY && obj->cells[i].c < 0) return obj->cells[i].s;
    return none;
}
static int leaving_row(struct solver *sv, Sym entering) {
    double best = INFINITY; int at = -1;
    for (int i = 0; i < sv->nrows; i++) {
        if (sv->rows[i].basic.type == S_EXTERNAL) continue;
        double c = row_coeff(sv->rows[i].row, entering);
        if (c < 0) { double ratio = -sv->rows[i].row->constant / c; if (ratio < best) { best = ratio; at = i; } }
    }
    return at;
}
static int optimize(struct solver *sv, Row *obj) {
    for (int guard = 0; guard < 100000; guard++) {
        Sym e = entering_symbol(obj);
        if (e.type == S_INVALID) return 0;
        int li = leaving_row(sv, e);
        if (li < 0) return -1;   /* unbounded */
        Sym leaving = sv->rows[li].basic;
        Row *r = rows_take(sv, li);
        row_solve_for2(r, leaving, e);
        substitute(sv, e, r);
        rows_put(sv, e, r);
    }
    return -1;
}
static Sym any_pivotable(const Row *r) {
    Sym none = { 0, S_INVALID };
    for (int i = 0; i < r->n; i++) if (r->cells[i].s.type == S_SLACK || r->cells[i].s.type == S_ERROR) return r->cells[i].s;
    return none;
}
static int add_with_artificial(struct solver *sv, Row *row) {
    Sym art = sym_new(sv, S_SLACK);
    rows_put(sv, art, row_copy(row));
    sv->artificial = row_copy(row);
    optimize(sv, sv->artificial);
    int ok = near_zero(sv->artificial->constant);
    row_free(sv->artificial); sv->artificial = NULL;
    int ai = rows_find(sv, art);
    if (ai >= 0) {
        Row *r = rows_take(sv, ai);
        if (r->n == 0) { row_free(r); row_free(row); return ok; }
        Sym ent = any_pivotable(r);
        if (ent.type == S_INVALID) { row_free(r); row_free(row); return 0; }
        row_solve_for2(r, art, ent);
        substitute(sv, ent, r);
        rows_put(sv, ent, r);
    }
    for (int i = 0; i < sv->nrows; i++) row_remove(sv->rows[i].row, art);
    row_remove(&sv->objective, art);
    row_free(row);
    return ok;
}
/* expression: sum(coef[i] * var[idx[i]]) + constant  (op) 0 ; op -1 <=, 0 ==, 1 >= ; strength < 0: required */
#define MAXTERMS 8
struct expr { int n; int var[MAXTERMS]; double coef[MAXTERMS]; double constant; };
static Sym var_sym(struct solver *sv, int v) { if (sv->vars[v].type == S_INVALID) sv->vars[v] = sym_new(sv, S_EXTERNAL); return sv->vars[v]; }
static int solver_add(struct solver *sv, const struct expr *e, int op, double strength) {
    Row *row = row_new(e->constant);
    for (int i = 0; i < e->n; i++) {
        if (near_zero(e->coef[i])) continue;
        Sym s = var_sym(sv, e->var[i]);
        int ri = rows_find(sv, s);
        if (ri >= 0) row_insert_row(row, sv->rows[ri].row, e->coef[i]); else row_insert_sym(row, s, e->coef[i]);
    }
    int required = strength < 0;
    Sym marker = { 0, S_INVALID }, other = { 0, S_INVALID };
    if (op != 0) {
        double c = op < 0 ? 1.0 : -1.0;
        Sym slack = sym_new(sv, S_SLACK); marker = slack; row_insert_sym(row, slack, c);
        if (!required) { Sym err = sym_new(sv, S_ERROR); other = err; row_insert_sym(row, err, -c); row_insert_sym(&sv->objective, err, strength); }
    } else if (!required) {
        Sym ep = sym_new(sv, S_ERROR), em = sym_new(sv, S_ERROR); marker = ep; other = em;
        row_insert_sym(row, ep, -1.0); row_insert_sym(row, em, 1.0);
        row_insert_sym(&sv->objective, ep, strength); row_insert_sym(&sv->objective, em, strength);
    } else { Sym d = sym_new(sv, S_DUMMY); marker = d; row_insert_sym(row, d, 1.0); }
    if (row->constant < 0) row_reverse(row);
    /* choose the subject */
    Sym subject = { 0, S_INVALID };
    for (int i = 0; i < row->n; i++) if (row->cells[i].s.type == S_EXTERNAL) { subject = row->cells[i].s; break; }
    if (subject.type == S_INVALID && (marker.type == S_SLACK || marker.type == S_ERROR) && row_coeff(row, marker) < 0) subject = marker;
    if (subject.type == S_INVALID && (other.type == S_SLACK || other.type == S_ERROR) && row_coeff(row, other) < 0) subject = other;
    if (subject.type == S_INVALID) {
        int all_dummy = 1;
        for (int i = 0; i < row->n; i++) if (row->cells[i].s.type != S_DUMMY) { all_dummy = 0; break; }
        if (all_dummy) {
            if (!near_zero(row->constant)) { row_free(row); return -1; }   /* unsatisfiable */
            subject = marker;
        }
    }
    if (subject.type == S_INVALID) {
        if (!add_with_artificial(sv, row)) return -1;
    } else {
        row_solve_for(row, subject);
        substitute(sv, subject, row);
        rows_put(sv, subject, row);
    }
    optimize(sv, &sv->objective);
    return 0;
}
static double solver_value(struct solver *sv, int v) {
    if (sv->vars[v].type == S_INVALID) return 0;
    int ri = rows_find(sv, sv->vars[v]);
    return ri >= 0 ? sv->rows[ri].row->constant : 0;
}
static void solver_free(struct solver *sv) {
    for (int i = 0; i < sv->nrows; i++) row_free(sv->rows[i].row);
    free(sv->rows); free(sv->objective.cells); free(sv->vars);
}

/* =========================================================== constraints */
struct nslc {
    Class isa; double constant, multiplier; long active, priority; id first, second; long a1, a2, rel;
    id identifier; id owner;    /* the view it is installed on */
    long archived;
};
#define LC(x) ((struct nslc *)(x))
struct anchor { Class isa; id item; long attr; };
#define AN(x) ((struct anchor *)(x))

/* per-view Auto Layout state, kept beside the views (open-addressing hash keyed by pointer) */
struct alview { id view; long noTranslate; float hug[2], comp[2]; id constraints; long needsUpdate; };
static struct alview *g_al; static size_t g_capal, g_nal;
static pthread_mutex_t g_al_lock = PTHREAD_MUTEX_INITIALIZER;
#define TOMB ((id)(uintptr_t)1)
static size_t al_hash(id v) { uintptr_t x = (uintptr_t)v; x ^= x >> 17; x *= 0xed5ad4bbu; x ^= x >> 11; return (size_t)x; }
static void al_grow(void) {
    size_t oc = g_capal; struct alview *old = g_al;
    g_capal = oc ? oc * 2 : 1024; g_al = calloc(g_capal, sizeof *g_al); g_nal = 0;
    for (size_t i = 0; i < oc; i++) if (old[i].view && old[i].view != TOMB) {
        size_t j = al_hash(old[i].view) & (g_capal - 1);
        while (g_al[j].view) j = (j + 1) & (g_capal - 1);
        g_al[j] = old[i]; g_nal++;
    }
    free(old);
}
static struct alview *al_of(id v, int create) {
    if (g_capal) {
        for (size_t j = al_hash(v) & (g_capal - 1), k = 0; g_al[j].view && k < g_capal; j = (j + 1) & (g_capal - 1), k++)
            if (g_al[j].view == v) return &g_al[j];
    }
    if (!create) return NULL;
    if ((g_nal + 1) * 2 > g_capal) al_grow();
    size_t j = al_hash(v) & (g_capal - 1);
    while (g_al[j].view && g_al[j].view != TOMB) j = (j + 1) & (g_capal - 1);
    struct alview *a = &g_al[j];
    memset(a, 0, sizeof *a); a->view = v; g_nal++;
    int label = rt_is_kind_of(v, RT_CLASS(UILabel));
    a->hug[0] = a->hug[1] = label ? 251 : 250; a->comp[0] = a->comp[1] = 750;
    return a;
}
static unsigned long g_generation = 1, g_active_count;
void rt_al_invalidate(void) { g_generation++; }
static int g_applying;
static void uninstall(id c);
/* dealloc of a view: drop its state, the constraints installed on it, and the
 * constraints elsewhere that name it (they would point at a freed object) */
void rt_al_view_gone(id v) {
    if (!g_capal) return;
    pthread_mutex_lock(&g_al_lock);
    struct alview *a = al_of(v, 0);
    if (a) {
        id cs = a->constraints; a->constraints = NULL;
        for (size_t i = 0; cs && i < rt_array_count(cs); i++) { struct nslc *c = LC(rt_array_at(cs, i)); c->owner = NULL; c->active = 0; if (g_active_count) g_active_count--; }
        objc_release(cs);
        a->view = TOMB;
    }
    for (size_t j = 0; j < g_capal; j++) {
        struct alview *b = &g_al[j];
        if (!b->view || b->view == TOMB || !b->constraints) continue;
        for (size_t i = rt_array_count(b->constraints); i-- > 0; ) {
            id c = rt_array_at(b->constraints, i);
            if (LC(c)->first == v || LC(c)->second == v) uninstall(c);
        }
    }
    rt_al_invalidate();
    pthread_mutex_unlock(&g_al_lock);
}
static id lc_init_full(id o, id item, long a1, long rel, id item2, long a2, double mul, double k) {
    struct nslc *x = LC(o);
    x->first = item; x->second = item2; x->a1 = a1; x->a2 = a2; x->rel = rel; x->multiplier = mul; x->constant = k; x->priority = 1000;
    return o;
}
static id lc_make(Class c, SEL s, id item, long a1, long rel, id item2, long a2, double mul, double k) {
    (void)s; return objc_autorelease(lc_init_full(rt_alloc_instance(c, 0), item, a1, rel, item2, a2, mul, k));
}
/* the nearest common ancestor of the items (where UIKit installs an activated constraint) */
static id common_ancestor(id a, id b) {
    if (!b) return a;
    for (id x = a; x; x = V(x)->superview) for (id y = b; y; y = V(y)->superview) if (x == y) return x;
    return a;
}
static void install(id view, id c) {
    struct alview *a = al_of(view, 1);
    if (!a->constraints) a->constraints = RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableArray"), "alloc"), "init");
    for (size_t i = 0; i < rt_array_count(a->constraints); i++) if (rt_array_at(a->constraints, i) == c) return;
    RT_SEND1(void, a->constraints, "addObject:", id, c);
    LC(c)->owner = view; LC(c)->active = 1; g_active_count++;
    rt_al_invalidate();
}
static void uninstall(id c) {
    id view = LC(c)->owner;
    if (!view || !LC(c)->active) return;
    struct alview *a = al_of(view, 0);
    if (a && a->constraints) RT_SEND1(void, a->constraints, "removeObjectIdenticalTo:", id, c);
    LC(c)->owner = NULL; LC(c)->active = 0; if (g_active_count) g_active_count--;
    rt_al_invalidate();
}
static void lc_setActive(id self, SEL s, long on) {
    (void)s; on &= 0xff;
    if (on && !LC(self)->active) { id v = common_ancestor(LC(self)->first, LC(self)->second); if (v) { objc_retain(self); install(v, self); objc_release(self); } }
    else if (!on && LC(self)->active) { objc_retain(self); uninstall(self); objc_release(self); }
}
static rt_bool lc_isActive(id self, SEL s) { (void)s; return LC(self)->active ? 1 : 0; }
static void lc_activate(Class c, SEL s, id arr) { (void)c; (void)s; for (size_t i = 0; arr && i < rt_array_count(arr); i++) lc_setActive(rt_array_at(arr, i), NULL, 1); }
static void lc_deactivate(Class c, SEL s, id arr) { (void)c; (void)s; for (size_t i = 0; arr && i < rt_array_count(arr); i++) lc_setActive(rt_array_at(arr, i), NULL, 0); }
static double lc_constant(id self, SEL s) { (void)s; return LC(self)->constant; }
static void lc_setConstant(id self, SEL s, double v) { (void)s; if (LC(self)->constant != v) { LC(self)->constant = v; rt_al_invalidate(); } }
static float lc_priority(id self, SEL s) { (void)s; return (float)LC(self)->priority; }
static void lc_setPriority(id self, SEL s, float p) { (void)s; LC(self)->priority = (long)p; rt_al_invalidate(); }
static double lc_multiplier(id self, SEL s) { (void)s; return LC(self)->multiplier; }
static id lc_first(id self, SEL s) { (void)s; return LC(self)->first; }
static id lc_second(id self, SEL s) { (void)s; return LC(self)->second; }
static long lc_a1(id self, SEL s) { (void)s; return LC(self)->a1; }
static long lc_a2(id self, SEL s) { (void)s; return LC(self)->a2; }
static long lc_rel(id self, SEL s) { (void)s; return LC(self)->rel; }
static id lc_ident(id self, SEL s) { (void)s; return LC(self)->identifier; }
static void lc_setIdent(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(LC(self)->identifier); LC(self)->identifier = v; }
static rt_bool lc_archived(id self, SEL s) { (void)s; return LC(self)->archived ? 1 : 0; }
static void lc_setArchived(id self, SEL s, long v) { (void)s; LC(self)->archived = v & 0xff; }
static id lc_initWithCoder(id self, SEL s, id coder) {
    (void)s;
    #define DEC(k) RT_SEND1(id, coder, "decodeObjectForKey:", id, rt_str(k))
    #define HAS(k) (RT_SEND1(long, coder, "containsValueForKey:", id, rt_str(k)) & 0xff)
    #define DECI(k) RT_SEND1(long, coder, "decodeIntegerForKey:", id, rt_str(k))
    #define DECD(k) RT_SEND1(double, coder, "decodeDoubleForKey:", id, rt_str(k))
    double k = 0;
    if (HAS("NSConstant")) {
        id co = DEC("NSConstant");
        if (co && rt_is_kind_of(co, RT_CLASS(_NSLayoutConstraintConstant))) k = RT_SEND0(double, co, "doubleValue");
        else k = DECD("NSConstant");
    }
    lc_init_full(self, DEC("NSFirstItem"), HAS("NSFirstAttributeV2") ? DECI("NSFirstAttributeV2") : DECI("NSFirstAttribute"), DECI("NSRelation"),
                 DEC("NSSecondItem"), HAS("NSSecondAttributeV2") ? DECI("NSSecondAttributeV2") : DECI("NSSecondAttribute"),
                 HAS("NSMultiplier") ? DECD("NSMultiplier") : 1.0, k);
    if (HAS("NSPriority")) LC(self)->priority = DECI("NSPriority");
    if (HAS("NSLayoutIdentifier")) lc_setIdent(self, NULL, DEC("NSLayoutIdentifier"));
    LC(self)->archived = 1;
    return self;
}
static void lc_dealloc(id self, SEL s) { (void)s; objc_release(LC(self)->identifier); rt_dealloc_instance(self); }
static id lcc_initWithCoder(id self, SEL s, id coder) { (void)s; ((double *)self)[1] = RT_SEND1(double, coder, "decodeDoubleForKey:", id, rt_str("UINumericConstant")); return self; }
static double lcc_value(id self, SEL s) { (void)s; return ((double *)self)[1]; }
/* ------------------------------------------------- visual format language */
/* "H:|-(m)-[a(==100@750)]-8-[b(>=a)]-|": edges (|), connections (-pred-, -, nothing),
 * views with size predicates; standard spacing is 20 to the superview, 8 between views */
struct vfl { const char *p; id metrics, views, out; int vertical; char err[160]; };
static void vfl_ws(struct vfl *v) { while (*v->p == ' ') v->p++; }
static int vfl_name(struct vfl *v, char *buf, size_t n) {
    size_t k = 0;
    while ((*v->p == '_' || (*v->p >= 'a' && *v->p <= 'z') || (*v->p >= 'A' && *v->p <= 'Z') || (*v->p >= '0' && *v->p <= '9')) && k + 1 < n) buf[k++] = *v->p++;
    buf[k] = 0; return (int)k;
}
static int vfl_number_or_metric(struct vfl *v, double *out) {
    vfl_ws(v);
    const char *s = v->p;
    if (*s == '-' || *s == '.' || (*s >= '0' && *s <= '9')) { char *e; *out = strtod(s, &e); if (e == s) return 0; v->p = e; return 1; }
    char nm[128];
    if (!vfl_name(v, nm, sizeof nm)) return 0;
    id m = v->metrics ? rt_dict_get(v->metrics, rt_str(nm)) : NULL;
    if (!m) { snprintf(v->err, sizeof v->err, "unknown metric %s", nm); return 0; }
    *out = RT_SEND0(double, m, "doubleValue");
    return 1;
}
/* one predicate: (relation)? (constant|metric|viewName) (@priority)? */
struct pred { long rel; double k; id view; double prio; };
static int vfl_pred(struct vfl *v, struct pred *pr) {
    vfl_ws(v); pr->rel = 0; pr->k = 0; pr->view = NULL; pr->prio = 1000;
    if (!strncmp(v->p, "==", 2)) { v->p += 2; } else if (!strncmp(v->p, ">=", 2)) { pr->rel = 1; v->p += 2; } else if (!strncmp(v->p, "<=", 2)) { pr->rel = -1; v->p += 2; }
    vfl_ws(v);
    const char *save = v->p; char nm[128];
    if (vfl_name(v, nm, sizeof nm) && !(nm[0] >= '0' && nm[0] <= '9')) {
        id vw = v->views ? rt_dict_get(v->views, rt_str(nm)) : NULL;
        if (vw) pr->view = vw;
        else { v->p = save; if (!vfl_number_or_metric(v, &pr->k)) return 0; }
    } else { v->p = save; if (!vfl_number_or_metric(v, &pr->k)) return 0; }
    vfl_ws(v);
    if (*v->p == '@') { v->p++; if (!vfl_number_or_metric(v, &pr->prio)) return 0; }
    return 1;
}
/* predicate list: a single number/metric, or (pred, pred, ...) */
static int vfl_preds(struct vfl *v, struct pred *ps, int max) {
    vfl_ws(v);
    if (*v->p != '(') { if (!vfl_number_or_metric(v, &ps[0].k)) return -1; ps[0].rel = 0; ps[0].view = NULL; ps[0].prio = 1000; return 1; }
    v->p++; int n = 0;
    for (;;) {
        if (n == max || !vfl_pred(v, &ps[n])) return -1;
        n++; vfl_ws(v);
        if (*v->p == ',') { v->p++; continue; }
        if (*v->p == ')') { v->p++; return n; }
        return -1;
    }
}
static void vfl_add(struct vfl *v, id a, long aa, long rel, id b, long ba, double k, double prio) {
    id c = lc_make(RT_CLASS(NSLayoutConstraint), NULL, a, aa, rel, b, ba, 1, k);
    LC(c)->priority = (long)prio;
    RT_SEND1(void, v->out, "addObject:", id, c);
}
static id lc_visual(Class c, SEL s, id fmt, long opts, id metrics, id views) {
    (void)c; (void)s;
    struct vfl v = { fmt ? rt_str_utf8(fmt) : "", metrics, views, RT_SEND0(id, (id)rt_class_named("NSMutableArray"), "array"), 0, "" };
    if (!strncmp(v.p, "V:", 2)) { v.vertical = 1; v.p += 2; } else if (!strncmp(v.p, "H:", 2)) v.p += 2;
    long lead = v.vertical ? 3 : 5, trail = v.vertical ? 4 : 6, size = v.vertical ? 8 : 7;
    id prev = NULL, first_view = NULL; int prev_is_super = 0;
    struct pred conn[4]; int nconn = -1;   /* -1: no pending connection; 0: adjacency */
    id views_in_order[64]; int nv = 0;
    for (;;) {
        vfl_ws(&v);
        if (!*v.p) break;
        if (*v.p == '|') {
            v.p++;
            if (prev) {   /* trailing edge: superview.trailing - view.trailing (rel) spacing */
                id sup = V(prev)->superview;
                if (nconn < 0) nconn = 0;
                if (nconn == 0) vfl_add(&v, sup, trail, 0, prev, trail, 0, 1000);
                for (int i = 0; i < nconn; i++) vfl_add(&v, sup, trail, conn[i].rel, prev, trail, conn[i].k, conn[i].prio);
            } else prev_is_super = 1;
            nconn = -1;
            continue;
        }
        if (*v.p == '-') {   /* connection */
            v.p++; vfl_ws(&v);
            if (*v.p == '[' || *v.p == '|') { conn[0].rel = 0; conn[0].k = prev_is_super || *v.p == '|' ? 20 : 8; conn[0].view = NULL; conn[0].prio = 1000; nconn = 1; continue; }
            int n = vfl_preds(&v, conn, 4);
            if (n < 0) { snprintf(v.err, sizeof v.err, "bad connection near '%.20s'", v.p); break; }
            vfl_ws(&v);
            if (*v.p != '-') { snprintf(v.err, sizeof v.err, "expected '-' near '%.20s'", v.p); break; }
            v.p++; nconn = n;
            continue;
        }
        if (*v.p == '[') {
            v.p++; vfl_ws(&v);
            char nm[128];
            if (!vfl_name(&v, nm, sizeof nm)) { snprintf(v.err, sizeof v.err, "expected a view name"); break; }
            id view = views ? rt_dict_get(views, rt_str(nm)) : NULL;
            if (!view) { snprintf(v.err, sizeof v.err, "unknown view %s", nm); break; }
            vfl_ws(&v);
            if (*v.p == '(') {   /* size predicates */
                struct pred ps[4]; int n = vfl_preds(&v, ps, 4);
                if (n < 0) { snprintf(v.err, sizeof v.err, "bad size predicate for %s", nm); break; }
                for (int i = 0; i < n; i++) vfl_add(&v, view, size, ps[i].rel, ps[i].view, ps[i].view ? size : 0, ps[i].k, ps[i].prio);
            }
            vfl_ws(&v);
            if (*v.p != ']') { snprintf(v.err, sizeof v.err, "expected ']' after %s", nm); break; }
            v.p++;
            if (prev_is_super && !prev) {   /* leading edge to the superview */
                id sup = V(view)->superview;
                if (nconn < 0) nconn = 0;
                if (nconn == 0) vfl_add(&v, view, lead, 0, sup, lead, 0, 1000);
                for (int i = 0; i < nconn; i++) vfl_add(&v, view, lead, conn[i].rel, sup, lead, conn[i].k, conn[i].prio);
            } else if (prev) {
                if (nconn < 0) nconn = 0;
                if (nconn == 0) vfl_add(&v, view, lead, 0, prev, trail, 0, 1000);
                for (int i = 0; i < nconn; i++) vfl_add(&v, view, lead, conn[i].rel, prev, trail, conn[i].k, conn[i].prio);
            }
            nconn = -1; prev = view; prev_is_super = 0;
            if (!first_view) first_view = view;
            if (nv < 64) views_in_order[nv++] = view;
            continue;
        }
        snprintf(v.err, sizeof v.err, "unexpected '%c'", *v.p);
        break;
    }
    if (v.err[0]) { rt_warn("visual format \"%s\": %s", fmt ? rt_str_utf8(fmt) : "", v.err); return v.out; }
    /* alignment options: NSLayoutFormatAlignAll<attribute> = 1 << attribute */
    for (long a = 1; a <= 12; a++)
        if (opts & (1L << a)) for (int i = 1; i < nv; i++) vfl_add(&v, views_in_order[i], a, 0, views_in_order[0], a, 0, 1000);
    return v.out;
}

/* ------------------------------------------------------------- anchors */
static id anchor_make(Class c, id item, long attr) { id o = rt_alloc_instance(c, 0); AN(o)->item = item; AN(o)->attr = attr; return objc_autorelease(o); }
static id an_rel(id self, id other, long rel, double mul, double k) {
    return lc_make(RT_CLASS(NSLayoutConstraint), NULL, AN(self)->item, AN(self)->attr, rel, other ? AN(other)->item : NULL, other ? AN(other)->attr : 0, mul, k);
}
static id an_eq(id self, SEL s, id o) { (void)s; return an_rel(self, o, 0, 1, 0); }
static id an_ge(id self, SEL s, id o) { (void)s; return an_rel(self, o, 1, 1, 0); }
static id an_le(id self, SEL s, id o) { (void)s; return an_rel(self, o, -1, 1, 0); }
static id an_eqK(id self, SEL s, id o, double k) { (void)s; return an_rel(self, o, 0, 1, k); }
static id an_geK(id self, SEL s, id o, double k) { (void)s; return an_rel(self, o, 1, 1, k); }
static id an_leK(id self, SEL s, id o, double k) { (void)s; return an_rel(self, o, -1, 1, k); }
static id dim_eqC(id self, SEL s, double k) { (void)s; return an_rel(self, NULL, 0, 1, k); }
static id dim_geC(id self, SEL s, double k) { (void)s; return an_rel(self, NULL, 1, 1, k); }
static id dim_leC(id self, SEL s, double k) { (void)s; return an_rel(self, NULL, -1, 1, k); }
static id dim_eqM(id self, SEL s, id o, double m) { (void)s; return an_rel(self, o, 0, m, 0); }
static id dim_geM(id self, SEL s, id o, double m) { (void)s; return an_rel(self, o, 1, m, 0); }
static id dim_leM(id self, SEL s, id o, double m) { (void)s; return an_rel(self, o, -1, m, 0); }
static id dim_eqMK(id self, SEL s, id o, double m, double k) { (void)s; return an_rel(self, o, 0, m, k); }
static id dim_geMK(id self, SEL s, id o, double m, double k) { (void)s; return an_rel(self, o, 1, m, k); }
static id dim_leMK(id self, SEL s, id o, double m, double k) { (void)s; return an_rel(self, o, -1, m, k); }
#define ANCHOR(name, cls, attr) static id v_##name(id self, SEL s) { (void)s; return anchor_make(RT_CLASS(cls), self, attr); }
ANCHOR(leftAnchor, NSLayoutXAxisAnchor, 1) ANCHOR(rightAnchor, NSLayoutXAxisAnchor, 2) ANCHOR(topAnchor, NSLayoutYAxisAnchor, 3)
ANCHOR(bottomAnchor, NSLayoutYAxisAnchor, 4) ANCHOR(leadingAnchor, NSLayoutXAxisAnchor, 5) ANCHOR(trailingAnchor, NSLayoutXAxisAnchor, 6)
ANCHOR(widthAnchor, NSLayoutDimension, 7) ANCHOR(heightAnchor, NSLayoutDimension, 8) ANCHOR(centerXAnchor, NSLayoutXAxisAnchor, 9)
ANCHOR(centerYAnchor, NSLayoutYAxisAnchor, 10) ANCHOR(lastBaselineAnchor, NSLayoutYAxisAnchor, 11) ANCHOR(firstBaselineAnchor, NSLayoutYAxisAnchor, 12)

/* ---------------------------------------------------------- UIView API */
static void v_addConstraint(id self, SEL s, id c) { (void)s; if (c) { objc_retain(c); if (LC(c)->active) uninstall(c); install(self, c); objc_release(c); } }
static void v_addConstraints(id self, SEL s, id arr) { for (size_t i = 0; arr && i < rt_array_count(arr); i++) v_addConstraint(self, s, rt_array_at(arr, i)); }
static void v_removeConstraint(id self, SEL s, id c) { (void)self; (void)s; if (c) { objc_retain(c); uninstall(c); objc_release(c); } }
static void v_removeConstraints(id self, SEL s, id arr) {
    if (!arr) return;
    id copy = RT_SEND0(id, arr, "copy");
    for (size_t i = 0; i < rt_array_count(copy); i++) v_removeConstraint(self, s, rt_array_at(copy, i));
    objc_release(copy);
}
static id v_constraints(id self, SEL s) {
    (void)s; struct alview *a = al_of(self, 0);
    return a && a->constraints ? objc_autorelease(RT_SEND0(id, a->constraints, "copy")) : RT_SEND0(id, (id)rt_class_named("NSArray"), "array");
}
static rt_bool v_translates(id self, SEL s) { (void)s; struct alview *a = al_of(self, 0); return a && a->noTranslate ? 0 : 1; }
static void v_setTranslates(id self, SEL s, long t) { (void)s; al_of(self, 1)->noTranslate = !(t & 0xff); rt_al_invalidate(); }
static float v_hug(id self, SEL s, long axis) { (void)s; struct alview *a = al_of(self, 1); return a->hug[axis & 1]; }
static void v_setHug(id self, SEL s, float p, long axis) { (void)s; al_of(self, 1)->hug[axis & 1] = p; rt_al_invalidate(); }
static float v_comp(id self, SEL s, long axis) { (void)s; struct alview *a = al_of(self, 1); return a->comp[axis & 1]; }
static void v_setComp(id self, SEL s, float p, long axis) { (void)s; al_of(self, 1)->comp[axis & 1] = p; rt_al_invalidate(); }
static void v_invalidateIntrinsic(id self, SEL s) { (void)self; (void)s; rt_al_invalidate(); }
static void v_setNeedsUpdateConstraints(id self, SEL s) { (void)s; al_of(self, 1)->needsUpdate = 1; rt_al_invalidate(); }
static rt_bool v_needsUpdateConstraints(id self, SEL s) { (void)s; struct alview *a = al_of(self, 0); return a && a->needsUpdate; }
static void v_updateConstraints(id self, SEL s) { (void)self; (void)s; }
static void v_updateConstraintsIfNeeded(id self, SEL s) {
    (void)s; struct alview *a = al_of(self, 0);
    if (a && a->needsUpdate) { a->needsUpdate = 0; RT_SEND0(void, self, "updateConstraints"); }
}
static CGSize v_systemLayoutSize(id self, SEL s, CGSize target) {
    (void)s; (void)target;
    CGSize in = RT_SEND0(CGSize, self, "intrinsicContentSize");
    CGSize cur = rt_view_bounds(self).size;
    return (CGSize){ in.width >= 0 ? in.width : cur.width, in.height >= 0 ? in.height : cur.height };
}
static CGSize v_systemLayoutSize2(id self, SEL s, CGSize target, float hp, float vp) { (void)hp; (void)vp; return v_systemLayoutSize(self, s, target); }
static rt_bool v_hasAmbiguous(id self, SEL s) { (void)self; (void)s; return 0; }

/* initWithCoder: additions for UIView (called from the NIB decoder) */
void rt_al_decode_view(id view, id coder) {
    if (RT_SEND1(long, coder, "decodeBoolForKey:", id, rt_str("UIViewDoesNotTranslateAutoresizingMaskIntoConstraints")) & 0xff)
        al_of(view, 1)->noTranslate = 1;
    const char *keys[2] = { "UIViewContentHuggingPriority", "UIViewContentCompressionResistancePriority" };
    for (int k = 0; k < 2; k++) {
        if (!(RT_SEND1(long, coder, "containsValueForKey:", id, rt_str(keys[k])) & 0xff)) continue;
        id str = RT_SEND1(id, coder, "decodeObjectForKey:", id, rt_str(keys[k]));
        if (!str || !rt_is_kind_of(str, rt_class_named("NSString"))) continue;
        float h = 0, v = 0;
        if (sscanf(rt_str_utf8(str), "{%f, %f}", &h, &v) == 2) {
            struct alview *a = al_of(view, 1);
            if (k == 0) { a->hug[0] = h; a->hug[1] = v; } else { a->comp[0] = h; a->comp[1] = v; }
        }
    }
    id cs = RT_SEND1(id, coder, "decodeObjectForKey:", id, rt_str("UIViewAutolayoutConstraints"));
    for (size_t i = 0; cs && i < rt_array_count(cs); i++) {
        id c = rt_array_at(cs, i);
        if (c && rt_is_kind_of(c, RT_CLASS(NSLayoutConstraint))) install(view, c);
    }
}

/* ============================================================ solving */
struct pv { id view; int var; struct alview *al; };    /* var: index of left; top +1, width +2, height +3 */
struct ctx { struct pv *v; int n, cap; };
static int pv_index(struct ctx *x, id view) { for (int i = 0; i < x->n; i++) if (x->v[i].view == view) return i; return -1; }
static int pv_add(struct ctx *x, id view) {
    int i = pv_index(x, view);
    if (i >= 0) return i;
    if (V(view)->superview && !rt_is_kind_of(view, RT_CLASS(UIWindow))) pv_add(x, V(view)->superview);   /* ancestors first */
    if (x->n == x->cap) { x->cap = x->cap ? x->cap * 2 : 64; x->v = realloc(x->v, sizeof *x->v * (size_t)x->cap); }
    x->v[x->n].view = view; x->v[x->n].var = 4 * x->n; x->v[x->n].al = al_of(view, 0);
    return x->n++;
}
static id window_of(id v) { while (v && !rt_is_kind_of(v, RT_CLASS(UIWindow))) v = V(v)->superview; return v; }
static double font_metric(id view, int ascent) {
    id f = rt_responds(view, rt_sel("font")) ? RT_SEND0(id, view, "font") : NULL;
    if (!f) return 0;
    return ascent ? RT_SEND0(double, f, "ascender") : -RT_SEND0(double, f, "descender");
}
/* attribute as a linear expression of the view's variables; returns 0 if unsupported */
static int attr_expr(struct ctx *x, id item, long attr, double scale, struct expr *e) {
    int i = pv_index(x, item); if (i < 0) return 0;
    int L = x->v[i].var, T = L + 1, W = L + 2, H = L + 3;
    UIEdgeInsets m = V(item)->layoutMargins;
    if (!m.top && !m.left && !m.bottom && !m.right) m = (UIEdgeInsets){ 8, 8, 8, 8 };
    #define T1(v, c) do { e->var[e->n] = (v); e->coef[e->n++] = (c) * scale; } while (0)
    switch (attr) {
    case 1: case 5: T1(L, 1); break;                                   /* left, leading */
    case 2: case 6: T1(L, 1); T1(W, 1); break;                         /* right, trailing */
    case 3: T1(T, 1); break;                                           /* top */
    case 4: T1(T, 1); T1(H, 1); break;                                 /* bottom */
    case 7: T1(W, 1); break;                                           /* width */
    case 8: T1(H, 1); break;                                           /* height */
    case 9: T1(L, 1); T1(W, 0.5); break;                               /* centerX */
    case 10: T1(T, 1); T1(H, 0.5); break;                              /* centerY */
    case 11: T1(T, 1); T1(H, 1); e->constant -= font_metric(item, 0) * scale; break;   /* last baseline */
    case 12: T1(T, 1); e->constant += font_metric(item, 1) * scale; break;             /* first baseline */
    case 13: case 17: T1(L, 1); e->constant += m.left * scale; break;
    case 14: case 18: T1(L, 1); T1(W, 1); e->constant -= m.right * scale; break;
    case 15: T1(T, 1); e->constant += m.top * scale; break;
    case 16: T1(T, 1); T1(H, 1); e->constant -= m.bottom * scale; break;
    case 19: T1(L, 1); T1(W, 0.5); e->constant += (m.left - m.right) / 2 * scale; break;
    case 20: T1(T, 1); T1(H, 0.5); e->constant += (m.top - m.bottom) / 2 * scale; break;
    default: return 0;
    }
    return 1;
}
static double strength_of(double prio) { return prio >= 1000 ? -1 : pow(10.0, prio / 125.0); }
static void collect(struct ctx *x, id view, id win, id *list, size_t *n, size_t cap) {
    struct alview *a = al_of(view, 0);
    for (size_t i = 0; a && a->constraints && i < rt_array_count(a->constraints); i++) {
        id c = rt_array_at(a->constraints, i);
        id f = LC(c)->first, s2 = LC(c)->second;
        if (!f || window_of(f) != win || (s2 && window_of(s2) != win)) continue;
        pv_add(x, f); if (s2) pv_add(x, s2);
        if (*n < cap) list[(*n)++] = c;
    }
    id subs = V(view)->subviews;
    for (size_t i = 0; subs && i < rt_array_count(subs); i++) collect(x, rt_array_at(subs, i), win, list, n, cap);
}
static void call_update_constraints(id view) {
    struct alview *a = al_of(view, 0);
    id subs = V(view)->subviews;
    for (size_t i = 0; subs && i < rt_array_count(subs); i++) call_update_constraints(rt_array_at(subs, i));
    if (a && a->needsUpdate) { a->needsUpdate = 0; RT_SEND0(void, view, "updateConstraints"); }
}
static unsigned long g_solved_generation;
static id g_solved_window;
void rt_al_layout_window(id win) {
    if (!g_active_count || g_applying) return;
    if (g_solved_generation == g_generation && g_solved_window == win) return;
    pthread_mutex_lock(&g_al_lock);
    call_update_constraints(win);
    struct ctx x = { 0 };
    pv_add(&x, win);
    size_t cap = 8192, n = 0;
    id *list = malloc(sizeof(id) * cap);
    collect(&x, win, win, list, &n, cap);
    if (!n) { free(list); free(x.v); g_solved_generation = g_generation; g_solved_window = win; pthread_mutex_unlock(&g_al_lock); return; }
    struct solver sv; memset(&sv, 0, sizeof sv);
    sv.nvars = 4 * x.n; sv.vars = calloc((size_t)sv.nvars, sizeof(Sym));
    /* geometry of views placed by frames, relative to their superview */
    for (int i = 0; i < x.n; i++) {
        id v = x.v[i].view; int L = x.v[i].var;
        struct alview *a = x.v[i].al;
        int sup = V(v)->superview ? pv_index(&x, V(v)->superview) : -1;
        CGRect f = rt_is_kind_of(v, RT_CLASS(UIWindow)) ? rt_view_bounds(v) : rt_view_frame(v);
        int free_view = a && a->noTranslate && sup >= 0;
        struct expr e;
        for (int k = 0; k < 4; k++) {
            double cur = k == 0 ? f.origin.x : k == 1 ? f.origin.y : k == 2 ? f.size.width : f.size.height;
            memset(&e, 0, sizeof e);
            e.var[0] = L + k; e.coef[0] = 1; e.n = 1; e.constant = -cur;
            if (k < 2 && sup >= 0) {   /* left - superview.left = frame.x - superview.bounds.x */
                CGRect sb = rt_view_bounds(V(v)->superview);
                e.constant += k == 0 ? sb.origin.x : sb.origin.y;
                e.var[1] = x.v[sup].var + k; e.coef[1] = -1; e.n = 2;
            }
            solver_add(&sv, &e, 0, free_view ? 1e-3 : -1);   /* free views: only a very weak stay */
        }
        if (free_view) {
            CGSize in = RT_SEND0(CGSize, v, "intrinsicContentSize");
            for (int k = 0; k < 2; k++) {
                double iv = k ? in.height : in.width;
                if (iv < 0) continue;
                memset(&e, 0, sizeof e); e.var[0] = L + 2 + k; e.coef[0] = 1; e.n = 1; e.constant = -iv;
                solver_add(&sv, &e, -1, strength_of(a->hug[k]));   /* hugging: size <= intrinsic */
                solver_add(&sv, &e, 1, strength_of(a->comp[k]));   /* compression resistance: size >= intrinsic */
            }
        }
        memset(&e, 0, sizeof e); e.var[0] = L + 2; e.coef[0] = 1; e.n = 1; solver_add(&sv, &e, 1, -1);   /* w >= 0 */
        memset(&e, 0, sizeof e); e.var[0] = L + 3; e.coef[0] = 1; e.n = 1; solver_add(&sv, &e, 1, -1);   /* h >= 0 */
    }
    int broken = 0;
    for (size_t i = 0; i < n; i++) {
        struct nslc *c = LC(list[i]);
        struct expr e; memset(&e, 0, sizeof e);
        if (!attr_expr(&x, c->first, c->a1, 1, &e)) continue;
        if (c->second && c->a2) { if (!attr_expr(&x, c->second, c->a2, -c->multiplier, &e)) continue; }
        e.constant -= c->constant;
        if (solver_add(&sv, &e, (int)(c->rel < 0 ? -1 : c->rel > 0 ? 1 : 0), strength_of((double)c->priority))) broken++;
    }
    if (broken && rt_first_time("autolayout-unsatisfiable")) rt_log("auto layout: %d constraint(s) could not be satisfied and were broken", broken);
    /* apply frames, parents first (the list is ordered ancestors first) */
    g_applying = 1;
    for (int i = 0; i < x.n; i++) {
        id v = x.v[i].view; struct alview *a = x.v[i].al;
        int sup = V(v)->superview ? pv_index(&x, V(v)->superview) : -1;
        if (!a || !a->noTranslate || sup < 0) continue;
        int L = x.v[i].var, SL = x.v[sup].var;
        CGRect f = { { solver_value(&sv, L) - solver_value(&sv, SL), solver_value(&sv, L + 1) - solver_value(&sv, SL + 1) },
                     { fmax(0, solver_value(&sv, L + 2)), fmax(0, solver_value(&sv, L + 3)) } };
        /* the solver works in the superview's frame space; frames are in its bounds space */
        CGRect sb = rt_view_bounds(V(v)->superview);
        f.origin.x += sb.origin.x; f.origin.y += sb.origin.y;
        CGRect old = rt_view_frame(v);
        if (fabs(old.origin.x - f.origin.x) > 0.01 || fabs(old.origin.y - f.origin.y) > 0.01 || fabs(old.size.width - f.size.width) > 0.01 || fabs(old.size.height - f.size.height) > 0.01) {
            rt_view_set_frame(v, f);
            RT_SEND0(void, v, "setNeedsLayout");
        }
    }
    g_applying = 0;
    solver_free(&sv);
    free(list); free(x.v);
    g_solved_generation = g_generation; g_solved_window = win;
    pthread_mutex_unlock(&g_al_lock);
}
int rt_al_applying(void) { return g_applying; }

void rt_autolayout_init(void) {
    Class o = rt_class_named("NSObject");
    NEWCLASS(NSLayoutConstraint, o, sizeof(struct nslc) - sizeof(void *));
    Class lc = RT_CLASS(NSLayoutConstraint);
    CM(lc, "constraintWithItem:attribute:relatedBy:toItem:attribute:multiplier:constant:", lc_make);
    CM(lc, "constraintsWithVisualFormat:options:metrics:views:", lc_visual);
    CM(lc, "activateConstraints:", lc_activate); CM(lc, "deactivateConstraints:", lc_deactivate);
    IM(lc, "constant", lc_constant); IM(lc, "setConstant:", lc_setConstant); IM(lc, "priority", lc_priority); IM(lc, "setPriority:", lc_setPriority);
    IM(lc, "multiplier", lc_multiplier); IM(lc, "firstItem", lc_first); IM(lc, "secondItem", lc_second);
    IM(lc, "firstAttribute", lc_a1); IM(lc, "secondAttribute", lc_a2); IM(lc, "relation", lc_rel);
    IM(lc, "isActive", lc_isActive); IM(lc, "setActive:", lc_setActive); IM(lc, "active", lc_isActive);
    IM(lc, "identifier", lc_ident); IM(lc, "setIdentifier:", lc_setIdent);
    IM(lc, "shouldBeArchived", lc_archived); IM(lc, "setShouldBeArchived:", lc_setArchived);
    IM(lc, "initWithCoder:", lc_initWithCoder); IM(lc, "dealloc", lc_dealloc);
    NEWCLASS(_NSLayoutConstraintConstant, o, sizeof(double));
    IM(RT_CLASS(_NSLayoutConstraintConstant), "initWithCoder:", lcc_initWithCoder); IM(RT_CLASS(_NSLayoutConstraintConstant), "doubleValue", lcc_value);

    NEWCLASS(NSLayoutAnchor, o, sizeof(struct anchor) - sizeof(void *));
    Class an = RT_CLASS(NSLayoutAnchor);
    IM(an, "constraintEqualToAnchor:", an_eq); IM(an, "constraintGreaterThanOrEqualToAnchor:", an_ge); IM(an, "constraintLessThanOrEqualToAnchor:", an_le);
    IM(an, "constraintEqualToAnchor:constant:", an_eqK); IM(an, "constraintGreaterThanOrEqualToAnchor:constant:", an_geK);
    IM(an, "constraintLessThanOrEqualToAnchor:constant:", an_leK);
    NEWCLASS(NSLayoutXAxisAnchor, an, 0); NEWCLASS(NSLayoutYAxisAnchor, an, 0); NEWCLASS(NSLayoutDimension, an, 0);
    Class dm = RT_CLASS(NSLayoutDimension);
    IM(dm, "constraintEqualToConstant:", dim_eqC); IM(dm, "constraintGreaterThanOrEqualToConstant:", dim_geC); IM(dm, "constraintLessThanOrEqualToConstant:", dim_leC);
    IM(dm, "constraintEqualToAnchor:multiplier:", dim_eqM); IM(dm, "constraintGreaterThanOrEqualToAnchor:multiplier:", dim_geM);
    IM(dm, "constraintLessThanOrEqualToAnchor:multiplier:", dim_leM);
    IM(dm, "constraintEqualToAnchor:multiplier:constant:", dim_eqMK); IM(dm, "constraintGreaterThanOrEqualToAnchor:multiplier:constant:", dim_geMK);
    IM(dm, "constraintLessThanOrEqualToAnchor:multiplier:constant:", dim_leMK);

    Class v = RT_CLASS(UIView);
    IM(v, "leftAnchor", v_leftAnchor); IM(v, "rightAnchor", v_rightAnchor); IM(v, "topAnchor", v_topAnchor); IM(v, "bottomAnchor", v_bottomAnchor);
    IM(v, "leadingAnchor", v_leadingAnchor); IM(v, "trailingAnchor", v_trailingAnchor); IM(v, "widthAnchor", v_widthAnchor); IM(v, "heightAnchor", v_heightAnchor);
    IM(v, "centerXAnchor", v_centerXAnchor); IM(v, "centerYAnchor", v_centerYAnchor);
    IM(v, "lastBaselineAnchor", v_lastBaselineAnchor); IM(v, "firstBaselineAnchor", v_firstBaselineAnchor);
    IM(v, "addConstraint:", v_addConstraint); IM(v, "addConstraints:", v_addConstraints);
    IM(v, "removeConstraint:", v_removeConstraint); IM(v, "removeConstraints:", v_removeConstraints); IM(v, "constraints", v_constraints);
    IM(v, "translatesAutoresizingMaskIntoConstraints", v_translates); IM(v, "setTranslatesAutoresizingMaskIntoConstraints:", v_setTranslates);
    IM(v, "contentHuggingPriorityForAxis:", v_hug); IM(v, "setContentHuggingPriority:forAxis:", v_setHug);
    IM(v, "contentCompressionResistancePriorityForAxis:", v_comp); IM(v, "setContentCompressionResistancePriority:forAxis:", v_setComp);
    IM(v, "invalidateIntrinsicContentSize", v_invalidateIntrinsic);
    IM(v, "setNeedsUpdateConstraints", v_setNeedsUpdateConstraints); IM(v, "needsUpdateConstraints", v_needsUpdateConstraints);
    IM(v, "updateConstraints", v_updateConstraints); IM(v, "updateConstraintsIfNeeded", v_updateConstraintsIfNeeded);
    IM(v, "systemLayoutSizeFittingSize:", v_systemLayoutSize);
    IM(v, "systemLayoutSizeFittingSize:withHorizontalFittingPriority:verticalFittingPriority:", v_systemLayoutSize2);
    IM(v, "hasAmbiguousLayout", v_hasAmbiguous);
}
