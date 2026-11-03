/* JavaScript for UIWebView pages: QuickJS with a DOM binding.
 *
 * Every DOM node gets one JS object (class Node, cached on the node so
 * identity and expando properties such as onclick hold). The C side covers
 * the tree, attributes, inline styles, geometry and navigation; a JS prelude
 * adds the parts that are easier there: events and listeners, classList,
 * timers bookkeeping, location, console, localStorage, small window APIs.
 *
 * Navigation leaves through webview_navigate(): assigning location or
 * loading an iframe (the common "command://..." bridge to the app) asks the
 * UIWebView delegate's webView:shouldStartLoadWithRequest:navigationType:. */
#include "webengine.h"
#include "quickjs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

char *rt_url_resolve(const char *base, const char *ref);

struct webjs { JSRuntime *rt; JSContext *ctx; struct wdocument *d; long serial; };
struct jtimer { long serial; long id; };
static JSClassID cls_node, cls_style;
/* live contexts by serial: a timer that fires after its page went away finds nothing */
static struct webjs *g_live[64]; static long g_serial;
static struct webjs *live_find(long serial) { for (int i = 0; i < 64; i++) if (g_live[i] && g_live[i]->serial == serial) return g_live[i]; return NULL; }

static struct webjs *J(JSContext *ctx) { return JS_GetContextOpaque(ctx); }
static struct dnode *NODE(JSValueConst v) { return JS_GetOpaque(v, cls_node); }

/* ------------------------------------------------------------------ wrappers */
static JSValue wrap(JSContext *ctx, struct dnode *n) {
    if (!n) return JS_NULL;
    if (n->jsptr) return JS_DupValue(ctx, JS_MKPTR(JS_TAG_OBJECT, n->jsptr));
    JSValue proto = JS_GetClassProto(ctx, cls_node);
    JSValue o = JS_NewObjectProtoClass(ctx, proto, cls_node);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(o)) return o;
    JS_SetOpaque(o, n);
    n->jsptr = JS_VALUE_GET_PTR(o);
    return JS_DupValue(ctx, o);   /* one reference stays with the node */
}
void webjs_node_freed(struct webjs *j, struct dnode *n) {
    if (j && n->jsptr) { JS_FreeValue(j->ctx, JS_MKPTR(JS_TAG_OBJECT, n->jsptr)); n->jsptr = NULL; }
}
static JSValue str(JSContext *ctx, const char *s) { return JS_NewString(ctx, s ? s : ""); }
static JSValue str_free(JSContext *ctx, char *s) { JSValue v = JS_NewString(ctx, s ? s : ""); free(s); return v; }
static char *arg_str(JSContext *ctx, JSValueConst v) {
    if (JS_IsNull(v) || JS_IsUndefined(v)) return strdup("");
    const char *s = JS_ToCString(ctx, v); if (!s) { JS_FreeValue(ctx, JS_GetException(ctx)); return strdup(""); }
    char *r = strdup(s); JS_FreeCString(ctx, s); return r;
}
typedef struct { struct dnode **v; size_t n, cap; } nvec;
static void nv_push(nvec *v, struct dnode *n) { if (v->n == v->cap) { v->cap = v->cap ? v->cap * 2 : 16; v->v = realloc(v->v, v->cap * sizeof *v->v); } v->v[v->n++] = n; }
static JSValue array_free(JSContext *ctx, nvec *v) {
    JSValue a = JS_NewArray(ctx);
    for (size_t i = 0; i < v->n; i++) JS_SetPropertyUint32(ctx, a, (uint32_t)i, wrap(ctx, v->v[i]));
    free(v->v);
    return a;
}

/* iframes inserted into the document (or given a src while in it) load: the app sees the request */
static void iframe_check(JSContext *ctx, struct dnode *root) {
    struct webjs *j = J(ctx);
    if (!root || !dom_connected(root)) return;
    for (struct dnode *n = root; n; n = dom_next_pre(n, root)) {
        if (n->type != N_ELEMENT || strcmp(n->tag, "iframe")) continue;
        const char *src = dom_attr(n, "src"); if (!src || !*src) continue;
        char *abs = rt_url_resolve(j->d->url ? j->d->url : "", src);
        webview_navigate(j->d->web, abs, 5, 0);
        free(abs);
    }
}

/* ------------------------------------------------------------------ node properties */
enum { G_NODETYPE, G_NODENAME, G_TAGNAME, G_ID, G_CLASSNAME, G_PARENT, G_PARENTEL, G_FIRST, G_LAST, G_NEXT, G_PREV, G_FIRSTEL, G_LASTEL,
       G_NEXTEL, G_PREVEL, G_CHILDNODES, G_CHILDREN, G_CHILDCOUNT, G_INNERHTML, G_OUTERHTML, G_TEXT, G_NODEVALUE, G_STYLE, G_OWNERDOC,
       G_OFFL, G_OFFT, G_OFFW, G_OFFH, G_CLIENTW, G_CLIENTH, G_SCROLLW, G_SCROLLH, G_SCROLLT, G_SCROLLL, G_HREF, G_SRC, G_VALUE, G_TITLE,
       G_NAME, G_TYPE, G_ALT, G_CHECKED, G_DISABLED, G_OFFPARENT, G_BODY, G_DOCEL, G_HEAD, G_DOCTITLE, G_READY, G_URL, G_ISCONN, G_DATASET };

static struct dnode *el_sib(struct dnode *n, int next) { for (n = next ? n->next : n->prev; n; n = next ? n->next : n->prev) if (n->type == N_ELEMENT) return n; return NULL; }
static struct dnode *el_child(struct dnode *n, int last) { for (struct dnode *k = last ? n->last : n->first; k; k = last ? k->prev : k->next) if (k->type == N_ELEMENT) return k; return NULL; }
static char *upper(const char *s) { char *r = strdup(s ? s : ""); for (char *p = r; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32; return r; }
static JSValue resolved_attr(JSContext *ctx, struct dnode *n, const char *name) {
    const char *v = dom_attr(n, name); if (!v) return str(ctx, "");
    return str_free(ctx, rt_url_resolve(J(ctx)->d->url ? J(ctx)->d->url : "", v));
}
static JSValue style_obj(JSContext *ctx, struct dnode *n) {
    JSValue proto = JS_GetClassProto(ctx, cls_style);
    JSValue o = JS_NewObjectProtoClass(ctx, proto, cls_style);
    JS_FreeValue(ctx, proto);
    JS_SetOpaque(o, n);
    return o;
}
static JSValue node_get(JSContext *ctx, JSValueConst this, int magic) {
    struct dnode *n = NODE(this); if (!n) return JS_UNDEFINED;
    struct wdocument *d = J(ctx)->d;
    switch (magic) {
    case G_NODETYPE: return JS_NewInt32(ctx, n->type);
    case G_NODENAME: return n->type == N_ELEMENT ? str_free(ctx, upper(n->tag)) : str(ctx, n->type == N_TEXT ? "#text" : n->type == N_DOCUMENT ? "#document" : n->type == N_COMMENT ? "#comment" : "#document-fragment");
    case G_TAGNAME: return n->type == N_ELEMENT ? str_free(ctx, upper(n->tag)) : JS_UNDEFINED;
    case G_ID: return str(ctx, dom_attr(n, "id"));
    case G_CLASSNAME: return str(ctx, dom_attr(n, "class"));
    case G_PARENT: return wrap(ctx, n->parent);
    case G_PARENTEL: return wrap(ctx, n->parent && n->parent->type == N_ELEMENT ? n->parent : NULL);
    case G_FIRST: return wrap(ctx, n->first);
    case G_LAST: return wrap(ctx, n->last);
    case G_NEXT: return wrap(ctx, n->next);
    case G_PREV: return wrap(ctx, n->prev);
    case G_FIRSTEL: return wrap(ctx, el_child(n, 0));
    case G_LASTEL: return wrap(ctx, el_child(n, 1));
    case G_NEXTEL: return wrap(ctx, el_sib(n, 1));
    case G_PREVEL: return wrap(ctx, el_sib(n, 0));
    case G_CHILDNODES: case G_CHILDREN: { nvec v = { 0 }; for (struct dnode *k = n->first; k; k = k->next) if (magic == G_CHILDNODES || k->type == N_ELEMENT) nv_push(&v, k); return array_free(ctx, &v); }
    case G_CHILDCOUNT: { int c = 0; for (struct dnode *k = n->first; k; k = k->next) if (k->type == N_ELEMENT) c++; return JS_NewInt32(ctx, c); }
    case G_INNERHTML: return str_free(ctx, dom_serialize_children(n));
    case G_OUTERHTML: return str_free(ctx, dom_serialize(n));
    case G_TEXT: return n->type == N_TEXT || n->type == N_COMMENT ? str(ctx, n->text) : n->type == N_DOCUMENT ? JS_NULL : str_free(ctx, dom_text_content(n));
    case G_NODEVALUE: return n->type == N_TEXT || n->type == N_COMMENT ? str(ctx, n->text) : JS_NULL;
    case G_STYLE: return n->type == N_ELEMENT ? style_obj(ctx, n) : JS_UNDEFINED;
    case G_OWNERDOC: return n->type == N_DOCUMENT ? JS_NULL : wrap(ctx, d->root);
    case G_OFFL: case G_OFFT: case G_OFFW: case G_OFFH: case G_CLIENTW: case G_CLIENTH: case G_SCROLLW: case G_SCROLLH: {
        web_ensure_layout(d);
        if (n->type == N_DOCUMENT) n = dom_html(d);
        if (!n || !n->laid) return JS_NewInt32(ctx, 0);
        double bx = n->cs.border[1] + n->cs.border[3], by = n->cs.border[0] + n->cs.border[2], v = 0;
        switch (magic) {
        case G_OFFL: v = n->box.origin.x; break; case G_OFFT: v = n->box.origin.y; break;
        case G_OFFW: v = n->box.size.width; break; case G_OFFH: v = n->box.size.height; break;
        case G_CLIENTW: case G_SCROLLW: v = n->box.size.width - bx; break;
        case G_CLIENTH: v = n->box.size.height - by; if (magic == G_SCROLLH && n == dom_html(d)) v = fmax(v, d->height); break;
        case G_SCROLLH: v = n == dom_html(d) || n == dom_body(d) ? fmax(d->height, n->box.size.height - by) : n->box.size.height - by; break;
        }
        return JS_NewInt32(ctx, (int32_t)lround(v));
    }
    case G_SCROLLT: case G_SCROLLL: return JS_NewInt32(ctx, 0);
    case G_HREF: return n->type == N_ELEMENT && dom_attr(n, "href") ? resolved_attr(ctx, n, "href") : str(ctx, "");
    case G_SRC: return dom_attr(n, "src") ? resolved_attr(ctx, n, "src") : str(ctx, "");
    case G_VALUE: return str(ctx, dom_attr(n, "value"));
    case G_TITLE: return str(ctx, dom_attr(n, "title"));
    case G_NAME: return str(ctx, dom_attr(n, "name"));
    case G_TYPE: return str(ctx, dom_attr(n, "type"));
    case G_ALT: return str(ctx, dom_attr(n, "alt"));
    case G_CHECKED: return JS_NewBool(ctx, dom_attr(n, "checked") != NULL);
    case G_DISABLED: return JS_NewBool(ctx, dom_attr(n, "disabled") != NULL);
    case G_OFFPARENT: { for (struct dnode *p = n->parent; p && p->type == N_ELEMENT; p = p->parent) if (p->cs.position != P_STATIC || !strcmp(p->tag, "body")) return wrap(ctx, p); return JS_NULL; }
    case G_BODY: return wrap(ctx, dom_body(d));
    case G_DOCEL: return wrap(ctx, dom_html(d));
    case G_HEAD: return wrap(ctx, dom_head(d));
    case G_DOCTITLE: {
        for (struct dnode *k = d->root; k; k = dom_next_pre(k, d->root)) if (k->type == N_ELEMENT && !strcmp(k->tag, "title")) return str_free(ctx, dom_text_content(k));
        return str(ctx, "");
    }
    case G_READY: return str(ctx, "complete");
    case G_URL: return str(ctx, d->url);
    case G_ISCONN: return JS_NewBool(ctx, dom_connected(n));
    }
    return JS_UNDEFINED;
}
static JSValue node_set(JSContext *ctx, JSValueConst this, JSValueConst val, int magic) {
    struct dnode *n = NODE(this); if (!n) return JS_UNDEFINED;
    char *v = arg_str(ctx, val);
    switch (magic) {
    case G_ID: dom_set_attr(n, "id", v); break;
    case G_CLASSNAME: dom_set_attr(n, "class", v); break;
    case G_INNERHTML: if (n->type == N_ELEMENT) { dom_set_inner_html(n, v); iframe_check(ctx, n); } break;
    case G_TEXT: case G_NODEVALUE:
        if (n->type == N_TEXT || n->type == N_COMMENT) { free(n->text); n->text = v; v = NULL; n->doc->dirty = 1; }
        else if (n->type == N_ELEMENT && magic == G_TEXT) dom_set_text_content(n, v);
        break;
    case G_HREF: dom_set_attr(n, "href", v); break;
    case G_SRC: dom_set_attr(n, "src", v); if (!strcmp(n->tag, "iframe")) iframe_check(ctx, n); break;
    case G_VALUE: dom_set_attr(n, "value", v); break;
    case G_TITLE: dom_set_attr(n, "title", v); break;
    case G_NAME: dom_set_attr(n, "name", v); break;
    case G_TYPE: dom_set_attr(n, "type", v); break;
    case G_ALT: dom_set_attr(n, "alt", v); break;
    case G_CHECKED: case G_DISABLED: { const char *a = magic == G_CHECKED ? "checked" : "disabled"; if (JS_ToBool(ctx, val)) dom_set_attr(n, a, ""); else dom_remove_attr(n, a); break; }
    case G_DOCTITLE: {
        struct wdocument *d = J(ctx)->d; struct dnode *t = NULL;
        for (struct dnode *k = d->root; k; k = dom_next_pre(k, d->root)) if (k->type == N_ELEMENT && !strcmp(k->tag, "title")) { t = k; break; }
        if (!t) { t = dom_new(d, N_ELEMENT, "title"); if (dom_head(d)) dom_append(dom_head(d), t); }
        dom_set_text_content(t, v); free(d->title); d->title = strdup(v);
        break;
    }
    case G_STYLE: dom_set_attr(n, "style", v); break;
    default: break;
    }
    free(v);
    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------ node methods */
enum { M_GETATTR, M_SETATTR, M_HASATTR, M_REMATTR, M_APPEND, M_REMOVECHILD, M_INSERT, M_REPLACE, M_CLONE, M_CONTAINS, M_HASCHILD,
       M_BYTAG, M_BYCLASS, M_QS, M_QSA, M_RECT, M_MATCHES, M_REMOVE, M_BYID, M_CREATE, M_CREATETEXT, M_CREATEFRAG, M_CREATECOMMENT, M_FROMPOINT,
       M_BYNAME, M_GETCOMPUTED, M_INSERTADJ };

static void collect(struct dnode *root, int (*pred)(struct dnode *, void *), void *arg, nvec *out) {
    for (struct dnode *n = root->first ? root->first : NULL; n; n = dom_next_pre(n, root)) if (n->type == N_ELEMENT && pred(n, arg)) nv_push(out, n);
}
static int p_tag(struct dnode *n, void *a) { return !strcmp((const char *)a, "*") || web_ieq(n->tag, (const char *)a); }
static int p_class(struct dnode *n, void *a) {
    char *list = strdup((const char *)a); int ok = 1; char *save;
    for (char *c = strtok_r(list, " \t\n", &save); c; c = strtok_r(NULL, " \t\n", &save)) if (!dom_has_class(n, c)) { ok = 0; break; }
    free(list); return ok;
}
static int p_name(struct dnode *n, void *a) { const char *v = dom_attr(n, "name"); return v && !strcmp(v, (const char *)a); }
static int p_sel(struct dnode *n, void *a) { return sel_matches((struct wsel *)a, n, NULL); }

static JSValue node_call(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv, int magic) {
    struct dnode *n = NODE(this);
    struct wdocument *d = J(ctx)->d;
    if (!n) return JS_ThrowTypeError(ctx, "not a node");
    #define ARG(i) (argc > (i) ? argv[i] : JS_UNDEFINED)
    switch (magic) {
    case M_GETATTR: { char *a = arg_str(ctx, ARG(0)); const char *v = dom_attr(n, a); free(a); return v ? str(ctx, v) : JS_NULL; }
    case M_SETATTR: { char *a = arg_str(ctx, ARG(0)), *v = arg_str(ctx, ARG(1)); dom_set_attr(n, a, v); if (web_ieq(a, "src") && n->tag && !strcmp(n->tag, "iframe")) iframe_check(ctx, n); free(a); free(v); return JS_UNDEFINED; }
    case M_HASATTR: { char *a = arg_str(ctx, ARG(0)); int r = dom_attr(n, a) != NULL; free(a); return JS_NewBool(ctx, r); }
    case M_REMATTR: { char *a = arg_str(ctx, ARG(0)); dom_remove_attr(n, a); free(a); return JS_UNDEFINED; }
    case M_APPEND: case M_INSERT: {
        struct dnode *c = NODE(ARG(0)); if (!c) return JS_ThrowTypeError(ctx, "appendChild: not a node");
        struct dnode *ref = magic == M_INSERT ? NODE(ARG(1)) : NULL;
        int frag = c->type == N_FRAGMENT; struct dnode *first = frag ? c->first : c;
        dom_insert_before(n, c, ref);
        if (frag) { for (struct dnode *k = first; k && k->parent == n; k = k->next) iframe_check(ctx, k); } else iframe_check(ctx, c);
        return JS_DupValue(ctx, ARG(0));
    }
    case M_REMOVECHILD: { struct dnode *c = NODE(ARG(0)); if (c && c->parent == n) dom_remove(c); return JS_DupValue(ctx, ARG(0)); }
    case M_REPLACE: { struct dnode *c = NODE(ARG(0)), *old = NODE(ARG(1)); if (c && old && old->parent == n) { dom_insert_before(n, c, old); dom_remove(old); iframe_check(ctx, c); } return JS_DupValue(ctx, ARG(1)); }
    case M_CLONE: return wrap(ctx, dom_clone(n, argc > 0 && JS_ToBool(ctx, ARG(0))));
    case M_CONTAINS: { struct dnode *c = NODE(ARG(0)); return JS_NewBool(ctx, c && dom_contains(n, c)); }
    case M_HASCHILD: return JS_NewBool(ctx, n->first != NULL);
    case M_BYTAG: { char *a = arg_str(ctx, ARG(0)); nvec v = { 0 }; collect(n, p_tag, a, &v); free(a); return array_free(ctx, &v); }
    case M_BYCLASS: { char *a = arg_str(ctx, ARG(0)); nvec v = { 0 }; collect(n, p_class, a, &v); free(a); return array_free(ctx, &v); }
    case M_BYNAME: { char *a = arg_str(ctx, ARG(0)); nvec v = { 0 }; collect(n, p_name, a, &v); free(a); return array_free(ctx, &v); }
    case M_QS: case M_QSA: {
        char *a = arg_str(ctx, ARG(0)); int ok; struct wsel *s = sel_parse(a, &ok); free(a);
        if (!sel_count(s)) { sel_free(s); return magic == M_QS ? JS_NULL : JS_NewArray(ctx); }
        nvec v = { 0 }; collect(n, p_sel, s, &v); sel_free(s);
        if (magic == M_QS) { JSValue r = v.n ? wrap(ctx, v.v[0]) : JS_NULL; free(v.v); return r; }
        return array_free(ctx, &v);
    }
    case M_MATCHES: { char *a = arg_str(ctx, ARG(0)); int ok; struct wsel *s = sel_parse(a, &ok); free(a); int r = sel_matches(s, n, NULL); sel_free(s); return JS_NewBool(ctx, r); }
    case M_RECT: {
        web_ensure_layout(d);
        JSValue o = JS_NewObject(ctx); CGRect b = n->laid ? n->box : (CGRect){ { 0, 0 }, { 0, 0 } };
        JS_SetPropertyStr(ctx, o, "left", JS_NewFloat64(ctx, b.origin.x)); JS_SetPropertyStr(ctx, o, "top", JS_NewFloat64(ctx, b.origin.y));
        JS_SetPropertyStr(ctx, o, "x", JS_NewFloat64(ctx, b.origin.x)); JS_SetPropertyStr(ctx, o, "y", JS_NewFloat64(ctx, b.origin.y));
        JS_SetPropertyStr(ctx, o, "width", JS_NewFloat64(ctx, b.size.width)); JS_SetPropertyStr(ctx, o, "height", JS_NewFloat64(ctx, b.size.height));
        JS_SetPropertyStr(ctx, o, "right", JS_NewFloat64(ctx, b.origin.x + b.size.width)); JS_SetPropertyStr(ctx, o, "bottom", JS_NewFloat64(ctx, b.origin.y + b.size.height));
        return o;
    }
    case M_REMOVE: dom_remove(n); return JS_UNDEFINED;
    case M_BYID: { char *a = arg_str(ctx, ARG(0)); struct dnode *r = dom_by_id(d->root, a); free(a); return wrap(ctx, r); }
    case M_CREATE: { char *a = arg_str(ctx, ARG(0)); struct dnode *e = dom_new(d, N_ELEMENT, a); free(a); return wrap(ctx, e); }
    case M_CREATETEXT: { char *a = arg_str(ctx, ARG(0)); struct dnode *e = dom_new(d, N_TEXT, a); free(a); return wrap(ctx, e); }
    case M_CREATECOMMENT: { char *a = arg_str(ctx, ARG(0)); struct dnode *e = dom_new(d, N_COMMENT, a); free(a); return wrap(ctx, e); }
    case M_CREATEFRAG: return wrap(ctx, dom_new(d, N_FRAGMENT, NULL));
    case M_FROMPOINT: {
        double x = 0, y = 0; JS_ToFloat64(ctx, &x, ARG(0)); JS_ToFloat64(ctx, &y, ARG(1));
        struct dnode *h = web_hit(d, x, y);
        if (!h && x >= 0 && y >= 0 && x < d->vw && y < d->vh) h = dom_html(d);
        return wrap(ctx, h);
    }
    case M_GETCOMPUTED: {
        struct dnode *e = NODE(ARG(0)); if (!e) return JS_NULL;
        web_ensure_layout(d);
        JSValue o = JS_NewObject(ctx); char buf[64];
        static const char *disp[] = { "none", "block", "inline", "inline-block", "list-item" }, *pos[] = { "static", "relative", "absolute", "fixed" };
        JS_SetPropertyStr(ctx, o, "display", str(ctx, disp[e->cs.display])); JS_SetPropertyStr(ctx, o, "position", str(ctx, pos[e->cs.position]));
        JS_SetPropertyStr(ctx, o, "visibility", str(ctx, e->cs.vis_hidden ? "hidden" : "visible"));
        snprintf(buf, sizeof buf, "%g", e->cs.opacity); JS_SetPropertyStr(ctx, o, "opacity", str(ctx, buf));
        snprintf(buf, sizeof buf, "%gpx", e->laid ? e->box.size.width : 0); JS_SetPropertyStr(ctx, o, "width", str(ctx, buf));
        snprintf(buf, sizeof buf, "%gpx", e->laid ? e->box.size.height : 0); JS_SetPropertyStr(ctx, o, "height", str(ctx, buf));
        snprintf(buf, sizeof buf, "%gpx", e->cs.size); JS_SetPropertyStr(ctx, o, "fontSize", str(ctx, buf));
        snprintf(buf, sizeof buf, "rgba(%d, %d, %d, %g)", (int)lround(e->cs.color[0] * 255), (int)lround(e->cs.color[1] * 255), (int)lround(e->cs.color[2] * 255), e->cs.color[3]);
        JS_SetPropertyStr(ctx, o, "color", str(ctx, buf));
        return o;
    }
    case M_INSERTADJ: {
        char *where = arg_str(ctx, ARG(0)), *html = arg_str(ctx, ARG(1));
        struct dnode *frag = dom_new(d, N_FRAGMENT, NULL); dom_parse_into(d, frag, html, 1);
        if (web_ieq(where, "beforeend")) dom_append(n, frag);
        else if (web_ieq(where, "afterbegin")) dom_insert_before(n, frag, n->first);
        else if (web_ieq(where, "beforebegin") && n->parent) dom_insert_before(n->parent, frag, n);
        else if (web_ieq(where, "afterend") && n->parent) dom_insert_before(n->parent, frag, n->next);
        free(where); free(html); return JS_UNDEFINED;
    }
    }
    #undef ARG
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry node_proto[] = {
    JS_CGETSET_MAGIC_DEF("nodeType", node_get, NULL, G_NODETYPE), JS_CGETSET_MAGIC_DEF("nodeName", node_get, NULL, G_NODENAME),
    JS_CGETSET_MAGIC_DEF("tagName", node_get, NULL, G_TAGNAME), JS_CGETSET_MAGIC_DEF("localName", node_get, NULL, G_TAGNAME),
    JS_CGETSET_MAGIC_DEF("id", node_get, node_set, G_ID), JS_CGETSET_MAGIC_DEF("className", node_get, node_set, G_CLASSNAME),
    JS_CGETSET_MAGIC_DEF("parentNode", node_get, NULL, G_PARENT), JS_CGETSET_MAGIC_DEF("parentElement", node_get, NULL, G_PARENTEL),
    JS_CGETSET_MAGIC_DEF("firstChild", node_get, NULL, G_FIRST), JS_CGETSET_MAGIC_DEF("lastChild", node_get, NULL, G_LAST),
    JS_CGETSET_MAGIC_DEF("nextSibling", node_get, NULL, G_NEXT), JS_CGETSET_MAGIC_DEF("previousSibling", node_get, NULL, G_PREV),
    JS_CGETSET_MAGIC_DEF("firstElementChild", node_get, NULL, G_FIRSTEL), JS_CGETSET_MAGIC_DEF("lastElementChild", node_get, NULL, G_LASTEL),
    JS_CGETSET_MAGIC_DEF("nextElementSibling", node_get, NULL, G_NEXTEL), JS_CGETSET_MAGIC_DEF("previousElementSibling", node_get, NULL, G_PREVEL),
    JS_CGETSET_MAGIC_DEF("childNodes", node_get, NULL, G_CHILDNODES), JS_CGETSET_MAGIC_DEF("children", node_get, NULL, G_CHILDREN),
    JS_CGETSET_MAGIC_DEF("childElementCount", node_get, NULL, G_CHILDCOUNT),
    JS_CGETSET_MAGIC_DEF("innerHTML", node_get, node_set, G_INNERHTML), JS_CGETSET_MAGIC_DEF("outerHTML", node_get, NULL, G_OUTERHTML),
    JS_CGETSET_MAGIC_DEF("textContent", node_get, node_set, G_TEXT), JS_CGETSET_MAGIC_DEF("innerText", node_get, node_set, G_TEXT),
    JS_CGETSET_MAGIC_DEF("nodeValue", node_get, node_set, G_NODEVALUE), JS_CGETSET_MAGIC_DEF("data", node_get, node_set, G_NODEVALUE),
    JS_CGETSET_MAGIC_DEF("style", node_get, node_set, G_STYLE), JS_CGETSET_MAGIC_DEF("ownerDocument", node_get, NULL, G_OWNERDOC),
    JS_CGETSET_MAGIC_DEF("offsetLeft", node_get, NULL, G_OFFL), JS_CGETSET_MAGIC_DEF("offsetTop", node_get, NULL, G_OFFT),
    JS_CGETSET_MAGIC_DEF("offsetWidth", node_get, NULL, G_OFFW), JS_CGETSET_MAGIC_DEF("offsetHeight", node_get, NULL, G_OFFH),
    JS_CGETSET_MAGIC_DEF("clientWidth", node_get, NULL, G_CLIENTW), JS_CGETSET_MAGIC_DEF("clientHeight", node_get, NULL, G_CLIENTH),
    JS_CGETSET_MAGIC_DEF("scrollWidth", node_get, NULL, G_SCROLLW), JS_CGETSET_MAGIC_DEF("scrollHeight", node_get, NULL, G_SCROLLH),
    JS_CGETSET_MAGIC_DEF("scrollTop", node_get, node_set, G_SCROLLT), JS_CGETSET_MAGIC_DEF("scrollLeft", node_get, node_set, G_SCROLLL),
    JS_CGETSET_MAGIC_DEF("offsetParent", node_get, NULL, G_OFFPARENT), JS_CGETSET_MAGIC_DEF("isConnected", node_get, NULL, G_ISCONN),
    JS_CGETSET_MAGIC_DEF("href", node_get, node_set, G_HREF), JS_CGETSET_MAGIC_DEF("src", node_get, node_set, G_SRC),
    JS_CGETSET_MAGIC_DEF("value", node_get, node_set, G_VALUE), JS_CGETSET_MAGIC_DEF("title", node_get, node_set, G_TITLE),
    JS_CGETSET_MAGIC_DEF("name", node_get, node_set, G_NAME), JS_CGETSET_MAGIC_DEF("type", node_get, node_set, G_TYPE),
    JS_CGETSET_MAGIC_DEF("alt", node_get, node_set, G_ALT), JS_CGETSET_MAGIC_DEF("checked", node_get, node_set, G_CHECKED),
    JS_CGETSET_MAGIC_DEF("disabled", node_get, node_set, G_DISABLED),
    JS_CFUNC_MAGIC_DEF("getAttribute", 1, node_call, M_GETATTR), JS_CFUNC_MAGIC_DEF("setAttribute", 2, node_call, M_SETATTR),
    JS_CFUNC_MAGIC_DEF("hasAttribute", 1, node_call, M_HASATTR), JS_CFUNC_MAGIC_DEF("removeAttribute", 1, node_call, M_REMATTR),
    JS_CFUNC_MAGIC_DEF("appendChild", 1, node_call, M_APPEND), JS_CFUNC_MAGIC_DEF("removeChild", 1, node_call, M_REMOVECHILD),
    JS_CFUNC_MAGIC_DEF("insertBefore", 2, node_call, M_INSERT), JS_CFUNC_MAGIC_DEF("replaceChild", 2, node_call, M_REPLACE),
    JS_CFUNC_MAGIC_DEF("cloneNode", 1, node_call, M_CLONE), JS_CFUNC_MAGIC_DEF("contains", 1, node_call, M_CONTAINS),
    JS_CFUNC_MAGIC_DEF("hasChildNodes", 0, node_call, M_HASCHILD),
    JS_CFUNC_MAGIC_DEF("getElementsByTagName", 1, node_call, M_BYTAG), JS_CFUNC_MAGIC_DEF("getElementsByClassName", 1, node_call, M_BYCLASS),
    JS_CFUNC_MAGIC_DEF("querySelector", 1, node_call, M_QS), JS_CFUNC_MAGIC_DEF("querySelectorAll", 1, node_call, M_QSA),
    JS_CFUNC_MAGIC_DEF("getBoundingClientRect", 0, node_call, M_RECT), JS_CFUNC_MAGIC_DEF("matches", 1, node_call, M_MATCHES),
    JS_CFUNC_MAGIC_DEF("webkitMatchesSelector", 1, node_call, M_MATCHES), JS_CFUNC_MAGIC_DEF("remove", 0, node_call, M_REMOVE),
    JS_CFUNC_MAGIC_DEF("insertAdjacentHTML", 2, node_call, M_INSERTADJ),
    /* document methods (harmless on other nodes) */
    JS_CFUNC_MAGIC_DEF("getElementById", 1, node_call, M_BYID), JS_CFUNC_MAGIC_DEF("createElement", 1, node_call, M_CREATE),
    JS_CFUNC_MAGIC_DEF("createTextNode", 1, node_call, M_CREATETEXT), JS_CFUNC_MAGIC_DEF("createComment", 1, node_call, M_CREATECOMMENT),
    JS_CFUNC_MAGIC_DEF("createDocumentFragment", 0, node_call, M_CREATEFRAG), JS_CFUNC_MAGIC_DEF("elementFromPoint", 2, node_call, M_FROMPOINT),
    JS_CFUNC_MAGIC_DEF("getElementsByName", 1, node_call, M_BYNAME), JS_CFUNC_MAGIC_DEF("__computed", 1, node_call, M_GETCOMPUTED),
    JS_CGETSET_MAGIC_DEF("body", node_get, NULL, G_BODY), JS_CGETSET_MAGIC_DEF("documentElement", node_get, NULL, G_DOCEL),
    JS_CGETSET_MAGIC_DEF("head", node_get, NULL, G_HEAD), JS_CGETSET_MAGIC_DEF("readyState", node_get, NULL, G_READY),
    JS_CGETSET_MAGIC_DEF("URL", node_get, NULL, G_URL),
};

/* ------------------------------------------------------------------ element.style */
static char *css_name(const char *js) {   /* backgroundColor -> background-color, webkitTransform -> -webkit-transform */
    char *out = malloc(strlen(js) * 2 + 3), *o = out;
    if (!strncmp(js, "webkit", 6) || !strncmp(js, "Webkit", 6)) *o++ = '-';
    for (const char *p = js; *p; p++) { if (*p >= 'A' && *p <= 'Z') { *o++ = '-'; *o++ = (char)(*p + 32); } else *o++ = *p; }
    *o = 0;
    if (out[0] == '-' && out[1] == 'W') out[1] = 'w';
    if (!strcmp(out, "css-float")) strcpy(out, "float");
    return out;
}
static JSValue style_get(JSContext *ctx, JSValueConst obj, JSAtom atom, JSValueConst receiver) {
    (void)receiver;
    struct dnode *n = JS_GetOpaque(obj, cls_style);
    const char *name = JS_AtomToCString(ctx, atom);
    if (!n || !name) { if (name) JS_FreeCString(ctx, name); return JS_UNDEFINED; }
    JSValue r;
    if (!strcmp(name, "cssText")) r = str(ctx, dom_attr(n, "style"));
    else if (!strcmp(name, "getPropertyValue") || !strcmp(name, "setProperty") || !strcmp(name, "removeProperty")) {
        JSValue proto = JS_GetClassProto(ctx, cls_style); JSValue holder = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "__styleMethods");
        r = JS_GetPropertyStr(ctx, holder, name); JS_FreeValue(ctx, holder); JS_FreeValue(ctx, proto);
    } else if (!strcmp(name, "toString") || !strcmp(name, "constructor") || name[0] == '_' || (name[0] >= '0' && name[0] <= '9')) r = JS_UNDEFINED;
    else { char *css = css_name(name); char *v = css_get_inline(dom_attr(n, "style"), css); free(css); r = str_free(ctx, v); }
    JS_FreeCString(ctx, name);
    return r;
}
static int style_set(JSContext *ctx, JSValueConst obj, JSAtom atom, JSValueConst value, JSValueConst receiver, int flags) {
    (void)receiver; (void)flags;
    struct dnode *n = JS_GetOpaque(obj, cls_style);
    const char *name = JS_AtomToCString(ctx, atom);
    if (!n || !name) { if (name) JS_FreeCString(ctx, name); return 1; }
    char *v = arg_str(ctx, value);
    if (!strcmp(name, "cssText")) dom_set_attr(n, "style", v);
    else {
        char *css = css_name(name);
        /* numbers for lengths mean pixels only in quirks; keep what the page wrote */
        char *ns = css_set_inline(dom_attr(n, "style"), css, v);
        dom_set_attr(n, "style", ns);
        free(ns); free(css);
    }
    free(v); JS_FreeCString(ctx, name);
    return 1;
}
static JSClassExoticMethods style_exotic = { .get_property = style_get, .set_property = style_set };

/* ------------------------------------------------------------------ globals implemented in C */
static JSValue n_navigate(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) {
    (void)this; struct webjs *j = J(ctx);
    char *u = arg_str(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    char *abs = rt_url_resolve(j->d->url ? j->d->url : "", u);
    webview_navigate(j->d->web, abs, 5, 1);
    free(abs); free(u);
    return JS_UNDEFINED;
}
static JSValue n_log(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) {
    (void)this;
    char *s = arg_str(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    rt_log("UIWebView console: %s", s);
    free(s);
    return JS_UNDEFINED;
}
static void fire_timer(void *p) {
    struct jtimer *t = p; struct webjs *j = live_find(t->serial);
    if (j) {
        char src[64]; snprintf(src, sizeof src, "__fireTimer(%ld)", t->id);
        char *r = webjs_eval(j, src); free(r);
    }
    free(t);
}
static JSValue n_timer(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) {
    (void)this; int32_t id = 0; double ms = 0;
    if (argc > 0) JS_ToInt32(ctx, &id, argv[0]);
    if (argc > 1) JS_ToFloat64(ctx, &ms, argv[1]);
    if (!(ms >= 0)) ms = 0;
    struct jtimer *t = calloc(1, sizeof *t); t->serial = J(ctx)->serial; t->id = id;
    rt_after(ms / 1000.0, fire_timer, t);
    return JS_UNDEFINED;
}
static JSValue n_screen(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) {
    (void)this; (void)argc; (void)argv; struct wdocument *d = J(ctx)->d;
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "sw", JS_NewFloat64(ctx, d->screen_w)); JS_SetPropertyStr(ctx, o, "sh", JS_NewFloat64(ctx, d->screen_h));
    JS_SetPropertyStr(ctx, o, "vw", JS_NewFloat64(ctx, d->vw)); JS_SetPropertyStr(ctx, o, "vh", JS_NewFloat64(ctx, d->vh));
    return o;
}

static const char PRELUDE[] =
"(function(g){\n"
"var N=g.__nodeProto, doc=g.document;\n"
"g.window=g; g.self=g; g.top=g; g.parent=g; g.frames=[];\n"
"function ls(o){ if(!o.__ls) Object.defineProperty(o,'__ls',{value:{},enumerable:false}); return o.__ls; }\n"
"var ET={addEventListener:function(t,f,c){ if(!f) return; var l=ls(this); (l[t]||(l[t]=[])).indexOf(f)<0&&l[t].push(f); },\n"
" removeEventListener:function(t,f){ var l=ls(this)[t]; if(l){ var i=l.indexOf(f); if(i>=0) l.splice(i,1);} },\n"
" dispatchEvent:function(e){ return __dispatch(this,e); }};\n"
"for(var k in ET){ N[k]=ET[k]; g[k]=ET[k]; }\n"
"function Ev(type,init){ this.type=type; this.bubbles=!!(init&&init.bubbles); this.cancelable=!!(init&&init.cancelable); this.defaultPrevented=false; this.target=null; this.currentTarget=null; this.timeStamp=Date.now(); this._stop=false; }\n"
"Ev.prototype.preventDefault=function(){ this.defaultPrevented=true; };\n"
"Ev.prototype.stopPropagation=function(){ this._stop=true; };\n"
"Ev.prototype.stopImmediatePropagation=function(){ this._stop=true; this._stopNow=true; };\n"
"Ev.prototype.initEvent=function(t,b,c){ this.type=t; this.bubbles=b; this.cancelable=c; };\n"
"g.Event=Ev; g.CustomEvent=function(t,i){ Ev.call(this,t,i); this.detail=i&&i.detail; }; g.CustomEvent.prototype=Ev.prototype;\n"
"g.MouseEvent=g.CustomEvent; g.TouchEvent=g.CustomEvent;\n"
"doc.createEvent=function(){ return new Ev(''); };\n"
"function callh(o,e){ var r; var h=o['on'+e.type]; e.currentTarget=o;\n"
"  if(typeof h=='function'){ try{ r=h.call(o,e); }catch(x){ __report(x); } if(r===false) e.preventDefault(); }\n"
"  var l=o.__ls&&o.__ls[e.type]; if(l){ l=l.slice(); for(var i=0;i<l.length&&!e._stopNow;i++){ try{ if(typeof l[i]=='function') l[i].call(o,e); else if(l[i]&&l[i].handleEvent) l[i].handleEvent(e); }catch(x){ __report(x); } } } }\n"
"function __dispatch(t,e){ if(typeof e=='string') e=new Ev(e,{bubbles:true,cancelable:true}); e.target=t; e.srcElement=t;\n"
"  for(var o=t;o;o=(o===doc?g:(o.parentNode||(o.nodeType===9?g:null)))){ callh(o,e); if(e._stop||!e.bubbles) break; if(o===g) break; }\n"
"  return !e.defaultPrevented; }\n"
"g.__dispatch=__dispatch;\n"
"g.__click=function(t){ var e=new Ev('click',{bubbles:true,cancelable:true}); e.clientX=e.pageX=g.__cx||0; e.clientY=e.pageY=g.__cy||0; e.button=0; return __dispatch(t,e)?0:1; };\n"
"g.__fire=function(o,t){ var e=new Ev(t,{bubbles:false}); e.target=o; callh(o,e); };\n"
"N.click=function(){ __dispatch(this,new Ev('click',{bubbles:true,cancelable:true})); };\n"
"N.focus=N.blur=N.scrollIntoView=function(){};\n"
"N.closest=function(s){ for(var o=this;o&&o.nodeType===1;o=o.parentNode) if(o.matches(s)) return o; return null; };\n"
"N.append=function(){ for(var i=0;i<arguments.length;i++){ var a=arguments[i]; this.appendChild(typeof a=='string'?doc.createTextNode(a):a);} };\n"
"N.prepend=function(){ for(var i=arguments.length-1;i>=0;i--){ var a=arguments[i]; this.insertBefore(typeof a=='string'?doc.createTextNode(a):a,this.firstChild);} };\n"
"N.item=function(i){ return this[i]; };\n"
"Object.defineProperty(N,'classList',{get:function(){ var el=this; function L(){ return (el.className||'').split(/\\s+/).filter(function(x){return x;}); }\n"
"  return {add:function(){ var l=L(); for(var i=0;i<arguments.length;i++) if(l.indexOf(arguments[i])<0) l.push(arguments[i]); el.className=l.join(' '); },\n"
"   remove:function(){ var a=[].slice.call(arguments); el.className=L().filter(function(x){return a.indexOf(x)<0;}).join(' '); },\n"
"   contains:function(c){ return L().indexOf(c)>=0; },\n"
"   toggle:function(c,f){ var h=L().indexOf(c)>=0; if(f===undefined) f=!h; if(f) this.add(c); else this.remove(c); return f; },\n"
"   item:function(i){ return L()[i]||null; }, get length(){ return L().length; }, toString:function(){ return el.className; } }; }});\n"
"Object.defineProperty(N,'dataset',{get:function(){ var el=this; return new Proxy({},{get:function(t,k){ if(typeof k!='string') return undefined; return el.getAttribute('data-'+k.replace(/[A-Z]/g,function(c){return '-'+c.toLowerCase();})); },\n"
"  set:function(t,k,v){ el.setAttribute('data-'+k.replace(/[A-Z]/g,function(c){return '-'+c.toLowerCase();}),v); return true; }}); }});\n"
"g.__styleMethods={getPropertyValue:function(p){ return this[p.replace(/-([a-z])/g,function(m,c){return c.toUpperCase();})]||''; },\n"
" setProperty:function(p,v){ this[p.replace(/-([a-z])/g,function(m,c){return c.toUpperCase();})]=v; }, removeProperty:function(p){ this[p.replace(/-([a-z])/g,function(m,c){return c.toUpperCase();})]=''; }};\n"
"var T={}, tid=0;\n"
"g.setTimeout=function(f,ms){ var a=[].slice.call(arguments,2); var id=++tid; T[id]={f:f,a:a}; __n.timer(id,+ms||0); return id; };\n"
"g.setInterval=function(f,ms){ var a=[].slice.call(arguments,2); var id=++tid; T[id]={f:f,a:a,ms:Math.max(+ms||0,4)}; __n.timer(id,T[id].ms); return id; };\n"
"g.clearTimeout=g.clearInterval=function(id){ delete T[id]; };\n"
"g.requestAnimationFrame=function(f){ return g.setTimeout(function(){ f(Date.now()); },16); }; g.cancelAnimationFrame=g.clearTimeout;\n"
"g.__fireTimer=function(id){ var t=T[id]; if(!t) return; if(t.ms===undefined) delete T[id]; else __n.timer(id,t.ms);\n"
"  try{ if(typeof t.f=='function') t.f.apply(g,t.a); else (0,eval)(String(t.f)); }catch(x){ __report(x); } };\n"
"g.__report=function(x){ __n.log('error: '+(x&&x.stack?x+'\\n'+x.stack:x)); };\n"
"function out(){ return [].map.call(arguments,function(a){ try{ return typeof a=='object'?JSON.stringify(a):String(a);}catch(e){return String(a);} }).join(' '); }\n"
"g.console={log:function(){__n.log(out.apply(null,arguments));},info:function(){__n.log(out.apply(null,arguments));},warn:function(){__n.log(out.apply(null,arguments));},\n"
" error:function(){__n.log(out.apply(null,arguments));},debug:function(){},trace:function(){},dir:function(){},time:function(){},timeEnd:function(){}};\n"
"g.alert=function(m){ __n.log('alert: '+m); }; g.confirm=function(m){ __n.log('confirm: '+m); return true; }; g.prompt=function(){ return null; };\n"
"g.print=g.focus=g.blur=g.scrollTo=g.scrollBy=g.scroll=function(){}; g.open=function(u){ if(u) __n.navigate(String(u)); return null; }; g.close=function(){};\n"
"function parse(u){ var m=/^([a-zA-Z][a-zA-Z0-9+.-]*:)(\\/\\/([^\\/?#]*))?([^?#]*)(\\?[^#]*)?(#.*)?$/.exec(u)||[]; return {protocol:m[1]||'',host:m[3]||'',hostname:(m[3]||'').replace(/:.*/,''),port:'',pathname:m[4]||'',search:m[5]||'',hash:m[6]||''}; }\n"
"var loc={ get href(){ return doc.URL; }, set href(v){ __n.navigate(String(v)); }, assign:function(v){ __n.navigate(String(v)); }, replace:function(v){ __n.navigate(String(v)); },\n"
" reload:function(){ __n.navigate(doc.URL); }, toString:function(){ return doc.URL; } };\n"
"['protocol','host','hostname','port','pathname','search'].forEach(function(k){ Object.defineProperty(loc,k,{get:function(){ return parse(doc.URL)[k]; }}); });\n"
"Object.defineProperty(loc,'hash',{get:function(){ return parse(doc.URL).hash; }, set:function(v){}});\n"
"Object.defineProperty(g,'location',{get:function(){ return loc; }, set:function(v){ __n.navigate(String(v)); }});\n"
"Object.defineProperty(doc,'location',{get:function(){ return loc; }, set:function(v){ __n.navigate(String(v)); }});\n"
"var ck=''; Object.defineProperty(doc,'cookie',{get:function(){ return ck; }, set:function(v){ var kv=String(v).split(';')[0]; ck=ck?ck+'; '+kv:kv; }});\n"
"doc.defaultView=g; doc.write=doc.writeln=function(){ var b=doc.body; if(b) b.insertAdjacentHTML('beforeend',[].join.call(arguments,'')); }; doc.open=doc.close=function(){};\n"
"doc.hasFocus=function(){ return true; }; doc.activeElement=null; doc.compatMode='CSS1Compat'; doc.characterSet='UTF-8';\n"
"var s=__n.screen(); g.screen={width:s.sw,height:s.sh,availWidth:s.sw,availHeight:s.sh,colorDepth:24};\n"
"Object.defineProperty(g,'innerWidth',{get:function(){ return __n.screen().vw; }}); Object.defineProperty(g,'innerHeight',{get:function(){ return __n.screen().vh; }});\n"
"g.outerWidth=s.sw; g.outerHeight=s.sh; g.devicePixelRatio=2; g.pageXOffset=g.pageYOffset=g.scrollX=g.scrollY=0; g.orientation=0;\n"
"g.navigator={userAgent:'Mozilla/5.0 (iPhone; CPU iPhone OS 9_3 like Mac OS X) AppleWebKit/601.1.46 (KHTML, like Gecko) Mobile/13E188',\n"
" platform:'iPhone',language:'en-US',languages:['en-US'],onLine:false,cookieEnabled:true,vendor:'Apple Computer, Inc.',appName:'Netscape',appVersion:'5.0',standalone:false};\n"
"g.getComputedStyle=function(el){ return doc.__computed(el); };\n"
"var st={}; g.localStorage=g.sessionStorage={getItem:function(k){ return k in st?st[k]:null; },setItem:function(k,v){ st[k]=String(v); },removeItem:function(k){ delete st[k]; },clear:function(){ st={}; },key:function(i){ return Object.keys(st)[i]||null; },get length(){ return Object.keys(st).length; }};\n"
"g.Image=function(w,h){ var i=doc.createElement('img'); if(w) i.setAttribute('width',w); if(h) i.setAttribute('height',h); return i; };\n"
"g.XMLHttpRequest=function(){ this.readyState=0; this.status=0; this.responseText=''; };\n"
"g.XMLHttpRequest.prototype={open:function(){ this.readyState=1; },setRequestHeader:function(){},abort:function(){},getAllResponseHeaders:function(){ return ''; },getResponseHeader:function(){ return null; },\n"
" send:function(){ var x=this; g.setTimeout(function(){ x.readyState=4; x.status=0; if(x.onreadystatechange) x.onreadystatechange(); if(x.onerror) x.onerror(); },0); }};\n"
"g.matchMedia=function(q){ return {matches:false,media:q,addListener:function(){},removeListener:function(){}}; };\n"
"g.HTMLElement=g.Element=g.Node=function(){}; g.Node.prototype=N; g.Node.ELEMENT_NODE=1; g.Node.TEXT_NODE=3;\n"
"g.performance={now:function(){ return Date.now(); }};\n"
"})(this);\n";

/* ------------------------------------------------------------------ lifecycle */
static void report_exception(JSContext *ctx, const char *where) {
    JSValue e = JS_GetException(ctx);
    const char *m = JS_ToCString(ctx, e);
    JSValue stack = JS_IsObject(e) ? JS_GetPropertyStr(ctx, e, "stack") : JS_UNDEFINED;
    const char *st = JS_IsUndefined(stack) ? NULL : JS_ToCString(ctx, stack);
    rt_log("UIWebView script error in %s: %s%s%s", where, m ? m : "?", st ? "\n" : "", st ? st : "");
    if (st) JS_FreeCString(ctx, st);
    if (m) JS_FreeCString(ctx, m);
    JS_FreeValue(ctx, stack); JS_FreeValue(ctx, e);
}
static void run_jobs(struct webjs *j) {
    JSContext *c; for (int i = 0; i < 1000 && JS_ExecutePendingJob(j->rt, &c) > 0; i++) {}
}
static void after_js(struct webjs *j) {
    run_jobs(j);
    if (j->d->dirty) webview_doc_changed(j->d->web);
}

struct webjs *webjs_new(struct wdocument *d) {
    struct webjs *j = calloc(1, sizeof *j);
    j->d = d; j->serial = ++g_serial;
    for (int i = 0; i < 64; i++) if (!g_live[i]) { g_live[i] = j; break; }
    j->rt = JS_NewRuntime();
    JS_SetMemoryLimit(j->rt, 128u << 20);
    JS_SetMaxStackSize(j->rt, 512u << 10);
    if (!cls_node) { JS_NewClassID(&cls_node); JS_NewClassID(&cls_style); }
    JSClassDef nd = { "Node", .finalizer = NULL }, sd = { "CSSStyleDeclaration", .exotic = &style_exotic };
    JS_NewClass(j->rt, cls_node, &nd); JS_NewClass(j->rt, cls_style, &sd);
    j->ctx = JS_NewContext(j->rt);
    JS_SetContextOpaque(j->ctx, j);
    JSContext *ctx = j->ctx;
    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, node_proto, sizeof node_proto / sizeof node_proto[0]);
    JS_SetClassProto(ctx, cls_node, JS_DupValue(ctx, proto));
    JS_SetClassProto(ctx, cls_style, JS_NewObject(ctx));
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "__nodeProto", proto);
    JS_SetPropertyStr(ctx, global, "document", wrap(ctx, d->root));
    JSValue nat = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, nat, "navigate", JS_NewCFunction(ctx, n_navigate, "navigate", 1));
    JS_SetPropertyStr(ctx, nat, "log", JS_NewCFunction(ctx, n_log, "log", 1));
    JS_SetPropertyStr(ctx, nat, "timer", JS_NewCFunction(ctx, n_timer, "timer", 2));
    JS_SetPropertyStr(ctx, nat, "screen", JS_NewCFunction(ctx, n_screen, "screen", 0));
    JS_SetPropertyStr(ctx, global, "__n", nat);
    JSValue docobj = wrap(ctx, d->root);
    static const JSCFunctionListEntry doc_list[] = { JS_CGETSET_MAGIC_DEF("title", node_get, node_set, G_DOCTITLE) };
    JS_SetPropertyFunctionList(ctx, docobj, doc_list, 1);
    JS_FreeValue(ctx, docobj);
    JS_FreeValue(ctx, global);
    JS_UpdateStackTop(j->rt);
    JSValue r = JS_Eval(ctx, PRELUDE, sizeof PRELUDE - 1, "<prelude>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) report_exception(ctx, "prelude");
    JS_FreeValue(ctx, r);
    return j;
}
void webjs_free(struct webjs *j) {
    if (!j) return;
    for (int i = 0; i < 64; i++) if (g_live[i] == j) g_live[i] = NULL;
    for (size_t i = 0; i < j->d->nall; i++) webjs_node_freed(j, j->d->all[i]);
    JS_FreeContext(j->ctx);
    JS_FreeRuntime(j->rt);
    free(j);
}
static char *eval_src(struct webjs *j, const char *src, size_t n, const char *file) {
    JSContext *ctx = j->ctx;
    JS_UpdateStackTop(j->rt);
    JSValue r = JS_Eval(ctx, src, n, file, JS_EVAL_TYPE_GLOBAL);
    char *out = NULL;
    if (JS_IsException(r)) report_exception(ctx, file);
    else if (!JS_IsUndefined(r) && !JS_IsNull(r)) out = arg_str(ctx, r);
    JS_FreeValue(ctx, r);
    after_js(j);
    return out ? out : strdup("");
}
char *webjs_eval(struct webjs *j, const char *src) {
    if (!j) return strdup("");
    return eval_src(j, src, strlen(src), "<eval>");
}
void webjs_run_scripts(struct webjs *j) {
    struct wdocument *d = j->d;
    nvec scripts = { 0 };
    for (struct dnode *n = d->root; n; n = dom_next_pre(n, d->root)) {
        if (n->type != N_ELEMENT || strcmp(n->tag, "script")) continue;
        const char *type = dom_attr(n, "type");
        if (type && *type && !strstr(type, "javascript") && !strstr(type, "ecmascript") && strcmp(type, "module")) continue;
        nv_push(&scripts, n);
    }
    for (size_t i = 0; i < scripts.n; i++) {
        struct dnode *n = scripts.v[i];
        const char *src = dom_attr(n, "src");
        if (src) {
            char *url = rt_url_resolve(d->url ? d->url : "", src), *path = web_file_path_of(url);
            size_t len = 0; char *text = path ? web_read_file(path, &len) : NULL;
            if (text) { char *r = eval_src(j, text, len, src); free(r); }
            else rt_log("UIWebView: script %s not loaded (only bundled files)", url);
            free(text); free(path); free(url);
        } else {
            char *text = dom_text_content(n);
            char *r = eval_src(j, text, strlen(text), "<inline script>"); free(r); free(text);
        }
    }
    free(scripts.v);
    char *r = webjs_eval(j, "__fire(document,'DOMContentLoaded');__dispatch(document,new Event('DOMContentLoaded'));"
                            "if(typeof window.onload=='function'){try{window.onload(new Event('load'));}catch(x){__report(x);}}"
                            "var __l=window.__ls&&window.__ls.load;if(__l)__l.slice().forEach(function(f){try{f(new Event('load'));}catch(x){__report(x);}});"
                            "if(document.body&&typeof document.body.onload=='function')document.body.onload();");
    free(r);
    /* <body onload="..."> */
    struct dnode *body = dom_body(d);
    const char *onload = body ? dom_attr(body, "onload") : NULL;
    if (onload) { r = eval_src(j, onload, strlen(onload), "<body onload>"); free(r); }
}
int webjs_click(struct webjs *j, struct dnode *target) {
    if (!j || !target) return 0;
    JSContext *ctx = j->ctx;
    JS_UpdateStackTop(j->rt);
    int prevented = 0;
    /* inline onclick="..." attributes on the target's ancestors become handlers first */
    for (struct dnode *n = target; n && n->type == N_ELEMENT; n = n->parent) {
        const char *code = dom_attr(n, "onclick");
        if (!code) continue;
        JSValue o = wrap(ctx, n);
        JSValue h = JS_GetPropertyStr(ctx, o, "onclick");
        if (!JS_IsFunction(ctx, h)) {
            size_t len = strlen(code) + 64; char *src = malloc(len);
            snprintf(src, len, "(function(event){%s\n})", code);
            JSValue f = JS_Eval(ctx, src, strlen(src), "<onclick>", JS_EVAL_TYPE_GLOBAL);
            free(src);
            if (JS_IsException(f)) report_exception(ctx, "onclick"); else JS_SetPropertyStr(ctx, o, "onclick", JS_DupValue(ctx, f));
            JS_FreeValue(ctx, f);
        }
        JS_FreeValue(ctx, h); JS_FreeValue(ctx, o);
    }
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, global, "__click");
    JSValue arg = wrap(ctx, target);
    JSValue r = JS_Call(ctx, fn, global, 1, &arg);
    if (JS_IsException(r)) report_exception(ctx, "click");
    else { int32_t v = 0; JS_ToInt32(ctx, &v, r); prevented = v != 0; }
    JS_FreeValue(ctx, r); JS_FreeValue(ctx, arg); JS_FreeValue(ctx, fn); JS_FreeValue(ctx, global);
    after_js(j);
    return prevented;
}
