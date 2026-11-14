/*
 * NSDirectoryEnumerator (recursive, relative paths, attributes, skipDescendants)
 * and NSURL resource values backed by stat(2): NSURLIsDirectoryKey, sizes,
 * dates, names... Also CFURLCopyResourcePropertyForKey and friends.
 */
#include "../internal.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")
#define NEWCLASS(N, SUPER, SZ) rt_native_class(RT_CLASS(N), &iosrt_meta_##N, #N, SUPER, SZ)

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSEnumerator);
RT_DEFINE_CLASS(NSDirectoryEnumerator);
id rt_dict_new(id *vals, id *keys, size_t n);

static const double kRef1970 = 978307200.0;
static id date_of(time_t t) { return RT_SEND1(id, (id)rt_class_named("NSDate"), "dateWithTimeIntervalSinceReferenceDate:", double, (double)t - kRef1970); }

/* the attribute dictionary NSFileManager reports for a stat result */
static id attrs_for(const struct stat *st) {
    const char *type = S_ISDIR(st->st_mode) ? "NSFileTypeDirectory" : S_ISLNK(st->st_mode) ? "NSFileTypeSymbolicLink" :
                       S_ISREG(st->st_mode) ? "NSFileTypeRegular" : "NSFileTypeUnknown";
    id keys[9] = { rt_str("NSFileSize"), rt_str("NSFileType"), rt_str("NSFileModificationDate"), rt_str("NSFileCreationDate"),
                   rt_str("NSFilePosixPermissions"), rt_str("NSFileSystemFileNumber"), rt_str("NSFileReferenceCount"),
                   rt_str("NSFileOwnerAccountID"), rt_str("NSFileGroupOwnerAccountID") };
    id vals[9] = { rt_num_int(st->st_size), rt_str(type), date_of(st->st_mtime), date_of(st->st_ctime),
                   rt_num_int(st->st_mode & 07777), rt_num_int((long long)st->st_ino), rt_num_int((long long)st->st_nlink),
                   rt_num_int(st->st_uid), rt_num_int(st->st_gid) };
    return objc_autorelease(rt_dict_new(vals, keys, 9));
}
static id fm_attrsItem(id self, SEL s, id path, id *err) {
    (void)self; (void)s; if (err) *err = NULL;
    struct stat st; if (!path || lstat(rt_str_utf8(path), &st)) return NULL;
    if (S_ISLNK(st.st_mode)) { struct stat t; if (!stat(rt_str_utf8(path), &t) && S_ISDIR(t.st_mode)) { /* keep the link type, as Foundation does */ } }
    return attrs_for(&st);
}

/* ------------------------------------------------------------ NSDirectoryEnumerator */
struct dlevel { DIR *d; char *rel; };
/* lives after NSEnumerator's own ivars */
struct direnum {
    char *root; struct dlevel stack[64]; int depth;
    id lastAttrs; char *lastRel; int lastIsDir; int skip; int urls; unsigned long opts;
};
static size_t g_base;
#define DE(x) ((struct direnum *)((char *)(x) + g_base))
static void de_push(struct direnum *e, const char *rel) {
    if (e->depth >= 64) return;
    char full[4096]; snprintf(full, sizeof full, "%s%s%s", e->root, *rel ? "/" : "", rel);
    DIR *d = opendir(full);
    if (!d) return;
    e->stack[e->depth].d = d; e->stack[e->depth].rel = strdup(rel); e->depth++;
}
static void de_pop(struct direnum *e) { e->depth--; closedir(e->stack[e->depth].d); free(e->stack[e->depth].rel); }
static id de_make(Class c, const char *root, int urls, unsigned long opts) {
    id o = rt_alloc_instance(c, 0); struct direnum *e = DE(o);
    e->root = strdup(root); e->urls = urls; e->opts = opts;
    size_t n = strlen(e->root); while (n > 1 && e->root[n - 1] == '/') e->root[--n] = 0;
    de_push(e, "");
    return o;
}
static id de_next(id self, SEL s) {
    (void)s; struct direnum *e = DE(self);
    /* descend into the directory returned last, unless skipDescendants was called */
    if (e->lastRel && e->lastIsDir && !e->skip && !(e->opts & 1 /* SkipsSubdirectoryDescendants */)) de_push(e, e->lastRel);
    free(e->lastRel); e->lastRel = NULL; e->skip = 0;
    while (e->depth > 0) {
        struct dlevel *l = &e->stack[e->depth - 1];
        struct dirent *d = readdir(l->d);
        if (!d) { de_pop(e); continue; }
        if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) continue;
        if ((e->opts & 4 /* SkipsHiddenFiles */) && d->d_name[0] == '.') continue;
        char rel[4096]; snprintf(rel, sizeof rel, "%s%s%s", l->rel, *l->rel ? "/" : "", d->d_name);
        char full[4096]; snprintf(full, sizeof full, "%s/%s", e->root, rel);
        struct stat st; int ok = lstat(full, &st) == 0;
        e->lastRel = strdup(rel); e->lastIsDir = ok && S_ISDIR(st.st_mode);
        if ((e->opts & 2 /* SkipsPackageDescendants */) && e->lastIsDir && strchr(d->d_name, '.')) e->skip = 1;
        objc_release(e->lastAttrs); e->lastAttrs = ok ? objc_retain(attrs_for(&st)) : NULL;
        if (e->urls) return RT_SEND2(id, (id)rt_class_named("NSURL"), "fileURLWithPath:isDirectory:", id, rt_str(full), long, e->lastIsDir);
        return rt_str(rel);
    }
    return NULL;
}
static id de_all(id self, SEL s) {
    id a = RT_SEND0(id, (id)rt_class_named("NSMutableArray"), "array");
    for (id o; (o = de_next(self, s));) RT_SEND1(void, a, "addObject:", id, o);
    return a;
}
static id de_fileAttributes(id self, SEL s) { (void)s; return DE(self)->lastAttrs; }
static id de_dirAttributes(id self, SEL s) {
    (void)s; struct stat st; return stat(DE(self)->root, &st) ? NULL : attrs_for(&st);
}
static void de_skip(id self, SEL s) { (void)s; DE(self)->skip = 1; }
static unsigned long de_level(id self, SEL s) {
    (void)s; struct direnum *e = DE(self); if (!e->lastRel) return 0;
    unsigned long n = 1; for (const char *p = e->lastRel; *p; p++) if (*p == '/') n++;
    return n;
}
static rt_bool de_isEnumeratingDirectoryPostOrder(id self, SEL s) { (void)self; (void)s; return 0; }
static void de_dealloc(id self, SEL s) {
    struct direnum *e = DE(self);
    while (e->depth > 0) de_pop(e);
    free(e->root); free(e->lastRel); objc_release(e->lastAttrs);
    struct { id r; Class c; } sup = { self, RT_CLASS(NSDirectoryEnumerator) };
    ((void (*)(void *, SEL))objc_msgSendSuper2)(&sup, s);
}
/* fast enumeration over the remaining entries */
struct fe_state { unsigned long state; id *items; unsigned long *mutations; unsigned long extra[5]; };
static unsigned long de_fast(id self, SEL s, struct fe_state *st, id *buf, unsigned long len) {
    (void)s; static unsigned long mut;
    if (!len) return 0;
    st->mutations = &mut; st->items = buf;
    id o = de_next(self, NULL);
    if (!o) return 0;
    buf[0] = o; st->state = 1;
    return 1;
}

static id fm_enumeratorAtPath(id self, SEL s, id path) {
    (void)self; (void)s; if (!path) return NULL;
    struct stat st; if (stat(rt_str_utf8(path), &st) || !S_ISDIR(st.st_mode)) return NULL;
    return objc_autorelease(de_make(RT_CLASS(NSDirectoryEnumerator), rt_str_utf8(path), 0, 0));
}
static id fm_enumeratorAtURL(id self, SEL s, id url, id keys, unsigned long opts, void *handler) {
    (void)self; (void)s; (void)keys; (void)handler; if (!url) return NULL;
    id p = RT_SEND0(id, url, "path"); if (!p) return NULL;
    struct stat st; if (stat(rt_str_utf8(p), &st) || !S_ISDIR(st.st_mode)) return NULL;
    return objc_autorelease(de_make(RT_CLASS(NSDirectoryEnumerator), rt_str_utf8(p), 1, opts));
}
static id fm_subpathsAtPath(id self, SEL s, id path) {
    id e = fm_enumeratorAtPath(self, s, path);
    return e ? de_all(e, NULL) : NULL;
}
static id fm_subpathsErr(id self, SEL s, id path, id *err) { if (err) *err = NULL; return fm_subpathsAtPath(self, s, path); }
static id fm_contentsURL(id self, SEL s, id url, id keys, unsigned long opts, id *err) {
    (void)self; (void)s; (void)keys; if (err) *err = NULL;
    id p = url ? RT_SEND0(id, url, "path") : NULL; if (!p) return NULL;
    DIR *d = opendir(rt_str_utf8(p)); if (!d) return NULL;
    id a = RT_SEND0(id, (id)rt_class_named("NSMutableArray"), "array");
    for (struct dirent *e; (e = readdir(d));) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if ((opts & 4) && e->d_name[0] == '.') continue;
        char full[4096]; snprintf(full, sizeof full, "%s/%s", rt_str_utf8(p), e->d_name);
        struct stat st; int dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        RT_SEND1(void, a, "addObject:", id, RT_SEND2(id, (id)rt_class_named("NSURL"), "fileURLWithPath:isDirectory:", id, rt_str(full), long, dir));
    }
    closedir(d);
    return a;
}

/* ------------------------------------------------------------ NSURL resource values */
static int key_is(id k, const char *name) { return k && !strcmp(rt_str_utf8(k), name); }
static id rt_num_bool(int v) { return RT_SEND1(id, (id)rt_class_named("NSNumber"), "numberWithBool:", long, v ? 1 : 0); }
static id resource_value(id url, id key) {
    id p = url ? RT_SEND0(id, url, "path") : NULL;
    if (!p || !key) return NULL;
    const char *path = rt_str_utf8(p);
    struct stat st, lst;
    int ok = stat(path, &st) == 0, lok = lstat(path, &lst) == 0;
    const char *base = strrchr(path, '/'); base = base ? base + 1 : path;
    if (key_is(key, "NSURLNameKey") || key_is(key, "NSURLLocalizedNameKey")) return rt_str(base);
    if (key_is(key, "NSURLPathKey")) return p;
    if (key_is(key, "NSURLParentDirectoryURLKey")) return RT_SEND0(id, url, "URLByDeletingLastPathComponent");
    if (key_is(key, "NSURLIsAliasFileKey") || key_is(key, "NSURLIsUbiquitousItemKey") || key_is(key, "NSURLIsVolumeKey") ||
        key_is(key, "NSURLIsExcludedFromBackupKey") || key_is(key, "NSURLHasHiddenExtensionKey")) return rt_num_bool(0);
    if (!ok && !lok) return NULL;
    if (key_is(key, "NSURLIsDirectoryKey")) return rt_num_bool(ok && S_ISDIR(st.st_mode));
    if (key_is(key, "NSURLIsRegularFileKey")) return rt_num_bool(ok && S_ISREG(st.st_mode));
    if (key_is(key, "NSURLIsSymbolicLinkKey")) return rt_num_bool(lok && S_ISLNK(lst.st_mode));
    if (key_is(key, "NSURLIsPackageKey")) return rt_num_bool(ok && S_ISDIR(st.st_mode) && strchr(base, '.') && base[0] != '.');
    if (key_is(key, "NSURLIsHiddenKey")) return rt_num_bool(base[0] == '.');
    if (key_is(key, "NSURLIsReadableKey")) return rt_num_bool(access(path, R_OK) == 0);
    if (key_is(key, "NSURLIsWritableKey")) return rt_num_bool(access(path, W_OK) == 0);
    if (key_is(key, "NSURLIsExecutableKey")) return rt_num_bool(access(path, X_OK) == 0);
    if (key_is(key, "NSURLFileSizeKey") || key_is(key, "NSURLTotalFileSizeKey") || key_is(key, "NSURLFileAllocatedSizeKey") ||
        key_is(key, "NSURLTotalFileAllocatedSizeKey")) return ok && S_ISREG(st.st_mode) ? rt_num_int(st.st_size) : NULL;
    if (key_is(key, "NSURLContentModificationDateKey") || key_is(key, "NSURLAttributeModificationDateKey")) return date_of(st.st_mtime);
    if (key_is(key, "NSURLCreationDateKey")) return date_of(st.st_ctime);
    if (key_is(key, "NSURLContentAccessDateKey")) return date_of(st.st_atime);
    if (key_is(key, "NSURLFileResourceTypeKey"))
        return rt_str(lok && S_ISLNK(lst.st_mode) ? "NSURLFileResourceTypeSymbolicLink" : S_ISDIR(st.st_mode) ? "NSURLFileResourceTypeDirectory" : "NSURLFileResourceTypeRegular");
    if (key_is(key, "NSURLFileResourceIdentifierKey")) return rt_num_int((long long)st.st_ino);
    if (key_is(key, "NSURLLinkCountKey")) return rt_num_int((long long)st.st_nlink);
    if (key_is(key, "NSURLTypeIdentifierKey")) {
        if (S_ISDIR(st.st_mode)) return rt_str("public.folder");
        const char *ext = strrchr(base, '.');
        if (ext && !strcasecmp(ext, ".caf")) return rt_str("com.apple.coreaudio-format");
        if (ext && !strcasecmp(ext, ".png")) return rt_str("public.png");
        if (ext && !strcasecmp(ext, ".plist")) return rt_str("com.apple.property-list");
        return rt_str("public.data");
    }
    return NULL;
}
static rt_bool url_getResource(id self, SEL s, id *out, id key, id *err) {
    (void)s; if (err) *err = NULL;
    if (out) *out = resource_value(self, key);
    return 1;
}
static id url_resourceValues(id self, SEL s, id keys, id *err) {
    (void)s; if (err) *err = NULL;
    id d = RT_SEND0(id, (id)rt_class_named("NSMutableDictionary"), "dictionary");
    for (size_t i = 0; keys && i < rt_array_count(keys); i++) {
        id k = rt_array_at(keys, i), v = resource_value(self, k);
        if (v) RT_SEND2(void, d, "setObject:forKey:", id, v, id, k);
    }
    return d;
}
static rt_bool url_setResource(id self, SEL s, id v, id k, id *err) { (void)self; (void)s; (void)v; (void)k; if (err) *err = NULL; return 1; }
static rt_bool url_setResources(id self, SEL s, id d, id *err) { (void)self; (void)s; (void)d; if (err) *err = NULL; return 1; }
static void url_noop(id self, SEL s) { (void)self; (void)s; }
static void url_setTemp(id self, SEL s, id v, id k) { (void)self; (void)s; (void)v; (void)k; }
static rt_bool url_checkReachable(id self, SEL s, id *err) {
    (void)s; if (err) *err = NULL;
    id p = RT_SEND0(id, self, "path"); return p && access(rt_str_utf8(p), F_OK) == 0;
}

/* CoreFoundation spellings */
rt_bool iosrt_CFURLCopyResourcePropertyForKey(id url, id key, id *out, id *err) {
    if (err) *err = NULL;
    id v = resource_value(url, key);
    if (out) *out = objc_retain(v);
    return 1;
}
id iosrt_CFURLCopyResourcePropertiesForKeys(id url, id keys, id *err) { return objc_retain(url_resourceValues(url, NULL, keys, err)); }
void iosrt_CFURLSetTemporaryResourcePropertyForKey(id url, id key, id v) { (void)url; (void)key; (void)v; }
rt_bool iosrt_CFURLSetResourcePropertyForKey(id url, id key, id v, id *err) { (void)url; (void)key; (void)v; if (err) *err = NULL; return 1; }
void iosrt_CFURLClearResourcePropertyCache(id url) { (void)url; }
rt_bool iosrt_CFURLResourceIsReachable(id url, id *err) { return url_checkReachable(url, NULL, err); }

void rt_fileenum_init(void) {
    size_t class_getInstanceSize(Class);
    g_base = class_getInstanceSize(RT_CLASS(NSEnumerator));
    NEWCLASS(NSDirectoryEnumerator, RT_CLASS(NSEnumerator), sizeof(struct direnum));
    Class de = RT_CLASS(NSDirectoryEnumerator);
    IM(de, "nextObject", de_next); IM(de, "allObjects", de_all);
    IM(de, "fileAttributes", de_fileAttributes); IM(de, "directoryAttributes", de_dirAttributes);
    IM(de, "skipDescendants", de_skip); IM(de, "skipDescendents", de_skip); IM(de, "level", de_level);
    IM(de, "isEnumeratingDirectoryPostOrder", de_isEnumeratingDirectoryPostOrder);
    IM(de, "countByEnumeratingWithState:objects:count:", de_fast); IM(de, "dealloc", de_dealloc);

    Class fm = rt_class_named("NSFileManager");
    IM(fm, "enumeratorAtPath:", fm_enumeratorAtPath);
    IM(fm, "enumeratorAtURL:includingPropertiesForKeys:options:errorHandler:", fm_enumeratorAtURL);
    IM(fm, "subpathsAtPath:", fm_subpathsAtPath); IM(fm, "subpathsOfDirectoryAtPath:error:", fm_subpathsErr);
    IM(fm, "contentsOfDirectoryAtURL:includingPropertiesForKeys:options:error:", fm_contentsURL);
    IM(fm, "attributesOfItemAtPath:error:", fm_attrsItem);

    Class u = rt_class_named("NSURL");
    IM(u, "getResourceValue:forKey:error:", url_getResource); IM(u, "resourceValuesForKeys:error:", url_resourceValues);
    IM(u, "setResourceValue:forKey:error:", url_setResource); IM(u, "setResourceValues:error:", url_setResources);
    IM(u, "removeAllCachedResourceValues", url_noop); IM(u, "setTemporaryResourceValue:forKey:", url_setTemp);
    IM(u, "checkResourceIsReachableAndReturnError:", url_checkReachable);
}
