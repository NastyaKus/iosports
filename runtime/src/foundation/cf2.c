/*
 * CoreFoundation, part 2: functions found by tracing GarageBand, on top of the
 * toll-free bridged Foundation objects (CFStringRef is an NSString, CFURLRef
 * an NSURL, CFBundleRef an NSBundle, ...), plus the Foundation methods they need.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")
typedef struct { long location, length; } CFRange;
void *rt_dlsym_name(const char *cname);
id rt_data_new(const void *b, size_t n);
const void *rt_data_bytes(id d, size_t *n);
static Class cls(const char *n) { return rt_class_named(n); }
static id retained(id o) { return o ? objc_retain(o) : NULL; }

/* ------------------------------------------------------------- strings */
long iosrt_CFStringGetFileSystemRepresentation(id s, char *buf, long max) {
    if (!s || !buf || max <= 0) return 0;
    const char *u = rt_str_utf8(s); size_t n = strlen(u);
    if ((long)n + 1 > max) return 0;
    memcpy(buf, u, n + 1);
    return 1;
}
long iosrt_CFStringGetMaximumSizeOfFileSystemRepresentation(id s) {
    return s ? (long)RT_SEND0(unsigned long, s, "length") * 3 + 1 : 1;
}
CFRange iosrt_CFStringFind(id s, id find, unsigned long opts) {
    CFRange r = { -1, 0 };   /* kCFNotFound */
    if (!s || !find) return r;
    NSRange nr = ((NSRange (*)(id, SEL, id, unsigned long))objc_msgSend)(s, rt_sel("rangeOfString:options:"), find, opts);
    if (nr.length) { r.location = (long)nr.location; r.length = (long)nr.length; }
    return r;
}
void iosrt_CFStringTrimWhitespace(id ms) {
    if (!ms) return;
    id t = RT_SEND1(id, ms, "stringByTrimmingCharactersInSet:", id, RT_SEND0(id, (id)cls("NSCharacterSet"), "whitespaceAndNewlineCharacterSet"));
    RT_SEND1(void, ms, "setString:", id, t);
}
void iosrt_CFStringReplaceAll(id ms, id repl) { if (ms) RT_SEND1(void, ms, "setString:", id, repl ? repl : rt_str("")); }
long iosrt_CFStringFindAndReplace(id ms, id find, id repl, CFRange r, unsigned long opts) {
    if (!ms || !find) return 0;
    return (long)((unsigned long (*)(id, SEL, id, id, unsigned long, NSRange))objc_msgSend)(ms, rt_sel("replaceOccurrencesOfString:withString:options:range:"),
        find, repl, opts, (NSRange){ (unsigned long)r.location, (unsigned long)r.length });
}
id iosrt_CFStringCreateByCombiningStrings(void *alloc, id array, id sep) { (void)alloc; return retained(RT_SEND1(id, array, "componentsJoinedByString:", id, sep ? sep : rt_str(""))); }
id iosrt_CFStringCreateWithBytes(const void *alloc, const uint8_t *b, long n, uint32_t enc, long bom);
id iosrt_CFStringCreateWithBytesNoCopy(void *alloc, const uint8_t *b, long n, uint32_t enc, long ext, void *dealloc) {
    (void)dealloc; return iosrt_CFStringCreateWithBytes(alloc, b, n, enc, ext);
}
id iosrt_CFStringCreateWithCharactersNoCopy(void *alloc, const unichar *u, long n, void *dealloc) { (void)alloc; (void)dealloc; return rt_str_new_utf16(u, (size_t)n); }
id iosrt_CFMakeCollectable(id o) { return o; }

/* ----------------------------------------------------------------- URLs */
id iosrt_CFURLCopyStrictPath(id url, unsigned char *isAbsolute) {
    if (!url) return NULL;
    id p = RT_SEND0(id, url, "path");
    const char *u = p ? rt_str_utf8(p) : "";
    if (isAbsolute) *isAbsolute = u[0] == '/';
    return rt_str_new(u[0] == '/' ? u + 1 : u);
}
id iosrt_CFURLCopyAbsoluteURL(id url) { return url ? retained(RT_SEND0(id, url, "absoluteURL")) : NULL; }
id iosrt_CFURLCreateCopyAppendingPathComponent(void *alloc, id url, id comp, long isDir) {
    (void)alloc; return url ? retained(RT_SEND2(id, url, "URLByAppendingPathComponent:isDirectory:", id, comp, long, isDir & 0xff)) : NULL;
}
id iosrt_CFURLCreateCopyDeletingLastPathComponent(void *alloc, id url) { (void)alloc; return url ? retained(RT_SEND0(id, url, "URLByDeletingLastPathComponent")) : NULL; }
long iosrt_CFURLHasDirectoryPath(id url) { return url && (RT_SEND0(long, url, "hasDirectoryPath") & 0xff); }
id iosrt_CFURLGetBaseURL(id url) { return url ? RT_SEND0(id, url, "baseURL") : NULL; }
id iosrt_CFURLCreateFilePathURL(void *alloc, id url, void *err) { (void)alloc; if (err) *(void **)err = NULL; return retained(url); }
id iosrt_CFURLCreateStringByReplacingPercentEscapes(void *alloc, id s, id leave) { (void)alloc; (void)leave; return s ? retained(RT_SEND0(id, s, "stringByRemovingPercentEncoding")) : NULL; }
id iosrt_CFURLCreateStringByReplacingPercentEscapesUsingEncoding(void *alloc, id s, id leave, uint32_t enc) { (void)enc; return iosrt_CFURLCreateStringByReplacingPercentEscapes(alloc, s, leave); }
long iosrt_CFURLIsFileReferenceURL(id url) { (void)url; return 0; }

/* ---------------------------------------------------------------- bundles */
id iosrt_CFBundleCopyResourcesDirectoryURL(id b) { return b ? retained(RT_SEND0(id, b, "resourceURL")) : NULL; }
id iosrt_CFBundleCopyResourceURLsOfType(id b, id type, id sub) { return b ? retained(RT_SEND2(id, b, "URLsForResourcesWithExtension:subdirectory:", id, type, id, sub)) : NULL; }
id iosrt_CFBundleCreate(void *alloc, id url) { (void)alloc; return url ? retained(RT_SEND1(id, (id)cls("NSBundle"), "bundleWithURL:", id, url)) : NULL; }
long iosrt_CFBundleLoadExecutable(id b) { return b && (RT_SEND0(long, b, "load") & 0xff); }
void iosrt_CFBundleUnloadExecutable(id b) { (void)b; }
void *iosrt_CFBundleGetFunctionPointerForName(id b, id name) {
    if (!b || !name) return NULL;
    iosrt_CFBundleLoadExecutable(b);
    return rt_dlsym_name(rt_str_utf8(name));
}

/* ------------------------------------------------------------ preferences */
id iosrt_CFPreferencesCopyAppValue(id key, id app);
void iosrt_CFPreferencesSetAppValue(id key, id val, id app);
long iosrt_CFPreferencesGetAppBooleanValue(id key, id app, unsigned char *valid) {
    id v = iosrt_CFPreferencesCopyAppValue(key, app);
    long r = 0, ok = 0;
    if (v && (RT_SEND1(long, v, "respondsToSelector:", SEL, rt_sel("boolValue")) & 0xff)) { r = RT_SEND0(long, v, "boolValue") & 0xff; ok = 1; }
    objc_release(v);
    if (valid) *valid = (unsigned char)ok;
    return r;
}
long iosrt_CFPreferencesGetAppIntegerValue(id key, id app, unsigned char *valid) {
    id v = iosrt_CFPreferencesCopyAppValue(key, app);
    long r = 0, ok = 0;
    if (v && (RT_SEND1(long, v, "respondsToSelector:", SEL, rt_sel("integerValue")) & 0xff)) { r = RT_SEND0(long, v, "integerValue"); ok = 1; }
    objc_release(v);
    if (valid) *valid = (unsigned char)ok;
    return r;
}
id iosrt_CFPreferencesCopyValue(id key, id app, id user, id host) { (void)user; (void)host; return iosrt_CFPreferencesCopyAppValue(key, app); }
void iosrt_CFPreferencesSetValue(id key, id val, id app, id user, id host) { (void)user; (void)host; iosrt_CFPreferencesSetAppValue(key, val, app); }
id iosrt_kCFPreferencesAnyHost, iosrt_kCFPreferencesCurrentHost, iosrt_kCFPreferencesCurrentUser, iosrt_kCFPreferencesAnyUser,
   iosrt_kCFPreferencesCurrentApplication, iosrt_kCFPreferencesAnyApplication;

/* ------------------------------------------------------ sets, data, arrays */
id iosrt_CFSetCreateMutable(void *alloc, long cap, const void *cb) { (void)alloc; (void)cap; (void)cb; return RT_SEND0(id, RT_SEND0(id, (id)cls("NSMutableSet"), "alloc"), "init"); }
void iosrt_CFSetAddValue(id set, id v) { if (set && v) RT_SEND1(void, set, "addObject:", id, v); }
void iosrt_CFSetRemoveValue(id set, id v) { if (set && v) RT_SEND1(void, set, "removeObject:", id, v); }
long iosrt_CFSetContainsValue(id set, id v) { return set && v && (RT_SEND1(long, set, "containsObject:", id, v) & 0xff); }
long iosrt_CFSetGetCount(id set) { return set ? (long)RT_SEND0(unsigned long, set, "count") : 0; }
char iosrt_kCFTypeSetCallBacks[48];
id iosrt_CFDataCreateWithBytesNoCopy(void *alloc, const void *b, long n, void *dealloc) { (void)alloc; (void)dealloc; return rt_data_new(b, (size_t)n); }
void iosrt_CFDataIncreaseLength(id d, long n) { if (d && n > 0) RT_SEND1(void, d, "increaseLengthBy:", unsigned long, (unsigned long)n); }
void iosrt_CFDataReplaceBytes(id d, CFRange r, const void *b, long n) {
    if (d) ((void (*)(id, SEL, NSRange, const void *, unsigned long))objc_msgSend)(d, rt_sel("replaceBytesInRange:withBytes:length:"),
        (NSRange){ (unsigned long)r.location, (unsigned long)r.length }, b, (unsigned long)n);
}
long iosrt_CFDictionaryGetCountOfKey(id d, id k) { return d && k && RT_SEND1(id, d, "objectForKey:", id, k) ? 1 : 0; }
struct sort_ctx { long (*cmp)(id, id, void *); void *ctx; };
static long sort_tramp(id a, id b, void *c) { struct sort_ctx *s = c; return s->cmp(a, b, s->ctx); }
void iosrt_CFArraySortValues(id a, CFRange r, void *cmp, void *ctx) {
    if (!a || !cmp || r.length < 2) return;
    struct sort_ctx sc = { (long (*)(id, id, void *))cmp, ctx };
    NSRange nr = { (unsigned long)r.location, (unsigned long)r.length };
    id sub = RT_SEND1(id, a, "subarrayWithRange:", NSRange, nr);
    id sorted = RT_SEND2(id, sub, "sortedArrayUsingFunction:context:", void *, (void *)sort_tramp, void *, &sc);
    RT_SEND2(void, a, "replaceObjectsInRange:withObjectsFromArray:", NSRange, nr, id, sorted);
}

/* --------------------------------------------------------- property lists */
id rt_bplist_write(id plist);
id iosrt_CFPropertyListCreateWithData(void *alloc, id data, unsigned long opts, long *fmt, id *err) {
    (void)alloc;
    if (err) *err = NULL;
    if (!data) return NULL;
    size_t n; const uint8_t *b = rt_data_bytes(data, &n);
    if (fmt) *fmt = n >= 8 && !memcmp(b, "bplist00", 8) ? 200 : 100;   /* binary / XML */
    id r = rt_plist_parse(b, n);
    if (r && opts) { id m = RT_SEND0(id, r, "mutableCopy"); objc_release(r); r = m; }
    return r;
}
id iosrt_CFPropertyListCreateFromXMLData(void *alloc, id data, unsigned long opts, id *errStr) { return iosrt_CFPropertyListCreateWithData(alloc, data, opts, NULL, errStr); }
id iosrt_CFPropertyListCreateData(void *alloc, id plist, long fmt, unsigned long opts, id *err) {
    (void)alloc; (void)fmt; (void)opts;
    if (err) *err = NULL;
    return plist ? retained(rt_bplist_write(plist)) : NULL;   /* binary format is readable wherever XML is */
}
id iosrt_CFPropertyListCreateXMLData(void *alloc, id plist) { return iosrt_CFPropertyListCreateData(alloc, plist, 100, 0, NULL); }
id iosrt_CFPropertyListCreateDeepCopy(void *alloc, id plist, unsigned long opts) {
    if (!plist) return NULL;
    id d = rt_bplist_write(plist);
    return iosrt_CFPropertyListCreateWithData(alloc, d, opts, NULL, NULL);
}
id iosrt_CFTimeZoneCopyDefault(void) { return retained(RT_SEND0(id, (id)cls("NSTimeZone"), "defaultTimeZone")); }

/* ---------------------------------------------- Foundation methods used above */
static rt_bool url_hasDirectoryPath(id self, SEL s) {
    (void)s; id str = RT_SEND0(id, self, "absoluteString");
    const char *u = str ? rt_str_utf8(str) : ""; size_t n = strlen(u);
    return n && u[n - 1] == '/';
}
static id url_baseURL(id self, SEL s) { (void)self; (void)s; return NULL; }
static void md_replaceBytes(id self, SEL s, NSRange r, const void *b, unsigned long n) {
    (void)s;
    size_t len; const uint8_t *cur = rt_data_bytes(self, &len);
    if (r.location > len) r.location = len;
    if (r.length > len - r.location) r.length = len - r.location;
    size_t nl = len - r.length + n;
    uint8_t *buf = rt_xmalloc(nl ? nl : 1);
    memcpy(buf, cur, r.location); if (n && b) memcpy(buf + r.location, b, n); else if (n) memset(buf + r.location, 0, n);
    memcpy(buf + r.location + n, cur + r.location + r.length, len - r.location - r.length);
    RT_SEND1(void, self, "setLength:", unsigned long, 0);
    RT_SEND2(void, self, "appendBytes:length:", const void *, buf, unsigned long, nl);
    free(buf);
}
static void ma_sortFunc(id self, SEL s, void *fn, void *ctx) {
    (void)s; id sorted = RT_SEND2(id, self, "sortedArrayUsingFunction:context:", void *, fn, void *, ctx);
    objc_retain(sorted);
    RT_SEND0(void, self, "removeAllObjects"); RT_SEND1(void, self, "addObjectsFromArray:", id, sorted);
    objc_release(sorted);
}
static void ma_replaceRange(id self, SEL s, NSRange r, id other) {
    (void)s;
    unsigned long n = RT_SEND0(unsigned long, other, "count");
    objc_retain(other);
    for (unsigned long i = 0; i < r.length; i++) RT_SEND1(void, self, "removeObjectAtIndex:", unsigned long, r.location);
    for (unsigned long i = 0; i < n; i++) RT_SEND2(void, self, "insertObject:atIndex:", id, RT_SEND1(id, other, "objectAtIndex:", unsigned long, i), unsigned long, r.location + i);
    objc_release(other);
}

void rt_cf2_init(void) {
    iosrt_kCFPreferencesAnyHost = rt_str_new("kCFPreferencesAnyHost");
    iosrt_kCFPreferencesCurrentHost = rt_str_new("kCFPreferencesCurrentHost");
    iosrt_kCFPreferencesCurrentUser = rt_str_new("kCFPreferencesCurrentUser");
    iosrt_kCFPreferencesAnyUser = rt_str_new("kCFPreferencesAnyUser");
    iosrt_kCFPreferencesCurrentApplication = rt_str_new("kCFPreferencesCurrentApplication");
    iosrt_kCFPreferencesAnyApplication = rt_str_new("kCFPreferencesAnyApplication");
    Class url = cls("NSURL");
    IM(url, "hasDirectoryPath", url_hasDirectoryPath); IM(url, "baseURL", url_baseURL);
    IM(cls("NSMutableData"), "replaceBytesInRange:withBytes:length:", md_replaceBytes);
    Class ma = cls("NSMutableArray");
    IM(ma, "sortUsingFunction:context:", ma_sortFunc); IM(ma, "replaceObjectsInRange:withObjectsFromArray:", ma_replaceRange);
}
