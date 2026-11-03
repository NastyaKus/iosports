/*
 * Compiled NIB loading (NIBArchive format, what ibtool emits for iOS):
 * parser, UINibDecoder (a keyed NSCoder over one archived object at a time),
 * object graph instantiation with File's Owner / external proxies,
 * UIClassSwapper (custom classes), outlet / action / outlet-collection
 * connections, user-defined runtime attributes, awakeFromNib, plus the
 * initWithCoder: implementations of the UIKit classes the runtime provides.
 *
 * Archive layout: "NIBArchive", two int32 versions, then (count, offset)
 * pairs for objects, keys, values and class names. Integers in the tables
 * are 7-bit varints whose high bit marks the LAST byte.
 */
#include "uikit_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSCoder);
RT_DECLARE_CLASS(NSMutableArray);
RT_DECLARE_CLASS(NSString);
RT_DECLARE_CLASS(NSNumber);
RT_DECLARE_CLASS(NSBundle);
RT_DECLARE_CLASS(UIView);
RT_DECLARE_CLASS(UIColor);
RT_DECLARE_CLASS(UIFont);
RT_DECLARE_CLASS(UIFontDescriptor);
RT_DECLARE_CLASS(UIImage);
RT_DECLARE_CLASS(UILabel);
RT_DECLARE_CLASS(UIImageView);
RT_DECLARE_CLASS(UIControl);
RT_DECLARE_CLASS(UIButton);
RT_DECLARE_CLASS(UIButtonContent);
RT_DECLARE_CLASS(UIScrollView);
RT_DECLARE_CLASS(UIPageControl);
RT_DECLARE_CLASS(UISlider);
RT_DECLARE_CLASS(UISwitch);
RT_DECLARE_CLASS(UISegmentedControl);
RT_DECLARE_CLASS(UIActivityIndicatorView);
RT_DECLARE_CLASS(UIProgressView);
RT_DECLARE_CLASS(UITextField);
RT_DECLARE_CLASS(UITextView);
RT_DECLARE_CLASS(UITableView);
RT_DECLARE_CLASS(UIToolbar);
RT_DECLARE_CLASS(UINavigationBar);
RT_DECLARE_CLASS(UIBarButtonItem);
RT_DECLARE_CLASS(UINavigationItem);
RT_DECLARE_CLASS(UIViewController);
RT_DECLARE_CLASS(UIGestureRecognizer);
RT_DECLARE_CLASS(UIWindow);
RT_DEFINE_CLASS(UINibDecoder);
RT_DEFINE_CLASS(UINib);
RT_DEFINE_CLASS(UIRuntimeConnection);
RT_DEFINE_CLASS(UIRuntimeOutletConnection);
RT_DEFINE_CLASS(UIRuntimeEventConnection);
RT_DEFINE_CLASS(UIRuntimeOutletCollectionConnection);
RT_DEFINE_CLASS(UIProxyObject);

id rt_data_new(const void *b, size_t n);
const void *rt_data_bytes(id d, size_t *n);
id rt_button_content_new(id title, id attrTitle, id titleColor, id shadowColor, id image, id bgImage);
void rt_button_apply_content(id button, unsigned long state, id content);
id iosrt_UINibExternalObjects;

/* ================================================================ parser */
enum { NV_I8, NV_I16, NV_I32, NV_I64, NV_TRUE, NV_FALSE, NV_F32, NV_F64, NV_DATA, NV_NIL, NV_OBJ };
struct nval { int key; int type; union { long long i; double d; struct { const uint8_t *p; size_t n; } data; uint32_t obj; } u; };
struct nobj { int cls, vstart, vcount; };
struct nibfile {
    uint8_t *buf; size_t len;
    struct nobj *objs; int nobjs; char **keys; int nkeys; struct nval *vals; int nvals; char **classes; int nclasses;
    id *decoded; char *state;   /* 0 = not yet, 1 = in progress, 2 = done */
    id owner, external;
    id all;                     /* every decoded object (retained), for awakeFromNib and cleanup */
};
static int varint(const uint8_t *b, size_t len, size_t *o, unsigned long long *out) {
    unsigned long long r = 0; int sh = 0;
    while (*o < len) {
        uint8_t c = b[(*o)++];
        r |= (unsigned long long)(c & 0x7f) << sh; sh += 7;
        if (c & 0x80) { *out = r; return 1; }
        if (sh > 63) return 0;
    }
    return 0;
}
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static void nib_free(struct nibfile *nf) {
    if (!nf) return;
    for (int i = 0; i < nf->nkeys; i++) free(nf->keys[i]);
    for (int i = 0; i < nf->nclasses; i++) free(nf->classes[i]);
    free(nf->keys); free(nf->classes); free(nf->objs); free(nf->vals); free(nf->decoded); free(nf->state); free(nf->buf);
    objc_release(nf->all);
    free(nf);
}
static struct nibfile *nib_parse(uint8_t *b, size_t len) {
    if (len < 50 || memcmp(b, "NIBArchive", 10)) return NULL;
    struct nibfile *nf = calloc(1, sizeof *nf); nf->buf = b; nf->len = len;
    uint32_t h[10]; for (int i = 0; i < 10; i++) h[i] = rd32(b + 10 + i * 4);
    uint32_t no = h[2], oo = h[3], nk = h[4], ok = h[5], nv = h[6], ov = h[7], nc = h[8], oc = h[9];
    size_t o; unsigned long long x, y, z;
    nf->classes = calloc(nc + 1, sizeof(char *)); nf->nclasses = (int)nc; o = oc;
    for (uint32_t i = 0; i < nc; i++) {
        if (!varint(b, len, &o, &x) || !varint(b, len, &o, &y)) goto bad;
        o += 4 * y; if (o + x > len) goto bad;
        nf->classes[i] = strndup((const char *)b + o, x); o += x;
    }
    nf->keys = calloc(nk + 1, sizeof(char *)); nf->nkeys = (int)nk; o = ok;
    for (uint32_t i = 0; i < nk; i++) { if (!varint(b, len, &o, &x) || o + x > len) goto bad; nf->keys[i] = strndup((const char *)b + o, x); o += x; }
    nf->vals = calloc(nv + 1, sizeof(struct nval)); nf->nvals = (int)nv; o = ov;
    for (uint32_t i = 0; i < nv; i++) {
        struct nval *v = &nf->vals[i];
        if (!varint(b, len, &o, &x) || o >= len) goto bad;
        v->key = (int)x; v->type = b[o++];
        switch (v->type) {
        case NV_I8: v->u.i = (int8_t)b[o]; o += 1; break;
        case NV_I16: { int16_t t; memcpy(&t, b + o, 2); v->u.i = t; o += 2; break; }
        case NV_I32: { int32_t t; memcpy(&t, b + o, 4); v->u.i = t; o += 4; break; }
        case NV_I64: { int64_t t; memcpy(&t, b + o, 8); v->u.i = t; o += 8; break; }
        case NV_TRUE: v->u.i = 1; break;
        case NV_FALSE: v->u.i = 0; break;
        case NV_F32: { float t; memcpy(&t, b + o, 4); v->u.d = t; o += 4; break; }
        case NV_F64: { double t; memcpy(&t, b + o, 8); v->u.d = t; o += 8; break; }
        case NV_DATA: if (!varint(b, len, &o, &x) || o + x > len) goto bad; v->u.data.p = b + o; v->u.data.n = x; o += x; break;
        case NV_NIL: break;
        case NV_OBJ: v->u.obj = rd32(b + o); o += 4; break;
        default: goto bad;
        }
        if (o > len) goto bad;
    }
    nf->objs = calloc(no + 1, sizeof(struct nobj)); nf->nobjs = (int)no; o = oo;
    for (uint32_t i = 0; i < no; i++) {
        if (!varint(b, len, &o, &x) || !varint(b, len, &o, &y) || !varint(b, len, &o, &z)) goto bad;
        nf->objs[i].cls = (int)x; nf->objs[i].vstart = (int)y; nf->objs[i].vcount = (int)z;
        if (x >= nc || y + z > nv) goto bad;
    }
    nf->decoded = calloc(no + 1, sizeof(id)); nf->state = calloc(no + 1, 1);
    nf->all = RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "alloc"), "init");
    return nf;
bad:
    rt_warn("NIB: malformed NIBArchive");
    nf->buf = NULL; nib_free(nf);
    return NULL;
}

/* ============================================================= UINibDecoder */
struct nibcoder { Class isa; struct nibfile *nf; int obj; };
#define NC(x) ((struct nibcoder *)(x))
static id decode_object(struct nibfile *nf, int idx);
static const struct nval *find_val(id coder, const char *key) {
    struct nibcoder *c = NC(coder); struct nibfile *nf = c->nf;
    if (!nf || c->obj < 0) return NULL;
    const struct nobj *o = &nf->objs[c->obj];
    for (int i = 0; i < o->vcount; i++) { const struct nval *v = &nf->vals[o->vstart + i]; if (!strcmp(nf->keys[v->key], key)) return v; }
    return NULL;
}
static const char *kstr(id key) { return key ? rt_str_utf8(key) : ""; }
static id nc_decodeObject(id self, SEL s, id key) {
    (void)s; const struct nval *v = find_val(self, kstr(key));
    if (!v) return NULL;
    switch (v->type) {
    case NV_OBJ: return decode_object(NC(self)->nf, (int)v->u.obj);
    case NV_DATA: return objc_autorelease(rt_data_new(v->u.data.p, v->u.data.n));
    case NV_TRUE: case NV_FALSE: return RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithBool:", long, v->u.i);
    case NV_F32: case NV_F64: return RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithDouble:", double, v->u.d);
    case NV_NIL: return NULL;
    default: return RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithLongLong:", long long, v->u.i);
    }
}
static id nc_decodeObjectOfClass(id self, SEL s, Class c, id key) { (void)c; return nc_decodeObject(self, s, key); }
static long long num_val(const struct nval *v) {
    if (!v) return 0;
    if (v->type == NV_F32 || v->type == NV_F64) return (long long)v->u.d;
    if (v->type == NV_OBJ || v->type == NV_DATA || v->type == NV_NIL) return 0;
    return v->u.i;
}
static double dbl_val(const struct nval *v) {
    if (!v) return 0;
    if (v->type == NV_F32 || v->type == NV_F64) return v->u.d;
    return (double)num_val(v);
}
static rt_bool nc_decodeBool(id self, SEL s, id key) { (void)s; return num_val(find_val(self, kstr(key))) != 0; }
static long nc_decodeInteger(id self, SEL s, id key) { (void)s; return (long)num_val(find_val(self, kstr(key))); }
static int nc_decodeInt32(id self, SEL s, id key) { (void)s; return (int)num_val(find_val(self, kstr(key))); }
static long long nc_decodeInt64(id self, SEL s, id key) { (void)s; return num_val(find_val(self, kstr(key))); }
static double nc_decodeDouble(id self, SEL s, id key) { (void)s; return dbl_val(find_val(self, kstr(key))); }
static float nc_decodeFloat(id self, SEL s, id key) { (void)s; return (float)dbl_val(find_val(self, kstr(key))); }
static rt_bool nc_contains(id self, SEL s, id key) { (void)s; return find_val(self, kstr(key)) != NULL; }
static rt_bool nc_yes(id self, SEL s) { (void)self; (void)s; return 1; }
static rt_bool nc_no(id self, SEL s) { (void)self; (void)s; return 0; }
/* geometry blobs: one type byte (6 = float32, 7 = float64) then the components; old nibs use "{x, y}" strings */
static int decode_doubles(id coder, const char *key, double *out, int n) {
    for (int i = 0; i < n; i++) out[i] = 0;
    const struct nval *v = find_val(coder, key);
    if (!v) return 0;
    if (v->type == NV_DATA && v->u.data.n >= 1) {
        const uint8_t *p = v->u.data.p; size_t len = v->u.data.n;
        int t = p[0];
        size_t sz = t == 6 ? 4 : 8;
        if (len >= 1 + sz * n) {
            for (int i = 0; i < n; i++) {
                if (sz == 8) memcpy(&out[i], p + 1 + 8 * i, 8);
                else { float f; memcpy(&f, p + 1 + 4 * i, 4); out[i] = f; }
            }
            return 1;
        }
        return 0;
    }
    if (v->type == NV_OBJ) {
        id o = decode_object(NC(coder)->nf, (int)v->u.obj);
        if (o && rt_is_kind_of(o, RT_CLASS(NSString))) {
            const char *str = rt_str_utf8(o); int k = 0;
            while (*str && k < n) { if ((*str >= '0' && *str <= '9') || *str == '-' || *str == '.') { char *e; out[k++] = strtod(str, &e); str = e; } else str++; }
            return k == n;
        }
    }
    return 0;
}
static CGRect nc_decodeRect(id self, SEL s, id key) { (void)s; double d[4]; decode_doubles(self, kstr(key), d, 4); return (CGRect){ { d[0], d[1] }, { d[2], d[3] } }; }
static CGPoint nc_decodePoint(id self, SEL s, id key) { (void)s; double d[2]; decode_doubles(self, kstr(key), d, 2); return (CGPoint){ d[0], d[1] }; }
static CGSize nc_decodeSize(id self, SEL s, id key) { (void)s; double d[2]; decode_doubles(self, kstr(key), d, 2); return (CGSize){ d[0], d[1] }; }
static UIEdgeInsets nc_decodeInsets(id self, SEL s, id key) { (void)s; double d[4]; decode_doubles(self, kstr(key), d, 4); return (UIEdgeInsets){ d[0], d[1], d[2], d[3] }; }
static gfx_mat nc_decodeAffine(id self, SEL s, id key) { (void)s; double d[6]; if (!decode_doubles(self, kstr(key), d, 6)) return gfx_mat_identity(); return (gfx_mat){ d[0], d[1], d[2], d[3], d[4], d[5] }; }
static const uint8_t *nc_decodeBytes(id self, SEL s, id key, unsigned long *len) {
    (void)s; const struct nval *v = find_val(self, kstr(key));
    if (!v || v->type != NV_DATA) { if (len) *len = 0; return NULL; }
    if (len) *len = v->u.data.n;
    return v->u.data.p;
}
/* "UINibEncoderEmptyKey" values in order: array elements, dictionary k,v pairs */
static id coder_elements(id coder) {
    struct nibcoder *c = NC(coder); struct nibfile *nf = c->nf;
    id arr = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array");
    const struct nobj *o = &nf->objs[c->obj];
    for (int i = 0; i < o->vcount; i++) {
        const struct nval *v = &nf->vals[o->vstart + i];
        if (strcmp(nf->keys[v->key], "UINibEncoderEmptyKey")) continue;
        id e = NULL;
        if (v->type == NV_OBJ) e = decode_object(nf, (int)v->u.obj);
        else if (v->type == NV_DATA) e = objc_autorelease(rt_data_new(v->u.data.p, v->u.data.n));
        else if (v->type == NV_F32 || v->type == NV_F64) e = RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithDouble:", double, v->u.d);
        else if (v->type != NV_NIL) e = RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithLongLong:", long long, v->u.i);
        RT_SEND1(void, arr, "addObject:", id, e ? e : RT_SEND0(id, (id)rt_class_named("NSNull"), "null"));
    }
    return arr;
}

/* ============================================================ object graph */
static int class_is(const char *cn, const char *a) { return !strcmp(cn, a); }
static id make_coder(struct nibfile *nf, int idx) {
    id c = rt_alloc_instance(RT_CLASS(UINibDecoder), 0);
    NC(c)->nf = nf; NC(c)->obj = idx;
    return objc_autorelease(c);
}
static id decode_value_object(struct nibfile *nf, int idx, const char *cn, id coder) {
    /* Foundation value classes */
    if (class_is(cn, "NSString") || class_is(cn, "NSMutableString") || class_is(cn, "NSLocalizableString")) {
        unsigned long n; const uint8_t *b = nc_decodeBytes(coder, NULL, rt_str("NS.bytes"), &n);
        id str;
        if (b) { char *t = strndup((const char *)b, n); str = rt_str_new(t); free(t); }
        else { id inner = nc_decodeObject(coder, NULL, rt_str("NS.string")); str = inner ? objc_retain(inner) : rt_str_new(""); }
        if (class_is(cn, "NSMutableString")) { id m = RT_SEND0(id, str, "mutableCopy"); objc_release(str); str = m; }
        return str;
    }
    if (class_is(cn, "NSArray") || class_is(cn, "NSMutableArray")) {
        id e = coder_elements(coder);
        return class_is(cn, "NSArray") ? RT_SEND0(id, e, "copy") : RT_SEND0(id, e, "mutableCopy");
    }
    if (class_is(cn, "NSSet") || class_is(cn, "NSMutableSet")) {
        id e = coder_elements(coder);
        return objc_retain(RT_SEND1(id, (id)rt_class_named(class_is(cn, "NSSet") ? "NSSet" : "NSMutableSet"), "setWithArray:", id, e));
    }
    if (class_is(cn, "NSOrderedSet") || class_is(cn, "NSMutableOrderedSet")) {
        id e = coder_elements(coder);
        return objc_retain(RT_SEND1(id, (id)rt_class_named("NSMutableOrderedSet"), "orderedSetWithArray:", id, e));
    }
    if (class_is(cn, "NSDictionary") || class_is(cn, "NSMutableDictionary")) {
        id e = coder_elements(coder);
        id d = RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableDictionary"), "alloc"), "init");
        for (size_t i = 0; i + 1 < rt_array_count(e); i += 2) RT_SEND2(void, d, "setObject:forKey:", id, rt_array_at(e, i + 1), id, rt_array_at(e, i));
        return d;
    }
    if (class_is(cn, "NSNumber")) {
        const struct nval *v;
        if ((v = find_val(coder, "NS.dblval")) || (v = find_val(coder, "NS.floatval"))) return objc_retain(RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithDouble:", double, dbl_val(v)));
        if ((v = find_val(coder, "NS.boolval"))) return objc_retain(RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithBool:", long, num_val(v)));
        v = find_val(coder, "NS.intval");
        return objc_retain(RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithLongLong:", long long, num_val(v)));
    }
    if (class_is(cn, "NSNull")) return objc_retain(RT_SEND0(id, (id)rt_class_named("NSNull"), "null"));
    if (class_is(cn, "NSData") || class_is(cn, "NSMutableData")) { unsigned long n; const uint8_t *b = nc_decodeBytes(coder, NULL, rt_str("NS.bytes"), &n); return rt_data_new(b, b ? n : 0); }
    if (class_is(cn, "UIProxyObject")) {
        id ident = nc_decodeObject(coder, NULL, rt_str("UIProxiedObjectIdentifier"));
        const char *n = ident ? rt_str_utf8(ident) : "";
        if (!strcmp(n, "IBFilesOwner")) return objc_retain(nf->owner);
        if (!strcmp(n, "IBFirstResponder")) return NULL;
        id ext = nf->external && ident ? rt_dict_get(nf->external, ident) : NULL;
        if (!ext) rt_warn("NIB: external object '%s' was not provided", n);
        return objc_retain(ext);
    }
    if (class_is(cn, "UIImageNibPlaceholder")) {
        id name = nc_decodeObject(coder, NULL, rt_str("UIResourceName"));
        return objc_retain(name ? RT_SEND1(id, (id)RT_CLASS(UIImage), "imageNamed:", id, name) : NULL);
    }
    (void)idx;
    return (id)-1;   /* not a value class */
}
static id decode_object(struct nibfile *nf, int idx) {
    if (idx < 0 || idx >= nf->nobjs) return NULL;
    if (nf->state[idx] == 2 || nf->state[idx] == 1) return nf->decoded[idx];   /* in-progress: the allocated instance */
    nf->state[idx] = 1;
    const char *cn = nf->classes[nf->objs[idx].cls];
    id coder = make_coder(nf, idx);
    id r = decode_value_object(nf, idx, cn, coder);
    if (r == (id)-1) {
        Class cls = NULL;
        if (class_is(cn, "UIClassSwapper")) {
            id name = nc_decodeObject(coder, NULL, rt_str("UIClassName")), orig = nc_decodeObject(coder, NULL, rt_str("UIOriginalClassName"));
            cls = name ? rt_class_named(rt_str_utf8(name)) : NULL;
            if (!cls) { rt_warn("NIB: custom class %s not found, using %s", name ? rt_str_utf8(name) : "?", orig ? rt_str_utf8(orig) : "?"); cls = orig ? rt_class_named(rt_str_utf8(orig)) : NULL; }
        } else cls = rt_class_named(cn);
        if (!cls) { if (rt_first_time(cn)) rt_warn("NIB: class %s is not available; decoding as NSObject", cn); cls = RT_CLASS(NSObject); }
        id obj = RT_SEND0(id, (id)cls, "alloc");
        nf->decoded[idx] = obj;   /* visible to cycles while initializing */
        id inited = RT_SEND1(id, obj, "initWithCoder:", id, coder);
        if (!rt_responds(obj, rt_sel("initWithCoder:"))) inited = obj;
        r = inited;
    }
    nf->decoded[idx] = r; nf->state[idx] = 2;
    if (r) { RT_SEND1(void, nf->all, "addObject:", id, r); objc_release(r); }
    return r;
}

/* ======================================================== connections */
struct conn { Class isa; id label, source, dest; long mask; };
#define CN(x) ((struct conn *)(x))
static id conn_initWithCoder(id self, SEL s, id coder) {
    (void)s; struct conn *c = CN(self);
    c->label = objc_retain(nc_decodeObject(coder, NULL, rt_str("UILabel")));
    c->source = nc_decodeObject(coder, NULL, rt_str("UISource"));
    c->dest = nc_decodeObject(coder, NULL, rt_str("UIDestination"));
    if (!c->dest) c->dest = nc_decodeObject(coder, NULL, rt_str("UIDestinations"));
    c->mask = nc_decodeInteger(coder, NULL, rt_str("UIEventMask"));
    objc_retain(c->source); objc_retain(c->dest);
    return self;
}
static void conn_dealloc(id self, SEL s) { (void)s; objc_release(CN(self)->label); objc_release(CN(self)->source); objc_release(CN(self)->dest); rt_dealloc_instance(self); }
static void conn_connect(id self, SEL s) {
    (void)s; struct conn *c = CN(self);
    if (!c->source || !c->label) return;
    if (rt_is_kind_of(self, RT_CLASS(UIRuntimeEventConnection))) {
        SEL action = rt_sel(rt_str_utf8(c->label));
        if (rt_responds(c->source, rt_sel("addTarget:action:forControlEvents:")))
            RT_SEND3(void, c->source, "addTarget:action:forControlEvents:", id, c->dest, SEL, action, unsigned long, (unsigned long)c->mask);
        else if (rt_responds(c->source, rt_sel("setAction:"))) { RT_SEND1(void, c->source, "setTarget:", id, c->dest); RT_SEND1(void, c->source, "setAction:", SEL, action); }
        else if (rt_responds(c->source, rt_sel("addTarget:action:"))) RT_SEND2(void, c->source, "addTarget:action:", id, c->dest, SEL, action);
        return;
    }
    RT_SEND2(void, c->source, "setValue:forKey:", id, c->dest, id, c->label);
}

/* ============================================================ instantiate */
static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = n > 0 ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f); *len = b ? (size_t)n : 0; return b;
}
static id instantiate(struct nibfile *nf, id owner, id external) {
    nf->owner = owner; nf->external = external;
    id root_coder = make_coder(nf, 0);
    id top = nc_decodeObject(root_coder, NULL, rt_str("UINibTopLevelObjectsKey"));
    nc_decodeObject(root_coder, NULL, rt_str("UINibObjectsKey"));
    id conns = nc_decodeObject(root_coder, NULL, rt_str("UINibConnectionsKey"));
    for (size_t i = 0; conns && i < rt_array_count(conns); i++) {
        id c = rt_array_at(conns, i);
        if (rt_is_kind_of(c, RT_CLASS(UIRuntimeConnection))) conn_connect(c, NULL);
    }
    /* user defined runtime attributes */
    id kvs = nc_decodeObject(root_coder, NULL, rt_str("UINibKeyValuePairsKey"));
    for (size_t i = 0; kvs && i < rt_array_count(kvs); i++) {
        id pair = rt_array_at(kvs, i);
        if (!rt_responds(pair, rt_sel("valueForKey:"))) continue;
        id obj = RT_SEND1(id, pair, "valueForKey:", id, rt_str("object")), kp = RT_SEND1(id, pair, "valueForKey:", id, rt_str("keyPath")), val = RT_SEND1(id, pair, "valueForKey:", id, rt_str("value"));
        if (obj && kp) RT_SEND2(void, obj, "setValue:forKeyPath:", id, val, id, kp);
    }
    /* awakeFromNib on everything that came out of the archive (not the owner) */
    id all = nf->all;
    for (size_t i = 0; i < rt_array_count(all); i++) {
        id o = rt_array_at(all, i);
        if (o == owner || rt_is_kind_of(o, RT_CLASS(UIRuntimeConnection))) continue;
        if (rt_responds(o, rt_sel("awakeFromNib"))) RT_SEND0(void, o, "awakeFromNib");
    }
    id wins = nc_decodeObject(root_coder, NULL, rt_str("UINibVisibleWindowsKey"));
    for (size_t i = 0; wins && i < rt_array_count(wins); i++) RT_SEND0(void, rt_array_at(wins, i), "makeKeyAndVisible");
    id result = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array");
    for (size_t i = 0; top && i < rt_array_count(top); i++) {
        id o = rt_array_at(top, i);
        if (o != owner && o != RT_SEND0(id, (id)rt_class_named("NSNull"), "null")) RT_SEND1(void, result, "addObject:", id, o);
    }
    return result;
}
id rt_nib_instantiate_data(uint8_t *buf, size_t len, id owner, id external, const char *what) {
    if (len >= 6 && !memcmp(buf, "bplist", 6)) { rt_warn("NIB %s is an NSKeyedArchiver (old) nib; only NIBArchive nibs are supported", what); free(buf); return NULL; }
    struct nibfile *nf = nib_parse(buf, len);
    if (!nf) { free(buf); return NULL; }
    void *pool = objc_autoreleasePoolPush();
    id r = objc_retain(instantiate(nf, owner, external));
    objc_autoreleasePoolPop(pool);
    nib_free(nf);
    return objc_autorelease(r);
}
id rt_nib_instantiate(const char *path, id owner, id external) {
    char p[4096]; snprintf(p, sizeof p, "%s", path);
    struct stat st;
    if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
        /* compiled nib bundle: runtime.nib, else objects-*.nib */
        char q[4200]; snprintf(q, sizeof q, "%s/runtime.nib", path);
        if (stat(q, &st) == 0) snprintf(p, sizeof p, "%s", q);
        else {
            DIR *d = opendir(path); struct dirent *e; int found = 0;
            while (d && (e = readdir(d))) if (!strncmp(e->d_name, "objects", 7) && strstr(e->d_name, ".nib")) { snprintf(p, sizeof p, "%s/%s", path, e->d_name); found = 1; break; }
            if (d) closedir(d);
            if (!found) { snprintf(q, sizeof q, "%s/keyedobjects.nib", path); snprintf(p, sizeof p, "%s", q); }
        }
    }
    size_t len; uint8_t *buf = read_file(p, &len);
    if (!buf) { rt_warn("NIB %s could not be read", p); return NULL; }
    rt_log("loading NIB %s", strrchr(p, '/') ? strrchr(p, '/') + 1 : p);
    return rt_nib_instantiate_data(buf, len, owner, external, p);
}
const char *rt_nib_find(const char *name, id bundle) {
    static char out[4096];
    if (!name || !*name) return NULL;
    if (!bundle) bundle = RT_SEND0(id, (id)RT_CLASS(NSBundle), "mainBundle");
    const char *dir = rt_str_utf8(RT_SEND0(id, bundle, "bundlePath"));
    char base[1024]; snprintf(base, sizeof base, "%s", name);
    size_t bl = strlen(base); if (bl > 4 && !strcmp(base + bl - 4, ".nib")) base[bl - 4] = 0;
    const char *subs[] = { "", "Base.lproj/", "en.lproj/", NULL };
    const char *idioms[] = { "~iphone", "", NULL };
    struct stat st;
    for (int i = 0; subs[i]; i++) for (int j = 0; idioms[j]; j++) {
        snprintf(out, sizeof out, "%s/%s%s%s.nib", dir, subs[i], base, idioms[j]);
        if (stat(out, &st) == 0) return out;
    }
    return NULL;
}

/* ================================================================ UINib */
struct uinib { Class isa; id path; id data; };
static id nib_withName(Class c, SEL s, id name, id bundle) {
    (void)s; const char *p = name ? rt_nib_find(rt_str_utf8(name), bundle) : NULL;
    if (!p) { rt_warn("UINib: nib '%s' not found", name ? rt_str_utf8(name) : "?"); return NULL; }
    id n = rt_alloc_instance(c, 0); ((struct uinib *)n)->path = rt_str_new(p);
    return objc_autorelease(n);
}
static id nib_withData(Class c, SEL s, id data, id bundle) { (void)s; (void)bundle; id n = rt_alloc_instance(c, 0); ((struct uinib *)n)->data = objc_retain(data); return objc_autorelease(n); }
static id nib_instantiate(id self, SEL s, id owner, id opts) {
    (void)s; struct uinib *n = (struct uinib *)self;
    id ext = opts ? rt_dict_get(opts, iosrt_UINibExternalObjects) : NULL;
    if (n->path) return rt_nib_instantiate(rt_str_utf8(n->path), owner, ext);
    if (n->data) { size_t len; const void *b = rt_data_bytes(n->data, &len); uint8_t *copy = malloc(len ? len : 1); memcpy(copy, b, len); return rt_nib_instantiate_data(copy, len, owner, ext, "(data)"); }
    return NULL;
}
static void nib_dealloc(id self, SEL s) { (void)s; objc_release(((struct uinib *)self)->path); objc_release(((struct uinib *)self)->data); rt_dealloc_instance(self); }
static id bundle_loadNib(id self, SEL s, id name, id owner, id opts) {
    (void)s; const char *p = name ? rt_nib_find(rt_str_utf8(name), self) : NULL;
    if (!p) { rt_warn("loadNibNamed: '%s' not found", name ? rt_str_utf8(name) : "?"); return NULL; }
    return rt_nib_instantiate(p, owner, opts ? rt_dict_get(opts, iosrt_UINibExternalObjects) : NULL);
}

/* ===================================================== initWithCoder: impls */
static id dec(id c, const char *k) { return RT_SEND1(id, c, "decodeObjectForKey:", id, rt_str(k)); }
static int has(id c, const char *k) { return RT_SEND1(long, c, "containsValueForKey:", id, rt_str(k)) & 0xff; }
static long decI(id c, const char *k) { return RT_SEND1(long, c, "decodeIntegerForKey:", id, rt_str(k)); }
static double decD(id c, const char *k) { return RT_SEND1(double, c, "decodeDoubleForKey:", id, rt_str(k)); }
static int decB(id c, const char *k) { return RT_SEND1(long, c, "decodeBoolForKey:", id, rt_str(k)) & 0xff; }
static id super_initWithCoder(id self, Class cls, id coder) { struct { id r; Class c; } sup = { self, cls }; return ((id (*)(void *, SEL, id))objc_msgSendSuper2)(&sup, rt_sel("initWithCoder:"), coder); }
static id nsobject_initWithCoder(id self, SEL s, id coder) { (void)s; (void)coder; return self; }
static id view_initWithCoder(id self, SEL s, id coder) {
    (void)s;
    self = ((id (*)(id, SEL, CGRect))objc_msgSend)(self, rt_sel("initWithFrame:"), (CGRect){ { 0, 0 }, { 0, 0 } });
    if (!self) return NULL;
    struct uiview *v = (struct uiview *)self;
    CGRect b = has(coder, "UIBounds") ? RT_SEND1(CGRect, coder, "decodeCGRectForKey:", id, rt_str("UIBounds")) : (CGRect){ { 0, 0 }, { 0, 0 } };
    CGPoint c = RT_SEND1(CGPoint, coder, "decodeCGPointForKey:", id, rt_str("UICenter"));
    RT_SEND1(void, v->layer, "setBounds:", CGRect, b);
    RT_SEND1(void, v->layer, "setPosition:", CGPoint, c);
    if (has(coder, "UIBackgroundColor")) RT_SEND1(void, self, "setBackgroundColor:", id, dec(coder, "UIBackgroundColor"));
    if (has(coder, "UIAlpha")) RT_SEND1(void, self, "setAlpha:", double, decD(coder, "UIAlpha"));
    if (decB(coder, "UIHidden")) RT_SEND1(void, self, "setHidden:", long, 1);
    if (decB(coder, "UIClipsToBounds")) RT_SEND1(void, self, "setClipsToBounds:", long, 1);
    v->autoresizing = decI(coder, "UIAutoresizingMask");
    v->autoresizesSubviews = has(coder, "UIAutoresizeSubviews") ? decB(coder, "UIAutoresizeSubviews") : 1;
    v->contentMode = decI(coder, "UIContentMode");
    v->tag = decI(coder, "UITag");
    if (decB(coder, "UIUserInteractionDisabled")) v->interaction = 0;
    v->multitouch = decB(coder, "UIMultipleTouchEnabled");
    if (has(coder, "UITintColor")) RT_SEND1(void, self, "setTintColor:", id, dec(coder, "UITintColor"));
    id subs = dec(coder, "UISubviews");
    for (size_t i = 0; subs && i < rt_array_count(subs); i++) { id sv = rt_array_at(subs, i); if (sv && rt_is_kind_of(sv, RT_CLASS(UIView))) rt_view_add_subview(self, sv); }
    id gestures = dec(coder, "gestureRecognizers");
    for (size_t i = 0; gestures && i < rt_array_count(gestures); i++) RT_SEND1(void, self, "addGestureRecognizer:", id, rt_array_at(gestures, i));
    if (has(coder, "UIAccessibilityLabel")) RT_SEND1(void, self, "setAccessibilityLabel:", id, dec(coder, "UIAccessibilityLabel"));
    return self;
}
static void apply_label(id self, id coder) {
    if (has(coder, "UIAttributedText")) RT_SEND1(void, self, "setAttributedText:", id, dec(coder, "UIAttributedText"));
    if (has(coder, "UIText")) RT_SEND1(void, self, "setText:", id, dec(coder, "UIText"));
    if (has(coder, "UITextColor")) RT_SEND1(void, self, "setTextColor:", id, dec(coder, "UITextColor"));
    if (has(coder, "UIHighlightedColor")) RT_SEND1(void, self, "setHighlightedTextColor:", id, dec(coder, "UIHighlightedColor"));
    if (has(coder, "UIFont")) RT_SEND1(void, self, "setFont:", id, dec(coder, "UIFont"));
    if (has(coder, "UITextAlignment")) RT_SEND1(void, self, "setTextAlignment:", long, decI(coder, "UITextAlignment"));
    if (has(coder, "UINumberOfLines")) RT_SEND1(void, self, "setNumberOfLines:", long, decI(coder, "UINumberOfLines"));
    if (has(coder, "UILineBreakMode")) RT_SEND1(void, self, "setLineBreakMode:", long, decI(coder, "UILineBreakMode"));
    if (has(coder, "UIShadowColor")) RT_SEND1(void, self, "setShadowColor:", id, dec(coder, "UIShadowColor"));
    if (has(coder, "UIShadowOffset")) RT_SEND1(void, self, "setShadowOffset:", CGSize, RT_SEND1(CGSize, coder, "decodeCGSizeForKey:", id, rt_str("UIShadowOffset")));
    if (decB(coder, "UIAdjustsFontSizeToFit")) RT_SEND1(void, self, "setAdjustsFontSizeToFitWidth:", long, 1);
    if (has(coder, "UIMinimumScaleFactor")) RT_SEND1(void, self, "setMinimumScaleFactor:", double, decD(coder, "UIMinimumScaleFactor"));
    if (has(coder, "UIMinimumFontSize")) RT_SEND1(void, self, "setMinimumFontSize:", double, decD(coder, "UIMinimumFontSize"));
    if (decB(coder, "UIDisabled")) RT_SEND1(void, self, "setEnabled:", long, 0);
    if (decB(coder, "UIHighlighted")) RT_SEND1(void, self, "setHighlighted:", long, 1);
    if (has(coder, "UIPreferredMaxLayoutWidth")) RT_SEND1(void, self, "setPreferredMaxLayoutWidth:", double, decD(coder, "UIPreferredMaxLayoutWidth"));
}
static id label_initWithCoder(id self, SEL s, id coder) { (void)s; self = super_initWithCoder(self, RT_CLASS(UILabel), coder); if (self) { ((struct uiview *)self)->interaction = !has(coder, "UIUserInteractionDisabled") ? 0 : !decB(coder, "UIUserInteractionDisabled"); apply_label(self, coder); } return self; }
static id imageview_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = super_initWithCoder(self, RT_CLASS(UIImageView), coder);
    if (!self) return NULL;
    if (has(coder, "UIImage")) RT_SEND1(void, self, "setImage:", id, dec(coder, "UIImage"));
    if (has(coder, "UIHighlightedImage")) RT_SEND1(void, self, "setHighlightedImage:", id, dec(coder, "UIHighlightedImage"));
    if (decB(coder, "UIHighlighted")) RT_SEND1(void, self, "setHighlighted:", long, 1);
    ((struct uiview *)self)->interaction = has(coder, "UIUserInteractionDisabled") ? !decB(coder, "UIUserInteractionDisabled") : 0;
    return self;
}
static id control_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = super_initWithCoder(self, RT_CLASS(UIControl), coder);
    if (!self) return NULL;
    struct uicontrol *c = (struct uicontrol *)self;
    if (!c->targets) c->targets = RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "alloc"), "init");
    c->enabled = !decB(coder, "UIDisabled");
    c->selected = decB(coder, "UISelected");
    c->highlighted = decB(coder, "UIHighlighted");
    c->hAlign = decI(coder, "UIContentHorizontalAlignment");
    c->vAlign = decI(coder, "UIContentVerticalAlignment");
    return self;
}
static id button_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = super_initWithCoder(self, RT_CLASS(UIButton), coder);
    if (!self) return NULL;
    struct uibutton *b = (struct uibutton *)self;
    b->type = decI(coder, "UIButtonType");
    if (has(coder, "UIFont")) RT_SEND1(void, self, "setFont:", id, dec(coder, "UIFont"));
    id content = dec(coder, "UIButtonStatefulContent");
    id keys = content ? RT_SEND0(id, content, "allKeys") : NULL;
    for (size_t i = 0; keys && i < rt_array_count(keys); i++) { id k = rt_array_at(keys, i); rt_button_apply_content(self, (unsigned long)RT_SEND0(long, k, "integerValue"), rt_dict_get(content, k)); }
    if (has(coder, "UIContentEdgeInsets")) b->contentInsets = RT_SEND1(UIEdgeInsets, coder, "decodeUIEdgeInsetsForKey:", id, rt_str("UIContentEdgeInsets"));
    if (has(coder, "UITitleEdgeInsets")) b->titleInsets = RT_SEND1(UIEdgeInsets, coder, "decodeUIEdgeInsetsForKey:", id, rt_str("UITitleEdgeInsets"));
    if (has(coder, "UIImageEdgeInsets")) b->imageInsets = RT_SEND1(UIEdgeInsets, coder, "decodeUIEdgeInsetsForKey:", id, rt_str("UIImageEdgeInsets"));
    b->adjustsHighlighted = has(coder, "UIAdjustsImageWhenHighlighted") ? decB(coder, "UIAdjustsImageWhenHighlighted") : 1;
    b->adjustsDisabled = has(coder, "UIAdjustsImageWhenDisabled") ? decB(coder, "UIAdjustsImageWhenDisabled") : 1;
    b->showsTouch = decB(coder, "UIShowsTouchWhenHighlighted");
    if (has(coder, "UITitleLabelLines") || has(coder, "UILineBreakMode")) { id tl = RT_SEND0(id, self, "titleLabel"); if (has(coder, "UILineBreakMode")) RT_SEND1(void, tl, "setLineBreakMode:", long, decI(coder, "UILineBreakMode")); }
    return self;
}
static id bcontent_initWithCoder(id self, SEL s, id coder) {
    (void)s; id c = rt_button_content_new(dec(coder, "UITitle"), dec(coder, "UIAttributedTitle"), dec(coder, "UITitleColor"), dec(coder, "UIShadowColor"), dec(coder, "UIImage"), dec(coder, "UIBackgroundImage"));
    rt_dealloc_instance(self);
    return c;
}
static id scroll_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = super_initWithCoder(self, RT_CLASS(UIScrollView), coder);
    if (!self) return NULL;
    struct uiscroll *v = (struct uiscroll *)self;
    if (has(coder, "UIContentSize")) v->contentSize = RT_SEND1(CGSize, coder, "decodeCGSizeForKey:", id, rt_str("UIContentSize"));
    v->paging = decB(coder, "UIPagingEnabled");
    v->scrollEnabled = !decB(coder, "UIScrollDisabled");
    if (has(coder, "UIBounceDisabled")) v->bounces = !decB(coder, "UIBounceDisabled");
    v->alwaysBounceV = decB(coder, "UIAlwaysBounceVertical"); v->alwaysBounceH = decB(coder, "UIAlwaysBounceHorizontal");
    if (has(coder, "UIShowsHorizontalScrollIndicator")) v->showsH = decB(coder, "UIShowsHorizontalScrollIndicator");
    if (has(coder, "UIShowsVerticalScrollIndicator")) v->showsV = decB(coder, "UIShowsVerticalScrollIndicator");
    if (has(coder, "UIDelaysContentTouches")) v->delaysContent = decB(coder, "UIDelaysContentTouches");
    if (has(coder, "UICanCancelContentTouches")) v->cancelsContent = decB(coder, "UICanCancelContentTouches");
    if (has(coder, "UIMinimumZoomScale")) v->minZoom = decD(coder, "UIMinimumZoomScale");
    if (has(coder, "UIMaximumZoomScale")) v->maxZoom = decD(coder, "UIMaximumZoomScale");
    RT_SEND1(void, self, "setClipsToBounds:", long, has(coder, "UIClipsToBounds") ? decB(coder, "UIClipsToBounds") : 1);
    return self;
}
static id pagecontrol_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = super_initWithCoder(self, RT_CLASS(UIPageControl), coder);
    if (!self) return NULL;
    RT_SEND1(void, self, "setNumberOfPages:", long, decI(coder, "UINumberOfPages"));
    RT_SEND1(void, self, "setCurrentPage:", long, decI(coder, "UICurrentPage"));
    RT_SEND1(void, self, "setHidesForSinglePage:", long, decB(coder, "UIHidesForSinglePage"));
    if (has(coder, "UIPageIndicatorTintColor")) RT_SEND1(void, self, "setPageIndicatorTintColor:", id, dec(coder, "UIPageIndicatorTintColor"));
    if (has(coder, "UICurrentPageIndicatorTintColor")) RT_SEND1(void, self, "setCurrentPageIndicatorTintColor:", id, dec(coder, "UICurrentPageIndicatorTintColor"));
    return self;
}
static id slider_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = super_initWithCoder(self, RT_CLASS(UISlider), coder);
    if (!self) return NULL;
    RT_SEND1(void, self, "setMaximumValue:", float, has(coder, "UIMaxValue") ? (float)decD(coder, "UIMaxValue") : 1.f);
    RT_SEND1(void, self, "setMinimumValue:", float, (float)decD(coder, "UIMinValue"));
    RT_SEND1(void, self, "setValue:", float, (float)decD(coder, "UIValue"));
    RT_SEND1(void, self, "setContinuous:", long, has(coder, "UIContinuous") ? decB(coder, "UIContinuous") : 1);
    if (has(coder, "UIMinimumTrackTintColor")) RT_SEND1(void, self, "setMinimumTrackTintColor:", id, dec(coder, "UIMinimumTrackTintColor"));
    if (has(coder, "UIMaximumTrackTintColor")) RT_SEND1(void, self, "setMaximumTrackTintColor:", id, dec(coder, "UIMaximumTrackTintColor"));
    if (has(coder, "UIThumbTintColor")) RT_SEND1(void, self, "setThumbTintColor:", id, dec(coder, "UIThumbTintColor"));
    return self;
}
static id switch_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = super_initWithCoder(self, RT_CLASS(UISwitch), coder);
    if (!self) return NULL;
    RT_SEND1(void, self, "setOn:", long, decB(coder, "UISwitchOn"));
    if (has(coder, "UIOnTintColor")) RT_SEND1(void, self, "setOnTintColor:", id, dec(coder, "UIOnTintColor"));
    return self;
}
static id segmented_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = super_initWithCoder(self, RT_CLASS(UISegmentedControl), coder);
    if (!self) return NULL;
    id segs = dec(coder, "UISegments");
    RT_SEND0(void, self, "removeAllSegments");
    for (size_t i = 0; segs && i < rt_array_count(segs); i++) {
        id seg = rt_array_at(segs, i), info = rt_responds(seg, rt_sel("valueForKey:")) ? RT_SEND1(id, seg, "valueForKey:", id, rt_str("info")) : NULL;
        RT_SEND3(void, self, "insertSegmentWithTitle:atIndex:animated:", id, info ? info : rt_str(""), unsigned long, i, long, 0);
    }
    RT_SEND1(void, self, "setSelectedSegmentIndex:", long, has(coder, "UISelectedSegmentIndex") ? decI(coder, "UISelectedSegmentIndex") : -1);
    return self;
}
/* UISegment: just remember its title/image for the segmented control above */
struct uiseg { Class isa; id info; };
static id segment_initWithCoder(id self, SEL s, id coder) { (void)s; ((struct uiseg *)self)->info = objc_retain(dec(coder, "UISegmentInfo")); return self; }
static id segment_valueForKey(id self, SEL s, id k) { (void)s; (void)k; return ((struct uiseg *)self)->info; }
static id activity_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = super_initWithCoder(self, RT_CLASS(UIActivityIndicatorView), coder);
    if (!self) return NULL;
    RT_SEND1(void, self, "setActivityIndicatorViewStyle:", long, decI(coder, "UIActivityIndicatorViewStyle"));
    RT_SEND1(void, self, "setHidesWhenStopped:", long, has(coder, "UIHidesWhenStopped") ? decB(coder, "UIHidesWhenStopped") : 1);
    if (has(coder, "UIColor")) RT_SEND1(void, self, "setColor:", id, dec(coder, "UIColor"));
    if (decB(coder, "UIAnimating")) RT_SEND0(void, self, "startAnimating");
    return self;
}
static id progress_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = super_initWithCoder(self, RT_CLASS(UIProgressView), coder);
    if (!self) return NULL;
    RT_SEND1(void, self, "setProgress:", float, (float)decD(coder, "UIProgress"));
    if (has(coder, "UIProgressTintColor")) RT_SEND1(void, self, "setProgressTintColor:", id, dec(coder, "UIProgressTintColor"));
    if (has(coder, "UITrackTintColor")) RT_SEND1(void, self, "setTrackTintColor:", id, dec(coder, "UITrackTintColor"));
    return self;
}
static id textfield_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = super_initWithCoder(self, RT_CLASS(UITextField), coder);
    if (!self) return NULL;
    if (has(coder, "UIText")) RT_SEND1(void, self, "setText:", id, dec(coder, "UIText"));
    if (has(coder, "UIPlaceholder")) RT_SEND1(void, self, "setPlaceholder:", id, dec(coder, "UIPlaceholder"));
    if (has(coder, "UIFont")) RT_SEND1(void, self, "setFont:", id, dec(coder, "UIFont"));
    if (has(coder, "UITextColor")) RT_SEND1(void, self, "setTextColor:", id, dec(coder, "UITextColor"));
    if (has(coder, "UITextAlignment")) RT_SEND1(void, self, "setTextAlignment:", long, decI(coder, "UITextAlignment"));
    if (has(coder, "UIBorderStyle")) RT_SEND1(void, self, "setBorderStyle:", long, decI(coder, "UIBorderStyle"));
    return self;
}
static id textview_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = super_initWithCoder(self, RT_CLASS(UITextView), coder);
    if (!self) return NULL;
    if (has(coder, "UIText")) RT_SEND1(void, self, "setText:", id, dec(coder, "UIText"));
    if (has(coder, "UIAttributedText")) RT_SEND1(void, self, "setAttributedText:", id, dec(coder, "UIAttributedText"));
    if (has(coder, "UIFont")) RT_SEND1(void, self, "setFont:", id, dec(coder, "UIFont"));
    if (has(coder, "UITextColor")) RT_SEND1(void, self, "setTextColor:", id, dec(coder, "UITextColor"));
    if (has(coder, "UIEditable")) RT_SEND1(void, self, "setEditable:", long, decB(coder, "UIEditable"));
    return self;
}
static id table_initWithCoder(id self, SEL s, id coder) {
    (void)s;
    self = RT_SEND2(id, self, "initWithFrame:style:", CGRect, ((CGRect){ { 0, 0 }, { 0, 0 } }), long, decI(coder, "UIStyle"));
    if (!self) return NULL;
    /* reuse UIView decoding for geometry and subviews by calling the UIScrollView/UIView coder logic on top */
    struct uiview *v = (struct uiview *)self;
    RT_SEND1(void, v->layer, "setBounds:", CGRect, RT_SEND1(CGRect, coder, "decodeCGRectForKey:", id, rt_str("UIBounds")));
    RT_SEND1(void, v->layer, "setPosition:", CGPoint, RT_SEND1(CGPoint, coder, "decodeCGPointForKey:", id, rt_str("UICenter")));
    v->autoresizing = decI(coder, "UIAutoresizingMask");
    if (has(coder, "UIBackgroundColor")) RT_SEND1(void, self, "setBackgroundColor:", id, dec(coder, "UIBackgroundColor"));
    if (has(coder, "UIRowHeight")) RT_SEND1(void, self, "setRowHeight:", double, decD(coder, "UIRowHeight"));
    if (has(coder, "UISeparatorStyle")) RT_SEND1(void, self, "setSeparatorStyle:", long, decI(coder, "UISeparatorStyle"));
    if (decB(coder, "UIHidden")) RT_SEND1(void, self, "setHidden:", long, 1);
    return self;
}
static id toolbar_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = super_initWithCoder(self, (Class)RT_SEND0(id, self, "class") == RT_CLASS(UINavigationBar) ? RT_CLASS(UINavigationBar) : RT_CLASS(UIToolbar), coder);
    if (!self) return NULL;
    id items = dec(coder, "UIItems");
    if (items) RT_SEND1(void, self, "setItems:", id, items);
    if (has(coder, "UIBarStyle")) RT_SEND1(void, self, "setBarStyle:", long, decI(coder, "UIBarStyle"));
    if (has(coder, "UIBarTintColor")) RT_SEND1(void, self, "setBarTintColor:", id, dec(coder, "UIBarTintColor"));
    return self;
}
static id barbutton_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = RT_SEND0(id, self, "init");
    if (!self) return NULL;
    if (has(coder, "UICustomView")) return RT_SEND1(id, self, "initWithCustomView:", id, dec(coder, "UICustomView"));
    if (has(coder, "UISystemItemIdentifier") || has(coder, "UIIsSystemItem")) self = RT_SEND3(id, self, "initWithBarButtonSystemItem:target:action:", long, decI(coder, "UISystemItemIdentifier"), id, NULL, SEL, NULL);
    if (has(coder, "UITitle")) RT_SEND1(void, self, "setTitle:", id, dec(coder, "UITitle"));
    if (has(coder, "UIImage")) RT_SEND1(void, self, "setImage:", id, dec(coder, "UIImage"));
    if (has(coder, "UIStyle")) RT_SEND1(void, self, "setStyle:", long, decI(coder, "UIStyle"));
    if (has(coder, "UIWidth")) RT_SEND1(void, self, "setWidth:", double, decD(coder, "UIWidth"));
    if (has(coder, "UIEnabled")) RT_SEND1(void, self, "setEnabled:", long, decB(coder, "UIEnabled"));
    if (has(coder, "UITag")) RT_SEND1(void, self, "setTag:", long, decI(coder, "UITag"));
    return self;
}
static id navitem_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = RT_SEND0(id, self, "init");
    if (has(coder, "UITitle")) RT_SEND1(void, self, "setTitle:", id, dec(coder, "UITitle"));
    if (has(coder, "UILeftBarButtonItem")) RT_SEND1(void, self, "setLeftBarButtonItem:", id, dec(coder, "UILeftBarButtonItem"));
    if (has(coder, "UIRightBarButtonItem")) RT_SEND1(void, self, "setRightBarButtonItem:", id, dec(coder, "UIRightBarButtonItem"));
    if (has(coder, "UITitleView")) RT_SEND1(void, self, "setTitleView:", id, dec(coder, "UITitleView"));
    return self;
}
static id gesture_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = RT_SEND0(id, self, "init");
    if (!self) return NULL;
    id targets = dec(coder, "UIGestureRecognizer.targets");
    for (size_t i = 0; targets && i < rt_array_count(targets); i++) { (void)rt_array_at(targets, i); }
    if (has(coder, "UIGestureRecognizer.cancelsTouchesInView")) RT_SEND1(void, self, "setCancelsTouchesInView:", long, decB(coder, "UIGestureRecognizer.cancelsTouchesInView"));
    if (has(coder, "UIGestureRecognizer.enabled")) RT_SEND1(void, self, "setEnabled:", long, decB(coder, "UIGestureRecognizer.enabled"));
    if (has(coder, "UITapGestureRecognizer.numberOfTapsRequired") && rt_responds(self, rt_sel("setNumberOfTapsRequired:"))) RT_SEND1(void, self, "setNumberOfTapsRequired:", unsigned long, (unsigned long)decI(coder, "UITapGestureRecognizer.numberOfTapsRequired"));
    if (has(coder, "UILongPressGestureRecognizer.minimumPressDuration")) RT_SEND1(void, self, "setMinimumPressDuration:", double, decD(coder, "UILongPressGestureRecognizer.minimumPressDuration"));
    return self;
}
/* graphics value classes */
static const char *system_color_names[][2] = {
    { "clearColor", "0 0 0 0" }, { "blackColor", "0 0 0 1" }, { "whiteColor", "1 1 1 1" }, { "darkTextColor", "0 0 0 1" }, { "lightTextColor", "1 1 1 0.6" },
    { "groupTableViewBackgroundColor", "0.937 0.937 0.957 1" }, { "scrollViewTexturedBackgroundColor", "0.43 0.44 0.47 1" }, { "viewFlipsideBackgroundColor", "0.12 0.12 0.13 1" },
    { "underPageBackgroundColor", "0.71 0.72 0.75 1" }, { "tableCellGroupedBackgroundColor", "1 1 1 1" }, { "labelColor", "0 0 0 1" },
    { "systemBlueColor", "0 0.478 1 1" }, { "systemRedColor", "1 0.231 0.188 1" }, { "systemGreenColor", "0.204 0.78 0.349 1" }, { "systemBackgroundColor", "1 1 1 1" },
    { "secondaryLabelColor", "0.235 0.235 0.263 0.6" }, { "tableBackgroundColor", "1 1 1 1" }, { "tableSeparatorColor", "0.78 0.78 0.8 1" },
    { NULL, NULL } };
static id color_initWithCoder(id self, SEL s, id coder) {
    (void)s; struct uicolor *u = (struct uicolor *)self;
    if (has(coder, "UISystemColorName")) {
        const char *n = rt_str_utf8(dec(coder, "UISystemColorName"));
        for (int i = 0; system_color_names[i][0]; i++) if (!strcmp(n, system_color_names[i][0])) { sscanf(system_color_names[i][1], "%lf %lf %lf %lf", &u->r, &u->g, &u->b, &u->a); return self; }
    }
    if (has(coder, "UIPatternSelector") || has(coder, "UIPatternImage") || has(coder, "UIImage")) {
        id img = dec(coder, "UIImage"); if (!img) img = dec(coder, "UIPatternImage");
        if (img) { u->pattern = objc_retain(img); u->a = 1; return self; }
    }
    if (has(coder, "UIWhite")) { u->r = u->g = u->b = decD(coder, "UIWhite"); u->a = has(coder, "UIAlpha") ? decD(coder, "UIAlpha") : 1; return self; }
    if (has(coder, "UIRed") || has(coder, "UIGreen") || has(coder, "UIBlue")) {
        u->r = decD(coder, "UIRed"); u->g = decD(coder, "UIGreen"); u->b = decD(coder, "UIBlue"); u->a = has(coder, "UIAlpha") ? decD(coder, "UIAlpha") : 1;
        return self;
    }
    /* NSColor-style fallback */
    id rgb = dec(coder, "NSRGB");
    if (rgb) { size_t n; const char *b = rt_data_bytes(rgb, &n); u->a = 1; sscanf(b, "%lf %lf %lf %lf", &u->r, &u->g, &u->b, &u->a); }
    id w = dec(coder, "NSWhite");
    if (w) { size_t n; const char *b = rt_data_bytes(w, &n); u->a = 1; sscanf(b, "%lf %lf", &u->r, &u->a); u->g = u->b = u->r; }
    return self;
}
static id font_initWithCoder(id self, SEL s, id coder) {
    (void)s; id name = dec(coder, "UIFontName");
    double size = has(coder, "UIFontPointSize") ? decD(coder, "UIFontPointSize") : decD(coder, "NSSize");
    long traits = decI(coder, "UIFontTraits");
    id f;
    if (decB(coder, "UISystemFont") && name && strncmp(rt_str_utf8(name), ".", 1) == 0) f = rt_font_make(size, (traits & 2) != 0, NULL);
    else if (name) f = RT_SEND2(id, (id)RT_CLASS(UIFont), "fontWithName:size:", id, name, double, size);
    else f = rt_font_make(size, (traits & 2) != 0, NULL);
    if (f && (traits & 2)) ((struct uifont *)f)->bold = 1;
    rt_dealloc_instance(self);
    return objc_retain(f);
}
static id fontdesc_initWithCoder(id self, SEL s, id coder) {
    (void)s; id attrs = dec(coder, "UIFontDescriptorAttributes");
    rt_dealloc_instance(self);
    return objc_retain(RT_SEND1(id, (id)RT_CLASS(UIFontDescriptor), "fontDescriptorWithFontAttributes:", id, attrs));
}
static id image_initWithCoder(id self, SEL s, id coder) {
    (void)s; id name = dec(coder, "UIResourceName");
    rt_dealloc_instance(self);
    return objc_retain(name ? RT_SEND1(id, (id)RT_CLASS(UIImage), "imageNamed:", id, name) : NULL);
}
/* NSAttributedString in nibs: NSString + attributes */
static id attrstr_initWithCoder(id self, SEL s, id coder) {
    (void)s; id str = dec(coder, "NSString"), attrs = dec(coder, "NSAttributes");
    if (attrs && !rt_is_kind_of(attrs, (Class)rt_class_named("NSDictionary"))) attrs = NULL;
    return RT_SEND2(id, self, "initWithString:attributes:", id, str ? str : rt_str(""), id, attrs);
}

void rt_nib_init(void) {
    Class o = RT_CLASS(NSObject);
    iosrt_UINibExternalObjects = rt_str_new("UINibExternalObjects");
    NEWCLASS(UINibDecoder, RT_CLASS(NSCoder), sizeof(struct nibcoder) - sizeof(void *));
    Class c = RT_CLASS(UINibDecoder);
    IM(c, "decodeObjectForKey:", nc_decodeObject); IM(c, "decodeObjectOfClass:forKey:", nc_decodeObjectOfClass);
    IM(c, "decodeObjectOfClasses:forKey:", nc_decodeObjectOfClass); IM(c, "decodeTopLevelObjectForKey:error:", nc_decodeObject);
    IM(c, "decodeBoolForKey:", nc_decodeBool); IM(c, "decodeIntegerForKey:", nc_decodeInteger); IM(c, "decodeIntForKey:", nc_decodeInt32);
    IM(c, "decodeInt32ForKey:", nc_decodeInt32); IM(c, "decodeInt64ForKey:", nc_decodeInt64);
    IM(c, "decodeDoubleForKey:", nc_decodeDouble); IM(c, "decodeFloatForKey:", nc_decodeFloat);
    IM(c, "decodeCGRectForKey:", nc_decodeRect); IM(c, "decodeCGPointForKey:", nc_decodePoint); IM(c, "decodeCGSizeForKey:", nc_decodeSize);
    IM(c, "decodeUIEdgeInsetsForKey:", nc_decodeInsets); IM(c, "decodeCGAffineTransformForKey:", nc_decodeAffine);
    IM(c, "decodeBytesForKey:returnedLength:", nc_decodeBytes); IM(c, "containsValueForKey:", nc_contains);
    IM(c, "allowsKeyedCoding", nc_yes); IM(c, "requiresSecureCoding", nc_no);

    NEWCLASS(UIRuntimeConnection, o, sizeof(struct conn) - sizeof(void *));
    IM(RT_CLASS(UIRuntimeConnection), "initWithCoder:", conn_initWithCoder); IM(RT_CLASS(UIRuntimeConnection), "dealloc", conn_dealloc);
    IM(RT_CLASS(UIRuntimeConnection), "connect", conn_connect);
    NEWCLASS(UIRuntimeOutletConnection, RT_CLASS(UIRuntimeConnection), 0);
    NEWCLASS(UIRuntimeEventConnection, RT_CLASS(UIRuntimeConnection), 0);
    NEWCLASS(UIRuntimeOutletCollectionConnection, RT_CLASS(UIRuntimeConnection), 0);
    NEWCLASS(UIProxyObject, o, 0);

    NEWCLASS(UINib, o, sizeof(struct uinib) - sizeof(void *));
    Class n = RT_CLASS(UINib);
    CM(n, "nibWithNibName:bundle:", nib_withName); CM(n, "nibWithData:bundle:", nib_withData);
    IM(n, "instantiateWithOwner:options:", nib_instantiate); IM(n, "dealloc", nib_dealloc);
    IM(RT_CLASS(NSBundle), "loadNibNamed:owner:options:", bundle_loadNib);

    IM(o, "initWithCoder:", nsobject_initWithCoder);
    IM(RT_CLASS(UIView), "initWithCoder:", view_initWithCoder);
    IM(RT_CLASS(UILabel), "initWithCoder:", label_initWithCoder);
    IM(RT_CLASS(UIImageView), "initWithCoder:", imageview_initWithCoder);
    IM(RT_CLASS(UIControl), "initWithCoder:", control_initWithCoder);
    IM(RT_CLASS(UIButton), "initWithCoder:", button_initWithCoder);
    IM(RT_CLASS(UIButtonContent), "initWithCoder:", bcontent_initWithCoder);
    IM(RT_CLASS(UIScrollView), "initWithCoder:", scroll_initWithCoder);
    IM(RT_CLASS(UIPageControl), "initWithCoder:", pagecontrol_initWithCoder);
    IM(RT_CLASS(UISlider), "initWithCoder:", slider_initWithCoder);
    IM(RT_CLASS(UISwitch), "initWithCoder:", switch_initWithCoder);
    IM(RT_CLASS(UISegmentedControl), "initWithCoder:", segmented_initWithCoder);
    IM(RT_CLASS(UIActivityIndicatorView), "initWithCoder:", activity_initWithCoder);
    IM(RT_CLASS(UIProgressView), "initWithCoder:", progress_initWithCoder);
    IM(RT_CLASS(UITextField), "initWithCoder:", textfield_initWithCoder);
    IM(RT_CLASS(UITextView), "initWithCoder:", textview_initWithCoder);
    IM(RT_CLASS(UITableView), "initWithCoder:", table_initWithCoder);
    IM(RT_CLASS(UIToolbar), "initWithCoder:", toolbar_initWithCoder);
    IM(RT_CLASS(UINavigationBar), "initWithCoder:", toolbar_initWithCoder);
    IM(RT_CLASS(UIBarButtonItem), "initWithCoder:", barbutton_initWithCoder);
    IM(RT_CLASS(UINavigationItem), "initWithCoder:", navitem_initWithCoder);
    IM(RT_CLASS(UIGestureRecognizer), "initWithCoder:", gesture_initWithCoder);
    IM(RT_CLASS(UIColor), "initWithCoder:", color_initWithCoder);
    IM(RT_CLASS(UIFont), "initWithCoder:", font_initWithCoder);
    IM(RT_CLASS(UIFontDescriptor), "initWithCoder:", fontdesc_initWithCoder);
    IM(RT_CLASS(UIImage), "initWithCoder:", image_initWithCoder);
    IM((Class)rt_class_named("NSAttributedString"), "initWithCoder:", attrstr_initWithCoder);
    /* UISegment: private class used inside segmented controls */
    static struct objc_class seg_cls, seg_meta;
    rt_native_class(&seg_cls, &seg_meta, "UISegment", RT_CLASS(NSObject), sizeof(struct uiseg) - sizeof(void *));
    rt_native_method(&seg_cls, "initWithCoder:", (void *)segment_initWithCoder, "");
    rt_native_method(&seg_cls, "valueForKey:", (void *)segment_valueForKey, "");
}
