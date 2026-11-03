/*
 * CoreFoundation HLE: CF types are the runtime's Foundation objects (toll-free
 * bridging), so these functions are thin wrappers over the ObjC classes.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

RT_DECLARE_CLASS(NSNumber);
RT_DECLARE_CLASS(NSString);
RT_DECLARE_CLASS(NSMutableString);
RT_DECLARE_CLASS(NSArray);
RT_DECLARE_CLASS(NSMutableArray);
RT_DECLARE_CLASS(NSDictionary);
RT_DECLARE_CLASS(NSMutableDictionary);
RT_DECLARE_CLASS(NSData);
RT_DECLARE_CLASS(NSMutableData);
RT_DECLARE_CLASS(NSURL);
RT_DECLARE_CLASS(NSUUID);
RT_DECLARE_CLASS(NSDate);
RT_DECLARE_CLASS(NSBundle);
RT_DECLARE_CLASS(NSUserDefaults);
RT_DECLARE_CLASS(NSNull);
id rt_dict_new(id *vals, id *keys, size_t n);
void rt_dict_set(id dict, id key, id val);
id rt_data_new(const void *b, size_t n);
const void *rt_data_bytes(id d, size_t *n);

/* ------------------------------------------------------------- type ids */
enum { TID_STRING = 7, TID_NUMBER = 22, TID_BOOLEAN = 21, TID_ARRAY = 19, TID_DICTIONARY = 18, TID_DATA = 20, TID_DATE = 42, TID_URL = 29, TID_NULL = 16, TID_UUID = 41 };
unsigned long iosrt_CFGetTypeID(id o) {
    if (!o) return 0;
    if (rt_is_string(o)) return TID_STRING;
    if (rt_is_kind_of(o, RT_CLASS(NSNumber))) { const char *t = RT_SEND0(const char *, o, "objCType"); return t && t[0] == 'c' ? TID_BOOLEAN : TID_NUMBER; }
    if (rt_is_kind_of(o, RT_CLASS(NSArray))) return TID_ARRAY;
    if (rt_is_kind_of(o, RT_CLASS(NSDictionary))) return TID_DICTIONARY;
    if (rt_is_kind_of(o, RT_CLASS(NSData))) return TID_DATA;
    if (rt_is_kind_of(o, RT_CLASS(NSDate))) return TID_DATE;
    if (rt_is_kind_of(o, RT_CLASS(NSURL))) return TID_URL;
    if (rt_is_kind_of(o, RT_CLASS(NSNull))) return TID_NULL;
    return 1;
}
#define TYPEID(name, v) unsigned long iosrt_##name(void) { return v; }
TYPEID(CFStringGetTypeID, TID_STRING) TYPEID(CFNumberGetTypeID, TID_NUMBER) TYPEID(CFBooleanGetTypeID, TID_BOOLEAN)
TYPEID(CFArrayGetTypeID, TID_ARRAY) TYPEID(CFDictionaryGetTypeID, TID_DICTIONARY) TYPEID(CFDataGetTypeID, TID_DATA)
TYPEID(CFDateGetTypeID, TID_DATE) TYPEID(CFURLGetTypeID, TID_URL) TYPEID(CFNullGetTypeID, TID_NULL) TYPEID(CFUUIDGetTypeID, TID_UUID)

/* --------------------------------------------------------------- numbers */
/* CFNumberType: 1 SInt8 2 SInt16 3 SInt32 4 SInt64 5 Float32 6 Float64 7 char 8 short 9 int 10 long 11 longlong
 * 12 float 13 double 14 CFIndex 15 NSInteger 16 CGFloat */
static int cfnum_is_float(long t) { return t == 5 || t == 6 || t == 12 || t == 13 || t == 16; }
id iosrt_CFNumberCreate(const void *alloc, long type, const void *p) {
    (void)alloc; id n;
    switch (type) {
    case 1: case 7: n = rt_num_int(*(const int8_t *)p); break;
    case 2: case 8: n = rt_num_int(*(const int16_t *)p); break;
    case 3: case 9: n = rt_num_int(*(const int32_t *)p); break;
    case 5: case 12: n = rt_num_dbl(*(const float *)p); break;
    case 6: case 13: case 16: n = rt_num_dbl(*(const double *)p); break;
    default: n = rt_num_int(*(const int64_t *)p); break;
    }
    return objc_retain(n);
}
rt_bool iosrt_CFNumberGetValue(id n, long type, void *p) {
    if (!n) return 0;
    long long iv = RT_SEND0(long long, n, "longLongValue");
    double dv = RT_SEND0(double, n, "doubleValue");
    switch (type) {
    case 1: case 7: *(int8_t *)p = (int8_t)iv; break;
    case 2: case 8: *(int16_t *)p = (int16_t)iv; break;
    case 3: case 9: *(int32_t *)p = (int32_t)iv; break;
    case 5: case 12: *(float *)p = (float)dv; break;
    case 6: case 13: case 16: *(double *)p = dv; break;
    default: *(int64_t *)p = iv; break;
    }
    return 1;
}
long iosrt_CFNumberGetType(id n) { const char *t = n ? RT_SEND0(const char *, n, "objCType") : "q"; return t[0] == 'd' ? 13 : t[0] == 'f' ? 12 : 4; }
rt_bool iosrt_CFNumberIsFloatType(id n) { return cfnum_is_float(iosrt_CFNumberGetType(n)); }
long iosrt_CFNumberGetByteSize(id n) { return iosrt_CFNumberGetType(n) == 12 ? 4 : 8; }
long iosrt_CFNumberCompare(id a, id b, void *ctx) { (void)ctx; return RT_SEND1(long, a, "compare:", id, b); }
id iosrt_kCFBooleanTrue, iosrt_kCFBooleanFalse, iosrt_kCFNull;
rt_bool iosrt_CFBooleanGetValue(id b) { return b && (RT_SEND0(long, b, "boolValue") & 0xff) != 0; }

/* --------------------------------------------------------------- strings */
id iosrt_CFStringCreateWithCStringNoCopy(const void *alloc, const char *s, uint32_t enc, const void *dealloc) { (void)alloc; (void)enc; (void)dealloc; return s ? rt_str_new(s) : NULL; }
id iosrt_CFStringCreateWithBytes(const void *alloc, const uint8_t *b, long n, uint32_t enc, long bom) {
    (void)alloc; (void)bom;
    id s = rt_alloc_instance(RT_CLASS(NSString), 0);
    unsigned long nsenc = enc == 0x08000100 ? 4 : enc == 0x0600 ? 1 : enc == 0x0100 || enc == 0x10000100 ? 10 : 4;
    return ((id (*)(id, SEL, const void *, unsigned long, unsigned long))objc_msgSend)(s, rt_sel("initWithBytes:length:encoding:"), b, (unsigned long)n, nsenc);
}
id iosrt_CFStringCreateWithCharacters(const void *alloc, const unichar *u, long n) { (void)alloc; return rt_str_new_utf16(u, (size_t)n); }
id iosrt_CFStringCreateCopy(const void *alloc, id s) { (void)alloc; return s ? RT_SEND0(id, s, "copy") : NULL; }
id iosrt_CFStringCreateMutable(const void *alloc, long max) { (void)alloc; (void)max; return RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableString), "alloc"), "init"); }
id iosrt_CFStringCreateMutableCopy(const void *alloc, long max, id s) { (void)alloc; (void)max; return s ? RT_SEND0(id, s, "mutableCopy") : iosrt_CFStringCreateMutable(alloc, 0); }
void iosrt_CFStringAppend(id m, id s) { if (m && s) RT_SEND1(void, m, "appendString:", id, s); }
void iosrt_CFStringAppendCharacters(id m, const unichar *chars, long n) {
    if (!m || !chars || n <= 0) return;
    id s = rt_str_new_utf16(chars, (size_t)n); RT_SEND1(void, m, "appendString:", id, s); objc_release(s);
}
/* in-place case mapping of a mutable string (the locale argument is ignored) */
void iosrt_CFStringLowercase(id m, id locale) { (void)locale; if (m) RT_SEND1(void, m, "setString:", id, RT_SEND0(id, m, "lowercaseString")); }
void iosrt_CFStringUppercase(id m, id locale) { (void)locale; if (m) RT_SEND1(void, m, "setString:", id, RT_SEND0(id, m, "uppercaseString")); }
void iosrt_CFStringCapitalize(id m, id locale) { (void)locale; if (m) RT_SEND1(void, m, "setString:", id, RT_SEND0(id, m, "capitalizedString")); }
/* Unicode normalization: strings are kept as given (most app text is already NFC) */
void iosrt_CFStringNormalize(id m, long form) { (void)m; (void)form; }
void iosrt_CFStringAppendCString(id m, const char *s, uint32_t enc) { (void)enc; if (m && s) RT_SEND1(void, m, "appendString:", id, rt_str(s)); }
unichar iosrt_CFStringGetCharacterAtIndex(id s, long i) { return (unichar)RT_SEND1(unsigned long, s, "characterAtIndex:", unsigned long, (unsigned long)i); }
void iosrt_CFStringGetCharacters(id s, NSRange r, unichar *buf) { RT_SEND2(void, s, "getCharacters:range:", unichar *, buf, NSRange, r); }
long iosrt_CFStringCompare(id a, id b, unsigned long opts) { return (opts & 1) ? RT_SEND1(long, a, "caseInsensitiveCompare:", id, b) : RT_SEND1(long, a, "compare:", id, b); }
rt_bool iosrt_CFStringHasPrefix(id a, id b) { return RT_SEND1(long, a, "hasPrefix:", id, b) & 0xff; }
rt_bool iosrt_CFStringHasSuffix(id a, id b) { return RT_SEND1(long, a, "hasSuffix:", id, b) & 0xff; }
long iosrt_CFStringGetMaximumSizeForEncoding(long len, uint32_t enc) { (void)enc; return len * 4; }
long iosrt_CFStringGetBytes(id s, NSRange r, uint32_t enc, uint8_t loss, long ext, uint8_t *buf, long max, long *used) {
    (void)enc; (void)loss; (void)ext;
    id sub = RT_SEND1(id, s, "substringWithRange:", NSRange, r);
    const char *u = rt_str_utf8(sub); long n = (long)strlen(u);
    if (buf) { if (n > max) n = max; memcpy(buf, u, (size_t)n); }
    if (used) *used = n;
    return (long)r.length;
}
uint32_t iosrt_CFStringGetSystemEncoding(void) { return 0x08000100; }
id iosrt_CFStringCreateArrayBySeparatingStrings(const void *alloc, id s, id sep) { (void)alloc; return objc_retain(RT_SEND1(id, s, "componentsSeparatedByString:", id, sep)); }
double iosrt_CFStringGetDoubleValue(id s) { return s ? RT_SEND0(double, s, "doubleValue") : 0; }
int iosrt_CFStringGetIntValue(id s) { return s ? (int)RT_SEND0(long, s, "integerValue") : 0; }
id iosrt_CFStringCreateWithSubstring(const void *alloc, id s, NSRange r) { (void)alloc; return objc_retain(RT_SEND1(id, s, "substringWithRange:", NSRange, r)); }
id iosrt_CFCopyTypeIDDescription(unsigned long t) { (void)t; return rt_str_new("CFType"); }

/* ------------------------------------------------- arrays and dictionaries */
id iosrt_CFArrayCreate(const void *alloc, const void **vals, long n, const void *cb) { (void)alloc; (void)cb; return rt_array_new((id *)vals, (size_t)n); }
id iosrt_CFArrayCreateMutable(const void *alloc, long cap, const void *cb) { (void)alloc; (void)cap; (void)cb; return RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "alloc"), "init"); }
id iosrt_CFArrayCreateMutableCopy(const void *alloc, long cap, id a) { (void)alloc; (void)cap; return a ? RT_SEND0(id, a, "mutableCopy") : iosrt_CFArrayCreateMutable(alloc, 0, NULL); }
id iosrt_CFArrayCreateCopy(const void *alloc, id a) { (void)alloc; return a ? RT_SEND0(id, a, "copy") : NULL; }
void iosrt_CFArrayAppendValue(id a, const void *v) { if (a && v) RT_SEND1(void, a, "addObject:", id, (id)v); }
void iosrt_CFArrayRemoveValueAtIndex(id a, long i) { RT_SEND1(void, a, "removeObjectAtIndex:", unsigned long, (unsigned long)i); }
void iosrt_CFArrayRemoveAllValues(id a) { RT_SEND0(void, a, "removeAllObjects"); }
void iosrt_CFArrayInsertValueAtIndex(id a, long i, const void *v) { RT_SEND2(void, a, "insertObject:atIndex:", id, (id)v, unsigned long, (unsigned long)i); }
void iosrt_CFArraySetValueAtIndex(id a, long i, const void *v) { RT_SEND2(void, a, "replaceObjectAtIndex:withObject:", unsigned long, (unsigned long)i, id, (id)v); }
rt_bool iosrt_CFArrayContainsValue(id a, NSRange r, const void *v) { (void)r; return a && (RT_SEND1(long, a, "containsObject:", id, (id)v) & 0xff); }
void iosrt_CFArrayGetValues(id a, NSRange r, const void **out) { for (unsigned long i = 0; i < r.length; i++) out[i] = rt_array_at(a, r.location + i); }
id iosrt_CFDictionaryCreate(const void *alloc, const void **keys, const void **vals, long n, const void *kcb, const void *vcb) {
    (void)alloc; (void)kcb; (void)vcb; return rt_dict_new((id *)vals, (id *)keys, (size_t)n);
}
id iosrt_CFDictionaryCreateMutable(const void *alloc, long cap, const void *kcb, const void *vcb) { (void)alloc; (void)cap; (void)kcb; (void)vcb; return rt_dict_new(NULL, NULL, 0); }
id iosrt_CFDictionaryCreateMutableCopy(const void *alloc, long cap, id d) { (void)alloc; (void)cap; return d ? RT_SEND0(id, d, "mutableCopy") : rt_dict_new(NULL, NULL, 0); }
id iosrt_CFDictionaryCreateCopy(const void *alloc, id d) { (void)alloc; return d ? RT_SEND0(id, d, "copy") : NULL; }
void iosrt_CFDictionarySetValue(id d, const void *k, const void *v) { if (d && k && v) rt_dict_set(d, (id)k, (id)v); }
void iosrt_CFDictionaryAddValue(id d, const void *k, const void *v) { if (d && k && v && !rt_dict_get(d, (id)k)) rt_dict_set(d, (id)k, (id)v); }
void iosrt_CFDictionaryRemoveValue(id d, const void *k) { if (d && k) RT_SEND1(void, d, "removeObjectForKey:", id, (id)k); }
void iosrt_CFDictionaryRemoveAllValues(id d) { RT_SEND0(void, d, "removeAllObjects"); }
long iosrt_CFDictionaryGetCount(id d) { return d ? RT_SEND0(long, d, "count") : 0; }
rt_bool iosrt_CFDictionaryContainsKey(id d, const void *k) { return d && k && rt_dict_get(d, (id)k) != NULL; }
rt_bool iosrt_CFDictionaryGetValueIfPresent(id d, const void *k, const void **v) { id r = d && k ? rt_dict_get(d, (id)k) : NULL; if (v) *v = r; return r != NULL; }
void iosrt_CFDictionaryGetKeysAndValues(id d, const void **keys, const void **vals) {
    id ks = RT_SEND0(id, d, "allKeys");
    for (size_t i = 0; i < rt_array_count(ks); i++) { id k = rt_array_at(ks, i); if (keys) keys[i] = k; if (vals) vals[i] = rt_dict_get(d, k); }
}
void iosrt_CFDictionaryApplyFunction(id d, void (*fn)(const void *, const void *, void *), void *ctx) {
    id ks = RT_SEND0(id, d, "allKeys");
    for (size_t i = 0; i < rt_array_count(ks); i++) { id k = rt_array_at(ks, i); fn(k, rt_dict_get(d, k), ctx); }
}
/* the callback structs are only compared by address */
char iosrt_kCFTypeArrayCallBacks[48], iosrt_kCFTypeDictionaryKeyCallBacks[48], iosrt_kCFTypeDictionaryValueCallBacks[48],
     iosrt_kCFCopyStringDictionaryKeyCallBacks[48];

/* ------------------------------------------------------------------- data */
id iosrt_CFDataCreate(const void *alloc, const void *b, long n) { (void)alloc; return rt_data_new(b, (size_t)n); }
id iosrt_CFDataCreateMutable(const void *alloc, long cap) { (void)alloc; (void)cap; return RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableData), "alloc"), "init"); }
const void *iosrt_CFDataGetBytePtr(id d) { return d ? rt_data_bytes(d, NULL) : NULL; }
void *iosrt_CFDataGetMutableBytePtr(id d) { return d ? RT_SEND0(void *, d, "mutableBytes") : NULL; }
long iosrt_CFDataGetLength(id d) { size_t n = 0; if (d) rt_data_bytes(d, &n); return (long)n; }
void iosrt_CFDataAppendBytes(id d, const void *b, long n) { RT_SEND2(void, d, "appendBytes:length:", const void *, b, unsigned long, (unsigned long)n); }
void iosrt_CFDataSetLength(id d, long n) { RT_SEND1(void, d, "setLength:", unsigned long, (unsigned long)n); }
void iosrt_CFDataGetBytes(id d, NSRange r, uint8_t *out) { const uint8_t *b = rt_data_bytes(d, NULL); memcpy(out, b + r.location, r.length); }

/* -------------------------------------------------------------- URL & misc */
id iosrt_CFURLCreateWithFileSystemPath(const void *alloc, id path, long style, long dir) { (void)alloc; (void)style; (void)dir; return objc_retain(RT_SEND1(id, (id)RT_CLASS(NSURL), "fileURLWithPath:", id, path)); }
id iosrt_CFURLCreateWithString(const void *alloc, id s, id base) { (void)alloc; (void)base; return objc_retain(RT_SEND1(id, (id)RT_CLASS(NSURL), "URLWithString:", id, s)); }
rt_bool iosrt_CFURLGetFileSystemRepresentation(id url, long resolve, uint8_t *buf, long max) {
    (void)resolve; if (!url) return 0; const char *p = rt_str_utf8(RT_SEND0(id, url, "path")); size_t n = strlen(p);
    if ((long)n + 1 > max) return 0; memcpy(buf, p, n + 1); return 1;
}
id iosrt_CFURLCopyFileSystemPath(id url, long style) { (void)style; return objc_retain(RT_SEND0(id, url, "path")); }
id iosrt_CFURLGetString(id url) { return RT_SEND0(id, url, "absoluteString"); }
id iosrt_CFURLCopyPathExtension(id url) { return objc_retain(RT_SEND0(id, url, "pathExtension")); }
id iosrt_CFURLCreateStringByAddingPercentEscapes(const void *alloc, id s, id leave, id escape, uint32_t enc) {
    (void)alloc; (void)leave; (void)escape; (void)enc; return objc_retain(RT_SEND1(id, s, "stringByAddingPercentEscapesUsingEncoding:", unsigned long, 4));
}
id iosrt_CFUUIDCreate(const void *alloc) { (void)alloc; return RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSUUID), "alloc"), "init"); }
id iosrt_CFUUIDCreateString(const void *alloc, id u) { (void)alloc; return objc_retain(RT_SEND0(id, u, "UUIDString")); }
id iosrt_CFPreferencesCopyAppValue(id key, id app) { (void)app; id ud = RT_SEND0(id, (id)RT_CLASS(NSUserDefaults), "standardUserDefaults"); return objc_retain(RT_SEND1(id, ud, "objectForKey:", id, key)); }
void iosrt_CFPreferencesSetAppValue(id key, id val, id app) { (void)app; id ud = RT_SEND0(id, (id)RT_CLASS(NSUserDefaults), "standardUserDefaults"); RT_SEND2(void, ud, "setObject:forKey:", id, val, id, key); }
rt_bool iosrt_CFPreferencesAppSynchronize(id app) { (void)app; return 1; }
id iosrt_CFBundleGetValueForInfoDictionaryKey(id bundle, id key) { return RT_SEND1(id, bundle, "objectForInfoDictionaryKey:", id, key); }
id iosrt_CFBundleGetIdentifier(id bundle) { return RT_SEND0(id, bundle, "bundleIdentifier"); }
id iosrt_CFBundleCopyResourceURL(id bundle, id name, id type, id sub) { (void)sub; return objc_retain(RT_SEND2(id, bundle, "URLForResource:withExtension:", id, name, id, type)); }
id iosrt_CFBundleCopyBundleURL(id bundle) { return objc_retain(RT_SEND0(id, bundle, "bundleURL")); }
id iosrt_CFBundleGetInfoDictionary(id bundle) { return RT_SEND0(id, bundle, "infoDictionary"); }
id iosrt_CFLocaleCopyCurrent(void) { return objc_retain(RT_SEND0(id, (id)rt_class_named("NSLocale"), "currentLocale")); }
id iosrt_CFLocaleGetValue(id loc, id key) { return RT_SEND1(id, loc, "objectForKey:", id, key); }
id iosrt_CFLocaleCopyPreferredLanguages(void) { return objc_retain(RT_SEND0(id, (id)rt_class_named("NSLocale"), "preferredLanguages")); }
id iosrt_CFTimeZoneCopySystem(void) { return objc_retain(RT_SEND0(id, (id)rt_class_named("NSTimeZone"), "systemTimeZone")); }
double iosrt_CFTimeZoneGetSecondsFromGMT(id tz, double at) { (void)at; return (double)RT_SEND0(long, tz, "secondsFromGMT"); }
id iosrt_CFDateCreate(const void *alloc, double t) { (void)alloc; return objc_retain(RT_SEND1(id, (id)RT_CLASS(NSDate), "dateWithTimeIntervalSinceReferenceDate:", double, t)); }
double iosrt_CFDateGetAbsoluteTime(id d) { return RT_SEND0(double, d, "timeIntervalSinceReferenceDate"); }
void iosrt_CFRunLoopAddSource(void *rl, void *src, id mode) { (void)rl; (void)src; (void)mode; }
void iosrt_CFRunLoopRemoveSource(void *rl, void *src, id mode) { (void)rl; (void)src; (void)mode; }
void *iosrt_CFRunLoopObserverCreate(const void *a, unsigned long act, long rep, long ord, void *cb, void *ctx) { (void)a; (void)act; (void)rep; (void)ord; (void)cb; (void)ctx; return NULL; }
void iosrt_CFRunLoopAddObserver(void *rl, void *o, id mode) { (void)rl; (void)o; (void)mode; }
uint32_t iosrt_CFStringConvertNSStringEncodingToEncoding(unsigned long e) { return e == 4 ? 0x08000100 : e == 1 ? 0x0600 : 0x08000100; }
uint32_t iosrt_CFStringConvertEncodingToWindowsCodepage(uint32_t e) { return e == 0x0600 ? 20127 : e == 0x0201 ? 1252 : e == 0x0500 ? 1252 : 65001; }
uint32_t iosrt_CFStringConvertWindowsCodepageToEncoding(uint32_t cp) { return cp == 1252 ? 0x0500 : cp == 20127 ? 0x0600 : 0x08000100; }
unsigned long iosrt_CFStringConvertEncodingToNSStringEncoding(uint32_t e) { return e == 0x0600 ? 1 : 4; }

void rt_cf_init(void) {
    iosrt_kCFBooleanTrue = objc_retain(RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithBool:", long, 1));
    iosrt_kCFBooleanFalse = objc_retain(RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithBool:", long, 0));
    iosrt_kCFNull = RT_SEND0(id, (id)RT_CLASS(NSNull), "null");
}

struct cfuuid_bytes { uint8_t b[16]; };
struct cfuuid_bytes iosrt_CFUUIDGetUUIDBytes(id u) { struct cfuuid_bytes r; memset(&r, 0, sizeof r); if (u) RT_SEND1(void, u, "getUUIDBytes:", uint8_t *, r.b); return r; }

/* ------------------------------------------------- CFNotificationCenter
 * Local and Darwin centers share one in-process registry (there are no other
 * processes to hear Darwin notifications); posts reach C callbacks
 * registered here. */
typedef void (*cfnote_cb)(void *center, void *observer, id name, const void *object, id userInfo);
struct cfnote_obs { void *observer; cfnote_cb cb; id name; const void *object; struct cfnote_obs *next; };
static struct cfnote_obs *g_cfnotes;
static pthread_mutex_t g_cfnote_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_local_center, g_darwin_center;
void *iosrt_CFNotificationCenterGetLocalCenter(void) { return &g_local_center; }
void *iosrt_CFNotificationCenterGetDarwinNotifyCenter(void) { return &g_darwin_center; }
void *iosrt_CFNotificationCenterGetDistributedCenter(void) { return &g_darwin_center; }
void iosrt_CFNotificationCenterAddObserver(void *center, void *observer, cfnote_cb cb, id name, const void *object, long behavior) {
    (void)center; (void)behavior;
    if (!cb) return;
    struct cfnote_obs *o = calloc(1, sizeof *o);
    o->observer = observer; o->cb = cb; o->name = name ? RT_SEND0(id, name, "copy") : NULL; o->object = object;
    pthread_mutex_lock(&g_cfnote_lock); o->next = g_cfnotes; g_cfnotes = o; pthread_mutex_unlock(&g_cfnote_lock);
}
static void cfnote_remove(void *observer, id name, const void *object, int all) {
    pthread_mutex_lock(&g_cfnote_lock);
    for (struct cfnote_obs **pp = &g_cfnotes; *pp;) {
        struct cfnote_obs *o = *pp;
        int match = o->observer == observer && (all || ((!name || (o->name && (RT_SEND1(long, o->name, "isEqualToString:", id, name) & 0xff))) && (!object || o->object == object)));
        if (match) { *pp = o->next; objc_release(o->name); free(o); } else pp = &o->next;
    }
    pthread_mutex_unlock(&g_cfnote_lock);
}
void iosrt_CFNotificationCenterRemoveObserver(void *center, void *observer, id name, const void *object) { (void)center; cfnote_remove(observer, name, object, 0); }
void iosrt_CFNotificationCenterRemoveEveryObserver(void *center, void *observer) { (void)center; cfnote_remove(observer, NULL, NULL, 1); }
void iosrt_CFNotificationCenterPostNotification(void *center, id name, const void *object, id userInfo, long deliverImmediately) {
    (void)deliverImmediately;
    /* snapshot matching observers, then call outside the lock */
    struct cfnote_obs *hits[64]; int n = 0;
    pthread_mutex_lock(&g_cfnote_lock);
    for (struct cfnote_obs *o = g_cfnotes; o && n < 64; o = o->next)
        if ((!o->name || (name && (RT_SEND1(long, o->name, "isEqualToString:", id, name) & 0xff))) && (!o->object || o->object == object)) hits[n++] = o;
    struct { void *observer; cfnote_cb cb; } calls[64];
    for (int i = 0; i < n; i++) { calls[i].observer = hits[i]->observer; calls[i].cb = hits[i]->cb; }
    pthread_mutex_unlock(&g_cfnote_lock);
    for (int i = 0; i < n; i++) calls[i].cb(center, calls[i].observer, name, object, userInfo);
}
void iosrt_CFNotificationCenterPostNotificationWithOptions(void *center, id name, const void *object, id userInfo, unsigned long opts) {
    iosrt_CFNotificationCenterPostNotification(center, name, object, userInfo, (long)(opts & 1));
}
