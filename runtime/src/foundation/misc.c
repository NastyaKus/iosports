/*
 * Foundation HLE, part 3: NSProcessInfo, NSDate, NSData, NSBundle (+plist),
 * NSException, NSThread, NSTimer/NSRunLoop, NSUserDefaults, NSFileManager,
 * NSNotificationCenter and a CoreFoundation C subset (toll-free bridged).
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <math.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSString);
RT_DECLARE_CLASS(NSArray);
RT_DECLARE_CLASS(NSMutableArray);
RT_DECLARE_CLASS(NSDictionary);
RT_DECLARE_CLASS(NSMutableDictionary);
RT_DECLARE_CLASS(NSNumber);
RT_DEFINE_CLASS(NSProcessInfo);
RT_DEFINE_CLASS(NSDate);
RT_DEFINE_CLASS(NSData);
RT_DEFINE_CLASS(NSMutableData);
RT_DEFINE_CLASS(NSBundle);
RT_DEFINE_CLASS(NSException);
RT_DEFINE_CLASS(NSThread);
RT_DEFINE_CLASS(NSTimer);
RT_DEFINE_CLASS(NSRunLoop);
RT_DEFINE_CLASS(NSUserDefaults);
RT_DEFINE_CLASS(NSFileManager);
RT_DEFINE_CLASS(NSNotificationCenter);
RT_DEFINE_CLASS(NSNotification);

id rt_dict_new(id *vals, id *keys, size_t n);
void rt_dict_set(id dict, id key, id val);
void rt_main_queue_wait(int ms);
extern char **environ;

char *rt_bundle_path;
static const double kRefDate1970 = 978307200.0;
static double now_ref(void) { struct timeval tv; gettimeofday(&tv, NULL); return (double)tv.tv_sec + tv.tv_usec / 1e6 - kRefDate1970; }
static id singleton(Class c, id *slot) { if (!*slot) *slot = rt_alloc_instance(c, 0); return *slot; }
static id nsnull(void) { return RT_SEND0(id, (id)rt_class_named("NSNull"), "null"); }

/* ============================================================== NSProcessInfo */
static id g_procinfo;
struct osver { long major, minor, patch; };
static id pi_processInfo(Class c, SEL s) { (void)s; return singleton(c, &g_procinfo); }
static int g_argc_copy; static char **g_argv_copy;
void rt_foundation_set_args(int argc, char **argv) { g_argc_copy = argc; g_argv_copy = argv; }
static id pi_arguments(id self, SEL s) {
    (void)self; (void)s; id a = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array");
    for (int i = 0; i < g_argc_copy; i++) RT_SEND1(void, a, "addObject:", id, rt_str(g_argv_copy[i]));
    return a;
}
static id pi_environment(id self, SEL s) {
    (void)self; (void)s; id d = objc_autorelease(rt_dict_new(NULL, NULL, 0));
    for (char **e = environ; e && *e; e++) {
        const char *eq = strchr(*e, '='); if (!eq) continue;
        char *k = strndup(*e, (size_t)(eq - *e)); rt_dict_set(d, rt_str(k), rt_str(eq + 1)); free(k);
    }
    return d;
}
static id pi_processName(id self, SEL s) { (void)self; (void)s; const char *p = rt_main_image ? strrchr(rt_main_image->path, '/') : NULL; return rt_str(p ? p + 1 : "app"); }
static int pi_pid(id self, SEL s) { (void)self; (void)s; return getpid(); }
static double pi_uptime(id self, SEL s) { (void)self; (void)s; struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec / 1e9; }
static struct osver pi_osver(id self, SEL s) { (void)self; (void)s; return (struct osver){ 12, 0, 0 }; }
static id pi_osverString(id self, SEL s) { (void)self; (void)s; return rt_str("Version 12.0 (iosrt)"); }
static rt_bool pi_atLeast(id self, SEL s, const struct osver *v) { (void)self; (void)s; return v->major < 12 || (v->major == 12 && v->minor == 0 && v->patch == 0); }
static unsigned long long pi_mem(id self, SEL s) { (void)self; (void)s; return (unsigned long long)sysconf(_SC_PHYS_PAGES) * (unsigned long long)sysconf(_SC_PAGESIZE); }
static unsigned long pi_cpus(id self, SEL s) { (void)self; (void)s; return (unsigned long)sysconf(_SC_NPROCESSORS_ONLN); }
static long pi_zero(id self, SEL s) { (void)self; (void)s; return 0; }

/* ===================================================================== NSDate */
struct nsdate { Class isa; double t; };
static id date_make(Class c, double t) { id r = rt_alloc_instance(c ? c : RT_CLASS(NSDate), 0); ((struct nsdate *)r)->t = t; return objc_autorelease(r); }
static id dt_date(Class c, SEL s) { (void)s; return date_make(c, now_ref()); }
static id dt_sinceNow(Class c, SEL s, double d) { (void)s; return date_make(c, now_ref() + d); }
static id dt_since1970(Class c, SEL s, double d) { (void)s; return date_make(c, d - kRefDate1970); }
static id dt_sinceRef(Class c, SEL s, double d) { (void)s; return date_make(c, d); }
static id dt_distantFuture(Class c, SEL s) { (void)s; return date_make(c, 63113904000.0); }
static id dt_distantPast(Class c, SEL s) { (void)s; return date_make(c, -63114076800.0); }
static double dt_cref(Class c, SEL s) { (void)c; (void)s; return now_ref(); }
static id dt_init(id self, SEL s) { (void)s; ((struct nsdate *)self)->t = now_ref(); return self; }
static double dt_ref(id self, SEL s) { (void)s; return ((struct nsdate *)self)->t; }
static double dt_1970(id self, SEL s) { (void)s; return ((struct nsdate *)self)->t + kRefDate1970; }
static double dt_now(id self, SEL s) { (void)s; return ((struct nsdate *)self)->t - now_ref(); }
static double dt_sinceDate(id self, SEL s, id o) { (void)s; return ((struct nsdate *)self)->t - ((struct nsdate *)o)->t; }
static id dt_adding(id self, SEL s, double d) { (void)s; return date_make(self->isa, ((struct nsdate *)self)->t + d); }
static long dt_compare(id self, SEL s, id o) { (void)s; double a = ((struct nsdate *)self)->t, b = ((struct nsdate *)o)->t; return a < b ? -1 : a > b ? 1 : 0; }
static id dt_description(id self, SEL s) {
    (void)s; time_t t = (time_t)(((struct nsdate *)self)->t + kRefDate1970); struct tm tm; gmtime_r(&t, &tm);
    char b[64]; strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S +0000", &tm); return rt_str(b);
}

/* ===================================================================== NSData */
struct nsdata { Class isa; uint8_t *bytes; size_t len, cap; long mut; long nofree; };
static id data_make(Class c, const void *b, size_t n) {
    id r = rt_alloc_instance(c ? c : RT_CLASS(NSData), 0); struct nsdata *d = (struct nsdata *)r;
    d->bytes = rt_xmalloc(n + 1); if (n && b) memcpy(d->bytes, b, n); else if (n) memset(d->bytes, 0, n);
    d->len = n; d->cap = n + 1;
    return r;
}
id rt_data_new(const void *b, size_t n) { return data_make(NULL, b, n); }
const void *rt_data_bytes(id d, size_t *n) { struct nsdata *x = (struct nsdata *)d; if (n) *n = x->len; return x->bytes; }
static id da_data(Class c, SEL s) { (void)s; return objc_autorelease(data_make(c, NULL, 0)); }
static id da_withBytes(Class c, SEL s, const void *b, unsigned long n) { (void)s; return objc_autorelease(data_make(c, b, n)); }
static id da_withBytesNoCopy(Class c, SEL s, void *b, unsigned long n) { (void)s; id r = data_make(c, b, n); free(b); return objc_autorelease(r); }
static id da_withBytesNoCopyFree(Class c, SEL s, void *b, unsigned long n, long fr) { (void)s; id r = data_make(c, b, n); if (fr & 0xff) free(b); return objc_autorelease(r); }
static id da_withData(Class c, SEL s, id o) { (void)s; struct nsdata *d = (struct nsdata *)o; return objc_autorelease(data_make(c, d ? d->bytes : NULL, d ? d->len : 0)); }
static uint8_t *read_all(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    rt_sb sb; rt_sb_init(&sb); char buf[8192]; size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) rt_sb_append(&sb, buf, k);
    fclose(f); *n = sb.len;
    return (uint8_t *)(sb.buf ? sb.buf : calloc(1, 1));
}
static id da_withContentsOfFile(Class c, SEL s, id path) {
    (void)s; size_t n; uint8_t *b = path ? read_all(rt_str_utf8(path), &n) : NULL;
    if (!b) return NULL;
    id r = data_make(c, b, n); free(b); return objc_autorelease(r);
}
static id da_initWithBytes(id self, SEL s, const void *b, unsigned long n) { (void)s; Class c = self->isa; rt_dealloc_instance(self); return data_make(c, b, n); }
static id da_initWithContentsOfFile(id self, SEL s, id path) { Class c = self->isa; rt_dealloc_instance(self); id r = da_withContentsOfFile(c, s, path); return objc_retain(r); }
static void da_dealloc(id self, SEL s) { (void)s; free(((struct nsdata *)self)->bytes); rt_dealloc_instance(self); }
static const void *da_bytes(id self, SEL s) { (void)s; return ((struct nsdata *)self)->bytes; }
static unsigned long da_length(id self, SEL s) { (void)s; return ((struct nsdata *)self)->len; }
static void da_getBytes(id self, SEL s, void *buf, unsigned long n) { (void)s; struct nsdata *d = (struct nsdata *)self; memcpy(buf, d->bytes, n < d->len ? n : d->len); }
static rt_bool da_write(id self, SEL s, id path, long atomic) {
    (void)s; (void)atomic; struct nsdata *d = (struct nsdata *)self;
    FILE *f = fopen(rt_str_utf8(path), "wb"); if (!f) return 0;
    size_t w = fwrite(d->bytes, 1, d->len, f); fclose(f); return w == d->len;
}
static rt_bool da_isEqual(id self, SEL s, id o) { (void)s; if (!o || !rt_is_kind_of(o, RT_CLASS(NSData))) return 0; struct nsdata *a = (struct nsdata *)self, *b = (struct nsdata *)o; return a->len == b->len && !memcmp(a->bytes, b->bytes, a->len); }
static id da_description(id self, SEL s) {
    (void)s; struct nsdata *d = (struct nsdata *)self; rt_sb sb; rt_sb_init(&sb); rt_sb_putc(&sb, '<');
    for (size_t i = 0; i < d->len; i++) { if (i && i % 4 == 0) rt_sb_putc(&sb, ' '); rt_sb_printf(&sb, "%02x", d->bytes[i]); }
    rt_sb_putc(&sb, '>'); char *t = rt_sb_take(&sb); id r = rt_str(t); free(t); return r;
}
static id da_subdata(id self, SEL s, NSRange r) { (void)s; struct nsdata *d = (struct nsdata *)self; if (r.location + r.length > d->len) rt_guest_exit(73, "-[NSData subdataWithRange:]: range out of bounds"); return objc_autorelease(data_make(RT_CLASS(NSData), d->bytes + r.location, r.length)); }
static id da_copy(id self, SEL s, void *z) { (void)s; (void)z; struct nsdata *d = (struct nsdata *)self; return data_make(RT_CLASS(NSData), d->bytes, d->len); }
static id da_mcopy(id self, SEL s, void *z) { (void)s; (void)z; struct nsdata *d = (struct nsdata *)self; return data_make(RT_CLASS(NSMutableData), d->bytes, d->len); }
static void md_reserve(struct nsdata *d, size_t n) { if (n + 1 > d->cap) { d->cap = (n + 1) * 2; d->bytes = realloc(d->bytes, d->cap); } }
static id md_withLength(Class c, SEL s, unsigned long n) { (void)s; return objc_autorelease(data_make(c, NULL, n)); }
static id md_withCapacity(Class c, SEL s, unsigned long n) { (void)s; id r = data_make(c, NULL, 0); md_reserve((struct nsdata *)r, n); return objc_autorelease(r); }
static void *md_mutableBytes(id self, SEL s) { (void)s; return ((struct nsdata *)self)->bytes; }
static void md_append(id self, SEL s, const void *b, unsigned long n) { (void)s; struct nsdata *d = (struct nsdata *)self; md_reserve(d, d->len + n); memcpy(d->bytes + d->len, b, n); d->len += n; }
static void md_appendData(id self, SEL s, id o) { struct nsdata *x = (struct nsdata *)o; if (x) md_append(self, s, x->bytes, x->len); }
static void md_setLength(id self, SEL s, unsigned long n) { (void)s; struct nsdata *d = (struct nsdata *)self; md_reserve(d, n); if (n > d->len) memset(d->bytes + d->len, 0, n - d->len); d->len = n; }
static void md_increase(id self, SEL s, unsigned long n) { md_setLength(self, s, ((struct nsdata *)self)->len + n); }

/* ===================================================================== plist */
static id plist_obj(const uint8_t *data, size_t len, const uint8_t *offtab, int offsz, int refsz, uint64_t nobj, uint64_t idx, int depth);
static uint64_t be_n(const uint8_t *p, int n) { uint64_t v = 0; for (int i = 0; i < n; i++) v = v << 8 | p[i]; return v; }
static uint64_t bp_count(const uint8_t *data, size_t len, const uint8_t **p, uint8_t low) {
    if (low != 0xf) return low;
    uint8_t m = **p; (*p)++;
    int n = 1 << (m & 0xf);
    if (*p + n > data + len) return 0;
    uint64_t v = be_n(*p, n); *p += n; return v;
}
static id plist_obj(const uint8_t *data, size_t len, const uint8_t *offtab, int offsz, int refsz, uint64_t nobj, uint64_t idx, int depth) {
    if (idx >= nobj || depth > 64) return NULL;
    uint64_t off = be_n(offtab + idx * (uint64_t)offsz, offsz);
    if (off >= len) return NULL;
    const uint8_t *p = data + off;
    uint8_t m = *p++, hi = m >> 4, lo = m & 0xf;
    switch (hi) {
    case 0x0: if (lo == 8 || lo == 9) return RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithBool:", long, lo == 9); return nsnull();
    case 0x1: { int n = 1 << lo; return rt_num_int((long long)be_n(p, n)); }
    case 0x2: { if (lo == 2) { uint32_t v = (uint32_t)be_n(p, 4); float f; memcpy(&f, &v, 4); return rt_num_dbl(f); } uint64_t v = be_n(p, 8); double d; memcpy(&d, &v, 8); return rt_num_dbl(d); }
    case 0x3: { uint64_t v = be_n(p, 8); double d; memcpy(&d, &v, 8); return date_make(NULL, d); }
    case 0x4: { uint64_t n = bp_count(data, len, &p, lo); if (p + n > data + len) return NULL; return objc_autorelease(data_make(NULL, p, n)); }
    case 0x5: { uint64_t n = bp_count(data, len, &p, lo); if (p + n > data + len) return NULL; char *t = strndup((const char *)p, n); id r = rt_str(t); free(t); return r; }
    case 0x6: { uint64_t n = bp_count(data, len, &p, lo); if (p + n * 2 > data + len) return NULL; unichar *u = rt_xmalloc(n * 2 + 2); for (uint64_t i = 0; i < n; i++) u[i] = (unichar)be_n(p + i * 2, 2); id r = objc_autorelease(rt_str_new_utf16(u, n)); free(u); return r; }
    case 0x8: return rt_num_int((long long)be_n(p, lo + 1));
    case 0xa: {
        uint64_t n = bp_count(data, len, &p, lo);
        id a = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array");
        for (uint64_t i = 0; i < n; i++) { id o = plist_obj(data, len, offtab, offsz, refsz, nobj, be_n(p + i * (uint64_t)refsz, refsz), depth + 1); if (o) RT_SEND1(void, a, "addObject:", id, o); }
        return a;
    }
    case 0xd: {
        uint64_t n = bp_count(data, len, &p, lo);
        id d = objc_autorelease(rt_dict_new(NULL, NULL, 0));
        for (uint64_t i = 0; i < n; i++) {
            id k = plist_obj(data, len, offtab, offsz, refsz, nobj, be_n(p + i * (uint64_t)refsz, refsz), depth + 1);
            id v = plist_obj(data, len, offtab, offsz, refsz, nobj, be_n(p + (n + i) * (uint64_t)refsz, refsz), depth + 1);
            if (k && v) rt_dict_set(d, k, v);
        }
        return d;
    }
    default: return NULL;
    }
}
/* XML plist: small recursive-descent parser */
struct xp { const char *p, *e; };
static void xp_ws(struct xp *x) {
    for (;;) {
        while (x->p < x->e && (*x->p == ' ' || *x->p == '\n' || *x->p == '\r' || *x->p == '\t')) x->p++;
        if (x->p + 4 < x->e && !strncmp(x->p, "<!--", 4)) { const char *q = strstr(x->p, "-->"); x->p = q ? q + 3 : x->e; continue; }
        if (x->p + 2 < x->e && (!strncmp(x->p, "<?", 2) || !strncmp(x->p, "<!", 2))) { const char *q = memchr(x->p, '>', (size_t)(x->e - x->p)); x->p = q ? q + 1 : x->e; continue; }
        break;
    }
}
static char *xp_text(struct xp *x, const char *close) {
    const char *q = strstr(x->p, close); if (!q) q = x->e;
    rt_sb sb; rt_sb_init(&sb);
    for (const char *c = x->p; c < q; c++) {
        if (*c == '&') {
            if (!strncmp(c, "&lt;", 4)) { rt_sb_putc(&sb, '<'); c += 3; continue; }
            if (!strncmp(c, "&gt;", 4)) { rt_sb_putc(&sb, '>'); c += 3; continue; }
            if (!strncmp(c, "&amp;", 5)) { rt_sb_putc(&sb, '&'); c += 4; continue; }
            if (!strncmp(c, "&quot;", 6)) { rt_sb_putc(&sb, '"'); c += 5; continue; }
            if (!strncmp(c, "&apos;", 6)) { rt_sb_putc(&sb, '\''); c += 5; continue; }
        }
        rt_sb_putc(&sb, *c);
    }
    x->p = q + strlen(close);
    return rt_sb_take(&sb);
}
static id xp_value(struct xp *x, int depth) {
    xp_ws(x);
    if (x->p >= x->e || *x->p != '<' || depth > 64) return NULL;
    const char *t = x->p + 1;
    if (!strncmp(t, "dict>", 5)) {
        x->p += 6; id d = objc_autorelease(rt_dict_new(NULL, NULL, 0));
        for (;;) {
            xp_ws(x);
            if (!strncmp(x->p, "</dict>", 7)) { x->p += 7; return d; }
            if (strncmp(x->p, "<key>", 5)) return d;
            x->p += 5; char *k = xp_text(x, "</key>");
            id v = xp_value(x, depth + 1);
            if (v) rt_dict_set(d, rt_str(k), v);
            free(k);
        }
    }
    if (!strncmp(t, "dict/>", 6)) { x->p += 7; return objc_autorelease(rt_dict_new(NULL, NULL, 0)); }
    if (!strncmp(t, "array>", 6)) {
        x->p += 7; id a = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array");
        for (;;) {
            xp_ws(x);
            if (!strncmp(x->p, "</array>", 8)) { x->p += 8; return a; }
            id v = xp_value(x, depth + 1); if (!v) return a;
            RT_SEND1(void, a, "addObject:", id, v);
        }
    }
    if (!strncmp(t, "array/>", 7)) { x->p += 8; return RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array"); }
    if (!strncmp(t, "string>", 7)) { x->p += 8; char *s = xp_text(x, "</string>"); id r = rt_str(s); free(s); return r; }
    if (!strncmp(t, "string/>", 8)) { x->p += 9; return rt_str(""); }
    if (!strncmp(t, "integer>", 8)) { x->p += 9; char *s = xp_text(x, "</integer>"); id r = rt_num_int(strtoll(s, NULL, 10)); free(s); return r; }
    if (!strncmp(t, "real>", 5)) { x->p += 6; char *s = xp_text(x, "</real>"); id r = rt_num_dbl(strtod(s, NULL)); free(s); return r; }
    if (!strncmp(t, "true/>", 6)) { x->p += 7; return RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithBool:", long, 1); }
    if (!strncmp(t, "false/>", 7)) { x->p += 8; return RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithBool:", long, 0); }
    if (!strncmp(t, "date>", 5)) { x->p += 6; char *s = xp_text(x, "</date>"); free(s); return date_make(NULL, 0); }
    if (!strncmp(t, "data>", 5)) {
        x->p += 6; char *s = xp_text(x, "</data>");
        static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        rt_sb out; rt_sb_init(&out); uint32_t acc = 0; int bits = 0;
        for (char *c = s; *c; c++) { const char *q = strchr(b64, *c); if (!q || !*c) continue; acc = acc << 6 | (uint32_t)(q - b64); bits += 6; if (bits >= 8) { bits -= 8; rt_sb_putc(&out, (char)(acc >> bits)); } }
        id r = objc_autorelease(data_make(NULL, out.buf, out.len)); free(out.buf); free(s); return r;
    }
    if (!strncmp(t, "plist", 5)) {
        const char *q = memchr(x->p, '>', (size_t)(x->e - x->p)); x->p = q ? q + 1 : x->e;
        return xp_value(x, depth + 1);
    }
    return NULL;
}
/* old-style .strings: "key" = "value"; */
static id strings_parse(const char *s, size_t n) {
    id d = objc_autorelease(rt_dict_new(NULL, NULL, 0));
    const char *p = s, *e = s + n;
    char *parts[2]; int k = 0;
    while (p < e) {
        if (*p == '/' && p + 1 < e && p[1] == '*') { const char *q = strstr(p + 2, "*/"); p = q ? q + 2 : e; continue; }
        if (*p == '/' && p + 1 < e && p[1] == '/') { while (p < e && *p != '\n') p++; continue; }
        if (*p == '"') {
            rt_sb sb; rt_sb_init(&sb); p++;
            while (p < e && *p != '"') {
                if (*p == '\\' && p + 1 < e) { p++; rt_sb_putc(&sb, *p == 'n' ? '\n' : *p == 't' ? '\t' : *p); p++; continue; }
                rt_sb_putc(&sb, *p++);
            }
            p++;
            parts[k++] = rt_sb_take(&sb);
            if (k == 2) { rt_dict_set(d, rt_str(parts[0]), rt_str(parts[1])); free(parts[0]); free(parts[1]); k = 0; }
            continue;
        }
        if (*p == ';') { if (k == 1) { free(parts[0]); k = 0; } }
        p++;
    }
    if (k == 1) free(parts[0]);
    return d;
}
id rt_plist_parse(const uint8_t *data, size_t len) {
    if (len >= 40 && !memcmp(data, "bplist00", 8)) {
        const uint8_t *tr = data + len - 32;
        int offsz = tr[6], refsz = tr[7];
        uint64_t nobj = be_n(tr + 8, 8), top = be_n(tr + 16, 8), offtab = be_n(tr + 24, 8);
        if (offsz < 1 || offsz > 8 || refsz < 1 || refsz > 8 || offtab + nobj * (uint64_t)offsz > len) return NULL;
        id r = plist_obj(data, len, data + offtab, offsz, refsz, nobj, top, 0);
        return objc_retain(r);
    }
    /* UTF-16 .strings files */
    if (len >= 2 && data[0] == 0xff && data[1] == 0xfe) {
        id s = rt_str_new_utf16((const unichar *)(data + 2), (len - 2) / 2);
        const char *u = rt_str_utf8(s);
        id r = objc_retain(strings_parse(u, strlen(u)));
        objc_release(s);
        return r;
    }
    struct xp x = { (const char *)data, (const char *)data + len };
    xp_ws(&x);
    if (x.p < x.e && *x.p == '<') { id r = xp_value(&x, 0); return objc_retain(r); }
    return objc_retain(strings_parse((const char *)data, len));
}

/* ================================================================== NSBundle */
struct nsbundle { Class isa; id path; id info; id strings; };
static id g_main_bundle;
void rt_set_bundle_path(const char *p) { free(rt_bundle_path); rt_bundle_path = p ? rt_xstrdup(p) : NULL; }
static id b_main(Class c, SEL s) {
    (void)s;
    if (!g_main_bundle) {
        g_main_bundle = rt_alloc_instance(c, 0);
        struct nsbundle *b = (struct nsbundle *)g_main_bundle;
        char dir[4096];
        if (rt_bundle_path) snprintf(dir, sizeof dir, "%s", rt_bundle_path);
        else if (rt_main_image) { snprintf(dir, sizeof dir, "%s", rt_main_image->path); char *sl = strrchr(dir, '/'); if (sl) *sl = 0; else strcpy(dir, "."); }
        else strcpy(dir, ".");
        b->path = rt_str_new(dir);
        char ip[4200]; snprintf(ip, sizeof ip, "%s/Info.plist", dir);
        size_t n; uint8_t *d = read_all(ip, &n);
        if (d) { b->info = rt_plist_parse(d, n); free(d); }
        if (!b->info) b->info = rt_dict_new(NULL, NULL, 0);
    }
    return g_main_bundle;
}
static id b_bundlePath(id self, SEL s) { (void)s; return ((struct nsbundle *)self)->path; }
static id b_info(id self, SEL s) { (void)s; return ((struct nsbundle *)self)->info; }
static id b_infoKey(id self, SEL s, id k) { (void)s; return rt_dict_get(((struct nsbundle *)self)->info, k); }
static id b_ident(id self, SEL s) { (void)s; return rt_dict_get(((struct nsbundle *)self)->info, rt_str("CFBundleIdentifier")); }
static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static id b_pathForResource(id self, SEL s, id name, id type) {
    (void)s; const char *dir = rt_str_utf8(((struct nsbundle *)self)->path);
    char fn[1024];
    if (type && RT_SEND0(unsigned long, type, "length")) snprintf(fn, sizeof fn, "%s.%s", rt_str_utf8(name), rt_str_utf8(type));
    else snprintf(fn, sizeof fn, "%s", name ? rt_str_utf8(name) : "");
    const char *subs[] = { "", "Base.lproj/", "en.lproj/", NULL };
    char p[4200];
    for (int i = 0; subs[i]; i++) { snprintf(p, sizeof p, "%s/%s%s", dir, subs[i], fn); if (exists(p)) return rt_str(p); }
    return NULL;
}
static id b_pathForResourceDir(id self, SEL s, id name, id type, id sub) {
    if (!sub) return b_pathForResource(self, s, name, type);
    id n2 = RT_SEND1(id, sub, "stringByAppendingPathComponent:", id, name);
    return b_pathForResource(self, s, n2, type);
}
static id b_URLForResource(id self, SEL s, id name, id ext) { return b_pathForResource(self, s, name, ext); }
static id b_localized(id self, SEL s, id key, id value, id table) {
    (void)s; struct nsbundle *b = (struct nsbundle *)self;
    if (!b->strings) {
        char p[4200]; const char *dir = rt_str_utf8(b->path);
        const char *t = table ? rt_str_utf8(table) : "Localizable";
        snprintf(p, sizeof p, "%s/en.lproj/%s.strings", dir, t);
        size_t n; uint8_t *d = read_all(p, &n);
        if (!d) { snprintf(p, sizeof p, "%s/Base.lproj/%s.strings", dir, t); d = read_all(p, &n); }
        b->strings = d ? rt_plist_parse(d, n) : rt_dict_new(NULL, NULL, 0);
        free(d);
    }
    id v = key ? rt_dict_get(b->strings, key) : NULL;
    if (v) return v;
    if (value && RT_SEND0(unsigned long, value, "length")) return value;
    return key;
}
static id b_resourcePath(id self, SEL s) { return b_bundlePath(self, s); }
static id b_executablePath(id self, SEL s) { (void)self; (void)s; return rt_str(rt_main_image ? rt_main_image->path : ""); }

/* ================================================================ NSException */
struct nsexc { Class isa; id name, reason, userInfo; };
static id ex_make(Class c, id name, id reason, id info) {
    id r = rt_alloc_instance(c, 0); struct nsexc *e = (struct nsexc *)r;
    e->name = objc_retain(name); e->reason = objc_retain(reason); e->userInfo = objc_retain(info); return r;
}
static id ex_withName(Class c, SEL s, id n, id r, id u) { (void)s; return objc_autorelease(ex_make(c, n, r, u)); }
static id ex_initWithName(id self, SEL s, id n, id r, id u) { (void)s; struct nsexc *e = (struct nsexc *)self; e->name = objc_retain(n); e->reason = objc_retain(r); e->userInfo = objc_retain(u); return self; }
extern _Noreturn void objc_exception_throw(id);
id rt_NSException_raiseFormat(Class c, SEL s, id name, id fmt, const uint64_t *args) {
    (void)s; id reason = RT_SEND0(id, (id)RT_CLASS(NSString), "alloc");
    extern id rt_NSString_initWithFormat(id, SEL, id, const uint64_t *);
    reason = rt_NSString_initWithFormat(reason, rt_sel("initWithFormat:"), fmt, args);
    objc_exception_throw(ex_make(c, name, reason, NULL));
}
static void ex_raise(id self, SEL s) { (void)s; objc_exception_throw(self); }
static id ex_name(id self, SEL s) { (void)s; return ((struct nsexc *)self)->name; }
static id ex_reason(id self, SEL s) { (void)s; return ((struct nsexc *)self)->reason; }
static id ex_userInfo(id self, SEL s) { (void)s; return ((struct nsexc *)self)->userInfo; }

/* =================================================================== NSThread */
static id g_main_thread_obj, g_other_thread_obj;
static rt_bool th_isMain(Class c, SEL s) { (void)c; (void)s; return rt_is_guest_main_thread(); }
static rt_bool th_iisMain(id self, SEL s) { (void)s; return self == g_main_thread_obj; }
static id th_current(Class c, SEL s) { (void)s; return rt_is_guest_main_thread() ? singleton(c, &g_main_thread_obj) : singleton(c, &g_other_thread_obj); }
static id th_main(Class c, SEL s) { (void)s; return singleton(c, &g_main_thread_obj); }
static void th_sleep(Class c, SEL s, double t) { (void)c; (void)s; if (t > 0) { struct timespec ts = { (time_t)t, (long)((t - floor(t)) * 1e9) }; nanosleep(&ts, NULL); } }
struct detach { id target; SEL sel; id arg; };
static void *detach_main(void *p) {
    struct detach d = *(struct detach *)p; free(p);
    void *pool = objc_autoreleasePoolPush();
    ((void (*)(id, SEL, id))objc_msgSend)(d.target, d.sel, d.arg);
    objc_autoreleasePoolPop(pool);
    objc_release(d.target); objc_release(d.arg);
    return NULL;
}
static void th_detach(Class c, SEL s, SEL sel, id target, id arg) {
    (void)c; (void)s; struct detach *d = rt_xmalloc(sizeof *d); d->target = objc_retain(target); d->sel = sel; d->arg = objc_retain(arg);
    pthread_t t; pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &a, detach_main, d); pthread_attr_destroy(&a);
}

/* ==================================================== NSTimer / NSRunLoop */
struct nstimer { Class isa; double fire, interval; long repeats, valid; id target; SEL sel; id userInfo; void *block; };
static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;
static id *timers; static size_t ntimers, captimers;
static volatile int runloop_stop;
static void add_timer(id t) {
    pthread_mutex_lock(&timer_lock);
    if (ntimers == captimers) { captimers = captimers ? captimers * 2 : 16; timers = realloc(timers, captimers * sizeof(id)); }
    timers[ntimers++] = objc_retain(t);
    pthread_mutex_unlock(&timer_lock);
}
static id timer_make(Class c, double iv, id target, SEL sel, id info, long rep, void *block) {
    id r = rt_alloc_instance(c ? c : RT_CLASS(NSTimer), 0); struct nstimer *t = (struct nstimer *)r;
    t->interval = iv < 0.0001 ? 0.0001 : iv; t->fire = now_ref() + t->interval; t->repeats = rep & 0xff; t->valid = 1;
    t->target = objc_retain(target); t->sel = sel; t->userInfo = objc_retain(info); t->block = block ? _Block_copy(block) : NULL;
    return objc_autorelease(r);
}
static id tm_scheduled(Class c, SEL s, double iv, id target, SEL sel, id info, long rep) { (void)s; id t = timer_make(c, iv, target, sel, info, rep, NULL); add_timer(t); return t; }
static id tm_scheduledBlock(Class c, SEL s, double iv, long rep, void *blk) { (void)s; id t = timer_make(c, iv, NULL, NULL, NULL, rep, blk); add_timer(t); return t; }
static id tm_timer(Class c, SEL s, double iv, id target, SEL sel, id info, long rep) { (void)s; return timer_make(c, iv, target, sel, info, rep, NULL); }
static void tm_invalidate(id self, SEL s) { (void)s; ((struct nstimer *)self)->valid = 0; }
static rt_bool tm_isValid(id self, SEL s) { (void)s; return ((struct nstimer *)self)->valid; }
static id tm_userInfo(id self, SEL s) { (void)s; return ((struct nstimer *)self)->userInfo; }
static void tm_fire(id self, SEL s) {
    (void)s; struct nstimer *t = (struct nstimer *)self;
    if (t->block) ((void (*)(void *, id))((struct rt_block *)t->block)->invoke)(t->block, self);
    else if (t->target) ((void (*)(id, SEL, id))objc_msgSend)(t->target, t->sel, self);
}
static void tm_dealloc(id self, SEL s) { (void)s; struct nstimer *t = (struct nstimer *)self; objc_release(t->target); objc_release(t->userInfo); if (t->block) _Block_release(t->block); rt_dealloc_instance(self); }
struct perform_ctx { Class isa; id target; SEL sel; id arg; };
void rt_schedule_perform(id target, SEL sel, id arg, double delay) {
    id t = timer_make(NULL, delay, NULL, NULL, NULL, 0, NULL);
    struct nstimer *tm = (struct nstimer *)t;
    tm->target = objc_retain(target); tm->sel = sel; tm->userInfo = objc_retain(arg);
    tm->interval = delay; tm->fire = now_ref() + delay; tm->repeats = -1;   /* -1: perform with userInfo as argument */
    add_timer(t);
}
/* Run one iteration of the main run loop: main queue + due timers. Returns ms until next timer (or -1). */
int rt_runloop_run_once(int max_wait_ms) {
    rt_runloop_drain_main_queue();
    double now = now_ref(), next = 1e18;
    for (;;) {
        id due = NULL;
        pthread_mutex_lock(&timer_lock);
        for (size_t i = 0; i < ntimers; i++) {
            struct nstimer *t = (struct nstimer *)timers[i];
            if (!t->valid) { objc_release(timers[i]); timers[i] = timers[--ntimers]; i--; continue; }
            if (t->fire <= now) { due = objc_retain(timers[i]); break; }
            if (t->fire < next) next = t->fire;
        }
        pthread_mutex_unlock(&timer_lock);
        if (!due) break;
        struct nstimer *t = (struct nstimer *)due;
        void *pool = objc_autoreleasePoolPush();
        if (t->repeats == -1) { t->valid = 0; ((void (*)(id, SEL, id))objc_msgSend)(t->target, t->sel, t->userInfo); }
        else {
            if (t->repeats) { t->fire += t->interval; if (t->fire < now) t->fire = now + t->interval; } else t->valid = 0;
            tm_fire(due, NULL);
        }
        objc_autoreleasePoolPop(pool);
        objc_release(due);
        now = now_ref();
    }
    int wait = max_wait_ms;
    if (next < 1e17) { int ms = (int)((next - now) * 1000.0); if (ms < 0) ms = 0; if (ms < wait) wait = ms; }
    if (wait > 0 && !rt_main_queue_pending()) rt_main_queue_wait(wait);
    rt_runloop_drain_main_queue();
    return next < 1e17 ? (int)((next - now_ref()) * 1000.0) : -1;
}
int rt_runloop_has_sources(void) { pthread_mutex_lock(&timer_lock); size_t n = ntimers; pthread_mutex_unlock(&timer_lock); return n > 0 || rt_main_queue_pending(); }
void rt_runloop_stop(void) { runloop_stop = 1; }
extern volatile int rt_quit_requested;
static void runloop_until(double until) {
    runloop_stop = 0;
    while (!runloop_stop && !rt_quit_requested && now_ref() < until) {
        int ms = (int)((until - now_ref()) * 1000.0); if (ms > 50) ms = 50; if (ms < 0) ms = 0;
        rt_runloop_run_once(ms);
    }
}
static id g_runloop;
static id rl_current(Class c, SEL s) { (void)s; return singleton(c, &g_runloop); }
static void rl_run(id self, SEL s) { (void)self; (void)s; runloop_until(1e18); }
static void rl_runUntilDate(id self, SEL s, id date) { (void)self; (void)s; runloop_until(date ? ((struct nsdate *)date)->t : now_ref()); }
static rt_bool rl_runModeBeforeDate(id self, SEL s, id mode, id date) { (void)self; (void)s; (void)mode; rt_runloop_run_once(date ? (int)((((struct nsdate *)date)->t - now_ref()) * 1000) : 0); return 1; }
static void rl_addTimer(id self, SEL s, id t, id mode) { (void)self; (void)s; (void)mode; add_timer(t); }

/* ============================================================ NSUserDefaults */
struct nsdefaults { Class isa; id dict; };
static id g_defaults;
static id ud_standard(Class c, SEL s) { (void)s; if (!g_defaults) { g_defaults = rt_alloc_instance(c, 0); ((struct nsdefaults *)g_defaults)->dict = rt_dict_new(NULL, NULL, 0); } return g_defaults; }
static id ud_get(id self, SEL s, id k) { (void)s; return rt_dict_get(((struct nsdefaults *)self)->dict, k); }
static void ud_set(id self, SEL s, id v, id k) { (void)s; RT_SEND2(void, ((struct nsdefaults *)self)->dict, "setValue:forKey:", id, v, id, k); }
static long ud_integer(id self, SEL s, id k) { id v = ud_get(self, s, k); return v ? RT_SEND0(long, v, "integerValue") : 0; }
static rt_bool ud_bool(id self, SEL s, id k) { id v = ud_get(self, s, k); return v ? (RT_SEND0(long, v, "boolValue") & 0xff) != 0 : 0; }
static double ud_double(id self, SEL s, id k) { id v = ud_get(self, s, k); return v ? RT_SEND0(double, v, "doubleValue") : 0; }
static float ud_float(id self, SEL s, id k) { return (float)ud_double(self, s, k); }
static void ud_setInteger(id self, SEL s, long v, id k) { ud_set(self, s, rt_num_int(v), k); }
static void ud_setBool(id self, SEL s, long v, id k) { ud_set(self, s, RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithBool:", long, v & 0xff), k); }
static void ud_setDouble(id self, SEL s, double v, id k) { ud_set(self, s, rt_num_dbl(v), k); }
static void ud_setFloat(id self, SEL s, float v, id k) { ud_set(self, s, rt_num_dbl(v), k); }
static void ud_remove(id self, SEL s, id k) { ud_set(self, s, NULL, k); }
static void ud_register(id self, SEL s, id d) {
    (void)s; id keys = RT_SEND0(id, d, "allKeys");
    for (size_t i = 0; i < rt_array_count(keys); i++) { id k = rt_array_at(keys, i); if (!ud_get(self, s, k)) ud_set(self, s, rt_dict_get(d, k), k); }
}
static rt_bool ud_sync(id self, SEL s) { (void)self; (void)s; return 1; }

/* ============================================================== NSFileManager */
static id g_fm;
const char *rt_home_dir(void) {
    static char home[1024];
    if (!home[0]) {
        const char *h = getenv("IOSRT_HOME");
        snprintf(home, sizeof home, "%s", h ? h : "/tmp/iosrt-home");
        mkdir(home, 0755);
        char p[1100];
        snprintf(p, sizeof p, "%s/Documents", home); mkdir(p, 0755);
        snprintf(p, sizeof p, "%s/Library", home); mkdir(p, 0755);
        snprintf(p, sizeof p, "%s/Library/Caches", home); mkdir(p, 0755);
        snprintf(p, sizeof p, "%s/tmp", home); mkdir(p, 0755);
    }
    return home;
}
static id fm_default(Class c, SEL s) { (void)s; return singleton(c, &g_fm); }
static rt_bool fm_exists(id self, SEL s, id path) { (void)self; (void)s; return path && exists(rt_str_utf8(path)); }
static rt_bool fm_existsDir(id self, SEL s, id path, bool *isdir) {
    (void)self; (void)s; struct stat st; if (!path || stat(rt_str_utf8(path), &st)) return 0;
    if (isdir) *isdir = S_ISDIR(st.st_mode); return 1;
}
static id fm_contents(id self, SEL s, id path, id *err) {
    (void)self; (void)s; if (err) *err = NULL;
    DIR *d = opendir(rt_str_utf8(path)); if (!d) return NULL;
    id a = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array"); struct dirent *e;
    while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; RT_SEND1(void, a, "addObject:", id, rt_str(e->d_name)); }
    closedir(d);
    return RT_SEND1(id, a, "sortedArrayUsingSelector:", SEL, rt_sel("compare:"));
}
static int mkdirs(const char *p) {
    char t[4096]; snprintf(t, sizeof t, "%s", p);
    for (char *c = t + 1; *c; c++) if (*c == '/') { *c = 0; mkdir(t, 0755); *c = '/'; }
    return mkdir(t, 0755) == 0 || errno == EEXIST;
}
static rt_bool fm_createDir(id self, SEL s, id path, long inter, id attrs, id *err) {
    (void)self; (void)s; (void)attrs; if (err) *err = NULL;
    return (inter & 0xff) ? mkdirs(rt_str_utf8(path)) : mkdir(rt_str_utf8(path), 0755) == 0;
}
static rt_bool fm_remove(id self, SEL s, id path, id *err) { (void)self; (void)s; if (err) *err = NULL; const char *p = rt_str_utf8(path); return unlink(p) == 0 || rmdir(p) == 0; }
static rt_bool fm_createFile(id self, SEL s, id path, id data, id attrs) {
    (void)self; (void)s; (void)attrs; FILE *f = fopen(rt_str_utf8(path), "wb"); if (!f) return 0;
    if (data) { size_t n; const void *b = rt_data_bytes(data, &n); fwrite(b, 1, n, f); }
    fclose(f); return 1;
}
static id fm_contentsAtPath(id self, SEL s, id path) { (void)self; (void)s; return da_withContentsOfFile(RT_CLASS(NSData), NULL, path); }
id iosrt_NSHomeDirectory(void) { return rt_str(rt_home_dir()); }
id iosrt_NSTemporaryDirectory(void) { char p[1100]; snprintf(p, sizeof p, "%s/tmp/", rt_home_dir()); return rt_str(p); }
id iosrt_NSSearchPathForDirectoriesInDomains(unsigned long dir, unsigned long mask, long expand) {
    (void)mask; (void)expand; char p[1200]; const char *h = rt_home_dir();
    switch (dir) {
    case 9: snprintf(p, sizeof p, "%s/Documents", h); break;
    case 13: snprintf(p, sizeof p, "%s/Library/Caches", h); break;
    case 14: snprintf(p, sizeof p, "%s/Library/Application Support", h); mkdirs(p); break;
    default: snprintf(p, sizeof p, "%s/Library", h); break;
    }
    id s = rt_str(p);
    return objc_autorelease(rt_array_new(&s, 1));
}

/* ======================================================= NSNotificationCenter */
struct nsnote { Class isa; id name, object, userInfo; };
struct observer { id observer; SEL sel; id name; id object; void *block; };
static pthread_mutex_t nc_lock = PTHREAD_MUTEX_INITIALIZER;
static struct observer *obs; static size_t nobs, capobs;
static id g_center;
static id note_make(id name, id object, id info) {
    id r = rt_alloc_instance(RT_CLASS(NSNotification), 0); struct nsnote *n = (struct nsnote *)r;
    n->name = objc_retain(name); n->object = objc_retain(object); n->userInfo = objc_retain(info); return objc_autorelease(r);
}
static id nt_withName(Class c, SEL s, id name, id obj, id info) { (void)c; (void)s; return note_make(name, obj, info); }
static id nt_withName2(Class c, SEL s, id name, id obj) { (void)c; (void)s; return note_make(name, obj, NULL); }
static id nt_name(id self, SEL s) { (void)s; return ((struct nsnote *)self)->name; }
static id nt_object(id self, SEL s) { (void)s; return ((struct nsnote *)self)->object; }
static id nt_userInfo(id self, SEL s) { (void)s; return ((struct nsnote *)self)->userInfo; }
static void nt_dealloc(id self, SEL s) { (void)s; struct nsnote *n = (struct nsnote *)self; objc_release(n->name); objc_release(n->object); objc_release(n->userInfo); rt_dealloc_instance(self); }
static id nc_default(Class c, SEL s) { (void)s; return singleton(c, &g_center); }
static void nc_add(id observer, SEL sel, id name, id object, void *block) {
    pthread_mutex_lock(&nc_lock);
    if (nobs == capobs) { capobs = capobs ? capobs * 2 : 16; obs = realloc(obs, capobs * sizeof *obs); }
    obs[nobs++] = (struct observer){ observer, sel, name ? RT_SEND0(id, name, "copy") : NULL, object, block ? _Block_copy(block) : NULL };
    pthread_mutex_unlock(&nc_lock);
}
static void nc_addObserver(id self, SEL s, id observer, SEL sel, id name, id object) { (void)self; (void)s; nc_add(observer, sel, name, object, NULL); }
static id nc_addBlock(id self, SEL s, id name, id object, id queue, void *block) {
    (void)self; (void)s; (void)queue;
    id token = rt_alloc_instance(RT_CLASS(NSObject), 0);
    nc_add(token, NULL, name, object, block);
    return objc_autorelease(token);
}
static void nc_removeObserverNameObj(id self, SEL s, id observer, id name, id object) {
    (void)self; (void)s;
    pthread_mutex_lock(&nc_lock);
    for (size_t i = 0; i < nobs; i++) {
        struct observer *o = &obs[i];
        if (o->observer != observer) continue;
        if (name && !(o->name && (RT_SEND1(long, o->name, "isEqualToString:", id, name) & 0xff))) continue;
        if (object && o->object != object) continue;
        objc_release(o->name); if (o->block) _Block_release(o->block);
        obs[i] = obs[--nobs]; i--;
    }
    pthread_mutex_unlock(&nc_lock);
}
static void nc_removeObserver(id self, SEL s, id observer) { nc_removeObserverNameObj(self, s, observer, NULL, NULL); }
static void nc_post(id self, SEL s, id note) {
    (void)self; (void)s; struct nsnote *n = (struct nsnote *)note;
    pthread_mutex_lock(&nc_lock);
    size_t cnt = nobs; struct observer *snap = rt_xmalloc((cnt + 1) * sizeof *snap); memcpy(snap, obs, cnt * sizeof *snap);
    pthread_mutex_unlock(&nc_lock);
    for (size_t i = 0; i < cnt; i++) {
        struct observer *o = &snap[i];
        if (o->name && !(n->name && (RT_SEND1(long, o->name, "isEqualToString:", id, n->name) & 0xff))) continue;
        if (o->object && o->object != n->object) continue;
        if (o->block) ((void (*)(void *, id))((struct rt_block *)o->block)->invoke)(o->block, note);
        else ((void (*)(id, SEL, id))objc_msgSend)(o->observer, o->sel, note);
    }
    free(snap);
}
static void nc_postName(id self, SEL s, id name, id obj) { nc_post(self, s, note_make(name, obj, NULL)); }
static void nc_postNameInfo(id self, SEL s, id name, id obj, id info) { nc_post(self, s, note_make(name, obj, info)); }

/* ============================================================ CoreFoundation */
const void *iosrt_CFRetain(const void *o) { return objc_retain((id)o); }
void iosrt_CFRelease(const void *o) { objc_release((id)o); }
const void *iosrt_CFAutorelease(const void *o) { return objc_autorelease((id)o); }
long iosrt_CFGetRetainCount(const void *o) { return rt_retain_count((id)o); }
rt_bool iosrt_CFEqual(const void *a, const void *b) { return a == b || (a && b && (RT_SEND1(long, (id)a, "isEqual:", id, (id)b) & 0xff)); }
unsigned long iosrt_CFHash(const void *o) { return RT_SEND0(unsigned long, (id)o, "hash"); }
const void *iosrt_CFCopyDescription(const void *o) { return objc_retain(RT_SEND0(id, (id)o, "description")); }
void iosrt_CFShow(const void *o) { char *d = rt_describe((id)o); rt_out(2, d, strlen(d)); rt_out(2, "\n", 1); free(d); }
double iosrt_CFAbsoluteTimeGetCurrent(void) { return now_ref(); }
const void *iosrt_CFStringCreateWithCString(const void *alloc, const char *s, uint32_t enc) { (void)alloc; (void)enc; return s ? rt_str_new(s) : NULL; }
rt_bool iosrt_CFStringGetCString(const void *str, char *buf, long size, uint32_t enc) {
    (void)enc; const char *u = rt_str_utf8((id)str); size_t n = strlen(u);
    if ((long)n + 1 > size) return 0; memcpy(buf, u, n + 1); return 1;
}
const char *iosrt_CFStringGetCStringPtr(const void *str, uint32_t enc) { (void)enc; return rt_str_utf8((id)str); }
long iosrt_CFStringGetLength(const void *str) { return RT_SEND0(long, (id)str, "length"); }
const void *iosrt_darwin_CFStringCreateWithFormat(const void *alloc, const void *opts, id fmt, const uint64_t *args) {
    (void)alloc; (void)opts; extern id rt_NSString_initWithFormat(id, SEL, id, const uint64_t *);
    return rt_NSString_initWithFormat(rt_alloc_instance(RT_CLASS(NSString), 0), NULL, fmt, args);
}
const void *iosrt_CFStringCreateWithFormatAndArguments(const void *alloc, const void *opts, id fmt, const uint64_t *va) { return iosrt_darwin_CFStringCreateWithFormat(alloc, opts, fmt, va); }
long iosrt_CFArrayGetCount(const void *a) { return a ? RT_SEND0(long, (id)a, "count") : 0; }
const void *iosrt_CFArrayGetValueAtIndex(const void *a, long i) { return RT_SEND1(id, (id)a, "objectAtIndex:", unsigned long, (unsigned long)i); }
const void *iosrt_CFDictionaryGetValue(const void *d, const void *k) { return rt_dict_get((id)d, (id)k); }
static int dummy_runloop;
void *iosrt_CFRunLoopGetMain(void) { return &dummy_runloop; }
void *iosrt_CFRunLoopGetCurrent(void) { return &dummy_runloop; }
void iosrt_CFRunLoopRun(void) { runloop_until(1e18); }
void iosrt_CFRunLoopStop(void *rl) { (void)rl; runloop_stop = 1; }
int32_t iosrt_CFRunLoopRunInMode(id mode, double seconds, long ret) { (void)mode; (void)ret; runloop_until(now_ref() + seconds); return 3; }
void *iosrt_CFBundleGetMainBundle(void) { return b_main(RT_CLASS(NSBundle), NULL); }
void *iosrt_kCFAllocatorDefault = NULL;
void *iosrt_kCFAllocatorNull = NULL;
id iosrt_NSDefaultRunLoopMode, iosrt_NSRunLoopCommonModes, iosrt_kCFRunLoopDefaultMode, iosrt_kCFRunLoopCommonModes;
id iosrt_NSGenericException, iosrt_NSInvalidArgumentException, iosrt_NSRangeException, iosrt_NSInternalInconsistencyException;
double iosrt_NSFoundationVersionNumber = 1400.0;
double iosrt_kCFCoreFoundationVersionNumber = 1400.0;

void rt_foundation_misc_init(void) {
    Class o = RT_CLASS(NSObject);
#define NEWCLASS(N, SUPER, SZ) rt_native_class(RT_CLASS(N), &iosrt_meta_##N, #N, SUPER, SZ)
    NEWCLASS(NSProcessInfo, o, 0);
    Class pi = RT_CLASS(NSProcessInfo);
    CM(pi, "processInfo", pi_processInfo); IM(pi, "arguments", pi_arguments); IM(pi, "environment", pi_environment);
    IM(pi, "processName", pi_processName); IM(pi, "processIdentifier", pi_pid); IM(pi, "systemUptime", pi_uptime);
    IM(pi, "operatingSystemVersion", pi_osver); IM(pi, "operatingSystemVersionString", pi_osverString);
    IM(pi, "isOperatingSystemAtLeastVersion:", pi_atLeast); IM(pi, "physicalMemory", pi_mem);
    IM(pi, "processorCount", pi_cpus); IM(pi, "activeProcessorCount", pi_cpus); IM(pi, "thermalState", pi_zero);
    IM(pi, "isLowPowerModeEnabled", pi_zero);

    NEWCLASS(NSDate, o, sizeof(double));
    Class dt = RT_CLASS(NSDate);
    CM(dt, "date", dt_date); CM(dt, "dateWithTimeIntervalSinceNow:", dt_sinceNow); CM(dt, "dateWithTimeIntervalSince1970:", dt_since1970);
    CM(dt, "dateWithTimeIntervalSinceReferenceDate:", dt_sinceRef); CM(dt, "distantFuture", dt_distantFuture);
    CM(dt, "distantPast", dt_distantPast); CM(dt, "timeIntervalSinceReferenceDate", dt_cref);
    IM(dt, "init", dt_init); IM(dt, "timeIntervalSinceReferenceDate", dt_ref); IM(dt, "timeIntervalSince1970", dt_1970);
    IM(dt, "timeIntervalSinceNow", dt_now); IM(dt, "timeIntervalSinceDate:", dt_sinceDate);
    IM(dt, "dateByAddingTimeInterval:", dt_adding); IM(dt, "compare:", dt_compare); IM(dt, "description", dt_description);

    NEWCLASS(NSData, o, sizeof(struct nsdata) - sizeof(void *));
    NEWCLASS(NSMutableData, RT_CLASS(NSData), 0);
    Class da = RT_CLASS(NSData), mda = RT_CLASS(NSMutableData);
    CM(da, "data", da_data); CM(da, "dataWithBytes:length:", da_withBytes); CM(da, "dataWithBytesNoCopy:length:", da_withBytesNoCopy);
    CM(da, "dataWithBytesNoCopy:length:freeWhenDone:", da_withBytesNoCopyFree); CM(da, "dataWithData:", da_withData);
    CM(da, "dataWithContentsOfFile:", da_withContentsOfFile);
    IM(da, "initWithBytes:length:", da_initWithBytes); IM(da, "initWithContentsOfFile:", da_initWithContentsOfFile);
    IM(da, "dealloc", da_dealloc); IM(da, "bytes", da_bytes); IM(da, "length", da_length); IM(da, "getBytes:length:", da_getBytes);
    IM(da, "writeToFile:atomically:", da_write); IM(da, "isEqual:", da_isEqual); IM(da, "isEqualToData:", da_isEqual);
    IM(da, "description", da_description); IM(da, "subdataWithRange:", da_subdata);
    IM(da, "copyWithZone:", da_copy); IM(da, "mutableCopyWithZone:", da_mcopy);
    CM(mda, "dataWithLength:", md_withLength); CM(mda, "dataWithCapacity:", md_withCapacity);
    IM(mda, "mutableBytes", md_mutableBytes); IM(mda, "appendBytes:length:", md_append); IM(mda, "appendData:", md_appendData);
    IM(mda, "setLength:", md_setLength); IM(mda, "increaseLengthBy:", md_increase);

    NEWCLASS(NSBundle, o, sizeof(struct nsbundle) - sizeof(void *));
    Class b = RT_CLASS(NSBundle);
    CM(b, "mainBundle", b_main); IM(b, "bundlePath", b_bundlePath); IM(b, "resourcePath", b_resourcePath);
    IM(b, "executablePath", b_executablePath);
    IM(b, "infoDictionary", b_info); IM(b, "objectForInfoDictionaryKey:", b_infoKey); IM(b, "bundleIdentifier", b_ident);
    IM(b, "pathForResource:ofType:", b_pathForResource); IM(b, "pathForResource:ofType:inDirectory:", b_pathForResourceDir);
    IM(b, "URLForResource:withExtension:", b_URLForResource);
    IM(b, "localizedStringForKey:value:table:", b_localized);

    NEWCLASS(NSException, o, sizeof(struct nsexc) - sizeof(void *));
    Class ex = RT_CLASS(NSException);
    extern char iosrt_imp_exceptionRaiseFormat[];
    CM(ex, "exceptionWithName:reason:userInfo:", ex_withName); CM(ex, "raise:format:", iosrt_imp_exceptionRaiseFormat);
    IM(ex, "initWithName:reason:userInfo:", ex_initWithName); IM(ex, "raise", ex_raise);
    IM(ex, "name", ex_name); IM(ex, "reason", ex_reason); IM(ex, "userInfo", ex_userInfo); IM(ex, "description", ex_reason);

    NEWCLASS(NSThread, o, 0);
    Class th = RT_CLASS(NSThread);
    CM(th, "isMainThread", th_isMain); IM(th, "isMainThread", th_iisMain); CM(th, "currentThread", th_current);
    CM(th, "mainThread", th_main); CM(th, "sleepForTimeInterval:", th_sleep);
    CM(th, "detachNewThreadSelector:toTarget:withObject:", th_detach);

    NEWCLASS(NSTimer, o, sizeof(struct nstimer) - sizeof(void *));
    Class tm = RT_CLASS(NSTimer);
    CM(tm, "scheduledTimerWithTimeInterval:target:selector:userInfo:repeats:", tm_scheduled);
    CM(tm, "scheduledTimerWithTimeInterval:repeats:block:", tm_scheduledBlock);
    CM(tm, "timerWithTimeInterval:target:selector:userInfo:repeats:", tm_timer);
    IM(tm, "invalidate", tm_invalidate); IM(tm, "isValid", tm_isValid); IM(tm, "userInfo", tm_userInfo);
    IM(tm, "fire", tm_fire); IM(tm, "dealloc", tm_dealloc);

    NEWCLASS(NSRunLoop, o, 0);
    Class rl = RT_CLASS(NSRunLoop);
    CM(rl, "currentRunLoop", rl_current); CM(rl, "mainRunLoop", rl_current);
    IM(rl, "run", rl_run); IM(rl, "runUntilDate:", rl_runUntilDate); IM(rl, "runMode:beforeDate:", rl_runModeBeforeDate);
    IM(rl, "addTimer:forMode:", rl_addTimer);

    NEWCLASS(NSUserDefaults, o, sizeof(id));
    Class ud = RT_CLASS(NSUserDefaults);
    CM(ud, "standardUserDefaults", ud_standard);
    IM(ud, "objectForKey:", ud_get); IM(ud, "stringForKey:", ud_get); IM(ud, "arrayForKey:", ud_get); IM(ud, "dictionaryForKey:", ud_get);
    IM(ud, "dataForKey:", ud_get); IM(ud, "setObject:forKey:", ud_set); IM(ud, "integerForKey:", ud_integer);
    IM(ud, "boolForKey:", ud_bool); IM(ud, "doubleForKey:", ud_double); IM(ud, "floatForKey:", ud_float);
    IM(ud, "setInteger:forKey:", ud_setInteger); IM(ud, "setBool:forKey:", ud_setBool); IM(ud, "setDouble:forKey:", ud_setDouble);
    IM(ud, "setFloat:forKey:", ud_setFloat); IM(ud, "removeObjectForKey:", ud_remove); IM(ud, "registerDefaults:", ud_register);
    IM(ud, "synchronize", ud_sync);

    NEWCLASS(NSFileManager, o, 0);
    Class fm = RT_CLASS(NSFileManager);
    CM(fm, "defaultManager", fm_default); IM(fm, "fileExistsAtPath:", fm_exists); IM(fm, "fileExistsAtPath:isDirectory:", fm_existsDir);
    IM(fm, "contentsOfDirectoryAtPath:error:", fm_contents);
    IM(fm, "createDirectoryAtPath:withIntermediateDirectories:attributes:error:", fm_createDir);
    IM(fm, "removeItemAtPath:error:", fm_remove); IM(fm, "createFileAtPath:contents:attributes:", fm_createFile);
    IM(fm, "contentsAtPath:", fm_contentsAtPath);

    NEWCLASS(NSNotification, o, sizeof(struct nsnote) - sizeof(void *));
    Class nt = RT_CLASS(NSNotification);
    CM(nt, "notificationWithName:object:userInfo:", nt_withName); CM(nt, "notificationWithName:object:", nt_withName2);
    IM(nt, "name", nt_name); IM(nt, "object", nt_object); IM(nt, "userInfo", nt_userInfo); IM(nt, "dealloc", nt_dealloc);
    NEWCLASS(NSNotificationCenter, o, 0);
    Class nc = RT_CLASS(NSNotificationCenter);
    CM(nc, "defaultCenter", nc_default);
    IM(nc, "addObserver:selector:name:object:", nc_addObserver);
    IM(nc, "addObserverForName:object:queue:usingBlock:", nc_addBlock);
    IM(nc, "removeObserver:", nc_removeObserver); IM(nc, "removeObserver:name:object:", nc_removeObserverNameObj);
    IM(nc, "postNotification:", nc_post); IM(nc, "postNotificationName:object:", nc_postName);
    IM(nc, "postNotificationName:object:userInfo:", nc_postNameInfo);

    iosrt_NSDefaultRunLoopMode = rt_str_new("kCFRunLoopDefaultMode");
    iosrt_kCFRunLoopDefaultMode = iosrt_NSDefaultRunLoopMode;
    iosrt_NSRunLoopCommonModes = rt_str_new("kCFRunLoopCommonModes");
    iosrt_kCFRunLoopCommonModes = iosrt_NSRunLoopCommonModes;
    iosrt_NSGenericException = rt_str_new("NSGenericException");
    iosrt_NSInvalidArgumentException = rt_str_new("NSInvalidArgumentException");
    iosrt_NSRangeException = rt_str_new("NSRangeException");
    iosrt_NSInternalInconsistencyException = rt_str_new("NSInternalInconsistencyException");
}
