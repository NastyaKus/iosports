/*
 * Foundation HLE, part 1: NSObject, NSString family, NSNumber, NSNull,
 * NSAutoreleasePool, NSLog and the NSStringFrom* helpers.
 * All classes are "native": their IMPs are runtime functions registered in
 * the same method tables the guest classes use, so guests can subclass them,
 * add categories and swizzle them.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <math.h>
#include <ctype.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")

RT_DEFINE_CLASS(NSObject);
RT_DEFINE_CLASS(NSString);
RT_DEFINE_CLASS(NSMutableString);
RT_DEFINE_CLASS(__NSCFConstantString);
RT_DEFINE_CLASS(NSNumber);
RT_DEFINE_CLASS(NSNull);
RT_DEFINE_CLASS(NSAutoreleasePool);
RT_DEFINE_CLASS(NSCharacterSet);
RT_DECLARE_CLASS(NSArray);
RT_DECLARE_CLASS(NSMutableArray);
RT_DECLARE_CLASS(NSData);

/* ================================================================ NSObject */
static id o_alloc(Class cls, SEL s) { (void)s; return rt_alloc_instance(cls, 0); }
static id o_allocWithZone(Class cls, SEL s, void *z) { (void)s; (void)z; return rt_alloc_instance(cls, 0); }
static id o_new(Class cls, SEL s) { (void)s; return RT_SEND0(id, RT_SEND0(id, (id)cls, "alloc"), "init"); }
static id o_init(id self, SEL s) { (void)s; return self; }
static void o_dealloc(id self, SEL s) { (void)s; rt_dealloc_instance(self); }
static id o_retain(id self, SEL s) { (void)s; return objc_retain(self); }
static void o_release(id self, SEL s) { (void)s; objc_release(self); }
static id o_autorelease(id self, SEL s) { (void)s; return objc_autorelease(self); }
static unsigned long o_retainCount(id self, SEL s) { (void)s; return (unsigned long)rt_retain_count(self); }
static id o_self(id self, SEL s) { (void)s; return self; }
static Class o_class(id self, SEL s) { (void)s; return self->isa; }
static Class o_cclass(Class self, SEL s) { (void)s; return self; }
static Class o_superclass(id self, SEL s) { (void)s; return self->isa->superclass; }
static Class o_csuperclass(Class self, SEL s) { (void)s; return self->superclass; }
static rt_bool o_isKindOfClass(id self, SEL s, Class c) { (void)s; return rt_is_kind_of(self, c); }
static rt_bool o_isMemberOfClass(id self, SEL s, Class c) { (void)s; return self->isa == c; }
static rt_bool o_respondsToSelector(id self, SEL s, SEL q) { (void)s; return rt_responds(self, q); }
static rt_bool o_cinstancesRespond(Class self, SEL s, SEL q) { (void)s; int f = 0; rt_lookup(self, q, &f); return f; }
static rt_bool o_cisSubclassOfClass(Class self, SEL s, Class c) { (void)s; for (Class k = self; k; k = k->superclass) if (k == c) return 1; return 0; }
int rt_class_conforms(Class c, const void *proto);
static rt_bool o_conformsToProtocol(id self, SEL s, const void *p) { (void)s; return p && rt_class_conforms(rt_class_is_meta(self->isa) ? (Class)self : self->isa, p); }
static id o_perform0(id self, SEL s, SEL q) { (void)s; return ((id (*)(id, SEL))objc_msgSend)(self, q); }
static id o_perform1(id self, SEL s, SEL q, id a) { (void)s; return ((id (*)(id, SEL, id))objc_msgSend)(self, q, a); }
static id o_perform2(id self, SEL s, SEL q, id a, id b) { (void)s; return ((id (*)(id, SEL, id, id))objc_msgSend)(self, q, a, b); }
static id o_description(id self, SEL s) {
    (void)s; char b[256];
    snprintf(b, sizeof b, "<%s: %p>", rt_class_name(self->isa), (void *)self);
    return rt_str(b);
}
static id o_cdescription(Class self, SEL s) { (void)s; return rt_str(rt_class_name(self)); }
static unsigned long o_hash(id self, SEL s) { (void)s; return (unsigned long)(uintptr_t)self >> 4; }
static rt_bool o_isEqual(id self, SEL s, id o) { (void)s; return self == o; }
static id o_copy(id self, SEL s) { (void)s; return ((id (*)(id, SEL, void *))objc_msgSend)(self, rt_sel("copyWithZone:"), NULL); }
static id o_mutableCopy(id self, SEL s) { (void)s; return ((id (*)(id, SEL, void *))objc_msgSend)(self, rt_sel("mutableCopyWithZone:"), NULL); }
static void *o_zone(id self, SEL s) { (void)self; (void)s; return NULL; }
static rt_bool o_isProxy(id self, SEL s) { (void)self; (void)s; return 0; }
static IMP o_methodForSelector(id self, SEL s, SEL q) { (void)s; int f; IMP i = rt_lookup(self->isa, q, &f); return i; }
static void o_doesNotRecognize(id self, SEL s, SEL q) {
    (void)s; char b[256]; snprintf(b, sizeof b, "-[%s %s]: unrecognized selector", rt_class_name(self->isa), q); rt_guest_exit(71, b);
}
static void o_cinitialize(Class c, SEL s) { (void)c; (void)s; }
static void o_performAfterDelay(id self, SEL s, SEL q, id arg, double delay);

/* ================================================================ strings */
struct nsstr { Class isa; unichar *u; size_t len, cap; char *utf8; long mut; };
struct cfconst { Class isa; uint64_t flags; const void *str; long len; };
struct sv { const uint8_t *a; const unichar *u; size_t len; unichar *owned; };

static int is_our_str(id s) {
    for (Class c = s->isa; c; c = c->superclass) {
        if (c == RT_CLASS(__NSCFConstantString)) return 0;
        if (c == RT_CLASS(NSString)) return 1;
    }
    return 0;
}
int rt_is_string(id o) {
    if (!o) return 0;
    for (Class c = o->isa; c; c = c->superclass) if (c == RT_CLASS(NSString)) return 1;
    return 0;
}
static void sv_get(id s, struct sv *v) {
    memset(v, 0, sizeof *v);
    if (!s) return;
    if (s->isa == RT_CLASS(__NSCFConstantString)) {
        struct cfconst *c = (struct cfconst *)s;
        if (c->flags & 0x10) v->u = c->str; else v->a = c->str;
        v->len = (size_t)c->len;
    } else if (is_our_str(s)) {
        struct nsstr *n = (struct nsstr *)s;
        v->u = n->u; v->len = n->len;
    } else {
        /* foreign NSString subclass: use the primitive methods */
        size_t n = RT_SEND0(unsigned long, s, "length");
        v->owned = rt_xmalloc((n + 1) * sizeof(unichar));
        for (size_t i = 0; i < n; i++) v->owned[i] = RT_SEND1(unsigned short, s, "characterAtIndex:", unsigned long, i);
        v->u = v->owned; v->len = n;
    }
}
static inline unichar sv_at(const struct sv *v, size_t i) { return v->a ? v->a[i] : v->u[i]; }
static void sv_free(struct sv *v) { free(v->owned); }

static size_t utf8_to_utf16(const char *s, size_t n, unichar *out) {
    size_t o = 0;
    const uint8_t *p = (const uint8_t *)s, *e = p + n;
    while (p < e) {
        uint32_t c = *p++;
        if (c >= 0xf0 && p + 2 < e + 0) { c = ((c & 7) << 18) | ((p[0] & 0x3f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f); p += 3; }
        else if (c >= 0xe0 && p + 1 < e) { c = ((c & 0xf) << 12) | ((p[0] & 0x3f) << 6) | (p[1] & 0x3f); p += 2; }
        else if (c >= 0xc0 && p < e) { c = ((c & 0x1f) << 6) | (p[0] & 0x3f); p += 1; }
        if (c >= 0x10000) { c -= 0x10000; if (out) { out[o] = (unichar)(0xd800 + (c >> 10)); out[o + 1] = (unichar)(0xdc00 + (c & 0x3ff)); } o += 2; }
        else { if (out) out[o] = (unichar)c; o++; }
    }
    return o;
}
static char *utf16_to_utf8(const struct sv *v) {
    rt_sb sb; rt_sb_init(&sb);
    for (size_t i = 0; i < v->len; i++) {
        uint32_t c = sv_at(v, i);
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < v->len) { uint32_t d = sv_at(v, i + 1); if (d >= 0xdc00 && d < 0xe000) { c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00); i++; } }
        char b[4]; int n;
        if (c < 0x80) { b[0] = (char)c; n = 1; }
        else if (c < 0x800) { b[0] = (char)(0xc0 | c >> 6); b[1] = (char)(0x80 | (c & 0x3f)); n = 2; }
        else if (c < 0x10000) { b[0] = (char)(0xe0 | c >> 12); b[1] = (char)(0x80 | ((c >> 6) & 0x3f)); b[2] = (char)(0x80 | (c & 0x3f)); n = 3; }
        else { b[0] = (char)(0xf0 | c >> 18); b[1] = (char)(0x80 | ((c >> 12) & 0x3f)); b[2] = (char)(0x80 | ((c >> 6) & 0x3f)); b[3] = (char)(0x80 | (c & 0x3f)); n = 4; }
        rt_sb_append(&sb, b, (size_t)n);
    }
    return rt_sb_take(&sb);
}

static void str_set(struct nsstr *n, const unichar *u, size_t len) {
    if (len > n->cap) { n->cap = len + 8; n->u = realloc(n->u, n->cap * sizeof(unichar)); }
    if (len) memmove(n->u, u, len * sizeof(unichar));
    n->len = len;
    free(n->utf8); n->utf8 = NULL;
}
static void str_append(struct nsstr *n, const struct sv *v) {
    if (n->len + v->len > n->cap) { n->cap = (n->len + v->len) * 2 + 8; n->u = realloc(n->u, n->cap * sizeof(unichar)); }
    for (size_t i = 0; i < v->len; i++) n->u[n->len + i] = sv_at(v, i);
    n->len += v->len;
    free(n->utf8); n->utf8 = NULL;
}
static id str_alloc(Class c) { return rt_alloc_instance(c ? c : RT_CLASS(NSString), 0); }
static id str_init_utf8(id self, const char *s, size_t n) {
    struct nsstr *st = (struct nsstr *)self;
    size_t m = utf8_to_utf16(s, n, NULL);
    unichar *u = rt_xmalloc((m + 1) * sizeof(unichar));
    utf8_to_utf16(s, n, u);
    free(st->u); st->u = u; st->len = m; st->cap = m + 1;
    free(st->utf8); st->utf8 = NULL;
    return self;
}
id rt_str_new(const char *utf8) { return str_init_utf8(str_alloc(NULL), utf8 ? utf8 : "", utf8 ? strlen(utf8) : 0); }
id rt_str(const char *utf8) { return objc_autorelease(rt_str_new(utf8)); }
id rt_str_new_utf16(const unichar *u, size_t n) { id s = str_alloc(NULL); str_set((struct nsstr *)s, u, n); return s; }
static id str_from_sv(Class c, const struct sv *v, size_t from, size_t len) {
    id s = str_alloc(c);
    struct nsstr *n = (struct nsstr *)s;
    n->u = rt_xmalloc((len + 1) * sizeof(unichar)); n->cap = len + 1; n->len = len;
    for (size_t i = 0; i < len; i++) n->u[i] = sv_at(v, from + i);
    return s;
}

static pthread_mutex_t constcache_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { const void *k; char *v; } *constcache; static size_t nconst, capconst;
const char *rt_str_utf8(id s) {
    if (!s) return NULL;
    if (s->isa == RT_CLASS(__NSCFConstantString)) {
        struct cfconst *c = (struct cfconst *)s;
        if (!(c->flags & 0x10)) return c->str;
        pthread_mutex_lock(&constcache_lock);
        for (size_t i = 0; i < nconst; i++) if (constcache[i].k == s) { pthread_mutex_unlock(&constcache_lock); return constcache[i].v; }
        struct sv v; sv_get(s, &v);
        char *u = utf16_to_utf8(&v);
        if (nconst == capconst) { capconst = capconst ? capconst * 2 : 32; constcache = realloc(constcache, capconst * sizeof *constcache); }
        constcache[nconst].k = s; constcache[nconst++].v = u;
        pthread_mutex_unlock(&constcache_lock);
        return u;
    }
    if (is_our_str(s)) {
        struct nsstr *n = (struct nsstr *)s;
        if (!n->utf8) { struct sv v; sv_get(s, &v); n->utf8 = utf16_to_utf8(&v); }
        return n->utf8;
    }
    struct sv v; sv_get(s, &v);
    char *u = utf16_to_utf8(&v); sv_free(&v);
    id holder = rt_str(u);       /* keep it alive for the autorelease scope */
    free(u);
    return rt_str_utf8(holder);
}
const unichar *rt_str_utf16(id s, size_t *len, unichar **tofree) {
    struct sv v; sv_get(s, &v);
    *len = v.len;
    if (v.u) { *tofree = v.owned; return v.u; }
    unichar *u = rt_xmalloc((v.len + 1) * sizeof(unichar));
    for (size_t i = 0; i < v.len; i++) u[i] = v.a[i];
    *tofree = u;
    return u;
}

char *rt_describe(id o) {
    if (!o) return rt_xstrdup("(null)");
    if (rt_is_string(o)) return rt_xstrdup(rt_str_utf8(o));
    void *pool = objc_autoreleasePoolPush();
    id d = RT_SEND0(id, o, "description");
    char *r = rt_xstrdup(d ? rt_str_utf8(d) : "(null)");
    objc_autoreleasePoolPop(pool);
    return r;
}

static id fmt_to_str(Class c, id fmt, const uint64_t *args) {
    rt_sb sb; rt_sb_init(&sb);
    struct rt_va va = { args };
    rt_format(&sb, fmt ? rt_str_utf8(fmt) : "", &va, 1);
    char *s = rt_sb_take(&sb);
    id r = str_init_utf8(str_alloc(c), s, strlen(s));
    free(s);
    return r;
}

/* class methods */
static id s_string(Class c, SEL s) { (void)s; return objc_autorelease(str_alloc(c)); }
static id s_stringWithUTF8String(Class c, SEL s, const char *u) { (void)s; if (!u) return NULL; return objc_autorelease(str_init_utf8(str_alloc(c), u, strlen(u))); }
static id s_stringWithCString(Class c, SEL s, const char *u, unsigned long enc) { (void)enc; return s_stringWithUTF8String(c, s, u); }
static id s_stringWithString(Class c, SEL s, id o) { (void)s; struct sv v; sv_get(o, &v); id r = str_from_sv(c, &v, 0, v.len); sv_free(&v); return objc_autorelease(r); }
static id s_stringWithCharacters(Class c, SEL s, const unichar *u, unsigned long n) { (void)s; id r = str_alloc(c); str_set((struct nsstr *)r, u, n); return objc_autorelease(r); }
id rt_NSString_stringWithFormat(Class c, SEL s, id fmt, const uint64_t *args) { (void)s; return objc_autorelease(fmt_to_str(c, fmt, args)); }
/* instance initializers */
static id s_initWithUTF8String(id self, SEL s, const char *u) { (void)s; if (!u) { objc_release(self); return NULL; } return str_init_utf8(self, u, strlen(u)); }
static id s_initWithCString(id self, SEL s, const char *u, unsigned long e) { (void)e; return s_initWithUTF8String(self, s, u); }
static id s_initWithString(id self, SEL s, id o) { (void)s; struct sv v; sv_get(o, &v); struct nsstr *n = (struct nsstr *)self; n->len = 0; str_append(n, &v); sv_free(&v); return self; }
static id s_initWithCharacters(id self, SEL s, const unichar *u, unsigned long n) { (void)s; str_set((struct nsstr *)self, u, n); return self; }
static id s_initWithBytes(id self, SEL s, const void *b, unsigned long n, unsigned long enc) {
    (void)s;
    if (enc == 10 || enc == 0x94000100 || enc == 0x90000100) { str_set((struct nsstr *)self, b, n / 2); return self; }
    if (enc == 5 || enc == 1) { struct nsstr *st = (struct nsstr *)self; unichar *u = rt_xmalloc((n + 1) * 2); for (size_t i = 0; i < n; i++) u[i] = ((const uint8_t *)b)[i]; str_set(st, u, n); free(u); return self; }
    return str_init_utf8(self, b, n);
}
id rt_NSString_initWithFormat(id self, SEL s, id fmt, const uint64_t *args) {
    (void)s; Class c = self->isa; objc_release(self); return fmt_to_str(c, fmt, args);
}
static id s_initWithFormatArguments(id self, SEL s, id fmt, const uint64_t *va) { return rt_NSString_initWithFormat(self, s, fmt, va); }
static id s_init(id self, SEL s) { (void)s; return self; }
static void s_dealloc(id self, SEL s) {
    (void)s;
    if (is_our_str(self)) { struct nsstr *n = (struct nsstr *)self; free(n->u); free(n->utf8); }
    rt_dealloc_instance(self);
}
/* primitives */
static unsigned long s_length(id self, SEL s) { (void)s; struct sv v; sv_get(self, &v); size_t n = v.len; sv_free(&v); return n; }
static unsigned long s_characterAtIndex(id self, SEL s, unsigned long i) {
    (void)s; struct sv v; sv_get(self, &v);
    void rt_soft_error(const char *msg);
    if (i >= v.len) { sv_free(&v); rt_soft_error("-[NSString characterAtIndex:]: Range or index out of bounds"); return 0; }
    unichar c = sv_at(&v, i); sv_free(&v); return c;
}
static void s_getCharacters(id self, SEL s, unichar *buf, NSRange r) { (void)s; struct sv v; sv_get(self, &v); for (size_t i = 0; i < r.length && r.location + i < v.len; i++) buf[i] = sv_at(&v, r.location + i); sv_free(&v); }
static const char *s_UTF8String(id self, SEL s) { (void)s; return rt_str_utf8(self); }
static const char *s_cStringUsingEncoding(id self, SEL s, unsigned long e) { (void)e; return s_UTF8String(self, s); }
static rt_bool s_getCString(id self, SEL s, char *buf, unsigned long max, unsigned long e) {
    (void)s; (void)e; const char *u = rt_str_utf8(self); size_t n = strlen(u);
    if (n + 1 > max) return 0; memcpy(buf, u, n + 1); return 1;
}
static unsigned long s_lengthOfBytes(id self, SEL s, unsigned long e) { (void)s; (void)e; return strlen(rt_str_utf8(self)); }
static id s_description(id self, SEL s) { (void)s; return self; }
static id s_copyWithZone(id self, SEL s, void *z) {
    (void)s; (void)z;
    if (self->isa == RT_CLASS(__NSCFConstantString)) return self;
    if (is_our_str(self) && !((struct nsstr *)self)->mut) return objc_retain(self);
    struct sv v; sv_get(self, &v); id r = str_from_sv(RT_CLASS(NSString), &v, 0, v.len); sv_free(&v); return r;
}
static id s_mutableCopyWithZone(id self, SEL s, void *z) {
    (void)s; (void)z; struct sv v; sv_get(self, &v);
    id r = str_from_sv(RT_CLASS(NSMutableString), &v, 0, v.len); ((struct nsstr *)r)->mut = 1; sv_free(&v); return r;
}
static unsigned long str_hash_sv(const struct sv *v) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < v->len; i++) { h ^= sv_at(v, i); h *= 1099511628211ULL; }
    return (unsigned long)h;
}
static unsigned long s_hash(id self, SEL s) { (void)s; struct sv v; sv_get(self, &v); unsigned long h = str_hash_sv(&v); sv_free(&v); return h; }
static long sv_cmp(const struct sv *a, const struct sv *b, int ci) {
    size_t n = a->len < b->len ? a->len : b->len;
    for (size_t i = 0; i < n; i++) {
        unichar x = sv_at(a, i), y = sv_at(b, i);
        if (ci) { if (x < 128) x = (unichar)tolower(x); if (y < 128) y = (unichar)tolower(y); }
        if (x != y) return x < y ? -1 : 1;
    }
    return a->len == b->len ? 0 : a->len < b->len ? -1 : 1;
}
static rt_bool s_isEqualToString(id self, SEL s, id o) {
    (void)s; if (self == o) return 1; if (!o || !rt_is_string(o)) return 0;
    struct sv a, b; sv_get(self, &a); sv_get(o, &b);
    long r = a.len == b.len ? sv_cmp(&a, &b, 0) : 1;
    sv_free(&a); sv_free(&b); return r == 0;
}
static long s_compare(id self, SEL s, id o) { (void)s; struct sv a, b; sv_get(self, &a); sv_get(o, &b); long r = sv_cmp(&a, &b, 0); sv_free(&a); sv_free(&b); return r; }
static long s_ccompare(id self, SEL s, id o) { (void)s; struct sv a, b; sv_get(self, &a); sv_get(o, &b); long r = sv_cmp(&a, &b, 1); sv_free(&a); sv_free(&b); return r; }
static NSRange sv_find(const struct sv *h, const struct sv *n, size_t from, int back) {
    NSRange nf = { (unsigned long)-1 >> 1, 0 };
    nf.location = 0x7fffffffffffffffUL;
    if (n->len == 0 || n->len > h->len) return nf;
    if (!back) {
        for (size_t i = from; i + n->len <= h->len; i++) {
            size_t k = 0; while (k < n->len && sv_at(h, i + k) == sv_at(n, k)) k++;
            if (k == n->len) return (NSRange){ i, n->len };
        }
    } else {
        for (size_t i = h->len - n->len + 1; i-- > from;) {
            size_t k = 0; while (k < n->len && sv_at(h, i + k) == sv_at(n, k)) k++;
            if (k == n->len) return (NSRange){ i, n->len };
        }
    }
    return nf;
}
static NSRange s_rangeOfString(id self, SEL s, id o) { (void)s; struct sv a, b; sv_get(self, &a); sv_get(o, &b); NSRange r = sv_find(&a, &b, 0, 0); sv_free(&a); sv_free(&b); return r; }
static NSRange s_rangeOfStringOptions(id self, SEL s, id o, unsigned long opt) { (void)s; struct sv a, b; sv_get(self, &a); sv_get(o, &b); NSRange r = sv_find(&a, &b, 0, (opt & 4) != 0); sv_free(&a); sv_free(&b); return r; }
static rt_bool s_containsString(id self, SEL s, id o) { return s_rangeOfString(self, s, o).length != 0; }
static rt_bool s_hasPrefix(id self, SEL s, id o) {
    (void)s; struct sv a, b; sv_get(self, &a); sv_get(o, &b); int r = b.len <= a.len;
    for (size_t i = 0; r && i < b.len; i++) r = sv_at(&a, i) == sv_at(&b, i);
    sv_free(&a); sv_free(&b); return r && b.len;
}
static rt_bool s_hasSuffix(id self, SEL s, id o) {
    (void)s; struct sv a, b; sv_get(self, &a); sv_get(o, &b); int r = b.len <= a.len;
    for (size_t i = 0; r && i < b.len; i++) r = sv_at(&a, a.len - b.len + i) == sv_at(&b, i);
    sv_free(&a); sv_free(&b); return r && b.len;
}
static id s_append(id self, SEL s, id o) {
    (void)s; struct sv a, b; sv_get(self, &a); sv_get(o, &b);
    id r = str_from_sv(RT_CLASS(NSString), &a, 0, a.len); str_append((struct nsstr *)r, &b);
    sv_free(&a); sv_free(&b); return objc_autorelease(r);
}
id rt_NSString_stringByAppendingFormat(id self, SEL s, id fmt, const uint64_t *args) {
    id t = objc_autorelease(fmt_to_str(NULL, fmt, args));
    return s_append(self, s, t);
}
static id s_substringFromIndex(id self, SEL s, unsigned long i) { (void)s; struct sv v; sv_get(self, &v); if (i > v.len) i = v.len; id r = str_from_sv(NULL, &v, i, v.len - i); sv_free(&v); return objc_autorelease(r); }
static id s_substringToIndex(id self, SEL s, unsigned long i) { (void)s; struct sv v; sv_get(self, &v); if (i > v.len) i = v.len; id r = str_from_sv(NULL, &v, 0, i); sv_free(&v); return objc_autorelease(r); }
static id s_substringWithRange(id self, SEL s, NSRange rg) {
    (void)s; struct sv v; sv_get(self, &v);
    if (rg.location > v.len) rg.location = v.len;
    if (rg.location + rg.length > v.len) rg.length = v.len - rg.location;
    id r = str_from_sv(NULL, &v, rg.location, rg.length); sv_free(&v); return objc_autorelease(r);
}
static id case_map(id self, int up) {
    struct sv v; sv_get(self, &v); id r = str_from_sv(NULL, &v, 0, v.len); sv_free(&v);
    struct nsstr *n = (struct nsstr *)r;
    for (size_t i = 0; i < n->len; i++) if (n->u[i] < 128) n->u[i] = (unichar)(up ? toupper(n->u[i]) : tolower(n->u[i]));
    return objc_autorelease(r);
}
static id s_upper(id self, SEL s) { (void)s; return case_map(self, 1); }
static id s_lower(id self, SEL s) { (void)s; return case_map(self, 0); }
static id s_capitalized(id self, SEL s) {
    (void)s; id r = case_map(self, 0); struct nsstr *n = (struct nsstr *)r; int start = 1;
    for (size_t i = 0; i < n->len; i++) { if (start && n->u[i] < 128) n->u[i] = (unichar)toupper(n->u[i]); start = n->u[i] == ' '; }
    return r;
}
static int s_intValue(id self, SEL s) { (void)s; return atoi(rt_str_utf8(self)); }
static long s_integerValue(id self, SEL s) { (void)s; return strtol(rt_str_utf8(self), NULL, 10); }
static double s_doubleValue(id self, SEL s) { (void)s; return strtod(rt_str_utf8(self), NULL); }
static float s_floatValue(id self, SEL s) { (void)s; return (float)strtod(rt_str_utf8(self), NULL); }
static rt_bool s_boolValue(id self, SEL s) { (void)s; const char *u = rt_str_utf8(self); while (*u == ' ') u++; return *u == 'Y' || *u == 'y' || *u == 'T' || *u == 't' || (*u >= '1' && *u <= '9'); }
static id s_components(id self, SEL s, id sep) {
    (void)s; struct sv a, b; sv_get(self, &a); sv_get(sep, &b);
    id arr = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array");
    size_t pos = 0;
    for (;;) {
        NSRange r = sv_find(&a, &b, pos, 0);
        size_t end = r.length ? r.location : a.len;
        id part = str_from_sv(NULL, &a, pos, end - pos);
        RT_SEND1(void, arr, "addObject:", id, part); objc_release(part);
        if (!r.length) break;
        pos = r.location + r.length;
    }
    sv_free(&a); sv_free(&b);
    return arr;
}
static id s_replace(id self, SEL s, id target, id repl) {
    (void)s; struct sv a, b, c; sv_get(self, &a); sv_get(target, &b); sv_get(repl, &c);
    id r = str_alloc(NULL); struct nsstr *n = (struct nsstr *)r;
    size_t pos = 0;
    for (;;) {
        NSRange f = sv_find(&a, &b, pos, 0);
        size_t end = f.length ? f.location : a.len;
        struct sv part = a; part.owned = NULL;
        if (part.a) part.a += pos; else part.u += pos;
        part.len = end - pos;
        str_append(n, &part);
        if (!f.length) break;
        str_append(n, &c);
        pos = f.location + f.length;
    }
    sv_free(&a); sv_free(&b); sv_free(&c);
    return objc_autorelease(r);
}
static id s_trim(id self, SEL s, id set) {
    (void)s; (void)set;  /* whitespace+newline set is the only one supported */
    struct sv v; sv_get(self, &v); size_t b = 0, e = v.len;
    while (b < e && (sv_at(&v, b) == ' ' || sv_at(&v, b) == '\t' || sv_at(&v, b) == '\n' || sv_at(&v, b) == '\r')) b++;
    while (e > b && (sv_at(&v, e - 1) == ' ' || sv_at(&v, e - 1) == '\t' || sv_at(&v, e - 1) == '\n' || sv_at(&v, e - 1) == '\r')) e--;
    id r = str_from_sv(NULL, &v, b, e - b); sv_free(&v); return objc_autorelease(r);
}
/* path helpers */
static id s_lastPathComponent(id self, SEL s) { (void)s; const char *u = rt_str_utf8(self); size_t n = strlen(u); while (n > 1 && u[n - 1] == '/') n--; const char *p = u + n; while (p > u && p[-1] != '/') p--; char *t = strndup(p, (size_t)(u + n - p)); id r = rt_str(t); free(t); return r; }
static id s_pathExtension(id self, SEL s) { (void)s; const char *u = rt_str_utf8(self); const char *sl = strrchr(u, '/'); const char *d = strrchr(u, '.'); return rt_str(d && (!sl || d > sl) ? d + 1 : ""); }
static id s_deletingLast(id self, SEL s) { (void)s; const char *u = rt_str_utf8(self); const char *sl = strrchr(u, '/'); if (!sl) return rt_str(""); if (sl == u) return rt_str("/"); char *t = strndup(u, (size_t)(sl - u)); id r = rt_str(t); free(t); return r; }
static id s_deletingExt(id self, SEL s) { (void)s; const char *u = rt_str_utf8(self); const char *sl = strrchr(u, '/'); const char *d = strrchr(u, '.'); if (!d || (sl && d < sl)) return self; char *t = strndup(u, (size_t)(d - u)); id r = rt_str(t); free(t); return r; }
static id s_appendingPath(id self, SEL s, id comp) {
    (void)s; const char *a = rt_str_utf8(self), *b = rt_str_utf8(comp); rt_sb sb; rt_sb_init(&sb); rt_sb_puts(&sb, a);
    if (sb.len && sb.buf[sb.len - 1] != '/' && *b) rt_sb_putc(&sb, '/');
    while (*b == '/' && sb.len) b++;
    rt_sb_puts(&sb, b); char *t = rt_sb_take(&sb); id r = rt_str(t); free(t); return r;
}
static id s_appendingExt(id self, SEL s, id ext) { (void)s; char b[4096]; snprintf(b, sizeof b, "%s.%s", rt_str_utf8(self), rt_str_utf8(ext)); return rt_str(b); }
static id s_dataUsingEncoding(id self, SEL s, unsigned long e) {
    (void)s; (void)e; const char *u = rt_str_utf8(self);
    return ((id (*)(id, SEL, const void *, unsigned long))objc_msgSend)((id)RT_CLASS(NSData), rt_sel("dataWithBytes:length:"), u, strlen(u));
}
static rt_bool s_writeToFile(id self, SEL s, id path, long atomically, unsigned long enc, id *err) {
    (void)s; (void)atomically; (void)enc; if (err) *err = NULL;
    FILE *f = fopen(rt_str_utf8(path), "wb"); if (!f) return 0;
    const char *u = rt_str_utf8(self); fwrite(u, 1, strlen(u), f); fclose(f); return 1;
}
static id s_stringWithContentsOfFile(Class c, SEL s, id path, unsigned long enc, id *err) {
    (void)s; (void)enc; if (err) *err = NULL;
    FILE *f = fopen(rt_str_utf8(path), "rb"); if (!f) return NULL;
    rt_sb sb; rt_sb_init(&sb); char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) rt_sb_append(&sb, buf, n);
    fclose(f);
    id r = str_init_utf8(str_alloc(c), sb.buf ? sb.buf : "", sb.len); free(sb.buf);
    return objc_autorelease(r);
}
/* NSMutableString */
static id ms_withCapacity(Class c, SEL s, unsigned long n) { (void)s; (void)n; id r = str_alloc(c); ((struct nsstr *)r)->mut = 1; return objc_autorelease(r); }
static id ms_initWithCapacity(id self, SEL s, unsigned long n) { (void)s; (void)n; ((struct nsstr *)self)->mut = 1; return self; }
static id ms_init(id self, SEL s) { (void)s; ((struct nsstr *)self)->mut = 1; return self; }
static void ms_appendString(id self, SEL s, id o) { (void)s; struct sv v; sv_get(o, &v); str_append((struct nsstr *)self, &v); sv_free(&v); }
void rt_NSMutableString_appendFormat(id self, SEL s, id fmt, const uint64_t *args) { id t = fmt_to_str(NULL, fmt, args); ms_appendString(self, s, t); objc_release(t); }
static void ms_setString(id self, SEL s, id o) { (void)s; ((struct nsstr *)self)->len = 0; ms_appendString(self, s, o); }
static void ms_replaceRange(id self, SEL s, NSRange r, id o) {
    (void)s; struct nsstr *n = (struct nsstr *)self; struct sv v; sv_get(o, &v);
    if (r.location > n->len) r.location = n->len;
    if (r.location + r.length > n->len) r.length = n->len - r.location;
    size_t nl = n->len - r.length + v.len;
    unichar *u = rt_xmalloc((nl + 1) * sizeof(unichar));
    memcpy(u, n->u, r.location * sizeof(unichar));
    for (size_t i = 0; i < v.len; i++) u[r.location + i] = sv_at(&v, i);
    memcpy(u + r.location + v.len, n->u + r.location + r.length, (n->len - r.location - r.length) * sizeof(unichar));
    free(n->u); n->u = u; n->len = nl; n->cap = nl + 1; free(n->utf8); n->utf8 = NULL;
    sv_free(&v);
}
static void ms_insert(id self, SEL s, id o, unsigned long i) { ms_replaceRange(self, s, (NSRange){ i, 0 }, o); }
static void ms_delete(id self, SEL s, NSRange r) { ms_replaceRange(self, s, r, rt_str("")); }

/* ================================================================ NSNumber */
struct nsnum { Class isa; long kind; long long i; double d; };   /* kind: 0 int, 1 double, 2 bool, 3 unsigned */
static id num_make(Class c, long kind, long long i, double d) {
    id r = rt_alloc_instance(c ? c : RT_CLASS(NSNumber), 0);
    struct nsnum *n = (struct nsnum *)r; n->kind = kind; n->i = i; n->d = d;
    return r;
}
id rt_num_int(long long v) { return objc_autorelease(num_make(NULL, 0, v, (double)v)); }
id rt_num_dbl(double v) { return objc_autorelease(num_make(NULL, 1, (long long)v, v)); }
#define NUM_CTOR_I(fn, T) static id fn(Class c, SEL s, T v) { (void)s; return objc_autorelease(num_make(c, 0, (long long)v, (double)v)); }
NUM_CTOR_I(n_withInt, int)
NUM_CTOR_I(n_withLong, long)
NUM_CTOR_I(n_withShort, int)
NUM_CTOR_I(n_withChar, int)
static id n_withUnsigned(Class c, SEL s, unsigned long v) { (void)s; return objc_autorelease(num_make(c, 3, (long long)v, (double)v)); }
static id n_withBool(Class c, SEL s, long v) { (void)s; return objc_autorelease(num_make(c, 2, (v & 0xff) != 0, (v & 0xff) != 0)); }
static id n_withDouble(Class c, SEL s, double v) { (void)s; return objc_autorelease(num_make(c, 1, (long long)v, v)); }
static id n_withFloat(Class c, SEL s, float v) { (void)s; return objc_autorelease(num_make(c, 1, (long long)v, (double)v)); }
static id n_initWithInt(id self, SEL s, int v) { (void)s; struct nsnum *n = (struct nsnum *)self; n->kind = 0; n->i = v; n->d = v; return self; }
static id n_initWithDouble(id self, SEL s, double v) { (void)s; struct nsnum *n = (struct nsnum *)self; n->kind = 1; n->i = (long long)v; n->d = v; return self; }
static id n_initWithBool(id self, SEL s, long v) { (void)s; struct nsnum *n = (struct nsnum *)self; n->kind = 2; n->i = (v & 0xff) != 0; n->d = (double)n->i; return self; }
static int n_intValue(id self, SEL s) { (void)s; return (int)((struct nsnum *)self)->i; }
static long long n_llValue(id self, SEL s) { (void)s; return ((struct nsnum *)self)->i; }
static double n_doubleValue(id self, SEL s) { (void)s; return ((struct nsnum *)self)->d; }
static float n_floatValue(id self, SEL s) { (void)s; return (float)((struct nsnum *)self)->d; }
static rt_bool n_boolValue(id self, SEL s) { (void)s; struct nsnum *n = (struct nsnum *)self; return n->kind == 1 ? n->d != 0 : n->i != 0; }
static long n_charValue(id self, SEL s) { (void)s; return (signed char)((struct nsnum *)self)->i; }
static const char *n_objCType(id self, SEL s) { (void)s; long k = ((struct nsnum *)self)->kind; return k == 1 ? "d" : k == 2 ? "c" : k == 3 ? "Q" : "q"; }
static id n_stringValue(id self, SEL s) {
    (void)s; struct nsnum *n = (struct nsnum *)self; char b[64];
    if (n->kind == 1) {
        snprintf(b, sizeof b, "%.17g", n->d);
        double back = strtod(b, NULL);
        for (int p = 1; p <= 17; p++) { char t[64]; snprintf(t, sizeof t, "%.*g", p, n->d); if (strtod(t, NULL) == back) { memcpy(b, t, sizeof t); break; } }
    } else if (n->kind == 3) snprintf(b, sizeof b, "%llu", (unsigned long long)n->i);
    else snprintf(b, sizeof b, "%lld", n->i);
    return rt_str(b);
}
static rt_bool n_isEqualToNumber(id self, SEL s, id o) {
    (void)s; if (!o || !rt_is_kind_of(o, RT_CLASS(NSNumber))) return 0;
    struct nsnum *a = (struct nsnum *)self, *b = (struct nsnum *)o;
    return (a->kind == 1 || b->kind == 1) ? a->d == b->d : a->i == b->i;
}
static long n_compare(id self, SEL s, id o) { (void)s; struct nsnum *a = (struct nsnum *)self, *b = (struct nsnum *)o; double x = a->kind == 1 ? a->d : (double)a->i, y = b->kind == 1 ? b->d : (double)b->i; return x < y ? -1 : x > y ? 1 : 0; }
static unsigned long n_hash(id self, SEL s) { (void)s; struct nsnum *n = (struct nsnum *)self; return n->kind == 1 && n->d != floor(n->d) ? (unsigned long)(n->d * 2654435761.0) : (unsigned long)n->i; }
static id n_copy(id self, SEL s, void *z) { (void)s; (void)z; return objc_retain(self); }

/* ================================================================ NSNull */
static struct { Class isa; } null_singleton;
static id null_null(Class c, SEL s) { (void)c; (void)s; return (id)&null_singleton; }
static id null_desc(id self, SEL s) { (void)self; (void)s; return rt_str("<null>"); }

/* ============================================================ autorelease */
struct nspool { Class isa; void *token; };
static id pool_init(id self, SEL s) { (void)s; ((struct nspool *)self)->token = objc_autoreleasePoolPush(); return self; }
static void pool_drain(id self, SEL s) { (void)s; objc_autoreleasePoolPop(((struct nspool *)self)->token); rt_dealloc_instance(self); }
static void pool_add(Class c, SEL s, id o) { (void)c; (void)s; objc_autorelease(o); }

/* ================================================================ NSCharacterSet (minimal) */
static id cs_ws(Class c, SEL s) { (void)s; static id one; if (!one) one = rt_alloc_instance(c, 0); return one; }

/* ================================================================ C functions */
static int log_plain = -1;
void iosrt_darwin_NSLogv(id fmt, const uint64_t *args) {
    if (log_plain < 0) log_plain = getenv("IOSRT_LOG_PLAIN") != NULL;
    void *pool = objc_autoreleasePoolPush();
    rt_sb sb; rt_sb_init(&sb);
    if (!log_plain) {
        struct timeval tv; gettimeofday(&tv, NULL);
        struct tm tm; localtime_r(&tv.tv_sec, &tm);
        char ts[64]; strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
        const char *name = rt_main_image ? strrchr(rt_main_image->path, '/') : NULL;
        rt_sb_printf(&sb, "%s.%03ld %s[%d:%ld] ", ts, (long)tv.tv_usec / 1000, name ? name + 1 : "app", getpid(), (long)gettid());
    }
    struct rt_va va = { args };
    rt_format(&sb, fmt ? rt_str_utf8(fmt) : "(null)", &va, 1);
    if (!sb.len || sb.buf[sb.len - 1] != '\n') rt_sb_putc(&sb, '\n');
    rt_out(2, sb.buf, sb.len);
    free(sb.buf);
    objc_autoreleasePoolPop(pool);
}
void iosrt_darwin_NSLog(id fmt, const uint64_t *args) { iosrt_darwin_NSLogv(fmt, args); }
id iosrt_NSStringFromClass(Class c) { return c ? rt_str(rt_class_name(c)) : NULL; }
Class iosrt_NSClassFromString(id s) { return s ? rt_class_named(rt_str_utf8(s)) : NULL; }
id iosrt_NSStringFromSelector(SEL s) { return s ? rt_str(s) : NULL; }
SEL iosrt_NSSelectorFromString(id s) { return s ? rt_sel(rt_str_utf8(s)) : NULL; }
id iosrt_NSStringFromRange(NSRange r) { char b[64]; snprintf(b, sizeof b, "{%lu, %lu}", r.location, r.length); return rt_str(b); }
id iosrt_NSStringFromProtocol(const void *p) { const char *rt_protocol_name(const void *); return rt_str(rt_protocol_name(p)); }

void rt_foundation_misc_init(void);
void rt_collections_init(void);
void rt_blocks_init(void);

void rt_foundation_init(void) {
    Class o = RT_CLASS(NSObject);
    rt_native_class(o, &iosrt_meta_NSObject, "NSObject", NULL, sizeof(void *));
    CM(o, "alloc", o_alloc); CM(o, "allocWithZone:", o_allocWithZone); CM(o, "new", o_new);
    CM(o, "class", o_cclass); CM(o, "superclass", o_csuperclass); CM(o, "description", o_cdescription);
    CM(o, "instancesRespondToSelector:", o_cinstancesRespond); CM(o, "isSubclassOfClass:", o_cisSubclassOfClass);
    CM(o, "initialize", o_cinitialize);
    IM(o, "init", o_init); IM(o, "dealloc", o_dealloc); IM(o, "retain", o_retain); IM(o, "release", o_release);
    IM(o, "autorelease", o_autorelease); IM(o, "retainCount", o_retainCount); IM(o, "self", o_self);
    IM(o, "class", o_class); IM(o, "superclass", o_superclass); IM(o, "isKindOfClass:", o_isKindOfClass);
    IM(o, "isMemberOfClass:", o_isMemberOfClass); IM(o, "respondsToSelector:", o_respondsToSelector);
    IM(o, "conformsToProtocol:", o_conformsToProtocol); IM(o, "performSelector:", o_perform0);
    IM(o, "performSelector:withObject:", o_perform1); IM(o, "performSelector:withObject:withObject:", o_perform2);
    IM(o, "performSelector:withObject:afterDelay:", o_performAfterDelay);
    IM(o, "description", o_description); IM(o, "debugDescription", o_description); IM(o, "hash", o_hash);
    IM(o, "isEqual:", o_isEqual); IM(o, "copy", o_copy); IM(o, "mutableCopy", o_mutableCopy); IM(o, "zone", o_zone);
    IM(o, "isProxy", o_isProxy); IM(o, "methodForSelector:", o_methodForSelector);
    IM(o, "doesNotRecognizeSelector:", o_doesNotRecognize);

    rt_blocks_init();

    Class s = RT_CLASS(NSString);
    rt_native_class(s, &iosrt_meta_NSString, "NSString", o, sizeof(struct nsstr) - sizeof(void *));
    rt_native_class(RT_CLASS(NSMutableString), &iosrt_meta_NSMutableString, "NSMutableString", s, 0);
    rt_native_class(RT_CLASS(__NSCFConstantString), &iosrt_meta___NSCFConstantString, "__NSCFConstantString", s, 0);
    CM(s, "string", s_string); CM(s, "stringWithUTF8String:", s_stringWithUTF8String);
    CM(s, "stringWithCString:encoding:", s_stringWithCString); CM(s, "stringWithString:", s_stringWithString);
    CM(s, "stringWithCharacters:length:", s_stringWithCharacters);
    extern char iosrt_imp_stringWithFormat[], iosrt_imp_initWithFormat[], iosrt_imp_stringByAppendingFormat[], iosrt_imp_appendFormat[];
    CM(s, "stringWithFormat:", iosrt_imp_stringWithFormat); CM(s, "localizedStringWithFormat:", iosrt_imp_stringWithFormat);
    CM(s, "stringWithContentsOfFile:encoding:error:", s_stringWithContentsOfFile);
    IM(s, "init", s_init); IM(s, "initWithUTF8String:", s_initWithUTF8String); IM(s, "initWithCString:encoding:", s_initWithCString);
    IM(s, "initWithString:", s_initWithString); IM(s, "initWithCharacters:length:", s_initWithCharacters);
    IM(s, "initWithBytes:length:encoding:", s_initWithBytes); IM(s, "initWithFormat:", iosrt_imp_initWithFormat);
    IM(s, "initWithFormat:arguments:", s_initWithFormatArguments);
    IM(s, "dealloc", s_dealloc); IM(s, "length", s_length); IM(s, "characterAtIndex:", s_characterAtIndex);
    IM(s, "getCharacters:range:", s_getCharacters);
    IM(s, "UTF8String", s_UTF8String); IM(s, "cStringUsingEncoding:", s_cStringUsingEncoding);
    IM(s, "getCString:maxLength:encoding:", s_getCString); IM(s, "lengthOfBytesUsingEncoding:", s_lengthOfBytes);
    IM(s, "description", s_description); IM(s, "copyWithZone:", s_copyWithZone); IM(s, "mutableCopyWithZone:", s_mutableCopyWithZone);
    IM(s, "hash", s_hash); IM(s, "isEqual:", s_isEqualToString); IM(s, "isEqualToString:", s_isEqualToString);
    IM(s, "compare:", s_compare); IM(s, "caseInsensitiveCompare:", s_ccompare); IM(s, "localizedCompare:", s_compare);
    IM(s, "rangeOfString:", s_rangeOfString); IM(s, "rangeOfString:options:", s_rangeOfStringOptions);
    IM(s, "containsString:", s_containsString); IM(s, "hasPrefix:", s_hasPrefix); IM(s, "hasSuffix:", s_hasSuffix);
    IM(s, "stringByAppendingString:", s_append); IM(s, "stringByAppendingFormat:", iosrt_imp_stringByAppendingFormat);
    IM(s, "substringFromIndex:", s_substringFromIndex); IM(s, "substringToIndex:", s_substringToIndex);
    IM(s, "substringWithRange:", s_substringWithRange); IM(s, "uppercaseString", s_upper); IM(s, "lowercaseString", s_lower);
    IM(s, "capitalizedString", s_capitalized);
    IM(s, "intValue", s_intValue); IM(s, "integerValue", s_integerValue); IM(s, "longLongValue", s_integerValue);
    IM(s, "doubleValue", s_doubleValue); IM(s, "floatValue", s_floatValue); IM(s, "boolValue", s_boolValue);
    IM(s, "componentsSeparatedByString:", s_components);
    IM(s, "stringByReplacingOccurrencesOfString:withString:", s_replace);
    IM(s, "stringByTrimmingCharactersInSet:", s_trim);
    IM(s, "lastPathComponent", s_lastPathComponent); IM(s, "pathExtension", s_pathExtension);
    IM(s, "stringByDeletingLastPathComponent", s_deletingLast); IM(s, "stringByDeletingPathExtension", s_deletingExt);
    IM(s, "stringByAppendingPathComponent:", s_appendingPath); IM(s, "stringByAppendingPathExtension:", s_appendingExt);
    IM(s, "dataUsingEncoding:", s_dataUsingEncoding); IM(s, "writeToFile:atomically:encoding:error:", s_writeToFile);
    Class ms = RT_CLASS(NSMutableString);
    CM(ms, "stringWithCapacity:", ms_withCapacity); CM(ms, "string", ms_withCapacity);
    IM(ms, "initWithCapacity:", ms_initWithCapacity); IM(ms, "init", ms_init);
    IM(ms, "appendString:", ms_appendString); IM(ms, "appendFormat:", iosrt_imp_appendFormat);
    IM(ms, "setString:", ms_setString); IM(ms, "insertString:atIndex:", ms_insert);
    IM(ms, "deleteCharactersInRange:", ms_delete); IM(ms, "replaceCharactersInRange:withString:", ms_replaceRange);

    Class n = RT_CLASS(NSNumber);
    rt_native_class(n, &iosrt_meta_NSNumber, "NSNumber", o, sizeof(struct nsnum) - sizeof(void *));
    CM(n, "numberWithInt:", n_withInt); CM(n, "numberWithInteger:", n_withLong); CM(n, "numberWithLong:", n_withLong);
    CM(n, "numberWithLongLong:", n_withLong); CM(n, "numberWithShort:", n_withShort); CM(n, "numberWithChar:", n_withChar);
    CM(n, "numberWithUnsignedInt:", n_withUnsigned); CM(n, "numberWithUnsignedInteger:", n_withUnsigned);
    CM(n, "numberWithUnsignedLong:", n_withUnsigned); CM(n, "numberWithUnsignedLongLong:", n_withUnsigned);
    CM(n, "numberWithUnsignedChar:", n_withUnsigned); CM(n, "numberWithUnsignedShort:", n_withUnsigned);
    CM(n, "numberWithBool:", n_withBool); CM(n, "numberWithDouble:", n_withDouble); CM(n, "numberWithFloat:", n_withFloat);
    IM(n, "initWithInt:", n_initWithInt); IM(n, "initWithInteger:", n_initWithInt); IM(n, "initWithDouble:", n_initWithDouble);
    IM(n, "initWithBool:", n_initWithBool);
    IM(n, "intValue", n_intValue); IM(n, "integerValue", n_llValue); IM(n, "longValue", n_llValue);
    IM(n, "longLongValue", n_llValue); IM(n, "unsignedIntegerValue", n_llValue); IM(n, "unsignedIntValue", n_intValue);
    IM(n, "doubleValue", n_doubleValue); IM(n, "floatValue", n_floatValue); IM(n, "boolValue", n_boolValue);
    IM(n, "charValue", n_charValue); IM(n, "objCType", n_objCType);
    IM(n, "unsignedLongLongValue", n_llValue); IM(n, "unsignedLongValue", n_llValue); IM(n, "shortValue", n_intValue);
    IM(n, "unsignedShortValue", n_intValue); IM(n, "unsignedCharValue", n_charValue);
    IM(n, "stringValue", n_stringValue); IM(n, "description", n_stringValue);
    IM(n, "isEqualToNumber:", n_isEqualToNumber); IM(n, "isEqual:", n_isEqualToNumber); IM(n, "compare:", n_compare);
    IM(n, "hash", n_hash); IM(n, "copyWithZone:", n_copy);

    rt_native_class(RT_CLASS(NSNull), &iosrt_meta_NSNull, "NSNull", o, 0);
    null_singleton.isa = RT_CLASS(NSNull);
    CM(RT_CLASS(NSNull), "null", null_null); IM(RT_CLASS(NSNull), "description", null_desc);

    Class p = RT_CLASS(NSAutoreleasePool);
    rt_native_class(p, &iosrt_meta_NSAutoreleasePool, "NSAutoreleasePool", o, sizeof(void *));
    IM(p, "init", pool_init); IM(p, "drain", pool_drain); IM(p, "release", pool_drain);
    CM(p, "addObject:", pool_add);

    rt_native_class(RT_CLASS(NSCharacterSet), &iosrt_meta_NSCharacterSet, "NSCharacterSet", o, 0);
    CM(RT_CLASS(NSCharacterSet), "whitespaceCharacterSet", cs_ws);
    CM(RT_CLASS(NSCharacterSet), "whitespaceAndNewlineCharacterSet", cs_ws);

    rt_collections_init();
    rt_foundation_misc_init();
    void rt_foundation_more_init(void);
    rt_foundation_more_init();
    void rt_foundation_more2_init(void);
    rt_foundation_more2_init();
    void rt_cf_init(void);
    rt_cf_init();
    void rt_stubs_init(void); rt_stubs_init();
    void rt_foundation_more3_init(void); rt_foundation_more3_init();
    void rt_foundation_more4_init(void); rt_foundation_more4_init();
    void rt_urlio_init(void); rt_urlio_init();
    void rt_archiver_init(void); rt_archiver_init();
}

/* performSelector:withObject:afterDelay: -> NSTimer machinery in misc.c */
void rt_schedule_perform(id target, SEL sel, id arg, double delay);
static void o_performAfterDelay(id self, SEL s, SEL q, id arg, double delay) { (void)s; rt_schedule_perform(self, q, arg, delay); }
