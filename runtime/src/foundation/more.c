/*
 * Foundation HLE, part 4: NSMethodSignature/NSInvocation + message forwarding,
 * NSProxy, key-value coding, NSURL, NSError, NSValue, locks, NSIndexPath,
 * NSIndexSet, NSUUID, NSCache, NSMapTable, NSPointerArray, NSOperation(Queue),
 * NSAssertionHandler.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")
#define NEWCLASS(N, SUPER, SZ) rt_native_class(RT_CLASS(N), &iosrt_meta_##N, #N, SUPER, SZ)

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSString);
RT_DECLARE_CLASS(NSNumber);
RT_DECLARE_CLASS(NSArray);
RT_DECLARE_CLASS(NSMutableArray);
RT_DECLARE_CLASS(NSMutableDictionary);
RT_DEFINE_CLASS(NSMethodSignature);
RT_DEFINE_CLASS(NSInvocation);
RT_DEFINE_CLASS(NSProxy);
RT_DEFINE_CLASS(NSURL);
RT_DEFINE_CLASS(NSError);
RT_DEFINE_CLASS(NSValue);
RT_DEFINE_CLASS(NSLock);
RT_DEFINE_CLASS(NSRecursiveLock);
RT_DEFINE_CLASS(NSCondition);
RT_DEFINE_CLASS(NSIndexPath);
RT_DEFINE_CLASS(NSIndexSet);
RT_DEFINE_CLASS(NSMutableIndexSet);
RT_DEFINE_CLASS(NSUUID);
RT_DEFINE_CLASS(NSCache);
RT_DEFINE_CLASS(NSMapTable);
RT_DEFINE_CLASS(NSPointerArray);
RT_DEFINE_CLASS(NSOperation);
RT_DEFINE_CLASS(NSBlockOperation);
RT_DEFINE_CLASS(NSOperationQueue);
RT_DEFINE_CLASS(NSAssertionHandler);

id rt_dict_new(id *vals, id *keys, size_t n);
void rt_dict_set(id dict, id key, id val);
void rt_soft_error(const char *msg);
int rt_is_strict(void);
struct rt_method *rt_find_method(Class cls, SEL sel);
id rt_data_new(const void *b, size_t n);
void iosrt_dispatch_async(void *q, void *block);
extern struct { const char *label; int main; } iosrt_dispatch_main_q;
void *iosrt_dispatch_get_global_queue(long prio, unsigned long flags);

/* ============================================================ type encodings */
enum { TK_VOID, TK_INT, TK_FLOAT, TK_DOUBLE, TK_HFA_F, TK_HFA_D, TK_STRUCT, TK_PTR };
struct tinfo { int kind; size_t size, align; int nelem; };

static const char *skip_qual(const char *t) { while (*t && strchr("rnNoORVA+-", *t)) t++; return t; }
static const char *skip_digits(const char *t) { while (*t == '-' || isdigit((unsigned char)*t)) t++; return t; }
static const char *parse_type(const char *t, struct tinfo *ti);
/* returns pointer after the type; fills size/align and float-element info for HFA detection */
static const char *parse_struct(const char *t, struct tinfo *ti, char close) {
    /* t points after '{' or '(' : name=fields} */
    const char *eq = t; int depth = 0;
    while (*eq && !(*eq == '=' && depth == 0) && !(*eq == close && depth == 0)) { if (*eq == '{' || *eq == '(') depth++; if (*eq == '}' || *eq == ')') depth--; eq++; }
    size_t size = 0, align = 1; int fk = -1, n = 0, uniform = 1;
    if (*eq == '=') {
        t = eq + 1;
        while (*t && *t != close) {
            if (*t == '"') { t = strchr(t + 1, '"'); if (!t) break; t++; continue; }   /* field names */
            struct tinfo f; t = parse_type(t, &f);
            if (f.align > align) align = f.align;
            if (close == '}') { size = (size + f.align - 1) & ~(f.align - 1); size += f.size; }
            else if (f.size > size) size = f.size;
            int ek = f.kind == TK_FLOAT || f.kind == TK_HFA_F ? 1 : f.kind == TK_DOUBLE || f.kind == TK_HFA_D ? 2 : 0;
            int cnt = f.kind == TK_HFA_F || f.kind == TK_HFA_D ? f.nelem : 1;
            if (!ek || (fk >= 0 && fk != ek)) uniform = 0;
            fk = ek; n += cnt;
        }
        if (*t == close) t++;
    } else { t = *eq ? eq + 1 : eq; }
    size = (size + align - 1) & ~(align - 1);
    ti->size = size ? size : 0; ti->align = align;
    if (uniform && fk > 0 && n >= 1 && n <= 4 && close == '}') { ti->kind = fk == 1 ? TK_HFA_F : TK_HFA_D; ti->nelem = n; }
    else { ti->kind = TK_STRUCT; ti->nelem = 0; }
    return t;
}
static const char *parse_type(const char *t, struct tinfo *ti) {
    t = skip_qual(t);
    memset(ti, 0, sizeof *ti);
    char c = *t ? *t++ : 'v';
    switch (c) {
    case 'v': ti->kind = TK_VOID; break;
    case 'c': case 'C': case 'B': ti->kind = TK_INT; ti->size = ti->align = 1; break;
    case 's': case 'S': ti->kind = TK_INT; ti->size = ti->align = 2; break;
    case 'i': case 'I': ti->kind = TK_INT; ti->size = ti->align = 4; break;
    case 'l': case 'L': case 'q': case 'Q': ti->kind = TK_INT; ti->size = ti->align = 8; break;
    case 'f': ti->kind = TK_FLOAT; ti->size = ti->align = 4; break;
    case 'd': case 'D': ti->kind = TK_DOUBLE; ti->size = ti->align = 8; break;
    case '@':
        ti->kind = TK_PTR; ti->size = ti->align = 8;
        if (*t == '?') t++;                                      /* block */
        else if (*t == '"') { const char *q = strchr(t + 1, '"'); t = q ? q + 1 : t + strlen(t); }
        break;
    case '#': case ':': case '*': case '%': case '?': ti->kind = TK_PTR; ti->size = ti->align = 8; break;
    case '^': { struct tinfo d; t = parse_type(t, &d); ti->kind = TK_PTR; ti->size = ti->align = 8; break; }
    case '{': t = parse_struct(t, ti, '}'); break;
    case '(': t = parse_struct(t, ti, ')'); break;
    case '[': {
        long n = strtol(t, (char **)&t, 10); struct tinfo e; t = parse_type(t, &e); if (*t == ']') t++;
        ti->kind = TK_STRUCT; ti->size = e.size * (size_t)n; ti->align = e.align; break;
    }
    case 'b': { strtol(t, (char **)&t, 10); ti->kind = TK_INT; ti->size = ti->align = 4; break; }
    default: ti->kind = TK_PTR; ti->size = ti->align = 8; break;
    }
    return skip_digits(t);
}

/* =========================================================== NSMethodSignature */
#define MAX_ARGS 24
struct nssig { Class isa; char *types; int nargs; struct tinfo ret; struct tinfo args[MAX_ARGS]; char *argtypes[MAX_ARGS]; char *rettype; };
static char *dup_range(const char *a, const char *b) { char *r = rt_xmalloc((size_t)(b - a) + 1); memcpy(r, a, (size_t)(b - a)); r[b - a] = 0; return r; }
static id sig_make(const char *types) {
    if (!types || !*types) return NULL;
    id o = rt_alloc_instance(RT_CLASS(NSMethodSignature), 0);
    struct nssig *s = (struct nssig *)o;
    s->types = rt_xstrdup(types);
    const char *p = types, *start = skip_qual(p);
    p = parse_type(p, &s->ret);
    s->rettype = dup_range(start, p); { char *d = s->rettype; while (*d) d++; while (d > s->rettype && isdigit((unsigned char)d[-1])) *--d = 0; }
    while (*p && s->nargs < MAX_ARGS) {
        start = skip_qual(p);
        p = parse_type(p, &s->args[s->nargs]);
        char *at = dup_range(start, p); { char *d = at; while (*d) d++; while (d > at && isdigit((unsigned char)d[-1])) *--d = 0; }
        s->argtypes[s->nargs++] = at;
    }
    return objc_autorelease(o);
}
static id sig_withTypes(Class c, SEL s, const char *types) { (void)c; (void)s; return sig_make(types); }
static unsigned long sig_numArgs(id self, SEL s) { (void)s; return (unsigned long)((struct nssig *)self)->nargs; }
static const char *sig_argType(id self, SEL s, unsigned long i) { (void)s; struct nssig *g = (struct nssig *)self; return i < (unsigned long)g->nargs ? g->argtypes[i] : "v"; }
static const char *sig_retType(id self, SEL s) { (void)s; return ((struct nssig *)self)->rettype; }
static unsigned long sig_retLen(id self, SEL s) { (void)s; return ((struct nssig *)self)->ret.size; }
static unsigned long sig_frameLen(id self, SEL s) { (void)s; return 8u * (unsigned long)((struct nssig *)self)->nargs; }
static rt_bool sig_oneway(id self, SEL s) { (void)self; (void)s; return 0; }

/* NSObject -methodSignatureForSelector: from the method's type encoding */
static id o_methodSignature(id self, SEL s, SEL q) {
    (void)s; struct rt_method *m = rt_find_method(self->isa, q);
    if (!m || !m->types || !*m->types) return NULL;
    return sig_make(m->types);
}
static id o_cmethodSignature(Class self, SEL s, SEL q) { (void)s; struct rt_method *m = rt_find_method(self, q); return m && m->types && *m->types ? sig_make(m->types) : NULL; }
static id o_instanceMethodSignature(Class self, SEL s, SEL q) { (void)s; struct rt_method *m = rt_find_method(self, q); return m && m->types && *m->types ? sig_make(m->types) : NULL; }

/* ================================================================ NSInvocation */
struct rt_callframe { uint64_t x[9]; double d[8]; uint64_t *stack; uint64_t nstack; uint64_t rx[2]; double rd[4]; };
extern void iosrt_call(void *fn, struct rt_callframe *f);
struct nsinv { Class isa; id sig; uint8_t *argbuf[MAX_ARGS]; uint8_t *retbuf; long retained; };
static id inv_make(id sig) {
    if (!sig) return NULL;
    id o = rt_alloc_instance(RT_CLASS(NSInvocation), 0);
    struct nsinv *v = (struct nsinv *)o; struct nssig *g = (struct nssig *)sig;
    v->sig = objc_retain(sig);
    for (int i = 0; i < g->nargs; i++) v->argbuf[i] = rt_xcalloc(1, g->args[i].size > 8 ? g->args[i].size : 8);
    v->retbuf = rt_xcalloc(1, g->ret.size > 32 ? g->ret.size : 32);
    return objc_autorelease(o);
}
static id inv_withSig(Class c, SEL s, id sig) { (void)c; (void)s; return inv_make(sig); }
static void inv_dealloc(id self, SEL s) {
    (void)s; struct nsinv *v = (struct nsinv *)self; struct nssig *g = (struct nssig *)v->sig;
    if (v->retained) for (int i = 0; i < g->nargs; i++) if (g->args[i].kind == TK_PTR && g->argtypes[i][0] == '@') objc_release(*(id *)v->argbuf[i]);
    for (int i = 0; i < g->nargs; i++) free(v->argbuf[i]);
    free(v->retbuf); objc_release(v->sig); rt_dealloc_instance(self);
}
static id inv_sig(id self, SEL s) { (void)s; return ((struct nsinv *)self)->sig; }
static void inv_setArg(id self, SEL s, const void *p, long i) {
    (void)s; struct nsinv *v = (struct nsinv *)self; struct nssig *g = (struct nssig *)v->sig;
    if (i < 0 || i >= g->nargs) { rt_soft_error("-[NSInvocation setArgument:atIndex:]: index out of range"); return; }
    if (v->retained && g->argtypes[i][0] == '@') { objc_retain(*(id *)p); objc_release(*(id *)v->argbuf[i]); }
    memcpy(v->argbuf[i], p, g->args[i].size);
}
static void inv_getArg(id self, SEL s, void *p, long i) {
    (void)s; struct nsinv *v = (struct nsinv *)self; struct nssig *g = (struct nssig *)v->sig;
    if (i < 0 || i >= g->nargs) { rt_soft_error("-[NSInvocation getArgument:atIndex:]: index out of range"); return; }
    memcpy(p, v->argbuf[i], g->args[i].size);
}
static id inv_target(id self, SEL s) { (void)s; return *(id *)((struct nsinv *)self)->argbuf[0]; }
static void inv_setTarget(id self, SEL s, id t) { inv_setArg(self, s, &t, 0); }
static SEL inv_selector(id self, SEL s) { (void)s; return *(SEL *)((struct nsinv *)self)->argbuf[1]; }
static void inv_setSelector(id self, SEL s, SEL q) { inv_setArg(self, s, &q, 1); }
static void inv_retainArgs(id self, SEL s) {
    (void)s; struct nsinv *v = (struct nsinv *)self; struct nssig *g = (struct nssig *)v->sig;
    if (v->retained) return;
    v->retained = 1;
    for (int i = 0; i < g->nargs; i++) if (g->argtypes[i][0] == '@') objc_retain(*(id *)v->argbuf[i]);
}
static rt_bool inv_argsRetained(id self, SEL s) { (void)s; return ((struct nsinv *)self)->retained; }
static void inv_getRet(id self, SEL s, void *p) { (void)s; struct nsinv *v = (struct nsinv *)self; memcpy(p, v->retbuf, ((struct nssig *)v->sig)->ret.size); }
static void inv_setRet(id self, SEL s, const void *p) { (void)s; struct nsinv *v = (struct nsinv *)self; memcpy(v->retbuf, p, ((struct nssig *)v->sig)->ret.size); }

/* Marshal invocation arguments into an AArch64 call frame (AAPCS64 / Darwin, non-variadic) */
static void frame_from_args(struct nsinv *v, struct rt_callframe *f, uint64_t *stackbuf, size_t stackcap) {
    struct nssig *g = (struct nssig *)v->sig;
    int ngr = 0, nfr = 0; size_t ns = 0;
    memset(f, 0, sizeof *f);
    for (int i = 0; i < g->nargs; i++) {
        struct tinfo *t = &g->args[i]; uint8_t *src = v->argbuf[i];
        switch (t->kind) {
        case TK_FLOAT: case TK_DOUBLE:
            if (nfr < 8) { double d = 0; memcpy(&d, src, t->size); memcpy(&f->d[nfr++], &d, 8); }
            else if (ns < stackcap) memcpy(&stackbuf[ns++], src, t->size);
            break;
        case TK_HFA_F: case TK_HFA_D: {
            size_t es = t->kind == TK_HFA_F ? 4 : 8;
            if (nfr + t->nelem <= 8) { for (int k = 0; k < t->nelem; k++) { uint64_t z = 0; memcpy(&z, src + k * es, es); memcpy(&f->d[nfr++], &z, 8); } }
            else { nfr = 8; size_t words = (t->size + 7) / 8; for (size_t k = 0; k < words && ns < stackcap; k++) memcpy(&stackbuf[ns++], src + k * 8, 8); }
            break;
        }
        case TK_STRUCT:
            if (t->size > 16) {          /* passed by reference */
                uint64_t p = (uint64_t)(uintptr_t)src;
                if (ngr < 8) f->x[ngr++] = p; else if (ns < stackcap) stackbuf[ns++] = p;
            } else {
                int words = (int)((t->size + 7) / 8);
                if (ngr + words <= 8) { for (int k = 0; k < words; k++) { uint64_t z = 0; memcpy(&z, src + k * 8, (k + 1) * 8 <= (int)t->size ? 8 : t->size - (size_t)k * 8); f->x[ngr++] = z; } }
                else { ngr = 8; for (int k = 0; k < words && ns < stackcap; k++) memcpy(&stackbuf[ns++], src + k * 8, 8); }
            }
            break;
        default: {
            uint64_t z = 0; memcpy(&z, src, t->size > 8 ? 8 : t->size);
            if (t->size == 1 && strchr("cB", g->argtypes[i][0])) z = (uint64_t)(int64_t)(int8_t)z;
            if (ngr < 8) f->x[ngr++] = z; else if (ns < stackcap) stackbuf[ns++] = z;
        }
        }
    }
    if (g->ret.kind == TK_STRUCT && g->ret.size > 16) f->x[8] = (uint64_t)(uintptr_t)v->retbuf;
    f->stack = stackbuf; f->nstack = ns;
}
static void ret_from_frame(struct nsinv *v, const struct rt_callframe *f) {
    struct nssig *g = (struct nssig *)v->sig; struct tinfo *r = &g->ret;
    switch (r->kind) {
    case TK_VOID: break;
    case TK_FLOAT: memcpy(v->retbuf, &f->rd[0], 4); break;
    case TK_DOUBLE: memcpy(v->retbuf, &f->rd[0], 8); break;
    case TK_HFA_F: for (int k = 0; k < r->nelem; k++) memcpy(v->retbuf + k * 4, &f->rd[k], 4); break;
    case TK_HFA_D: for (int k = 0; k < r->nelem; k++) memcpy(v->retbuf + k * 8, &f->rd[k], 8); break;
    case TK_STRUCT: if (r->size <= 16) memcpy(v->retbuf, f->rx, r->size); break;   /* else written through x8 */
    default: memcpy(v->retbuf, &f->rx[0], r->size > 8 ? 8 : r->size); break;
    }
}
static void inv_invokeWithTarget(id self, SEL s, id target) {
    (void)s; struct nsinv *v = (struct nsinv *)self;
    *(id *)v->argbuf[0] = target;
    if (!target) { memset(v->retbuf, 0, ((struct nssig *)v->sig)->ret.size); return; }
    uint64_t stack[64]; struct rt_callframe f;
    frame_from_args(v, &f, stack, 64);
    iosrt_call((void *)objc_msgSend, &f);
    ret_from_frame(v, &f);
}
static void inv_invoke(id self, SEL s) { inv_invokeWithTarget(self, s, inv_target(self, s)); }

/* Called by iosrt_msgForward with the caller's registers: build an NSInvocation,
 * hand it to -forwardInvocation:, then load its return value back into the frame. */
void rt_forward_invocation(struct rt_callframe *f) {
    id self = (id)(uintptr_t)f->x[0]; SEL sel = (SEL)(uintptr_t)f->x[1];
    void *pool = objc_autoreleasePoolPush();
    id sig = RT_SEND1(id, self, "methodSignatureForSelector:", SEL, sel);
    if (!sig) {
        char b[300]; snprintf(b, sizeof b, "-[%s %s]: forwarding without a method signature", rt_class_name(self->isa), sel);
        rt_soft_error(b);
        memset(f->rx, 0, sizeof f->rx); memset(f->rd, 0, sizeof f->rd);
        objc_autoreleasePoolPop(pool);
        return;
    }
    id inv = inv_make(sig);
    struct nsinv *v = (struct nsinv *)inv; struct nssig *g = (struct nssig *)sig;
    /* unmarshal registers / caller stack into the argument buffers */
    int ngr = 0, nfr = 0; const uint64_t *stk = f->stack; size_t ns = 0;
    for (int i = 0; i < g->nargs; i++) {
        struct tinfo *t = &g->args[i]; uint8_t *dst = v->argbuf[i];
        switch (t->kind) {
        case TK_FLOAT: case TK_DOUBLE: if (nfr < 8) memcpy(dst, &f->d[nfr++], t->size); else memcpy(dst, &stk[ns++], t->size); break;
        case TK_HFA_F: case TK_HFA_D: {
            size_t es = t->kind == TK_HFA_F ? 4 : 8;
            if (nfr + t->nelem <= 8) for (int k = 0; k < t->nelem; k++) memcpy(dst + k * es, &f->d[nfr++], es);
            else { nfr = 8; memcpy(dst, &stk[ns], t->size); ns += (t->size + 7) / 8; }
            break;
        }
        case TK_STRUCT:
            if (t->size > 16) { const void *p = (const void *)(uintptr_t)(ngr < 8 ? f->x[ngr++] : stk[ns++]); memcpy(dst, p, t->size); }
            else {
                int words = (int)((t->size + 7) / 8);
                if (ngr + words <= 8) { uint64_t tmp[2]; for (int k = 0; k < words; k++) tmp[k] = f->x[ngr++]; memcpy(dst, tmp, t->size); }
                else { ngr = 8; memcpy(dst, &stk[ns], t->size); ns += (size_t)words; }
            }
            break;
        default: if (ngr < 8) memcpy(dst, &f->x[ngr++], t->size > 8 ? 8 : t->size); else memcpy(dst, &stk[ns++], t->size > 8 ? 8 : t->size);
        }
    }
    RT_SEND1(void, self, "forwardInvocation:", id, inv);
    /* return value back into registers (or through the caller's x8 buffer) */
    struct tinfo *r = &g->ret;
    memset(f->rx, 0, sizeof f->rx); memset(f->rd, 0, sizeof f->rd);
    switch (r->kind) {
    case TK_VOID: break;
    case TK_FLOAT: memcpy(&f->rd[0], v->retbuf, 4); break;
    case TK_DOUBLE: memcpy(&f->rd[0], v->retbuf, 8); break;
    case TK_HFA_F: for (int k = 0; k < r->nelem; k++) memcpy(&f->rd[k], v->retbuf + k * 4, 4); break;
    case TK_HFA_D: for (int k = 0; k < r->nelem; k++) memcpy(&f->rd[k], v->retbuf + k * 8, 8); break;
    case TK_STRUCT: if (r->size <= 16) memcpy(f->rx, v->retbuf, r->size); else if (f->x[8]) memcpy((void *)(uintptr_t)f->x[8], v->retbuf, r->size); break;
    default: memcpy(&f->rx[0], v->retbuf, r->size > 8 ? 8 : r->size);
    }
    /* keep an object return value alive past the pool */
    if (g->rettype[0] == '@' && f->rx[0]) { objc_retain((id)(uintptr_t)f->rx[0]); objc_autoreleasePoolPop(pool); objc_autorelease((id)(uintptr_t)f->rx[0]); }
    else objc_autoreleasePoolPop(pool);
}
IMP rt_default_forward_imp;
static void o_forwardInvocation(id self, SEL s, id inv) {
    (void)s; char b[300]; SEL q = inv_selector(inv, NULL);
    snprintf(b, sizeof b, "-[%s %s]: unrecognized selector (forwardInvocation:)", rt_class_name(self->isa), q);
    rt_soft_error(b);
}

/* ===================================================================== NSProxy */
static id px_alloc(Class c, SEL s) { (void)s; return rt_alloc_instance(c, 0); }
static void px_dealloc(id self, SEL s) { (void)s; rt_dealloc_instance(self); }
static id px_retain(id self, SEL s) { (void)s; return objc_retain(self); }
static void px_release(id self, SEL s) { (void)s; objc_release(self); }
static id px_autorelease(id self, SEL s) { (void)s; return objc_autorelease(self); }
static Class px_class(id self, SEL s) { (void)s; return self->isa; }
static Class px_cclass(Class self, SEL s) { (void)s; return self; }
static rt_bool px_respondsToSelector(Class self, SEL s, SEL q) { (void)s; int f = 0; rt_lookup(self->isa, q, &f); return f; }
static id px_description(id self, SEL s) { (void)s; char b[128]; snprintf(b, sizeof b, "<%s: %p>", rt_class_name(self->isa), (void *)self); return rt_str(b); }
static rt_bool px_isProxy(id self, SEL s) { (void)self; (void)s; return 1; }
static id px_self(id self, SEL s) { (void)s; return self; }
static unsigned long px_hash(id self, SEL s) { (void)s; return (unsigned long)(uintptr_t)self >> 4; }
static rt_bool px_isEqual(id self, SEL s, id o) { (void)s; return self == o; }

/* ======================================================= key-value coding */
static char *cap_first(const char *k) { char *r = rt_xstrdup(k); if (r[0]) r[0] = (char)toupper((unsigned char)r[0]); return r; }
static struct rt_ivar *find_ivar(Class c, const char *name) {
    for (; c; c = c->superclass) { struct rt_class *rc = rt_cls(c); for (size_t i = 0; i < rc->nivars; i++) if (!strcmp(rc->ivars[i].name, name)) return &rc->ivars[i]; }
    return NULL;
}
static id box_scalar(const char *type, const void *p) {
    switch (type ? *skip_qual(type) : '@') {
    case 'c': case 'B': return RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithBool:", long, *(const signed char *)p);
    case 'C': return rt_num_int(*(const unsigned char *)p);
    case 's': return rt_num_int(*(const short *)p);
    case 'S': return rt_num_int(*(const unsigned short *)p);
    case 'i': return rt_num_int(*(const int *)p);
    case 'I': return rt_num_int(*(const unsigned *)p);
    case 'l': case 'q': return rt_num_int(*(const long long *)p);
    case 'L': case 'Q': return rt_num_int((long long)*(const unsigned long long *)p);
    case 'f': return rt_num_dbl(*(const float *)p);
    case 'd': return rt_num_dbl(*(const double *)p);
    default: return *(const id *)p;
    }
}
static void unbox_scalar(const char *type, id v, void *p) {
    char t = type ? *skip_qual(type) : '@';
    long long iv = v ? RT_SEND0(long long, v, "longLongValue") : 0;
    double dv = v ? RT_SEND0(double, v, "doubleValue") : 0;
    switch (t) {
    case 'c': case 'B': case 'C': *(char *)p = (char)iv; break;
    case 's': case 'S': *(short *)p = (short)iv; break;
    case 'i': case 'I': *(int *)p = (int)iv; break;
    case 'l': case 'q': case 'L': case 'Q': *(long long *)p = iv; break;
    case 'f': *(float *)p = (float)dv; break;
    case 'd': *(double *)p = dv; break;
    default: break;
    }
}
static id o_valueForKey(id self, SEL s, id key) {
    (void)s; if (!key) return NULL;
    const char *k = rt_str_utf8(key);
    char *K = cap_first(k); char sel[512];
    const char *cands[3] = { k, NULL, NULL }; char b1[512], b2[512];
    snprintf(b1, sizeof b1, "is%s", K); snprintf(b2, sizeof b2, "get%s", K); cands[1] = b1; cands[2] = b2;
    for (int i = 0; i < 3; i++) {
        struct rt_method *m = rt_find_method(self->isa, rt_sel(cands[i]));
        if (!m) continue;
        free(K);
        const char *rt = m->types && *m->types ? m->types : "@";
        struct tinfo ti; parse_type(rt, &ti);
        if (*skip_qual(rt) == '@' || *skip_qual(rt) == '#') return ((id (*)(id, SEL))m->imp)(self, m->name);
        uint8_t buf[32] = { 0 };
        if (ti.kind == TK_DOUBLE) { double d = ((double (*)(id, SEL))m->imp)(self, m->name); memcpy(buf, &d, 8); }
        else if (ti.kind == TK_FLOAT) { float d = ((float (*)(id, SEL))m->imp)(self, m->name); memcpy(buf, &d, 4); }
        else { long long v = ((long long (*)(id, SEL))m->imp)(self, m->name); memcpy(buf, &v, 8); }
        return box_scalar(rt, buf);
    }
    const char *ivn[4] = { NULL, NULL, k, NULL }; char i1[512], i2[512];
    snprintf(i1, sizeof i1, "_%s", k); snprintf(i2, sizeof i2, "_is%s", K); ivn[0] = i1; ivn[1] = i2; ivn[3] = b1;
    for (int i = 0; i < 4; i++) {
        struct rt_ivar *iv = find_ivar(self->isa, ivn[i]);
        if (iv) { free(K); return box_scalar(iv->type, (uint8_t *)self + iv->offset); }
    }
    free(K);
    snprintf(sel, sizeof sel, "[<%s %p> valueForUndefinedKey:]: this class is not key value coding-compliant for the key %s", rt_class_name(self->isa), (void *)self, k);
    rt_soft_error(sel);
    return NULL;
}
static void o_setValueForKey(id self, SEL s, id value, id key) {
    (void)s; if (!key) return;
    const char *k = rt_str_utf8(key);
    char *K = cap_first(k); char setter[512];
    snprintf(setter, sizeof setter, "set%s:", K);
    struct rt_method *m = rt_find_method(self->isa, rt_sel(setter));
    if (m) {
        const char *types = m->types && *m->types ? m->types : "v24@0:8@16";
        /* argument 2 type: skip return, self, _cmd */
        struct tinfo t; const char *p = parse_type(types, &t); p = parse_type(p, &t); p = parse_type(p, &t);
        const char *at = skip_qual(p);
        free(K);
        if (*at == '@' || *at == '#' || !*at) { ((void (*)(id, SEL, id))m->imp)(self, m->name, value); return; }
        struct tinfo ai; parse_type(at, &ai);
        if (ai.kind == TK_DOUBLE) ((void (*)(id, SEL, double))m->imp)(self, m->name, value ? RT_SEND0(double, value, "doubleValue") : 0);
        else if (ai.kind == TK_FLOAT) ((void (*)(id, SEL, float))m->imp)(self, m->name, value ? (float)RT_SEND0(double, value, "doubleValue") : 0);
        else ((void (*)(id, SEL, long long))m->imp)(self, m->name, value ? RT_SEND0(long long, value, "longLongValue") : 0);
        return;
    }
    char i1[512], i2[512], i3[512];
    snprintf(i1, sizeof i1, "_%s", k); snprintf(i2, sizeof i2, "_is%s", K); snprintf(i3, sizeof i3, "is%s", K);
    const char *ivn[4] = { i1, i2, k, i3 };
    free(K);
    for (int i = 0; i < 4; i++) {
        struct rt_ivar *iv = find_ivar(self->isa, ivn[i]);
        if (!iv) continue;
        void *slot = (uint8_t *)self + iv->offset;
        if (!iv->type || *skip_qual(iv->type) == '@') { id old = *(id *)slot; *(id *)slot = objc_retain(value); objc_release(old); }
        else unbox_scalar(iv->type, value, slot);
        return;
    }
    snprintf(setter, sizeof setter, "[<%s %p> setValue:forUndefinedKey:]: this class is not key value coding-compliant for the key %s", rt_class_name(self->isa), (void *)self, k);
    rt_soft_error(setter);
}
static id o_valueForKeyPath(id self, SEL s, id path) {
    (void)s; id cur = self;
    id parts = RT_SEND1(id, path, "componentsSeparatedByString:", id, rt_str("."));
    for (size_t i = 0; cur && i < rt_array_count(parts); i++) cur = RT_SEND1(id, cur, "valueForKey:", id, rt_array_at(parts, i));
    return cur;
}
static void o_setValueForKeyPath(id self, SEL s, id value, id path) {
    (void)s; id parts = RT_SEND1(id, path, "componentsSeparatedByString:", id, rt_str("."));
    size_t n = rt_array_count(parts); id cur = self;
    for (size_t i = 0; cur && i + 1 < n; i++) cur = RT_SEND1(id, cur, "valueForKey:", id, rt_array_at(parts, i));
    if (cur && n) RT_SEND2(void, cur, "setValue:forKey:", id, value, id, rt_array_at(parts, n - 1));
}
static void o_setValuesForKeys(id self, SEL s, id dict) {
    (void)s; id keys = RT_SEND0(id, dict, "allKeys");
    for (size_t i = 0; i < rt_array_count(keys); i++) { id k = rt_array_at(keys, i); RT_SEND2(void, self, "setValue:forKey:", id, rt_dict_get(dict, k), id, k); }
}
static void o_kvo_noop(id self, SEL s, id a, id b, unsigned long o, void *c) { (void)self; (void)s; (void)a; (void)b; (void)o; (void)c; }
static void o_kvo_remove(id self, SEL s, id a, id b) { (void)self; (void)s; (void)a; (void)b; }
static void o_willChange(id self, SEL s, id k) { (void)self; (void)s; (void)k; }

/* ======================================================================= NSURL */
struct nsurl { Class isa; id str; long file; };
static id url_make(Class c, const char *s, int file) {
    id o = rt_alloc_instance(c ? c : RT_CLASS(NSURL), 0); struct nsurl *u = (struct nsurl *)o;
    if (file) { char b[4200]; snprintf(b, sizeof b, "file://%s", s); u->str = rt_str_new(b); }
    else u->str = rt_str_new(s);
    u->file = file || !strncmp(s, "file://", 7);
    return o;
}
static id u_withString(Class c, SEL s, id str) { (void)s; if (!str) return NULL; return objc_autorelease(url_make(c, rt_str_utf8(str), 0)); }
/* RFC 3986 reference resolution (section 5.2), malloc'd result */
static int has_scheme(const char *r) {
    if (!((*r >= 'a' && *r <= 'z') || (*r >= 'A' && *r <= 'Z'))) return 0;
    for (const char *p = r + 1; *p; p++) {
        if (*p == ':') return 1;
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '+' || *p == '-' || *p == '.')) return 0;
    }
    return 0;
}
static void remove_dot_segments(char *path) {
    char *out = malloc(strlen(path) + 2), *o = out; const char *in = path;
    *o = 0;
    while (*in) {
        if (!strncmp(in, "../", 3)) in += 3;
        else if (!strncmp(in, "./", 2)) in += 2;
        else if (!strncmp(in, "/./", 3)) in += 2;
        else if (!strcmp(in, "/.")) { in = "/"; }
        else if (!strncmp(in, "/../", 4) || !strcmp(in, "/..")) {
            in = in[3] ? in + 3 : "/";
            char *sl = strrchr(out, '/'); if (sl) { o = sl; *o = 0; } else { o = out; *o = 0; }
        } else if (!strcmp(in, ".") || !strcmp(in, "..")) in += strlen(in);
        else {
            const char *start = in; if (*in == '/') in++;
            while (*in && *in != '/') in++;
            size_t n = (size_t)(in - start); memcpy(o, start, n); o += n; *o = 0;
        }
    }
    strcpy(path, out); free(out);
}
char *rt_url_resolve(const char *base, const char *ref) {
    size_t cap = strlen(base) + strlen(ref) + 8; char *r = malloc(cap);
    if (has_scheme(ref) || !*base) { strcpy(r, ref); return r; }
    const char *bs = strchr(base, ':'); size_t slen = bs ? (size_t)(bs - base) + 1 : 0;    /* "scheme:" */
    const char *rest = base + slen;
    size_t alen = 0;                                                                       /* "//authority" */
    if (!strncmp(rest, "//", 2)) alen = 2 + strcspn(rest + 2, "/?#");
    const char *bpath = rest + alen; size_t plen = strcspn(bpath, "?#");
    if (!strncmp(ref, "//", 2)) { snprintf(r, cap, "%.*s%s", (int)slen, base, ref); return r; }
    if (*ref == '#' ) { size_t n = strcspn(base, "#"); snprintf(r, cap, "%.*s%s", (int)n, base, ref); return r; }
    if (*ref == '?') { snprintf(r, cap, "%.*s%s", (int)(slen + alen + plen), base, ref); return r; }
    char *path = malloc(plen + strlen(ref) + 4);
    if (*ref == '/') strcpy(path, ref);
    else {
        const char *sl = NULL; for (size_t i = 0; i < plen; i++) if (bpath[i] == '/') sl = bpath + i;
        size_t keep = sl ? (size_t)(sl - bpath) + 1 : 0;
        if (!keep && alen) { path[0] = '/'; keep = 1; }   /* "http://host" + "a" -> "/a" */
        else memcpy(path, bpath, keep);
        strcpy(path + keep, ref);
    }
    /* split off query/fragment before normalizing the path */
    size_t pl = strcspn(path, "?#"); char tail[2048]; snprintf(tail, sizeof tail, "%s", path + pl); path[pl] = 0;
    remove_dot_segments(path);
    snprintf(r, cap, "%.*s%s%s", (int)(slen + alen), base, path, tail);
    free(path);
    return r;
}
static id u_withStringRel(Class c, SEL s, id str, id base) {
    if (!str) return NULL;
    if (!base) return u_withString(c, s, str);
    char *abs = rt_url_resolve(rt_str_utf8(((struct nsurl *)base)->str), rt_str_utf8(str));
    id r = objc_autorelease(url_make(c, abs, 0)); free(abs);
    return r;
}
static id u_fileURL(Class c, SEL s, id path) { (void)s; if (!path) return NULL; return objc_autorelease(url_make(c, rt_str_utf8(path), 1)); }
static id u_fileURLDir(Class c, SEL s, id path, long isdir) { (void)isdir; return u_fileURL(c, s, path); }
static id u_initWithString(id self, SEL s, id str) { (void)s; struct nsurl *u = (struct nsurl *)self; if (!str) { objc_release(self); return NULL; } u->str = objc_retain(RT_SEND0(id, str, "copy")); objc_release(u->str); u->file = !strncmp(rt_str_utf8(str), "file://", 7); return self; }
static id u_initFileURL(id self, SEL s, id path) { Class c = self->isa; (void)s; rt_dealloc_instance(self); return path ? url_make(c, rt_str_utf8(path), 1) : NULL; }
static id u_initFileURLDir(id self, SEL s, id path, long isdir) { (void)isdir; return u_initFileURL(self, s, path); }
static id u_initWithStringRel(id self, SEL s, id str, id base) { (void)s; Class c = self->isa; rt_dealloc_instance(self); return objc_retain(u_withStringRel(c, rt_sel("URLWithString:relativeToURL:"), str, base)); }
static void u_dealloc(id self, SEL s) { (void)s; objc_release(((struct nsurl *)self)->str); rt_dealloc_instance(self); }
static id u_absoluteString(id self, SEL s) { (void)s; return ((struct nsurl *)self)->str; }
static const char *after_scheme(const char *u) { const char *p = strstr(u, "://"); return p ? p + 3 : u; }
static id u_path(id self, SEL s) {
    (void)s; const char *u = rt_str_utf8(((struct nsurl *)self)->str);
    const char *p = after_scheme(u);
    if (((struct nsurl *)self)->file) { const char *q = strchr(p, '?'); char *t = q ? strndup(p, (size_t)(q - p)) : strdup(p); id r = rt_str(t); free(t); return r; }
    const char *sl = strchr(p, '/'); if (!sl) return rt_str("");
    const char *q = strpbrk(sl, "?#"); char *t = q ? strndup(sl, (size_t)(q - sl)) : strdup(sl); id r = rt_str(t); free(t); return r;
}
static id u_scheme(id self, SEL s) { (void)s; const char *u = rt_str_utf8(((struct nsurl *)self)->str); const char *p = strstr(u, ":"); if (!p) return NULL; char *t = strndup(u, (size_t)(p - u)); id r = rt_str(t); free(t); return r; }
static id u_host(id self, SEL s) { (void)s; const char *u = rt_str_utf8(((struct nsurl *)self)->str); if (!strstr(u, "://")) return NULL; const char *p = after_scheme(u); size_t n = strcspn(p, "/:?#"); char *t = strndup(p, n); id r = rt_str(t); free(t); return r; }
static id u_query(id self, SEL s) { (void)s; const char *u = rt_str_utf8(((struct nsurl *)self)->str); const char *q = strchr(u, '?'); if (!q) return NULL; size_t n = strcspn(q + 1, "#"); char *t = strndup(q + 1, n); id r = rt_str(t); free(t); return r; }
static rt_bool u_isFile(id self, SEL s) { (void)s; return ((struct nsurl *)self)->file; }
static id u_lastPath(id self, SEL s) { return RT_SEND0(id, u_path(self, s), "lastPathComponent"); }
static id u_pathExt(id self, SEL s) { return RT_SEND0(id, u_path(self, s), "pathExtension"); }
static id u_appending(id self, SEL s, id comp) {
    struct nsurl *u = (struct nsurl *)self;
    if (u->file) return u_fileURL(RT_CLASS(NSURL), s, RT_SEND1(id, u_path(self, s), "stringByAppendingPathComponent:", id, comp));
    char b[4200]; const char *a = rt_str_utf8(u->str); size_t n = strlen(a);
    snprintf(b, sizeof b, "%s%s%s", a, n && a[n - 1] == '/' ? "" : "/", rt_str_utf8(comp));
    return objc_autorelease(url_make(NULL, b, 0));
}
static id u_appendingDir(id self, SEL s, id comp, long d) { (void)d; return u_appending(self, s, comp); }
static id u_deletingLast(id self, SEL s) {
    struct nsurl *u = (struct nsurl *)self;
    if (u->file) return u_fileURL(RT_CLASS(NSURL), s, RT_SEND0(id, u_path(self, s), "stringByDeletingLastPathComponent"));
    return self;
}
static id u_self(id self, SEL s) { (void)s; return self; }
static const char *u_fsrep(id self, SEL s) { return rt_str_utf8(u_path(self, s)); }
static rt_bool u_isEqual(id self, SEL s, id o) { (void)s; return o && rt_is_kind_of(o, RT_CLASS(NSURL)) && (RT_SEND1(long, ((struct nsurl *)self)->str, "isEqualToString:", id, ((struct nsurl *)o)->str) & 0xff); }
static unsigned long u_hash(id self, SEL s) { (void)s; return RT_SEND0(unsigned long, ((struct nsurl *)self)->str, "hash"); }
static id u_copy(id self, SEL s, void *z) { (void)s; (void)z; return objc_retain(self); }
static rt_bool u_checkReachable(id self, SEL s, id *err) { (void)s; if (err) *err = NULL; return access(rt_str_utf8(u_path(self, NULL)), F_OK) == 0; }

/* ===================================================================== NSError */
struct nserror { Class isa; id domain; long code; id userInfo; };
static id er_make(Class c, SEL s, id domain, long code, id info) {
    (void)s; id o = rt_alloc_instance(c, 0); struct nserror *e = (struct nserror *)o;
    e->domain = objc_retain(domain); e->code = code; e->userInfo = objc_retain(info); return objc_autorelease(o);
}
static id er_init(id self, SEL s, id domain, long code, id info) { (void)s; struct nserror *e = (struct nserror *)self; e->domain = objc_retain(domain); e->code = code; e->userInfo = objc_retain(info); return self; }
static id er_domain(id self, SEL s) { (void)s; return ((struct nserror *)self)->domain; }
static long er_code(id self, SEL s) { (void)s; return ((struct nserror *)self)->code; }
static id er_userInfo(id self, SEL s) { (void)s; return ((struct nserror *)self)->userInfo; }
static id er_desc(id self, SEL s) {
    (void)s; struct nserror *e = (struct nserror *)self;
    id d = e->userInfo ? rt_dict_get(e->userInfo, rt_str("NSLocalizedDescription")) : NULL;
    if (d) return d;
    char b[512]; snprintf(b, sizeof b, "The operation couldn't be completed. (%s error %ld.)", e->domain ? rt_str_utf8(e->domain) : "?", e->code);
    return rt_str(b);
}

/* ===================================================================== NSValue */
struct nsvalue { Class isa; char type[64]; uint8_t bytes[128]; };
static id val_make(const void *b, size_t n, const char *type) {
    id o = rt_alloc_instance(RT_CLASS(NSValue), 0); struct nsvalue *v = (struct nsvalue *)o;
    snprintf(v->type, sizeof v->type, "%s", type); memcpy(v->bytes, b, n > 128 ? 128 : n); return objc_autorelease(o);
}
static id v_withRect(Class c, SEL s, CGRect r) { (void)c; (void)s; return val_make(&r, sizeof r, "{CGRect={CGPoint=dd}{CGSize=dd}}"); }
static id v_withPoint(Class c, SEL s, CGPoint p) { (void)c; (void)s; return val_make(&p, sizeof p, "{CGPoint=dd}"); }
static id v_withSize(Class c, SEL s, CGSize p) { (void)c; (void)s; return val_make(&p, sizeof p, "{CGSize=dd}"); }
static id v_withRange(Class c, SEL s, NSRange p) { (void)c; (void)s; return val_make(&p, sizeof p, "{_NSRange=QQ}"); }
static id v_withPointer(Class c, SEL s, const void *p) { (void)c; (void)s; return val_make(&p, sizeof p, "^v"); }
static id v_withEdgeInsets(Class c, SEL s, double a, double b, double cc, double d) { (void)c; (void)s; double e[4] = { a, b, cc, d }; return val_make(e, sizeof e, "{UIEdgeInsets=dddd}"); }
static id v_withAffine(Class c, SEL s, const double *t) { (void)c; (void)s; return val_make(t, 48, "{CGAffineTransform=dddddd}"); }
static id v_withBytes(Class c, SEL s, const void *b, const char *type) { (void)c; (void)s; struct tinfo t; parse_type(type, &t); return val_make(b, t.size, type); }
static CGRect v_rect(id self, SEL s) { (void)s; CGRect r; memcpy(&r, ((struct nsvalue *)self)->bytes, sizeof r); return r; }
static CGPoint v_point(id self, SEL s) { (void)s; CGPoint r; memcpy(&r, ((struct nsvalue *)self)->bytes, sizeof r); return r; }
static CGSize v_size(id self, SEL s) { (void)s; CGSize r; memcpy(&r, ((struct nsvalue *)self)->bytes, sizeof r); return r; }
static NSRange v_range(id self, SEL s) { (void)s; NSRange r; memcpy(&r, ((struct nsvalue *)self)->bytes, sizeof r); return r; }
static void *v_pointer(id self, SEL s) { (void)s; void *p; memcpy(&p, ((struct nsvalue *)self)->bytes, sizeof p); return p; }
static void v_getValue(id self, SEL s, void *out) { (void)s; struct nsvalue *v = (struct nsvalue *)self; struct tinfo t; parse_type(v->type, &t); memcpy(out, v->bytes, t.size > 128 ? 128 : t.size); }
static const char *v_objCType(id self, SEL s) { (void)s; return ((struct nsvalue *)self)->type; }
static rt_bool v_isEqual(id self, SEL s, id o) { (void)s; return o && o->isa == self->isa && !memcmp(((struct nsvalue *)self)->bytes, ((struct nsvalue *)o)->bytes, 128); }
static id v_copy(id self, SEL s, void *z) { (void)s; (void)z; return objc_retain(self); }

/* ======================================================================= locks */
struct nslock { Class isa; pthread_mutex_t m; pthread_cond_t c; id name; };
static id lk_init(id self, SEL s) { (void)s; struct nslock *l = (struct nslock *)self; pthread_mutex_init(&l->m, NULL); pthread_cond_init(&l->c, NULL); return self; }
static id rlk_init(id self, SEL s) { (void)s; struct nslock *l = (struct nslock *)self; pthread_mutexattr_t a; pthread_mutexattr_init(&a); pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE); pthread_mutex_init(&l->m, &a); pthread_cond_init(&l->c, NULL); return self; }
static void lk_lock(id self, SEL s) { (void)s; pthread_mutex_lock(&((struct nslock *)self)->m); }
static void lk_unlock(id self, SEL s) { (void)s; pthread_mutex_unlock(&((struct nslock *)self)->m); }
static rt_bool lk_try(id self, SEL s) { (void)s; return pthread_mutex_trylock(&((struct nslock *)self)->m) == 0; }
static rt_bool lk_lockBefore(id self, SEL s, id date) { (void)date; lk_lock(self, s); return 1; }
static void lk_wait(id self, SEL s) { (void)s; struct nslock *l = (struct nslock *)self; pthread_cond_wait(&l->c, &l->m); }
static rt_bool lk_waitUntil(id self, SEL s, id date) {
    (void)s; struct nslock *l = (struct nslock *)self;
    double secs = date ? RT_SEND0(double, date, "timeIntervalSinceNow") : 0; if (secs < 0) secs = 0;
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)secs; ts.tv_nsec += (long)((secs - floor(secs)) * 1e9); if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    return pthread_cond_timedwait(&l->c, &l->m, &ts) == 0;
}
static void lk_signal(id self, SEL s) { (void)s; pthread_cond_signal(&((struct nslock *)self)->c); }
static void lk_broadcast(id self, SEL s) { (void)s; pthread_cond_broadcast(&((struct nslock *)self)->c); }
static void lk_setName(id self, SEL s, id n) { (void)s; struct nslock *l = (struct nslock *)self; objc_retain(n); objc_release(l->name); l->name = n; }
static void lk_dealloc(id self, SEL s) { (void)s; struct nslock *l = (struct nslock *)self; pthread_mutex_destroy(&l->m); pthread_cond_destroy(&l->c); objc_release(l->name); rt_dealloc_instance(self); }

/* ============================================================ NSIndexPath / NSIndexSet */
struct nsip { Class isa; unsigned long n; unsigned long idx[8]; };
static id ip_make(const unsigned long *v, unsigned long n) { id o = rt_alloc_instance(RT_CLASS(NSIndexPath), 0); struct nsip *p = (struct nsip *)o; p->n = n > 8 ? 8 : n; memcpy(p->idx, v, p->n * sizeof *v); return objc_autorelease(o); }
static id ip_rowSection(Class c, SEL s, long row, long sec) { (void)c; (void)s; unsigned long v[2] = { (unsigned long)sec, (unsigned long)row }; return ip_make(v, 2); }
static id ip_withIndex(Class c, SEL s, unsigned long i) { (void)c; (void)s; return ip_make(&i, 1); }
static id ip_withIndexes(Class c, SEL s, const unsigned long *v, unsigned long n) { (void)c; (void)s; return ip_make(v, n); }
static long ip_row(id self, SEL s) { (void)s; struct nsip *p = (struct nsip *)self; return p->n > 1 ? (long)p->idx[1] : 0; }
static long ip_section(id self, SEL s) { (void)s; struct nsip *p = (struct nsip *)self; return p->n > 0 ? (long)p->idx[0] : 0; }
static unsigned long ip_length(id self, SEL s) { (void)s; return ((struct nsip *)self)->n; }
static unsigned long ip_indexAt(id self, SEL s, unsigned long i) { (void)s; struct nsip *p = (struct nsip *)self; return i < p->n ? p->idx[i] : 0x7fffffffffffffffUL; }
static long ip_compare(id self, SEL s, id o) {
    (void)s; struct nsip *a = (struct nsip *)self, *b = (struct nsip *)o;
    for (unsigned long i = 0; i < a->n && i < b->n; i++) if (a->idx[i] != b->idx[i]) return a->idx[i] < b->idx[i] ? -1 : 1;
    return a->n == b->n ? 0 : a->n < b->n ? -1 : 1;
}
static rt_bool ip_isEqual(id self, SEL s, id o) { return o && rt_is_kind_of(o, RT_CLASS(NSIndexPath)) && ip_compare(self, s, o) == 0; }
static unsigned long ip_hash(id self, SEL s) { (void)s; struct nsip *p = (struct nsip *)self; unsigned long h = p->n; for (unsigned long i = 0; i < p->n; i++) h = h * 31 + p->idx[i]; return h; }
static id ip_copy(id self, SEL s, void *z) { (void)s; (void)z; return objc_retain(self); }
static id ip_desc(id self, SEL s) { (void)s; struct nsip *p = (struct nsip *)self; char b[256]; int o = snprintf(b, sizeof b, "<NSIndexPath> {length = %lu, path = ", p->n); for (unsigned long i = 0; i < p->n; i++) o += snprintf(b + o, sizeof b - (size_t)o, "%s%lu", i ? " - " : "", p->idx[i]); snprintf(b + o, sizeof b - (size_t)o, "}"); return rt_str(b); }

struct nsis { Class isa; unsigned long *v; unsigned long n, cap; };
static void is_add(struct nsis *s, unsigned long i) {
    unsigned long k = 0; while (k < s->n && s->v[k] < i) k++;
    if (k < s->n && s->v[k] == i) return;
    if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 16; s->v = realloc(s->v, s->cap * sizeof *s->v); }
    memmove(s->v + k + 1, s->v + k, (s->n - k) * sizeof *s->v); s->v[k] = i; s->n++;
}
static void is_remove(struct nsis *s, unsigned long i) { for (unsigned long k = 0; k < s->n; k++) if (s->v[k] == i) { memmove(s->v + k, s->v + k + 1, (s->n - k - 1) * sizeof *s->v); s->n--; return; } }
static id is_set(Class c, SEL s) { (void)s; return objc_autorelease(rt_alloc_instance(c, 0)); }
static id is_withIndex(Class c, SEL s, unsigned long i) { id o = is_set(c, s); is_add((struct nsis *)o, i); return o; }
static id is_withRange(Class c, SEL s, NSRange r) { id o = is_set(c, s); for (unsigned long i = 0; i < r.length && i < 1000000; i++) is_add((struct nsis *)o, r.location + i); return o; }
static void is_dealloc(id self, SEL s) { (void)s; free(((struct nsis *)self)->v); rt_dealloc_instance(self); }
static unsigned long is_count(id self, SEL s) { (void)s; return ((struct nsis *)self)->n; }
static unsigned long is_first(id self, SEL s) { (void)s; struct nsis *x = (struct nsis *)self; return x->n ? x->v[0] : 0x7fffffffffffffffUL; }
static unsigned long is_last(id self, SEL s) { (void)s; struct nsis *x = (struct nsis *)self; return x->n ? x->v[x->n - 1] : 0x7fffffffffffffffUL; }
static rt_bool is_contains(id self, SEL s, unsigned long i) { (void)s; struct nsis *x = (struct nsis *)self; for (unsigned long k = 0; k < x->n; k++) if (x->v[k] == i) return 1; return 0; }
static unsigned long is_greater(id self, SEL s, unsigned long i) { (void)s; struct nsis *x = (struct nsis *)self; for (unsigned long k = 0; k < x->n; k++) if (x->v[k] > i) return x->v[k]; return 0x7fffffffffffffffUL; }
static void is_addIndex(id self, SEL s, unsigned long i) { (void)s; is_add((struct nsis *)self, i); }
static void is_removeIndex(id self, SEL s, unsigned long i) { (void)s; is_remove((struct nsis *)self, i); }
static void is_addRange(id self, SEL s, NSRange r) { (void)s; for (unsigned long i = 0; i < r.length; i++) is_add((struct nsis *)self, r.location + i); }
static void is_removeAll(id self, SEL s) { (void)s; ((struct nsis *)self)->n = 0; }
static void is_enumerate(id self, SEL s, void *blk) {
    (void)s; struct nsis *x = (struct nsis *)self; bool stop = 0;
    for (unsigned long k = 0; k < x->n && !stop; k++) ((void (*)(void *, unsigned long, bool *))((struct rt_block *)blk)->invoke)(blk, x->v[k], &stop);
}
static id is_copy(id self, SEL s, void *z) { (void)s; (void)z; struct nsis *x = (struct nsis *)self; id o = rt_alloc_instance(RT_CLASS(NSIndexSet), 0); for (unsigned long k = 0; k < x->n; k++) is_add((struct nsis *)o, x->v[k]); return o; }
static id is_mcopy(id self, SEL s, void *z) { (void)s; (void)z; struct nsis *x = (struct nsis *)self; id o = rt_alloc_instance(RT_CLASS(NSMutableIndexSet), 0); for (unsigned long k = 0; k < x->n; k++) is_add((struct nsis *)o, x->v[k]); return o; }

/* ===================================================================== NSUUID */
struct nsuuid { Class isa; uint8_t b[16]; };
void iosrt_arc4random_buf(void *b, size_t n);
static id uu_init(id self, SEL s) { (void)s; struct nsuuid *u = (struct nsuuid *)self; iosrt_arc4random_buf(u->b, 16); u->b[6] = (uint8_t)((u->b[6] & 0x0f) | 0x40); u->b[8] = (uint8_t)((u->b[8] & 0x3f) | 0x80); return self; }
static id uu_uuid(Class c, SEL s) { return objc_autorelease(uu_init(rt_alloc_instance(c, 0), s)); }
static id uu_string(id self, SEL s) {
    (void)s; struct nsuuid *u = (struct nsuuid *)self; char b[40];
    snprintf(b, sizeof b, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", u->b[0], u->b[1], u->b[2], u->b[3], u->b[4], u->b[5], u->b[6], u->b[7], u->b[8], u->b[9], u->b[10], u->b[11], u->b[12], u->b[13], u->b[14], u->b[15]);
    return rt_str(b);
}
static void uu_getBytes(id self, SEL s, uint8_t *out) { (void)s; memcpy(out, ((struct nsuuid *)self)->b, 16); }

/* ============================================================ NSCache / NSMapTable */
struct nscache { Class isa; id dict; id name; id delegate; long limit; };
static id ca_init(id self, SEL s) { (void)s; ((struct nscache *)self)->dict = rt_dict_new(NULL, NULL, 0); return self; }
static void ca_dealloc(id self, SEL s) { (void)s; struct nscache *c = (struct nscache *)self; objc_release(c->dict); objc_release(c->name); rt_dealloc_instance(self); }
static id ca_get(id self, SEL s, id k) { (void)s; struct nscache *c = (struct nscache *)self; if (!c->dict) ca_init(self, s); return k ? rt_dict_get(c->dict, k) : NULL; }
static void ca_set(id self, SEL s, id v, id k) { (void)s; struct nscache *c = (struct nscache *)self; if (!c->dict) ca_init(self, s); if (k && v) rt_dict_set(c->dict, k, v); }
static void ca_setCost(id self, SEL s, id v, id k, unsigned long cost) { (void)cost; ca_set(self, s, v, k); }
static void ca_remove(id self, SEL s, id k) { struct nscache *c = (struct nscache *)self; if (!c->dict) ca_init(self, s); if (k) RT_SEND1(void, c->dict, "removeObjectForKey:", id, k); }
static void ca_removeAll(id self, SEL s) { struct nscache *c = (struct nscache *)self; if (!c->dict) ca_init(self, s); RT_SEND0(void, c->dict, "removeAllObjects"); }
static void ca_setL(id self, SEL s, long v) { (void)s; ((struct nscache *)self)->limit = v; }
static void ca_setName(id self, SEL s, id n) { (void)s; struct nscache *c = (struct nscache *)self; objc_retain(n); objc_release(c->name); c->name = n; }
static void ca_setDelegate(id self, SEL s, id d) { (void)s; ((struct nscache *)self)->delegate = d; }
static unsigned long ca_count(id self, SEL s) { struct nscache *c = (struct nscache *)self; if (!c->dict) ca_init(self, s); return RT_SEND0(unsigned long, c->dict, "count"); }
static id ca_keyEnum(id self, SEL s) { struct nscache *c = (struct nscache *)self; if (!c->dict) ca_init(self, s); return RT_SEND0(id, c->dict, "keyEnumerator"); }
static id ca_objEnum(id self, SEL s) { struct nscache *c = (struct nscache *)self; if (!c->dict) ca_init(self, s); return RT_SEND0(id, RT_SEND0(id, c->dict, "allValues"), "objectEnumerator"); }
static id ca_dictRep(id self, SEL s) { struct nscache *c = (struct nscache *)self; if (!c->dict) ca_init(self, s); return RT_SEND0(id, c->dict, "copy"); }
static id mt_make(Class c, SEL s) { return objc_autorelease(ca_init(rt_alloc_instance(c, 0), s)); }
static id mt_makeOpts(Class c, SEL s, unsigned long a, unsigned long b) { (void)a; (void)b; return mt_make(c, s); }
static id mt_initOpts(id self, SEL s, unsigned long a, unsigned long b, unsigned long cap) { (void)a; (void)b; (void)cap; return ca_init(self, s); }

/* ============================================================== NSPointerArray */
struct nsparr { Class isa; void **v; unsigned long n, cap; };
static id pa_make(Class c, SEL s) { (void)s; return objc_autorelease(rt_alloc_instance(c, 0)); }
static id pa_initOpts(id self, SEL s, unsigned long o) { (void)s; (void)o; return self; }
static void pa_add(id self, SEL s, void *p) { (void)s; struct nsparr *a = (struct nsparr *)self; if (a->n == a->cap) { a->cap = a->cap ? a->cap * 2 : 8; a->v = realloc(a->v, a->cap * sizeof *a->v); } a->v[a->n++] = p; }
static void *pa_at(id self, SEL s, unsigned long i) { (void)s; struct nsparr *a = (struct nsparr *)self; return i < a->n ? a->v[i] : NULL; }
static void pa_removeAt(id self, SEL s, unsigned long i) { (void)s; struct nsparr *a = (struct nsparr *)self; if (i < a->n) { memmove(a->v + i, a->v + i + 1, (a->n - i - 1) * sizeof *a->v); a->n--; } }
static void pa_replace(id self, SEL s, unsigned long i, void *p) { (void)s; struct nsparr *a = (struct nsparr *)self; if (i < a->n) a->v[i] = p; }
static void pa_insert(id self, SEL s, void *p, unsigned long i) { struct nsparr *a = (struct nsparr *)self; pa_add(self, s, NULL); if (i < a->n) { memmove(a->v + i + 1, a->v + i, (a->n - 1 - i) * sizeof *a->v); a->v[i] = p; } }
static unsigned long pa_count(id self, SEL s) { (void)s; return ((struct nsparr *)self)->n; }
static void pa_setCount(id self, SEL s, unsigned long n) { struct nsparr *a = (struct nsparr *)self; while (a->n < n) pa_add(self, s, NULL); if (n < a->n) a->n = n; }
static void pa_compact(id self, SEL s) { (void)s; struct nsparr *a = (struct nsparr *)self; unsigned long k = 0; for (unsigned long i = 0; i < a->n; i++) if (a->v[i]) a->v[k++] = a->v[i]; a->n = k; }
static id pa_all(id self, SEL s) { (void)s; struct nsparr *a = (struct nsparr *)self; id r = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array"); for (unsigned long i = 0; i < a->n; i++) if (a->v[i]) RT_SEND1(void, r, "addObject:", id, (id)a->v[i]); return r; }
static void pa_dealloc(id self, SEL s) { (void)s; free(((struct nsparr *)self)->v); rt_dealloc_instance(self); }

/* ========================================================= NSOperation / Queue */
struct nsop { Class isa; id blocks; long finished, cancelled, executing; void *completion; id queue; };
struct nsopq { Class isa; long main; long max; id name; long ops; pthread_mutex_t m; pthread_cond_t c; };
static void op_runBlocks(id self) {
    struct nsop *o = (struct nsop *)self;
    for (size_t i = 0; i < rt_array_count(o->blocks); i++) { void *b = rt_array_at(o->blocks, i); ((void (*)(void *))((struct rt_block *)b)->invoke)(b); }
}
static void op_main(id self, SEL s) { (void)s; if (((struct nsop *)self)->blocks) op_runBlocks(self); }
static void op_start(id self, SEL s) {
    (void)s; struct nsop *o = (struct nsop *)self;
    if (!o->cancelled) { o->executing = 1; RT_SEND0(void, self, "main"); o->executing = 0; }
    o->finished = 1;
    if (o->completion) ((void (*)(void *))((struct rt_block *)o->completion)->invoke)(o->completion);
}
static void op_cancel(id self, SEL s) { (void)s; ((struct nsop *)self)->cancelled = 1; }
static rt_bool op_isCancelled(id self, SEL s) { (void)s; return ((struct nsop *)self)->cancelled; }
static rt_bool op_isFinished(id self, SEL s) { (void)s; return ((struct nsop *)self)->finished; }
static rt_bool op_isExecuting(id self, SEL s) { (void)s; return ((struct nsop *)self)->executing; }
static void op_setCompletion(id self, SEL s, void *b) { (void)s; ((struct nsop *)self)->completion = b ? _Block_copy(b) : NULL; }
static void op_noopL(id self, SEL s, long v) { (void)self; (void)s; (void)v; }
static void op_waitUntilFinished(id self, SEL s) { (void)s; while (!((struct nsop *)self)->finished) usleep(1000); }
static void op_addDependency(id self, SEL s, id o) { (void)self; (void)s; (void)o; }
static id bop_withBlock(Class c, SEL s, void *b) {
    (void)s; id o = rt_alloc_instance(c, 0); struct nsop *op = (struct nsop *)o;
    op->blocks = RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "alloc"), "init");
    id cb = (id)_Block_copy(b); RT_SEND1(void, op->blocks, "addObject:", id, cb); objc_release(cb);
    return objc_autorelease(o);
}
static void bop_add(id self, SEL s, void *b) {
    (void)s; struct nsop *op = (struct nsop *)self;
    if (!op->blocks) op->blocks = RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "alloc"), "init");
    id cb = (id)_Block_copy(b); RT_SEND1(void, op->blocks, "addObject:", id, cb); objc_release(cb);
}
static id g_mainq;
static id oq_init(id self, SEL s) { (void)s; struct nsopq *q = (struct nsopq *)self; pthread_mutex_init(&q->m, NULL); pthread_cond_init(&q->c, NULL); q->max = -1; return self; }
static id oq_main(Class c, SEL s) { if (!g_mainq) { g_mainq = oq_init(rt_alloc_instance(c, 0), s); ((struct nsopq *)g_mainq)->main = 1; } return g_mainq; }
static id oq_current(Class c, SEL s) { return rt_is_guest_main_thread() ? oq_main(c, s) : NULL; }
struct op_job { id q; id op; };
static void op_job_run(void *p) {
    struct op_job *j = p; struct nsopq *q = (struct nsopq *)j->q;
    void *pool = objc_autoreleasePoolPush();
    RT_SEND0(void, j->op, "start");
    objc_autoreleasePoolPop(pool);
    pthread_mutex_lock(&q->m); q->ops--; pthread_cond_broadcast(&q->c); pthread_mutex_unlock(&q->m);
    objc_release(j->op); free(j);
}
void iosrt_dispatch_async_f(void *q, void *ctx, void (*fn)(void *));
static void oq_addOp(id self, SEL s, id op) {
    (void)s; struct nsopq *q = (struct nsopq *)self;
    struct op_job *j = rt_xmalloc(sizeof *j); j->q = self; j->op = objc_retain(op);
    pthread_mutex_lock(&q->m); q->ops++; pthread_mutex_unlock(&q->m);
    iosrt_dispatch_async_f(q->main ? (void *)&iosrt_dispatch_main_q : iosrt_dispatch_get_global_queue(0, 0), j, op_job_run);
}
static void oq_addBlock(id self, SEL s, void *b) { oq_addOp(self, s, bop_withBlock(RT_CLASS(NSBlockOperation), NULL, b)); }
static void oq_addOps(id self, SEL s, id ops, long wait) { for (size_t i = 0; i < rt_array_count(ops); i++) oq_addOp(self, s, rt_array_at(ops, i)); (void)wait; }
static void oq_waitAll(id self, SEL s) { (void)s; struct nsopq *q = (struct nsopq *)self; if (q->main && rt_is_guest_main_thread()) { rt_runloop_drain_main_queue(); return; } pthread_mutex_lock(&q->m); while (q->ops > 0) pthread_cond_wait(&q->c, &q->m); pthread_mutex_unlock(&q->m); }
static void oq_setMax(id self, SEL s, long m) { (void)s; ((struct nsopq *)self)->max = m; }
static long oq_count(id self, SEL s) { (void)s; return ((struct nsopq *)self)->ops; }
static void oq_setName(id self, SEL s, id n) { (void)s; struct nsopq *q = (struct nsopq *)self; objc_retain(n); objc_release(q->name); q->name = n; }
static void oq_noop(id self, SEL s) { (void)self; (void)s; }

/* ========================================================== NSAssertionHandler */
static id g_assert;
static id ah_current(Class c, SEL s) { (void)s; if (!g_assert) g_assert = rt_alloc_instance(c, 0); return g_assert; }
void rt_NSAssertionHandler_handleFailure(id self, SEL s, SEL method, id obj, id file, long line, id fmt, const uint64_t *args) {
    (void)self; (void)s; (void)obj;
    rt_sb sb; rt_sb_init(&sb); struct rt_va va = { args };
    rt_sb_printf(&sb, "*** Assertion failure in %s, %s:%ld: ", method ? method : "?", file ? rt_str_utf8(file) : "?", line);
    if (fmt) rt_format(&sb, rt_str_utf8(fmt), &va, 1);
    char *m = rt_sb_take(&sb); rt_soft_error(m); free(m);
}
void rt_NSAssertionHandler_handleFailureFn(id self, SEL s, id fn, id file, long line, id fmt, const uint64_t *args) {
    (void)self; (void)s;
    rt_sb sb; rt_sb_init(&sb); struct rt_va va = { args };
    rt_sb_printf(&sb, "*** Assertion failure in %s, %s:%ld: ", fn ? rt_str_utf8(fn) : "?", file ? rt_str_utf8(file) : "?", line);
    if (fmt) rt_format(&sb, rt_str_utf8(fmt), &va, 1);
    char *m = rt_sb_take(&sb); rt_soft_error(m); free(m);
}

/* ========================================================================= init */
void rt_foundation_more_init(void) {
    Class o = RT_CLASS(NSObject);
    /* NSObject additions: signatures, forwarding, KVC/KVO stubs */
    IM(o, "methodSignatureForSelector:", o_methodSignature); CM(o, "methodSignatureForSelector:", o_cmethodSignature);
    CM(o, "instanceMethodSignatureForSelector:", o_instanceMethodSignature);
    IM(o, "forwardInvocation:", o_forwardInvocation);
    rt_default_forward_imp = (IMP)o_forwardInvocation;
    IM(o, "valueForKey:", o_valueForKey); IM(o, "setValue:forKey:", o_setValueForKey);
    IM(o, "valueForKeyPath:", o_valueForKeyPath); IM(o, "setValue:forKeyPath:", o_setValueForKeyPath);
    IM(o, "setValuesForKeysWithDictionary:", o_setValuesForKeys);
    IM(o, "addObserver:forKeyPath:options:context:", o_kvo_noop); IM(o, "removeObserver:forKeyPath:", o_kvo_remove);
    IM(o, "willChangeValueForKey:", o_willChange); IM(o, "didChangeValueForKey:", o_willChange);

    NEWCLASS(NSMethodSignature, o, sizeof(struct nssig) - sizeof(void *));
    Class sg = RT_CLASS(NSMethodSignature);
    CM(sg, "signatureWithObjCTypes:", sig_withTypes); IM(sg, "numberOfArguments", sig_numArgs);
    IM(sg, "getArgumentTypeAtIndex:", sig_argType); IM(sg, "methodReturnType", sig_retType);
    IM(sg, "methodReturnLength", sig_retLen); IM(sg, "frameLength", sig_frameLen); IM(sg, "isOneway", sig_oneway);
    NEWCLASS(NSInvocation, o, sizeof(struct nsinv) - sizeof(void *));
    Class iv = RT_CLASS(NSInvocation);
    CM(iv, "invocationWithMethodSignature:", inv_withSig); IM(iv, "dealloc", inv_dealloc); IM(iv, "methodSignature", inv_sig);
    IM(iv, "setArgument:atIndex:", inv_setArg); IM(iv, "getArgument:atIndex:", inv_getArg);
    IM(iv, "target", inv_target); IM(iv, "setTarget:", inv_setTarget); IM(iv, "selector", inv_selector); IM(iv, "setSelector:", inv_setSelector);
    IM(iv, "retainArguments", inv_retainArgs); IM(iv, "argumentsRetained", inv_argsRetained);
    IM(iv, "getReturnValue:", inv_getRet); IM(iv, "setReturnValue:", inv_setRet);
    IM(iv, "invoke", inv_invoke); IM(iv, "invokeWithTarget:", inv_invokeWithTarget);

    /* NSProxy: a second root class */
    rt_native_class(RT_CLASS(NSProxy), &iosrt_meta_NSProxy, "NSProxy", NULL, sizeof(void *));
    Class px = RT_CLASS(NSProxy);
    CM(px, "alloc", px_alloc); CM(px, "allocWithZone:", px_alloc); CM(px, "class", px_cclass);
    CM(px, "respondsToSelector:", px_respondsToSelector); CM(px, "description", px_description);
    IM(px, "dealloc", px_dealloc); IM(px, "retain", px_retain); IM(px, "release", px_release); IM(px, "autorelease", px_autorelease);
    IM(px, "class", px_class); IM(px, "description", px_description); IM(px, "isProxy", px_isProxy); IM(px, "self", px_self);
    IM(px, "hash", px_hash); IM(px, "isEqual:", px_isEqual); IM(px, "forwardInvocation:", o_forwardInvocation);
    IM(px, "methodSignatureForSelector:", o_methodSignature);

    NEWCLASS(NSURL, o, sizeof(struct nsurl) - sizeof(void *));
    Class u = RT_CLASS(NSURL);
    CM(u, "URLWithString:", u_withString); CM(u, "URLWithString:relativeToURL:", u_withStringRel);
    CM(u, "fileURLWithPath:", u_fileURL); CM(u, "fileURLWithPath:isDirectory:", u_fileURLDir);
    IM(u, "initWithString:", u_initWithString); IM(u, "initFileURLWithPath:", u_initFileURL); IM(u, "initFileURLWithPath:isDirectory:", u_initFileURLDir); IM(u, "initWithString:relativeToURL:", u_initWithStringRel); IM(u, "dealloc", u_dealloc);
    IM(u, "absoluteString", u_absoluteString); IM(u, "description", u_absoluteString); IM(u, "relativeString", u_absoluteString);
    IM(u, "path", u_path); IM(u, "relativePath", u_path); IM(u, "scheme", u_scheme); IM(u, "host", u_host); IM(u, "query", u_query);
    IM(u, "isFileURL", u_isFile); IM(u, "lastPathComponent", u_lastPath); IM(u, "pathExtension", u_pathExt);
    IM(u, "URLByAppendingPathComponent:", u_appending); IM(u, "URLByAppendingPathComponent:isDirectory:", u_appendingDir);
    IM(u, "URLByDeletingLastPathComponent", u_deletingLast); IM(u, "absoluteURL", u_self); IM(u, "standardizedURL", u_self);
    IM(u, "URLByResolvingSymlinksInPath", u_self); IM(u, "fileSystemRepresentation", u_fsrep);
    IM(u, "isEqual:", u_isEqual); IM(u, "hash", u_hash); IM(u, "copyWithZone:", u_copy);
    IM(u, "checkResourceIsReachableAndReturnError:", u_checkReachable);

    NEWCLASS(NSError, o, sizeof(struct nserror) - sizeof(void *));
    Class er = RT_CLASS(NSError);
    CM(er, "errorWithDomain:code:userInfo:", er_make); IM(er, "initWithDomain:code:userInfo:", er_init);
    IM(er, "domain", er_domain); IM(er, "code", er_code); IM(er, "userInfo", er_userInfo);
    IM(er, "localizedDescription", er_desc); IM(er, "description", er_desc); IM(er, "localizedFailureReason", er_desc);

    NEWCLASS(NSValue, o, sizeof(struct nsvalue) - sizeof(void *));
    Class v = RT_CLASS(NSValue);
    CM(v, "valueWithCGRect:", v_withRect); CM(v, "valueWithCGPoint:", v_withPoint); CM(v, "valueWithCGSize:", v_withSize);
    CM(v, "valueWithRange:", v_withRange); CM(v, "valueWithPointer:", v_withPointer); CM(v, "valueWithNonretainedObject:", v_withPointer);
    CM(v, "valueWithUIEdgeInsets:", v_withEdgeInsets); CM(v, "valueWithCGAffineTransform:", v_withAffine);
    CM(v, "valueWithBytes:objCType:", v_withBytes); CM(v, "value:withObjCType:", v_withBytes);
    IM(v, "CGRectValue", v_rect); IM(v, "CGPointValue", v_point); IM(v, "CGSizeValue", v_size); IM(v, "rangeValue", v_range);
    IM(v, "pointerValue", v_pointer); IM(v, "nonretainedObjectValue", v_pointer); IM(v, "getValue:", v_getValue);
    IM(v, "objCType", v_objCType); IM(v, "isEqual:", v_isEqual); IM(v, "isEqualToValue:", v_isEqual); IM(v, "copyWithZone:", v_copy);

    NEWCLASS(NSLock, o, sizeof(struct nslock) - sizeof(void *));
    NEWCLASS(NSRecursiveLock, o, sizeof(struct nslock) - sizeof(void *));
    NEWCLASS(NSCondition, o, sizeof(struct nslock) - sizeof(void *));
    Class locks[3] = { RT_CLASS(NSLock), RT_CLASS(NSRecursiveLock), RT_CLASS(NSCondition) };
    for (int i = 0; i < 3; i++) {
        Class l = locks[i];
        IM(l, "init", i == 1 ? rlk_init : lk_init); IM(l, "lock", lk_lock); IM(l, "unlock", lk_unlock); IM(l, "tryLock", lk_try);
        IM(l, "lockBeforeDate:", lk_lockBefore); IM(l, "setName:", lk_setName); IM(l, "dealloc", lk_dealloc);
    }
    IM(RT_CLASS(NSCondition), "wait", lk_wait); IM(RT_CLASS(NSCondition), "waitUntilDate:", lk_waitUntil);
    IM(RT_CLASS(NSCondition), "signal", lk_signal); IM(RT_CLASS(NSCondition), "broadcast", lk_broadcast);

    NEWCLASS(NSIndexPath, o, sizeof(struct nsip) - sizeof(void *));
    Class ip = RT_CLASS(NSIndexPath);
    CM(ip, "indexPathForRow:inSection:", ip_rowSection); CM(ip, "indexPathForItem:inSection:", ip_rowSection);
    CM(ip, "indexPathWithIndex:", ip_withIndex); CM(ip, "indexPathWithIndexes:length:", ip_withIndexes);
    IM(ip, "row", ip_row); IM(ip, "item", ip_row); IM(ip, "section", ip_section); IM(ip, "length", ip_length);
    IM(ip, "indexAtPosition:", ip_indexAt); IM(ip, "compare:", ip_compare); IM(ip, "isEqual:", ip_isEqual); IM(ip, "hash", ip_hash);
    IM(ip, "copyWithZone:", ip_copy); IM(ip, "description", ip_desc);

    NEWCLASS(NSIndexSet, o, sizeof(struct nsis) - sizeof(void *));
    NEWCLASS(NSMutableIndexSet, RT_CLASS(NSIndexSet), 0);
    Class is = RT_CLASS(NSIndexSet), mis = RT_CLASS(NSMutableIndexSet);
    CM(is, "indexSet", is_set); CM(is, "indexSetWithIndex:", is_withIndex); CM(is, "indexSetWithIndexesInRange:", is_withRange);
    IM(is, "dealloc", is_dealloc); IM(is, "count", is_count); IM(is, "firstIndex", is_first); IM(is, "lastIndex", is_last);
    IM(is, "containsIndex:", is_contains); IM(is, "indexGreaterThanIndex:", is_greater); IM(is, "enumerateIndexesUsingBlock:", is_enumerate);
    IM(is, "copyWithZone:", is_copy); IM(is, "mutableCopyWithZone:", is_mcopy);
    IM(mis, "addIndex:", is_addIndex); IM(mis, "removeIndex:", is_removeIndex); IM(mis, "addIndexesInRange:", is_addRange);
    IM(mis, "removeAllIndexes", is_removeAll);

    NEWCLASS(NSUUID, o, sizeof(struct nsuuid) - sizeof(void *));
    CM(RT_CLASS(NSUUID), "UUID", uu_uuid); IM(RT_CLASS(NSUUID), "init", uu_init); IM(RT_CLASS(NSUUID), "UUIDString", uu_string);
    IM(RT_CLASS(NSUUID), "getUUIDBytes:", uu_getBytes);

    NEWCLASS(NSCache, o, sizeof(struct nscache) - sizeof(void *));
    NEWCLASS(NSMapTable, o, sizeof(struct nscache) - sizeof(void *));
    Class caches[2] = { RT_CLASS(NSCache), RT_CLASS(NSMapTable) };
    for (int i = 0; i < 2; i++) {
        Class c = caches[i];
        IM(c, "init", ca_init); IM(c, "dealloc", ca_dealloc); IM(c, "objectForKey:", ca_get); IM(c, "setObject:forKey:", ca_set);
        IM(c, "removeObjectForKey:", ca_remove); IM(c, "removeAllObjects", ca_removeAll); IM(c, "count", ca_count);
    }
    IM(RT_CLASS(NSCache), "setObject:forKey:cost:", ca_setCost); IM(RT_CLASS(NSCache), "setCountLimit:", ca_setL);
    IM(RT_CLASS(NSCache), "setTotalCostLimit:", ca_setL); IM(RT_CLASS(NSCache), "setName:", ca_setName);
    IM(RT_CLASS(NSCache), "setDelegate:", ca_setDelegate); IM(RT_CLASS(NSCache), "setEvictsObjectsWithDiscardedContent:", ca_setL);
    Class mt = RT_CLASS(NSMapTable);
    CM(mt, "strongToStrongObjectsMapTable", mt_make); CM(mt, "weakToStrongObjectsMapTable", mt_make);
    CM(mt, "strongToWeakObjectsMapTable", mt_make); CM(mt, "weakToWeakObjectsMapTable", mt_make);
    CM(mt, "mapTableWithKeyOptions:valueOptions:", mt_makeOpts); IM(mt, "initWithKeyOptions:valueOptions:capacity:", mt_initOpts);
    IM(mt, "keyEnumerator", ca_keyEnum); IM(mt, "objectEnumerator", ca_objEnum); IM(mt, "dictionaryRepresentation", ca_dictRep);

    NEWCLASS(NSPointerArray, o, sizeof(struct nsparr) - sizeof(void *));
    Class pa = RT_CLASS(NSPointerArray);
    CM(pa, "weakObjectsPointerArray", pa_make); CM(pa, "strongObjectsPointerArray", pa_make); CM(pa, "pointerArrayWithOptions:", pa_make);
    IM(pa, "initWithOptions:", pa_initOpts); IM(pa, "addPointer:", pa_add); IM(pa, "pointerAtIndex:", pa_at);
    IM(pa, "removePointerAtIndex:", pa_removeAt); IM(pa, "replacePointerAtIndex:withPointer:", pa_replace);
    IM(pa, "insertPointer:atIndex:", pa_insert); IM(pa, "count", pa_count); IM(pa, "setCount:", pa_setCount);
    IM(pa, "compact", pa_compact); IM(pa, "allObjects", pa_all); IM(pa, "dealloc", pa_dealloc);

    NEWCLASS(NSOperation, o, sizeof(struct nsop) - sizeof(void *));
    NEWCLASS(NSBlockOperation, RT_CLASS(NSOperation), 0);
    NEWCLASS(NSOperationQueue, o, sizeof(struct nsopq) - sizeof(void *));
    Class op = RT_CLASS(NSOperation), bop = RT_CLASS(NSBlockOperation), oq = RT_CLASS(NSOperationQueue);
    IM(op, "start", op_start); IM(op, "main", op_main); IM(op, "cancel", op_cancel); IM(op, "isCancelled", op_isCancelled);
    IM(op, "isFinished", op_isFinished); IM(op, "isExecuting", op_isExecuting); IM(op, "setCompletionBlock:", op_setCompletion);
    IM(op, "setQueuePriority:", op_noopL); IM(op, "setQualityOfService:", op_noopL); IM(op, "waitUntilFinished", op_waitUntilFinished);
    IM(op, "addDependency:", op_addDependency);
    CM(bop, "blockOperationWithBlock:", bop_withBlock); IM(bop, "addExecutionBlock:", bop_add);
    CM(oq, "mainQueue", oq_main); CM(oq, "currentQueue", oq_current);
    IM(oq, "init", oq_init); IM(oq, "addOperation:", oq_addOp); IM(oq, "addOperationWithBlock:", oq_addBlock);
    IM(oq, "addOperations:waitUntilFinished:", oq_addOps); IM(oq, "waitUntilAllOperationsAreFinished", oq_waitAll);
    IM(oq, "setMaxConcurrentOperationCount:", oq_setMax); IM(oq, "operationCount", oq_count); IM(oq, "setName:", oq_setName);
    IM(oq, "cancelAllOperations", oq_noop); IM(oq, "setSuspended:", op_noopL); IM(oq, "setQualityOfService:", op_noopL);

    NEWCLASS(NSAssertionHandler, o, 0);
    extern char iosrt_imp_handleFailureInMethod[], iosrt_imp_handleFailureInFunction[];
    CM(RT_CLASS(NSAssertionHandler), "currentHandler", ah_current);
    IM(RT_CLASS(NSAssertionHandler), "handleFailureInMethod:object:file:lineNumber:description:", iosrt_imp_handleFailureInMethod);
    IM(RT_CLASS(NSAssertionHandler), "handleFailureInFunction:file:lineNumber:description:", iosrt_imp_handleFailureInFunction);
}
