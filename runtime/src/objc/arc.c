/*
 * Reference counting, autorelease pools, ARC entry points, weak references,
 * associated objects, @synchronized, blocks runtime and the public
 * objc_* / class_* / object_* C API.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>

#define OBJ_MAGIC 0x6f626a5f72747e21ULL
struct obj_header { uint64_t magic; int64_t refcnt; };

RT_DECLARE_CLASS(NSObject);
RT_DEFINE_CLASS(NSBlock);
RT_DEFINE_CLASS(__NSStackBlock);
RT_DEFINE_CLASS(__NSMallocBlock);
RT_DEFINE_CLASS(__NSGlobalBlock);

static struct obj_header *hdr(id o) {
    if (!o || rt_image_contains((uint64_t)(uintptr_t)o)) return NULL;
    if (rt_cls(o->isa)->flags & RT_CLS_META) return NULL;          /* class objects */
    struct obj_header *h = (struct obj_header *)((uint8_t *)o - sizeof(struct obj_header));
    return h->magic == OBJ_MAGIC ? h : NULL;
}

int rt_class_has_own_method(Class cls, SEL sel);
struct rt_method *rt_find_method(Class cls, SEL sel);
/* ARC/C++ ivar construction & destruction: .cxx_construct runs root-first,
 * .cxx_destruct runs leaf-first, each only for classes that define it. */
static void call_cxx_construct(id o, Class c) {
    if (!c) return;
    call_cxx_construct(o, c->superclass);
    SEL s = rt_sel(".cxx_construct");
    if ((rt_cls(c)->flags & RT_CLS_GUEST) && rt_class_has_own_method(c, s)) ((void (*)(id, SEL))rt_find_method(c, s)->imp)(o, s);
}
static void call_cxx_destruct(id o) {
    SEL s = rt_sel(".cxx_destruct");
    for (Class c = o->isa; c; c = c->superclass)
        if ((rt_cls(c)->flags & RT_CLS_GUEST) && rt_class_has_own_method(c, s)) ((void (*)(id, SEL))rt_find_method(c, s)->imp)(o, s);
}
id rt_alloc_instance(Class cls, size_t extra) {
    size_t sz = rt_cls(cls)->instance_size + extra;
    if (sz < sizeof(void *)) sz = sizeof(void *);
    struct obj_header *h = rt_xcalloc(1, sizeof *h + sz);
    h->magic = OBJ_MAGIC; h->refcnt = 1;
    id o = (id)(h + 1);
    o->isa = cls;
    if (rt_cls(cls)->flags & RT_CLS_GUEST) call_cxx_construct(o, cls);
    return o;
}

static void weak_clear(id obj);
static void assoc_clear(id obj);

void rt_dealloc_instance(id o) {
    struct obj_header *h = hdr(o);
    if (!h) return;
    call_cxx_destruct(o);
    weak_clear(o);
    assoc_clear(o);
    h->magic = 0;
    free(h);
}

int rt_is_block(id o) {
    if (!o) return 0;
    Class c = o->isa;
    return c == RT_CLASS(__NSStackBlock) || c == RT_CLASS(__NSMallocBlock) || c == RT_CLASS(__NSGlobalBlock);
}

id objc_retain(id o) {
    if (!o) return o;
    if (rt_is_block(o)) {
        struct rt_block *b = (struct rt_block *)o;
        if (b->flags & RT_BLOCK_NEEDS_FREE) __atomic_add_fetch(&b->flags, 2, __ATOMIC_RELAXED);
        return o;
    }
    struct obj_header *h = hdr(o);
    if (h) __atomic_add_fetch(&h->refcnt, 1, __ATOMIC_RELAXED);
    return o;
}
void objc_release(id o) {
    if (!o) return;
    if (rt_is_block(o)) { _Block_release(o); return; }
    struct obj_header *h = hdr(o);
    if (!h) return;
    if (__atomic_sub_fetch(&h->refcnt, 1, __ATOMIC_ACQ_REL) == 0) {
        ((void (*)(id, SEL))objc_msgSend)(o, rt_sel("dealloc"));
    }
}
long rt_retain_count(id o) { struct obj_header *h = hdr(o); return h ? (long)h->refcnt : (long)0x7fffffffffffffffL; }

/* ------------------------------------------------------------ pools */
struct pool { id *objs; size_t n, cap; };
static __thread struct pool *tpool;
#define POOL_MARK ((id)(uintptr_t)1)
void *objc_autoreleasePoolPush(void) {
    if (!tpool) tpool = rt_xcalloc(1, sizeof *tpool);
    size_t tok = tpool->n;
    if (tpool->n == tpool->cap) { tpool->cap = tpool->cap ? tpool->cap * 2 : 256; tpool->objs = realloc(tpool->objs, tpool->cap * sizeof(id)); }
    tpool->objs[tpool->n++] = POOL_MARK;
    return (void *)(uintptr_t)(tok + 1);
}
void objc_autoreleasePoolPop(void *token) {
    if (!tpool) return;
    size_t tok = (size_t)(uintptr_t)token - 1;
    while (tpool->n > tok) {
        id o = tpool->objs[--tpool->n];
        if (o != POOL_MARK) objc_release(o);
    }
}
id objc_autorelease(id o) {
    if (!o) return o;
    if (!tpool) tpool = rt_xcalloc(1, sizeof *tpool);
    if (tpool->n == tpool->cap) { tpool->cap = tpool->cap ? tpool->cap * 2 : 256; tpool->objs = realloc(tpool->objs, tpool->cap * sizeof(id)); }
    tpool->objs[tpool->n++] = o;
    return o;
}
id rt_autoreleased(id o) { return objc_autorelease(o); }

id objc_autoreleaseReturnValue(id o) { return objc_autorelease(o); }
id objc_retainAutoreleaseReturnValue(id o) { return objc_autorelease(objc_retain(o)); }
id objc_retainAutoreleasedReturnValue(id o) { return objc_retain(o); }
id objc_unsafeClaimAutoreleasedReturnValue(id o) { return o; }
id objc_claimAutoreleasedReturnValue(id o) { return objc_retain(o); }
id objc_retainAutorelease(id o) { return objc_autorelease(objc_retain(o)); }
id objc_retainBlock(id b) { return (id)_Block_copy(b); }
void objc_storeStrong(id *loc, id obj) {
    id old = *loc;
    objc_retain(obj);
    *loc = obj;
    objc_release(old);
}

/* ------------------------------------------------------- weak refs */
static pthread_mutex_t weak_lock = PTHREAD_MUTEX_INITIALIZER;
struct weak_entry { id obj; id **slots; size_t n, cap; };
static struct weak_entry *weaks; static size_t nweaks, capweaks;
static struct weak_entry *weak_find(id o, int create) {
    for (size_t i = 0; i < nweaks; i++) if (weaks[i].obj == o) return &weaks[i];
    if (!create) return NULL;
    if (nweaks == capweaks) { capweaks = capweaks ? capweaks * 2 : 32; weaks = realloc(weaks, capweaks * sizeof *weaks); }
    memset(&weaks[nweaks], 0, sizeof *weaks);
    weaks[nweaks].obj = o;
    return &weaks[nweaks++];
}
static void weak_unregister(id o, id *slot) {
    struct weak_entry *e = weak_find(o, 0);
    if (!e) return;
    for (size_t i = 0; i < e->n; i++) if (e->slots[i] == slot) { e->slots[i] = e->slots[--e->n]; break; }
}
static void weak_clear(id o) {
    pthread_mutex_lock(&weak_lock);
    struct weak_entry *e = weak_find(o, 0);
    if (e) {
        for (size_t i = 0; i < e->n; i++) *e->slots[i] = NULL;
        free(e->slots);
        *e = weaks[--nweaks];
    }
    pthread_mutex_unlock(&weak_lock);
}
id objc_storeWeak(id *loc, id obj) {
    pthread_mutex_lock(&weak_lock);
    if (*loc) weak_unregister(*loc, loc);
    struct obj_header *h = hdr(obj);
    if (obj && h && h->refcnt <= 0) obj = NULL;           /* deallocating */
    *loc = obj;
    if (obj && h) {
        struct weak_entry *e = weak_find(obj, 1);
        if (e->n == e->cap) { e->cap = e->cap ? e->cap * 2 : 4; e->slots = realloc(e->slots, e->cap * sizeof(id *)); }
        e->slots[e->n++] = loc;
    }
    pthread_mutex_unlock(&weak_lock);
    return obj;
}
id objc_initWeak(id *loc, id obj) { *loc = NULL; return objc_storeWeak(loc, obj); }
void objc_destroyWeak(id *loc) { objc_storeWeak(loc, NULL); }
id objc_loadWeakRetained(id *loc) {
    pthread_mutex_lock(&weak_lock);
    id o = *loc;
    struct obj_header *h = hdr(o);
    if (o && h && h->refcnt <= 0) o = NULL;
    else objc_retain(o);
    pthread_mutex_unlock(&weak_lock);
    return o;
}
id objc_loadWeak(id *loc) { return objc_autorelease(objc_loadWeakRetained(loc)); }
void objc_copyWeak(id *to, id *from) { id o = objc_loadWeakRetained(from); objc_initWeak(to, o); objc_release(o); }
void objc_moveWeak(id *to, id *from) { objc_copyWeak(to, from); objc_destroyWeak(from); }

/* -------------------------------------------------- associated objects */
static pthread_mutex_t assoc_lock = PTHREAD_MUTEX_INITIALIZER;
struct assoc { id obj; const void *key; id val; unsigned long policy; };
static struct assoc *assocs; static size_t nassoc, capassoc;
void objc_setAssociatedObject(id obj, const void *key, id val, unsigned long policy) {
    int retain = policy == 1 || policy == 01401, copy = policy == 3 || policy == 01403;
    if (val && copy) val = RT_SEND0(id, val, "copy");
    else if (val && retain) objc_retain(val);
    pthread_mutex_lock(&assoc_lock);
    id old = NULL; unsigned long oldp = 0; int found = 0;
    for (size_t i = 0; i < nassoc; i++) if (assocs[i].obj == obj && assocs[i].key == key) {
        old = assocs[i].val; oldp = assocs[i].policy; found = 1;
        if (val) { assocs[i].val = val; assocs[i].policy = policy; }
        else assocs[i] = assocs[--nassoc];
        break;
    }
    if (!found && val) {
        if (nassoc == capassoc) { capassoc = capassoc ? capassoc * 2 : 32; assocs = realloc(assocs, capassoc * sizeof *assocs); }
        assocs[nassoc++] = (struct assoc){ obj, key, val, policy };
    }
    pthread_mutex_unlock(&assoc_lock);
    if (old && oldp != 0) objc_release(old);
}
id objc_getAssociatedObject(id obj, const void *key) {
    pthread_mutex_lock(&assoc_lock);
    id r = NULL;
    for (size_t i = 0; i < nassoc; i++) if (assocs[i].obj == obj && assocs[i].key == key) { r = assocs[i].val; break; }
    pthread_mutex_unlock(&assoc_lock);
    return r;
}
static void assoc_clear(id obj) {
    for (;;) {
        pthread_mutex_lock(&assoc_lock);
        id v = NULL; unsigned long p = 0; int hit = 0;
        for (size_t i = 0; i < nassoc; i++) if (assocs[i].obj == obj) { v = assocs[i].val; p = assocs[i].policy; assocs[i] = assocs[--nassoc]; hit = 1; break; }
        pthread_mutex_unlock(&assoc_lock);
        if (!hit) return;
        if (p) objc_release(v);
    }
}
void objc_removeAssociatedObjects(id obj) { assoc_clear(obj); }

/* ------------------------------------------------------- @synchronized */
static pthread_mutex_t sync_locks[64];
static int sync_init_done;
static pthread_mutex_t *sync_for(id o) { return &sync_locks[((uintptr_t)o >> 4) & 63]; }
int objc_sync_enter(id o) { if (o) pthread_mutex_lock(sync_for(o)); return 0; }
int objc_sync_exit(id o) { if (o) pthread_mutex_unlock(sync_for(o)); return 0; }

/* ------------------------------------------------------------- blocks */
#define BLOCK_FIELD_IS_OBJECT 3
#define BLOCK_FIELD_IS_BLOCK 7
#define BLOCK_FIELD_IS_BYREF 8
#define BLOCK_FIELD_IS_WEAK 16
#define BLOCK_BYREF_CALLER 128
#define BYREF_HAS_COPY_DISPOSE (1 << 25)
#define BYREF_NEEDS_FREE (1 << 24)
#define BYREF_LAYOUT_EXTENDED (1 << 28)
struct byref { void *isa; struct byref *forwarding; int flags; int size; void (*keep)(struct byref *, struct byref *); void (*destroy)(struct byref *); };

void *_Block_copy(const void *arg) {
    struct rt_block *b = (struct rt_block *)arg;
    if (!b) return NULL;
    if (b->flags & RT_BLOCK_NEEDS_FREE) { __atomic_add_fetch(&b->flags, 2, __ATOMIC_RELAXED); return b; }
    if (b->flags & RT_BLOCK_IS_GLOBAL) return b;
    struct rt_block *n = rt_xmalloc(b->descriptor->size);
    memmove(n, b, b->descriptor->size);
    n->flags &= ~(RT_BLOCK_REFCOUNT_MASK | RT_BLOCK_NEEDS_FREE);
    n->flags |= RT_BLOCK_NEEDS_FREE | 2;
    n->isa = RT_CLASS(__NSMallocBlock);
    if (b->flags & RT_BLOCK_HAS_COPY_DISPOSE) b->descriptor->copy(n, b);
    return n;
}
void _Block_release(const void *arg) {
    struct rt_block *b = (struct rt_block *)arg;
    if (!b || !(b->flags & RT_BLOCK_NEEDS_FREE)) return;
    int nf = __atomic_sub_fetch(&b->flags, 2, __ATOMIC_ACQ_REL);
    if ((nf & RT_BLOCK_REFCOUNT_MASK) == 0) {
        if (b->flags & RT_BLOCK_HAS_COPY_DISPOSE) b->descriptor->dispose(b);
        free(b);
    }
}
static struct byref *byref_copy(struct byref *src) {
    if (src->forwarding->flags & BYREF_NEEDS_FREE) {
        __atomic_add_fetch(&src->forwarding->flags, 2, __ATOMIC_RELAXED);
        return src->forwarding;
    }
    struct byref *c = rt_xmalloc((size_t)src->size);
    c->isa = NULL;
    c->flags = src->flags | BYREF_NEEDS_FREE | 4;
    c->forwarding = c;
    src->forwarding = c;
    c->size = src->size;
    if (src->flags & BYREF_HAS_COPY_DISPOSE) {
        c->keep = src->keep; c->destroy = src->destroy;
        size_t hdrsz = sizeof(struct byref);
        if (src->flags & BYREF_LAYOUT_EXTENDED) { memcpy((uint8_t *)c + hdrsz, (uint8_t *)src + hdrsz, 8); }
        src->keep(c, src);
    } else {
        size_t h = 24;
        memmove((uint8_t *)c + h, (uint8_t *)src + h, (size_t)src->size - h);
    }
    return c;
}
static void byref_release(struct byref *b) {
    b = b->forwarding;
    if (!(b->flags & BYREF_NEEDS_FREE)) return;
    int nf = __atomic_sub_fetch(&b->flags, 2, __ATOMIC_ACQ_REL);
    if ((nf & RT_BLOCK_REFCOUNT_MASK) == 0) {
        if (b->flags & BYREF_HAS_COPY_DISPOSE) b->destroy(b);
        free(b);
    }
}
void _Block_object_assign(void *dst, const void *obj, const int flags) {
    void **d = dst;
    if (flags & BLOCK_BYREF_CALLER) { *d = (void *)obj; return; }
    switch (flags & 0x1f) {
    case BLOCK_FIELD_IS_OBJECT: objc_retain((id)obj); *d = (void *)obj; break;
    case BLOCK_FIELD_IS_BLOCK: *d = _Block_copy(obj); break;
    case BLOCK_FIELD_IS_BYREF: case BLOCK_FIELD_IS_BYREF | BLOCK_FIELD_IS_WEAK: *d = byref_copy((struct byref *)obj); break;
    default: *d = (void *)obj; break;
    }
}
void _Block_object_dispose(const void *obj, const int flags) {
    if (flags & BLOCK_BYREF_CALLER) return;
    switch (flags & 0x1f) {
    case BLOCK_FIELD_IS_OBJECT: objc_release((id)obj); break;
    case BLOCK_FIELD_IS_BLOCK: _Block_release(obj); break;
    case BLOCK_FIELD_IS_BYREF: case BLOCK_FIELD_IS_BYREF | BLOCK_FIELD_IS_WEAK: byref_release((struct byref *)obj); break;
    default: break;
    }
}
static id blk_copy(id self, SEL s) { (void)s; return _Block_copy(self); }
static id blk_retain(id self, SEL s) { (void)s; return objc_retain(self); }
static void blk_release(id self, SEL s) { (void)s; _Block_release(self); }
static id blk_autorelease(id self, SEL s) { (void)s; return objc_autorelease(self); }
static void blk_invoke(id self, SEL s) { (void)s; ((void (*)(void *))((struct rt_block *)self)->invoke)(self); }

/* -------------------------------------------------------- exceptions */
_Noreturn void objc_exception_throw(id exc) {
    char *d = rt_describe(exc);
    char msg[1024];
    id name = rt_responds(exc, rt_sel("name")) ? RT_SEND0(id, exc, "name") : NULL;
    id reason = rt_responds(exc, rt_sel("reason")) ? RT_SEND0(id, exc, "reason") : NULL;
    if (name || reason) snprintf(msg, sizeof msg, "uncaught exception '%s', reason: '%s' (exception unwinding is not supported)",
                                 name ? rt_str_utf8(name) : "?", reason ? rt_str_utf8(reason) : "?");
    else snprintf(msg, sizeof msg, "uncaught exception: %s (exception unwinding is not supported)", d);
    free(d);
    rt_guest_exit(72, msg);
}
_Noreturn void objc_exception_rethrow(void) { rt_guest_exit(72, "objc_exception_rethrow"); }
id objc_begin_catch(void *e) { return (id)e; }
void objc_end_catch(void) {}
_Noreturn void objc_terminate(void) { rt_guest_exit(134, "objc_terminate"); }
void iosrt_objc_personality(void) { rt_guest_exit(72, "exception personality routine invoked"); }
_Noreturn void objc_enumerationMutation(id o) { (void)o; rt_guest_exit(73, "collection was mutated while being enumerated"); }

/* ------------------------------------------------------------ C API */
struct rt_method *rt_find_method(Class cls, SEL sel);
void rt_method_set_imp(struct rt_method *m, IMP imp);
void rt_method_exchange(struct rt_method *a, struct rt_method *b);
int rt_class_has_own_method(Class cls, SEL sel);
Class rt_allocate_class_pair(Class super, const char *name, size_t extra);
int rt_class_add_ivar(Class cls, const char *name, size_t size, uint8_t align, const char *type);
int rt_class_conforms(Class c, const void *proto);
const void *rt_protocol_named(const char *name);
const char *rt_protocol_name(const void *p);

char iosrt_objc_empty_cache[64];
char iosrt_objc_empty_vtable[64];

Class objc_getClass(const char *n) { return rt_class_named(n); }
Class objc_lookUpClass(const char *n) { return rt_class_named(n); }
Class objc_getRequiredClass(const char *n) { Class c = rt_class_named(n); if (!c) rt_guest_exit(74, "objc_getRequiredClass failed"); return c; }
Class objc_getMetaClass(const char *n) { Class c = rt_class_named(n); return c ? c->isa : NULL; }
SEL sel_registerName(const char *n) { return rt_sel(n); }
SEL sel_getUid(const char *n) { return rt_sel(n); }
const char *sel_getName(SEL s) { return s ? s : "<null selector>"; }
rt_bool sel_isEqual(SEL a, SEL b) { return a == b; }
Class object_getClass(id o) { return o ? o->isa : NULL; }
Class object_setClass(id o, Class c) { if (!o) return NULL; Class old = o->isa; o->isa = c; return old; }
const char *object_getClassName(id o) { return o ? rt_cls(o->isa)->name : "nil"; }
rt_bool object_isClass(id o) { return o && rt_class_is_meta(o->isa); }
const char *class_getName(Class c) { return c ? rt_cls(c)->name : "nil"; }
Class class_getSuperclass(Class c) { return c ? c->superclass : NULL; }
rt_bool class_isMetaClass(Class c) { return rt_class_is_meta(c); }
size_t class_getInstanceSize(Class c) { return c ? rt_cls(c)->instance_size : 0; }
rt_bool class_respondsToSelector(Class c, SEL s) { int f = 0; if (c) rt_lookup(c, s, &f); return f; }
void *class_getInstanceMethod(Class c, SEL s) { return c ? rt_find_method(c, s) : NULL; }
void *class_getClassMethod(Class c, SEL s) { return c ? rt_find_method(c->isa, s) : NULL; }
IMP class_getMethodImplementation(Class c, SEL s) { int f; IMP i = rt_lookup(c, s, &f); return f ? i : NULL; }
IMP method_getImplementation(struct rt_method *m) { return m ? m->imp : NULL; }
SEL method_getName(struct rt_method *m) { return m ? m->name : NULL; }
const char *method_getTypeEncoding(struct rt_method *m) { return m ? m->types : NULL; }
IMP method_setImplementation(struct rt_method *m, IMP imp) { IMP old = m->imp; rt_method_set_imp(m, imp); return old; }
void method_exchangeImplementations(struct rt_method *a, struct rt_method *b) { rt_method_exchange(a, b); }
rt_bool class_addMethod(Class c, SEL s, IMP imp, const char *types) {
    if (rt_class_has_own_method(c, s)) return 0;
    rt_add_method(c, s, imp, types); return 1;
}
IMP class_replaceMethod(Class c, SEL s, IMP imp, const char *types) {
    IMP old = NULL;
    if (rt_class_has_own_method(c, s)) old = rt_find_method(c, s)->imp;
    rt_add_method(c, s, imp, types);
    return old;
}
Class objc_allocateClassPair(Class super, const char *name, size_t extra) { return rt_allocate_class_pair(super, name, extra); }
void objc_registerClassPair(Class c) { (void)c; }
rt_bool class_addIvar(Class c, const char *n, size_t sz, uint8_t al, const char *t) { return rt_class_add_ivar(c, n, sz, al, t); }
rt_bool class_conformsToProtocol(Class c, const void *p) { return p && rt_class_conforms(c, p); }
const void *objc_getProtocol(const char *n) { return rt_protocol_named(n); }
const char *protocol_getName(const void *p) { return rt_protocol_name(p); }
void *class_getInstanceVariable(Class c, const char *name) {
    for (; c; c = c->superclass) { struct rt_class *rc = rt_cls(c); for (size_t i = 0; i < rc->nivars; i++) if (!strcmp(rc->ivars[i].name, name)) return &rc->ivars[i]; }
    return NULL;
}
long ivar_getOffset(struct rt_ivar *iv) { return iv ? iv->offset : 0; }
const char *ivar_getName(struct rt_ivar *iv) { return iv ? iv->name : NULL; }
const char *ivar_getTypeEncoding(struct rt_ivar *iv) { return iv ? iv->type : NULL; }
id object_getIvar(id o, struct rt_ivar *iv) { return o && iv ? *(id *)((uint8_t *)o + iv->offset) : NULL; }
void object_setIvar(id o, struct rt_ivar *iv, id v) { if (o && iv) *(id *)((uint8_t *)o + iv->offset) = v; }
void **class_copyMethodList(Class c, unsigned *out) {
    struct rt_class *rc = rt_cls(c);
    void **r = rt_xcalloc(rc->nm + 1, sizeof(void *));
    for (size_t i = 0; i < rc->nm; i++) r[i] = rc->mlist[i];
    if (out) *out = (unsigned)rc->nm;
    return r;
}
void **class_copyIvarList(Class c, unsigned *out) {
    struct rt_class *rc = rt_cls(c);
    void **r = rt_xcalloc(rc->nivars + 1, sizeof(void *));
    for (size_t i = 0; i < rc->nivars; i++) r[i] = &rc->ivars[i];
    if (out) *out = (unsigned)rc->nivars;
    return r;
}
void **class_copyPropertyList(Class c, unsigned *out) {
    struct rt_class *rc = rt_cls(c);
    void **r = rt_xcalloc(rc->nprops + 1, sizeof(void *));
    for (size_t i = 0; i < rc->nprops; i++) r[i] = &rc->props[i];
    if (out) *out = (unsigned)rc->nprops;
    return r;
}
const char *property_getName(struct rt_prop *p) { return p->name; }
const char *property_getAttributes(struct rt_prop *p) { return p->attrs; }

id objc_alloc(Class c) { return c ? RT_SEND0(id, (id)c, "alloc") : NULL; }
id objc_allocWithZone(Class c) { return c ? RT_SEND0(id, (id)c, "alloc") : NULL; }
id objc_alloc_init(Class c) { return c ? RT_SEND0(id, RT_SEND0(id, (id)c, "alloc"), "init") : NULL; }
id objc_opt_new(Class c) { return c ? RT_SEND0(id, (id)c, "new") : NULL; }
Class objc_opt_class(id o) { return o ? RT_SEND0(Class, o, "class") : NULL; }
id objc_opt_self(id o) { return o; }
rt_bool objc_opt_isKindOfClass(id o, Class c) { return o ? (RT_SEND1(long, o, "isKindOfClass:", Class, c) & 0xff) != 0 : 0; }
rt_bool objc_opt_respondsToSelector(id o, SEL s) { return o ? (RT_SEND1(long, o, "respondsToSelector:", SEL, s) & 0xff) != 0 : 0; }

/* atomic/nonatomic property helpers emitted by clang */
static pthread_mutex_t prop_lock = PTHREAD_MUTEX_INITIALIZER;
id objc_getProperty(id self, SEL _cmd, ptrdiff_t off, rt_bool atomic) {
    (void)_cmd;
    id *slot = (id *)((uint8_t *)self + off);
    if (!(atomic & 0xff)) return *slot;
    pthread_mutex_lock(&prop_lock); id v = objc_retain(*slot); pthread_mutex_unlock(&prop_lock);
    return objc_autorelease(v);
}
static void set_prop(id self, ptrdiff_t off, id val, int atomic, int copy) {
    id nv = copy ? (val ? RT_SEND0(id, val, "copy") : NULL) : objc_retain(val);
    id *slot = (id *)((uint8_t *)self + off);
    if (atomic) pthread_mutex_lock(&prop_lock);
    id old = *slot; *slot = nv;
    if (atomic) pthread_mutex_unlock(&prop_lock);
    objc_release(old);
}
void objc_setProperty(id self, SEL _cmd, ptrdiff_t off, id val, long atomic, long copy) { (void)_cmd; set_prop(self, off, val, (atomic & 0xff) != 0, (copy & 0xff) != 0); }
void objc_setProperty_atomic(id self, SEL c, id val, ptrdiff_t off) { (void)c; set_prop(self, off, val, 1, 0); }
void objc_setProperty_nonatomic(id self, SEL c, id val, ptrdiff_t off) { (void)c; set_prop(self, off, val, 0, 0); }
void objc_setProperty_atomic_copy(id self, SEL c, id val, ptrdiff_t off) { (void)c; set_prop(self, off, val, 1, 1); }
void objc_setProperty_nonatomic_copy(id self, SEL c, id val, ptrdiff_t off) { (void)c; set_prop(self, off, val, 0, 1); }
void objc_copyStruct(void *dst, const void *src, ptrdiff_t size, long atomic, long strong) {
    (void)strong;
    if (atomic & 0xff) pthread_mutex_lock(&prop_lock);
    memmove(dst, src, (size_t)size);
    if (atomic & 0xff) pthread_mutex_unlock(&prop_lock);
}
void objc_copyCppObjectAtomic(void *dst, const void *src, void (*copyHelper)(void *, const void *)) { pthread_mutex_lock(&prop_lock); copyHelper(dst, src); pthread_mutex_unlock(&prop_lock); }

void rt_blocks_init(void) {
    if (!sync_init_done) {
        pthread_mutexattr_t a; pthread_mutexattr_init(&a); pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
        for (int i = 0; i < 64; i++) pthread_mutex_init(&sync_locks[i], &a);
        sync_init_done = 1;
    }
    rt_native_class(RT_CLASS(NSBlock), &iosrt_meta_NSBlock, "NSBlock", RT_CLASS(NSObject), 0);
    rt_native_class(RT_CLASS(__NSStackBlock), &iosrt_meta___NSStackBlock, "__NSStackBlock__", RT_CLASS(NSBlock), 0);
    rt_native_class(RT_CLASS(__NSMallocBlock), &iosrt_meta___NSMallocBlock, "__NSMallocBlock__", RT_CLASS(NSBlock), 0);
    rt_native_class(RT_CLASS(__NSGlobalBlock), &iosrt_meta___NSGlobalBlock, "__NSGlobalBlock__", RT_CLASS(NSBlock), 0);
    Class b = RT_CLASS(NSBlock);
    rt_native_method(b, "copy", blk_copy, "@16@0:8");
    rt_native_method(b, "copyWithZone:", blk_copy, "@24@0:8^v16");
    rt_native_method(b, "retain", blk_retain, "@16@0:8");
    rt_native_method(b, "release", blk_release, "v16@0:8");
    rt_native_method(b, "autorelease", blk_autorelease, "@16@0:8");
    rt_native_method(b, "invoke", blk_invoke, "v16@0:8");
}
