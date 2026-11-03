/*
 * Foundation HLE, part 7: small additions found by tracing real apps —
 * NSArray makeObjectsPerformSelector:withObject:, NSBundle
 * pathsForResourcesOfType:inDirectory:, NSDictionary file-attribute
 * accessors, NSPointerArray fast enumeration, performSelectorOnMainThread
 * with modes, and a few vDSP helpers.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <math.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSArray);
RT_DECLARE_CLASS(NSMutableArray);
RT_DECLARE_CLASS(NSDictionary);
RT_DECLARE_CLASS(NSBundle);
RT_DECLARE_CLASS(NSPointerArray);
RT_DECLARE_CLASS(NSData);
RT_DECLARE_CLASS(NSMutableDictionary);
RT_DEFINE_CLASS(NSNetService);
RT_DEFINE_CLASS(NSNetServiceBrowser);
RT_DEFINE_CLASS(NSPort);
RT_DEFINE_CLASS(NSMachPort);

static void a_performWith(id self, SEL s, SEL q, id arg) {
    (void)s; size_t n = rt_array_count(self);
    for (size_t i = 0; i < n; i++) ((void (*)(id, SEL, id))objc_msgSend)(rt_array_at(self, i), q, arg);
}

static id b_pathsOfType(id self, SEL s, id ext, id sub) {
    (void)s;
    id dir = RT_SEND0(id, self, "bundlePath");
    if (sub && RT_SEND0(unsigned long, sub, "length")) dir = RT_SEND1(id, dir, "stringByAppendingPathComponent:", id, sub);
    id out = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array");
    const char *d = rt_str_utf8(dir), *e = ext ? rt_str_utf8(ext) : "";
    DIR *dp = opendir(d);
    if (!dp) return out;
    struct dirent *de;
    size_t el = strlen(e);
    while ((de = readdir(dp))) {
        if (de->d_name[0] == '.') continue;
        size_t nl = strlen(de->d_name);
        if (el) { if (nl <= el + 1 || de->d_name[nl - el - 1] != '.' || strcmp(de->d_name + nl - el, e)) continue; }
        char p[4096]; snprintf(p, sizeof p, "%s/%s", d, de->d_name);
        RT_SEND1(void, out, "addObject:", id, rt_str(p));
    }
    closedir(dp);
    return out;
}

static id d_attr(id self, const char *k) { return rt_dict_get(self, rt_str(k)); }
static unsigned long long d_fileSize(id self, SEL s) { (void)s; id v = d_attr(self, "NSFileSize"); return v ? RT_SEND0(unsigned long long, v, "unsignedLongLongValue") : 0; }
static id d_fileModificationDate(id self, SEL s) { (void)s; return d_attr(self, "NSFileModificationDate"); }
static id d_fileCreationDate(id self, SEL s) { (void)s; id v = d_attr(self, "NSFileCreationDate"); return v ? v : d_attr(self, "NSFileModificationDate"); }
static id d_fileType(id self, SEL s) { (void)s; return d_attr(self, "NSFileType"); }

/* NSPointerArray: { isa, void **v, n, cap } (see more.c) */
struct nsparr { Class isa; void **v; unsigned long n, cap; };
struct fast_state4 { unsigned long state; id *itemsPtr; unsigned long *mutationsPtr; unsigned long extra[5]; };
static unsigned long pa_fast(id self, SEL s, struct fast_state4 *st, id *buf, unsigned long len) {
    (void)s; (void)buf; (void)len; struct nsparr *a = (struct nsparr *)self;
    if (st->state) return 0;
    st->state = 1; st->itemsPtr = (id *)a->v; st->mutationsPtr = &a->cap;
    return a->n;
}

static void o_performOnMainModes(id self, SEL s, SEL sel, id arg, long wait, id modes) {
    (void)modes;
    ((void (*)(id, SEL, SEL, id, long))objc_msgSend)(self, rt_sel("performSelectorOnMainThread:withObject:waitUntilDone:"), sel, arg, wait);
    (void)s;
}
static void o_performAfterDelayModes(id self, SEL s, SEL sel, id arg, double delay, id modes) {
    (void)s; (void)modes;
    ((void (*)(id, SEL, SEL, id, double))objc_msgSend)(self, rt_sel("performSelector:withObject:afterDelay:"), sel, arg, delay);
}

/* ---- NSNetService / NSNetServiceBrowser: no mDNS on the device side yet.
 * Services keep their properties and TXT data; publish/resolve/search never
 * find anything (the delegate is told publishing failed). TXT records use the
 * real DNS-SD encoding so code that round-trips them works. */
struct nsnet { Class isa; id domain, type, name; long port; id delegate; id txt; };
static id ns_init(id self, SEL s, id dom, id type, id name, long port) {
    (void)s; struct nsnet *n = (struct nsnet *)self;
    n->domain = objc_retain(dom); n->type = objc_retain(type); n->name = objc_retain(name); n->port = (int)port; return self;
}
static id ns_init4(id self, SEL s, id dom, id type, id name) { return ns_init(self, s, dom, type, name, -1); }
static id ns_name(id self, SEL s) { (void)s; return ((struct nsnet *)self)->name; }
static id ns_type(id self, SEL s) { (void)s; return ((struct nsnet *)self)->type; }
static id ns_domain(id self, SEL s) { (void)s; return ((struct nsnet *)self)->domain; }
static long ns_port(id self, SEL s) { (void)s; return ((struct nsnet *)self)->port; }
static id ns_hostName(id self, SEL s) { (void)self; (void)s; return NULL; }
static id ns_addresses(id self, SEL s) { (void)self; (void)s; return RT_SEND0(id, (id)RT_CLASS(NSArray), "array"); }
static id ns_delegate(id self, SEL s) { (void)s; return ((struct nsnet *)self)->delegate; }
static void ns_setDelegate(id self, SEL s, id d) { (void)s; ((struct nsnet *)self)->delegate = d; }
static id ns_txt(id self, SEL s) { (void)s; return ((struct nsnet *)self)->txt; }
static rt_bool ns_setTXT(id self, SEL s, id d) { (void)s; struct nsnet *n = (struct nsnet *)self; id o = n->txt; n->txt = objc_retain(d); objc_release(o); return 1; }
static void ns_noop(id self, SEL s) { (void)self; (void)s; }
static void ns_noop1(id self, SEL s, id a) { (void)self; (void)s; (void)a; }
static void ns_noop2(id self, SEL s, id a, id b) { (void)self; (void)s; (void)a; (void)b; }
static void ns_publish(id self, SEL s) {
    (void)s; struct nsnet *n = (struct nsnet *)self;
    if (rt_first_time("NSNetService.publish")) rt_warn("NSNetService: Bonjour publishing/browsing is not implemented (services stay local)");
    if (n->delegate && rt_responds(n->delegate, rt_sel("netService:didNotPublish:"))) {
        id keys[2] = { rt_str("NSNetServicesErrorCode"), rt_str("NSNetServicesErrorDomain") };
        id vals[2] = { RT_SEND1(id, (id)rt_class_named("NSNumber"), "numberWithInt:", int, -72002), RT_SEND1(id, (id)rt_class_named("NSNumber"), "numberWithInt:", int, 10) };
        id err = RT_SEND3(id, (id)RT_CLASS(NSDictionary), "dictionaryWithObjects:forKeys:count:", id *, vals, id *, keys, unsigned long, 2);
        RT_SEND2(void, n->delegate, "netService:didNotPublish:", id, self, id, err);
    }
}
static void ns_publishOpts(id self, SEL s, unsigned long o) { (void)o; ns_publish(self, s); }
static void ns_dealloc(id self, SEL s) {
    (void)s; struct nsnet *n = (struct nsnet *)self;
    objc_release(n->domain); objc_release(n->type); objc_release(n->name); objc_release(n->txt);
    rt_dealloc_instance(self);
}
static id ns_dataFromTXT(Class c, SEL s, id dict) {
    (void)c; (void)s;
    unsigned char buf[8192]; size_t len = 0;
    id keys = dict ? RT_SEND0(id, dict, "allKeys") : NULL;
    size_t nk = keys ? rt_array_count(keys) : 0;
    for (size_t i = 0; i < nk; i++) {
        id k = rt_array_at(keys, i), v = rt_dict_get(dict, k);
        const char *ks = rt_str_utf8(k); size_t kl = strlen(ks);
        const void *vb = NULL; size_t vl = 0;
        if (v && rt_is_kind_of(v, RT_CLASS(NSData))) { vb = RT_SEND0(const void *, v, "bytes"); vl = RT_SEND0(unsigned long, v, "length"); }
        else if (v) { vb = rt_str_utf8(v); vl = strlen(vb); }
        size_t el = kl + (v ? 1 + vl : 0);
        if (el > 255 || len + 1 + el > sizeof buf) continue;
        buf[len++] = (unsigned char)el; memcpy(buf + len, ks, kl); len += kl;
        if (v) { buf[len++] = '='; memcpy(buf + len, vb, vl); len += vl; }
    }
    if (!len) buf[len++] = 0;   /* empty TXT record is a single zero byte */
    return RT_SEND2(id, (id)RT_CLASS(NSData), "dataWithBytes:length:", const void *, buf, unsigned long, len);
}
static id ns_dictFromTXT(Class c, SEL s, id data) {
    (void)c; (void)s;
    id d = RT_SEND0(id, (id)RT_CLASS(NSMutableDictionary), "dictionary");
    if (!data) return d;
    const unsigned char *p = RT_SEND0(const unsigned char *, data, "bytes"); size_t n = RT_SEND0(unsigned long, data, "length");
    for (size_t i = 0; i < n;) {
        size_t l = p[i++]; if (i + l > n) break;
        const unsigned char *e = memchr(p + i, '=', l);
        size_t kl = e ? (size_t)(e - (p + i)) : l;
        char key[256]; memcpy(key, p + i, kl); key[kl] = 0;
        id val = e ? RT_SEND2(id, (id)RT_CLASS(NSData), "dataWithBytes:length:", const void *, e + 1, unsigned long, l - kl - 1)
                   : RT_SEND0(id, (id)RT_CLASS(NSData), "data");
        if (kl) RT_SEND2(void, d, "setObject:forKey:", id, val, id, rt_str(key));
        i += l;
    }
    return d;
}
struct nsnb { Class isa; id delegate; };
static id nb_delegate(id self, SEL s) { (void)s; return ((struct nsnb *)self)->delegate; }
static void nb_setDelegate(id self, SEL s, id d) { (void)s; ((struct nsnb *)self)->delegate = d; }

/* ---- NSPort / NSMachPort: only used to keep secondary run loops alive. */
struct nsport { Class isa; id delegate; long valid; };
static id port_new(Class c, SEL s) { (void)s; id p = rt_alloc_instance(c, 0); ((struct nsport *)p)->valid = 1; return objc_autorelease(p); }
static id port_init(id self, SEL s) { (void)s; ((struct nsport *)self)->valid = 1; return self; }
static id port_delegate(id self, SEL s) { (void)s; return ((struct nsport *)self)->delegate; }
static void port_setDelegate(id self, SEL s, id d) { (void)s; ((struct nsport *)self)->delegate = d; }
static void port_invalidate(id self, SEL s) { (void)s; ((struct nsport *)self)->valid = 0; }
static rt_bool port_isValid(id self, SEL s) { (void)s; return ((struct nsport *)self)->valid != 0; }
static unsigned port_machPort(id self, SEL s) { (void)s; return (unsigned)(((uintptr_t)self >> 4) & 0x7fffffff); }
static void port_schedule(id self, SEL s, id rl, id mode) { (void)s; RT_SEND2(void, rl, "addPort:forMode:", id, self, id, mode); }
static void port_unschedule(id self, SEL s, id rl, id mode) { (void)s; RT_SEND2(void, rl, "removePort:forMode:", id, self, id, mode); }

/* ---- vDSP (Accelerate): plain C loops, float and double variants ---- */
typedef unsigned long vlen; typedef long vstr;
#define VDSP_BOTH(F) F(, float) F(D, double)
#define DEF(S, T) \
void iosrt_vDSP_maxmgv##S(const T *a, vstr ia, T *c, vlen n) { T m = 0; for (vlen i = 0; i < n; i++) { T v = a[i * ia] < 0 ? -a[i * ia] : a[i * ia]; if (v > m) m = v; } *c = m; } \
void iosrt_vDSP_maxmgvi##S(const T *a, vstr ia, T *c, vlen *ix, vlen n) { T m = 0; vlen k = 0; for (vlen i = 0; i < n; i++) { T v = a[i * ia] < 0 ? -a[i * ia] : a[i * ia]; if (v > m) { m = v; k = i * (vlen)ia; } } *c = m; *ix = k; } \
void iosrt_vDSP_meamgv##S(const T *a, vstr ia, T *c, vlen n) { double s = 0; for (vlen i = 0; i < n; i++) s += a[i * ia] < 0 ? -a[i * ia] : a[i * ia]; *c = n ? (T)(s / n) : 0; } \
void iosrt_vDSP_minv##S(const T *a, vstr ia, T *c, vlen n) { T m = n ? a[0] : (T)__builtin_inf(); for (vlen i = 0; i < n; i++) if (a[i * ia] < m) m = a[i * ia]; *c = m; } \
void iosrt_vDSP_maxv##S(const T *a, vstr ia, T *c, vlen n) { T m = n ? a[0] : (T)-__builtin_inf(); for (vlen i = 0; i < n; i++) if (a[i * ia] > m) m = a[i * ia]; *c = m; } \
void iosrt_vDSP_rect##S(const T *a, vstr ia, T *c, vlen ic, vlen n) { for (vlen i = 0; i < n; i++) { T r = a[i * ia], t = a[i * ia + 1]; c[i * ic] = r * (T)cos(t); c[i * ic + 1] = r * (T)sin(t); } } \
void iosrt_vDSP_vadd##S(const T *a, vstr ia, const T *b, vstr ib, T *c, vstr ic, vlen n) { for (vlen i = 0; i < n; i++) c[i * ic] = a[i * ia] + b[i * ib]; } \
void iosrt_vDSP_vmul##S(const T *a, vstr ia, const T *b, vstr ib, T *c, vstr ic, vlen n) { for (vlen i = 0; i < n; i++) c[i * ic] = a[i * ia] * b[i * ib]; } \
void iosrt_vDSP_vmax##S(const T *a, vstr ia, const T *b, vstr ib, T *c, vstr ic, vlen n) { for (vlen i = 0; i < n; i++) c[i * ic] = a[i * ia] > b[i * ib] ? a[i * ia] : b[i * ib]; } \
void iosrt_vDSP_vclip##S(const T *a, vstr ia, const T *lo, const T *hi, T *c, vstr ic, vlen n) { for (vlen i = 0; i < n; i++) { T v = a[i * ia]; c[i * ic] = v < *lo ? *lo : v > *hi ? *hi : v; } } \
void iosrt_vDSP_vclr##S(T *c, vstr ic, vlen n) { for (vlen i = 0; i < n; i++) c[i * ic] = 0; } \
void iosrt_vDSP_vfill##S(const T *a, T *c, vstr ic, vlen n) { for (vlen i = 0; i < n; i++) c[i * ic] = *a; } \
void iosrt_vDSP_vdbcon##S(const T *a, vstr ia, const T *b, T *c, vstr ic, vlen n, unsigned f) { T k = f ? 20 : 10; for (vlen i = 0; i < n; i++) c[i * ic] = k * (T)log10(a[i * ia] / *b); } \
void iosrt_vDSP_vflt16##S(const short *a, vstr ia, T *c, vstr ic, vlen n) { for (vlen i = 0; i < n; i++) c[i * ic] = a[i * ia]; } \
void iosrt_vDSP_vfix16##S(const T *a, vstr ia, short *c, vstr ic, vlen n) { for (vlen i = 0; i < n; i++) c[i * ic] = (short)a[i * ia]; } \
void iosrt_vDSP_vramp##S(const T *a, const T *b, T *c, vstr ic, vlen n) { for (vlen i = 0; i < n; i++) c[i * ic] = *a + (T)i * *b; } \
void iosrt_vDSP_vsadd##S(const T *a, vstr ia, const T *b, T *c, vstr ic, vlen n) { for (vlen i = 0; i < n; i++) c[i * ic] = a[i * ia] + *b; } \
void iosrt_vDSP_vsmul##S(const T *a, vstr ia, const T *b, T *c, vstr ic, vlen n) { for (vlen i = 0; i < n; i++) c[i * ic] = a[i * ia] * *b; } \
void iosrt_vDSP_vsma##S(const T *a, vstr ia, const T *b, const T *c, vstr icc, T *d, vstr id_, vlen n) { for (vlen i = 0; i < n; i++) d[i * id_] = a[i * ia] * *b + c[i * icc]; } \
void iosrt_vDSP_vrvrs##S(T *c, vstr ic, vlen n) { for (vlen i = 0; i < n / 2; i++) { T t = c[i * ic]; c[i * ic] = c[(n - 1 - i) * ic]; c[(n - 1 - i) * ic] = t; } } \
void iosrt_vDSP_vrampmul##S(const T *a, vstr ia, T *start, const T *step, T *c, vstr ic, vlen n) { T g = *start; for (vlen i = 0; i < n; i++) { c[i * ic] = a[i * ia] * g; g += *step; } *start = g; } \
void iosrt_vDSP_vrampmuladd##S(const T *a, vstr ia, T *start, const T *step, T *c, vstr ic, vlen n) { T g = *start; for (vlen i = 0; i < n; i++) { c[i * ic] += a[i * ia] * g; g += *step; } *start = g; } \
void iosrt_vDSP_vrampmul2##S(const T *a0, const T *a1, vstr ia, T *start, const T *step, T *c0, T *c1, vstr ic, vlen n) { T g = *start; for (vlen i = 0; i < n; i++) { c0[i * ic] = a0[i * ia] * g; c1[i * ic] = a1[i * ia] * g; g += *step; } *start = g; }
VDSP_BOTH(DEF)
#undef DEF

static void o_nop(id self, SEL s) { (void)self; (void)s; }
/* KVC collection proxies, simplified: hand out a mutable array stored under the key */
static id o_mutableArrayForKey(id self, SEL s, id key) {
    (void)s; id v = RT_SEND1(id, self, "valueForKey:", id, key);
    if (v && rt_is_kind_of(v, RT_CLASS(NSMutableArray))) return v;
    id m = v ? RT_SEND0(id, v, "mutableCopy") : RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "alloc"), "init");
    RT_SEND2(void, self, "setValue:forKey:", id, m, id, key);
    return objc_autorelease(m);
}
static id o_mutableSetForKey(id self, SEL s, id key) {
    (void)s; Class ms = (Class)rt_class_named("NSMutableSet");
    id v = RT_SEND1(id, self, "valueForKey:", id, key);
    if (v && rt_is_kind_of(v, ms)) return v;
    id m = v ? RT_SEND0(id, v, "mutableCopy") : RT_SEND0(id, (id)ms, "new");
    RT_SEND2(void, self, "setValue:forKey:", id, m, id, key);
    return objc_autorelease(m);
}
static id rq_initFull(id self, SEL s, id url, unsigned long policy, double timeout) {
    (void)s; self = RT_SEND1(id, self, "initWithURL:", id, url);
    if (self && rt_responds(self, rt_sel("setTimeoutInterval:"))) RT_SEND1(void, self, "setTimeoutInterval:", double, timeout);
    (void)policy; return self;
}
static id rq_withFull(Class c, SEL s, id url, unsigned long policy, double timeout) { return objc_autorelease(rq_initFull(RT_SEND0(id, (id)c, "alloc"), s, url, policy, timeout)); }
/* _NSDictionaryOfVariableBindings(@"a, b", a, b, nil): keys from the string, values from the varargs */
id rt_NSDictionaryOfVariableBindings(id keys, id first, const uint64_t *rest) {
    id d = RT_SEND0(id, (id)rt_class_named("NSMutableDictionary"), "dictionary");
    id parts = RT_SEND1(id, keys, "componentsSeparatedByString:", id, rt_str(","));
    for (size_t i = 0; i < rt_array_count(parts); i++) {
        id k = RT_SEND1(id, rt_array_at(parts, i), "stringByTrimmingCharactersInSet:", id, RT_SEND0(id, (id)rt_class_named("NSCharacterSet"), "whitespaceCharacterSet"));
        id v = i == 0 ? first : (id)rest[i - 1];
        if (!v) break;
        RT_SEND2(void, d, "setObject:forKey:", id, v, id, k);
    }
    return d;
}
/* vImage: box blur with edge extension (Accelerate) */
struct vimage_buffer { uint8_t *data; unsigned long height, width; size_t rowBytes; };
long iosrt_vImageBoxConvolve_ARGB8888(const struct vimage_buffer *src, const struct vimage_buffer *dst, void *tmp, unsigned long ox, unsigned long oy,
                                       uint32_t kh, uint32_t kw, const uint8_t *bg, uint32_t flags) {
    (void)tmp; (void)bg;
    if (flags & 128) return 0;                     /* kvImageGetTempBufferSize: none needed */
    if (!src || !dst || !(kh & 1) || !(kw & 1)) return -21767;   /* kvImageInvalidKernelSize */
    unsigned long w = dst->width, h = dst->height;
    uint32_t *mid = malloc(w * h * 4 * sizeof(uint32_t));
    int rx = (int)kw / 2, ry = (int)kh / 2;
    for (unsigned long y = 0; y < h; y++)
        for (unsigned long x = 0; x < w; x++)
            for (int c = 0; c < 4; c++) {
                uint32_t sum = 0;
                for (int k = -rx; k <= rx; k++) {
                    long sx = (long)(x + ox) + k; if (sx < 0) sx = 0; if (sx >= (long)src->width) sx = (long)src->width - 1;
                    long sy = (long)(y + oy); if (sy >= (long)src->height) sy = (long)src->height - 1;
                    sum += src->data[(size_t)sy * src->rowBytes + (size_t)sx * 4 + c];
                }
                mid[(y * w + x) * 4 + c] = sum;
            }
    for (unsigned long y = 0; y < h; y++)
        for (unsigned long x = 0; x < w; x++)
            for (int c = 0; c < 4; c++) {
                uint32_t sum = 0;
                for (int k = -ry; k <= ry; k++) { long sy = (long)y + k; if (sy < 0) sy = 0; if (sy >= (long)h) sy = (long)h - 1; sum += mid[((size_t)sy * w + x) * 4 + c]; }
                dst->data[y * dst->rowBytes + x * 4 + c] = (uint8_t)(sum / (kw * kh));
            }
    free(mid);
    return 0;
}
static id d_initWithDictCopy(id self, SEL s, id other, long copy) { (void)s; (void)copy; return RT_SEND1(id, self, "initWithDictionary:", id, other); }
struct rt_method *rt_find_method(Class cls, SEL sel);
void rt_foundation_more4_init(void) {
    IM(RT_CLASS(NSArray), "makeObjectsPerformSelector:withObject:", a_performWith);
    IM(RT_CLASS(NSDictionary), "initWithDictionary:copyItems:", d_initWithDictCopy);
    IM(RT_CLASS(NSObject), "awakeFromNib", o_nop); IM(RT_CLASS(NSObject), "prepareForInterfaceBuilder", o_nop);
    IM(RT_CLASS(NSObject), "mutableArrayValueForKey:", o_mutableArrayForKey); IM(RT_CLASS(NSObject), "mutableSetValueForKey:", o_mutableSetForKey);
    Class rq = (Class)rt_class_named("NSURLRequest");
    if (rq && !rt_find_method(rq, rt_sel("initWithURL:cachePolicy:timeoutInterval:"))) IM(rq, "initWithURL:cachePolicy:timeoutInterval:", rq_initFull);
    if (rq && !rt_find_method(rq->isa, rt_sel("requestWithURL:cachePolicy:timeoutInterval:"))) CM(rq, "requestWithURL:cachePolicy:timeoutInterval:", rq_withFull);
    IM(RT_CLASS(NSBundle), "pathsForResourcesOfType:inDirectory:", b_pathsOfType);
    Class d = RT_CLASS(NSDictionary);
    IM(d, "fileSize", d_fileSize); IM(d, "fileModificationDate", d_fileModificationDate);
    IM(d, "fileCreationDate", d_fileCreationDate); IM(d, "fileType", d_fileType);
    IM(RT_CLASS(NSPointerArray), "countByEnumeratingWithState:objects:count:", pa_fast);
    Class o = RT_CLASS(NSObject);
    IM(o, "performSelectorOnMainThread:withObject:waitUntilDone:modes:", o_performOnMainModes);
    IM(o, "performSelector:withObject:afterDelay:inModes:", o_performAfterDelayModes);

    rt_native_class(RT_CLASS(NSNetService), &iosrt_meta_NSNetService, "NSNetService", o, sizeof(struct nsnet) - sizeof(void *));
    Class ns = RT_CLASS(NSNetService);
    IM(ns, "initWithDomain:type:name:port:", ns_init); IM(ns, "initWithDomain:type:name:", ns_init4);
    IM(ns, "name", ns_name); IM(ns, "type", ns_type); IM(ns, "domain", ns_domain); IM(ns, "port", ns_port);
    IM(ns, "hostName", ns_hostName); IM(ns, "addresses", ns_addresses);
    IM(ns, "delegate", ns_delegate); IM(ns, "setDelegate:", ns_setDelegate);
    IM(ns, "TXTRecordData", ns_txt); IM(ns, "setTXTRecordData:", ns_setTXT);
    IM(ns, "publish", ns_publish); IM(ns, "publishWithOptions:", ns_publishOpts);
    IM(ns, "stop", ns_noop); IM(ns, "resolve", ns_noop); IM(ns, "resolveWithTimeout:", ns_noop);
    IM(ns, "startMonitoring", ns_noop); IM(ns, "stopMonitoring", ns_noop);
    IM(ns, "scheduleInRunLoop:forMode:", ns_noop2); IM(ns, "removeFromRunLoop:forMode:", ns_noop2);
    IM(ns, "dealloc", ns_dealloc);
    CM(ns, "dataFromTXTRecordDictionary:", ns_dataFromTXT); CM(ns, "dictionaryFromTXTRecordData:", ns_dictFromTXT);
    rt_native_class(RT_CLASS(NSNetServiceBrowser), &iosrt_meta_NSNetServiceBrowser, "NSNetServiceBrowser", o, sizeof(struct nsnb) - sizeof(void *));
    Class nb = RT_CLASS(NSNetServiceBrowser);
    IM(nb, "delegate", nb_delegate); IM(nb, "setDelegate:", nb_setDelegate);
    IM(nb, "searchForServicesOfType:inDomain:", ns_noop2); IM(nb, "searchForBrowsableDomains", ns_noop);
    IM(nb, "searchForRegistrationDomains", ns_noop); IM(nb, "stop", ns_noop);
    IM(nb, "scheduleInRunLoop:forMode:", ns_noop2); IM(nb, "removeFromRunLoop:forMode:", ns_noop2);
    (void)ns_noop1;

    rt_native_class(RT_CLASS(NSPort), &iosrt_meta_NSPort, "NSPort", o, sizeof(struct nsport) - sizeof(void *));
    rt_native_class(RT_CLASS(NSMachPort), &iosrt_meta_NSMachPort, "NSMachPort", RT_CLASS(NSPort), 0);
    Class pt = RT_CLASS(NSPort);
    CM(pt, "port", port_new); IM(pt, "init", port_init);
    IM(pt, "delegate", port_delegate); IM(pt, "setDelegate:", port_setDelegate);
    IM(pt, "invalidate", port_invalidate); IM(pt, "isValid", port_isValid); IM(pt, "machPort", port_machPort);
    IM(pt, "scheduleInRunLoop:forMode:", port_schedule); IM(pt, "removeFromRunLoop:forMode:", port_unschedule);
}
