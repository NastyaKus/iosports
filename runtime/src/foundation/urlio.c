/* URL flavours of the path-based file APIs (NSFileManager, NSData, NSString,
 * NSArray, NSDictionary). Each one converts a file URL to its path and calls
 * the path method, so behaviour stays in one place. Non-file URLs fail the
 * way an offline device would. */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")

static int class_is(Class c, Class base) { for (; c; c = c->superclass) if (c == base) return 1; return 0; }
static id path_of(id url) {
    if (!url) return NULL;
    if (!rt_is_kind_of(url, rt_class_named("NSURL"))) return url;   /* tolerate a path passed where a URL is expected */
    if (!(RT_SEND0(long, url, "isFileURL") & 0xff)) return NULL;
    id p = RT_SEND0(id, url, "path");
    id d = p ? RT_SEND0(id, p, "stringByRemovingPercentEncoding") : NULL;
    return d ? d : p;
}
static id no_file_error(void) {
    return RT_SEND3(id, (id)rt_class_named("NSError"), "errorWithDomain:code:userInfo:", id, rt_str("NSCocoaErrorDomain"), long, 4, id, NULL);   /* NSFileNoSuchFileError */
}
static id file_url(id path) { return path ? RT_SEND1(id, (id)rt_class_named("NSURL"), "fileURLWithPath:", id, path) : NULL; }
typedef long (*send_bool2)(id, SEL, id, id *);
#define FAIL_IF_NO(p) do { if (!(p)) { if (err) *err = no_file_error(); return 0; } } while (0)

/* ---- NSFileManager */
static long fm_removeURL(id self, SEL s, id url, id *err) {
    (void)s; id p = path_of(url); FAIL_IF_NO(p);
    return ((long (*)(id, SEL, id, id *))objc_msgSend)(self, rt_sel("removeItemAtPath:error:"), p, err) & 0xff;
}
static long fm_copyURL(id self, SEL s, id from, id to, id *err) {
    (void)s; id a = path_of(from), b = path_of(to); FAIL_IF_NO(a && b);
    return ((long (*)(id, SEL, id, id, id *))objc_msgSend)(self, rt_sel("copyItemAtPath:toPath:error:"), a, b, err) & 0xff;
}
static long fm_moveURL(id self, SEL s, id from, id to, id *err) {
    (void)s; id a = path_of(from), b = path_of(to); FAIL_IF_NO(a && b);
    return ((long (*)(id, SEL, id, id, id *))objc_msgSend)(self, rt_sel("moveItemAtPath:toPath:error:"), a, b, err) & 0xff;
}
static long fm_createDirURL(id self, SEL s, id url, long inter, id attrs, id *err) {
    (void)s; id p = path_of(url); FAIL_IF_NO(p);
    return ((long (*)(id, SEL, id, long, id, id *))objc_msgSend)(self, rt_sel("createDirectoryAtPath:withIntermediateDirectories:attributes:error:"), p, inter, attrs, err) & 0xff;
}
static id fm_contentsURL(id self, SEL s, id url, id keys, unsigned long opts, id *err) {
    (void)s; (void)keys; id p = path_of(url); if (!p) { if (err) *err = no_file_error(); return NULL; }
    id names = ((id (*)(id, SEL, id, id *))objc_msgSend)(self, rt_sel("contentsOfDirectoryAtPath:error:"), p, err);
    if (!names) return NULL;
    id out = RT_SEND0(id, (id)rt_class_named("NSMutableArray"), "array");
    unsigned long n = RT_SEND0(unsigned long, names, "count");
    for (unsigned long i = 0; i < n; i++) {
        id nm = RT_SEND1(id, names, "objectAtIndex:", unsigned long, i);
        if ((opts & 4) && rt_str_utf8(nm)[0] == '.') continue;   /* NSDirectoryEnumerationSkipsHiddenFiles */
        RT_SEND1(void, out, "addObject:", id, file_url(RT_SEND1(id, p, "stringByAppendingPathComponent:", id, nm)));
    }
    return out;
}
static id fm_urlForDirectory(id self, SEL s, unsigned long dir, unsigned long domain, id near, long create, id *err) {
    (void)s; (void)near;
    id urls = RT_SEND2(id, self, "URLsForDirectory:inDomains:", unsigned long, dir, unsigned long, domain);
    id u = urls && RT_SEND0(unsigned long, urls, "count") ? RT_SEND1(id, urls, "objectAtIndex:", unsigned long, 0) : NULL;
    if (!u) { if (err) *err = no_file_error(); return NULL; }
    if (create & 0xff) mkdir(rt_str_utf8(path_of(u)), 0755);
    return u;
}
/* app groups share nothing here: give each group a private folder in the app's Library */
static id fm_groupContainer(id self, SEL s, id ident) {
    (void)s;
    id lib = fm_urlForDirectory(self, NULL, 5 /* NSLibraryDirectory */, 1, NULL, 1, NULL);
    if (!lib || !ident) return NULL;
    id groups = RT_SEND1(id, path_of(lib), "stringByAppendingPathComponent:", id, rt_str("AppGroups"));
    mkdir(rt_str_utf8(groups), 0755);
    id p = RT_SEND1(id, groups, "stringByAppendingPathComponent:", id, ident);
    mkdir(rt_str_utf8(p), 0755);
    return file_url(p);
}
static long fm_existsURL(id self, SEL s, id url) { (void)s; id p = path_of(url); return p ? RT_SEND1(long, self, "fileExistsAtPath:", id, p) & 0xff : 0; }

/* ---- NSData */
static long data_writeURLOpts(id self, SEL s, id url, unsigned long opts, id *err) {
    (void)s; id p = path_of(url); FAIL_IF_NO(p);
    return ((long (*)(id, SEL, id, unsigned long, id *))objc_msgSend)(self, rt_sel("writeToFile:options:error:"), p, opts, err) & 0xff;
}
static id data_withURLOpts(Class c, SEL s, id url, unsigned long opts, id *err) {
    (void)s; id p = path_of(url); if (!p) { if (err) *err = no_file_error(); return NULL; }
    return ((id (*)(id, SEL, id, unsigned long, id *))objc_msgSend)((id)c, rt_sel("dataWithContentsOfFile:options:error:"), p, opts, err);
}
static id data_initWithURL(id self, SEL s, id url) {
    (void)s; id p = path_of(url); if (!p) { objc_release(self); return NULL; }
    return RT_SEND1(id, self, "initWithContentsOfFile:", id, p);
}
static id data_initWithURLOpts(id self, SEL s, id url, unsigned long opts, id *err) {
    (void)opts; id r = data_initWithURL(self, s, url);
    if (!r && err) *err = no_file_error();
    return r;
}

/* ---- NSString */
static long str_writeURL(id self, SEL s, id url, long atomically, unsigned long enc, id *err) {
    (void)s; id p = path_of(url); FAIL_IF_NO(p);
    return ((long (*)(id, SEL, id, long, unsigned long, id *))objc_msgSend)(self, rt_sel("writeToFile:atomically:encoding:error:"), p, atomically, enc, err) & 0xff;
}

/* ---- NSArray / NSDictionary */
static id coll_withURL(Class c, SEL s, id url) {
    id p = path_of(url); if (!p) return NULL;
    (void)s; const char *sel = class_is(c, rt_class_named("NSDictionary")) ? "dictionaryWithContentsOfFile:" : "arrayWithContentsOfFile:";
    return RT_SEND1(id, (id)c, sel, id, p);
}
static id coll_initWithURL(id self, SEL s, id url) {
    (void)s; id p = path_of(url); if (!p) { objc_release(self); return NULL; }
    Class c = self->isa; objc_release(self);
    const char *sel = class_is(c, rt_class_named("NSDictionary")) ? "dictionaryWithContentsOfFile:" : "arrayWithContentsOfFile:";
    return objc_retain(RT_SEND1(id, (id)c, sel, id, p));
}
static long coll_writeURL(id self, SEL s, id url, long atomically) {
    (void)s; id p = path_of(url); if (!p) return 0;
    return RT_SEND2(long, self, "writeToFile:atomically:", id, p, long, atomically) & 0xff;
}

void rt_urlio_init(void) {
    Class fm = rt_class_named("NSFileManager");
    IM(fm, "removeItemAtURL:error:", fm_removeURL); IM(fm, "trashItemAtURL:resultingItemURL:error:", fm_removeURL);
    IM(fm, "copyItemAtURL:toURL:error:", fm_copyURL); IM(fm, "moveItemAtURL:toURL:error:", fm_moveURL);
    IM(fm, "createDirectoryAtURL:withIntermediateDirectories:attributes:error:", fm_createDirURL);
    IM(fm, "contentsOfDirectoryAtURL:includingPropertiesForKeys:options:error:", fm_contentsURL);
    IM(fm, "URLForDirectory:inDomain:appropriateForURL:create:error:", fm_urlForDirectory);
    IM(fm, "containerURLForSecurityApplicationGroupIdentifier:", fm_groupContainer);
    IM(fm, "fileExistsAtURL:", fm_existsURL);
    Class da = rt_class_named("NSData");
    IM(da, "writeToURL:options:error:", data_writeURLOpts);
    CM(da, "dataWithContentsOfURL:options:error:", data_withURLOpts);
    IM(da, "initWithContentsOfURL:", data_initWithURL); IM(da, "initWithContentsOfURL:options:error:", data_initWithURLOpts);
    IM(rt_class_named("NSString"), "writeToURL:atomically:encoding:error:", str_writeURL);
    Class ar = rt_class_named("NSArray"), di = rt_class_named("NSDictionary");
    CM(ar, "arrayWithContentsOfURL:", coll_withURL); CM(di, "dictionaryWithContentsOfURL:", coll_withURL);
    IM(ar, "initWithContentsOfURL:", coll_initWithURL); IM(di, "initWithContentsOfURL:", coll_initWithURL);
    IM(ar, "writeToURL:atomically:", coll_writeURL); IM(di, "writeToURL:atomically:", coll_writeURL);
}
