/* UIWebView over the built-in web engine (webengine.h).
 *
 * There is no browser engine on this side, but many apps only use a web view
 * for bundled pages: help screens, tutorials, about boxes, licenses. Those
 * load from the app bundle and render natively here:
 *   - HTML into a DOM tree, CSS cascade (style/link sheets, @media, inline
 *     styles), block/inline/float/absolute layout, paint order by z-index;
 *   - JavaScript through QuickJS with a DOM binding: scripts run after the
 *     parse, then load handlers; stringByEvaluatingJavaScriptFromString:
 *     evaluates in the page; taps dispatch click events to elements;
 *   - links, location changes and iframe loads go through
 *     webView:shouldStartLoadWithRequest:navigationType: (apps route custom
 *     schemes there); loads are asynchronous, like UIKit: didStart, then
 *     didFinish/didFail.
 * Remote pages fail with NSURLErrorNotConnectedToInternet.
 *
 * View structure follows UIKit: the web view holds a UIScrollView
 * (subviews[0], also `scrollView`) which holds the document view. */
#include "webengine.h"
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

struct uiweb {
    struct uiview v; id delegate, scroll, content, request; long loading, scales;
    char *base;                 /* URL of the shown document */
    char **history; size_t nhist; double laidW, laidH;
    struct wdocument *doc;
};
struct uibrowser { struct uiview v; id web; };
#define WEB(x) ((struct uiweb *)(x))
char *rt_url_resolve(const char *base, const char *ref);

static void doc_free(struct uiweb *w) {
    if (!w->doc) return;
    webjs_free(w->doc->js); w->doc->js = NULL;
    wdoc_free(w->doc); w->doc = NULL;
}
static void screen_size(double *w, double *h) {
    id scr = RT_SEND0(id, (id)rt_class_named("UIScreen"), "mainScreen");
    CGRect b = scr ? RT_SEND0(CGRect, scr, "bounds") : (CGRect){ { 0, 0 }, { 320, 480 } };
    *w = b.size.width; *h = b.size.height;
}
/* stylesheets in document order: <style> text and <link rel=stylesheet> files */
static void collect_sheets(struct wdocument *d) {
    for (struct dnode *n = d->root; n; n = dom_next_pre(n, d->root)) {
        if (n->type != N_ELEMENT) continue;
        if (!strcmp(n->tag, "style")) { char *t = dom_text_content(n); css_add_sheet(d->sheet, t, d->url); free(t); }
        else if (!strcmp(n->tag, "link")) {
            const char *rel = dom_attr(n, "rel"), *href = dom_attr(n, "href");
            if (!rel || !strstr(rel, "stylesheet") || !href) continue;
            char *url = rt_url_resolve(d->url ? d->url : "", href), *path = web_file_path_of(url);
            char *text = path ? web_read_file(path, NULL) : NULL;
            if (text) css_add_sheet(d->sheet, text, url);
            free(text); free(path); free(url);
        }
    }
}
/* ------------------------------------------------------------------ the views */
static void web_relayout(id self) {
    struct uiweb *w = WEB(self);
    CGRect b = rt_view_bounds(self);
    rt_view_set_frame(w->scroll, (CGRect){ { 0, 0 }, b.size });
    struct wdocument *d = w->doc;
    if (d && (fabs(w->laidW - b.size.width) > 0.5 || fabs(w->laidH - b.size.height) > 0.5 || d->dirty)) {
        d->vw = b.size.width; d->vh = b.size.height;
        screen_size(&d->screen_w, &d->screen_h);
        d->orientation_landscape = d->screen_w > d->screen_h;
        web_layout(d);
        w->laidW = b.size.width; w->laidH = b.size.height;
    }
    double dh = d ? d->height : 0;
    double h = dh > b.size.height ? dh : b.size.height;
    rt_view_set_frame(w->content, (CGRect){ { 0, 0 }, { b.size.width, h } });
    ((struct uiscroll *)w->scroll)->contentSize = (CGSize){ b.size.width, d ? dh : 0 };
    rt_ui_mark_dirty();
}
void webview_doc_changed(id web) { if (web && WEB(web)->doc) web_relayout(web); }
static void web_show(id self, char *html, const char *base) {
    struct uiweb *w = WEB(self);
    doc_free(w);
    free(w->base); w->base = strdup(base ? base : "");
    struct wdocument *d = wdoc_new();
    d->web = self; d->url = strdup(w->base); d->sheet = css_new();
    dom_parse_into(d, d->root, html, 0);
    free(html);
    collect_sheets(d);
    for (struct dnode *n = d->root; n; n = dom_next_pre(n, d->root)) if (n->type == N_ELEMENT && !strcmp(n->tag, "title")) { d->title = dom_text_content(n); break; }
    CGRect b = rt_view_bounds(self);
    d->vw = b.size.width; d->vh = b.size.height; screen_size(&d->screen_w, &d->screen_h); d->orientation_landscape = d->screen_w > d->screen_h;
    d->dirty = 1;
    w->doc = d;
    d->js = webjs_new(d);
    webjs_run_scripts(d->js);
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
        char *path = web_file_path_of(u);
        html = path ? web_read_file(path, NULL) : NULL;
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
/* links, location and iframes from the page: asynchronous, like WebKit's navigation scheduling */
struct navreq { id web; char *url; long nav; int main; };
static void nav_run(void *p) {
    struct navreq *n = p; id self = n->web;
    id url = RT_SEND1(id, (id)rt_class_named("NSURL"), "URLWithString:", id, rt_str(n->url));
    if (url) {
        id req = RT_SEND1(id, (id)rt_class_named("NSURLRequest"), "requestWithURL:", id, url);
        if (n->main) web_start(self, req, NULL, NULL, n->nav);
        else {
            id d = WEB(self)->delegate;
            if (d && rt_responds(d, rt_sel("webView:shouldStartLoadWithRequest:navigationType:")))
                RT_SEND3(long, d, "webView:shouldStartLoadWithRequest:navigationType:", id, self, id, req, long, n->nav);
        }
    }
    free(n->url); objc_release(self); free(n);
}
void webview_navigate(id web, const char *url, long nav, int main_frame) {
    if (!web || !url) return;
    struct navreq *n = calloc(1, sizeof *n);
    n->web = objc_retain(web); n->url = strdup(url); n->nav = nav; n->main = main_frame;
    rt_after(0, nav_run, n);
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
    doc_free(w); free(w->base);
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
    (void)s; struct wdocument *d = WEB(self)->doc;
    if (!d || !d->js || !js) return rt_str("");
    if (d->dirty) web_relayout(self);
    char *r = webjs_eval(d->js, rt_str_utf8(js));
    id out = rt_str(r); free(r);
    return out;
}
static rt_bool web_scales(id self, SEL s) { (void)s; return WEB(self)->scales != 0; }
static void web_setScales(id self, SEL s, long v) { (void)s; WEB(self)->scales = v & 0xff; }
static void web_nop(id self, SEL s) { (void)self; (void)s; }
static rt_bool web_no(id self, SEL s) { (void)self; (void)s; return 0; }
static void web_nopL(id self, SEL s, long v) { (void)self; (void)s; (void)v; }

/* the document view: drawing and taps */
void rt_webview_draw(id view, struct cgctx *c, CGRect b) {
    (void)b;
    if (!rt_is_kind_of(view, RT_CLASS(UIWebBrowserView))) return;
    id web = ((struct uibrowser *)view)->web; if (!web || !WEB(web)->doc) return;
    struct wdocument *d = WEB(web)->doc;
    if (d->dirty) web_relayout(web);
    web_paint(d, c, rt_view_bounds(WEB(web)->scroll));   /* the scrolled-to part, in document coordinates */
}
static struct dnode *link_of(struct dnode *n) { for (; n && n->type == N_ELEMENT; n = n->parent) if (!strcmp(n->tag, "a") && dom_attr(n, "href")) return n; return NULL; }
static void br_touchesEnded(id self, SEL s, id touches, id ev) {
    (void)s; (void)ev;
    id web = ((struct uibrowser *)self)->web; if (!web) return;
    struct uiweb *w = WEB(web); struct wdocument *d = w->doc;
    if (!d || ((struct uiscroll *)w->scroll)->dragging) return;
    CGPoint p = RT_SEND1(CGPoint, RT_SEND0(id, touches, "anyObject"), "locationInView:", id, self);
    if (d->dirty) web_relayout(web);
    struct dnode *t = web_hit(d, p.x, p.y);
    struct dnode *a = link_of(t);
    if (!a) {   /* fingers are wide: a link run within a few points counts */
        for (size_t i = d->nitems; i-- > 0;) {
            struct witem *it = &d->items[i]; if (it->kind != IT_TEXT && it->kind != IT_IMAGE) continue;
            struct dnode *l = link_of(it->el); if (!l) continue;
            CGRect r = it->r;
            if (p.x >= r.origin.x - 4 && p.y >= r.origin.y - 4 && p.x <= r.origin.x + r.size.width + 4 && p.y <= r.origin.y + r.size.height + 4) { a = l; if (!t || !dom_contains(l, t)) t = it->el; break; }
        }
    }
    if (!t) return;
    if (d->js && webjs_click(d->js, t)) return;   /* a handler called preventDefault / returned false */
    if (!w->doc || w->doc != d) return;          /* the handler navigated away */
    if (!a) return;
    const char *href = dom_attr(a, "href");
    if (href[0] == '#') { RT_SEND1(void, w->scroll, "setContentOffset:", CGPoint, ((CGPoint){ 0, 0 })); return; }
    if (!strncmp(href, "javascript:", 11)) { char *r = webjs_eval(d->js, href + 11); free(r); return; }
    char *abs = rt_url_resolve(w->base ? w->base : "", href);
    id url = RT_SEND1(id, (id)rt_class_named("NSURL"), "URLWithString:", id, rt_str(abs));
    free(abs);
    if (url) web_start(web, RT_SEND1(id, (id)rt_class_named("NSURLRequest"), "requestWithURL:", id, url), NULL, NULL, 0);
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
