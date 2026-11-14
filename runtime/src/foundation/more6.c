/*
 * Foundation HLE, part 9: what creating a GarageBand song needs —
 * NSUndoManager (registration bookkeeping, no undo stack yet), NSHashTable,
 * set/dictionary bulk operations, lossy string encoding, CF encodings list,
 * CFUUID from bytes and CMTime construction.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")
#define NEWCLASS(N, SUPER, SZ) rt_native_class(RT_CLASS(N), &iosrt_meta_##N, #N, SUPER, SZ)
id rt_data_new(const void *b, size_t n);

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSMutableSet);
RT_DEFINE_CLASS(NSUndoManager);
RT_DEFINE_CLASS(NSHashTable);

/* ---------------------------------------------------------- NSUndoManager */
struct undo { Class isa; long levels, disabled, grouping, groupsByEvent; id actionName; };
#define UM(x) ((struct undo *)(x))
static id um_init(id self, SEL s) { (void)s; UM(self)->groupsByEvent = 1; return self; }
static void um_dealloc(id self, SEL s) {
    objc_release(UM(self)->actionName);
    struct { id r; Class c; } sup = { self, RT_CLASS(NSUndoManager) };
    ((void (*)(void *, SEL))objc_msgSendSuper2)(&sup, s);
}
static void um_setLevels(id self, SEL s, long n) { (void)s; UM(self)->levels = n; }
static long um_levels(id self, SEL s) { (void)s; return UM(self)->levels; }
static void um_disable(id self, SEL s) { (void)s; UM(self)->disabled++; }
static void um_enable(id self, SEL s) { (void)s; if (UM(self)->disabled > 0) UM(self)->disabled--; }
static rt_bool um_isEnabled(id self, SEL s) { (void)s; return UM(self)->disabled == 0; }
static void um_begin(id self, SEL s) { (void)s; UM(self)->grouping++; }
static void um_end(id self, SEL s) { (void)s; if (UM(self)->grouping > 0) UM(self)->grouping--; }
static long um_groupingLevel(id self, SEL s) { (void)s; return UM(self)->grouping; }
static void um_setGroupsByEvent(id self, SEL s, long v) { (void)s; UM(self)->groupsByEvent = v & 0xff; }
static rt_bool um_groupsByEvent(id self, SEL s) { (void)s; return UM(self)->groupsByEvent != 0; }
static rt_bool um_no(id self, SEL s) { (void)self; (void)s; return 0; }
static void um_noop(id self, SEL s) { (void)self; (void)s; }
static void um_noop1(id self, SEL s, id a) { (void)self; (void)s; (void)a; }
static void um_register(id self, SEL s, id target, SEL sel, id obj) { (void)self; (void)s; (void)target; (void)sel; (void)obj; }
static void um_registerBlock(id self, SEL s, id target, void *block) { (void)self; (void)s; (void)target; (void)block; }
/* the message sent to the returned proxy is swallowed (unrecognized selectors on nil are no-ops) */
static id um_prepare(id self, SEL s, id target) { (void)self; (void)s; (void)target; return NULL; }
static void um_setActionName(id self, SEL s, id n) { (void)s; id old = UM(self)->actionName; UM(self)->actionName = n ? RT_SEND0(id, n, "copy") : NULL; objc_release(old); }
static id um_actionName(id self, SEL s) { (void)s; return UM(self)->actionName ? UM(self)->actionName : rt_str(""); }
static id um_empty(id self, SEL s) { (void)self; (void)s; return rt_str(""); }
static id um_runLoopModes(id self, SEL s) { (void)self; (void)s; return RT_SEND0(id, (id)rt_class_named("NSArray"), "array"); }

/* ---------------------------------------------------------- NSHashTable (a mutable set; weak options keep strong refs) */
static id ht_weak(Class c, SEL s) { (void)s; return objc_autorelease(RT_SEND0(id, rt_alloc_instance(c, 0), "init")); }
static id ht_withOptions(Class c, SEL s, unsigned long o) { (void)o; return ht_weak(c, s); }
static id ht_initWithOptions(id self, SEL s, unsigned long o, unsigned long cap) { (void)s; (void)o; (void)cap; return RT_SEND0(id, self, "init"); }
static id ht_setRepresentation(id self, SEL s) { (void)s; return RT_SEND1(id, (id)rt_class_named("NSSet"), "setWithArray:", id, RT_SEND0(id, self, "allObjects")); }

/* ---------------------------------------------------------- sets, dictionaries */
static void ms_addArray(id self, SEL s, id arr) {
    (void)s; for (size_t i = 0; arr && i < rt_array_count(arr); i++) RT_SEND1(void, self, "addObject:", id, rt_array_at(arr, i));
}
static void ms_setSet(id self, SEL s, id other) {
    (void)s; id all = other ? RT_SEND0(id, other, "allObjects") : NULL;
    objc_retain(all);
    RT_SEND0(void, self, "removeAllObjects");
    ms_addArray(self, NULL, all);
    objc_release(all);
}
static void ms_unionSet(id self, SEL s, id other) { (void)s; if (other) ms_addArray(self, NULL, RT_SEND0(id, other, "allObjects")); }
static id set_initWithSet(id self, SEL s, id other) {
    (void)s; self = RT_SEND0(id, self, "init");
    if (self && other) ms_addArray(self, NULL, RT_SEND0(id, other, "allObjects"));
    return self;
}
static id set_initWithSetCopy(id self, SEL s, id other, long copy) { (void)copy; return set_initWithSet(self, s, other); }
static id set_initWithCapacity(id self, SEL s, unsigned long n) { (void)s; (void)n; return RT_SEND0(id, self, "init"); }
static void md_setDictionary(id self, SEL s, id other) {
    (void)s; objc_retain(other);
    RT_SEND0(void, self, "removeAllObjects");
    if (other) RT_SEND1(void, self, "addEntriesFromDictionary:", id, other);
    objc_release(other);
}
static id d_objectEnumerator(id self, SEL s) { (void)s; return RT_SEND0(id, RT_SEND0(id, self, "allValues"), "objectEnumerator"); }
static id as_dataFromRange(id self, SEL s, NSRange r, id attrs, id *err) { (void)self; (void)s; (void)r; (void)attrs; if (err) *err = NULL; return NULL; }
static long kua_versionForClassName(id self, SEL s, id name) { (void)self; (void)s; (void)name; return 0; }

/* lossy conversion: characters the target encoding lacks become '?' */
static id s_dataLossy(id self, SEL s, unsigned long enc, long lossy) {
    (void)s;
    id d = RT_SEND1(id, self, "dataUsingEncoding:", unsigned long, enc);
    if (d || !(lossy & 0xff)) return d;
    unsigned long n = RT_SEND0(unsigned long, self, "length");
    unsigned char *b = malloc(n ? n : 1);
    for (unsigned long i = 0; i < n; i++) {
        unsigned c = RT_SEND1(unsigned short, self, "characterAtIndex:", unsigned long, i);
        b[i] = (unsigned char)(c < 0x80 || (c < 0x100 && enc != 1) ? c : '?');
    }
    d = rt_data_new(b, n); free(b);
    return objc_autorelease(d);
}

/* ---------------------------------------------------------- NSFileCoordinator: one process, so accessors run at once */
RT_DEFINE_CLASS(NSFileCoordinator);
static id g_presenters;
static id fc_initWithPresenter(id self, SEL s, id p) { (void)s; (void)p; return self; }
static void fc_addPresenter(Class c, SEL s, id p) { (void)c; (void)s; if (!g_presenters) g_presenters = RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableArray"), "alloc"), "init"); if (p) RT_SEND1(void, g_presenters, "addObject:", id, p); }
static void fc_removePresenter(Class c, SEL s, id p) { (void)c; (void)s; if (g_presenters && p) RT_SEND1(void, g_presenters, "removeObjectIdenticalTo:", id, p); }
static id fc_presenters(Class c, SEL s) { (void)c; (void)s; return g_presenters ? RT_SEND0(id, g_presenters, "copy") : RT_SEND0(id, (id)rt_class_named("NSArray"), "array"); }
static void fc_one(id self, SEL s, id url, unsigned long o, id *err, struct rt_block *b) {
    (void)self; (void)s; (void)o; if (err) *err = NULL;
    if (b) ((void (*)(void *, id))b->invoke)(b, url);
}
static void fc_two(id self, SEL s, id u1, unsigned long o1, id u2, unsigned long o2, id *err, struct rt_block *b) {
    (void)self; (void)s; (void)o1; (void)o2; if (err) *err = NULL;
    if (b) ((void (*)(void *, id, id))b->invoke)(b, u1, u2);
}
static void fc_cancel(id self, SEL s) { (void)self; (void)s; }
static void md_removeForKeys(id self, SEL s, id keys) {
    (void)s; for (size_t i = 0; keys && i < rt_array_count(keys); i++) RT_SEND1(void, self, "removeObjectForKey:", id, rt_array_at(keys, i));
}

static id is_initWithIndexSet(id self, SEL s, id other) {
    (void)s; self = RT_SEND0(id, self, "init");
    if (self && other) RT_SEND1(void, self, "addIndexes:", id, other);
    return self;
}
static id os_array(id self, SEL s) { (void)s; return objc_autorelease(RT_SEND1(id, RT_SEND0(id, (id)rt_class_named("NSArray"), "alloc"), "initWithArray:", id, self)); }
static void ma_removeAtIndexes(id self, SEL s, id idx) {
    (void)s; if (!idx) return;
    unsigned long n = RT_SEND0(unsigned long, self, "count");
    for (unsigned long i = n; i-- > 0;)
        if (RT_SEND1(long, idx, "containsIndex:", unsigned long, i) & 0xff) RT_SEND1(void, self, "removeObjectAtIndex:", unsigned long, i);
}
static id s_replacingRange(id self, SEL s, NSRange r, id with) {
    (void)s; id m = RT_SEND0(id, self, "mutableCopy");
    RT_SEND2(void, m, "replaceCharactersInRange:withString:", NSRange, r, id, with ? with : rt_str(""));
    return objc_autorelease(m);
}
/* the locale only changes collation details; the range/options comparison carries the order */
static long s_compareLocale(id self, SEL s, id o, unsigned long opts, NSRange r, id loc) {
    (void)s; (void)loc;
    return ((long (*)(id, SEL, id, unsigned long, NSRange))objc_msgSend)(self, rt_sel("compare:options:range:"), o, opts, r);
}
/* NSDictionary (NSFileAttributes) accessors not covered elsewhere */
static unsigned long d_attrUL(id self, const char *key) { id v = RT_SEND1(id, self, "objectForKey:", id, rt_str(key)); return v ? RT_SEND0(unsigned long, v, "unsignedLongValue") : 0; }
static unsigned int d_hfsType(id self, SEL s) { (void)s; return (unsigned int)d_attrUL(self, "NSFileHFSTypeCode"); }
static unsigned int d_hfsCreator(id self, SEL s) { (void)s; return (unsigned int)d_attrUL(self, "NSFileHFSCreatorCode"); }
static unsigned long d_posixPerms(id self, SEL s) { (void)s; return d_attrUL(self, "NSFilePosixPermissions"); }
static unsigned long d_fsNumber(id self, SEL s) { (void)s; return d_attrUL(self, "NSFileSystemNumber"); }
static unsigned long d_fsFileNumber(id self, SEL s) { (void)s; return d_attrUL(self, "NSFileSystemFileNumber"); }
static rt_bool d_attrBool(id self, const char *key) { id v = RT_SEND1(id, self, "objectForKey:", id, rt_str(key)); return v ? (RT_SEND0(long, v, "boolValue") & 0xff) : 0; }
static rt_bool d_extHidden(id self, SEL s) { (void)s; return d_attrBool(self, "NSFileExtensionHidden"); }
static rt_bool d_immutable(id self, SEL s) { (void)s; return d_attrBool(self, "NSFileImmutable"); }
static rt_bool d_appendOnly(id self, SEL s) { (void)s; return d_attrBool(self, "NSFileAppendOnly"); }
static id d_owner(id self, SEL s) { (void)s; return RT_SEND1(id, self, "objectForKey:", id, rt_str("NSFileOwnerAccountName")); }
static id d_group(id self, SEL s) { (void)s; return RT_SEND1(id, self, "objectForKey:", id, rt_str("NSFileGroupOwnerAccountName")); }
/* there are no Finder aliases on this file system */
static id url_resolveAlias(Class c, SEL s, id url, unsigned long o, id *err) { (void)c; (void)s; (void)o; if (err) *err = NULL; return url; }
static void o_kvo_remove_ctx(id self, SEL s, id obs, id path, void *ctx) { (void)self; (void)s; (void)obs; (void)path; (void)ctx; }
static id nf_localized(Class c, SEL s, id num, unsigned long style) {
    (void)s; id f = RT_SEND0(id, RT_SEND0(id, (id)c, "alloc"), "init");
    RT_SEND1(void, f, "setNumberStyle:", unsigned long, style);
    id r = RT_SEND1(id, f, "stringFromNumber:", id, num);
    objc_retain(r); objc_release(f);
    return objc_autorelease(r);
}

static IMP rt_lookup_imp_named(const char *cls, const char *sel) {
    int found; IMP rt_lookup(Class, SEL, int *);
    return rt_lookup(rt_class_named(cls), rt_sel(sel), &found);
}

/* ---------------------------------------------------------- C functions */
const uint32_t *iosrt_CFStringGetListOfAvailableEncodings(void) {
    static const uint32_t list[] = {
        0x0000 /* MacRoman */, 0x0500 /* WindowsLatin1 */, 0x0201 /* ISOLatin1 */, 0x0B01 /* NextStepLatin */,
        0x0600 /* ASCII */, 0x0100 /* Unicode */, 0x08000100 /* UTF8 */, 0x0BFF /* NonLossyASCII */,
        0x10000100 /* UTF16BE */, 0x14000100 /* UTF16LE */, 0x0c000100 /* UTF32 */,
        0x18000100 /* UTF32BE */, 0x1c000100 /* UTF32LE */, 0xffffffffu
    };
    return list;
}
/* CFUUIDBytes is a 16-byte struct: it arrives in two registers */
id iosrt_CFUUIDCreateFromUUIDBytes(const void *alloc, uint64_t lo, uint64_t hi) {
    (void)alloc;
    id u = RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSUUID"), "alloc"), "init");
    uint8_t *b = (uint8_t *)u + sizeof(void *);   /* struct nsuuid { isa; uint8_t b[16]; } */
    memcpy(b, &lo, 8); memcpy(b + 8, &hi, 8);
    return u;
}
id iosrt_CFUUIDCreateFromString(const void *alloc, id str) {
    (void)alloc; if (!str) return NULL;
    const char *p = rt_str_utf8(str); uint8_t bytes[16]; int n = 0;
    for (; *p && n < 16; p++) {
        if (*p == '-') continue;
        unsigned v; if (sscanf(p, "%2x", &v) != 1) return NULL;
        bytes[n++] = (uint8_t)v; p++;
    }
    if (n != 16) return NULL;
    uint64_t lo, hi; memcpy(&lo, bytes, 8); memcpy(&hi, bytes + 8, 8);
    return iosrt_CFUUIDCreateFromUUIDBytes(alloc, lo, hi);
}
/* CMTime { int64 value; int32 timescale; uint32 flags; int64 epoch } is returned through x8 */
struct cmtime { int64_t value; int32_t timescale; uint32_t flags; int64_t epoch; };
struct cmtime iosrt_CMTimeMake(int64_t value, int32_t timescale) {
    struct cmtime t = { value, timescale, 1 /* valid */, 0 };
    return t;
}
struct cmtime iosrt_CMTimeConvertScale(struct cmtime t, int32_t scale, uint32_t method) {
    (void)method;
    if (!(t.flags & 1) || t.timescale == 0 || scale == 0) return t;
    double v = (double)t.value * scale / t.timescale;
    struct cmtime r = { (int64_t)(v < 0 ? v - 0.5 : v + 0.5), scale, t.flags, t.epoch };
    return r;
}
double iosrt_CMTimeGetSeconds(struct cmtime t) {
    if (!(t.flags & 1) || t.timescale == 0) return 0;
    return (double)t.value / t.timescale;
}
struct cmtime iosrt_CMTimeMakeWithSeconds(double sec, int32_t scale) {
    struct cmtime t = { (int64_t)(sec * scale), scale, 1, 0 };
    return t;
}

void rt_foundation_more6_init(void) {
    Class o = RT_CLASS(NSObject);
    NEWCLASS(NSUndoManager, o, sizeof(struct undo) - sizeof(void *));
    Class um = RT_CLASS(NSUndoManager);
    IM(um, "init", um_init); IM(um, "dealloc", um_dealloc);
    IM(um, "setLevelsOfUndo:", um_setLevels); IM(um, "levelsOfUndo", um_levels);
    IM(um, "disableUndoRegistration", um_disable); IM(um, "enableUndoRegistration", um_enable);
    IM(um, "isUndoRegistrationEnabled", um_isEnabled);
    IM(um, "beginUndoGrouping", um_begin); IM(um, "endUndoGrouping", um_end); IM(um, "groupingLevel", um_groupingLevel);
    IM(um, "setGroupsByEvent:", um_setGroupsByEvent); IM(um, "groupsByEvent", um_groupsByEvent);
    IM(um, "canUndo", um_no); IM(um, "canRedo", um_no); IM(um, "isUndoing", um_no); IM(um, "isRedoing", um_no);
    IM(um, "undo", um_noop); IM(um, "redo", um_noop); IM(um, "undoNestedGroup", um_noop);
    IM(um, "removeAllActions", um_noop); IM(um, "removeAllActionsWithTarget:", um_noop1);
    IM(um, "registerUndoWithTarget:selector:object:", um_register); IM(um, "registerUndoWithTarget:handler:", um_registerBlock);
    IM(um, "prepareWithInvocationTarget:", um_prepare);
    IM(um, "setActionName:", um_setActionName); IM(um, "undoActionName", um_actionName); IM(um, "redoActionName", um_empty);
    IM(um, "undoMenuItemTitle", um_empty); IM(um, "redoMenuItemTitle", um_empty);
    IM(um, "runLoopModes", um_runLoopModes); IM(um, "setRunLoopModes:", um_noop1);

    NEWCLASS(NSFileCoordinator, o, 0);
    Class fc = RT_CLASS(NSFileCoordinator);
    IM(fc, "initWithFilePresenter:", fc_initWithPresenter); IM(fc, "cancel", fc_cancel);
    CM(fc, "addFilePresenter:", fc_addPresenter); CM(fc, "removeFilePresenter:", fc_removePresenter); CM(fc, "filePresenters", fc_presenters);
    IM(fc, "coordinateReadingItemAtURL:options:error:byAccessor:", fc_one);
    IM(fc, "coordinateWritingItemAtURL:options:error:byAccessor:", fc_one);
    IM(fc, "coordinateReadingItemAtURL:options:writingItemAtURL:options:error:byAccessor:", fc_two);
    IM(fc, "coordinateWritingItemAtURL:options:writingItemAtURL:options:error:byAccessor:", fc_two);
    IM(rt_class_named("NSMutableDictionary"), "removeObjectsForKeys:", md_removeForKeys);
    Class mset = RT_CLASS(NSMutableSet);
    NEWCLASS(NSHashTable, mset, 0);
    Class ht = RT_CLASS(NSHashTable);
    CM(ht, "weakObjectsHashTable", ht_weak); CM(ht, "hashTableWithWeakObjects", ht_weak);
    CM(ht, "hashTableWithOptions:", ht_withOptions);
    IM(ht, "initWithOptions:capacity:", ht_initWithOptions); IM(ht, "setRepresentation", ht_setRepresentation);

    Class mis = rt_class_named("NSMutableIndexSet");
    IM(rt_class_named("NSIndexSet"), "initWithIndexSet:", is_initWithIndexSet);
    if (mis) IM(mis, "initWithIndexSet:", is_initWithIndexSet);
    /* NSIndexSet's own initialiser fills the receiver through the mutable methods */
    IM(rt_class_named("NSIndexSet"), "addIndexes:", (void *)rt_lookup_imp_named("NSMutableIndexSet", "addIndexes:"));
    IM(rt_class_named("NSIndexSet"), "addIndex:", (void *)rt_lookup_imp_named("NSMutableIndexSet", "addIndex:"));
    IM(rt_class_named("NSOrderedSet"), "array", os_array); IM(rt_class_named("NSMutableOrderedSet"), "array", os_array);
    IM(rt_class_named("NSMutableArray"), "removeObjectsAtIndexes:", ma_removeAtIndexes);
    IM(rt_class_named("NSString"), "stringByReplacingCharactersInRange:withString:", s_replacingRange);
    IM(rt_class_named("NSString"), "compare:options:range:locale:", s_compareLocale);
    Class dict = rt_class_named("NSDictionary");
    IM(dict, "fileHFSTypeCode", d_hfsType); IM(dict, "fileHFSCreatorCode", d_hfsCreator);
    IM(dict, "filePosixPermissions", d_posixPerms); IM(dict, "fileSystemNumber", d_fsNumber); IM(dict, "fileSystemFileNumber", d_fsFileNumber);
    IM(dict, "fileExtensionHidden", d_extHidden); IM(dict, "fileIsImmutable", d_immutable); IM(dict, "fileIsAppendOnly", d_appendOnly);
    IM(dict, "fileOwnerAccountName", d_owner); IM(dict, "fileGroupOwnerAccountName", d_group);
    CM(rt_class_named("NSURL"), "URLByResolvingAliasFileAtURL:options:error:", url_resolveAlias);
    IM(o, "removeObserver:forKeyPath:context:", o_kvo_remove_ctx);
    CM(rt_class_named("NSNumberFormatter"), "localizedStringFromNumber:numberStyle:", nf_localized);
    IM(mset, "addObjectsFromArray:", ms_addArray); IM(mset, "setSet:", ms_setSet); IM(mset, "unionSet:", ms_unionSet);
    Class set = rt_class_named("NSSet");
    IM(set, "initWithSet:", set_initWithSet); IM(set, "initWithSet:copyItems:", set_initWithSetCopy);
    IM(mset, "initWithCapacity:", set_initWithCapacity);
    IM(rt_class_named("NSMutableDictionary"), "setDictionary:", md_setDictionary);
    IM(rt_class_named("NSDictionary"), "objectEnumerator", d_objectEnumerator);
    Class as = rt_class_named("NSAttributedString"), kua = rt_class_named("NSKeyedUnarchiver");
    if (as) IM(as, "dataFromRange:documentAttributes:error:", as_dataFromRange);
    if (kua) IM(kua, "versionForClassName:", kua_versionForClassName);
    IM(rt_class_named("NSString"), "dataUsingEncoding:allowLossyConversion:", s_dataLossy);
}
