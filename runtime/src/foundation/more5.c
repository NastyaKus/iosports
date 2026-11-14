/*
 * Foundation HLE, part 8: additions found by tracing GarageBand —
 * NSString bytes-no-copy initialisers, class versions, NSDictionary file
 * initialisers, NSNotificationQueue, the App Store receipt URL and a few
 * CoreFoundation / libc odds.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSString);
RT_DECLARE_CLASS(NSDictionary);
RT_DECLARE_CLASS(NSMutableDictionary);
RT_DECLARE_CLASS(NSBundle);
RT_DEFINE_CLASS(NSNotificationQueue);

/* ---------------------------------------------------------- NSString */
/* the bytes are copied (our strings are UTF-16 internally); freeWhenDone: frees them now */
static id s_initNoCopy(id self, SEL s, void *b, unsigned long n, unsigned long enc, long freeWhenDone) {
    (void)s;
    id r = ((id (*)(id, SEL, const void *, unsigned long, unsigned long))objc_msgSend)(self, rt_sel("initWithBytes:length:encoding:"), b, n, enc);
    if ((freeWhenDone & 0xff) && b) free(b);
    return r;
}
static id s_initCharsNoCopy(id self, SEL s, unichar *u, unsigned long n, long freeWhenDone) {
    (void)s;
    id r = ((id (*)(id, SEL, const unichar *, unsigned long))objc_msgSend)(self, rt_sel("initWithCharacters:length:"), u, n);
    if ((freeWhenDone & 0xff) && u) free(u);
    return r;
}

/* ---------------------------------------------------- class versions */
static struct { Class c; long v; } *g_versions; static size_t g_nversions;
static pthread_mutex_t g_versions_lock = PTHREAD_MUTEX_INITIALIZER;
static void o_setVersion(Class self, SEL s, long v) {
    (void)s;
    pthread_mutex_lock(&g_versions_lock);
    size_t i = 0;
    while (i < g_nversions && g_versions[i].c != self) i++;
    if (i == g_nversions) { g_versions = realloc(g_versions, (g_nversions + 1) * sizeof *g_versions); g_versions[g_nversions++].c = self; }
    g_versions[i].v = v;
    pthread_mutex_unlock(&g_versions_lock);
}
static long o_version(Class self, SEL s) {
    (void)s; long v = 0;
    pthread_mutex_lock(&g_versions_lock);
    for (size_t i = 0; i < g_nversions; i++) if (g_versions[i].c == self) { v = g_versions[i].v; break; }
    pthread_mutex_unlock(&g_versions_lock);
    return v;
}

/* ------------------------------------------------------ NSDictionary */
static id d_initWithContentsOfFile(id self, SEL s, id path) {
    (void)s;
    Class c = self->isa;
    objc_release(self);
    id d = RT_SEND1(id, (id)c, "dictionaryWithContentsOfFile:", id, path);
    if (!d) return NULL;
    if (c == RT_CLASS(NSMutableDictionary) || rt_is_kind_of(d, c)) return objc_retain(d);
    return RT_SEND0(id, d, "copy");
}
static id d_initWithCapacity(id self, SEL s, unsigned long n) { (void)s; (void)n; return RT_SEND0(id, self, "init"); }

/* ------------------------------------------------ NSNotificationQueue */
/* coalescing is not modelled: notifications are posted right away (postingStyle NSPostNow)
 * or on the next run-loop turn (ASAP / when idle) */
struct nsnq { Class isa; id center; };
static id g_default_queue;
static id nq_default(Class c, SEL s) {
    (void)s;
    if (!g_default_queue) {
        g_default_queue = rt_alloc_instance(c, 0);
        ((struct nsnq *)g_default_queue)->center = objc_retain(RT_SEND0(id, (id)rt_class_named("NSNotificationCenter"), "defaultCenter"));
    }
    return g_default_queue;
}
static id nq_initWithCenter(id self, SEL s, id center) { (void)s; ((struct nsnq *)self)->center = objc_retain(center); return self; }
static void nq_enqueueModes(id self, SEL s, id note, long style, unsigned long coalesce, id modes) {
    (void)s; (void)coalesce; (void)modes;
    id center = ((struct nsnq *)self)->center;
    if (!center || !note) return;
    if (style == 3) RT_SEND1(void, center, "postNotification:", id, note);     /* NSPostNow */
    else ((void (*)(id, SEL, SEL, id, double))objc_msgSend)(center, rt_sel("performSelector:withObject:afterDelay:"), rt_sel("postNotification:"), note, 0.0);
}
static void nq_enqueue(id self, SEL s, id note, long style) { nq_enqueueModes(self, s, note, style, 0, NULL); }
static void nq_dequeue(id self, SEL s, id note, unsigned long coalesce) { (void)self; (void)s; (void)note; (void)coalesce; }

/* ---------------------------------------------------------- NSBundle */
/* where the receipt would be: <bundle>/StoreKit/receipt (it normally does not exist) */
static id b_receiptURL(id self, SEL s) {
    (void)s;
    id p = RT_SEND1(id, RT_SEND0(id, self, "bundlePath"), "stringByAppendingPathComponent:", id, rt_str("StoreKit/receipt"));
    return RT_SEND1(id, (id)rt_class_named("NSURL"), "fileURLWithPath:", id, p);
}

/* ------------------------------------------- replacing within a range */
/* options: 1 case-insensitive, 4 backwards (same result for replace-all), 8 anchored */
static unichar fold(unichar c) { return c < 128 && c >= 'A' && c <= 'Z' ? (unichar)(c + 32) : c; }
static id replaced(id self, id target, id repl, unsigned long opts, NSRange r, unsigned long *count) {
    size_t n, tn, rn; unichar *f1, *f2, *f3;
    const unichar *u = rt_str_utf16(self, &n, &f1), *t = rt_str_utf16(target, &tn, &f2), *w = rt_str_utf16(repl ? repl : rt_str(""), &rn, &f3);
    unsigned long cnt = 0;
    if (r.location > n) r.location = n;
    if (r.length > n - r.location) r.length = n - r.location;
    unichar *out = rt_xmalloc((n + 1) * 2 + (tn ? (n / tn + 1) : 1) * (rn + 1) * 2); size_t o = 0;
    memcpy(out, u, r.location * 2); o = r.location;
    size_t end = r.location + r.length, i = r.location;
    while (i < end) {
        int hit = tn && i + tn <= end;
        for (size_t k = 0; hit && k < tn; k++) if (opts & 1 ? fold(u[i + k]) != fold(t[k]) : u[i + k] != t[k]) hit = 0;
        if (hit && (opts & 8) && i != r.location) hit = 0;
        if (hit) { memcpy(out + o, w, rn * 2); o += rn; i += tn; cnt++; }
        else out[o++] = u[i++];
    }
    memcpy(out + o, u + end, (n - end) * 2); o += n - end;
    id res = rt_str_new_utf16(out, o);
    free(out); free(f1); free(f2); free(f3);
    if (count) *count = cnt;
    return res;
}
static unsigned long ms_replaceOccurrences(id self, SEL s, id target, id repl, unsigned long opts, NSRange r) {
    (void)s; unsigned long cnt = 0;
    if (!target) return 0;
    id res = replaced(self, target, repl, opts, r, &cnt);
    if (cnt) RT_SEND1(void, self, "setString:", id, res);
    objc_release(res);
    return cnt;
}
static id s_replacingOptsRange(id self, SEL s, id target, id repl, unsigned long opts, NSRange r) {
    (void)s; if (!target) return self;
    return objc_autorelease(replaced(self, target, repl, opts, r, NULL));
}
static id ms_initWithFormatLocaleArgs(id self, SEL s, id fmt, id locale, void *args) {
    (void)s; (void)locale;
    return ((id (*)(id, SEL, id, void *))objc_msgSend)(self, rt_sel("initWithFormat:arguments:"), fmt, args);
}

/* ------------------------------------------------ NSScanner + sets */
/* the scanner (more2.c) keeps a UTF-8 byte offset into its string */
struct nsscan { Class isa; id str; unsigned long loc; };
static unsigned utf8_next(const unsigned char *p, int *len) {
    unsigned c = p[0];
    if (c < 0x80) { *len = 1; return c; }
    if ((c & 0xe0) == 0xc0 && p[1]) { *len = 2; return (c & 0x1f) << 6 | (p[1] & 0x3f); }
    if ((c & 0xf0) == 0xe0 && p[1] && p[2]) { *len = 3; return (c & 0x0f) << 12 | (p[1] & 0x3f) << 6 | (p[2] & 0x3f); }
    if (p[1] && p[2] && p[3]) { *len = 4; return (c & 0x07) << 18 | (p[1] & 0x3f) << 12 | (p[2] & 0x3f) << 6 | (p[3] & 0x3f); }
    *len = 1; return c;
}
static int member(id set, unsigned ch) { return set && (RT_SEND1(long, set, "characterIsMember:", unsigned long, ch) & 0xff); }
/* scan characters that are (want=1) / are not (want=0) in the set, after skipping whitespace */
static rt_bool scan_set(id self, id set, int want, id *out) {
    struct nsscan *x = (struct nsscan *)self;
    const unsigned char *u = (const unsigned char *)rt_str_utf8(x->str);
    size_t n = strlen((const char *)u), i = x->loc < n ? x->loc : n;
    int len;
    while (i < n && (u[i] == ' ' || u[i] == '\t' || u[i] == '\n' || u[i] == '\r')) i++;   /* charactersToBeSkipped */
    size_t start = i;
    while (i < n) { unsigned c = utf8_next(u + i, &len); if (member(set, c) != want) break; i += (size_t)len; }
    if (i == start) { return 0; }
    if (out) { char *t = rt_xmalloc(i - start + 1); memcpy(t, u + start, i - start); t[i - start] = 0; *out = rt_str(t); free(t); }
    x->loc = i;
    return 1;
}
static rt_bool sc_scanSet(id self, SEL s, id set, id *out) { (void)s; return scan_set(self, set, 1, out); }
static rt_bool sc_scanUpToSet(id self, SEL s, id set, id *out) { (void)s; return scan_set(self, set, 0, out); }
static rt_bool sc_longlong(id self, SEL s, long long *v) { (void)s; long t; if (!(RT_SEND1(long, self, "scanInteger:", long *, &t) & 0xff)) return 0; if (v) *v = t; return 1; }
static id sc_string(id self, SEL s) { (void)s; return ((struct nsscan *)self)->str; }
static id sc_skipSet(id self, SEL s) { (void)self; (void)s; return RT_SEND0(id, (id)rt_class_named("NSCharacterSet"), "whitespaceAndNewlineCharacterSet"); }
static void sc_setSkip(id self, SEL s, id set) { (void)self; (void)s; (void)set; }
static void sc_setCase(id self, SEL s, long v) { (void)self; (void)s; (void)v; }
static rt_bool sc_case(id self, SEL s) { (void)self; (void)s; return 0; }
static id cs_illegal(Class c, SEL s) {
    (void)s;   /* non-characters U+FFFE/U+FFFF and the surrogate range */
    id m = RT_SEND1(id, (id)rt_class_named("NSMutableCharacterSet"), "characterSetWithRange:", NSRange, ((NSRange){ 0xd800, 0x800 }));
    RT_SEND1(void, m, "addCharactersInRange:", NSRange, ((NSRange){ 0xfffe, 2 }));
    (void)c; return m;
}
static id cs_symbols(Class c, SEL s) { (void)s; return RT_SEND1(id, (id)c, "characterSetWithCharactersInString:", id, rt_str("$+<=>^`|~")); }

/* ------------------------------------------- older file APIs, sets, index sets */
#include <sys/stat.h>
#include <limits.h>
static id fm_strWithFSRep(id self, SEL s, const char *p, unsigned long n) {
    (void)self; (void)s; if (!p) return NULL;
    return objc_autorelease(RT_SEND3(id, RT_SEND0(id, (id)RT_CLASS(NSString), "alloc"), "initWithBytes:length:encoding:", const void *, p, unsigned long, n, unsigned long, 4));
}
static rt_bool fm_createDirAttrs(id self, SEL s, id path, id attrs) {
    (void)self; (void)s; (void)attrs;
    const char *p = path ? rt_str_utf8(path) : NULL;
    if (!p) return 0;
    struct stat st;
    return mkdir(p, 0755) == 0 || (stat(p, &st) == 0 && S_ISDIR(st.st_mode));
}
static rt_bool s_writeToFileAtomically(id self, SEL s, id path, long atomically) {
    (void)s;
    return ((long (*)(id, SEL, id, long, unsigned long, id *))objc_msgSend)(self, rt_sel("writeToFile:atomically:encoding:error:"), path, atomically & 0xff, 4, NULL) & 0xff;
}
static id a_objectsAtIndexes(id self, SEL s, id idx) {
    (void)s; id out = RT_SEND0(id, (id)rt_class_named("NSMutableArray"), "array");
    unsigned long n = RT_SEND0(unsigned long, self, "count");
    for (unsigned long i = 0; idx && i < n; i++)
        if (RT_SEND1(long, idx, "containsIndex:", unsigned long, i) & 0xff) RT_SEND1(void, out, "addObject:", id, RT_SEND1(id, self, "objectAtIndex:", unsigned long, i));
    return out;
}
static rt_bool set_intersects(id self, SEL s, id other) {
    (void)s; id all = RT_SEND0(id, self, "allObjects");
    for (size_t i = 0; other && i < rt_array_count(all); i++) if (RT_SEND1(long, other, "containsObject:", id, rt_array_at(all, i)) & 0xff) return 1;
    return 0;
}
static void mset_minus(id self, SEL s, id other) {
    (void)s; id all = other ? RT_SEND0(id, other, "allObjects") : NULL;
    for (size_t i = 0; all && i < rt_array_count(all); i++) RT_SEND1(void, self, "removeObject:", id, rt_array_at(all, i));
}
static void mset_intersect(id self, SEL s, id other) {
    (void)s; id all = RT_SEND0(id, RT_SEND0(id, self, "allObjects"), "copy");
    for (size_t i = 0; i < rt_array_count(all); i++) {
        id o = rt_array_at(all, i);
        if (!other || !(RT_SEND1(long, other, "containsObject:", id, o) & 0xff)) RT_SEND1(void, self, "removeObject:", id, o);
    }
    objc_release(all);
}
static void mis_addIndexes(id self, SEL s, id other) {
    (void)s; if (!other) return;
    for (unsigned long i = RT_SEND0(unsigned long, other, "firstIndex"); i < (unsigned long)LONG_MAX; ) {
        RT_SEND1(void, self, "addIndex:", unsigned long, i);
        unsigned long nx = RT_SEND1(unsigned long, other, "indexGreaterThanIndex:", unsigned long, i);
        if (nx <= i) break;
        i = nx;
    }
}
static void mis_removeIndexes(id self, SEL s, id other) {
    (void)s; if (!other) return;
    for (unsigned long i = RT_SEND0(unsigned long, other, "firstIndex"); i < (unsigned long)LONG_MAX; ) {
        RT_SEND1(void, self, "removeIndex:", unsigned long, i);
        unsigned long nx = RT_SEND1(unsigned long, other, "indexGreaterThanIndex:", unsigned long, i);
        if (nx <= i) break;
        i = nx;
    }
}
/* NSLocaleLanguageDirectionLeftToRight; right-to-left for the usual RTL languages */
static long loc_charDirection(Class c, SEL s, id lang) {
    (void)c; (void)s; const char *l = lang ? rt_str_utf8(lang) : "";
    static const char *const rtl[] = { "ar", "he", "fa", "ur", "yi", "iw" };
    for (size_t i = 0; i < sizeof rtl / sizeof *rtl; i++) if (!strncmp(l, rtl[i], 2)) return 2;
    return 1;
}

/* ------------------------------------------------- C functions */
id iosrt_NSOpenStepRootDirectory(void) { return rt_str("/"); }
id iosrt_CFStringCreateWithFileSystemRepresentation(void *alloc, const char *buf) {
    (void)alloc; return buf ? rt_str_new(buf) : NULL;
}
void iosrt_tzset(void) { tzset(); }

void rt_foundation_more5_init(void) {
    Class st = RT_CLASS(NSString);
    IM(st, "writeToFile:atomically:", s_writeToFileAtomically);
    Class fm = rt_class_named("NSFileManager");
    IM(fm, "stringWithFileSystemRepresentation:length:", fm_strWithFSRep);
    IM(fm, "createDirectoryAtPath:attributes:", fm_createDirAttrs);
    IM(rt_class_named("NSArray"), "objectsAtIndexes:", a_objectsAtIndexes);
    IM(rt_class_named("NSSet"), "intersectsSet:", set_intersects);
    IM(rt_class_named("NSMutableSet"), "minusSet:", mset_minus);
    IM(rt_class_named("NSMutableSet"), "intersectSet:", mset_intersect);
    IM(rt_class_named("NSMutableIndexSet"), "addIndexes:", mis_addIndexes);
    IM(rt_class_named("NSMutableIndexSet"), "removeIndexes:", mis_removeIndexes);
    CM(rt_class_named("NSLocale"), "characterDirectionForLanguage:", loc_charDirection);
    CM(rt_class_named("NSLocale"), "lineDirectionForLanguage:", loc_charDirection);
    IM(st, "initWithBytesNoCopy:length:encoding:freeWhenDone:", s_initNoCopy);
    IM(st, "initWithCharactersNoCopy:length:freeWhenDone:", s_initCharsNoCopy);
    Class o = RT_CLASS(NSObject);
    CM(o, "setVersion:", o_setVersion); CM(o, "version", o_version);
    Class d = RT_CLASS(NSDictionary);
    IM(d, "initWithContentsOfFile:", d_initWithContentsOfFile);
    IM(RT_CLASS(NSMutableDictionary), "initWithCapacity:", d_initWithCapacity);
    rt_native_class(RT_CLASS(NSNotificationQueue), &iosrt_meta_NSNotificationQueue, "NSNotificationQueue", o, sizeof(struct nsnq) - sizeof(void *));
    Class nq = RT_CLASS(NSNotificationQueue);
    CM(nq, "defaultQueue", nq_default); IM(nq, "initWithNotificationCenter:", nq_initWithCenter);
    IM(nq, "enqueueNotification:postingStyle:", nq_enqueue);
    IM(nq, "enqueueNotification:postingStyle:coalesceMask:forModes:", nq_enqueueModes);
    IM(nq, "dequeueNotificationsMatching:coalesceMask:", nq_dequeue);
    IM(RT_CLASS(NSBundle), "appStoreReceiptURL", b_receiptURL);
    Class ms = rt_class_named("NSMutableString");
    IM(ms, "replaceOccurrencesOfString:withString:options:range:", ms_replaceOccurrences);
    IM(ms, "initWithFormat:locale:arguments:", ms_initWithFormatLocaleArgs);
    IM(st, "initWithFormat:locale:arguments:", ms_initWithFormatLocaleArgs);
    IM(st, "stringByReplacingOccurrencesOfString:withString:options:range:", s_replacingOptsRange);
    Class sc = rt_class_named("NSScanner");
    IM(sc, "scanCharactersFromSet:intoString:", sc_scanSet); IM(sc, "scanUpToCharactersFromSet:intoString:", sc_scanUpToSet);
    IM(sc, "scanLongLong:", sc_longlong); IM(sc, "string", sc_string);
    IM(sc, "charactersToBeSkipped", sc_skipSet); IM(sc, "setCharactersToBeSkipped:", sc_setSkip);
    IM(sc, "caseSensitive", sc_case); IM(sc, "setCaseSensitive:", sc_setCase);
    Class cs = rt_class_named("NSCharacterSet");
    CM(cs, "illegalCharacterSet", cs_illegal); CM(cs, "symbolCharacterSet", cs_symbols);
}

/* CFPreferencesSynchronize(appID, user, host): the defaults are written as they change */
long iosrt_CFPreferencesSynchronize(id app, id user, id host) { (void)app; (void)user; (void)host; return 1; }
