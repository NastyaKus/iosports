/*
 * NSKeyedArchiver / NSKeyedUnarchiver and a binary property list writer.
 *
 * Archive format (what Apple's Foundation writes and reads):
 *   { $archiver = NSKeyedArchiver; $version = 100000;
 *     $top = { root = UID }; $objects = [ "$null", ... ] }
 * Every object in $objects is either a plain plist value (strings, numbers,
 * data) or a dictionary of its encoded keys plus "$class", a UID of a
 * { $classname, $classes } dictionary. UIDs are represented while parsing
 * and building as { "CF$UID" = n } dictionaries (the XML plist form); the
 * binary reader in misc.c produces the same shape.
 *
 * The unarchiver also backs old (keyed-archive) NIBs: nib.c passes a hook
 * that resolves proxies and class swappers, and collects every decoded
 * object for awakeFromNib.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")
#define NEWCLASS(N, SUPER, SZ) rt_native_class(RT_CLASS(N), &iosrt_meta_##N, #N, SUPER, SZ)

RT_DEFINE_CLASS(NSKeyedArchiver);
RT_DEFINE_CLASS(NSKeyedUnarchiver);
/* clang's empty collection literals (@[] and @{}) load these singletons */
id iosrt___NSArray0__, iosrt___NSDictionary0__;
id iosrt_NSKeyedArchiveRootObjectKey, iosrt_NSInvalidArchiveOperationException, iosrt_NSInvalidUnarchiveOperationException;

id rt_dict_new(id *vals, id *keys, size_t n);
void rt_dict_set(id dict, id key, id val);
id rt_data_new(const void *b, size_t n);
const void *rt_data_bytes(id d, size_t *n);
void rt_soft_error(const char *msg);
typedef struct { double x, y; } KPoint;
typedef struct { double w, h; } KSize;
typedef struct { KPoint o; KSize s; } KRect;
typedef struct { double top, left, bottom, right; } KInsets;
typedef struct { double a, b, c, d, tx, ty; } KAffine;

static Class cls(const char *n) { return rt_class_named(n); }
static int is_kind(id o, const char *c) { Class k = cls(c); return o && k && rt_is_kind_of(o, k); }
static id mdict(void) { return objc_autorelease(rt_dict_new(NULL, NULL, 0)); }
static id marray(void) { return RT_SEND0(id, (id)cls("NSMutableArray"), "array"); }
static id uid_obj(long long n) { id d = mdict(); rt_dict_set(d, rt_str("CF$UID"), rt_num_int(n)); return d; }
/* the UID in a { "CF$UID" = n } dictionary, or -1 */
static long long uid_of(id v) {
    if (!v || !is_kind(v, "NSDictionary") || RT_SEND0(unsigned long, v, "count") != 1) return -1;
    id n = rt_dict_get(v, rt_str("CF$UID"));
    return n ? RT_SEND0(long long, n, "longLongValue") : -1;
}
/* "{{1, 2}, {3, 4}}", "{1, 2}", "[a, b, c, d, tx, ty]" */
static int parse_numbers(const char *s, double *out, int n) {
    int k = 0;
    for (int i = 0; i < n; i++) out[i] = 0;
    while (s && *s && k < n) {
        if ((*s >= '0' && *s <= '9') || *s == '-' || *s == '+' || *s == '.') { char *e; out[k++] = strtod(s, &e); s = e == s ? s + 1 : e; }
        else s++;
    }
    return k;
}

/* ============================================================== unarchiver */
typedef id (*rt_kua_hook)(void *ctx, const char *classname, id coder, Class *use_class);
struct kua {
    Class isa;
    id archive, objects, top;    /* parsed plist, $objects, $top */
    size_t n; id *cache; unsigned char *state;
    id cur;                       /* keyed values of the object being decoded */
    id delegate, classmap, all, error;
    long secure, policy;
    rt_kua_hook hook; void *hookctx;
};
#define KUA(x) ((struct kua *)(x))
static id g_class_map;          /* +[NSKeyedUnarchiver setClass:forClassName:] */
static id g_class_names;        /* +[NSKeyedArchiver setClassName:forClass:] (class name -> archived name) */

static id kua_decode_uid(id self, long long u);
static id kua_value(id self, id key) { return KUA(self)->cur && key ? rt_dict_get(KUA(self)->cur, key) : NULL; }
/* a key's value as an object: UIDs are decoded, inline plist values returned as they are */
static id kua_object_value(id self, id v) {
    long long u = uid_of(v);
    return u >= 0 ? kua_decode_uid(self, u) : v;
}
static Class resolve_class(id self, id names) {
    for (size_t i = 0; names && i < rt_array_count(names); i++) {
        id nm = rt_array_at(names, i);
        id m = KUA(self)->classmap ? rt_dict_get(KUA(self)->classmap, nm) : NULL;
        if (!m && g_class_map) m = rt_dict_get(g_class_map, nm);
        if (m) return (Class)m;
        Class c = cls(rt_str_utf8(nm));
        if (c) return c;
    }
    return NULL;
}
static id decode_elements(id self, id dict, const char *key) {
    id arr = marray(), list = rt_dict_get(dict, rt_str(key));
    for (size_t i = 0; list && i < rt_array_count(list); i++) {
        id o = kua_object_value(self, rt_array_at(list, i));
        RT_SEND1(void, arr, "addObject:", id, o ? o : RT_SEND0(id, (id)cls("NSNull"), "null"));
    }
    return arr;
}
/* Foundation classes with a fixed archived layout; returns +1, or (id)-1 when not one of them */
static id decode_builtin(id self, const char *cn, id d) {
    #define IS(x) (!strcmp(cn, x))
    if (IS("NSArray") || IS("NSMutableArray")) { id a = decode_elements(self, d, "NS.objects"); return IS("NSArray") ? RT_SEND0(id, a, "copy") : RT_SEND0(id, a, "mutableCopy"); }
    if (IS("NSSet") || IS("NSMutableSet") || IS("NSCountedSet")) return objc_retain(RT_SEND1(id, (id)cls(IS("NSSet") ? "NSSet" : "NSMutableSet"), "setWithArray:", id, decode_elements(self, d, "NS.objects")));
    if (IS("NSOrderedSet") || IS("NSMutableOrderedSet")) return objc_retain(RT_SEND1(id, (id)cls("NSMutableOrderedSet"), "orderedSetWithArray:", id, decode_elements(self, d, "NS.objects")));
    if (IS("NSDictionary") || IS("NSMutableDictionary")) {
        id ks = decode_elements(self, d, "NS.keys"), vs = decode_elements(self, d, "NS.objects");
        id r = RT_SEND0(id, RT_SEND0(id, (id)cls("NSMutableDictionary"), "alloc"), "init");
        for (size_t i = 0; i < rt_array_count(ks) && i < rt_array_count(vs); i++) RT_SEND2(void, r, "setObject:forKey:", id, rt_array_at(vs, i), id, rt_array_at(ks, i));
        if (IS("NSDictionary")) { id c = RT_SEND0(id, r, "copy"); objc_release(r); r = c; }
        return r;
    }
    if (IS("NSString") || IS("NSMutableString")) {
        id v = kua_object_value(self, rt_dict_get(d, rt_str("NS.string")));
        if (!v) { id b = rt_dict_get(d, rt_str("NS.bytes")); if (b) { size_t n; const char *p = rt_data_bytes(b, &n); char *t = strndup(p ? p : "", n); v = rt_str(t); free(t); } }
        if (!v) v = rt_str("");
        return IS("NSMutableString") ? RT_SEND0(id, v, "mutableCopy") : RT_SEND0(id, v, "copy");
    }
    if (IS("NSData") || IS("NSMutableData")) {
        id v = kua_object_value(self, rt_dict_get(d, rt_str("NS.data")));
        if (!v) v = rt_dict_get(d, rt_str("NS.bytes"));
        size_t n = 0; const void *b = v ? rt_data_bytes(v, &n) : NULL;
        id r = rt_data_new(b, n);
        if (IS("NSMutableData")) { id m = RT_SEND0(id, r, "mutableCopy"); objc_release(r); r = m; }
        return r;
    }
    if (IS("NSDate")) { id t = rt_dict_get(d, rt_str("NS.time")); return objc_retain(RT_SEND1(id, (id)cls("NSDate"), "dateWithTimeIntervalSinceReferenceDate:", double, t ? RT_SEND0(double, t, "doubleValue") : 0)); }
    if (IS("NSURL")) {
        id base = kua_object_value(self, rt_dict_get(d, rt_str("NS.base"))), rel = kua_object_value(self, rt_dict_get(d, rt_str("NS.relative")));
        if (!rel) return NULL;
        return objc_retain(RT_SEND2(id, (id)cls("NSURL"), "URLWithString:relativeToURL:", id, rel, id, base));
    }
    if (IS("NSNull")) return objc_retain(RT_SEND0(id, (id)cls("NSNull"), "null"));
    if (IS("NSUUID")) {
        id b = rt_dict_get(d, rt_str("NS.uuidbytes")); size_t n = 0; const uint8_t *p = b ? rt_data_bytes(b, &n) : NULL;
        if (!p || n < 16) return NULL;
        id u = RT_SEND0(id, (id)cls("NSUUID"), "alloc");
        return RT_SEND1(id, u, "initWithUUIDBytes:", const uint8_t *, p);
    }
    if (IS("NSValue")) {
        id sp = rt_dict_get(d, rt_str("NS.special")); long k = sp ? RT_SEND0(long, sp, "longValue") : 0;
        double v[4];
        if (k == 1) { id s = rt_dict_get(d, rt_str("NS.pointval")); parse_numbers(s ? rt_str_utf8(kua_object_value(self, s)) : "", v, 2); return objc_retain(RT_SEND1(id, (id)cls("NSValue"), "valueWithCGPoint:", KPoint, ((KPoint){ v[0], v[1] }))); }
        if (k == 2) { id s = rt_dict_get(d, rt_str("NS.sizeval")); parse_numbers(s ? rt_str_utf8(kua_object_value(self, s)) : "", v, 2); return objc_retain(RT_SEND1(id, (id)cls("NSValue"), "valueWithCGSize:", KSize, ((KSize){ v[0], v[1] }))); }
        if (k == 3) { id s = rt_dict_get(d, rt_str("NS.rectval")); parse_numbers(s ? rt_str_utf8(kua_object_value(self, s)) : "", v, 4); return objc_retain(RT_SEND1(id, (id)cls("NSValue"), "valueWithCGRect:", KRect, ((KRect){ { v[0], v[1] }, { v[2], v[3] } }))); }
        if (k == 4) {
            id l = rt_dict_get(d, rt_str("NS.rangeval.location")), n = rt_dict_get(d, rt_str("NS.rangeval.length"));
            struct { unsigned long loc, len; } r = { l ? RT_SEND0(unsigned long, l, "unsignedLongValue") : 0, n ? RT_SEND0(unsigned long, n, "unsignedLongValue") : 0 };
            return objc_retain(RT_SEND1(id, (id)cls("NSValue"), "valueWithRange:", __typeof__(r), r));
        }
        return NULL;
    }
    if (IS("NSAttributedString") || IS("NSMutableAttributedString")) {
        id str = kua_object_value(self, rt_dict_get(d, rt_str("NSString")));
        id attrs = kua_object_value(self, rt_dict_get(d, rt_str("NSAttributes")));
        if (str && !is_kind(str, "NSString")) str = RT_SEND0(id, str, "description");
        id a = RT_SEND0(id, (id)cls(IS("NSAttributedString") ? "NSAttributedString" : "NSMutableAttributedString"), "alloc");
        return RT_SEND2(id, a, "initWithString:attributes:", id, str ? str : rt_str(""), id, attrs && is_kind(attrs, "NSDictionary") ? attrs : NULL);
    }
    if (IS("NSLocale")) { id ident = kua_object_value(self, rt_dict_get(d, rt_str("NS.identifier"))); return objc_retain(RT_SEND1(id, (id)cls("NSLocale"), "localeWithLocaleIdentifier:", id, ident ? ident : rt_str("en_US"))); }
    if (IS("NSTimeZone")) { id nm = kua_object_value(self, rt_dict_get(d, rt_str("NS.name"))); id tz = nm ? RT_SEND1(id, (id)cls("NSTimeZone"), "timeZoneWithName:", id, nm) : NULL; return objc_retain(tz ? tz : RT_SEND0(id, (id)cls("NSTimeZone"), "localTimeZone")); }
    #undef IS
    return (id)-1;
}
static id kua_decode_uid(id self, long long u) {
    struct kua *k = KUA(self);
    if (u <= 0 || (size_t)u >= k->n) return NULL;
    if (k->state[u]) return k->cache[u];      /* decoded, or in progress (the allocated instance) */
    id raw = rt_array_at(k->objects, (size_t)u);
    if (!is_kind(raw, "NSDictionary") || !rt_dict_get(raw, rt_str("$class"))) {
        /* inline plist value (string, number, data, ...) */
        if (raw && is_kind(raw, "NSString") && !strcmp(rt_str_utf8(raw), "$null")) raw = NULL;
        k->cache[u] = objc_retain(raw); k->state[u] = 2;
        return raw;
    }
    long long cu = uid_of(rt_dict_get(raw, rt_str("$class")));
    id cdict = cu >= 0 && (size_t)cu < k->n ? rt_array_at(k->objects, (size_t)cu) : NULL;
    id cname = cdict && is_kind(cdict, "NSDictionary") ? rt_dict_get(cdict, rt_str("$classname")) : NULL;
    id chain = cdict && is_kind(cdict, "NSDictionary") ? rt_dict_get(cdict, rt_str("$classes")) : NULL;
    const char *cn = cname ? rt_str_utf8(cname) : "NSObject";
    k->state[u] = 1;
    id saved = k->cur; k->cur = raw;
    id r = (id)-1; Class use = NULL;
    if (k->hook) r = k->hook(k->hookctx, cn, self, &use);
    int from_hook = r != (id)-1;
    if (r == (id)-1 && !use) {
        /* a value class anywhere in the chain (subclasses of NSArray etc. archive their base layout) */
        for (size_t i = 0; chain && i < rt_array_count(chain) && r == (id)-1; i++) {
            const char *nm = rt_str_utf8(rt_array_at(chain, i));
            if (!cls(nm) || i == 0 || !strncmp(nm, "NS", 2)) r = decode_builtin(self, nm, raw);
        }
        if (r == (id)-1 && !chain) r = decode_builtin(self, cn, raw);
    }
    if (r == (id)-1) {
        Class c = use ? use : resolve_class(self, chain ? chain : RT_SEND1(id, (id)cls("NSArray"), "arrayWithObject:", id, rt_str(cn)));
        if (!c) {
            char m[256]; snprintf(m, sizeof m, "*** -[NSKeyedUnarchiver decodeObjectForKey:]: cannot decode object of class (%s)", cn);
            rt_soft_error(m); c = cls("NSObject");
        }
        id obj = RT_SEND0(id, (id)c, "alloc");
        k->cache[u] = obj;   /* visible to cycles while initializing */
        if (rt_responds(obj, rt_sel("initWithCoder:"))) r = RT_SEND1(id, obj, "initWithCoder:", id, self);
        else r = RT_SEND0(id, obj, "init");
        if (r && rt_responds(r, rt_sel("awakeAfterUsingCoder:"))) {
            id a = RT_SEND1(id, r, "awakeAfterUsingCoder:", id, self);
            if (a != r) { objc_retain(a); objc_release(r); r = a; }
        }
    }
    k->cur = saved;
    if (k->delegate && r && rt_responds(k->delegate, rt_sel("unarchiver:didDecodeObject:"))) {
        id a = RT_SEND2(id, k->delegate, "unarchiver:didDecodeObject:", id, self, id, r);
        if (a != r) { objc_retain(a); objc_release(r); r = a; }
    }
    k->cache[u] = r; k->state[u] = 2;
    if (r && k->all && !from_hook) RT_SEND1(void, k->all, "addObject:", id, r);
    return r;
}
/* set up an unarchiver over a parsed archive; returns 0 if it is not a keyed archive */
static int kua_setup(id self, id archive) {
    struct kua *k = KUA(self);
    if (!archive || !is_kind(archive, "NSDictionary")) return 0;
    id objects = rt_dict_get(archive, rt_str("$objects")), top = rt_dict_get(archive, rt_str("$top"));
    if (!objects || !top) return 0;
    k->archive = objc_retain(archive); k->objects = objc_retain(objects); k->top = objc_retain(top);
    k->n = rt_array_count(objects);
    k->cache = calloc(k->n + 1, sizeof(id)); k->state = calloc(k->n + 1, 1);
    k->cur = top;
    return 1;
}
static id kua_initForReadingWithData(id self, SEL s, id data) {
    (void)s;
    size_t n = 0; const uint8_t *b = data ? rt_data_bytes(data, &n) : NULL;
    id archive = b && n ? rt_plist_parse(b, n) : NULL;
    int ok = kua_setup(self, archive);
    objc_release(archive);
    if (!ok) { objc_release(self); return NULL; }
    return self;
}
static id kua_initForReadingFromData(id self, SEL s, id data, id *err) {
    id r = kua_initForReadingWithData(self, s, data);
    if (!r && err) *err = RT_SEND3(id, (id)cls("NSError"), "errorWithDomain:code:userInfo:", id, rt_str("NSCocoaErrorDomain"), long, 4864 /* NSCoderReadCorruptError */, id, NULL);
    return r;
}
static void kua_dealloc(id self, SEL s) {
    (void)s; struct kua *k = KUA(self);
    for (size_t i = 0; i < k->n; i++) if (k->state[i]) objc_release(k->cache[i]);
    free(k->cache); free(k->state);
    objc_release(k->archive); objc_release(k->objects); objc_release(k->top); objc_release(k->classmap); objc_release(k->all); objc_release(k->error);
    rt_dealloc_instance(self);
}
/* keyed decoding */
static id kua_decodeObject(id self, SEL s, id key) { (void)s; return kua_object_value(self, kua_value(self, key)); }
static id kua_decodeObjectOfClass(id self, SEL s, Class c, id key) { (void)c; return kua_decodeObject(self, s, key); }
static id kua_decodeTopLevel(id self, SEL s, id key, id *err) { if (err) *err = NULL; return kua_decodeObject(self, s, key); }
static id kua_decodeTopLevelOfClass(id self, SEL s, Class c, id key, id *err) { (void)c; return kua_decodeTopLevel(self, s, key, err); }
static long long kua_num(id self, id key) { id v = kua_value(self, key); return v && rt_responds(v, rt_sel("longLongValue")) ? RT_SEND0(long long, v, "longLongValue") : 0; }
static double kua_dbl(id self, id key) { id v = kua_value(self, key); return v && rt_responds(v, rt_sel("doubleValue")) ? RT_SEND0(double, v, "doubleValue") : 0; }
static rt_bool kua_decodeBool(id self, SEL s, id key) { (void)s; return kua_num(self, key) != 0; }
static long kua_decodeInteger(id self, SEL s, id key) { (void)s; return (long)kua_num(self, key); }
static int kua_decodeInt32(id self, SEL s, id key) { (void)s; return (int)kua_num(self, key); }
static long long kua_decodeInt64(id self, SEL s, id key) { (void)s; return kua_num(self, key); }
static double kua_decodeDouble(id self, SEL s, id key) { (void)s; return kua_dbl(self, key); }
static float kua_decodeFloat(id self, SEL s, id key) { (void)s; return (float)kua_dbl(self, key); }
static rt_bool kua_contains(id self, SEL s, id key) { (void)s; return kua_value(self, key) != NULL; }
static const void *kua_decodeBytes(id self, SEL s, id key, unsigned long *len) {
    (void)s; id v = kua_object_value(self, kua_value(self, key));
    if (!v || !is_kind(v, "NSData")) { if (len) *len = 0; return NULL; }
    size_t n; const void *b = rt_data_bytes(v, &n); if (len) *len = n; return b;
}
/* geometry: UIKit archives these as strings ("{{x, y}, {w, h}}"); NIB-style binary blobs are accepted too */
static int kua_numbers(id self, id key, double *out, int n) {
    id v = kua_object_value(self, kua_value(self, key));
    for (int i = 0; i < n; i++) out[i] = 0;
    if (!v) return 0;
    if (is_kind(v, "NSString")) return parse_numbers(rt_str_utf8(v), out, n) == n;
    if (is_kind(v, "NSData")) {
        size_t len; const uint8_t *p = rt_data_bytes(v, &len);
        if (len >= 1 + 8 * (size_t)n && p[0] == 7) { memcpy(out, p + 1, 8 * (size_t)n); return 1; }
        if (len >= 1 + 4 * (size_t)n && p[0] == 6) { for (int i = 0; i < n; i++) { float f; memcpy(&f, p + 1 + 4 * i, 4); out[i] = f; } return 1; }
    }
    return 0;
}
static KRect kua_decodeRect(id self, SEL s, id key) { (void)s; double d[4]; kua_numbers(self, key, d, 4); return (KRect){ { d[0], d[1] }, { d[2], d[3] } }; }
static KPoint kua_decodePoint(id self, SEL s, id key) { (void)s; double d[2]; kua_numbers(self, key, d, 2); return (KPoint){ d[0], d[1] }; }
static KSize kua_decodeSize(id self, SEL s, id key) { (void)s; double d[2]; kua_numbers(self, key, d, 2); return (KSize){ d[0], d[1] }; }
static KInsets kua_decodeInsets(id self, SEL s, id key) { (void)s; double d[4]; kua_numbers(self, key, d, 4); return (KInsets){ d[0], d[1], d[2], d[3] }; }
static KAffine kua_decodeAffine(id self, SEL s, id key) { (void)s; double d[6]; if (!kua_numbers(self, key, d, 6)) return (KAffine){ 1, 0, 0, 1, 0, 0 }; return (KAffine){ d[0], d[1], d[2], d[3], d[4], d[5] }; }
static rt_bool kua_yes(id self, SEL s) { (void)self; (void)s; return 1; }
static rt_bool kua_secure(id self, SEL s) { (void)s; return KUA(self)->secure != 0; }
static void kua_setSecure(id self, SEL s, long v) { (void)s; KUA(self)->secure = v & 0xff; }
static long kua_policy(id self, SEL s) { (void)s; return KUA(self)->policy; }
static void kua_setPolicy(id self, SEL s, long v) { (void)s; KUA(self)->policy = v; }
static id kua_error(id self, SEL s) { (void)s; return KUA(self)->error; }
static void kua_finish(id self, SEL s) { (void)self; (void)s; }
static id kua_delegate(id self, SEL s) { (void)s; return KUA(self)->delegate; }
static void kua_setDelegate(id self, SEL s, id d) { (void)s; KUA(self)->delegate = d; }
static void kua_setClassForName(id self, SEL s, Class c, id name) {
    (void)s; struct kua *k = KUA(self);
    if (!k->classmap) k->classmap = rt_dict_new(NULL, NULL, 0);
    if (name) rt_dict_set(k->classmap, name, (id)c);
}
static Class kua_classForName(id self, SEL s, id name) { (void)s; return KUA(self)->classmap && name ? (Class)rt_dict_get(KUA(self)->classmap, name) : NULL; }
static void kua_cSetClassForName(Class c0, SEL s, Class c, id name) { (void)c0; (void)s; if (!g_class_map) g_class_map = rt_dict_new(NULL, NULL, 0); if (name) rt_dict_set(g_class_map, name, (id)c); }
static Class kua_cClassForName(Class c0, SEL s, id name) { (void)c0; (void)s; return g_class_map && name ? (Class)rt_dict_get(g_class_map, name) : NULL; }
/* non-keyed decoding is not supported: zero the output */
static void kua_decodeValue(id self, SEL s, const char *type, void *out) { (void)self; (void)s; (void)type; (void)out; rt_soft_error("non-keyed NSCoder decoding is not supported"); }
/* convenience constructors */
static id unarchive_root(id data, id *err) {
    id u = RT_SEND0(id, (id)RT_CLASS(NSKeyedUnarchiver), "alloc");
    u = kua_initForReadingFromData(u, NULL, data, err);
    if (!u) return NULL;
    id r = objc_retain(kua_decodeObject(u, NULL, rt_str("root")));
    objc_release(u);
    return objc_autorelease(r);
}
static id kua_unarchiveData(Class c, SEL s, id data) { (void)c; (void)s; return unarchive_root(data, NULL); }
static id kua_unarchiveDataErr(Class c, SEL s, id data, id *err) { (void)c; (void)s; return unarchive_root(data, err); }
static id kua_unarchiveFile(Class c, SEL s, id path) {
    (void)c; (void)s; id d = path ? RT_SEND1(id, (id)cls("NSData"), "dataWithContentsOfFile:", id, path) : NULL;
    return d ? unarchive_root(d, NULL) : NULL;
}
static id kua_unarchivedOfClass(Class c, SEL s, Class want, id data, id *err) { (void)c; (void)s; (void)want; return unarchive_root(data, err); }
static id kua_initWithHook(id self, id archive, rt_kua_hook hook, void *ctx, id all) {
    if (!kua_setup(self, archive)) { objc_release(self); return NULL; }
    KUA(self)->hook = hook; KUA(self)->hookctx = ctx; KUA(self)->all = objc_retain(all);
    return self;
}
/* for nib.c: an unarchiver over a parsed keyed archive (+1), positioned at $top */
id rt_kua_new(id archive, rt_kua_hook hook, void *ctx, id all) {
    return kua_initWithHook(RT_SEND0(id, (id)RT_CLASS(NSKeyedUnarchiver), "alloc"), archive, hook, ctx, all);
}
/* make the object a key refers to the current one (its keys become the decoder's keys); 0 if absent */
int rt_kua_enter(id coder, const char *key) {
    long long u = uid_of(kua_value(coder, rt_str(key)));
    if (u <= 0 || (size_t)u >= KUA(coder)->n) return 0;
    id raw = rt_array_at(KUA(coder)->objects, (size_t)u);
    if (!is_kind(raw, "NSDictionary")) return 0;
    KUA(coder)->cur = raw;
    return 1;
}
/* the current object's keyed values, for hooks that need a key before decoding */
id rt_kua_value(id coder, const char *key) { return kua_object_value(coder, kua_value(coder, rt_str(key))); }

/* ================================================================ archiver */
struct kar {
    Class isa;
    id objects;        /* $objects being built */
    id top, cur;       /* $top and the dictionary of the object being encoded */
    id uids;           /* NSMapTable-like: object pointer (NSNumber) -> uid (NSNumber) */
    id classuids;      /* archived class name -> uid */
    id data, delegate, classnames;
    long format, secure, finished;
};
#define KAR(x) ((struct kar *)(x))
static id ptr_key(id o) { return rt_num_int((long long)(uintptr_t)o); }
static long long class_uid(id self, Class c);
static const char *archived_name(id self, Class c) {
    const char *n = rt_class_name(c);
    id over = KAR(self)->classnames ? rt_dict_get(KAR(self)->classnames, rt_str(n)) : NULL;
    if (!over && g_class_names) over = rt_dict_get(g_class_names, rt_str(n));
    return over ? rt_str_utf8(over) : n;
}
static long long class_uid(id self, Class c) {
    struct kar *k = KAR(self);
    const char *name = archived_name(self, c);
    id known = rt_dict_get(k->classuids, rt_str(name));
    if (known) return RT_SEND0(long long, known, "longLongValue");
    id chain = marray();
    for (Class p = c; p; p = p->superclass) RT_SEND1(void, chain, "addObject:", id, rt_str(p == c ? name : archived_name(self, p)));
    id d = mdict(); rt_dict_set(d, rt_str("$classname"), rt_str(name)); rt_dict_set(d, rt_str("$classes"), chain);
    long long u = (long long)RT_SEND0(unsigned long, k->objects, "count");
    RT_SEND1(void, k->objects, "addObject:", id, d);
    rt_dict_set(k->classuids, rt_str(name), rt_num_int(u));
    return u;
}
/* the class an object is archived as: Foundation collections under their public names */
static Class archive_class(id o) {
    static const char *pub[] = { "NSMutableArray", "NSArray", "NSMutableDictionary", "NSDictionary", "NSMutableSet", "NSSet", "NSMutableOrderedSet", "NSOrderedSet",
                                 "NSMutableString", "NSString", "NSMutableData", "NSData", "NSDate", "NSURL", "NSNull", "NSUUID", "NSValue", "NSMutableAttributedString", "NSAttributedString", NULL };
    for (int i = 0; pub[i]; i++) if (is_kind(o, pub[i])) {
        /* is it really mutable? our concrete classes may share one implementation */
        if (!strncmp(pub[i], "NSMutable", 9) && !(o->isa == cls(pub[i]) || rt_responds(o, rt_sel("addObject:")) || rt_responds(o, rt_sel("setObject:forKey:")) || rt_responds(o, rt_sel("appendString:")) || rt_responds(o, rt_sel("appendData:")))) continue;
        return cls(pub[i]);
    }
    if (rt_responds(o, rt_sel("classForKeyedArchiver"))) { Class c = RT_SEND0(Class, o, "classForKeyedArchiver"); if (c) return c; }
    return o->isa;
}
static long long kar_encode_obj(id self, id o);
static id encode_list(id self, id list) {
    id out = marray();
    for (size_t i = 0; list && i < rt_array_count(list); i++) RT_SEND1(void, out, "addObject:", id, uid_obj(kar_encode_obj(self, rt_array_at(list, i))));
    return out;
}
static long long kar_encode_obj(id self, id o) {
    struct kar *k = KAR(self);
    if (!o) return 0;
    if (k->delegate && rt_responds(k->delegate, rt_sel("archiver:willEncodeObject:"))) { o = RT_SEND2(id, k->delegate, "archiver:willEncodeObject:", id, self, id, o); if (!o) return 0; }
    id known = RT_SEND1(id, k->uids, "objectForKey:", id, ptr_key(o));
    if (known) return RT_SEND0(long long, known, "longLongValue");
    long long u = (long long)RT_SEND0(unsigned long, k->objects, "count");
    RT_SEND2(void, k->uids, "setObject:forKey:", id, rt_num_int(u), id, ptr_key(o));
    Class ac = archive_class(o);
    const char *an = rt_class_name(ac);
    /* inline plist values */
    if (!strcmp(an, "NSString") || (is_kind(o, "NSNumber") && !is_kind(o, "NSDecimalNumber")) || !strcmp(an, "NSData")) {
        RT_SEND1(void, k->objects, "addObject:", id, !strcmp(an, "NSString") ? RT_SEND0(id, o, "copy") : o);
        if (!strcmp(an, "NSString")) objc_release(rt_array_at(k->objects, (size_t)u));   /* balance the copy */
        return u;
    }
    id d = mdict();
    RT_SEND1(void, k->objects, "addObject:", id, d);
    id saved = k->cur; k->cur = d;
    if (!strcmp(an, "NSArray") || !strcmp(an, "NSMutableArray")) rt_dict_set(d, rt_str("NS.objects"), encode_list(self, o));
    else if (!strcmp(an, "NSSet") || !strcmp(an, "NSMutableSet") || !strcmp(an, "NSOrderedSet") || !strcmp(an, "NSMutableOrderedSet"))
        rt_dict_set(d, rt_str("NS.objects"), encode_list(self, RT_SEND0(id, o, !strncmp(an, "NSMutableOrdered", 16) || !strncmp(an, "NSOrdered", 9) ? "array" : "allObjects")));
    else if (!strcmp(an, "NSDictionary") || !strcmp(an, "NSMutableDictionary")) {
        id keys = RT_SEND0(id, o, "allKeys"), vals = marray();
        for (size_t i = 0; i < rt_array_count(keys); i++) RT_SEND1(void, vals, "addObject:", id, RT_SEND1(id, o, "objectForKey:", id, rt_array_at(keys, i)));
        rt_dict_set(d, rt_str("NS.keys"), encode_list(self, keys));
        rt_dict_set(d, rt_str("NS.objects"), encode_list(self, vals));
    }
    else if (!strcmp(an, "NSMutableString")) rt_dict_set(d, rt_str("NS.string"), RT_SEND0(id, o, "description"));
    else if (!strcmp(an, "NSMutableData")) rt_dict_set(d, rt_str("NS.data"), objc_autorelease(RT_SEND0(id, o, "copy")));
    else if (!strcmp(an, "NSDate")) rt_dict_set(d, rt_str("NS.time"), rt_num_dbl(RT_SEND0(double, o, "timeIntervalSinceReferenceDate")));
    else if (!strcmp(an, "NSURL")) { rt_dict_set(d, rt_str("NS.base"), uid_obj(0)); rt_dict_set(d, rt_str("NS.relative"), uid_obj(kar_encode_obj(self, RT_SEND0(id, o, "absoluteString")))); }
    else if (!strcmp(an, "NSNull")) {}
    else if (!strcmp(an, "NSUUID")) { uint8_t b[16]; RT_SEND1(void, o, "getUUIDBytes:", uint8_t *, b); rt_dict_set(d, rt_str("NS.uuidbytes"), objc_autorelease(rt_data_new(b, 16))); }
    else if (!strcmp(an, "NSAttributedString") || !strcmp(an, "NSMutableAttributedString")) {
        id str = RT_SEND0(id, o, "string");
        rt_dict_set(d, rt_str("NSString"), uid_obj(kar_encode_obj(self, str)));
        unsigned long len = RT_SEND0(unsigned long, str, "length");
        if (len) {
            struct { unsigned long loc, len; } eff = { 0, 0 };
            id attrs = ((id (*)(id, SEL, unsigned long, void *))objc_msgSend)(o, rt_sel("attributesAtIndex:effectiveRange:"), 0, &eff);
            if (attrs) rt_dict_set(d, rt_str("NSAttributes"), uid_obj(kar_encode_obj(self, attrs)));
        }
    }
    else if (!strcmp(an, "NSValue")) {
        const char *t = RT_SEND0(const char *, o, "objCType");
        char buf[96];
        if (t && strstr(t, "Point")) { KPoint p = RT_SEND0(KPoint, o, "CGPointValue"); snprintf(buf, sizeof buf, "{%g, %g}", p.x, p.y); rt_dict_set(d, rt_str("NS.special"), rt_num_int(1)); rt_dict_set(d, rt_str("NS.pointval"), rt_str(buf)); }
        else if (t && strstr(t, "Size")) { KSize z = RT_SEND0(KSize, o, "CGSizeValue"); snprintf(buf, sizeof buf, "{%g, %g}", z.w, z.h); rt_dict_set(d, rt_str("NS.special"), rt_num_int(2)); rt_dict_set(d, rt_str("NS.sizeval"), rt_str(buf)); }
        else if (t && strstr(t, "Rect")) { KRect r = RT_SEND0(KRect, o, "CGRectValue"); snprintf(buf, sizeof buf, "{{%g, %g}, {%g, %g}}", r.o.x, r.o.y, r.s.w, r.s.h); rt_dict_set(d, rt_str("NS.special"), rt_num_int(3)); rt_dict_set(d, rt_str("NS.rectval"), rt_str(buf)); }
        else if (t && strstr(t, "Range")) { struct { unsigned long loc, len; } r = RT_SEND0(__typeof__(r), o, "rangeValue"); rt_dict_set(d, rt_str("NS.special"), rt_num_int(4)); rt_dict_set(d, rt_str("NS.rangeval.location"), rt_num_int((long long)r.loc)); rt_dict_set(d, rt_str("NS.rangeval.length"), rt_num_int((long long)r.len)); }
    }
    else if (rt_responds(o, rt_sel("encodeWithCoder:"))) RT_SEND1(void, o, "encodeWithCoder:", id, self);
    else {
        char m[256]; snprintf(m, sizeof m, "*** -[NSKeyedArchiver encodeObject:forKey:]: object of class %s does not implement encodeWithCoder:", o->isa ? rt_class_name(o->isa) : "?");
        rt_soft_error(m);
    }
    k->cur = saved;
    rt_dict_set(d, rt_str("$class"), uid_obj(class_uid(self, ac)));
    return u;
}
static id kar_init(id self, SEL s) {
    (void)s; struct kar *k = KAR(self);
    k->objects = RT_SEND0(id, RT_SEND0(id, (id)cls("NSMutableArray"), "alloc"), "init");
    RT_SEND1(void, k->objects, "addObject:", id, rt_str("$null"));
    k->top = rt_dict_new(NULL, NULL, 0); k->cur = k->top;
    k->uids = rt_dict_new(NULL, NULL, 0); k->classuids = rt_dict_new(NULL, NULL, 0);
    k->format = 200;   /* NSPropertyListBinaryFormat_v1_0 */
    return self;
}
static id kar_initSecure(id self, SEL s, long secure) { self = kar_init(self, s); KAR(self)->secure = secure & 0xff; return self; }
static id kar_initForWriting(id self, SEL s, id data) { self = kar_init(self, s); KAR(self)->data = objc_retain(data); return self; }
static void kar_dealloc(id self, SEL s) {
    (void)s; struct kar *k = KAR(self);
    objc_release(k->objects); objc_release(k->top); objc_release(k->uids); objc_release(k->classuids); objc_release(k->data); objc_release(k->classnames);
    rt_dealloc_instance(self);
}
static void kar_set(id self, id key, id v) { if (key) rt_dict_set(KAR(self)->cur, key, v); }
static void kar_encodeObject(id self, SEL s, id o, id key) { (void)s; kar_set(self, key, uid_obj(kar_encode_obj(self, o))); }
static void kar_encodeBool(id self, SEL s, long v, id key) { (void)s; kar_set(self, key, RT_SEND1(id, (id)cls("NSNumber"), "numberWithBool:", long, v & 0xff)); }
static void kar_encodeInt(id self, SEL s, long long v, id key) { (void)s; kar_set(self, key, rt_num_int(v)); }
static void kar_encodeInt32(id self, SEL s, int v, id key) { (void)s; kar_set(self, key, rt_num_int(v)); }
static void kar_encodeDouble(id self, SEL s, double v, id key) { (void)s; kar_set(self, key, rt_num_dbl(v)); }
static void kar_encodeFloat(id self, SEL s, float v, id key) { (void)s; kar_set(self, key, rt_num_dbl(v)); }
static void kar_encodeBytes(id self, SEL s, const void *b, unsigned long n, id key) { (void)s; kar_set(self, key, objc_autorelease(rt_data_new(b, n))); }
static void kar_encodeRect(id self, SEL s, KRect r, id key) { (void)s; char b[128]; snprintf(b, sizeof b, "{{%g, %g}, {%g, %g}}", r.o.x, r.o.y, r.s.w, r.s.h); kar_set(self, key, uid_obj(kar_encode_obj(self, rt_str(b)))); }
static void kar_encodePoint(id self, SEL s, KPoint p, id key) { (void)s; char b[96]; snprintf(b, sizeof b, "{%g, %g}", p.x, p.y); kar_set(self, key, uid_obj(kar_encode_obj(self, rt_str(b)))); }
static void kar_encodeSize(id self, SEL s, KSize z, id key) { (void)s; char b[96]; snprintf(b, sizeof b, "{%g, %g}", z.w, z.h); kar_set(self, key, uid_obj(kar_encode_obj(self, rt_str(b)))); }
static void kar_encodeInsets(id self, SEL s, KInsets i, id key) { (void)s; char b[128]; snprintf(b, sizeof b, "{%g, %g, %g, %g}", i.top, i.left, i.bottom, i.right); kar_set(self, key, uid_obj(kar_encode_obj(self, rt_str(b)))); }
static void kar_encodeAffine(id self, SEL s, KAffine a, id key) { (void)s; char b[160]; snprintf(b, sizeof b, "[%g, %g, %g, %g, %g, %g]", a.a, a.b, a.c, a.d, a.tx, a.ty); kar_set(self, key, uid_obj(kar_encode_obj(self, rt_str(b)))); }
static void kar_encodeRoot(id self, SEL s, id o) { (void)s; kar_set(self, rt_str("root"), uid_obj(kar_encode_obj(self, o))); }
id rt_bplist_write(id plist);
static void kar_finish(id self, SEL s) {
    (void)s; struct kar *k = KAR(self);
    if (k->finished) return;
    k->finished = 1;
    id archive = mdict();
    rt_dict_set(archive, rt_str("$archiver"), rt_str("NSKeyedArchiver"));
    rt_dict_set(archive, rt_str("$version"), rt_num_int(100000));
    rt_dict_set(archive, rt_str("$top"), k->top);
    rt_dict_set(archive, rt_str("$objects"), k->objects);
    id bytes;
    if (k->format == 100) { id rt_plist_xml(id o); bytes = rt_plist_xml(archive); }
    else bytes = rt_bplist_write(archive);
    if (!k->data) k->data = RT_SEND0(id, RT_SEND0(id, (id)cls("NSMutableData"), "alloc"), "init");
    RT_SEND1(void, k->data, "appendData:", id, bytes);
    if (k->delegate && rt_responds(k->delegate, rt_sel("archiverDidFinish:"))) RT_SEND1(void, k->delegate, "archiverDidFinish:", id, self);
}
static id kar_encodedData(id self, SEL s) { kar_finish(self, s); return KAR(self)->data; }
static long kar_format(id self, SEL s) { (void)s; return KAR(self)->format; }
static void kar_setFormat(id self, SEL s, long f) { (void)s; KAR(self)->format = f; }
static rt_bool kar_secure(id self, SEL s) { (void)s; return KAR(self)->secure != 0; }
static void kar_setSecure(id self, SEL s, long v) { (void)s; KAR(self)->secure = v & 0xff; }
static id kar_delegate(id self, SEL s) { (void)s; return KAR(self)->delegate; }
static void kar_setDelegate(id self, SEL s, id d) { (void)s; KAR(self)->delegate = d; }
static void kar_setClassName(id self, SEL s, id name, Class c) {
    (void)s; struct kar *k = KAR(self);
    if (!k->classnames) k->classnames = rt_dict_new(NULL, NULL, 0);
    if (c && name) rt_dict_set(k->classnames, rt_str(rt_class_name(c)), name);
}
static id kar_classNameFor(id self, SEL s, Class c) { (void)s; return c && KAR(self)->classnames ? rt_dict_get(KAR(self)->classnames, rt_str(rt_class_name(c))) : NULL; }
static void kar_cSetClassName(Class c0, SEL s, id name, Class c) { (void)c0; (void)s; if (!g_class_names) g_class_names = rt_dict_new(NULL, NULL, 0); if (c && name) rt_dict_set(g_class_names, rt_str(rt_class_name(c)), name); }
static id kar_cClassNameFor(Class c0, SEL s, Class c) { (void)c0; (void)s; return c && g_class_names ? rt_dict_get(g_class_names, rt_str(rt_class_name(c))) : NULL; }
static void kar_encodeValue(id self, SEL s, const char *type, const void *p) { (void)self; (void)s; (void)type; (void)p; rt_soft_error("non-keyed NSCoder encoding is not supported"); }
static id archive_root(id root) {
    id a = kar_init(RT_SEND0(id, (id)RT_CLASS(NSKeyedArchiver), "alloc"), NULL);
    kar_encodeRoot(a, NULL, root);
    id d = objc_retain(kar_encodedData(a, NULL));
    objc_release(a);
    return objc_autorelease(d);
}
static id kar_archivedData(Class c, SEL s, id root) { (void)c; (void)s; return archive_root(root); }
static id kar_archivedDataSecure(Class c, SEL s, id root, long secure, id *err) { (void)c; (void)s; (void)secure; if (err) *err = NULL; return archive_root(root); }
static rt_bool kar_archiveToFile(Class c, SEL s, id root, id path) {
    (void)c; (void)s; if (!path) return 0;
    return RT_SEND2(long, archive_root(root), "writeToFile:atomically:", id, path, long, 1) & 0xff;
}

/* ===================================================== binary plist writer */
struct bpw { id *objs; size_t n, cap; rt_sb out; };
static size_t bpw_add(struct bpw *w, id o) {
    if (w->n == w->cap) { w->cap = w->cap ? w->cap * 2 : 64; w->objs = realloc(w->objs, w->cap * sizeof(id)); }
    w->objs[w->n] = o;
    return w->n++;
}
/* flatten: every reachable object gets an index (no de-duplication needed) */
static size_t bpw_collect(struct bpw *w, id o, int depth);
static void be_put(rt_sb *sb, uint64_t v, int n) { for (int i = n - 1; i >= 0; i--) rt_sb_putc(sb, (char)(v >> (8 * i))); }
static int int_size(uint64_t v) { return v <= 0xff ? 1 : v <= 0xffff ? 2 : v <= 0xffffffffu ? 4 : 8; }
static void put_marker_len(rt_sb *sb, int hi, uint64_t n) {
    if (n < 15) { rt_sb_putc(sb, (char)(hi << 4 | (int)n)); return; }
    rt_sb_putc(sb, (char)(hi << 4 | 0xf));
    int sz = int_size(n), lg = sz == 1 ? 0 : sz == 2 ? 1 : sz == 4 ? 2 : 3;
    rt_sb_putc(sb, (char)(0x10 | lg)); be_put(sb, n, sz);
}
id rt_bplist_write(id plist) {
    struct bpw w = { 0 }; rt_sb_init(&w.out);
    bpw_collect(&w, plist, 0);
    int refsz = w.n <= 0xff ? 1 : w.n <= 0xffff ? 2 : 4;
    uint64_t *offs = calloc(w.n + 1, sizeof *offs);
    rt_sb_puts(&w.out, "bplist00");
    /* children were collected right after their parent: re-walk to map objects to their child indexes */
    size_t next = 1;
    for (size_t i = 0; i < w.n; i++) {
        id o = w.objs[i]; rt_sb *sb = &w.out;
        offs[i] = sb->len;
        long long u = uid_of(o);
        if (!o || is_kind(o, "NSNull")) rt_sb_putc(sb, 0x00);
        else if (u >= 0) { int sz = int_size((uint64_t)u); rt_sb_putc(sb, (char)(0x80 | (sz - 1))); be_put(sb, (uint64_t)u, sz); }
        else if (is_kind(o, "NSString")) {
            const char *s = rt_str_utf8(o); int ascii = 1;
            for (const unsigned char *p = (const unsigned char *)s; *p; p++) if (*p >= 0x80) { ascii = 0; break; }
            if (ascii) { size_t n = strlen(s); put_marker_len(sb, 5, n); for (size_t k = 0; k < n; k++) rt_sb_putc(sb, s[k]); }
            else { size_t n; unichar *tf = NULL; const unichar *u16 = rt_str_utf16(o, &n, &tf); put_marker_len(sb, 6, n); for (size_t k = 0; k < n; k++) be_put(sb, u16[k], 2); free(tf); }
        }
        else if (is_kind(o, "NSNumber")) {
            const char *t = RT_SEND0(const char *, o, "objCType");
            if (t && t[0] == 'c') rt_sb_putc(sb, RT_SEND0(long, o, "boolValue") & 0xff ? 0x09 : 0x08);
            else if (t && (t[0] == 'd' || t[0] == 'f')) { double d = RT_SEND0(double, o, "doubleValue"); uint64_t b; memcpy(&b, &d, 8); rt_sb_putc(sb, 0x23); be_put(sb, b, 8); }
            else { long long v = RT_SEND0(long long, o, "longLongValue"); int sz = v < 0 ? 8 : int_size((uint64_t)v); rt_sb_putc(sb, (char)(0x10 | (sz == 1 ? 0 : sz == 2 ? 1 : sz == 4 ? 2 : 3))); be_put(sb, (uint64_t)v, sz); }
        }
        else if (is_kind(o, "NSData")) { size_t n; const char *b = rt_data_bytes(o, &n); put_marker_len(sb, 4, n); for (size_t k = 0; k < n; k++) rt_sb_putc(sb, b[k]); }
        else if (is_kind(o, "NSDate")) { double d = RT_SEND0(double, o, "timeIntervalSinceReferenceDate"); uint64_t b; memcpy(&b, &d, 8); rt_sb_putc(sb, 0x33); be_put(sb, b, 8); }
        else if (is_kind(o, "NSArray")) {
            size_t n = rt_array_count(o); put_marker_len(sb, 0xa, n);
            for (size_t k = 0; k < n; k++) be_put(sb, next++, refsz);
        }
        else if (is_kind(o, "NSSet")) {
            size_t n = RT_SEND0(unsigned long, o, "count"); put_marker_len(sb, 0xc, n);
            for (size_t k = 0; k < n; k++) be_put(sb, next++, refsz);
        }
        else if (is_kind(o, "NSDictionary")) {
            size_t n = RT_SEND0(unsigned long, o, "count"); put_marker_len(sb, 0xd, n);
            size_t first = next; next += 2 * n;
            for (size_t k = 0; k < n; k++) be_put(sb, first + 2 * k, refsz);       /* keys */
            for (size_t k = 0; k < n; k++) be_put(sb, first + 2 * k + 1, refsz);   /* values */
        }
        else { char *t = rt_describe(o); size_t n = strlen(t); put_marker_len(sb, 5, n); for (size_t k = 0; k < n; k++) rt_sb_putc(sb, t[k]); free(t); }
    }
    uint64_t table = w.out.len;
    int offsz = int_size(table);
    for (size_t i = 0; i < w.n; i++) be_put(&w.out, offs[i], offsz);
    for (int i = 0; i < 6; i++) rt_sb_putc(&w.out, 0);
    rt_sb_putc(&w.out, (char)offsz); rt_sb_putc(&w.out, (char)refsz);
    be_put(&w.out, w.n, 8); be_put(&w.out, 0, 8); be_put(&w.out, table, 8);
    id data = objc_autorelease(rt_data_new(w.out.buf, w.out.len));
    free(w.out.buf); free(offs); free(w.objs);
    return data;
}
/* Collection order must match the writer's: breadth-first, children of
 * object i taking the next free indexes in order (dictionaries: k0 v0 k1 v1 ...). */
static size_t bpw_collect(struct bpw *w, id root, int depth) {
    (void)depth;
    bpw_add(w, root);
    for (size_t i = 0; i < w->n; i++) {
        id o = w->objs[i];
        if (!o || uid_of(o) >= 0) continue;
        if (is_kind(o, "NSArray")) { size_t n = rt_array_count(o); for (size_t k = 0; k < n; k++) bpw_add(w, rt_array_at(o, k)); }
        else if (is_kind(o, "NSSet")) { id all = RT_SEND0(id, o, "allObjects"); for (size_t k = 0; k < rt_array_count(all); k++) bpw_add(w, rt_array_at(all, k)); }
        else if (is_kind(o, "NSDictionary") && !is_kind(o, "NSNull")) {
            id keys = RT_SEND0(id, o, "allKeys");
            for (size_t k = 0; k < rt_array_count(keys); k++) { id key = rt_array_at(keys, k); bpw_add(w, key); bpw_add(w, RT_SEND1(id, o, "objectForKey:", id, key)); }
        }
    }
    return 0;
}

void rt_archiver_init(void) {
    Class coder = cls("NSCoder");
    NEWCLASS(NSKeyedUnarchiver, coder, sizeof(struct kua) - sizeof(void *));
    Class u = RT_CLASS(NSKeyedUnarchiver);
    IM(u, "initForReadingWithData:", kua_initForReadingWithData); IM(u, "initForReadingFromData:error:", kua_initForReadingFromData);
    IM(u, "dealloc", kua_dealloc);
    IM(u, "decodeObjectForKey:", kua_decodeObject); IM(u, "decodeObjectOfClass:forKey:", kua_decodeObjectOfClass);
    IM(u, "decodeObjectOfClasses:forKey:", kua_decodeObjectOfClass); IM(u, "decodeArrayOfObjectsOfClass:forKey:", kua_decodeObjectOfClass);
    IM(u, "decodeArrayOfObjectsOfClasses:forKey:", kua_decodeObjectOfClass);
    IM(u, "decodeTopLevelObjectForKey:error:", kua_decodeTopLevel); IM(u, "decodeTopLevelObjectOfClass:forKey:error:", kua_decodeTopLevelOfClass);
    IM(u, "decodeTopLevelObjectOfClasses:forKey:error:", kua_decodeTopLevelOfClass);
    IM(u, "decodeBoolForKey:", kua_decodeBool); IM(u, "decodeIntegerForKey:", kua_decodeInteger); IM(u, "decodeIntForKey:", kua_decodeInt32);
    IM(u, "decodeInt32ForKey:", kua_decodeInt32); IM(u, "decodeInt64ForKey:", kua_decodeInt64);
    IM(u, "decodeDoubleForKey:", kua_decodeDouble); IM(u, "decodeFloatForKey:", kua_decodeFloat);
    IM(u, "decodeBytesForKey:returnedLength:", kua_decodeBytes); IM(u, "containsValueForKey:", kua_contains);
    IM(u, "decodeCGRectForKey:", kua_decodeRect); IM(u, "decodeCGPointForKey:", kua_decodePoint); IM(u, "decodeCGSizeForKey:", kua_decodeSize);
    IM(u, "decodeUIEdgeInsetsForKey:", kua_decodeInsets); IM(u, "decodeCGAffineTransformForKey:", kua_decodeAffine);
    IM(u, "allowsKeyedCoding", kua_yes); IM(u, "requiresSecureCoding", kua_secure); IM(u, "setRequiresSecureCoding:", kua_setSecure);
    IM(u, "decodingFailurePolicy", kua_policy); IM(u, "setDecodingFailurePolicy:", kua_setPolicy); IM(u, "error", kua_error);
    IM(u, "finishDecoding", kua_finish); IM(u, "delegate", kua_delegate); IM(u, "setDelegate:", kua_setDelegate);
    IM(u, "setClass:forClassName:", kua_setClassForName); IM(u, "classForClassName:", kua_classForName);
    IM(u, "decodeValueOfObjCType:at:", kua_decodeValue);
    CM(u, "setClass:forClassName:", kua_cSetClassForName); CM(u, "classForClassName:", kua_cClassForName);
    CM(u, "unarchiveObjectWithData:", kua_unarchiveData); CM(u, "unarchiveObjectWithData:error:", kua_unarchiveDataErr);
    CM(u, "unarchiveTopLevelObjectWithData:error:", kua_unarchiveDataErr); CM(u, "unarchiveObjectWithFile:", kua_unarchiveFile);
    CM(u, "unarchivedObjectOfClass:fromData:error:", kua_unarchivedOfClass); CM(u, "unarchivedObjectOfClasses:fromData:error:", kua_unarchivedOfClass);
    CM(u, "unarchivedArrayOfObjectsOfClass:fromData:error:", kua_unarchivedOfClass);

    NEWCLASS(NSKeyedArchiver, coder, sizeof(struct kar) - sizeof(void *));
    Class a = RT_CLASS(NSKeyedArchiver);
    IM(a, "init", kar_init); IM(a, "initRequiringSecureCoding:", kar_initSecure); IM(a, "initForWritingWithMutableData:", kar_initForWriting);
    IM(a, "dealloc", kar_dealloc);
    IM(a, "encodeObject:forKey:", kar_encodeObject); IM(a, "encodeConditionalObject:forKey:", kar_encodeObject);
    IM(a, "encodeBool:forKey:", kar_encodeBool); IM(a, "encodeInteger:forKey:", kar_encodeInt); IM(a, "encodeInt64:forKey:", kar_encodeInt);
    IM(a, "encodeInt:forKey:", kar_encodeInt32); IM(a, "encodeInt32:forKey:", kar_encodeInt32);
    IM(a, "encodeDouble:forKey:", kar_encodeDouble); IM(a, "encodeFloat:forKey:", kar_encodeFloat);
    IM(a, "encodeBytes:length:forKey:", kar_encodeBytes);
    IM(a, "encodeCGRect:forKey:", kar_encodeRect); IM(a, "encodeCGPoint:forKey:", kar_encodePoint); IM(a, "encodeCGSize:forKey:", kar_encodeSize);
    IM(a, "encodeUIEdgeInsets:forKey:", kar_encodeInsets); IM(a, "encodeCGAffineTransform:forKey:", kar_encodeAffine);
    IM(a, "encodeRootObject:", kar_encodeRoot); IM(a, "finishEncoding", kar_finish); IM(a, "encodedData", kar_encodedData);
    IM(a, "outputFormat", kar_format); IM(a, "setOutputFormat:", kar_setFormat);
    IM(a, "requiresSecureCoding", kar_secure); IM(a, "setRequiresSecureCoding:", kar_setSecure);
    IM(a, "allowsKeyedCoding", kua_yes); IM(a, "delegate", kar_delegate); IM(a, "setDelegate:", kar_setDelegate);
    IM(a, "setClassName:forClass:", kar_setClassName); IM(a, "classNameForClass:", kar_classNameFor);
    IM(a, "encodeValueOfObjCType:at:", kar_encodeValue);
    CM(a, "setClassName:forClass:", kar_cSetClassName); CM(a, "classNameForClass:", kar_cClassNameFor);
    CM(a, "archivedDataWithRootObject:", kar_archivedData); CM(a, "archivedDataWithRootObject:requiringSecureCoding:error:", kar_archivedDataSecure);
    CM(a, "archiveRootObject:toFile:", kar_archiveToFile);

    iosrt_NSKeyedArchiveRootObjectKey = rt_str_new("root");
    iosrt___NSArray0__ = RT_SEND0(id, RT_SEND0(id, (id)cls("NSArray"), "alloc"), "init");
    iosrt___NSDictionary0__ = RT_SEND0(id, RT_SEND0(id, (id)cls("NSDictionary"), "alloc"), "init");
    iosrt_NSInvalidArchiveOperationException = rt_str_new("NSInvalidArchiveOperationException");
    iosrt_NSInvalidUnarchiveOperationException = rt_str_new("NSInvalidUnarchiveOperationException");
}
