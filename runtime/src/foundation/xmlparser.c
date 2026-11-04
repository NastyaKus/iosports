/*
 * NSXMLParser: an event-driven (SAX-style) XML parser with the Foundation
 * delegate protocol. Covers what app configuration and data files use:
 * elements and attributes, text with entity and character references,
 * CDATA, comments, processing instructions, DOCTYPE (skipped), namespace
 * processing with prefix mapping, well-formedness errors (mismatched or
 * unclosed tags) reported through parser:parseErrorOccurred: and
 * abortParsing from a delegate.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
RT_DECLARE_CLASS(NSObject);
RT_DEFINE_CLASS(NSXMLParser);

struct xmlp {
    Class isa; id delegate, data, error;
    long ns, report_prefixes, resolve_ext;
    long line, col; int aborted;
};
#define X(x) ((struct xmlp *)(x))

static id xp_initData(id self, SEL s, id data) { (void)s; X(self)->data = objc_retain(data); return self; }
static id xp_initURL(id self, SEL s, id url) {
    (void)s;
    id path = url && rt_responds(url, rt_sel("path")) ? RT_SEND0(id, url, "path") : NULL;
    id data = path ? RT_SEND1(id, (id)rt_class_named("NSData"), "dataWithContentsOfFile:", id, path) : NULL;
    if (!data) { objc_release(self); return NULL; }
    X(self)->data = objc_retain(data);
    return self;
}
static void xp_dealloc(id self, SEL s) { (void)s; objc_release(X(self)->data); objc_release(X(self)->error); rt_dealloc_instance(self); }
static id xp_delegate(id self, SEL s) { (void)s; return X(self)->delegate; }
static void xp_setDelegate(id self, SEL s, id d) { (void)s; X(self)->delegate = d; }
static void xp_setNS(id self, SEL s, long v) { (void)s; X(self)->ns = v & 0xff; }
static rt_bool xp_ns(id self, SEL s) { (void)s; return X(self)->ns != 0; }
static void xp_setReport(id self, SEL s, long v) { (void)s; X(self)->report_prefixes = v & 0xff; }
static rt_bool xp_report(id self, SEL s) { (void)s; return X(self)->report_prefixes != 0; }
static void xp_setResolve(id self, SEL s, long v) { (void)s; X(self)->resolve_ext = v & 0xff; }
static rt_bool xp_resolve(id self, SEL s) { (void)s; return X(self)->resolve_ext != 0; }
static id xp_error(id self, SEL s) { (void)s; return X(self)->error; }
static long xp_line(id self, SEL s) { (void)s; return X(self)->line; }
static long xp_col(id self, SEL s) { (void)s; return X(self)->col; }
static void xp_abort(id self, SEL s) { (void)s; X(self)->aborted = 1; }
static id xp_nil(id self, SEL s) { (void)self; (void)s; return NULL; }

/* ------------------------------------------------------------------ parsing */
struct nsmap { char *prefix, *uri; int depth; };
struct ctx {
    id self, d; const char *p, *end, *start;
    char *stack[256]; int depth;
    struct nsmap ns[128]; int nns;
    int failed;
};
static int is_ws(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int is_name(int c) { return c && !is_ws(c) && c != '>' && c != '/' && c != '=' && c != '<' && c != '"' && c != '\''; }
static void put_utf8(char **o, unsigned cp) {
    unsigned char *p = (unsigned char *)*o;
    if (cp < 0x80) *p++ = (unsigned char)cp;
    else if (cp < 0x800) { *p++ = (unsigned char)(0xc0 | cp >> 6); *p++ = (unsigned char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { *p++ = (unsigned char)(0xe0 | cp >> 12); *p++ = (unsigned char)(0x80 | (cp >> 6 & 63)); *p++ = (unsigned char)(0x80 | (cp & 63)); }
    else { *p++ = (unsigned char)(0xf0 | cp >> 18); *p++ = (unsigned char)(0x80 | (cp >> 12 & 63)); *p++ = (unsigned char)(0x80 | (cp >> 6 & 63)); *p++ = (unsigned char)(0x80 | (cp & 63)); }
    *o = (char *)p;
}
static char *decode(const char *s, size_t n) {
    char *out = malloc(n * 4 + 1), *o = out;
    for (size_t i = 0; i < n;) {
        if (s[i] == '&') {
            size_t j = i + 1; while (j < n && j - i < 12 && s[j] != ';') j++;
            if (j < n && s[j] == ';') {
                const char *e = s + i + 1; size_t k = j - i - 1; unsigned cp = 0;
                if (k && e[0] == '#') cp = (unsigned)(e[1] == 'x' || e[1] == 'X' ? strtoul(e + 2, NULL, 16) : strtoul(e + 1, NULL, 10));
                else if (k == 2 && !strncmp(e, "lt", 2)) cp = '<'; else if (k == 2 && !strncmp(e, "gt", 2)) cp = '>';
                else if (k == 3 && !strncmp(e, "amp", 3)) cp = '&'; else if (k == 4 && !strncmp(e, "quot", 4)) cp = '"'; else if (k == 4 && !strncmp(e, "apos", 4)) cp = '\'';
                if (cp) { put_utf8(&o, cp); i = j + 1; continue; }
            }
        }
        *o++ = s[i++];
    }
    *o = 0;
    return out;
}
static void position(struct ctx *c, long *line, long *col) {
    long l = 1, k = 1;
    for (const char *q = c->start; q < c->p; q++) { if (*q == '\n') { l++; k = 1; } else k++; }
    *line = l; *col = k;
}
static void fail(struct ctx *c, long code, const char *what) {
    if (c->failed) return;
    c->failed = 1;
    long line, col; position(c, &line, &col);
    X(c->self)->line = line; X(c->self)->col = col;
    char msg[160]; snprintf(msg, sizeof msg, "%s (line %ld, column %ld)", what, line, col);
    id info = RT_SEND2(id, (id)rt_class_named("NSDictionary"), "dictionaryWithObject:forKey:", id, rt_str(msg), id, rt_str("NSLocalizedDescription"));
    id err = RT_SEND3(id, (id)rt_class_named("NSError"), "errorWithDomain:code:userInfo:", id, rt_str("NSXMLParserErrorDomain"), long, code, id, info);
    objc_retain(err); objc_release(X(c->self)->error); X(c->self)->error = err;
    if (c->d && rt_responds(c->d, rt_sel("parser:parseErrorOccurred:"))) RT_SEND2(void, c->d, "parser:parseErrorOccurred:", id, c->self, id, err);
}
static int stop(struct ctx *c) { return c->failed || X(c->self)->aborted; }
static int has(struct ctx *c, const char *sel) { return c->d && rt_responds(c->d, rt_sel(sel)); }
static const char *ns_lookup(struct ctx *c, const char *prefix) {
    for (int i = c->nns - 1; i >= 0; i--) if (!strcmp(c->ns[i].prefix, prefix)) return c->ns[i].uri;
    return NULL;
}
static void text_event(struct ctx *c, const char *s, size_t n) {
    if (!n || !has(c, "parser:foundCharacters:")) return;
    char *t = decode(s, n);
    RT_SEND2(void, c->d, "parser:foundCharacters:", id, c->self, id, rt_str(t));
    free(t);
}
static void element(struct ctx *c) {
    /* at '<' + name */
    const char *q = c->p + 1;
    const char *ns = q; while (q < c->end && is_name(*q)) q++;
    char *qname = strndup(ns, (size_t)(q - ns));
    id attrs = RT_SEND0(id, (id)rt_class_named("NSMutableDictionary"), "dictionary");
    int self_close = 0, nmaps = 0;
    char *anames[64], *avals[64]; int na = 0;
    for (;;) {
        while (q < c->end && is_ws(*q)) q++;
        if (q >= c->end) { c->p = q; fail(c, 5, "unexpected end of document in a tag"); free(qname); return; }
        if (*q == '/' && q + 1 < c->end && q[1] == '>') { self_close = 1; q += 2; break; }
        if (*q == '>') { q++; break; }
        const char *an = q; while (q < c->end && is_name(*q)) q++;
        if (q == an) { c->p = q; fail(c, 65, "malformed attribute"); free(qname); return; }
        char *name = strndup(an, (size_t)(q - an));
        while (q < c->end && is_ws(*q)) q++;
        if (q >= c->end || *q != '=') { c->p = q; fail(c, 41, "attribute without a value"); free(name); free(qname); return; }
        q++; while (q < c->end && is_ws(*q)) q++;
        if (q >= c->end || (*q != '"' && *q != '\'')) { c->p = q; fail(c, 39, "attribute value not quoted"); free(name); free(qname); return; }
        char qc = *q++; const char *av = q; while (q < c->end && *q != qc) q++;
        char *val = decode(av, (size_t)(q - av)); if (q < c->end) q++;
        if (na < 64) { anames[na] = name; avals[na] = val; na++; } else { free(name); free(val); }
    }
    /* namespace declarations */
    if (X(c->self)->ns) for (int i = 0; i < na; i++) {
        const char *pf = NULL;
        if (!strcmp(anames[i], "xmlns")) pf = ""; else if (!strncmp(anames[i], "xmlns:", 6)) pf = anames[i] + 6;
        if (!pf || c->nns >= 128) continue;
        c->ns[c->nns++] = (struct nsmap){ strdup(pf), strdup(avals[i]), c->depth };
        nmaps++;
        if (X(c->self)->report_prefixes && has(c, "parser:didStartMappingPrefix:toURI:"))
            RT_SEND3(void, c->d, "parser:didStartMappingPrefix:toURI:", id, c->self, id, rt_str(pf), id, rt_str(avals[i]));
    }
    for (int i = 0; i < na; i++) {
        int is_decl = !strcmp(anames[i], "xmlns") || !strncmp(anames[i], "xmlns:", 6);
        if (!(X(c->self)->ns && is_decl))   /* with namespace processing, declarations are not attributes */
            RT_SEND2(void, attrs, "setObject:forKey:", id, rt_str(avals[i]), id, rt_str(anames[i]));
        free(anames[i]); free(avals[i]);
    }
    (void)nmaps;
    id ename = rt_str(qname), uri = NULL, qn = NULL;
    if (X(c->self)->ns) {
        const char *colon = strchr(qname, ':'), *u;
        if (colon) { char pf[64]; size_t k = (size_t)(colon - qname); if (k > 63) k = 63; memcpy(pf, qname, k); pf[k] = 0; u = ns_lookup(c, pf); ename = rt_str(colon + 1); }
        else u = ns_lookup(c, "");
        uri = u ? rt_str(u) : NULL; qn = rt_str(qname);
    }
    c->p = q;
    if (has(c, "parser:didStartElement:namespaceURI:qualifiedName:attributes:"))
        ((void (*)(id, SEL, id, id, id, id, id))objc_msgSend)(c->d, rt_sel("parser:didStartElement:namespaceURI:qualifiedName:attributes:"), c->self, ename, uri, qn, attrs);
    if (self_close) {
        if (!stop(c) && has(c, "parser:didEndElement:namespaceURI:qualifiedName:"))
            ((void (*)(id, SEL, id, id, id, id))objc_msgSend)(c->d, rt_sel("parser:didEndElement:namespaceURI:qualifiedName:"), c->self, ename, uri, qn);
        while (c->nns && c->ns[c->nns - 1].depth == c->depth) { c->nns--; free(c->ns[c->nns].prefix); free(c->ns[c->nns].uri); }
        free(qname);
    } else if (c->depth < 256) c->stack[c->depth++] = qname;
    else { free(qname); fail(c, 1, "nesting too deep"); }
}
static void end_element(struct ctx *c) {
    const char *q = c->p + 2, *ns = q; while (q < c->end && is_name(*q)) q++;
    char *qname = strndup(ns, (size_t)(q - ns));
    while (q < c->end && *q != '>') q++;
    if (q < c->end) q++;
    if (!c->depth || strcmp(c->stack[c->depth - 1], qname)) { free(qname); fail(c, 76, "end tag does not match the open element"); return; }
    c->p = q;
    id ename = rt_str(qname), uri = NULL, qn = NULL;
    if (X(c->self)->ns) {
        const char *colon = strchr(qname, ':'); char pf[64] = { 0 };
        if (colon) { size_t k = (size_t)(colon - qname); if (k > 63) k = 63; memcpy(pf, qname, k); pf[k] = 0; ename = rt_str(colon + 1); }
        const char *u = ns_lookup(c, pf); uri = u ? rt_str(u) : NULL; qn = rt_str(qname);
    }
    if (has(c, "parser:didEndElement:namespaceURI:qualifiedName:"))
        ((void (*)(id, SEL, id, id, id, id))objc_msgSend)(c->d, rt_sel("parser:didEndElement:namespaceURI:qualifiedName:"), c->self, ename, uri, qn);
    c->depth--; free(c->stack[c->depth]);
    while (c->nns && c->ns[c->nns - 1].depth == c->depth) {
        c->nns--;
        if (X(c->self)->report_prefixes && has(c, "parser:didEndMappingPrefix:")) RT_SEND2(void, c->d, "parser:didEndMappingPrefix:", id, c->self, id, rt_str(c->ns[c->nns].prefix));
        free(c->ns[c->nns].prefix); free(c->ns[c->nns].uri);
    }
    free(qname);
}
static rt_bool xp_parse(id self, SEL s) {
    (void)s;
    struct xmlp *x = X(self);
    x->aborted = 0; objc_release(x->error); x->error = NULL;
    size_t n = x->data ? RT_SEND0(unsigned long, x->data, "length") : 0;
    const char *b = n ? RT_SEND0(const char *, x->data, "bytes") : "";
    struct ctx c = { 0 };
    c.self = self; c.d = x->delegate; c.p = c.start = b; c.end = b + n;
    if (n >= 3 && (unsigned char)b[0] == 0xef && (unsigned char)b[1] == 0xbb && (unsigned char)b[2] == 0xbf) c.p += 3;
    void *pool = objc_autoreleasePoolPush();
    if (has(&c, "parserDidStartDocument:")) RT_SEND1(void, c.d, "parserDidStartDocument:", id, self);
    int seen_root = 0;
    while (c.p < c.end && !stop(&c)) {
        if (*c.p != '<') {
            const char *t = c.p; while (c.p < c.end && *c.p != '<') c.p++;
            if (c.depth) text_event(&c, t, (size_t)(c.p - t));
            else { for (const char *q = t; q < c.p; q++) if (!is_ws(*q)) { c.p = q; fail(&c, 4, "text outside the document element"); break; } }
            continue;
        }
        if (c.end - c.p >= 4 && !strncmp(c.p, "<!--", 4)) {
            const char *e = strstr(c.p + 4, "-->"); if (!e || e > c.end) { fail(&c, 81, "unterminated comment"); break; }
            if (has(&c, "parser:foundComment:")) { char *t = strndup(c.p + 4, (size_t)(e - c.p - 4)); RT_SEND2(void, c.d, "parser:foundComment:", id, self, id, rt_str(t)); free(t); }
            c.p = e + 3; continue;
        }
        if (c.end - c.p >= 9 && !strncmp(c.p, "<![CDATA[", 9)) {
            const char *e = strstr(c.p + 9, "]]>"); if (!e || e > c.end) { fail(&c, 63, "unterminated CDATA section"); break; }
            if (has(&c, "parser:foundCDATA:")) RT_SEND2(void, c.d, "parser:foundCDATA:", id, self, id, RT_SEND2(id, (id)rt_class_named("NSData"), "dataWithBytes:length:", const void *, c.p + 9, unsigned long, (unsigned long)(e - c.p - 9)));
            c.p = e + 3; continue;
        }
        if (c.end - c.p >= 2 && c.p[1] == '?') {
            const char *e = strstr(c.p + 2, "?>"); if (!e || e > c.end) { fail(&c, 111, "unterminated processing instruction"); break; }
            const char *tn = c.p + 2, *te = tn; while (te < e && !is_ws(*te)) te++;
            if (strncmp(tn, "xml", 3) || te - tn != 3) {
                if (has(&c, "parser:foundProcessingInstructionWithTarget:data:")) {
                    char *tgt = strndup(tn, (size_t)(te - tn)); while (te < e && is_ws(*te)) te++; char *dat = strndup(te, (size_t)(e - te));
                    RT_SEND3(void, c.d, "parser:foundProcessingInstructionWithTarget:data:", id, self, id, rt_str(tgt), id, rt_str(dat));
                    free(tgt); free(dat);
                }
            }
            c.p = e + 2; continue;
        }
        if (c.end - c.p >= 2 && c.p[1] == '!') {   /* DOCTYPE and other declarations, with an internal subset */
            int depth = 0; const char *q = c.p + 2;
            while (q < c.end && (depth || *q != '>')) { if (*q == '[') depth++; else if (*q == ']') depth--; q++; }
            c.p = q < c.end ? q + 1 : q; continue;
        }
        if (c.end - c.p >= 2 && c.p[1] == '/') { end_element(&c); continue; }
        if (!c.depth && seen_root) { fail(&c, 5, "extra content after the document element"); break; }
        seen_root = 1;
        element(&c);
    }
    if (!stop(&c) && c.depth) fail(&c, 5, "the document ended inside an element");
    if (!stop(&c) && !seen_root) fail(&c, 4, "the document is empty");
    if (x->aborted && !c.failed) {
        c.failed = 0; fail(&c, 512, "parsing was aborted by the delegate");
    }
    for (int i = 0; i < c.depth; i++) free(c.stack[i]);
    for (int i = 0; i < c.nns; i++) { free(c.ns[i].prefix); free(c.ns[i].uri); }
    int ok = !c.failed && !x->aborted;
    if (ok && has(&c, "parserDidEndDocument:")) RT_SEND1(void, c.d, "parserDidEndDocument:", id, self);
    position(&c, &x->line, &x->col);
    objc_autoreleasePoolPop(pool);
    return ok;
}

void rt_xmlparser_init(void) {
    rt_native_class(RT_CLASS(NSXMLParser), &iosrt_meta_NSXMLParser, "NSXMLParser", RT_CLASS(NSObject), sizeof(struct xmlp) - sizeof(void *));
    Class c = RT_CLASS(NSXMLParser);
    IM(c, "initWithData:", xp_initData); IM(c, "initWithContentsOfURL:", xp_initURL); IM(c, "dealloc", xp_dealloc);
    IM(c, "delegate", xp_delegate); IM(c, "setDelegate:", xp_setDelegate);
    IM(c, "shouldProcessNamespaces", xp_ns); IM(c, "setShouldProcessNamespaces:", xp_setNS);
    IM(c, "shouldReportNamespacePrefixes", xp_report); IM(c, "setShouldReportNamespacePrefixes:", xp_setReport);
    IM(c, "shouldResolveExternalEntities", xp_resolve); IM(c, "setShouldResolveExternalEntities:", xp_setResolve);
    IM(c, "parse", xp_parse); IM(c, "abortParsing", xp_abort); IM(c, "parserError", xp_error);
    IM(c, "lineNumber", xp_line); IM(c, "columnNumber", xp_col); IM(c, "publicID", xp_nil); IM(c, "systemID", xp_nil);
}
