/* DOM for the UIWebView engine: an HTML tree builder (the forgiving subset
 * real pages need: void and raw-text elements, implied html/head/body,
 * auto-closing p/li/dt/dd/tr/td/option), tree edits, serialization for
 * innerHTML/outerHTML, and CSS selector matching shared by the cascade and
 * querySelector. */
#include "webengine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>
#include <ctype.h>

/* ------------------------------------------------------------------ text utils */
int web_ieq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) { int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b; if (x != y) return 0; a++; b++; }
    return !*a && !*b;
}
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
    { "uarr", 0x2191 }, { "darr", 0x2193 }, { "laquo", 0xab }, { "raquo", 0xbb }, { "iexcl", 0xa1 }, { "iquest", 0xbf }, { "para", 0xb6 }, { "sect", 0xa7 },
    { "eacute", 0xe9 }, { "egrave", 0xe8 }, { "ecirc", 0xea }, { "aacute", 0xe1 }, { "agrave", 0xe0 }, { "acirc", 0xe2 }, { "iacute", 0xed },
    { "oacute", 0xf3 }, { "uacute", 0xfa }, { "ntilde", 0xf1 }, { "ccedil", 0xe7 }, { "auml", 0xe4 }, { "ouml", 0xf6 }, { "uuml", 0xfc },
    { "Auml", 0xc4 }, { "Ouml", 0xd6 }, { "Uuml", 0xdc }, { "szlig", 0xdf }, { "euro", 0x20ac }, { "pound", 0xa3 }, { "yen", 0xa5 }, { "cent", 0xa2 },
    { "frac12", 0xbd }, { "frac14", 0xbc }, { "plusmn", 0xb1 }, { "micro", 0xb5 }, { "hearts", 0x2665 }, { "check", 0x2713 },
};
char *web_decode_entities(const char *s, size_t n) {
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
char *web_read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 8 << 20) { fclose(f); return NULL; }
    char *b = malloc((size_t)n + 1); size_t got = fread(b, 1, (size_t)n, f); fclose(f); b[got] = 0;
    if (len) *len = got;
    return b;
}
char *web_file_path_of(const char *url) {
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

/* ------------------------------------------------------------------ nodes */
struct wdocument *wdoc_new(void) {
    struct wdocument *d = calloc(1, sizeof *d);
    d->root = dom_new(d, N_DOCUMENT, NULL);
    return d;
}
struct dnode *dom_new(struct wdocument *d, int type, const char *s) {
    struct dnode *n = calloc(1, sizeof *n);
    n->type = type; n->doc = d;
    if (type == N_ELEMENT) { n->tag = strdup(s ? s : "div"); for (char *p = n->tag; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32; }
    else if (s) n->text = strdup(s);
    if (d->nall == d->acap) { d->acap = d->acap ? d->acap * 2 : 256; d->all = realloc(d->all, d->acap * sizeof *d->all); }
    d->all[d->nall++] = n;
    return n;
}
void wdoc_free(struct wdocument *d) {
    if (!d) return;
    for (size_t i = 0; i < d->nall; i++) {
        struct dnode *n = d->all[i];
        free(n->tag); free(n->text);
        for (int k = 0; k < n->na; k++) { free(n->an[k]); free(n->av[k]); }
        free(n->an); free(n->av); free(n->cs.bgimg);
        free(n);
    }
    for (size_t i = 0; i < d->nitems; i++) { free(d->items[i].text); objc_release(d->items[i].image); }
    free(d->items); free(d->all); free(d->url); free(d->title);
    if (d->sheet) css_free(d->sheet);
    free(d);
}
const char *dom_attr(const struct dnode *n, const char *name) {
    if (!n || n->type != N_ELEMENT) return NULL;
    for (int i = 0; i < n->na; i++) if (web_ieq(n->an[i], name)) return n->av[i];
    return NULL;
}
void dom_set_attr(struct dnode *n, const char *name, const char *val) {
    if (!n || n->type != N_ELEMENT) return;
    for (int i = 0; i < n->na; i++) if (web_ieq(n->an[i], name)) { free(n->av[i]); n->av[i] = strdup(val ? val : ""); n->doc->dirty = 1; return; }
    if (n->na == n->acap) { n->acap = n->acap ? n->acap * 2 : 4; n->an = realloc(n->an, n->acap * sizeof *n->an); n->av = realloc(n->av, n->acap * sizeof *n->av); }
    n->an[n->na] = strdup(name); for (char *p = n->an[n->na]; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
    n->av[n->na++] = strdup(val ? val : "");
    n->doc->dirty = 1;
}
void dom_remove_attr(struct dnode *n, const char *name) {
    if (!n) return;
    for (int i = 0; i < n->na; i++) if (web_ieq(n->an[i], name)) {
        free(n->an[i]); free(n->av[i]);
        memmove(n->an + i, n->an + i + 1, (size_t)(n->na - i - 1) * sizeof *n->an); memmove(n->av + i, n->av + i + 1, (size_t)(n->na - i - 1) * sizeof *n->av);
        n->na--; n->doc->dirty = 1; return;
    }
}
void dom_remove(struct dnode *c) {
    struct dnode *p = c->parent; if (!p) return;
    if (c->prev) c->prev->next = c->next; else p->first = c->next;
    if (c->next) c->next->prev = c->prev; else p->last = c->prev;
    c->parent = c->next = c->prev = NULL;
    c->doc->dirty = 1;
}
void dom_insert_before(struct dnode *p, struct dnode *c, struct dnode *ref) {
    if (!p || !c || c == p || dom_contains(c, p)) return;
    if (c->type == N_FRAGMENT) { while (c->first) { struct dnode *k = c->first; dom_remove(k); dom_insert_before(p, k, ref); } return; }
    if (c->parent) dom_remove(c);
    if (ref && ref->parent != p) ref = NULL;
    c->parent = p;
    if (!ref) { c->prev = p->last; if (p->last) p->last->next = c; else p->first = c; p->last = c; }
    else { c->next = ref; c->prev = ref->prev; if (ref->prev) ref->prev->next = c; else p->first = c; ref->prev = c; }
    p->doc->dirty = 1;
}
void dom_append(struct dnode *p, struct dnode *c) { dom_insert_before(p, c, NULL); }
int dom_contains(const struct dnode *a, const struct dnode *b) { for (; b; b = b->parent) if (a == b) return 1; return 0; }
int dom_connected(const struct dnode *n) { while (n && n->parent) n = n->parent; return n && n->type == N_DOCUMENT; }
struct dnode *dom_next_pre(struct dnode *n, struct dnode *stop) {
    if (n->first) return n->first;
    while (n && n != stop) { if (n->next) return n->next; n = n->parent; }
    return NULL;
}
struct dnode *dom_clone(struct dnode *n, int deep) {
    struct dnode *c = dom_new(n->doc, n->type, n->type == N_ELEMENT ? n->tag : n->text);
    for (int i = 0; i < n->na; i++) dom_set_attr(c, n->an[i], n->av[i]);
    if (deep) for (struct dnode *k = n->first; k; k = k->next) dom_append(c, dom_clone(k, 1));
    return c;
}
int dom_has_class(const struct dnode *n, const char *cls) {
    const char *list = dom_attr(n, "class"); size_t len = strlen(cls);
    for (const char *p = list; p && *p;) { while (is_space(*p)) p++; size_t k = strcspn(p, " \t\n\r\f"); if (k == len && !strncmp(p, cls, len)) return 1; p += k; }
    return 0;
}
struct dnode *dom_by_id(struct dnode *root, const char *idv) {
    for (struct dnode *n = root; n; n = dom_next_pre(n, root)) { const char *v = dom_attr(n, "id"); if (v && !strcmp(v, idv)) return n; }
    return NULL;
}
static struct dnode *child_tag(struct dnode *p, const char *tag) {
    for (struct dnode *k = p ? p->first : NULL; k; k = k->next) if (k->type == N_ELEMENT && !strcmp(k->tag, tag)) return k;
    return NULL;
}
struct dnode *dom_html(struct wdocument *d) { return child_tag(d->root, "html"); }
struct dnode *dom_head(struct wdocument *d) { return child_tag(dom_html(d), "head"); }
struct dnode *dom_body(struct wdocument *d) { return child_tag(dom_html(d), "body"); }

/* ------------------------------------------------------------------ text content and serialization */
typedef struct { char *b; size_t n, cap; } sbuf;
static void sb_add(sbuf *s, const char *t, size_t n) {
    if (s->n + n + 1 > s->cap) { s->cap = (s->n + n + 1) * 2; s->b = realloc(s->b, s->cap); }
    memcpy(s->b + s->n, t, n); s->n += n; s->b[s->n] = 0;
}
static void sb_str(sbuf *s, const char *t) { sb_add(s, t, strlen(t)); }
static void text_of(const struct dnode *n, sbuf *s) {
    if (n->type == N_TEXT) sb_str(s, n->text ? n->text : "");
    for (struct dnode *k = n->first; k; k = k->next) if (k->type != N_COMMENT) text_of(k, s);
}
char *dom_text_content(const struct dnode *n) { sbuf s = { 0 }; sb_add(&s, "", 0); text_of(n, &s); return s.b; }
void dom_set_text_content(struct dnode *n, const char *t) {
    while (n->first) dom_remove(n->first);
    if (t && *t) dom_append(n, dom_new(n->doc, N_TEXT, t));
    n->doc->dirty = 1;
}
static int is_void(const char *t) {
    static const char *v[] = { "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr", NULL };
    for (int i = 0; v[i]; i++) if (!strcmp(t, v[i])) return 1;
    return 0;
}
static int is_raw(const char *t) { return !strcmp(t, "script") || !strcmp(t, "style") || !strcmp(t, "textarea") || !strcmp(t, "title"); }
static void escape(sbuf *s, const char *t, int attr) {
    for (; *t; t++) {
        if (*t == '&') sb_str(s, "&amp;"); else if (*t == '<' && !attr) sb_str(s, "&lt;"); else if (*t == '>' && !attr) sb_str(s, "&gt;");
        else if (*t == '"' && attr) sb_str(s, "&quot;"); else sb_add(s, t, 1);
    }
}
static void ser(const struct dnode *n, sbuf *s) {
    if (n->type == N_TEXT) {
        if (n->parent && n->parent->type == N_ELEMENT && is_raw(n->parent->tag)) sb_str(s, n->text ? n->text : ""); else escape(s, n->text ? n->text : "", 0);
        return;
    }
    if (n->type == N_COMMENT) { sb_str(s, "<!--"); sb_str(s, n->text ? n->text : ""); sb_str(s, "-->"); return; }
    if (n->type == N_ELEMENT) {
        sb_str(s, "<"); sb_str(s, n->tag);
        for (int i = 0; i < n->na; i++) { sb_str(s, " "); sb_str(s, n->an[i]); sb_str(s, "=\""); escape(s, n->av[i], 1); sb_str(s, "\""); }
        sb_str(s, ">");
        if (is_void(n->tag)) return;
    }
    for (struct dnode *k = n->first; k; k = k->next) ser(k, s);
    if (n->type == N_ELEMENT) { sb_str(s, "</"); sb_str(s, n->tag); sb_str(s, ">"); }
}
char *dom_serialize_children(const struct dnode *n) { sbuf s = { 0 }; sb_add(&s, "", 0); for (struct dnode *k = n->first; k; k = k->next) ser(k, &s); return s.b; }
char *dom_serialize(const struct dnode *n) { sbuf s = { 0 }; sb_add(&s, "", 0); ser(n, &s); return s.b; }

/* ------------------------------------------------------------------ the tree builder */
static int closes_p(const char *t) {
    static const char *v[] = { "address", "article", "aside", "blockquote", "div", "dl", "fieldset", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6",
                               "header", "hr", "main", "nav", "ol", "p", "pre", "section", "table", "ul", "figure", "center", NULL };
    for (int i = 0; v[i]; i++) if (!strcmp(t, v[i])) return 1;
    return 0;
}
static int head_only(const char *t) { return !strcmp(t, "meta") || !strcmp(t, "link") || !strcmp(t, "title") || !strcmp(t, "base"); }

void dom_parse_into(struct wdocument *d, struct dnode *parent, const char *html, int fragment) {
    struct dnode *stack[256]; int sp = 0; stack[0] = parent;
    #define TOP stack[sp]
    const char *p = html ? html : "";
    while (*p) {
        if (*p != '<') {
            const char *e = strchr(p, '<'); size_t n = e ? (size_t)(e - p) : strlen(p);
            char *t = web_decode_entities(p, n);
            if (TOP->last && TOP->last->type == N_TEXT) { size_t ol = strlen(TOP->last->text); TOP->last->text = realloc(TOP->last->text, ol + strlen(t) + 1); strcpy(TOP->last->text + ol, t); free(t); }
            else { struct dnode *tn = dom_new(d, N_TEXT, NULL); tn->text = t; dom_append(TOP, tn); }
            p += n; continue;
        }
        if (!strncmp(p, "<!--", 4)) {
            const char *e = strstr(p + 4, "-->"); size_t n = e ? (size_t)(e - p - 4) : strlen(p + 4);
            struct dnode *c = dom_new(d, N_COMMENT, NULL); c->text = strndup(p + 4, n); dom_append(TOP, c);
            p = e ? e + 3 : p + strlen(p); continue;
        }
        if (p[1] == '!' || p[1] == '?') { const char *e = strchr(p, '>'); p = e ? e + 1 : p + strlen(p); continue; }
        int closing = p[1] == '/';
        const char *q = p + 1 + closing; char tag[32]; size_t tn = 0;
        while (*q && !is_space(*q) && *q != '>' && *q != '/') { if (tn < sizeof tag - 1) tag[tn++] = (char)(*q >= 'A' && *q <= 'Z' ? *q + 32 : *q); q++; }
        tag[tn] = 0;
        if (!tn || !((tag[0] >= 'a' && tag[0] <= 'z'))) {   /* a stray '<' is text */
            struct dnode *tn2 = dom_new(d, N_TEXT, "<"); dom_append(TOP, tn2); p++; continue;
        }
        if (closing) {
            const char *e = strchr(q, '>'); p = e ? e + 1 : p + strlen(p);
            int k = sp; while (k > 0 && strcmp(stack[k]->tag, tag)) k--;
            if (k > 0) sp = k - 1;
            else if (!strcmp(tag, "p")) dom_append(TOP, dom_new(d, N_ELEMENT, "p"));   /* </p> without <p> makes an empty paragraph */
            continue;
        }
        /* attributes */
        struct dnode *el = dom_new(d, N_ELEMENT, tag); int selfclose = 0;
        while (*q && *q != '>') {
            while (is_space(*q)) q++;
            if (*q == '/') { selfclose = 1; q++; continue; }
            if (*q == '>' || !*q) break;
            const char *an = q; while (*q && !is_space(*q) && *q != '=' && *q != '>' && *q != '/') q++;
            size_t anl = (size_t)(q - an); while (is_space(*q)) q++;
            const char *av = ""; size_t avl = 0;
            if (*q == '=') { q++; while (is_space(*q)) q++; if (*q == '"' || *q == '\'') { char qc = *q++; av = q; while (*q && *q != qc) q++; avl = (size_t)(q - av); if (*q) q++; } else { av = q; while (*q && !is_space(*q) && *q != '>') q++; avl = (size_t)(q - av); } }
            if (anl) { char *nm = strndup(an, anl), *vv = web_decode_entities(av, avl); if (!dom_attr(el, nm)) dom_set_attr(el, nm, vv); free(nm); free(vv); }
            else if (*q && *q != '>') q++;
        }
        p = *q ? q + 1 : q;
        /* implied end tags */
        if (closes_p(tag)) { for (int k = sp; k > 0; k--) { if (!strcmp(stack[k]->tag, "p")) { sp = k - 1; break; } if (closes_p(stack[k]->tag) || !strcmp(stack[k]->tag, "li")) break; } }
        if (!strcmp(tag, "li")) { for (int k = sp; k > 0; k--) { if (!strcmp(stack[k]->tag, "li")) { sp = k - 1; break; } if (!strcmp(stack[k]->tag, "ul") || !strcmp(stack[k]->tag, "ol")) break; } }
        if (!strcmp(tag, "dt") || !strcmp(tag, "dd")) { for (int k = sp; k > 0; k--) { if (!strcmp(stack[k]->tag, "dt") || !strcmp(stack[k]->tag, "dd")) { sp = k - 1; break; } if (!strcmp(stack[k]->tag, "dl")) break; } }
        if (!strcmp(tag, "option")) { if (sp > 0 && !strcmp(TOP->tag, "option")) sp--; }
        if (!strcmp(tag, "tr")) { for (int k = sp; k > 0; k--) { if (!strcmp(stack[k]->tag, "tr")) { sp = k - 1; break; } if (!strcmp(stack[k]->tag, "table")) break; } }
        if (!strcmp(tag, "td") || !strcmp(tag, "th")) { for (int k = sp; k > 0; k--) { if (!strcmp(stack[k]->tag, "td") || !strcmp(stack[k]->tag, "th")) { sp = k - 1; break; } if (!strcmp(stack[k]->tag, "tr") || !strcmp(stack[k]->tag, "table")) break; } }
        /* html/head/body tags merge into the implied ones */
        if (!fragment && (!strcmp(tag, "html") || !strcmp(tag, "body") || !strcmp(tag, "head"))) {
            dom_ensure_structure(d);
            struct dnode *t = !strcmp(tag, "html") ? dom_html(d) : !strcmp(tag, "head") ? dom_head(d) : dom_body(d);
            for (int i = 0; i < el->na; i++) if (!dom_attr(t, el->an[i])) dom_set_attr(t, el->an[i], el->av[i]);
            if (strcmp(tag, "html")) { sp = 0; stack[++sp] = dom_html(d); stack[++sp] = t; }
            else { sp = 0; stack[++sp] = t; }
            continue;
        }
        dom_append(TOP, el);
        if (is_raw(tag)) {
            char endtag[40]; snprintf(endtag, sizeof endtag, "</%s", tag);
            const char *e = p; while ((e = strchr(e, '<')) && strncasecmp(e, endtag, strlen(endtag))) e++;
            size_t n = e ? (size_t)(e - p) : strlen(p);
            if (n) { struct dnode *tx = dom_new(d, N_TEXT, NULL); tx->text = !strcmp(tag, "script") || !strcmp(tag, "style") ? strndup(p, n) : web_decode_entities(p, n); dom_append(el, tx); }
            p = e ? (strchr(e, '>') ? strchr(e, '>') + 1 : e + strlen(e)) : p + n;
            continue;
        }
        if (!selfclose && !is_void(tag) && sp < 255) stack[++sp] = el;
    }
    #undef TOP
    if (!fragment) dom_ensure_structure(d);
}

/* html > head + body; stray top-level nodes move into body (head-only ones into head) */
void dom_ensure_structure(struct wdocument *d) {
    struct dnode *html = dom_html(d);
    if (!html) { html = dom_new(d, N_ELEMENT, "html"); dom_append(d->root, html); }
    struct dnode *head = dom_head(d), *body = dom_body(d);
    if (!head) { head = dom_new(d, N_ELEMENT, "head"); dom_insert_before(html, head, html->first); }
    if (!body) { body = dom_new(d, N_ELEMENT, "body"); dom_append(html, body); }
    for (struct dnode *k = d->root->first; k;) {
        struct dnode *nx = k->next;
        if (k != html && k->type != N_COMMENT) { dom_remove(k); if (k->type == N_ELEMENT && head_only(k->tag)) dom_append(head, k); else dom_append(body, k); }
        k = nx;
    }
    for (struct dnode *k = html->first; k;) {
        struct dnode *nx = k->next;
        if (k != head && k != body && !(k->type == N_TEXT && strspn(k->text, " \t\r\n") == strlen(k->text)) && k->type != N_COMMENT) {
            dom_remove(k); if (k->type == N_ELEMENT && (head_only(k->tag) || !strcmp(k->tag, "style") || !strcmp(k->tag, "script")) && !body->first) dom_append(head, k); else dom_append(body, k);
        }
        k = nx;
    }
}
void dom_set_inner_html(struct dnode *n, const char *html) {
    while (n->first) dom_remove(n->first);
    dom_parse_into(n->doc, n, html, 1);
    n->doc->dirty = 1;
}

/* ------------------------------------------------------------------ selectors */
enum { PS_NONE, PS_FIRST, PS_LAST, PS_ONLY, PS_NEVER, PS_LINK, PS_ROOT, PS_EMPTY, PS_NOT };
struct sattr { char name[32]; char op; char val[64]; };   /* op: 0 exists, '=', '~', '^', '$', '*', '|' */
struct scomp {
    char tag[24]; char idn[48]; char cls[4][48]; int ncls; struct sattr at[3]; int nat; int pseudo[3]; int npseudo;
    struct scomp *notc;
    char comb;   /* combinator to the previous compound: ' ', '>', '+', '~' */
};
struct sone { struct scomp c[8]; int n; int spec; };
struct wsel { struct sone *s; size_t n; };

static const char *parse_compound(const char *q, struct scomp *c, int *ok) {
    memset(c, 0, sizeof *c);
    if (*q == '*') q++;
    else { size_t k = 0; while (*q && (isalnum((unsigned char)*q) || *q == '-' || *q == '_')) { if (k < sizeof c->tag - 1) c->tag[k++] = (char)(*q >= 'A' && *q <= 'Z' ? *q + 32 : *q); q++; } c->tag[k] = 0; }
    for (;;) {
        if (*q == '#') { q++; size_t k = 0; while (*q && (isalnum((unsigned char)*q) || *q == '-' || *q == '_')) { if (k < sizeof c->idn - 1) c->idn[k++] = *q; q++; } c->idn[k] = 0; }
        else if (*q == '.') { q++; size_t k = 0; char *dst = c->ncls < 4 ? c->cls[c->ncls++] : NULL; while (*q && (isalnum((unsigned char)*q) || *q == '-' || *q == '_')) { if (dst && k < 47) dst[k++] = *q; q++; } if (dst) dst[k] = 0; }
        else if (*q == '[') {
            q++; struct sattr tmp = { { 0 }, 0, { 0 } }; struct sattr *a = c->nat < 3 ? &c->at[c->nat++] : &tmp;
            while (is_space(*q)) q++;
            size_t k = 0; while (*q && *q != ']' && *q != '=' && *q != '~' && *q != '^' && *q != '$' && *q != '*' && *q != '|' && !is_space(*q)) { if (k < 31) a->name[k++] = (char)(*q >= 'A' && *q <= 'Z' ? *q + 32 : *q); q++; }
            a->name[k] = 0; while (is_space(*q)) q++;
            if (*q && *q != ']') {
                if (*q == '=') a->op = '='; else { a->op = *q; q++; }
                if (*q == '=') q++;
                while (is_space(*q)) q++;
                char qc = *q == '"' || *q == '\'' ? *q++ : 0; k = 0;
                while (*q && (qc ? *q != qc : (*q != ']' && !is_space(*q)))) { if (k < 63) a->val[k++] = *q; q++; }
                a->val[k] = 0; if (qc && *q) q++;
                while (is_space(*q)) q++;
            }
            if (*q == ']') q++; else { *ok = 0; return q; }
        }
        else if (*q == ':') {
            q++; if (*q == ':') { *ok = 0; return q; }   /* pseudo-elements never match an element */
            char name[32]; size_t k = 0; while (*q && (isalnum((unsigned char)*q) || *q == '-')) { if (k < 31) name[k++] = *q; q++; } name[k] = 0;
            int ps = PS_NEVER;
            if (!strcmp(name, "first-child")) ps = PS_FIRST; else if (!strcmp(name, "last-child")) ps = PS_LAST; else if (!strcmp(name, "only-child")) ps = PS_ONLY;
            else if (!strcmp(name, "link") || !strcmp(name, "any-link")) ps = PS_LINK; else if (!strcmp(name, "visited")) ps = PS_NEVER;
            else if (!strcmp(name, "root")) ps = PS_ROOT; else if (!strcmp(name, "empty")) ps = PS_EMPTY;
            else if (!strcmp(name, "not") && *q == '(') {
                q++; while (is_space(*q)) q++;
                c->notc = calloc(1, sizeof *c->notc); q = parse_compound(q, c->notc, ok); while (is_space(*q)) q++;
                if (*q == ')') q++; else *ok = 0;
                ps = PS_NOT;
            }
            if (*q == '(') { int depth = 0; while (*q) { if (*q == '(') depth++; else if (*q == ')' && --depth == 0) { q++; break; } q++; } }
            if (c->npseudo < 3) c->pseudo[c->npseudo++] = ps;
        }
        else break;
    }
    return q;
}
static void free_comp(struct scomp *c) { if (c->notc) { free_comp(c->notc); free(c->notc); } }
void sel_free(struct wsel *s) {
    if (!s) return;
    for (size_t i = 0; i < s->n; i++) for (int k = 0; k < s->s[i].n; k++) free_comp(&s->s[i].c[k]);
    free(s->s); free(s);
}
struct wsel *sel_parse(const char *text, int *okp) {
    struct wsel *s = calloc(1, sizeof *s); int allok = 1;
    const char *q = text;
    while (*q) {
        while (is_space(*q) || *q == ',') q++;
        if (!*q) break;
        struct sone one; memset(&one, 0, sizeof one); int ok = 1; char comb = ' ';
        while (*q && *q != ',' && ok) {
            while (is_space(*q)) q++;
            if (*q == '>' || *q == '+' || *q == '~') { comb = *q++; continue; }
            if (!*q || *q == ',') break;
            if (one.n == 8) { ok = 0; break; }
            const char *before = q;
            q = parse_compound(q, &one.c[one.n], &ok);
            if (q == before) { ok = 0; break; }
            one.c[one.n].comb = one.n ? comb : ' '; one.n++; comb = ' ';
        }
        while (*q && *q != ',') q++;   /* skip the rest of an unsupported selector */
        if (ok && one.n) {
            int a = 0, b = 0, c = 0;
            for (int k = 0; k < one.n; k++) { struct scomp *x = &one.c[k]; if (x->idn[0]) a++; b += x->ncls + x->nat + x->npseudo; if (x->tag[0]) c++; }
            one.spec = a * 10000 + b * 100 + c;
            s->s = realloc(s->s, (s->n + 1) * sizeof *s->s); s->s[s->n++] = one;
        } else { for (int k = 0; k < one.n; k++) free_comp(&one.c[k]); allok = 0; }
    }
    if (okp) *okp = allok && s->n > 0;
    return s;
}
static struct dnode *prev_el(const struct dnode *n) { for (struct dnode *p = n->prev; p; p = p->prev) if (p->type == N_ELEMENT) return p; return NULL; }
static struct dnode *next_el(const struct dnode *n) { for (struct dnode *p = n->next; p; p = p->next) if (p->type == N_ELEMENT) return p; return NULL; }
static int comp_match(const struct scomp *c, const struct dnode *el) {
    if (!el || el->type != N_ELEMENT) return 0;
    if (c->tag[0] && strcmp(c->tag, el->tag)) return 0;
    if (c->idn[0]) { const char *v = dom_attr(el, "id"); if (!v || strcmp(v, c->idn)) return 0; }
    for (int i = 0; i < c->ncls; i++) if (!dom_has_class(el, c->cls[i])) return 0;
    for (int i = 0; i < c->nat; i++) {
        const struct sattr *a = &c->at[i]; const char *v = dom_attr(el, a->name);
        if (!v) return 0;
        size_t vl = strlen(v), al = strlen(a->val);
        switch (a->op) {
        case '=': if (strcmp(v, a->val)) return 0; break;
        case '~': { const char *p = v; int f = 0; while (*p) { while (is_space(*p)) p++; size_t k = strcspn(p, " \t\n"); if (k == al && !strncmp(p, a->val, al)) f = 1; p += k; } if (!f) return 0; break; }
        case '^': if (strncmp(v, a->val, al)) return 0; break;
        case '$': if (vl < al || strcmp(v + vl - al, a->val)) return 0; break;
        case '*': if (!strstr(v, a->val)) return 0; break;
        case '|': if (strncmp(v, a->val, al) || (v[al] && v[al] != '-')) return 0; break;
        }
    }
    for (int i = 0; i < c->npseudo; i++) {
        switch (c->pseudo[i]) {
        case PS_FIRST: if (prev_el(el)) return 0; break;
        case PS_LAST: if (next_el(el)) return 0; break;
        case PS_ONLY: if (prev_el(el) || next_el(el)) return 0; break;
        case PS_LINK: if (strcmp(el->tag, "a") || !dom_attr(el, "href")) return 0; break;
        case PS_ROOT: if (!el->parent || el->parent->type != N_DOCUMENT) return 0; break;
        case PS_EMPTY: if (el->first) return 0; break;
        case PS_NOT: if (c->notc && comp_match(c->notc, el)) return 0; break;
        default: return 0;
        }
    }
    return 1;
}
/* matches compounds [0..k] ending at el */
static int match_from(const struct sone *s, int k, const struct dnode *el) {
    if (!comp_match(&s->c[k], el)) return 0;
    if (k == 0) return 1;
    char comb = s->c[k].comb;
    if (comb == '>') return el->parent && el->parent->type == N_ELEMENT && match_from(s, k - 1, el->parent);
    if (comb == '+') { struct dnode *p = prev_el(el); return p && match_from(s, k - 1, p); }
    if (comb == '~') { for (struct dnode *p = prev_el(el); p; p = prev_el(p)) if (match_from(s, k - 1, p)) return 1; return 0; }
    for (struct dnode *p = el->parent; p && p->type == N_ELEMENT; p = p->parent) if (match_from(s, k - 1, p)) return 1;
    return 0;
}
size_t sel_count(const struct wsel *s) { return s ? s->n : 0; }
int sel_matches_one(const struct wsel *s, size_t i, const struct dnode *el) { return match_from(&s->s[i], s->s[i].n - 1, el); }
int sel_specificity(const struct wsel *s, size_t i) { return s->s[i].spec; }
int sel_matches(const struct wsel *s, const struct dnode *el, int *spec) {
    int best = -1;
    for (size_t i = 0; s && i < s->n; i++) if (match_from(&s->s[i], s->s[i].n - 1, el) && s->s[i].spec > best) best = s->s[i].spec;
    if (spec) *spec = best;
    return best >= 0;
}
