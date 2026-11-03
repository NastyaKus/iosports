/* UIWebView with a small built-in HTML engine.
 *
 * There is no browser engine on this side, but many apps only use a web view
 * for bundled pages: help screens, about boxes, licenses. Those are simple
 * documents, so this file lays them out natively:
 *   - block flow (p, h1-h6, div, section, lists, blockquote, br, hr) with
 *     word wrapping, inline images, links, bold/italic runs;
 *   - a CSS subset from <style> and <link rel=stylesheet>: tag/.class/#id
 *     selectors (the last compound of a selector is matched), color,
 *     font-size, font-weight, text-align, margins, display:none;
 *   - links go through webView:shouldStartLoadWithRequest:navigationType:
 *     (apps route custom schemes there) and then load like a request;
 *   - loads are asynchronous, like UIKit: didStart, then didFinish/didFail.
 * Remote pages fail with NSURLErrorNotConnectedToInternet; scripts do not run.
 *
 * View structure follows UIKit: the web view holds a UIScrollView
 * (subviews[0], also `scrollView`) which holds the document view. */
#include "uikit_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <strings.h>

RT_DECLARE_CLASS(UIView);
RT_DECLARE_CLASS(UIScrollView);
RT_DEFINE_CLASS(UIWebView);
RT_DEFINE_CLASS(UIWebBrowserView);
#define V(x) ((struct uiview *)(x))

enum { RUN_TEXT, RUN_IMAGE, RUN_RULE };
struct wrun { int kind; CGRect r; char *text; double size; int bold; float col[4]; char *href; id image; };
struct wdoc { struct wrun *runs; size_t n, cap; double height; char *title; char **hrefs; size_t nhrefs; };
struct uiweb {
    struct uiview v; id delegate, scroll, content, request; long loading, scales;
    char *html, *base;          /* source of the shown document and its URL */
    char **history; size_t nhist; double laidW; struct wdoc doc;
};
struct uibrowser { struct uiview v; id web; };
#define WEB(x) ((struct uiweb *)(x))

/* ------------------------------------------------------------------ text utils */
static int ieq(const char *a, const char *b) { while (*a && *b) { int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b; if (x != y) return 0; a++; b++; } return !*a && !*b; }
static int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static void put_utf8(char **o, unsigned cp) {
    unsigned char *p = (unsigned char *)*o;
    if (cp < 0x80) *p++ = (unsigned char)cp;
    else if (cp < 0x800) { *p++ = (unsigned char)(0xc0 | cp >> 6); *p++ = (unsigned char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { *p++ = (unsigned char)(0xe0 | cp >> 12); *p++ = (unsigned char)(0x80 | (cp >> 6 & 63)); *p++ = (unsigned char)(0x80 | (cp & 63)); }
    else { *p++ = (unsigned char)(0xf0 | cp >> 18); *p++ = (unsigned char)(0x80 | (cp >> 12 & 63)); *p++ = (unsigned char)(0x80 | (cp >> 6 & 63)); *p++ = (unsigned char)(0x80 | (cp & 63)); }
    *o = (char *)p;
}
static const struct { const char *n; unsigned cp; } entities[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xa0 }, { "rsquo", 0x2019 }, { "lsquo", 0x2018 },
    { "rdquo", 0x201d }, { "ldquo", 0x201c }, { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 }, { "copy", 0xa9 }, { "reg", 0xae },
    { "trade", 0x2122 }, { "bull", 0x2022 }, { "middot", 0xb7 }, { "deg", 0xb0 }, { "times", 0xd7 }, { "rarr", 0x2192 }, { "larr", 0x2190 },
    { "eacute", 0xe9 }, { "egrave", 0xe8 }, { "auml", 0xe4 }, { "ouml", 0xf6 }, { "uuml", 0xfc }, { "szlig", 0xdf }, { "euro", 0x20ac },
};
/* decodes entities of s[0..n) into a new string */
static char *decode_entities(const char *s, size_t n) {
    char *out = malloc(n * 4 + 1), *o = out;
    for (size_t i = 0; i < n;) {
        if (s[i] == '&') {
            size_t j = i + 1; while (j < n && j - i < 12 && s[j] != ';' && !is_space(s[j]) && s[j] != '&') j++;
            if (j < n && s[j] == ';') {
                char name[16]; size_t k = j - i - 1; if (k < sizeof name) { memcpy(name, s + i + 1, k); name[k] = 0; } else name[0] = 0;
                unsigned cp = 0;
                if (name[0] == '#') cp = (unsigned)(name[1] == 'x' || name[1] == 'X' ? strtoul(name + 2, NULL, 16) : strtoul(name + 1, NULL, 10));
                else for (size_t e = 0; e < sizeof entities / sizeof entities[0]; e++) if (!strcmp(entities[e].n, name)) { cp = entities[e].cp; break; }
                if (cp) { put_utf8(&o, cp); i = j + 1; continue; }
            }
        }
        *o++ = s[i++];
    }
    *o = 0;
    return out;
}
static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 8 << 20) { fclose(f); return NULL; }
    char *b = malloc((size_t)n + 1); size_t got = fread(b, 1, (size_t)n, f); fclose(f); b[got] = 0;
    if (len) *len = got;
    return b;
}
/* file:// URL -> filesystem path (query/fragment dropped, %XX decoded); NULL for other schemes */
static char *file_path_of(const char *url) {
    if (strncmp(url, "file://", 7)) return NULL;
    const char *p = url + 7; if (!strncmp(p, "localhost/", 10)) p += 9;
    size_t n = strcspn(p, "?#"); char *out = malloc(n + 1), *o = out;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == '%' && i + 2 < n + 1 && p[i + 1] && p[i + 2]) { char h[3] = { p[i + 1], p[i + 2], 0 }; *o++ = (char)strtol(h, NULL, 16); i += 2; }
        else *o++ = p[i];
    }
    *o = 0;
    return out;
}
char *rt_url_resolve(const char *base, const char *ref);

/* ------------------------------------------------------------------ CSS subset */
struct wstyle {
    double size, indent, rindent, mtop, mbot, padL, padR; int bold, align, hidden, pre, list, upper, display, center_block, nobullet, isblock; long listN; float col[4]; const char *href;
    int has_mtop, has_mbot;
};
/* a selector is a chain of compounds (descendant combinators; > + ~ are treated as descendant) */
struct csscomp { char tag[24], cls[48], idn[48]; };
struct cssrule { struct csscomp c[6]; int n; char *decl; };
struct css { struct cssrule *r; size_t n, cap; };

static int parse_color(const char *v, float out[4]) {
    while (is_space(*v)) v++;
    if (*v == '#') {
        unsigned x = (unsigned)strtoul(v + 1, NULL, 16); size_t n = strspn(v + 1, "0123456789abcdefABCDEF");
        if (n == 3) { out[0] = ((x >> 8) & 15) / 15.f; out[1] = ((x >> 4) & 15) / 15.f; out[2] = (x & 15) / 15.f; out[3] = 1; return 1; }
        if (n == 6) { out[0] = ((x >> 16) & 255) / 255.f; out[1] = ((x >> 8) & 255) / 255.f; out[2] = (x & 255) / 255.f; out[3] = 1; return 1; }
        return 0;
    }
    if (!strncmp(v, "rgb", 3)) {
        const char *p = strchr(v, '('); if (!p) return 0;
        double c[4] = { 0, 0, 0, 1 }; int k = 0; p++;
        while (k < 4 && *p && *p != ')') { char *e; c[k] = strtod(p, &e); if (e == p) break; if (*e == '%') { c[k] *= k < 3 ? 2.55 : 0.01; e++; } k++; p = e; while (*p == ',' || is_space(*p)) p++; }
        if (k < 3) return 0;
        out[0] = (float)(c[0] / 255); out[1] = (float)(c[1] / 255); out[2] = (float)(c[2] / 255); out[3] = (float)c[3];
        return 1;
    }
    static const struct { const char *n; float c[4]; } named[] = {
        { "white", { 1, 1, 1, 1 } }, { "black", { 0, 0, 0, 1 } }, { "gray", { .5f, .5f, .5f, 1 } }, { "grey", { .5f, .5f, .5f, 1 } },
        { "silver", { .75f, .75f, .75f, 1 } }, { "red", { 1, 0, 0, 1 } }, { "green", { 0, .5f, 0, 1 } }, { "blue", { 0, 0, 1, 1 } },
        { "yellow", { 1, 1, 0, 1 } }, { "orange", { 1, .65f, 0, 1 } }, { "transparent", { 0, 0, 0, 0 } },
    };
    char w[32]; size_t n = strcspn(v, " ;!\t\n"); if (n >= sizeof w) return 0; memcpy(w, v, n); w[n] = 0;
    for (size_t i = 0; i < sizeof named / sizeof named[0]; i++) if (ieq(w, named[i].n)) { memcpy(out, named[i].c, sizeof named[i].c); return 1; }
    return 0;
}
/* a length relative to the parent font size; returns -1 when not understood */
static double parse_len(const char *v, double em) {
    while (is_space(*v)) v++;
    char *e; double x = strtod(v, &e);
    if (e == v) {
        if (!strncmp(v, "small", 5)) return em * 0.83;
        if (!strncmp(v, "large", 5)) return em * 1.2;
        if (!strncmp(v, "medium", 6)) return 16;
        return -1;
    }
    if (!strncmp(e, "em", 2) || !strncmp(e, "rem", 3)) return x * em;
    if (*e == '%') return x * em / 100;
    if (!strncmp(e, "pt", 2)) return x * 4 / 3;
    return x;   /* px or unitless */
}
static void apply_decls(struct wstyle *st, const char *decl, double parent_size) {
    const char *p = decl;
    while (*p) {
        while (is_space(*p) || *p == ';') p++;
        const char *colon = strchr(p, ':'); if (!colon) break;
        const char *end = strchr(colon, ';'); if (!end) end = colon + strlen(colon);
        char prop[40]; size_t pn = (size_t)(colon - p); while (pn && is_space(p[pn - 1])) pn--; if (pn >= sizeof prop) pn = sizeof prop - 1; memcpy(prop, p, pn); prop[pn] = 0;
        char val[160]; size_t vn = (size_t)(end - colon - 1); if (vn >= sizeof val) vn = sizeof val - 1; memcpy(val, colon + 1, vn); val[vn] = 0;
        const char *v = val; while (is_space(*v)) v++;
        if (ieq(prop, "color")) parse_color(v, st->col);
        else if (ieq(prop, "font-size")) { double x = parse_len(v, parent_size); if (x > 0) st->size = x; }
        else if (ieq(prop, "font-weight")) st->bold = !strncmp(v, "bold", 4) || atoi(v) >= 600;
        else if (ieq(prop, "text-align")) st->align = !strncmp(v, "center", 6) ? 1 : !strncmp(v, "right", 5) ? 2 : 0;
        else if (ieq(prop, "display")) { st->hidden = !strncmp(v, "none", 4); st->display = !strncmp(v, "block", 5) ? 1 : !strncmp(v, "inline", 6) ? 2 : st->display; }
        else if (ieq(prop, "text-transform")) st->upper = !strncmp(v, "uppercase", 9);
        else if (ieq(prop, "list-style-type") || ieq(prop, "list-style")) st->nobullet = !strncmp(v, "none", 4);
        else if (ieq(prop, "padding-left")) { double x = parse_len(v, st->size); if (x >= 0) st->padL = x; }
        else if (ieq(prop, "padding-right")) { double x = parse_len(v, st->size); if (x >= 0) st->padR = x; }
        else if (ieq(prop, "padding")) {   /* top [right [bottom [left]]] */
            const char *q = v; int k = 0; double vals[4] = { 0 };
            while (*q && k < 4) { vals[k++] = parse_len(q, st->size); while (*q && !is_space(*q)) q++; while (is_space(*q)) q++; }
            if (k >= 1 && vals[0] >= 0) { double r = k >= 2 ? vals[1] : vals[0], l = k >= 4 ? vals[3] : r; st->padR = r >= 0 ? r : 0; st->padL = l >= 0 ? l : 0; }
        }
        else if (ieq(prop, "margin-top")) { double x = parse_len(v, st->size); if (x >= 0) { st->mtop = x; st->has_mtop = 1; } }
        else if (ieq(prop, "margin-bottom")) { double x = parse_len(v, st->size); if (x >= 0) { st->mbot = x; st->has_mbot = 1; } }
        else if (ieq(prop, "margin")) {   /* top [right [bottom]] */
            double a = parse_len(v, st->size); const char *q = v; int k = 0; double vals[4] = { 0 };
            while (*q && k < 4) { vals[k++] = parse_len(q, st->size); while (*q && !is_space(*q)) q++; while (is_space(*q)) q++; }
            if (a >= 0) { st->mtop = vals[0]; st->mbot = k >= 3 ? vals[2] : vals[0]; st->has_mtop = st->has_mbot = 1; }
            st->center_block = strstr(v, "auto") != NULL;
        }
        p = *end ? end + 1 : end;
    }
}
static void css_parse(struct css *css, const char *text) {
    /* strip comments */
    size_t n = strlen(text); char *t = malloc(n + 1), *o = t;
    for (const char *p = text; *p;) { if (p[0] == '/' && p[1] == '*') { const char *e = strstr(p + 2, "*/"); p = e ? e + 2 : p + strlen(p); } else *o++ = *p++; }
    *o = 0;
    for (char *p = t; *p;) {
        char *ob = strchr(p, '{'); if (!ob) break;
        char *cb = strchr(ob, '}'); if (!cb) break;
        *ob = 0; *cb = 0;
        if (*p == '@') { p = cb + 1; continue; }   /* @media and friends: skipped */
        for (char *sel = strtok(p, ","); sel; sel = strtok(NULL, ",")) {
            while (is_space(*sel)) sel++;
            if (strchr(sel, '[')) continue;   /* attribute selectors: not supported, skip the rule */
            struct cssrule rule; memset(&rule, 0, sizeof rule);
            int ok = 1;
            for (char *q = sel; *q && ok;) {
                while (is_space(*q) || *q == '>' || *q == '+' || *q == '~') q++;
                if (!*q) break;
                size_t len = strcspn(q, " \t\n>+~");
                char simple[120]; snprintf(simple, sizeof simple, "%.*s", (int)(len < sizeof simple ? len : sizeof simple - 1), q); q += len;
                char *colon = strchr(simple, ':');
                if (colon) { if (strncmp(colon, ":link", 5) && strncmp(colon, ":visited", 8)) { ok = 0; break; } *colon = 0; }
                if (rule.n == 6) { ok = 0; break; }
                struct csscomp *c = &rule.c[rule.n++];
                char *t = simple; size_t k = strcspn(t, ".#"); snprintf(c->tag, sizeof c->tag, "%.*s", (int)k, t); t += k;
                while (*t) {
                    char kind = *t++; size_t m = strcspn(t, ".#");
                    if (kind == '.' && !c->cls[0]) snprintf(c->cls, sizeof c->cls, "%.*s", (int)m, t);
                    else if (kind == '#') snprintf(c->idn, sizeof c->idn, "%.*s", (int)m, t);
                    t += m;
                }
            }
            if (!ok || !rule.n) continue;
            if (css->n == css->cap) { css->cap = css->cap ? css->cap * 2 : 32; css->r = realloc(css->r, css->cap * sizeof *css->r); }
            rule.decl = strdup(ob + 1);
            css->r[css->n++] = rule;
        }
        p = cb + 1;
    }
    free(t);
}
static int has_class(const char *list, const char *cls) {
    size_t n = strlen(cls);
    for (const char *p = list; p && *p;) { while (is_space(*p)) p++; size_t k = strcspn(p, " \t\n"); if (k == n && !strncmp(p, cls, n)) return 1; p += k; }
    return 0;
}
static int comp_match(const struct csscomp *c, const char *tag, const char *cls, const char *idn) {
    if (c->tag[0] && strcmp(c->tag, "*") && !ieq(c->tag, tag)) return 0;
    if (c->cls[0] && !has_class(cls, c->cls)) return 0;
    if (c->idn[0] && (!idn || strcmp(c->idn, idn))) return 0;
    return 1;
}
/* ancestors: tags/classes/ids of the open elements, [1..depth] (0 is the document root) */
static void css_apply(struct css *css, struct wstyle *st, const char *tag, const char *cls, const char *idn, double parent_size,
                      char (*atags)[12], char (*acls)[64], char (*aids)[32], int depth) {
    for (size_t i = 0; i < css->n; i++) {
        struct cssrule *r = &css->r[i];
        if (!comp_match(&r->c[r->n - 1], tag, cls, idn)) continue;
        int j = r->n - 2;
        for (int d = depth; d >= 1 && j >= 0; d--) if (comp_match(&r->c[j], atags[d], acls[d], aids[d])) j--;
        if (j >= 0) continue;
        apply_decls(st, r->decl, parent_size);
    }
}
static void css_free(struct css *css) { for (size_t i = 0; i < css->n; i++) free(css->r[i].decl); free(css->r); }

/* ------------------------------------------------------------------ layout */
struct lay {
    struct wdoc *doc; struct css css; const char *base; double maxw, left, x, y, pendingMargin; size_t lineStart; int lineAlign, space;
    struct wstyle st[48]; char tags[48][12]; char cls[48][64]; char ids[48][32]; int depth;
};
/* keep hostile or runaway styles (nested relative sizes, huge paddings) in a sane range */
static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static void clamp_style(struct wstyle *st, double maxw) {
    st->size = clampd(st->size, 4, 120);
    st->mtop = clampd(st->mtop, 0, 400); st->mbot = clampd(st->mbot, 0, 400);
    st->padL = clampd(st->padL, 0, maxw / 3); st->padR = clampd(st->padR, 0, maxw / 3);
    st->indent = clampd(st->indent, 0, maxw * 0.6); st->rindent = clampd(st->rindent, 0, maxw * 0.3);
}
static void push_ids(struct lay *L, const char *cls, const char *idn) {
    snprintf(L->cls[L->depth], sizeof L->cls[0], "%s", cls ? cls : "");
    snprintf(L->ids[L->depth], sizeof L->ids[0], "%s", idn ? idn : "");
}
static double right_edge(struct lay *L) { return 8 + L->maxw - L->st[L->depth].rindent; }
static struct wrun *new_run(struct lay *L, int kind) {
    struct wdoc *d = L->doc;
    if (d->n == d->cap) { d->cap = d->cap ? d->cap * 2 : 64; d->runs = realloc(d->runs, d->cap * sizeof *d->runs); }
    struct wrun *r = &d->runs[d->n++]; memset(r, 0, sizeof *r); r->kind = kind; return r;
}
static struct wstyle *cur(struct lay *L) { return &L->st[L->depth]; }
static void flush_line(struct lay *L, int force_empty) {
    struct wdoc *d = L->doc;
    if (d->n == L->lineStart && !force_empty) return;
    double h = 0, right = L->left;
    for (size_t i = L->lineStart; i < d->n; i++) { if (d->runs[i].r.size.height > h) h = d->runs[i].r.size.height; double e = d->runs[i].r.origin.x + d->runs[i].r.size.width; if (e > right) right = e; }
    if (d->n == L->lineStart) h = ceil(cur(L)->size * 1.25);
    double boxw = right_edge(L) - L->left;
    double shift = L->lineAlign == 1 ? (boxw - (right - L->left)) / 2 : L->lineAlign == 2 ? boxw - (right - L->left) : 0;
    if (shift < 0) shift = 0;
    for (size_t i = L->lineStart; i < d->n; i++) { d->runs[i].r.origin.y = L->y + h - d->runs[i].r.size.height; d->runs[i].r.origin.x += shift; }
    L->y += h; L->x = L->left; L->lineStart = d->n; L->space = 0;
}
static void block_break(struct lay *L, double margin) {
    flush_line(L, 0);
    if (margin > L->pendingMargin) L->pendingMargin = margin;
}
static void begin_content(struct lay *L) {
    if (L->doc->n == L->lineStart) {   /* first thing on a new line */
        if (L->pendingMargin > 0) { L->y += L->pendingMargin; L->pendingMargin = 0; }
        L->lineAlign = cur(L)->align; L->x = L->left;
    }
}
static void place_word(struct lay *L, const char *w, size_t n) {
    struct wstyle *st = cur(L);
    if (st->hidden || !n) return;
    begin_content(L);
    id font = rt_font_make(st->size, st->bold, NULL);
    char *word = strndup(w, n);
    double ww = ceil(rt_text_measure(word, font, 1e9, 1).width), sw = L->space && L->x > L->left ? ceil(rt_text_measure(" ", font, 1e9, 1).width) : 0;
    if (L->x > L->left && L->x + sw + ww > right_edge(L)) { flush_line(L, 0); begin_content(L); sw = 0; }
    struct wdoc *d = L->doc; struct wrun *last = d->n > L->lineStart ? &d->runs[d->n - 1] : NULL;
    if (last && last->kind == RUN_TEXT && last->size == st->size && last->bold == st->bold && !memcmp(last->col, st->col, sizeof st->col) &&
        last->href == st->href && fabs(last->r.origin.x + last->r.size.width - L->x) < 0.01) {
        /* extend the previous run on this line */
        size_t ol = strlen(last->text); last->text = realloc(last->text, ol + n + 2);
        if (sw > 0) last->text[ol++] = ' ';
        memcpy(last->text + ol, word, n + 1);
        last->r.size.width = ceil(rt_text_measure(last->text, font, 1e9, 1).width);
        L->x = last->r.origin.x + last->r.size.width;
        free(word);
    } else {
        L->x += sw;
        struct wrun *r = new_run(L, RUN_TEXT);
        r->text = word; r->size = st->size; r->bold = st->bold; memcpy(r->col, st->col, sizeof r->col); r->href = (char *)st->href;
        r->r = (CGRect){ { L->x, 0 }, { ww, ceil(st->size * 1.25) } };
        L->x += ww;
    }
    L->space = 0;
}
static void place_text(struct lay *L, const char *s, size_t n) {
    char *t = decode_entities(s, n); const char *p = t;
    if (cur(L)->upper) for (char *u = t; *u; u++) if (*u >= 'a' && *u <= 'z') *u -= 32;
    while (*p) {
        if (is_space(*p)) {
            if (cur(L)->pre && *p == '\n') flush_line(L, 1);
            L->space = 1; p++; continue;
        }
        size_t k = 0; while (p[k] && !is_space(p[k])) k++;
        place_word(L, p, k); p += k;
        if (*p) L->space = 1;
    }
    free(t);
}
static void place_image(struct lay *L, const char *src, double w, double h) {
    if (cur(L)->hidden || !src) return;
    char *url = rt_url_resolve(L->base, src), *path = file_path_of(url);
    id img = path ? RT_SEND1(id, (id)rt_class_named("UIImage"), "imageWithContentsOfFile:", id, rt_str(path)) : NULL;
    free(url); free(path);
    if (img) { CGSize sz = rt_uiimage_size(img); if (w <= 0 && h <= 0) { w = sz.width; h = sz.height; } else if (w <= 0) w = h * sz.width / (sz.height > 0 ? sz.height : 1); else if (h <= 0) h = w * sz.height / (sz.width > 0 ? sz.width : 1); }
    if (w <= 0 || h <= 0) return;
    double avail = right_edge(L) - L->left; if (avail < 10) avail = 10;
    if (w > avail) { h *= avail / w; w = avail; }
    begin_content(L);
    double sw = L->space && L->x > L->left ? ceil(rt_text_measure(" ", rt_font_make(cur(L)->size, 0, NULL), 1e9, 1).width) : 0;
    if (L->x > L->left && L->x + sw + w > right_edge(L)) { flush_line(L, 0); begin_content(L); sw = 0; }
    L->x += sw;
    struct wrun *r = new_run(L, RUN_IMAGE);
    r->image = objc_retain(img); r->href = (char *)cur(L)->href; r->r = (CGRect){ { L->x, 0 }, { w, h } };
    L->x += w; L->space = 0;
}

/* tag defaults; returns 1 for block-level elements */
static int tag_defaults(const char *tag, struct wstyle *st, const struct wstyle *parent, int nested_list) {
    double em = parent->size;
    static const struct { const char *t; double scale, margin; } heads[] = { { "h1", 2, .67 }, { "h2", 1.5, .83 }, { "h3", 1.17, 1 }, { "h4", 1, 1.33 }, { "h5", .83, 1.67 }, { "h6", .67, 2.33 } };
    for (size_t i = 0; i < 6; i++) if (ieq(tag, heads[i].t)) { st->size = em * heads[i].scale; st->bold = 1; st->mtop = st->mbot = st->size * heads[i].margin; return 1; }
    if (ieq(tag, "p")) { st->mtop = st->mbot = em; return 1; }
    if (ieq(tag, "ul") || ieq(tag, "ol")) { st->mtop = st->mbot = nested_list ? 0 : em; st->padL = 30; st->list = ieq(tag, "ol") ? 2 : 1; st->listN = 0; return 1; }
    if (ieq(tag, "blockquote")) { st->mtop = st->mbot = em; st->padL = 30; return 1; }
    if (ieq(tag, "pre")) { st->pre = 1; st->mtop = st->mbot = em; return 1; }
    if (ieq(tag, "li") || ieq(tag, "div") || ieq(tag, "section") || ieq(tag, "article") || ieq(tag, "header") || ieq(tag, "footer") || ieq(tag, "nav") ||
        ieq(tag, "main") || ieq(tag, "aside") || ieq(tag, "table") || ieq(tag, "tr") || ieq(tag, "dl") || ieq(tag, "dt") || ieq(tag, "dd") || ieq(tag, "figure") || ieq(tag, "form") || ieq(tag, "body") || ieq(tag, "center")) {
        if (ieq(tag, "center")) st->align = 1;
        if (ieq(tag, "dd")) st->padL = 30;
        return 1;
    }
    if (ieq(tag, "b") || ieq(tag, "strong") || ieq(tag, "th")) st->bold = 1;
    else if (ieq(tag, "small")) st->size = em * .83;
    else if (ieq(tag, "big")) st->size = em * 1.2;
    else if (ieq(tag, "a")) { st->col[0] = 0; st->col[1] = .4f; st->col[2] = .93f; st->col[3] = 1; }
    return 0;
}
static const char *attr(char **names, char **vals, int n, const char *name) { for (int i = 0; i < n; i++) if (ieq(names[i], name)) return vals[i]; return NULL; }

static void layout_html(struct wdoc *doc, const char *html, const char *base, double width) {
    struct lay *L = calloc(1, sizeof *L);
    L->doc = doc; L->base = base ? base : "";
    L->left = 8; L->maxw = width - 16; if (L->maxw < 20) L->maxw = 20;
    L->x = L->left; L->y = 8;
    struct wstyle *root = &L->st[0]; root->size = 16; root->col[3] = 1; strcpy(L->tags[0], "#root");
    /* pass 1: collect stylesheets (<style> and linked .css files) */
    for (const char *p = html; (p = strchr(p, '<'));) {
        p++;
        if (!strncmp(p, "style", 5) || !strncmp(p, "STYLE", 5)) { const char *b = strchr(p, '>'); const char *e = b ? strstr(b, "</") : NULL; if (b && e) { char *t = strndup(b + 1, (size_t)(e - b - 1)); css_parse(&L->css, t); free(t); } }
        else if (!strncmp(p, "link", 4) || !strncmp(p, "LINK", 4)) {
            const char *e = strchr(p, '>'); if (!e) break;
            char *tag = strndup(p, (size_t)(e - p));
            if (strstr(tag, "stylesheet")) {
                char *h = strstr(tag, "href=");
                if (h) { h += 5; char q = *h == '"' || *h == '\'' ? *h++ : 0; size_t k = q ? strcspn(h, q == '"' ? "\"" : "'") : strcspn(h, " >"); char *href = strndup(h, k);
                    char *url = rt_url_resolve(L->base, href), *path = file_path_of(url); char *text = path ? read_file(path, NULL) : NULL;
                    if (text) css_parse(&L->css, text);
                    free(text); free(path); free(url); free(href); }
            }
            free(tag);
        }
    }
    /* pass 2: flow */
    const char *p = html;
    while (*p) {
        if (*p != '<') { const char *e = strchr(p, '<'); size_t n = e ? (size_t)(e - p) : strlen(p); if (!cur(L)->hidden) place_text(L, p, n); p += n; continue; }
        if (!strncmp(p, "<!--", 4)) { const char *e = strstr(p + 4, "-->"); p = e ? e + 3 : p + strlen(p); continue; }
        if (p[1] == '!' || p[1] == '?') { const char *e = strchr(p, '>'); p = e ? e + 1 : p + strlen(p); continue; }
        int closing = p[1] == '/';
        const char *q = p + 1 + closing; char tag[12]; size_t tn = 0;
        while (*q && !is_space(*q) && *q != '>' && *q != '/') { if (tn < sizeof tag - 1) tag[tn++] = *q; q++; }
        tag[tn] = 0;
        if (!tn) { place_text(L, "&lt;", 4); p++; continue; }
        /* attributes */
        char *names[16], *vals[16]; int na = 0; int selfclose = 0;
        while (*q && *q != '>') {
            while (is_space(*q)) q++;
            if (*q == '/') { selfclose = 1; q++; continue; }
            if (*q == '>' || !*q) break;
            const char *an = q; while (*q && !is_space(*q) && *q != '=' && *q != '>' && *q != '/') q++;
            size_t anl = (size_t)(q - an); while (is_space(*q)) q++;
            const char *av = ""; size_t avl = 0;
            if (*q == '=') { q++; while (is_space(*q)) q++; if (*q == '"' || *q == '\'') { char qc = *q++; av = q; while (*q && *q != qc) q++; avl = (size_t)(q - av); if (*q) q++; } else { av = q; while (*q && !is_space(*q) && *q != '>') q++; avl = (size_t)(q - av); } }
            if (anl && na < 16) { names[na] = strndup(an, anl); vals[na] = decode_entities(av, avl); na++; }
            else if (!anl && *q && *q != '>') q++;
        }
        p = *q ? q + 1 : q;
        if (!closing && (ieq(tag, "script") || ieq(tag, "style") || ieq(tag, "title"))) {
            char endtag[24]; snprintf(endtag, sizeof endtag, "</%s", tag);
            const char *e = p; while ((e = strchr(e, '<')) && strncasecmp(e, endtag, strlen(endtag))) e++;
            if (ieq(tag, "title") && e && !doc->title) { char *t = decode_entities(p, (size_t)(e - p)); doc->title = t; }
            p = e ? (strchr(e, '>') ? strchr(e, '>') + 1 : e + strlen(e)) : p + strlen(p);
        } else if (closing) {
            /* pop to the matching element */
            int k = L->depth; while (k > 0 && !ieq(L->tags[k], tag)) k--;
            if (k > 0) {
                while (L->depth >= k) {
                    struct wstyle *st = cur(L);
                    if (st->isblock) block_break(L, st->mbot);
                    L->left = L->st[L->depth - 1].indent + 8; if (L->doc->n == L->lineStart) L->x = L->left;
                    L->depth--;
                }
            }
        } else if (ieq(tag, "br")) {
            if (!cur(L)->hidden) { begin_content(L); flush_line(L, 1); }
        } else if (ieq(tag, "img")) {
            const char *w = attr(names, vals, na, "width"), *h = attr(names, vals, na, "height");
            struct wstyle st = *cur(L); st.mtop = st.mbot = st.padL = st.padR = 0; st.display = st.center_block = 0;
            css_apply(&L->css, &st, tag, attr(names, vals, na, "class"), attr(names, vals, na, "id"), cur(L)->size, L->tags, L->cls, L->ids, L->depth);
            const char *inl = attr(names, vals, na, "style"); if (inl) apply_decls(&st, inl, cur(L)->size);
            clamp_style(&st, L->maxw);
            if (st.hidden) {}
            else if (st.display == 1 && L->depth < 47) {   /* block image, centered with margin: auto */
                block_break(L, st.mtop);
                st.align = st.center_block ? 1 : st.align;
                L->depth++; L->st[L->depth] = st; strcpy(L->tags[L->depth], "img"); push_ids(L, NULL, NULL);
                place_image(L, attr(names, vals, na, "src"), w ? atof(w) : 0, h ? atof(h) : 0);
                flush_line(L, 0); L->depth--;
                block_break(L, st.mbot);
            } else place_image(L, attr(names, vals, na, "src"), w ? atof(w) : 0, h ? atof(h) : 0);
        } else if (ieq(tag, "hr")) {
            if (!cur(L)->hidden) { block_break(L, 8); begin_content(L); struct wrun *r = new_run(L, RUN_RULE); r->r = (CGRect){ { L->left, 0 }, { L->maxw, 1 } }; memcpy(r->col, cur(L)->col, sizeof r->col); r->col[3] *= .4f; flush_line(L, 0); L->pendingMargin = 8; }
        } else if (!(ieq(tag, "meta") || ieq(tag, "link") || ieq(tag, "input") || ieq(tag, "source") || ieq(tag, "html") || ieq(tag, "head") || ieq(tag, "wbr"))) {
            if (L->depth < 47) {
                struct wstyle *parent = cur(L), st = *parent;
                st.mtop = st.mbot = st.padL = st.padR = 0; st.has_mtop = st.has_mbot = 0; st.list = parent->list; st.display = st.center_block = 0;
                int nested = 0; for (int k = 1; k <= L->depth; k++) if (ieq(L->tags[k], "ul") || ieq(L->tags[k], "ol")) nested = 1;
                int block = tag_defaults(tag, &st, parent, nested);
                css_apply(&L->css, &st, tag, attr(names, vals, na, "class"), attr(names, vals, na, "id"), parent->size, L->tags, L->cls, L->ids, L->depth);
                const char *inl = attr(names, vals, na, "style"); if (inl) apply_decls(&st, inl, parent->size);
                if (st.display == 1) block = 1; else if (st.display == 2) block = 0;
                clamp_style(&st, L->maxw);
                if (block) { st.indent = clampd(st.indent + st.padL, 0, L->maxw * 0.6); st.rindent = clampd(st.rindent + st.padR, 0, L->maxw * 0.3); }
                st.isblock = block;
                if (ieq(tag, "a")) {   /* hrefs are owned by the document, runs point at them */
                    const char *href = attr(names, vals, na, "href"); st.href = NULL;
                    if (href) { doc->hrefs = realloc(doc->hrefs, (doc->nhrefs + 1) * sizeof *doc->hrefs); st.href = doc->hrefs[doc->nhrefs++] = strdup(href); }
                }
                if (block) block_break(L, st.mtop);
                if (ieq(tag, "li") && (parent->list || 1)) {
                    parent->listN++;
                    L->depth++; L->st[L->depth] = st; snprintf(L->tags[L->depth], sizeof L->tags[0], "%s", tag);
                    push_ids(L, attr(names, vals, na, "class"), attr(names, vals, na, "id"));
                    L->left = st.indent + 8;
                    if (!st.hidden && !st.nobullet) {
                        begin_content(L);
                        char bullet[16]; if (parent->list == 2) snprintf(bullet, sizeof bullet, "%ld.", parent->listN); else snprintf(bullet, sizeof bullet, "\xe2\x80\xa2");
                        id font = rt_font_make(st.size, st.bold, NULL); double bw = ceil(rt_text_measure(bullet, font, 1e9, 1).width);
                        struct wrun *r = new_run(L, RUN_TEXT); r->text = strdup(bullet); r->size = st.size; r->bold = st.bold; memcpy(r->col, st.col, sizeof r->col);
                        r->r = (CGRect){ { L->left - bw - 6, 0 }, { bw, ceil(st.size * 1.25) } };
                    }
                } else {
                    L->depth++; L->st[L->depth] = st; snprintf(L->tags[L->depth], sizeof L->tags[0], "%s", tag);
                    push_ids(L, attr(names, vals, na, "class"), attr(names, vals, na, "id"));
                    if (block) { L->left = st.indent + 8; if (L->doc->n == L->lineStart) L->x = L->left; }
                }
                if (selfclose && !ieq(tag, "li")) { if (block) block_break(L, st.mbot); L->depth--; L->left = cur(L)->indent + 8; }
            }
        }
        for (int i = 0; i < na; i++) { free(names[i]); free(vals[i]); }
    }
    flush_line(L, 0);
    doc->height = L->y + 8;
    css_free(&L->css);
    free(L);
}
static void doc_clear(struct wdoc *d) {
    for (size_t i = 0; i < d->n; i++) { free(d->runs[i].text); objc_release(d->runs[i].image); }
    for (size_t i = 0; i < d->nhrefs; i++) free(d->hrefs[i]);
    free(d->runs); free(d->title); free(d->hrefs);
    memset(d, 0, sizeof *d);
}

/* ------------------------------------------------------------------ the views */
static void web_relayout(id self) {
    struct uiweb *w = WEB(self);
    CGRect b = rt_view_bounds(self);
    rt_view_set_frame(w->scroll, (CGRect){ { 0, 0 }, b.size });
    if (w->html && fabs(w->laidW - b.size.width) > 0.5) {
        doc_clear(&w->doc);
        layout_html(&w->doc, w->html, w->base, b.size.width);
        w->laidW = b.size.width;
    }
    double h = w->doc.height > b.size.height ? w->doc.height : b.size.height;
    rt_view_set_frame(w->content, (CGRect){ { 0, 0 }, { b.size.width, h } });
    ((struct uiscroll *)w->scroll)->contentSize = (CGSize){ b.size.width, w->html ? w->doc.height : 0 };
    rt_ui_mark_dirty();
}
static void web_show(id self, char *html, const char *base) {
    struct uiweb *w = WEB(self);
    free(w->html); free(w->base);
    w->html = html; w->base = strdup(base ? base : "");
    w->laidW = -1;
    web_relayout(self);
    RT_SEND1(void, w->scroll, "setContentOffset:", CGPoint, ((CGPoint){ 0, 0 }));
}
static id make_error(const char *domain, long code, const char *desc) {
    id info = RT_SEND2(id, (id)rt_class_named("NSDictionary"), "dictionaryWithObject:forKey:", id, rt_str(desc), id, rt_str("NSLocalizedDescription"));
    return RT_SEND3(id, (id)rt_class_named("NSError"), "errorWithDomain:code:userInfo:", id, rt_str(domain), long, code, id, info);
}
/* what UIWebView reports: unknown schemes cannot be shown (WebKit 101), the web is offline here */
static id load_error(const char *url) {
    if (!strncmp(url, "http:", 5) || !strncmp(url, "https:", 6)) return make_error("NSURLErrorDomain", -1009, "The Internet connection appears to be offline.");
    if (!strncmp(url, "file:", 5)) return make_error("NSURLErrorDomain", -1100, "The requested URL was not found on this server.");
    return make_error("WebKitErrorDomain", 101, "The URL can’t be shown");
}
struct pending { id web; id request; char *html, *base; long nav; };
static void web_finish(void *ctx) {
    struct pending *pd = ctx; id self = pd->web; struct uiweb *w = WEB(self);
    id d = w->delegate;
    char *html = pd->html, *base = pd->base;
    if (!html && pd->request) {
        id url = RT_SEND0(id, pd->request, "URL");
        const char *u = url ? rt_str_utf8(RT_SEND0(id, url, "absoluteString")) : "";
        char *path = file_path_of(u);
        html = path ? read_file(path, NULL) : NULL;
        free(path); base = strdup(u);
    }
    w->loading = 0;
    if (html) {
        /* a successful navigation away from a loaded page goes into the back list */
        if (pd->nav != 2 && pd->nav != 3 && pd->request && w->base && *w->base) { w->history = realloc(w->history, (w->nhist + 1) * sizeof *w->history); w->history[w->nhist++] = strdup(w->base); }
        web_show(self, html, base);
        if (d && rt_responds(d, rt_sel("webViewDidFinishLoad:"))) RT_SEND1(void, d, "webViewDidFinishLoad:", id, self);
    } else {
        if (base && (!strncmp(base, "http", 4) || !strncmp(base, "file:", 5))) rt_warn("UIWebView: cannot load %s (only bundled pages, no network)", base);
        if (d && rt_responds(d, rt_sel("webView:didFailLoadWithError:"))) RT_SEND2(void, d, "webView:didFailLoadWithError:", id, self, id, load_error(base ? base : ""));
    }
    free(base); objc_release(pd->request); objc_release(self); free(pd);
}
/* nav (UIWebViewNavigationType): 0 link clicked, 2 back/forward, 3 reload, 5 other */
static void web_start(id self, id request, char *html, const char *base, long nav) {
    struct uiweb *w = WEB(self); id d = w->delegate;
    if (request && d && rt_responds(d, rt_sel("webView:shouldStartLoadWithRequest:navigationType:"))) {
        int ok = RT_SEND3(long, d, "webView:shouldStartLoadWithRequest:navigationType:", id, self, id, request, long, nav) & 0xff;   /* BOOL lives in the low byte */
        if (!ok) { free(html); return; }
    }
    if (request) { objc_retain(request); objc_release(w->request); w->request = request; }
    w->loading = 1;
    if (d && rt_responds(d, rt_sel("webViewDidStartLoad:"))) RT_SEND1(void, d, "webViewDidStartLoad:", id, self);
    struct pending *pd = calloc(1, sizeof *pd);
    pd->web = objc_retain(self); pd->request = objc_retain(request); pd->html = html; pd->base = base ? strdup(base) : NULL; pd->nav = nav;
    rt_after(0, web_finish, pd);
}
static id web_initWithFrame(id self, SEL s, CGRect fr) {
    struct { id r; Class c; } sup = { self, RT_CLASS(UIView) };
    self = ((id (*)(void *, SEL, CGRect))objc_msgSendSuper)(&sup, s, fr);
    if (!self) return NULL;
    struct uiweb *w = WEB(self);
    w->scroll = rt_view_new(RT_CLASS(UIScrollView), (CGRect){ { 0, 0 }, fr.size }); V(w->scroll)->autoresizing = 2 | 16;
    w->content = rt_view_new(RT_CLASS(UIWebBrowserView), (CGRect){ { 0, 0 }, fr.size });
    ((struct uibrowser *)w->content)->web = self;
    rt_view_add_subview(w->scroll, w->content);
    rt_view_add_subview(self, w->scroll);
    RT_SEND1(void, self, "setBackgroundColor:", id, rt_color_make(1, 1, 1, 1));
    return self;
}
static id web_initWithCoder(id self, SEL s, id coder) {
    struct { id r; Class c; } sup = { self, RT_CLASS(UIView) };
    self = ((id (*)(void *, SEL, id))objc_msgSendSuper)(&sup, s, coder);
    if (!self) return NULL;
    struct uiweb *w = WEB(self);
    if (!w->scroll) {
        CGRect b = rt_view_bounds(self);
        w->scroll = rt_view_new(RT_CLASS(UIScrollView), b); V(w->scroll)->autoresizing = 2 | 16;
        w->content = rt_view_new(RT_CLASS(UIWebBrowserView), b); ((struct uibrowser *)w->content)->web = self;
        rt_view_add_subview(w->scroll, w->content);
        RT_SEND2(void, self, "insertSubview:atIndex:", id, w->scroll, long, 0);
    }
    return self;
}
static void web_dealloc(id self, SEL s) {
    struct uiweb *w = WEB(self);
    doc_clear(&w->doc); free(w->html); free(w->base);
    for (size_t i = 0; i < w->nhist; i++) free(w->history[i]);
    free(w->history);
    objc_release(w->scroll); objc_release(w->content); objc_release(w->request);
    struct { id r; Class c; } sup = { self, RT_CLASS(UIView) };
    ((void (*)(void *, SEL))objc_msgSendSuper)(&sup, s);
}
static void web_layoutSubviews(id self, SEL s) {
    struct { id r; Class c; } sup = { self, RT_CLASS(UIView) };
    ((void (*)(void *, SEL))objc_msgSendSuper)(&sup, s);
    web_relayout(self);
}
static void web_loadRequest(id self, SEL s, id req) { (void)s; if (req) web_start(self, req, NULL, NULL, 5); }
static void web_loadHTML(id self, SEL s, id html, id baseURL) {
    (void)s;
    const char *base = baseURL ? rt_str_utf8(RT_SEND0(id, baseURL, "absoluteString")) : "";
    web_start(self, NULL, strdup(html ? rt_str_utf8(html) : ""), base, 5);
}
static void web_loadData(id self, SEL s, id data, id mime, id enc, id baseURL) {
    (void)s; (void)mime; (void)enc;
    size_t n = data ? RT_SEND0(unsigned long, data, "length") : 0; char *h = malloc(n + 1);
    if (n) memcpy(h, RT_SEND0(const void *, data, "bytes"), n);
    h[n] = 0;
    const char *base = baseURL ? rt_str_utf8(RT_SEND0(id, baseURL, "absoluteString")) : "";
    web_start(self, NULL, h, base, 5);
}
static void web_setDelegate(id self, SEL s, id d) { (void)s; WEB(self)->delegate = d; }
static id web_delegate(id self, SEL s) { (void)s; return WEB(self)->delegate; }
static id web_scrollView(id self, SEL s) { (void)s; return WEB(self)->scroll; }
static id web_request(id self, SEL s) { (void)s; return WEB(self)->request; }
static rt_bool web_isLoading(id self, SEL s) { (void)s; return WEB(self)->loading != 0; }
static rt_bool web_canGoBack(id self, SEL s) { (void)s; return WEB(self)->nhist > 0; }
static void web_goBack(id self, SEL s) {
    (void)s; struct uiweb *w = WEB(self);
    if (!w->nhist) return;
    char *u = w->history[--w->nhist];
    id url = RT_SEND1(id, (id)rt_class_named("NSURL"), "URLWithString:", id, rt_str(u));
    free(u);
    if (url) web_start(self, RT_SEND1(id, (id)rt_class_named("NSURLRequest"), "requestWithURL:", id, url), NULL, NULL, 2);
}
static void web_reload(id self, SEL s) { (void)s; if (WEB(self)->request) web_start(self, WEB(self)->request, NULL, NULL, 3); }
static id web_eval(id self, SEL s, id js) {
    (void)s; const char *j = js ? rt_str_utf8(js) : "";
    if (strstr(j, "document.title") && WEB(self)->doc.title) return rt_str(WEB(self)->doc.title);
    return rt_str("");
}
static rt_bool web_scales(id self, SEL s) { (void)s; return WEB(self)->scales != 0; }
static void web_setScales(id self, SEL s, long v) { (void)s; WEB(self)->scales = v & 0xff; }
static void web_nop(id self, SEL s) { (void)self; (void)s; }
static rt_bool web_no(id self, SEL s) { (void)self; (void)s; return 0; }
static void web_nopL(id self, SEL s, long v) { (void)self; (void)s; (void)v; }

/* the document view: drawing and link taps */
void rt_webview_draw(id view, struct cgctx *c, CGRect b) {
    (void)b;
    if (!rt_is_kind_of(view, RT_CLASS(UIWebBrowserView))) return;
    id web = ((struct uibrowser *)view)->web; if (!web) return;
    struct wdoc *d = &WEB(web)->doc;
    CGRect vis = rt_view_bounds(WEB(web)->scroll);   /* the scrolled-to part, in document coordinates */
    double y0 = vis.origin.y - 64, y1 = vis.origin.y + vis.size.height + 64;
    for (size_t i = 0; i < d->n; i++) {
        struct wrun *r = &d->runs[i];
        if (r->r.origin.y + r->r.size.height < y0 || r->r.origin.y > y1) continue;
        if (r->kind == RUN_TEXT) rt_text_draw(c, r->text, rt_font_make(r->size, r->bold, NULL), r->col, (CGRect){ r->r.origin, { r->r.size.width + 2, r->r.size.height } }, 0, 1, 0);
        else if (r->kind == RUN_IMAGE && r->image) rt_uiimage_draw(r->image, c, r->r, 1, NULL);
        else if (r->kind == RUN_RULE) { iosrt_CGContextSetRGBFillColor(c, r->col[0], r->col[1], r->col[2], r->col[3]); iosrt_CGContextFillRect(c, r->r); }
    }
}
static void br_touchesEnded(id self, SEL s, id touches, id ev) {
    (void)s; (void)ev;
    id web = ((struct uibrowser *)self)->web; if (!web) return;
    struct uiweb *w = WEB(web);
    if (((struct uiscroll *)w->scroll)->dragging) return;
    CGPoint p = RT_SEND1(CGPoint, RT_SEND0(id, touches, "anyObject"), "locationInView:", id, self);
    for (size_t i = 0; i < w->doc.n; i++) {
        struct wrun *r = &w->doc.runs[i];
        if (!r->href) continue;
        CGRect hit = { { r->r.origin.x - 4, r->r.origin.y - 4 }, { r->r.size.width + 8, r->r.size.height + 8 } };
        if (p.x < hit.origin.x || p.y < hit.origin.y || p.x > hit.origin.x + hit.size.width || p.y > hit.origin.y + hit.size.height) continue;
        if (r->href[0] == '#') { RT_SEND1(void, w->scroll, "setContentOffset:", CGPoint, ((CGPoint){ 0, 0 })); return; }
        char *abs = rt_url_resolve(w->base ? w->base : "", r->href);
        id url = RT_SEND1(id, (id)rt_class_named("NSURL"), "URLWithString:", id, rt_str(abs));
        free(abs);
        if (url) web_start(web, RT_SEND1(id, (id)rt_class_named("NSURLRequest"), "requestWithURL:", id, url), NULL, NULL, 0);
        return;
    }
}

void rt_webview_init(void) {
    Class v = RT_CLASS(UIView);
    NEWCLASS(UIWebView, v, sizeof(struct uiweb) - sizeof(struct uiview));
    Class web = RT_CLASS(UIWebView);
    IM(web, "initWithFrame:", web_initWithFrame); IM(web, "initWithCoder:", web_initWithCoder); IM(web, "dealloc", web_dealloc);
    IM(web, "layoutSubviews", web_layoutSubviews);
    IM(web, "loadRequest:", web_loadRequest); IM(web, "loadHTMLString:baseURL:", web_loadHTML); IM(web, "loadData:MIMEType:textEncodingName:baseURL:", web_loadData);
    IM(web, "setDelegate:", web_setDelegate); IM(web, "delegate", web_delegate); IM(web, "scrollView", web_scrollView); IM(web, "request", web_request);
    IM(web, "isLoading", web_isLoading); IM(web, "canGoBack", web_canGoBack); IM(web, "canGoForward", web_no);
    IM(web, "goBack", web_goBack); IM(web, "goForward", web_nop); IM(web, "reload", web_reload); IM(web, "stopLoading", web_nop);
    IM(web, "stringByEvaluatingJavaScriptFromString:", web_eval);
    IM(web, "scalesPageToFit", web_scales); IM(web, "setScalesPageToFit:", web_setScales);
    IM(web, "setDataDetectorTypes:", web_nopL); IM(web, "setAllowsInlineMediaPlayback:", web_nopL); IM(web, "setMediaPlaybackRequiresUserAction:", web_nopL);
    IM(web, "setKeyboardDisplayRequiresUserAction:", web_nopL); IM(web, "setSuppressesIncrementalRendering:", web_nopL);
    NEWCLASS(UIWebBrowserView, v, sizeof(struct uibrowser) - sizeof(struct uiview));
    IM(RT_CLASS(UIWebBrowserView), "touchesEnded:withEvent:", br_touchesEnded);
}
