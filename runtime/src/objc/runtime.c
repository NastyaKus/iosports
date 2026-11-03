/*
 * Objective-C runtime compatible with Apple's ObjC2 binary metadata
 * (class_ro_t, method lists incl. relative lists, non-fragile ivars,
 * categories, protocols, selector references) — written from the public
 * ABI description, independent of Apple's objc4 sources.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>

static pthread_mutex_t rt_lock;
static uint32_t g_cache_gen = 1;

/* ------------------------------------------------------------ selectors */
static struct { const char **slots; uint32_t cap, count; } seltab;
static pthread_mutex_t sel_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t strhash(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    while (*s) { h ^= (uint8_t)*s++; h *= 1099511628211ULL; }
    return h;
}
static uint64_t ptrhash(const void *p) { uint64_t x = (uint64_t)(uintptr_t)p; x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33; return x; }

static void sel_grow(void) {
    uint32_t nc = seltab.cap ? seltab.cap * 2 : 4096;
    const char **ns = rt_xcalloc(nc, sizeof *ns);
    for (uint32_t i = 0; i < seltab.cap; i++) {
        const char *s = seltab.slots[i];
        if (!s) continue;
        uint32_t j = (uint32_t)strhash(s) & (nc - 1);
        while (ns[j]) j = (j + 1) & (nc - 1);
        ns[j] = s;
    }
    free(seltab.slots); seltab.slots = ns; seltab.cap = nc;
}
SEL rt_sel(const char *name) {
    if (!name) return NULL;
    pthread_mutex_lock(&sel_lock);
    if ((seltab.count + 1) * 2 > seltab.cap) sel_grow();
    uint32_t j = (uint32_t)strhash(name) & (seltab.cap - 1);
    while (seltab.slots[j]) {
        if (seltab.slots[j] == name || !strcmp(seltab.slots[j], name)) { SEL r = seltab.slots[j]; pthread_mutex_unlock(&sel_lock); return r; }
        j = (j + 1) & (seltab.cap - 1);
    }
    const char *s = rt_xstrdup(name);
    seltab.slots[j] = s; seltab.count++;
    pthread_mutex_unlock(&sel_lock);
    return s;
}

/* ----------------------------------------------------------- class names */
static struct { const char **keys; Class *vals; uint32_t cap, count; } clstab;
static void cls_put(const char *name, Class c) {
    if ((clstab.count + 1) * 2 > clstab.cap) {
        uint32_t nc = clstab.cap ? clstab.cap * 2 : 1024;
        const char **nk = rt_xcalloc(nc, sizeof *nk); Class *nv = rt_xcalloc(nc, sizeof *nv);
        for (uint32_t i = 0; i < clstab.cap; i++) if (clstab.keys[i]) {
            uint32_t j = (uint32_t)strhash(clstab.keys[i]) & (nc - 1);
            while (nk[j]) j = (j + 1) & (nc - 1);
            nk[j] = clstab.keys[i]; nv[j] = clstab.vals[i];
        }
        free(clstab.keys); free(clstab.vals); clstab.keys = nk; clstab.vals = nv; clstab.cap = nc;
    }
    uint32_t j = (uint32_t)strhash(name) & (clstab.cap - 1);
    while (clstab.keys[j]) {
        if (!strcmp(clstab.keys[j], name)) {
            /* real class replaces a missing-class placeholder; first real wins otherwise */
            if (rt_cls(clstab.vals[j])->flags & RT_CLS_MISSING) clstab.vals[j] = c;
            return;
        }
        j = (j + 1) & (clstab.cap - 1);
    }
    clstab.keys[j] = name; clstab.vals[j] = c; clstab.count++;
}
Class rt_class_named(const char *name) {
    if (!name || !clstab.cap) return NULL;
    pthread_mutex_lock(&rt_lock);
    uint32_t j = (uint32_t)strhash(name) & (clstab.cap - 1);
    Class r = NULL;
    while (clstab.keys[j]) {
        if (!strcmp(clstab.keys[j], name)) { r = clstab.vals[j]; break; }
        j = (j + 1) & (clstab.cap - 1);
    }
    pthread_mutex_unlock(&rt_lock);
    if (r && (rt_cls(r)->flags & RT_CLS_MISSING)) return NULL;
    return r;
}

/* --------------------------------------------------------- method tables */
static struct rt_method *mtab_get(struct rt_mtab *t, SEL s) {
    if (!t->cap) return NULL;
    uint32_t j = (uint32_t)ptrhash(s) & (t->cap - 1);
    while (t->slots[j]) {
        if (t->slots[j]->name == s) return t->slots[j];
        j = (j + 1) & (t->cap - 1);
    }
    return NULL;
}
static void mtab_put(struct rt_mtab *t, struct rt_method *m) {
    if ((t->count + 1) * 2 > t->cap) {
        uint32_t nc = t->cap ? t->cap * 2 : 16;
        struct rt_method **ns = rt_xcalloc(nc, sizeof *ns);
        for (uint32_t i = 0; i < t->cap; i++) if (t->slots[i]) {
            uint32_t j = (uint32_t)ptrhash(t->slots[i]->name) & (nc - 1);
            while (ns[j]) j = (j + 1) & (nc - 1);
            ns[j] = t->slots[i];
        }
        free(t->slots); t->slots = ns; t->cap = nc;
    }
    uint32_t j = (uint32_t)ptrhash(m->name) & (t->cap - 1);
    while (t->slots[j]) {
        if (t->slots[j]->name == m->name) { t->slots[j] = m; return; }
        j = (j + 1) & (t->cap - 1);
    }
    t->slots[j] = m; t->count++;
}

static void cache_clear(struct rt_cache *c) {
    if (c->cap) { memset(c->keys, 0, sizeof(SEL) * c->cap); }
    c->count = 0; c->gen = g_cache_gen;
}
static IMP cache_get(struct rt_cache *c, SEL s) {
    if (c->gen != g_cache_gen) { cache_clear(c); return NULL; }
    if (!c->cap) return NULL;
    uint32_t j = (uint32_t)ptrhash(s) & (c->cap - 1);
    while (c->keys[j]) {
        if (c->keys[j] == s) return c->vals[j];
        j = (j + 1) & (c->cap - 1);
    }
    return NULL;
}
static void cache_put(struct rt_cache *c, SEL s, IMP imp) {
    if ((c->count + 1) * 4 > c->cap * 3) {
        uint32_t nc = c->cap ? c->cap * 2 : 32;
        SEL *nk = rt_xcalloc(nc, sizeof *nk); IMP *nv = rt_xcalloc(nc, sizeof *nv);
        for (uint32_t i = 0; i < c->cap; i++) if (c->keys[i]) {
            uint32_t j = (uint32_t)ptrhash(c->keys[i]) & (nc - 1);
            while (nk[j]) j = (j + 1) & (nc - 1);
            nk[j] = c->keys[i]; nv[j] = c->vals[i];
        }
        free(c->keys); free(c->vals); c->keys = nk; c->vals = nv; c->cap = nc;
    }
    uint32_t j = (uint32_t)ptrhash(s) & (c->cap - 1);
    while (c->keys[j] && c->keys[j] != s) j = (j + 1) & (c->cap - 1);
    if (!c->keys[j]) c->count++;
    c->keys[j] = s; c->vals[j] = imp;
}

static void add_method_locked(struct rt_class *rc, SEL sel, IMP imp, const char *types) {
    struct rt_method *m = mtab_get(&rc->methods, sel);
    if (!m) {
        m = rt_xcalloc(1, sizeof *m);
        m->name = sel;
        if (rc->nm == rc->capm) { rc->capm = rc->capm ? rc->capm * 2 : 16; rc->mlist = realloc(rc->mlist, rc->capm * sizeof *rc->mlist); }
        rc->mlist[rc->nm++] = m;
    }
    m->imp = imp; m->types = types;
    mtab_put(&rc->methods, m);
    g_cache_gen++;
}
void rt_add_method(Class cls, SEL sel, IMP imp, const char *types) {
    pthread_mutex_lock(&rt_lock);
    add_method_locked(rt_cls(cls), sel, imp, types);
    pthread_mutex_unlock(&rt_lock);
}

/* ------------------------------------------------------- native classes */
static struct rt_class *new_rt(Class c, const char *name, uint32_t flags) {
    struct rt_class *rc = rt_xcalloc(1, sizeof *rc);
    rc->name = name; rc->cls = c; rc->flags = flags;
    c->cache = rc;
    return rc;
}

void rt_native_class(Class cls, Class meta, const char *name, Class super, size_t ivar_bytes) {
    pthread_mutex_lock(&rt_lock);
    cls->isa = meta;
    cls->superclass = super;
    if (super) {
        meta->isa = super->isa->isa;          /* root metaclass */
        meta->superclass = super->isa;
    } else {
        meta->isa = meta;
        meta->superclass = cls;
    }
    struct rt_class *rc = new_rt(cls, name, RT_CLS_NATIVE | RT_CLS_REALIZED | RT_CLS_INITIALIZED | (super ? 0 : RT_CLS_ROOT));
    rc->instance_size = (super ? rt_cls(super)->instance_size : 0) + ivar_bytes;
    struct rt_class *rm = new_rt(meta, name, RT_CLS_NATIVE | RT_CLS_REALIZED | RT_CLS_INITIALIZED | RT_CLS_META);
    rm->instance_size = sizeof(struct objc_class);
    cls_put(name, cls);
    pthread_mutex_unlock(&rt_lock);
}
void rt_native_method(Class cls, const char *sel, void *imp, const char *types) { rt_add_method(cls, rt_sel(sel), (IMP)imp, types); }
void rt_native_cmethod(Class cls, const char *sel, void *imp, const char *types) { rt_add_method(cls->isa, rt_sel(sel), (IMP)imp, types); }

RT_DECLARE_CLASS(NSObject);

Class rt_missing_class(const char *name) {
    Class existing = NULL;
    pthread_mutex_lock(&rt_lock);
    if (clstab.cap) {
        uint32_t j = (uint32_t)strhash(name) & (clstab.cap - 1);
        while (clstab.keys[j]) { if (!strcmp(clstab.keys[j], name)) { existing = clstab.vals[j]; break; } j = (j + 1) & (clstab.cap - 1); }
    }
    pthread_mutex_unlock(&rt_lock);
    if (existing) return existing;
    Class c = rt_xcalloc(1, sizeof *c), m = rt_xcalloc(1, sizeof *m);
    char *nm = rt_xstrdup(name);
    rt_native_class(c, m, nm, RT_CLASS(NSObject), 0);
    rt_cls(c)->flags |= RT_CLS_MISSING;
    rt_cls(m)->flags |= RT_CLS_MISSING;
    return c;
}

/* ------------------------------------------------------- guest classes */
#define RO_META 1
struct class_ro {
    uint32_t flags, instanceStart, instanceSize, reserved;
    const uint8_t *ivarLayout;
    const char *name;
    const void *baseMethods;
    const struct protocol_list *baseProtocols;
    const void *ivars;
    const uint8_t *weakIvarLayout;
    const void *baseProperties;
};
struct protocol_list { uintptr_t count; const struct protocol_t *list[]; };
struct protocol_t {
    void *isa; const char *name; const struct protocol_list *protocols;
    const void *instanceMethods, *classMethods, *optInstanceMethods, *optClassMethods, *instanceProperties;
    uint32_t size, flags;
};
struct category_t {
    const char *name; Class cls; const void *instanceMethods; const void *classMethods;
    const struct protocol_list *protocols; const void *instanceProperties;
};

extern char iosrt_objc_empty_cache[];
static int is_unrealized(Class c) {
    return c->cache == NULL || c->cache == (void *)iosrt_objc_empty_cache;
}

static void add_method_list(struct rt_class *rc, const void *ml) {
    if (!ml) return;
    const uint8_t *p = ml;
    uint32_t ef, count;
    memcpy(&ef, p, 4); memcpy(&count, p + 4, 4);
    uint32_t entsize = ef & 0xfffc;
    int relative = (ef & 0x80000000u) != 0;
    const uint8_t *e = p + 8;
    for (uint32_t i = 0; i < count; i++, e += entsize) {
        SEL sel; const char *types; IMP imp;
        if (relative) {
            int32_t no, to, io;
            memcpy(&no, e, 4); memcpy(&to, e + 4, 4); memcpy(&io, e + 8, 4);
            const char *const *selref = (const char *const *)(e + no);
            sel = rt_sel(*selref);
            types = (const char *)(e + 4 + to);
            imp = (IMP)(uintptr_t)(e + 8 + io);
        } else {
            const char *n; memcpy(&n, e, 8); memcpy(&types, e + 8, 8); memcpy(&imp, e + 16, 8);
            sel = rt_sel(n);
            memcpy((void *)e, &sel, 8);   /* uniquify in place, like the real runtime */
        }
        add_method_locked(rc, sel, imp, types);
    }
}

static void add_protocols(struct rt_class *rc, const struct protocol_list *pl) {
    if (!pl) return;
    for (uintptr_t i = 0; i < pl->count; i++) {
        if (rc->nprotocols == rc->capprotocols) {
            rc->capprotocols = rc->capprotocols ? rc->capprotocols * 2 : 4;
            rc->protocols = realloc(rc->protocols, rc->capprotocols * sizeof(void *));
        }
        rc->protocols[rc->nprotocols++] = pl->list[i];
    }
}

static void realize_class_locked(Class c);

static void realize_one(Class c, Class nonmeta_super_meta_hint) {
    (void)nonmeta_super_meta_hint;
    const struct class_ro *ro = (const struct class_ro *)(c->bits & 0x00007ffffffffff8ULL);
    int meta = ro && (ro->flags & RO_META);
    struct rt_class *rc = new_rt(c, ro ? ro->name : "?", RT_CLS_GUEST | RT_CLS_REALIZED | (meta ? RT_CLS_META : 0) | (c->superclass ? 0 : RT_CLS_ROOT));
    rc->ro = ro;
    if (!ro) return;
    size_t start = ro->instanceStart, size = ro->instanceSize;
    if (!meta && c->superclass) {
        struct rt_class *sup = rt_cls(c->superclass);
        if (sup->flags & RT_CLS_MISSING) {
            if (sup->instance_size < start) sup->instance_size = start;   /* learn the hidden size */
        } else if (sup->instance_size > start) {
            size_t diff = (sup->instance_size - start + 7) & ~(size_t)7;
            if (ro->ivars) {
                const uint8_t *il = ro->ivars; uint32_t ent, cnt;
                memcpy(&ent, il, 4); memcpy(&cnt, il + 4, 4);
                for (uint32_t i = 0; i < cnt; i++) {
                    int32_t *off; memcpy(&off, il + 8 + (size_t)i * ent, 8);
                    if (off) *off += (int32_t)diff;
                }
            }
            size += diff;
            rt_log("slid ivars of %s by %zu bytes", ro->name, diff);
        }
    }
    rc->instance_size = meta ? sizeof(struct objc_class) : size;
    add_method_list(rc, ro->baseMethods);
    add_protocols(rc, ro->baseProtocols);
    if (!meta && ro->ivars) {
        const uint8_t *il = ro->ivars; uint32_t ent, cnt;
        memcpy(&ent, il, 4); memcpy(&cnt, il + 4, 4);
        rc->ivars = rt_xcalloc(cnt, sizeof *rc->ivars); rc->nivars = cnt;
        for (uint32_t i = 0; i < cnt; i++) {
            const uint8_t *iv = il + 8 + (size_t)i * ent;
            int32_t *off; const char *n, *t; uint32_t sz;
            memcpy(&off, iv, 8); memcpy(&n, iv + 8, 8); memcpy(&t, iv + 16, 8); memcpy(&sz, iv + 28, 4);
            rc->ivars[i].name = n; rc->ivars[i].type = t; rc->ivars[i].offset = off ? *off : 0; rc->ivars[i].size = sz;
        }
    }
    if (!meta && ro->baseProperties) {
        const uint8_t *pl = ro->baseProperties; uint32_t ent, cnt;
        memcpy(&ent, pl, 4); memcpy(&cnt, pl + 4, 4);
        rc->props = rt_xcalloc(cnt ? cnt : 1, sizeof *rc->props); rc->nprops = cnt;
        for (uint32_t i = 0; i < cnt; i++) {
            memcpy(&rc->props[i].name, pl + 8 + (size_t)i * ent, 8);
            memcpy(&rc->props[i].attrs, pl + 16 + (size_t)i * ent, 8);
        }
    }
    if (!meta) cls_put(ro->name, c);
}

static void realize_class_locked(Class c) {
    if (!c || !is_unrealized(c)) return;
    if (c->superclass) realize_class_locked(c->superclass);
    Class meta = c->isa;
    /* realize metaclass chain first so class methods are available */
    if (meta && is_unrealized(meta)) {
        if (meta->superclass) realize_class_locked(meta->superclass);
        realize_one(meta, NULL);
    }
    realize_one(c, NULL);
}
void rt_register_class(Class c) { pthread_mutex_lock(&rt_lock); realize_class_locked(c); pthread_mutex_unlock(&rt_lock); }

int rt_class_is_meta(Class c) { return c && (rt_cls(c)->flags & RT_CLS_META); }
const char *rt_class_name(Class c) { return c ? rt_cls(c)->name : "nil"; }

/* the non-meta class for a metaclass (search registry; metaclasses are few) */
static Class nonmeta_of(Class meta) {
    for (uint32_t i = 0; i < clstab.cap; i++) if (clstab.keys[i] && clstab.vals[i]->isa == meta) return clstab.vals[i];
    return NULL;
}

/* ----------------------------------------------------------- protocols */
static struct { const char **names; const struct protocol_t **protos; size_t n, cap; } prototab;
static const struct protocol_t *canonical_protocol(const struct protocol_t *p) {
    for (size_t i = 0; i < prototab.n; i++) if (!strcmp(prototab.names[i], p->name)) return prototab.protos[i];
    if (prototab.n == prototab.cap) {
        prototab.cap = prototab.cap ? prototab.cap * 2 : 32;
        prototab.names = realloc(prototab.names, prototab.cap * sizeof(char *));
        prototab.protos = realloc(prototab.protos, prototab.cap * sizeof(void *));
    }
    prototab.names[prototab.n] = p->name; prototab.protos[prototab.n++] = p;
    return p;
}
const void *rt_protocol_named(const char *name) {
    for (size_t i = 0; i < prototab.n; i++) if (!strcmp(prototab.names[i], name)) return prototab.protos[i];
    return NULL;
}
static int proto_conforms(const struct protocol_t *p, const struct protocol_t *want) {
    if (!p) return 0;
    if (p == want || !strcmp(p->name, want->name)) return 1;
    if (p->protocols) for (uintptr_t i = 0; i < p->protocols->count; i++) if (proto_conforms(p->protocols->list[i], want)) return 1;
    return 0;
}
int rt_class_conforms(Class c, const void *proto) {
    for (; c; c = c->superclass) {
        struct rt_class *rc = rt_cls(c);
        for (size_t i = 0; i < rc->nprotocols; i++) if (proto_conforms(rc->protocols[i], proto)) return 1;
    }
    return 0;
}
const char *rt_protocol_name(const void *p) { return p ? ((const struct protocol_t *)p)->name : "nil"; }

/* ------------------------------------------------------------ images */
static void call_load(Class c, IMP imp) {
    rt_log("+[%s load]", rt_cls(c)->name);
    ((void (*)(Class, SEL))imp)(c, rt_sel("load"));
}

int rt_objc_map_image(struct rt_image *img) {
    struct rt_section *s;
    unsigned nsel = 0, ncls = 0, ncat = 0;
    pthread_mutex_lock(&rt_lock);
    /* selector references */
    for (int i = 0; i < img->nsects; i++) {
        struct rt_section *sc = &img->sects[i];
        if (strcmp(sc->sectname, "__objc_selrefs")) continue;
        const char **refs = (const char **)(uintptr_t)sc->addr;
        for (uint64_t k = 0; k < sc->size / 8; k++) { refs[k] = rt_sel(refs[k]); nsel++; }
    }
    /* protocols */
    if ((s = rt_image_section(img, NULL, "__objc_protolist"))) {
        const struct protocol_t **pl = (const struct protocol_t **)(uintptr_t)s->addr;
        for (uint64_t k = 0; k < s->size / 8; k++) canonical_protocol(pl[k]);
    }
    if ((s = rt_image_section(img, NULL, "__objc_protorefs"))) {
        const struct protocol_t **pr = (const struct protocol_t **)(uintptr_t)s->addr;
        for (uint64_t k = 0; k < s->size / 8; k++) pr[k] = canonical_protocol(pr[k]);
    }
    /* classes */
    if ((s = rt_image_section(img, NULL, "__objc_classlist"))) {
        Class *cl = (Class *)(uintptr_t)s->addr;
        for (uint64_t k = 0; k < s->size / 8; k++) { realize_class_locked(cl[k]); ncls++; }
    }
    /* categories */
    for (int pass = 0; pass < 2; pass++) {
        s = rt_image_section(img, NULL, pass ? "__objc_catlist2" : "__objc_catlist");
        if (!s) continue;
        const struct category_t **cats = (const struct category_t **)(uintptr_t)s->addr;
        for (uint64_t k = 0; k < s->size / 8; k++) {
            const struct category_t *cat = cats[k];
            if (!cat->cls) { rt_warn("category %s on missing class skipped", cat->name); continue; }
            realize_class_locked(cat->cls);
            struct rt_class *rc = rt_cls(cat->cls), *rm = rt_cls(cat->cls->isa);
            add_method_list(rc, cat->instanceMethods);
            add_method_list(rm, cat->classMethods);
            add_protocols(rc, cat->protocols);
            ncat++;
        }
    }
    pthread_mutex_unlock(&rt_lock);
    rt_log("objc: %u selrefs, %u classes, %u categories", nsel, ncls, ncat);
    return 0;
}

static void load_class(Class c) {
    struct rt_class *rc = rt_cls(c);
    if (rc->flags & RT_CLS_LOADED) return;
    rc->flags |= RT_CLS_LOADED;
    if (c->superclass && (rt_cls(c->superclass)->flags & RT_CLS_GUEST)) load_class(c->superclass);
    const struct class_ro *mro = rt_cls(c->isa)->ro;
    if (!mro || !mro->baseMethods) return;
    /* +load is looked up in the class's own method list only */
    const uint8_t *p = mro->baseMethods; uint32_t ef, cnt; memcpy(&ef, p, 4); memcpy(&cnt, p + 4, 4);
    uint32_t ent = ef & 0xfffc; int rel = (ef & 0x80000000u) != 0;
    SEL load = rt_sel("load");
    for (uint32_t i = 0; i < cnt; i++) {
        const uint8_t *e = p + 8 + (size_t)i * ent;
        SEL n; IMP imp;
        if (rel) { int32_t no, io; memcpy(&no, e, 4); memcpy(&io, e + 8, 4); n = rt_sel(*(const char *const *)(e + no)); imp = (IMP)(uintptr_t)(e + 8 + io); }
        else { memcpy(&n, e, 8); memcpy(&imp, e + 16, 8); n = rt_sel(n); }
        if (n == load) { call_load(c, imp); return; }
    }
}

void rt_objc_load_image(struct rt_image *img) {
    struct rt_section *s = rt_image_section(img, NULL, "__objc_nlclslist");
    if (s) {
        Class *cl = (Class *)(uintptr_t)s->addr;
        for (uint64_t k = 0; k < s->size / 8; k++) load_class(cl[k]);
    }
    s = rt_image_section(img, NULL, "__objc_nlcatlist");
    if (s) {
        const struct category_t **cats = (const struct category_t **)(uintptr_t)s->addr;
        SEL load = rt_sel("load");
        for (uint64_t k = 0; k < s->size / 8; k++) {
            const struct category_t *cat = cats[k];
            const uint8_t *p = cat->classMethods; if (!p || !cat->cls) continue;
            uint32_t ef, cnt; memcpy(&ef, p, 4); memcpy(&cnt, p + 4, 4);
            uint32_t ent = ef & 0xfffc; int rel = (ef & 0x80000000u) != 0;
            for (uint32_t i = 0; i < cnt; i++) {
                const uint8_t *e = p + 8 + (size_t)i * ent;
                SEL n; IMP imp;
                if (rel) { int32_t no, io; memcpy(&no, e, 4); memcpy(&io, e + 8, 4); n = rt_sel(*(const char *const *)(e + no)); imp = (IMP)(uintptr_t)(e + 8 + io); }
                else { memcpy(&n, e, 8); memcpy(&imp, e + 16, 8); }
                if (n == load) call_load(cat->cls, imp);
            }
        }
    }
}

/* ------------------------------------------------------------- lookup */
IMP rt_lookup(Class cls, SEL sel, int *found) {
    pthread_mutex_lock(&rt_lock);
    if (is_unrealized(cls)) realize_class_locked(cls);
    for (Class c = cls; c; c = c->superclass) {
        if (is_unrealized(c)) realize_class_locked(c);
        struct rt_class *rc = rt_cls(c);
        struct rt_method *m = mtab_get(&rc->methods, sel);
        if (m) { pthread_mutex_unlock(&rt_lock); if (found) *found = 1; return m->imp; }
        if ((rc->flags & RT_CLS_MISSING) && !rc->warned_missing && !(rc->flags & RT_CLS_META)) {
            rc->warned_missing = 1;
            rt_warn("warning: class %s is not implemented by iosrt (first needed for -%s); falling back to NSObject", rc->name, sel);
        }
    }
    pthread_mutex_unlock(&rt_lock);
    if (found) *found = 0;
    return NULL;
}

static void initialize_class(Class cls) {
    struct rt_class *rc = rt_cls(cls);
    if (rc->flags & RT_CLS_INITIALIZED) return;
    pthread_t self = pthread_self();
    for (;;) {
        pthread_mutex_lock(&rt_lock);
        if (rc->flags & RT_CLS_INITIALIZED) { pthread_mutex_unlock(&rt_lock); return; }
        if (rc->flags & RT_CLS_INITIALIZING) {
            int mine = pthread_equal(rc->init_thread, self);
            pthread_mutex_unlock(&rt_lock);
            if (mine) return;
            sched_yield();
            continue;
        }
        rc->flags |= RT_CLS_INITIALIZING;
        rc->init_thread = self;
        pthread_mutex_unlock(&rt_lock);
        break;
    }
    if (cls->superclass) initialize_class(cls->superclass);
    int found = 0;
    IMP imp = rt_lookup(cls->isa, rt_sel("initialize"), &found);
    if (found && imp) {
        rt_log("+[%s initialize]", rc->name);
        ((void (*)(Class, SEL))imp)(cls, rt_sel("initialize"));
    }
    pthread_mutex_lock(&rt_lock);
    rc->flags = (rc->flags & ~RT_CLS_INITIALIZING) | RT_CLS_INITIALIZED;
    rt_cls(cls->isa)->flags |= RT_CLS_INITIALIZED;
    pthread_mutex_unlock(&rt_lock);
}

/* lenient mode: an unknown initializer keeps the object (returns self), anything else returns nil/0 */
static id self_imp(id self, SEL sel) { (void)sel; return self; }
static IMP lenient_imp(SEL sel) { extern char iosrt_nil_imp[]; return strncmp(sel, "init", 4) ? (IMP)iosrt_nil_imp : (IMP)self_imp; }
static id unrecognized_imp(id self, SEL sel) {
    Class c = self->isa;
    char msg[512];
    int meta = rt_class_is_meta(c);
    snprintf(msg, sizeof msg, "%c[%s %s]: unrecognized selector sent to %s %p", meta ? '+' : '-',
             rt_cls(c)->name, sel, meta ? "class" : "instance", (void *)self);
    rt_guest_exit(71, msg);
}

static IMP lookup_send(Class cls, id self, SEL sel, id *self_slot) {
    struct rt_class *rc = rt_cls(cls);
    if (is_unrealized(cls)) { rt_register_class(cls); rc = rt_cls(cls); }
    if (!(rc->flags & RT_CLS_INITIALIZED)) {
        Class target = (rc->flags & RT_CLS_META) ? nonmeta_of(cls) : cls;
        if (target) initialize_class(target);
    }
    pthread_mutex_lock(&rt_lock);
    IMP imp = cache_get(&rc->cache, sel);
    pthread_mutex_unlock(&rt_lock);
    if (imp) return imp;
    int found;
    imp = rt_lookup(cls, sel, &found);
    if (found) {
        pthread_mutex_lock(&rt_lock); cache_put(&rc->cache, sel, imp); pthread_mutex_unlock(&rt_lock);
        return imp;
    }
    /* dynamic resolution */
    int meta = (rc->flags & RT_CLS_META) != 0;
    SEL rs = rt_sel(meta ? "resolveClassMethod:" : "resolveInstanceMethod:");
    int rf; IMP rimp = rt_lookup(meta ? cls : cls->isa, rs, &rf);
    if (rf && rimp && sel != rs) {
        Class target = meta ? nonmeta_of(cls) : cls;
        if (target && ((long (*)(Class, SEL, SEL))rimp)(target, rs, sel) & 0xff) {
            imp = rt_lookup(cls, sel, &found);
            if (found) return imp;
        }
    }
    /* fast forwarding */
    SEL ft = rt_sel("forwardingTargetForSelector:");
    int ff; IMP fimp = rt_lookup(cls, ft, &ff);
    if (ff && fimp && sel != ft && self_slot) {
        id other = ((id (*)(id, SEL, SEL))fimp)(self, ft, sel);
        if (other && other != self) {
            *self_slot = other;
            return rt_lookup_for_send(other, sel);
        }
    }
    /* full forwarding: the class overrides -forwardInvocation: */
    {
        extern char iosrt_msgForward[];
        extern IMP rt_default_forward_imp;
        int fi; IMP fwd = rt_lookup(meta ? cls : cls, rt_sel("forwardInvocation:"), &fi);
        if (fi && fwd && fwd != rt_default_forward_imp && !meta) return (IMP)iosrt_msgForward;
    }
    {
        int rt_is_strict(void);
        int rt_first_time(const char *key);
        extern char iosrt_nil_imp[];
        if (!rt_is_strict()) {
            char key[512];
            snprintf(key, sizeof key, "%c[%s %s]", meta ? '+' : '-', rc->name, sel);
            if (rt_first_time(key)) rt_warn("missing: %s unrecognized selector (returns %s)", key, strncmp(sel, "init", 4) ? "nil" : "self");
            return lenient_imp(sel);
        }
    }
    return (IMP)unrecognized_imp;
}

IMP rt_lookup_for_send(id self, SEL sel) {
    return lookup_send(self->isa, self, sel, NULL);
}
/* called from objc_msgSend asm with x2 = pointer to the saved receiver */
IMP rt_lookup_for_send_slot(id self, SEL sel, id *slot) { return lookup_send(self->isa, self, sel, slot); }

struct objc_super { id receiver; Class cls; };
static IMP super_missing(struct objc_super *sup, Class start, SEL sel) {
    int rt_is_strict(void); int rt_first_time(const char *key);
    if (rt_is_strict()) return (IMP)unrecognized_imp;
    char key[512];
    snprintf(key, sizeof key, "super -[%s %s]", start ? rt_cls(start)->name : rt_cls(sup->cls)->name, sel);
    if (rt_first_time(key)) rt_warn("missing: %s unrecognized selector (returns %s)", key, strncmp(sel, "init", 4) ? "nil" : "self");
    return lenient_imp(sel);
}
IMP rt_lookup_super2(struct objc_super *sup, SEL sel) {
    Class start = sup->cls->superclass;
    if (!start) return super_missing(sup, NULL, sel);
    int found; IMP imp = rt_lookup(start, sel, &found);
    return found ? imp : super_missing(sup, start, sel);
}
IMP rt_lookup_super(struct objc_super *sup, SEL sel) {
    int found; IMP imp = rt_lookup(sup->cls, sel, &found);
    return found ? imp : super_missing(sup, sup->cls, sel);
}

rt_bool rt_responds(id obj, SEL sel) {
    if (!obj) return 0;
    int found; rt_lookup(obj->isa, sel, &found);
    return found;
}
Class rt_object_class(id obj) { return obj ? obj->isa : NULL; }
int rt_is_kind_of(id obj, Class cls) {
    if (!obj) return 0;
    for (Class c = obj->isa; c; c = c->superclass) if (c == cls) return 1;
    return 0;
}

/* ---------------------------------------------------------- public API */
struct rt_method *rt_find_method(Class cls, SEL sel) {
    pthread_mutex_lock(&rt_lock);
    struct rt_method *r = NULL;
    for (Class c = cls; c && !r; c = c->superclass) r = mtab_get(&rt_cls(c)->methods, sel);
    pthread_mutex_unlock(&rt_lock);
    return r;
}
void rt_method_set_imp(struct rt_method *m, IMP imp) {
    pthread_mutex_lock(&rt_lock); m->imp = imp; g_cache_gen++; pthread_mutex_unlock(&rt_lock);
}
void rt_method_exchange(struct rt_method *a, struct rt_method *b) {
    pthread_mutex_lock(&rt_lock); IMP t = a->imp; a->imp = b->imp; b->imp = t; g_cache_gen++; pthread_mutex_unlock(&rt_lock);
}
int rt_class_has_own_method(Class cls, SEL sel) {
    pthread_mutex_lock(&rt_lock);
    int r = mtab_get(&rt_cls(cls)->methods, sel) != NULL;
    pthread_mutex_unlock(&rt_lock);
    return r;
}
Class rt_allocate_class_pair(Class super, const char *name, size_t extra) {
    if (rt_class_named(name)) return NULL;
    Class c = rt_xcalloc(1, sizeof *c + extra), m = rt_xcalloc(1, sizeof *m);
    rt_native_class(c, m, rt_xstrdup(name), super, 0);
    rt_cls(c)->flags = (rt_cls(c)->flags & ~(uint32_t)RT_CLS_NATIVE) | RT_CLS_GUEST;
    return c;
}
int rt_class_add_ivar(Class cls, const char *name, size_t size, uint8_t align_log2, const char *type) {
    struct rt_class *rc = rt_cls(cls);
    size_t al = (size_t)1 << align_log2;
    size_t off = (rc->instance_size + al - 1) & ~(al - 1);
    rc->ivars = realloc(rc->ivars, (rc->nivars + 1) * sizeof *rc->ivars);
    rc->ivars[rc->nivars].name = rt_xstrdup(name);
    rc->ivars[rc->nivars].type = type ? rt_xstrdup(type) : "";
    rc->ivars[rc->nivars].offset = (long)off;
    rc->ivars[rc->nivars].size = (uint32_t)size;
    rc->nivars++;
    rc->instance_size = off + size;
    return 1;
}

void rt_objc_init(void) {
    pthread_mutexattr_t a; pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&rt_lock, &a);
}

Class rt_find_class_with_suffix(const char *suffix) {
    size_t sl = strlen(suffix);
    Class best = NULL;
    pthread_mutex_lock(&rt_lock);
    for (uint32_t i = 0; i < clstab.cap; i++) {
        const char *k = clstab.keys[i];
        if (!k) continue;
        size_t kl = strlen(k);
        if (kl >= sl && !strcmp(k + kl - sl, suffix) && (rt_cls(clstab.vals[i])->flags & RT_CLS_GUEST)) { best = clstab.vals[i]; break; }
    }
    pthread_mutex_unlock(&rt_lock);
    return best;
}

/* Map a code address inside a guest image to the closest preceding ObjC method
 * implementation: "-[Class selector]+offset". Used for crash reports of stripped apps. */
const char *rt_objc_symbolize(uint64_t addr, uint64_t *off, char *buf, size_t n) {
    uint64_t best = 0; const char *bc = NULL; SEL bs = NULL; int bmeta = 0;
    for (uint32_t i = 0; i < clstab.cap; i++) {
        if (!clstab.keys[i]) continue;
        for (int m = 0; m < 2; m++) {
            Class c = m ? clstab.vals[i]->isa : clstab.vals[i];
            struct rt_class *rc = rt_cls(c);
            if (!rc || !(rc->flags & RT_CLS_GUEST)) continue;
            for (size_t k = 0; k < rc->nm; k++) {
                uint64_t a = (uint64_t)(uintptr_t)rc->mlist[k]->imp;
                if (a <= addr && a > best) { best = a; bc = rc->name; bs = rc->mlist[k]->name; bmeta = m; }
            }
        }
    }
    if (!bc || addr - best > 0x4000) return NULL;
    snprintf(buf, n, "%c[%s %s]", bmeta ? '+' : '-', bc, bs);
    if (off) *off = addr - best;
    return buf;
}
