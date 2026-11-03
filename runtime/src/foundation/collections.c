/*
 * Foundation HLE, part 2: NSArray / NSMutableArray, NSDictionary /
 * NSMutableDictionary, NSSet / NSMutableSet (incl. fast enumeration,
 * block enumeration, sorting, literals).
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")

RT_DECLARE_CLASS(NSObject);
RT_DEFINE_CLASS(NSArray);
RT_DEFINE_CLASS(NSMutableArray);
RT_DEFINE_CLASS(NSDictionary);
RT_DEFINE_CLASS(NSMutableDictionary);
RT_DEFINE_CLASS(NSSet);
RT_DEFINE_CLASS(NSMutableSet);
RT_DEFINE_CLASS(NSEnumerator);

struct nsarr { Class isa; id *items; size_t n, cap; unsigned long mutations; long mut; };
struct nsdict { Class isa; id *keys; id *vals; size_t n, cap; unsigned long mutations; long mut; };
struct fast_state { unsigned long state; id *itemsPtr; unsigned long *mutationsPtr; unsigned long extra[5]; };

static void arr_reserve(struct nsarr *a, size_t n) {
    if (n <= a->cap) return;
    a->cap = n < 8 ? 8 : n * 2;
    a->items = realloc(a->items, a->cap * sizeof(id));
}
static void arr_push(struct nsarr *a, id o) { arr_reserve(a, a->n + 1); a->items[a->n++] = objc_retain(o); a->mutations++; }
static int arr_is_ours(id a) {
    for (Class c = a->isa; c; c = c->superclass) if (c == RT_CLASS(NSArray) || c == RT_CLASS(NSSet)) return 1;
    return 0;
}
size_t rt_array_count(id a) { return a ? ((struct nsarr *)a)->n : 0; }
id rt_array_at(id a, size_t i) { return a && i < ((struct nsarr *)a)->n ? ((struct nsarr *)a)->items[i] : NULL; }
void rt_soft_error(const char *msg);
static void range_error(const char *m, unsigned long i, size_t n) {
    char b[256];
    if (n) snprintf(b, sizeof b, "*** -[%s]: index %lu beyond bounds [0 .. %zu]", m, i, n - 1);
    else snprintf(b, sizeof b, "*** -[%s]: index %lu beyond bounds for empty array", m, i);
    rt_soft_error(b);
}

/* ------------------------------------------------------------ NSArray */
static id a_alloc_with(Class c, id *objs, size_t n) {
    id r = rt_alloc_instance(c, 0);
    struct nsarr *a = (struct nsarr *)r;
    arr_reserve(a, n);
    for (size_t i = 0; i < n; i++) {
        if (!objs[i]) { rt_soft_error("*** -[__NSPlaceholderArray initWithObjects:count:]: attempt to insert nil object"); continue; }
        a->items[a->n++] = objc_retain(objs[i]);
    }
    for (Class k = c; k; k = k->superclass) if (k == RT_CLASS(NSMutableArray) || k == RT_CLASS(NSMutableSet)) { a->mut = 1; break; }
    return r;
}
id rt_array_new(id *objs, size_t n) { return a_alloc_with(RT_CLASS(NSArray), objs, n); }
static id a_array(Class c, SEL s) { (void)s; return objc_autorelease(a_alloc_with(c, NULL, 0)); }
static id a_arrayWithObject(Class c, SEL s, id o) { (void)s; return objc_autorelease(a_alloc_with(c, &o, 1)); }
static id a_arrayWithObjectsCount(Class c, SEL s, id *o, unsigned long n) { (void)s; return objc_autorelease(a_alloc_with(c, o, n)); }
static id collect_nil_terminated(Class c, id first, const uint64_t *rest) {
    size_t n = 0, cap = 8; id *v = rt_xmalloc(cap * sizeof(id));
    for (id o = first; o; o = (id)(uintptr_t)rest[n - 1]) {
        if (n == cap) { cap *= 2; v = realloc(v, cap * sizeof(id)); }
        v[n++] = o;
    }
    id r = a_alloc_with(c, v, n); free(v);
    return r;
}
id rt_NSArray_arrayWithObjects(Class c, SEL s, id first, const uint64_t *rest) { (void)s; return objc_autorelease(collect_nil_terminated(c, first, rest)); }
id rt_NSArray_initWithObjects(id self, SEL s, id first, const uint64_t *rest) { (void)s; Class c = self->isa; rt_dealloc_instance(self); return collect_nil_terminated(c, first, rest); }
static id a_arrayWithArray(Class c, SEL s, id o) {
    (void)s;
    if (!o) return objc_autorelease(a_alloc_with(c, NULL, 0));
    struct nsarr *a = (struct nsarr *)o;
    return objc_autorelease(a_alloc_with(c, a->items, a->n));
}
static id a_initWithObjectsCount(id self, SEL s, id *o, unsigned long n) { (void)s; Class c = self->isa; rt_dealloc_instance(self); return a_alloc_with(c, o, n); }
static id a_initWithArray(id self, SEL s, id o) { (void)s; Class c = self->isa; rt_dealloc_instance(self); struct nsarr *a = (struct nsarr *)o; return a_alloc_with(c, a ? a->items : NULL, a ? a->n : 0); }
static id a_init(id self, SEL s) { (void)s; for (Class k = self->isa; k; k = k->superclass) if (k == RT_CLASS(NSMutableArray) || k == RT_CLASS(NSMutableSet)) ((struct nsarr *)self)->mut = 1; return self; }
static void a_dealloc(id self, SEL s) {
    (void)s; struct nsarr *a = (struct nsarr *)self;
    for (size_t i = 0; i < a->n; i++) objc_release(a->items[i]);
    free(a->items); rt_dealloc_instance(self);
}
static unsigned long a_count(id self, SEL s) { (void)s; return ((struct nsarr *)self)->n; }
static id a_objectAtIndex(id self, SEL s, unsigned long i) {
    (void)s; struct nsarr *a = (struct nsarr *)self;
    if (i >= a->n) { range_error("NSArray objectAtIndex:", i, a->n); return NULL; }
    return a->items[i];
}
static id a_first(id self, SEL s) { (void)s; struct nsarr *a = (struct nsarr *)self; return a->n ? a->items[0] : NULL; }
static id a_last(id self, SEL s) { (void)s; struct nsarr *a = (struct nsarr *)self; return a->n ? a->items[a->n - 1] : NULL; }
static int obj_eq(id a, id b) { return a == b || (a && b && (RT_SEND1(long, a, "isEqual:", id, b) & 0xff)); }
static unsigned long a_indexOfObject(id self, SEL s, id o) {
    (void)s; struct nsarr *a = (struct nsarr *)self;
    for (size_t i = 0; i < a->n; i++) if (obj_eq(a->items[i], o)) return i;
    return 0x7fffffffffffffffUL;
}
static rt_bool a_contains(id self, SEL s, id o) { return a_indexOfObject(self, s, o) != 0x7fffffffffffffffUL; }
static id a_byAdding(id self, SEL s, id o) {
    (void)s; struct nsarr *a = (struct nsarr *)self;
    id r = a_alloc_with(RT_CLASS(NSArray), a->items, a->n); ((struct nsarr *)r)->mut = 0;
    arr_push((struct nsarr *)r, o); return objc_autorelease(r);
}
static id a_byAddingArray(id self, SEL s, id o) {
    (void)s; struct nsarr *a = (struct nsarr *)self, *b = (struct nsarr *)o;
    id r = a_alloc_with(RT_CLASS(NSArray), a->items, a->n);
    for (size_t i = 0; b && i < b->n; i++) arr_push((struct nsarr *)r, b->items[i]);
    return objc_autorelease(r);
}
static id a_join(id self, SEL s, id sep) {
    (void)s; struct nsarr *a = (struct nsarr *)self; rt_sb sb; rt_sb_init(&sb);
    for (size_t i = 0; i < a->n; i++) {
        if (i) rt_sb_puts(&sb, rt_str_utf8(sep));
        char *d = rt_describe(a->items[i]); rt_sb_puts(&sb, d); free(d);
    }
    char *t = rt_sb_take(&sb); id r = rt_str(t); free(t); return r;
}
static unsigned long a_fast(id self, SEL s, struct fast_state *st, id *buf, unsigned long len) {
    (void)s; (void)buf; (void)len; struct nsarr *a = (struct nsarr *)self;
    if (st->state) return 0;
    st->state = 1; st->itemsPtr = a->items; st->mutationsPtr = &a->mutations;
    return a->n;
}
static void call_enum_block(void *blk, id o, unsigned long i, bool *stop) {
    ((void (*)(void *, id, unsigned long, bool *))((struct rt_block *)blk)->invoke)(blk, o, i, stop);
}
static void a_enumerate(id self, SEL s, void *blk) {
    (void)s; struct nsarr *a = (struct nsarr *)self; bool stop = 0;
    for (size_t i = 0; i < a->n && !stop; i++) call_enum_block(blk, a->items[i], i, &stop);
}
static void a_enumerateOpts(id self, SEL s, unsigned long opts, void *blk) {
    (void)s; struct nsarr *a = (struct nsarr *)self; bool stop = 0;
    if (opts & 2) { for (size_t i = a->n; i-- > 0 && !stop;) call_enum_block(blk, a->items[i], i, &stop); }
    else for (size_t i = 0; i < a->n && !stop; i++) call_enum_block(blk, a->items[i], i, &stop);
}
static unsigned long a_indexOfObjectPassing(id self, SEL s, void *blk) {
    (void)s; struct nsarr *a = (struct nsarr *)self; bool stop = 0;
    for (size_t i = 0; i < a->n && !stop; i++) {
        long r = ((long (*)(void *, id, unsigned long, bool *))((struct rt_block *)blk)->invoke)(blk, a->items[i], i, &stop);
        if (r & 0xff) return i;
    }
    return 0x7fffffffffffffffUL;
}

/* stable merge sort with a comparison callback */
typedef long (*cmp_fn)(id, id, void *);
static void msort(id *v, id *tmp, size_t n, cmp_fn f, void *ctx) {
    if (n < 2) return;
    size_t h = n / 2;
    msort(v, tmp, h, f, ctx); msort(v + h, tmp, n - h, f, ctx);
    size_t i = 0, j = h, k = 0;
    while (i < h && j < n) tmp[k++] = f(v[j], v[i], ctx) < 0 ? v[j++] : v[i++];
    while (i < h) tmp[k++] = v[i++];
    while (j < n) tmp[k++] = v[j++];
    memcpy(v, tmp, n * sizeof(id));
}
static long cmp_sel(id a, id b, void *ctx) { return ((long (*)(id, SEL, id))objc_msgSend)(a, (SEL)ctx, b); }
static long cmp_block(id a, id b, void *ctx) { return ((long (*)(void *, id, id))((struct rt_block *)ctx)->invoke)(ctx, a, b); }
struct fn_ctx { long (*fn)(id, id, void *); void *ctx; };
static long cmp_func(id a, id b, void *ctx) { struct fn_ctx *c = ctx; return c->fn(a, b, c->ctx); }
static void sort_items(struct nsarr *a, cmp_fn f, void *ctx) {
    id *tmp = rt_xmalloc((a->n + 1) * sizeof(id));
    msort(a->items, tmp, a->n, f, ctx);
    free(tmp); a->mutations++;
}
static id a_sortedSel(id self, SEL s, SEL cmp) { (void)s; struct nsarr *a = (struct nsarr *)self; id r = a_alloc_with(RT_CLASS(NSArray), a->items, a->n); sort_items((struct nsarr *)r, cmp_sel, (void *)cmp); return objc_autorelease(r); }
static id a_sortedCmp(id self, SEL s, void *blk) { (void)s; struct nsarr *a = (struct nsarr *)self; id r = a_alloc_with(RT_CLASS(NSArray), a->items, a->n); sort_items((struct nsarr *)r, cmp_block, blk); return objc_autorelease(r); }
static id a_sortedFunc(id self, SEL s, long (*fn)(id, id, void *), void *ctx) { (void)s; struct nsarr *a = (struct nsarr *)self; struct fn_ctx c = { fn, ctx }; id r = a_alloc_with(RT_CLASS(NSArray), a->items, a->n); sort_items((struct nsarr *)r, cmp_func, &c); return objc_autorelease(r); }
static id a_reverseEnum(id self, SEL s);
static id a_objectEnumerator(id self, SEL s);
static id a_copy(id self, SEL s, void *z) {
    (void)s; (void)z; struct nsarr *a = (struct nsarr *)self;
    if (!a->mut && arr_is_ours(self) && (self->isa == RT_CLASS(NSArray) || self->isa == RT_CLASS(NSSet))) return objc_retain(self);
    Class c = rt_is_kind_of(self, RT_CLASS(NSSet)) ? RT_CLASS(NSSet) : RT_CLASS(NSArray);
    id r = a_alloc_with(c, a->items, a->n); ((struct nsarr *)r)->mut = 0; return r;
}
static id a_mutableCopy(id self, SEL s, void *z) {
    (void)s; (void)z; struct nsarr *a = (struct nsarr *)self;
    Class c = rt_is_kind_of(self, RT_CLASS(NSSet)) ? RT_CLASS(NSMutableSet) : RT_CLASS(NSMutableArray);
    return a_alloc_with(c, a->items, a->n);
}
static int needs_quotes(const char *s) {
    if (!*s) return 1;
    for (; *s; s++) { char c = *s; if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '$' || c == '/' || (unsigned char)c >= 0x80)) return 1; }
    return 0;
}
static void desc_item(rt_sb *sb, id o, int indent);
static void indent_to(rt_sb *sb, int n) { for (int i = 0; i < n; i++) rt_sb_puts(sb, "    "); }
static void desc_array(rt_sb *sb, struct nsarr *a, int indent, const char *open, const char *close) {
    rt_sb_puts(sb, open); rt_sb_putc(sb, '\n');
    for (size_t i = 0; i < a->n; i++) {
        indent_to(sb, indent + 1); desc_item(sb, a->items[i], indent + 1);
        if (i + 1 < a->n) rt_sb_putc(sb, ',');
        rt_sb_putc(sb, '\n');
    }
    indent_to(sb, indent); rt_sb_puts(sb, close);
}
static void desc_dict(rt_sb *sb, struct nsdict *d, int indent) {
    rt_sb_puts(sb, "{\n");
    for (size_t i = 0; i < d->n; i++) {
        indent_to(sb, indent + 1); desc_item(sb, d->keys[i], indent + 1);
        rt_sb_puts(sb, " = "); desc_item(sb, d->vals[i], indent + 1); rt_sb_puts(sb, ";\n");
    }
    indent_to(sb, indent); rt_sb_puts(sb, "}");
}
static void desc_item(rt_sb *sb, id o, int indent) {
    if (o && rt_is_kind_of(o, RT_CLASS(NSArray))) { desc_array(sb, (struct nsarr *)o, indent, "(", ")"); return; }
    if (o && rt_is_kind_of(o, RT_CLASS(NSSet))) { desc_array(sb, (struct nsarr *)o, indent, "{(", ")}"); return; }
    if (o && rt_is_kind_of(o, RT_CLASS(NSDictionary))) { desc_dict(sb, (struct nsdict *)o, indent); return; }
    char *d = rt_describe(o);
    if (o && rt_is_string(o) && needs_quotes(d)) { rt_sb_putc(sb, '"'); rt_sb_puts(sb, d); rt_sb_putc(sb, '"'); }
    else rt_sb_puts(sb, d);
    free(d);
}
static id coll_description(id self, SEL s) { (void)s; rt_sb sb; rt_sb_init(&sb); desc_item(&sb, self, 0); char *t = rt_sb_take(&sb); id r = rt_str(t); free(t); return r; }
static rt_bool a_isEqual(id self, SEL s, id o) {
    (void)s; if (self == o) return 1; if (!o || !rt_is_kind_of(o, RT_CLASS(NSArray))) return 0;
    struct nsarr *a = (struct nsarr *)self, *b = (struct nsarr *)o;
    if (a->n != b->n) return 0;
    for (size_t i = 0; i < a->n; i++) if (!obj_eq(a->items[i], b->items[i])) return 0;
    return 1;
}
static unsigned long a_hash(id self, SEL s) { (void)s; return ((struct nsarr *)self)->n; }
static id a_subarray(id self, SEL s, NSRange r) {
    (void)s; struct nsarr *a = (struct nsarr *)self;
    if (r.location + r.length > a->n) { range_error("NSArray subarrayWithRange:", r.location + r.length, a->n); return objc_autorelease(a_alloc_with(RT_CLASS(NSArray), NULL, 0)); }
    return objc_autorelease(a_alloc_with(RT_CLASS(NSArray), a->items + r.location, r.length));
}
static void a_makeObjectsPerform(id self, SEL s, SEL q) { (void)s; struct nsarr *a = (struct nsarr *)self; for (size_t i = 0; i < a->n; i++) ((void (*)(id, SEL))objc_msgSend)(a->items[i], q); }
static id a_valueForKey(id self, SEL s, id key) {
    (void)s; struct nsarr *a = (struct nsarr *)self; id r = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array");
    for (size_t i = 0; i < a->n; i++) { id v = RT_SEND1(id, a->items[i], "valueForKey:", id, key); RT_SEND1(void, r, "addObject:", id, v ? v : RT_SEND0(id, (id)rt_class_named("NSNull"), "null")); }
    return r;
}

/* ------------------------------------------------------- NSMutableArray */
static id ma_withCapacity(Class c, SEL s, unsigned long n) { (void)s; id r = a_alloc_with(c, NULL, 0); arr_reserve((struct nsarr *)r, n); return objc_autorelease(r); }
static id ma_initWithCapacity(id self, SEL s, unsigned long n) { (void)s; ((struct nsarr *)self)->mut = 1; arr_reserve((struct nsarr *)self, n); return self; }
static void ma_add(id self, SEL s, id o) {
    (void)s; if (!o) { rt_soft_error("*** -[__NSArrayM insertObject:atIndex:]: object cannot be nil"); return; }
    arr_push((struct nsarr *)self, o);
}
static void ma_insert(id self, SEL s, id o, unsigned long i) {
    (void)s; struct nsarr *a = (struct nsarr *)self;
    if (!o) { rt_soft_error("*** -[__NSArrayM insertObject:atIndex:]: object cannot be nil"); return; }
    if (i > a->n) { range_error("__NSArrayM insertObject:atIndex:", i, a->n); return; }
    arr_reserve(a, a->n + 1);
    memmove(a->items + i + 1, a->items + i, (a->n - i) * sizeof(id));
    a->items[i] = objc_retain(o); a->n++; a->mutations++;
}
static void ma_removeAt(id self, SEL s, unsigned long i) {
    (void)s; struct nsarr *a = (struct nsarr *)self;
    if (i >= a->n) { range_error("__NSArrayM removeObjectAtIndex:", i, a->n); return; }
    id o = a->items[i];
    memmove(a->items + i, a->items + i + 1, (a->n - i - 1) * sizeof(id));
    a->n--; a->mutations++;
    objc_release(o);
}
static void ma_removeLast(id self, SEL s) { struct nsarr *a = (struct nsarr *)self; if (a->n) ma_removeAt(self, s, a->n - 1); }
static void ma_removeFirst(id self, SEL s) { struct nsarr *a = (struct nsarr *)self; if (a->n) ma_removeAt(self, s, 0); }
static void ma_removeObject(id self, SEL s, id o) {
    struct nsarr *a = (struct nsarr *)self;
    for (size_t i = a->n; i-- > 0;) if (obj_eq(a->items[i], o)) ma_removeAt(self, s, i);
}
static void ma_removeIdentical(id self, SEL s, id o) {
    struct nsarr *a = (struct nsarr *)self;
    for (size_t i = a->n; i-- > 0;) if (a->items[i] == o) ma_removeAt(self, s, i);
}
static void ma_removeAll(id self, SEL s);
static void ma_removeObjectsInArray(id self, SEL s, id other) {
    if (!other) return;
    if (other == self) { ma_removeAll(self, s); return; }
    unsigned long n = RT_SEND0(unsigned long, other, "count");
    for (unsigned long j = 0; j < n; j++) ma_removeObject(self, s, RT_SEND1(id, other, "objectAtIndex:", unsigned long, j));
}
static unsigned long a_indexIdentical(id self, SEL s, id o) {
    (void)s; struct nsarr *a = (struct nsarr *)self;
    for (size_t i = 0; i < a->n; i++) if (a->items[i] == o) return i;
    return (unsigned long)-1 >> 1;
}
static void ma_removeAll(id self, SEL s) {
    (void)s; struct nsarr *a = (struct nsarr *)self;
    size_t n = a->n; id *old = a->items;
    a->items = NULL; a->n = a->cap = 0; a->mutations++;
    for (size_t i = 0; i < n; i++) objc_release(old[i]);
    free(old);
}
static void ma_replace(id self, SEL s, unsigned long i, id o) {
    (void)s; struct nsarr *a = (struct nsarr *)self;
    if (i >= a->n) { range_error("__NSArrayM replaceObjectAtIndex:withObject:", i, a->n); return; }
    id old = a->items[i]; a->items[i] = objc_retain(o); a->mutations++; objc_release(old);
}
static void ma_setAtIndexed(id self, SEL s, id o, unsigned long i) {
    struct nsarr *a = (struct nsarr *)self;
    if (i == a->n) ma_add(self, s, o); else ma_replace(self, s, i, o);
}
static void ma_addArray(id self, SEL s, id o) { (void)s; struct nsarr *b = (struct nsarr *)o; for (size_t i = 0; b && i < b->n; i++) arr_push((struct nsarr *)self, b->items[i]); }
static void ma_exchange(id self, SEL s, unsigned long i, unsigned long j) { (void)s; struct nsarr *a = (struct nsarr *)self; id t = a->items[i]; a->items[i] = a->items[j]; a->items[j] = t; a->mutations++; }
static void ma_sortSel(id self, SEL s, SEL cmp) { (void)s; sort_items((struct nsarr *)self, cmp_sel, (void *)cmp); }
static void ma_sortCmp(id self, SEL s, void *blk) { (void)s; sort_items((struct nsarr *)self, cmp_block, blk); }

/* ---------------------------------------------------------- enumerator */
struct nsenum { Class isa; id coll; size_t i; long reverse; };
static id e_next(id self, SEL s) {
    (void)s; struct nsenum *e = (struct nsenum *)self; struct nsarr *a = (struct nsarr *)e->coll;
    if (e->i >= a->n) return NULL;
    size_t k = e->reverse ? a->n - 1 - e->i : e->i; e->i++;
    return a->items[k];
}
static id e_all(id self, SEL s) {
    struct nsenum *e = (struct nsenum *)self; id r = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array"); id o;
    (void)e; while ((o = e_next(self, s))) RT_SEND1(void, r, "addObject:", id, o);
    return r;
}
static void e_dealloc(id self, SEL s) { (void)s; objc_release(((struct nsenum *)self)->coll); rt_dealloc_instance(self); }
static id make_enum(id coll, int rev) { id r = rt_alloc_instance(RT_CLASS(NSEnumerator), 0); struct nsenum *e = (struct nsenum *)r; e->coll = objc_retain(coll); e->reverse = rev; return objc_autorelease(r); }
static id a_objectEnumerator(id self, SEL s) { (void)s; return make_enum(self, 0); }
static id a_reverseEnum(id self, SEL s) { (void)s; return make_enum(self, 1); }
static unsigned long e_fast(id self, SEL s, struct fast_state *st, id *buf, unsigned long len) {
    (void)s; if (!len) return 0;
    id o = e_next(self, s);
    if (!o) return 0;
    buf[0] = o; st->itemsPtr = buf; st->mutationsPtr = &st->extra[0]; st->state = 1;
    return 1;
}

/* ---------------------------------------------------------- NSDictionary */
static long dict_find(struct nsdict *d, id key) {
    unsigned long h = RT_SEND0(unsigned long, key, "hash");
    for (size_t i = 0; i < d->n; i++) {
        if (d->keys[i] == key) return (long)i;
        if (RT_SEND0(unsigned long, d->keys[i], "hash") == h && obj_eq(d->keys[i], key)) return (long)i;
    }
    return -1;
}
static void dict_set(struct nsdict *d, id key, id val) {
    if (!key || !val) { rt_soft_error("*** -[NSMutableDictionary setObject:forKey:]: key or object cannot be nil"); return; }
    long i = dict_find(d, key);
    objc_retain(val);
    if (i >= 0) { id old = d->vals[i]; d->vals[i] = val; objc_release(old); d->mutations++; return; }
    if (d->n == d->cap) { d->cap = d->cap ? d->cap * 2 : 8; d->keys = realloc(d->keys, d->cap * sizeof(id)); d->vals = realloc(d->vals, d->cap * sizeof(id)); }
    d->keys[d->n] = rt_is_string(key) || rt_responds(key, rt_sel("copyWithZone:")) ? RT_SEND0(id, key, "copy") : objc_retain(key);
    d->vals[d->n++] = val; d->mutations++;
}
id rt_dict_get(id dict, id key) { struct nsdict *d = (struct nsdict *)dict; if (!d || !key) return NULL; long i = dict_find(d, key); return i >= 0 ? d->vals[i] : NULL; }
static id d_alloc_with(Class c, id *vals, id *keys, size_t n) {
    id r = rt_alloc_instance(c, 0); struct nsdict *d = (struct nsdict *)r;
    for (size_t i = 0; i < n; i++) dict_set(d, keys[i], vals[i]);
    for (Class k = c; k; k = k->superclass) if (k == RT_CLASS(NSMutableDictionary)) { d->mut = 1; break; }
    return r;
}
id rt_dict_new(id *vals, id *keys, size_t n) { return d_alloc_with(RT_CLASS(NSMutableDictionary), vals, keys, n); }
void rt_dict_set(id dict, id key, id val) { dict_set((struct nsdict *)dict, key, val); }
static id d_dictionary(Class c, SEL s) { (void)s; return objc_autorelease(d_alloc_with(c, NULL, NULL, 0)); }
static id d_withObjectsForKeysCount(Class c, SEL s, id *v, id *k, unsigned long n) { (void)s; return objc_autorelease(d_alloc_with(c, v, k, n)); }
static id d_withObjectForKey(Class c, SEL s, id v, id k) { (void)s; return objc_autorelease(d_alloc_with(c, &v, &k, 1)); }
static id d_withObjectsForKeys(Class c, SEL s, id va, id ka) {
    (void)s; struct nsarr *v = (struct nsarr *)va, *k = (struct nsarr *)ka;
    return objc_autorelease(d_alloc_with(c, v->items, k->items, v->n < k->n ? v->n : k->n));
}
static id d_withDictionary(Class c, SEL s, id o) { (void)s; struct nsdict *d = (struct nsdict *)o; return objc_autorelease(d_alloc_with(c, d ? d->vals : NULL, d ? d->keys : NULL, d ? d->n : 0)); }
static id collect_pairs(Class c, id first, const uint64_t *rest) {
    id r = d_alloc_with(c, NULL, NULL, 0);
    id val = first; size_t i = 0;
    while (val) {
        id key = (id)(uintptr_t)rest[i++];
        dict_set((struct nsdict *)r, key, val);
        val = (id)(uintptr_t)rest[i++];
    }
    return r;
}
id rt_NSDictionary_dictionaryWithObjectsAndKeys(Class c, SEL s, id first, const uint64_t *rest) { (void)s; return objc_autorelease(collect_pairs(c, first, rest)); }
id rt_NSDictionary_initWithObjectsAndKeys(id self, SEL s, id first, const uint64_t *rest) { (void)s; Class c = self->isa; rt_dealloc_instance(self); return collect_pairs(c, first, rest); }
static id d_initWithObjectsForKeysCount(id self, SEL s, id *v, id *k, unsigned long n) { (void)s; Class c = self->isa; rt_dealloc_instance(self); return d_alloc_with(c, v, k, n); }
static id d_initWithDictionary(id self, SEL s, id o) { (void)s; Class c = self->isa; rt_dealloc_instance(self); struct nsdict *d = (struct nsdict *)o; return d_alloc_with(c, d ? d->vals : NULL, d ? d->keys : NULL, d ? d->n : 0); }
static id d_init(id self, SEL s) { (void)s; for (Class k = self->isa; k; k = k->superclass) if (k == RT_CLASS(NSMutableDictionary)) ((struct nsdict *)self)->mut = 1; return self; }
static void d_dealloc(id self, SEL s) {
    (void)s; struct nsdict *d = (struct nsdict *)self;
    for (size_t i = 0; i < d->n; i++) { objc_release(d->keys[i]); objc_release(d->vals[i]); }
    free(d->keys); free(d->vals); rt_dealloc_instance(self);
}
static unsigned long d_count(id self, SEL s) { (void)s; return ((struct nsdict *)self)->n; }
static id d_objectForKey(id self, SEL s, id k) { (void)s; return rt_dict_get(self, k); }
static id d_allKeys(id self, SEL s) { (void)s; struct nsdict *d = (struct nsdict *)self; return objc_autorelease(a_alloc_with(RT_CLASS(NSArray), d->keys, d->n)); }
static id d_allValues(id self, SEL s) { (void)s; struct nsdict *d = (struct nsdict *)self; return objc_autorelease(a_alloc_with(RT_CLASS(NSArray), d->vals, d->n)); }
static unsigned long d_fast(id self, SEL s, struct fast_state *st, id *buf, unsigned long len) {
    (void)s; (void)buf; (void)len; struct nsdict *d = (struct nsdict *)self;
    if (st->state) return 0;
    st->state = 1; st->itemsPtr = d->keys; st->mutationsPtr = &d->mutations;
    return d->n;
}
static void d_enumerate(id self, SEL s, void *blk) {
    (void)s; struct nsdict *d = (struct nsdict *)self; bool stop = 0;
    for (size_t i = 0; i < d->n && !stop; i++)
        ((void (*)(void *, id, id, bool *))((struct rt_block *)blk)->invoke)(blk, d->keys[i], d->vals[i], &stop);
}
static id d_keyEnumerator(id self, SEL s) { (void)s; return make_enum(d_allKeys(self, s), 0); }
static id d_copy(id self, SEL s, void *z) { (void)s; (void)z; struct nsdict *d = (struct nsdict *)self; if (!d->mut && self->isa == RT_CLASS(NSDictionary)) return objc_retain(self); id r = d_alloc_with(RT_CLASS(NSDictionary), d->vals, d->keys, d->n); ((struct nsdict *)r)->mut = 0; return r; }
static id d_mutableCopy(id self, SEL s, void *z) { (void)s; (void)z; struct nsdict *d = (struct nsdict *)self; return d_alloc_with(RT_CLASS(NSMutableDictionary), d->vals, d->keys, d->n); }
static rt_bool d_isEqual(id self, SEL s, id o) {
    (void)s; if (self == o) return 1; if (!o || !rt_is_kind_of(o, RT_CLASS(NSDictionary))) return 0;
    struct nsdict *a = (struct nsdict *)self, *b = (struct nsdict *)o;
    if (a->n != b->n) return 0;
    for (size_t i = 0; i < a->n; i++) if (!obj_eq(a->vals[i], rt_dict_get(o, a->keys[i]))) return 0;
    return 1;
}
static void md_setObject(id self, SEL s, id v, id k) { (void)s; dict_set((struct nsdict *)self, k, v); }
static void md_setValue(id self, SEL s, id v, id k) {
    (void)s; struct nsdict *d = (struct nsdict *)self;
    if (v) dict_set(d, k, v);
    else { long i = dict_find(d, k); if (i >= 0) { objc_release(d->keys[i]); objc_release(d->vals[i]); d->keys[i] = d->keys[d->n - 1]; d->vals[i] = d->vals[d->n - 1]; d->n--; d->mutations++; } }
}
static void md_remove(id self, SEL s, id k) { md_setValue(self, s, NULL, k); }
static void md_removeAll(id self, SEL s) {
    (void)s; struct nsdict *d = (struct nsdict *)self;
    for (size_t i = 0; i < d->n; i++) { objc_release(d->keys[i]); objc_release(d->vals[i]); }
    d->n = 0; d->mutations++;
}
static void md_addEntries(id self, SEL s, id o) { (void)s; struct nsdict *b = (struct nsdict *)o; for (size_t i = 0; b && i < b->n; i++) dict_set((struct nsdict *)self, b->keys[i], b->vals[i]); }
static id md_withCapacity(Class c, SEL s, unsigned long n) { (void)s; (void)n; return objc_autorelease(d_alloc_with(c, NULL, NULL, 0)); }

/* --------------------------------------------------------------- NSSet */
static void set_add(struct nsarr *a, id o) { for (size_t i = 0; i < a->n; i++) if (obj_eq(a->items[i], o)) return; arr_push(a, o); }
static id set_alloc_with(Class c, id *objs, size_t n) {
    id r = a_alloc_with(c, NULL, 0);
    for (size_t i = 0; i < n; i++) set_add((struct nsarr *)r, objs[i]);
    return r;
}
id rt_set_new(id *objs, size_t n) { return set_alloc_with(RT_CLASS(NSSet), objs, n); }
static id st_set(Class c, SEL s) { (void)s; return objc_autorelease(set_alloc_with(c, NULL, 0)); }
static id st_withObject(Class c, SEL s, id o) { (void)s; return objc_autorelease(set_alloc_with(c, &o, 1)); }
static id st_withArray(Class c, SEL s, id a) { (void)s; struct nsarr *x = (struct nsarr *)a; return objc_autorelease(set_alloc_with(c, x ? x->items : NULL, x ? x->n : 0)); }
static id st_withObjectsCount(Class c, SEL s, id *o, unsigned long n) { (void)s; return objc_autorelease(set_alloc_with(c, o, n)); }
static id st_anyObject(id self, SEL s) { (void)s; struct nsarr *a = (struct nsarr *)self; return a->n ? a->items[0] : NULL; }
static id st_allObjects(id self, SEL s) { (void)s; struct nsarr *a = (struct nsarr *)self; return objc_autorelease(a_alloc_with(RT_CLASS(NSArray), a->items, a->n)); }
static rt_bool st_member(id self, SEL s, id o) { return a_contains(self, s, o); }
static void st_add(id self, SEL s, id o) { (void)s; set_add((struct nsarr *)self, o); }
static void st_enumerate(id self, SEL s, void *blk) {
    (void)s; struct nsarr *a = (struct nsarr *)self; bool stop = 0;
    for (size_t i = 0; i < a->n && !stop; i++) ((void (*)(void *, id, bool *))((struct rt_block *)blk)->invoke)(blk, a->items[i], &stop);
}

void rt_collections_init(void) {
    Class o = RT_CLASS(NSObject);
    Class a = RT_CLASS(NSArray), ma = RT_CLASS(NSMutableArray);
    rt_native_class(a, &iosrt_meta_NSArray, "NSArray", o, sizeof(struct nsarr) - sizeof(void *));
    rt_native_class(ma, &iosrt_meta_NSMutableArray, "NSMutableArray", a, 0);
    extern char iosrt_imp_arrayWithObjects[], iosrt_imp_initWithObjects[];
    CM(a, "array", a_array); CM(a, "arrayWithObject:", a_arrayWithObject); CM(a, "arrayWithObjects:count:", a_arrayWithObjectsCount);
    CM(a, "arrayWithObjects:", iosrt_imp_arrayWithObjects); CM(a, "arrayWithArray:", a_arrayWithArray);
    IM(a, "init", a_init); IM(a, "initWithObjects:count:", a_initWithObjectsCount); IM(a, "initWithObjects:", iosrt_imp_initWithObjects);
    IM(a, "initWithArray:", a_initWithArray); IM(a, "dealloc", a_dealloc);
    IM(a, "count", a_count); IM(a, "objectAtIndex:", a_objectAtIndex); IM(a, "objectAtIndexedSubscript:", a_objectAtIndex);
    IM(a, "firstObject", a_first); IM(a, "lastObject", a_last); IM(a, "indexOfObject:", a_indexOfObject);
    IM(a, "containsObject:", a_contains); IM(a, "arrayByAddingObject:", a_byAdding); IM(a, "arrayByAddingObjectsFromArray:", a_byAddingArray);
    IM(a, "componentsJoinedByString:", a_join); IM(a, "countByEnumeratingWithState:objects:count:", a_fast);
    IM(a, "enumerateObjectsUsingBlock:", a_enumerate); IM(a, "enumerateObjectsWithOptions:usingBlock:", a_enumerateOpts);
    IM(a, "indexOfObjectPassingTest:", a_indexOfObjectPassing);
    IM(a, "sortedArrayUsingSelector:", a_sortedSel); IM(a, "sortedArrayUsingComparator:", a_sortedCmp);
    IM(a, "sortedArrayUsingFunction:context:", a_sortedFunc);
    IM(a, "objectEnumerator", a_objectEnumerator); IM(a, "reverseObjectEnumerator", a_reverseEnum);
    IM(a, "copyWithZone:", a_copy); IM(a, "mutableCopyWithZone:", a_mutableCopy); IM(a, "description", coll_description);
    IM(a, "isEqual:", a_isEqual); IM(a, "isEqualToArray:", a_isEqual); IM(a, "hash", a_hash);
    IM(a, "subarrayWithRange:", a_subarray); IM(a, "makeObjectsPerformSelector:", a_makeObjectsPerform); IM(a, "indexOfObjectIdenticalTo:", a_indexIdentical);
    IM(a, "valueForKey:", a_valueForKey);
    CM(ma, "arrayWithCapacity:", ma_withCapacity); IM(ma, "initWithCapacity:", ma_initWithCapacity);
    IM(ma, "addObject:", ma_add); IM(ma, "insertObject:atIndex:", ma_insert); IM(ma, "removeObjectAtIndex:", ma_removeAt);
    IM(ma, "removeLastObject", ma_removeLast); IM(ma, "removeFirstObject", ma_removeFirst); IM(ma, "removeObject:", ma_removeObject); IM(ma, "removeObjectIdenticalTo:", ma_removeIdentical); IM(ma, "removeObjectsInArray:", ma_removeObjectsInArray);
    IM(ma, "removeAllObjects", ma_removeAll); IM(ma, "replaceObjectAtIndex:withObject:", ma_replace);
    IM(ma, "setObject:atIndexedSubscript:", ma_setAtIndexed); IM(ma, "addObjectsFromArray:", ma_addArray);
    IM(ma, "exchangeObjectAtIndex:withObjectAtIndex:", ma_exchange);
    IM(ma, "sortUsingSelector:", ma_sortSel); IM(ma, "sortUsingComparator:", ma_sortCmp);

    Class en = RT_CLASS(NSEnumerator);
    rt_native_class(en, &iosrt_meta_NSEnumerator, "NSEnumerator", o, sizeof(struct nsenum) - sizeof(void *));
    IM(en, "nextObject", e_next); IM(en, "allObjects", e_all); IM(en, "dealloc", e_dealloc);
    IM(en, "countByEnumeratingWithState:objects:count:", e_fast);

    Class d = RT_CLASS(NSDictionary), md = RT_CLASS(NSMutableDictionary);
    rt_native_class(d, &iosrt_meta_NSDictionary, "NSDictionary", o, sizeof(struct nsdict) - sizeof(void *));
    rt_native_class(md, &iosrt_meta_NSMutableDictionary, "NSMutableDictionary", d, 0);
    extern char iosrt_imp_dictionaryWithObjectsAndKeys[], iosrt_imp_initWithObjectsAndKeys[];
    CM(d, "dictionary", d_dictionary); CM(d, "dictionaryWithObjects:forKeys:count:", d_withObjectsForKeysCount);
    CM(d, "dictionaryWithObject:forKey:", d_withObjectForKey); CM(d, "dictionaryWithObjects:forKeys:", d_withObjectsForKeys);
    CM(d, "dictionaryWithDictionary:", d_withDictionary); CM(d, "dictionaryWithObjectsAndKeys:", iosrt_imp_dictionaryWithObjectsAndKeys);
    IM(d, "init", d_init); IM(d, "initWithObjects:forKeys:count:", d_initWithObjectsForKeysCount);
    IM(d, "initWithObjectsAndKeys:", iosrt_imp_initWithObjectsAndKeys); IM(d, "initWithDictionary:", d_initWithDictionary);
    IM(d, "dealloc", d_dealloc); IM(d, "count", d_count); IM(d, "objectForKey:", d_objectForKey);
    IM(d, "objectForKeyedSubscript:", d_objectForKey); IM(d, "valueForKey:", d_objectForKey);
    IM(d, "allKeys", d_allKeys); IM(d, "allValues", d_allValues); IM(d, "countByEnumeratingWithState:objects:count:", d_fast);
    IM(d, "enumerateKeysAndObjectsUsingBlock:", d_enumerate); IM(d, "keyEnumerator", d_keyEnumerator);
    IM(d, "copyWithZone:", d_copy); IM(d, "mutableCopyWithZone:", d_mutableCopy); IM(d, "description", coll_description);
    IM(d, "isEqual:", d_isEqual); IM(d, "isEqualToDictionary:", d_isEqual); IM(d, "hash", d_count);
    CM(md, "dictionaryWithCapacity:", md_withCapacity);
    IM(md, "setObject:forKey:", md_setObject); IM(md, "setObject:forKeyedSubscript:", md_setValue);
    IM(md, "setValue:forKey:", md_setValue); IM(md, "removeObjectForKey:", md_remove);
    IM(md, "removeAllObjects", md_removeAll); IM(md, "addEntriesFromDictionary:", md_addEntries);

    Class st = RT_CLASS(NSSet), mst = RT_CLASS(NSMutableSet);
    rt_native_class(st, &iosrt_meta_NSSet, "NSSet", o, sizeof(struct nsarr) - sizeof(void *));
    rt_native_class(mst, &iosrt_meta_NSMutableSet, "NSMutableSet", st, 0);
    CM(st, "set", st_set); CM(st, "setWithObject:", st_withObject); CM(st, "setWithArray:", st_withArray);
    CM(st, "setWithObjects:count:", st_withObjectsCount);
    IM(st, "init", a_init); IM(st, "dealloc", a_dealloc); IM(st, "count", a_count); IM(st, "anyObject", st_anyObject);
    IM(st, "allObjects", st_allObjects); IM(st, "containsObject:", st_member); IM(st, "member:", st_member);
    IM(st, "countByEnumeratingWithState:objects:count:", a_fast); IM(st, "objectEnumerator", a_objectEnumerator);
    IM(st, "enumerateObjectsUsingBlock:", st_enumerate);
    IM(st, "copyWithZone:", a_copy); IM(st, "mutableCopyWithZone:", a_mutableCopy); IM(st, "description", coll_description);
    CM(mst, "setWithCapacity:", ma_withCapacity);
    IM(mst, "addObject:", st_add); IM(mst, "removeObject:", ma_removeObject); IM(mst, "removeAllObjects", ma_removeAll);
}
