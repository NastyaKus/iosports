/*
 * Foundation HLE, part 6: NSPredicate (block predicates; format predicates are
 * accepted but not parsed), NSCharacterSet with real membership, NSURLComponents,
 * NSURLQueryItem, NSURLConnection (offline: fails asynchronously), and small
 * additions found by tracing real apps.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")
#define NEWCLASS(N, SUPER, SZ) rt_native_class(RT_CLASS(N), &iosrt_meta_##N, #N, SUPER, SZ)

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSString);
RT_DECLARE_CLASS(NSArray);
RT_DECLARE_CLASS(NSMutableArray);
RT_DECLARE_CLASS(NSDictionary);
RT_DECLARE_CLASS(NSMutableDictionary);
RT_DECLARE_CLASS(NSCharacterSet);
RT_DECLARE_CLASS(NSURL);
RT_DEFINE_CLASS(NSPredicate);
RT_DEFINE_CLASS(NSMutableCharacterSet);
RT_DEFINE_CLASS(NSURLComponents);
RT_DEFINE_CLASS(NSURLQueryItem);
RT_DEFINE_CLASS(NSURLConnection);
RT_DEFINE_CLASS(NSURLResponse);
RT_DEFINE_CLASS(NSHTTPURLResponse);

id rt_dict_new(id *vals, id *keys, size_t n);
void rt_soft_error(const char *msg);
int rt_first_time(const char *key);
static rt_bool pred_eval_any(id pred, id obj);
static id RT_SEND3_err(void);
static void rt_schedule_perform_self(id self);

/* ================================================================== NSPredicate */
struct nspred { Class isa; void *block; id format; };
static id pr_withBlock(Class c, SEL s, void *b) { (void)s; id o = rt_alloc_instance(c, 0); ((struct nspred *)o)->block = _Block_copy(b); return objc_autorelease(o); }
id rt_NSPredicate_predicateWithFormat(Class c, SEL s, id fmt, const uint64_t *args) {
    (void)s; (void)args; id o = rt_alloc_instance(c, 0); ((struct nspred *)o)->format = objc_retain(fmt);
    char key[300]; snprintf(key, sizeof key, "NSPredicate format '%s'", fmt ? rt_str_utf8(fmt) : "");
    if (rt_first_time(key)) rt_warn("warning: %s is not evaluated (always true)", key);
    return objc_autorelease(o);
}
static id pr_withValue(Class c, SEL s, long v) { (void)s; id o = rt_alloc_instance(c, 0); ((struct nspred *)o)->format = objc_retain(rt_str(v & 0xff ? "TRUEPREDICATE" : "FALSEPREDICATE")); return objc_autorelease(o); }
static rt_bool pr_eval(id self, SEL s, id obj) {
    (void)s; struct nspred *p = (struct nspred *)self;
    if (p->block) return (((long (*)(void *, id, id))((struct rt_block *)p->block)->invoke)(p->block, obj, NULL) & 0xff) != 0;
    return !(p->format && !strcmp(rt_str_utf8(p->format), "FALSEPREDICATE"));
}
static rt_bool pr_evalBindings(id self, SEL s, id obj, id b) { (void)b; return pr_eval(self, s, obj); }
static id pr_format(id self, SEL s) { (void)s; return ((struct nspred *)self)->format; }
static id arr_filtered(id self, SEL s, id pred) {
    (void)s; id r = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array");
    for (size_t i = 0; i < rt_array_count(self); i++) { id o = rt_array_at(self, i); if (pred_eval_any(pred, o)) RT_SEND1(void, r, "addObject:", id, o); }
    return r;
}
static void marr_filter(id self, SEL s, id pred) {
    id keep = arr_filtered(self, s, pred);
    RT_SEND0(void, self, "removeAllObjects"); RT_SEND1(void, self, "addObjectsFromArray:", id, keep);
}

/* ============================================================== NSCharacterSet */
/* membership bitmap over the BMP, plus an "inverted" flag */
struct nscs { Class isa; uint8_t *bits; long inverted; };
static int cs_has(struct nscs *c, unsigned ch) { int in = ch < 65536 && c->bits && (c->bits[ch >> 3] >> (ch & 7) & 1); return c->inverted ? !in : in; }
static void cs_add(struct nscs *c, unsigned ch) { if (ch < 65536) c->bits[ch >> 3] |= (uint8_t)(1 << (ch & 7)); }
static id cs_make(Class c, int (*pred)(int), const char *extra) {
    id o = rt_alloc_instance(c ? c : RT_CLASS(NSCharacterSet), 0); struct nscs *x = (struct nscs *)o;
    x->bits = rt_xcalloc(1, 8192);
    if (pred) for (int ch = 0; ch < 128; ch++) if (pred(ch)) cs_add(x, (unsigned)ch);
    if (pred == isalpha || pred == isalnum) for (unsigned ch = 0xc0; ch < 0x2000; ch++) cs_add(x, ch);   /* rough Latin/Cyrillic letters */
    if (extra) for (const unsigned char *p = (const unsigned char *)extra; *p; p++) cs_add(x, *p);
    return objc_autorelease(o);
}
static int is_ws_nl(int c) { return isspace(c); }
static int is_ws(int c) { return c == ' ' || c == '\t'; }
static int is_nl(int c) { return c == '\n' || c == '\r'; }
static int is_query_allowed(int c) { return isalnum(c) || strchr("-._~!$&'()*+,;=:@/?", c) != NULL; }
static int is_path_allowed(int c) { return isalnum(c) || strchr("-._~!$&'()*+,;=:@/", c) != NULL; }
static int is_host_allowed(int c) { return isalnum(c) || strchr("-._~!$&'()*+,;=:[]", c) != NULL; }
static int is_frag_allowed(int c) { return is_query_allowed(c); }
static int is_user_allowed(int c) { return isalnum(c) || strchr("-._~!$&'()*+,;=", c) != NULL; }
#define CSF(name, pred) static id cs_##name(Class c, SEL s) { (void)s; return cs_make(c, pred, NULL); }
CSF(whitespace, is_ws) CSF(wsnl, is_ws_nl) CSF(newline, is_nl) CSF(alnum, isalnum) CSF(letters, isalpha) CSF(digits, isdigit)
CSF(punct, ispunct) CSF(upper, isupper) CSF(lower, islower) CSF(control, iscntrl) CSF(query, is_query_allowed) CSF(path, is_path_allowed)
CSF(host, is_host_allowed) CSF(frag, is_frag_allowed) CSF(user, is_user_allowed)
static id cs_withString(Class c, SEL s, id str) {
    (void)s; id o = cs_make(c, NULL, NULL); struct nscs *x = (struct nscs *)o;
    if (str) { size_t n; unichar *tf; const unichar *u = rt_str_utf16(str, &n, &tf); for (size_t i = 0; i < n; i++) cs_add(x, u[i]); free(tf); }
    return o;
}
static id cs_withRange(Class c, SEL s, NSRange r) { (void)s; id o = cs_make(c, NULL, NULL); for (unsigned long i = 0; i < r.length; i++) cs_add((struct nscs *)o, (unsigned)(r.location + i)); return o; }
static rt_bool cs_isMember(id self, SEL s, unsigned long ch) { (void)s; return cs_has((struct nscs *)self, (unsigned)ch); }
static id cs_inverted(id self, SEL s) {
    (void)s; struct nscs *a = (struct nscs *)self;
    id o = cs_make(RT_CLASS(NSCharacterSet), NULL, NULL); struct nscs *b = (struct nscs *)o;
    memcpy(b->bits, a->bits, 8192); b->inverted = !a->inverted; return o;
}
static void cs_addString(id self, SEL s, id str) { (void)s; size_t n; unichar *tf; const unichar *u = rt_str_utf16(str, &n, &tf); for (size_t i = 0; i < n; i++) cs_add((struct nscs *)self, u[i]); free(tf); }
static void cs_removeString(id self, SEL s, id str) { (void)s; struct nscs *x = (struct nscs *)self; size_t n; unichar *tf; const unichar *u = rt_str_utf16(str, &n, &tf); for (size_t i = 0; i < n; i++) x->bits[u[i] >> 3] &= (uint8_t)~(1 << (u[i] & 7)); free(tf); }
static void cs_formUnion(id self, SEL s, id other) { (void)s; struct nscs *a = (struct nscs *)self, *b = (struct nscs *)other; for (unsigned ch = 0; ch < 65536; ch++) if (cs_has(b, ch)) cs_add(a, ch); }
static rt_bool cs_isSuperset(id self, SEL s, id other) { (void)s; struct nscs *a = (struct nscs *)self, *b = (struct nscs *)other; for (unsigned ch = 0; ch < 65536; ch++) if (cs_has(b, ch) && !cs_has(a, ch)) return 0; return 1; }
static void cs_addRange(id self, SEL s, NSRange r) { (void)s; for (unsigned long i = 0; i < r.length; i++) cs_add((struct nscs *)self, (unsigned)(r.location + i)); }
static id cs_copy(id self, SEL s, void *z) { (void)z; return cs_inverted(cs_inverted(self, s), s); }
static id cs_mcopy(id self, SEL s, void *z) { (void)s; (void)z; struct nscs *a = (struct nscs *)self; id o = cs_make(RT_CLASS(NSMutableCharacterSet), NULL, NULL); memcpy(((struct nscs *)o)->bits, a->bits, 8192); ((struct nscs *)o)->inverted = a->inverted; return objc_retain(o); }
/* string operations that take a character set */
static id str_trimSet(id self, SEL s, id set) {
    (void)s; struct nscs *x = (struct nscs *)set; size_t n; unichar *tf; const unichar *u = rt_str_utf16(self, &n, &tf);
    size_t b = 0, e = n; while (b < e && cs_has(x, u[b])) b++; while (e > b && cs_has(x, u[e - 1])) e--;
    id r = objc_autorelease(rt_str_new_utf16(u + b, e - b)); free(tf); return r;
}
static NSRange str_rangeOfSet(id self, SEL s, id set) {
    (void)s; struct nscs *x = (struct nscs *)set; size_t n; unichar *tf; const unichar *u = rt_str_utf16(self, &n, &tf);
    NSRange r = { 0x7fffffffffffffffUL, 0 };
    for (size_t i = 0; i < n; i++) if (cs_has(x, u[i])) { r.location = i; r.length = 1; break; }
    free(tf); return r;
}
static id str_splitBySet(id self, SEL s, id set) {
    (void)s; struct nscs *x = (struct nscs *)set; size_t n; unichar *tf; const unichar *u = rt_str_utf16(self, &n, &tf);
    id a = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array"); size_t st = 0;
    for (size_t i = 0; i <= n; i++) if (i == n || cs_has(x, u[i])) { id part = rt_str_new_utf16(u + st, i - st); RT_SEND1(void, a, "addObject:", id, part); objc_release(part); st = i + 1; }
    free(tf); return a;
}
static id str_percentAllowed(id self, SEL s, id set) {
    (void)s; struct nscs *x = (struct nscs *)set; const unsigned char *p = (const unsigned char *)rt_str_utf8(self); rt_sb sb; rt_sb_init(&sb);
    for (; *p; p++) { if (*p < 128 && cs_has(x, *p)) rt_sb_putc(&sb, (char)*p); else rt_sb_printf(&sb, "%%%02X", *p); }
    char *t = rt_sb_take(&sb); id r = rt_str(t); free(t); return r;
}
static long str_compareOpts(id self, SEL s, id o, unsigned long opts) {
    (void)s;
    if (opts & 64) {   /* NSNumericSearch */
        const char *a = rt_str_utf8(self), *b = o ? rt_str_utf8(o) : "";
        while (*a && *b) {
            if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
                long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
                if (x != y) return x < y ? -1 : 1;
            } else { int ca = (opts & 1) ? tolower((unsigned char)*a) : (unsigned char)*a, cb = (opts & 1) ? tolower((unsigned char)*b) : (unsigned char)*b; if (ca != cb) return ca < cb ? -1 : 1; a++; b++; }
        }
        return *a ? 1 : *b ? -1 : 0;
    }
    return (opts & 1) ? RT_SEND1(long, self, "caseInsensitiveCompare:", id, o) : RT_SEND1(long, self, "compare:", id, o);
}
static long str_compareOptsRange(id self, SEL s, id o, unsigned long opts, NSRange r) { return str_compareOpts(RT_SEND1(id, self, "substringWithRange:", NSRange, r), s, o, opts); }
static id str_initContentsUsed(id self, SEL s, id path, unsigned long *enc, id *err) {
    (void)s; objc_release(self); if (enc) *enc = 4;
    return objc_retain(((id (*)(id, SEL, id, unsigned long, id *))objc_msgSend)((id)RT_CLASS(NSString), rt_sel("stringWithContentsOfFile:encoding:error:"), path, 4, err));
}

/* ============================================================= NSURLComponents */
struct nsuc { Class isa; id scheme, host, path, query, fragment; id port; };
static void uc_set(id *slot, id v) { id c = v ? RT_SEND0(id, v, "copy") : NULL; objc_release(*slot); *slot = c; }
static id uc_parse(Class c, const char *u) {
    id o = rt_alloc_instance(c, 0); struct nsuc *x = (struct nsuc *)o;
    const char *p = u, *sch = strstr(u, "://");
    if (sch) { char *t = strndup(u, (size_t)(sch - u)); x->scheme = rt_str_new(t); free(t); p = sch + 3;
        size_t hl = strcspn(p, "/?#"); char *h = strndup(p, hl); char *colon = strrchr(h, ':');
        if (colon) { x->port = objc_retain(rt_num_int(atoi(colon + 1))); *colon = 0; }
        x->host = rt_str_new(h); free(h); p += hl; }
    size_t pl = strcspn(p, "?#"); char *pa = strndup(p, pl); x->path = rt_str_new(pa); free(pa); p += pl;
    if (*p == '?') { p++; size_t ql = strcspn(p, "#"); char *q = strndup(p, ql); x->query = rt_str_new(q); free(q); p += ql; }
    if (*p == '#') x->fragment = rt_str_new(p + 1);
    return o;
}
static id ucm_withString(Class c, SEL s, id str) { (void)s; return str ? objc_autorelease(uc_parse(c, rt_str_utf8(str))) : NULL; }
static id ucm_withURL(Class c, SEL s, id url, long res) { (void)res; return ucm_withString(c, s, url ? RT_SEND0(id, url, "absoluteString") : NULL); }
static id uc_init(id self, SEL s) { (void)s; return self; }
static id uc_initWithString(id self, SEL s, id str) { Class c = self->isa; (void)s; rt_dealloc_instance(self); return str ? uc_parse(c, rt_str_utf8(str)) : NULL; }
static id uc_initWithURL(id self, SEL s, id url, long r) { (void)r; return uc_initWithString(self, s, url ? RT_SEND0(id, url, "absoluteString") : NULL); }
static id uc_string(id self, SEL s) {
    (void)s; struct nsuc *x = (struct nsuc *)self; rt_sb sb; rt_sb_init(&sb);
    if (x->scheme) rt_sb_printf(&sb, "%s://", rt_str_utf8(x->scheme));
    if (x->host) rt_sb_puts(&sb, rt_str_utf8(x->host));
    if (x->port) rt_sb_printf(&sb, ":%ld", RT_SEND0(long, x->port, "integerValue"));
    if (x->path) rt_sb_puts(&sb, rt_str_utf8(x->path));
    if (x->query) rt_sb_printf(&sb, "?%s", rt_str_utf8(x->query));
    if (x->fragment) rt_sb_printf(&sb, "#%s", rt_str_utf8(x->fragment));
    char *t = rt_sb_take(&sb); id r = rt_str(t); free(t); return r;
}
static id uc_URL(id self, SEL s) { return RT_SEND1(id, (id)RT_CLASS(NSURL), "URLWithString:", id, uc_string(self, s)); }
#define UCPROP(n, N, f) static id uc_get_##n(id self, SEL s) { (void)s; return ((struct nsuc *)self)->f; } \
    static void uc_set_##n(id self, SEL s, id v) { (void)s; uc_set(&((struct nsuc *)self)->f, v); }
UCPROP(scheme, Scheme, scheme) UCPROP(host, Host, host) UCPROP(path, Path, path) UCPROP(query, Query, query)
UCPROP(fragment, Fragment, fragment) UCPROP(port, Port, port)
struct nsqi { Class isa; id name, value; };
static id qi_make(Class c, SEL s, id n, id v) { (void)s; id o = rt_alloc_instance(c, 0); struct nsqi *q = (struct nsqi *)o; q->name = objc_retain(n); q->value = objc_retain(v); return objc_autorelease(o); }
static id qi_init(id self, SEL s, id n, id v) { (void)s; struct nsqi *q = (struct nsqi *)self; q->name = objc_retain(n); q->value = objc_retain(v); return self; }
static id qi_name(id self, SEL s) { (void)s; return ((struct nsqi *)self)->name; }
static id qi_value(id self, SEL s) { (void)s; return ((struct nsqi *)self)->value; }
static id uc_queryItems(id self, SEL s) {
    (void)s; struct nsuc *x = (struct nsuc *)self; if (!x->query) return NULL;
    id items = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array");
    id parts = RT_SEND1(id, x->query, "componentsSeparatedByString:", id, rt_str("&"));
    for (size_t i = 0; i < rt_array_count(parts); i++) {
        const char *kv = rt_str_utf8(rt_array_at(parts, i)); const char *eq = strchr(kv, '=');
        char *k = eq ? strndup(kv, (size_t)(eq - kv)) : strdup(kv);
        RT_SEND1(void, items, "addObject:", id, qi_make(RT_CLASS(NSURLQueryItem), NULL, rt_str(k), eq ? rt_str(eq + 1) : NULL)); free(k);
    }
    return items;
}
static void uc_setQueryItems(id self, SEL s, id items) {
    (void)s; struct nsuc *x = (struct nsuc *)self;
    if (!items) { uc_set(&x->query, NULL); return; }
    rt_sb sb; rt_sb_init(&sb);
    for (size_t i = 0; i < rt_array_count(items); i++) {
        struct nsqi *q = (struct nsqi *)rt_array_at(items, i);
        if (i) rt_sb_putc(&sb, '&');
        rt_sb_puts(&sb, rt_str_utf8(q->name));
        if (q->value) { rt_sb_putc(&sb, '='); rt_sb_puts(&sb, rt_str_utf8(RT_SEND1(id, q->value, "stringByAddingPercentEscapesUsingEncoding:", unsigned long, 4))); }
    }
    char *t = rt_sb_take(&sb); uc_set(&x->query, rt_str(t)); free(t);
}

/* ================================================================ NSURLConnection */
/* No network stack yet: connections fail like an offline device (NSURLErrorNotConnectedToInternet). */
struct nsconn { Class isa; id delegate; id request; };
static id offline_error(void) { return RT_SEND3_err(); }
static void conn_fail(id self) {
    struct nsconn *c = (struct nsconn *)self;
    if (c->delegate && rt_responds(c->delegate, rt_sel("connection:didFailWithError:")))
        RT_SEND2(void, c->delegate, "connection:didFailWithError:", id, self, id, offline_error());
}
static void conn_start(id self, SEL s) {
    (void)s; objc_retain(self);
    rt_schedule_perform_self(self);
}
static id conn_init(id self, SEL s, id req, id del, long start) {
    struct nsconn *c = (struct nsconn *)self; c->request = objc_retain(req); c->delegate = del;
    if (start & 0xff) conn_start(self, s);
    return self;
}
static id conn_init2(id self, SEL s, id req, id del) { return conn_init(self, s, req, del, 1); }
static id conn_with(Class c, SEL s, id req, id del) { return objc_autorelease(conn_init2(rt_alloc_instance(c, 0), s, req, del)); }
static void conn_cancel(id self, SEL s) { (void)s; ((struct nsconn *)self)->delegate = NULL; }
static void conn_noop(id self, SEL s, id a, id b) { (void)self; (void)s; (void)a; (void)b; }
static void conn_sendAsync(Class c, SEL s, id req, id q, void *blk) {
    (void)c; (void)s; (void)req; (void)q;
    if (blk) ((void (*)(void *, id, id, id))((struct rt_block *)blk)->invoke)(blk, NULL, NULL, offline_error());
}
static id conn_sendSync(Class c, SEL s, id req, id *resp, id *err) { (void)c; (void)s; (void)req; if (resp) *resp = NULL; if (err) *err = offline_error(); return NULL; }
static void conn_fire(id self, SEL s) { (void)s; conn_fail(self); objc_release(self); }

/* forward declarations used above */
static rt_bool pred_eval_any(id pred, id obj) { return pred ? (RT_SEND1(long, pred, "evaluateWithObject:", id, obj) & 0xff) != 0 : 1; }
static id RT_SEND3_err(void) {
    return RT_SEND3(id, (id)rt_class_named("NSError"), "errorWithDomain:code:userInfo:", id, rt_str("NSURLErrorDomain"), long, -1009, id, NULL);
}
void rt_schedule_perform(id target, SEL sel, id arg, double delay);
static void rt_schedule_perform_self(id self) { rt_schedule_perform(self, rt_sel("iosrt_connFire"), NULL, 0.01); }

void rt_foundation_more3_init(void) {
    Class o = RT_CLASS(NSObject);
    NEWCLASS(NSPredicate, o, sizeof(struct nspred) - sizeof(void *));
    Class pr = RT_CLASS(NSPredicate);
    extern char iosrt_imp_predicateWithFormat[];
    CM(pr, "predicateWithBlock:", pr_withBlock); CM(pr, "predicateWithFormat:", iosrt_imp_predicateWithFormat);
    CM(pr, "predicateWithValue:", pr_withValue);
    IM(pr, "evaluateWithObject:", pr_eval); IM(pr, "evaluateWithObject:substitutionVariables:", pr_evalBindings);
    IM(pr, "predicateFormat", pr_format);
    IM(RT_CLASS(NSArray), "filteredArrayUsingPredicate:", arr_filtered); IM(RT_CLASS(NSMutableArray), "filterUsingPredicate:", marr_filter);

    /* NSCharacterSet gets real storage (it was declared with no ivars in foundation.c) */
    rt_cls(RT_CLASS(NSCharacterSet))->instance_size = sizeof(struct nscs);
    NEWCLASS(NSMutableCharacterSet, RT_CLASS(NSCharacterSet), 0);
    Class cs = RT_CLASS(NSCharacterSet), mcs = RT_CLASS(NSMutableCharacterSet);
    CM(cs, "whitespaceCharacterSet", cs_whitespace); CM(cs, "whitespaceAndNewlineCharacterSet", cs_wsnl);
    CM(cs, "newlineCharacterSet", cs_newline); CM(cs, "alphanumericCharacterSet", cs_alnum); CM(cs, "letterCharacterSet", cs_letters);
    CM(cs, "decimalDigitCharacterSet", cs_digits); CM(cs, "punctuationCharacterSet", cs_punct);
    CM(cs, "uppercaseLetterCharacterSet", cs_upper); CM(cs, "lowercaseLetterCharacterSet", cs_lower); CM(cs, "controlCharacterSet", cs_control);
    CM(cs, "URLQueryAllowedCharacterSet", cs_query); CM(cs, "URLPathAllowedCharacterSet", cs_path); CM(cs, "URLHostAllowedCharacterSet", cs_host);
    CM(cs, "URLFragmentAllowedCharacterSet", cs_frag); CM(cs, "URLUserAllowedCharacterSet", cs_user); CM(cs, "URLPasswordAllowedCharacterSet", cs_user);
    CM(cs, "characterSetWithCharactersInString:", cs_withString); CM(cs, "characterSetWithRange:", cs_withRange);
    IM(cs, "characterIsMember:", cs_isMember); IM(cs, "longCharacterIsMember:", cs_isMember); IM(cs, "invertedSet", cs_inverted); IM(cs, "isSupersetOfSet:", cs_isSuperset);
    IM(cs, "copyWithZone:", cs_copy); IM(cs, "mutableCopyWithZone:", cs_mcopy);
    IM(mcs, "addCharactersInString:", cs_addString); IM(mcs, "removeCharactersInString:", cs_removeString);
    IM(mcs, "formUnionWithCharacterSet:", cs_formUnion); IM(mcs, "addCharactersInRange:", cs_addRange);
    Class st = RT_CLASS(NSString);
    IM(st, "stringByTrimmingCharactersInSet:", str_trimSet); IM(st, "rangeOfCharacterFromSet:", str_rangeOfSet);
    IM(st, "componentsSeparatedByCharactersInSet:", str_splitBySet);
    IM(st, "stringByAddingPercentEncodingWithAllowedCharacters:", str_percentAllowed);
    IM(st, "compare:options:", str_compareOpts); IM(st, "compare:options:range:", str_compareOptsRange);
    IM(st, "initWithContentsOfFile:usedEncoding:error:", str_initContentsUsed);

    NEWCLASS(NSURLComponents, o, sizeof(struct nsuc) - sizeof(void *));
    Class uc = RT_CLASS(NSURLComponents);
    CM(uc, "componentsWithString:", ucm_withString); CM(uc, "componentsWithURL:resolvingAgainstBaseURL:", ucm_withURL);
    IM(uc, "init", uc_init); IM(uc, "initWithString:", uc_initWithString); IM(uc, "initWithURL:resolvingAgainstBaseURL:", uc_initWithURL);
    IM(uc, "string", uc_string); IM(uc, "URL", uc_URL); IM(uc, "queryItems", uc_queryItems); IM(uc, "setQueryItems:", uc_setQueryItems);
#define UCREG(n, N) IM(uc, #n, uc_get_##n); IM(uc, "set" #N ":", uc_set_##n); IM(uc, "percentEncoded" #N, uc_get_##n);
    UCREG(scheme, Scheme) UCREG(host, Host) UCREG(path, Path) UCREG(query, Query) UCREG(fragment, Fragment) UCREG(port, Port)
    NEWCLASS(NSURLQueryItem, o, sizeof(struct nsqi) - sizeof(void *));
    CM(RT_CLASS(NSURLQueryItem), "queryItemWithName:value:", qi_make); IM(RT_CLASS(NSURLQueryItem), "initWithName:value:", qi_init);
    IM(RT_CLASS(NSURLQueryItem), "name", qi_name); IM(RT_CLASS(NSURLQueryItem), "value", qi_value);

    NEWCLASS(NSURLConnection, o, sizeof(struct nsconn) - sizeof(void *));
    Class cn = RT_CLASS(NSURLConnection);
    CM(cn, "connectionWithRequest:delegate:", conn_with); CM(cn, "sendAsynchronousRequest:queue:completionHandler:", conn_sendAsync);
    CM(cn, "sendSynchronousRequest:returningResponse:error:", conn_sendSync);
    IM(cn, "initWithRequest:delegate:startImmediately:", conn_init); IM(cn, "initWithRequest:delegate:", conn_init2);
    IM(cn, "start", conn_start); IM(cn, "cancel", conn_cancel); IM(cn, "scheduleInRunLoop:forMode:", conn_noop);
    IM(cn, "setDelegateQueue:", conn_noop); IM(cn, "iosrt_connFire", conn_fire);
    NEWCLASS(NSURLResponse, o, 0);
    NEWCLASS(NSHTTPURLResponse, RT_CLASS(NSURLResponse), 0);
}
