/* The small web engine behind UIWebView: a DOM tree (webdom.c), CSS cascade
 * (webcss.c), box layout producing a display list plus hit testing
 * (weblayout.c) and JavaScript through QuickJS (webjs.c). webview.c wraps a
 * document in the UIKit views. */
#pragma once
#include "uikit_internal.h"
#include <stddef.h>

struct wdocument;
struct webjs;

/* ------------------------------------------------------------------ CSS values */
struct wlen { double v; char u; };   /* u: 0 auto/unset, 'x' px, '%' percent, 'e' em (cascade only), 'n' number */
enum { D_NONE, D_BLOCK, D_INLINE, D_INLINE_BLOCK, D_LIST_ITEM };
enum { P_STATIC, P_RELATIVE, P_ABSOLUTE, P_FIXED };

struct cstyle {
    /* inherited */
    double size; struct wlen lh;              /* line-height: u 0 normal, 'x' px, 'n' factor */
    float color[4];
    int bold, italic, align, upper, pre, nowrap, nobullet, vis_hidden, no_pointer, list_decimal;
    /* not inherited */
    int display, position, floatv;            /* floatv: 0 none, 1 left, 2 right */
    struct wlen left, top, right, bottom, width, height, minh, margin[4], pad[4];   /* t r b l */
    double border[4]; float bcol[4][4]; double radius;
    int bcol_cur[4];                          /* border color is currentColor */
    float bg[4]; char *bgimg; int bgsize_kind; struct wlen bgsize[2];   /* kind: 0 auto, 1 lengths, 2 cover, 3 contain */
    float grad[2][4]; int has_grad; int bg_norepeat; float bgpos[2];   /* bgpos: 0..1 of the free space */
    double opacity; int z, zauto, overflow_hidden;
};

/* ------------------------------------------------------------------ DOM */
enum { N_ELEMENT = 1, N_TEXT = 3, N_COMMENT = 8, N_DOCUMENT = 9, N_FRAGMENT = 11 };
struct dnode {
    int type;
    char *tag;                     /* elements: lowercase name */
    char **an, **av; int na, acap; /* attributes */
    char *text;                    /* text and comments (entities decoded) */
    struct dnode *parent, *first, *last, *next, *prev;
    struct wdocument *doc;
    struct cstyle cs;              /* computed style of the last cascade */
    CGRect box; int laid;          /* border box in document coordinates from the last layout */
    CGRect cbox;                   /* content box */
    long seq;                      /* document order of the last cascade */
    int zk, posk;                  /* paint key: stacking z, positioned */
    void *jsptr;                   /* the element's JS object (owned reference), or NULL */
};

/* paint items */
enum { IT_BOX, IT_TEXT, IT_IMAGE, IT_RULE };
struct witem {
    int kind; struct dnode *el; CGRect r;
    char *text; double size; int bold; float col[4]; id image;
    float alpha; int zk, posk; long seq, sub;
};

struct css;   /* webcss.c */
struct wdocument {
    struct dnode *root;            /* N_DOCUMENT */
    struct dnode **all; size_t nall, acap;   /* every node ever made: freed with the document */
    char *url, *title;
    struct css *sheet;
    id web;                        /* the UIWebView (not retained) */
    struct webjs *js;
    int dirty;                     /* DOM or styles changed since the last layout */
    double vw, vh;                 /* viewport */
    double screen_w, screen_h; int orientation_landscape;
    struct witem *items; size_t nitems, icap; double height;
    long seqctr;
};

/* webdom.c */
struct wdocument *wdoc_new(void);
void wdoc_free(struct wdocument *d);
struct dnode *dom_new(struct wdocument *d, int type, const char *tag_or_text);
void dom_parse_into(struct wdocument *d, struct dnode *parent, const char *html, int fragment);
void dom_ensure_structure(struct wdocument *d);
struct dnode *dom_html(struct wdocument *d), *dom_head(struct wdocument *d), *dom_body(struct wdocument *d);
const char *dom_attr(const struct dnode *n, const char *name);
void dom_set_attr(struct dnode *n, const char *name, const char *val);
void dom_remove_attr(struct dnode *n, const char *name);
void dom_append(struct dnode *parent, struct dnode *child);
void dom_insert_before(struct dnode *parent, struct dnode *child, struct dnode *ref);
void dom_remove(struct dnode *child);
int dom_contains(const struct dnode *a, const struct dnode *b);
int dom_connected(const struct dnode *n);
struct dnode *dom_clone(struct dnode *n, int deep);
char *dom_serialize_children(const struct dnode *n);     /* innerHTML */
char *dom_serialize(const struct dnode *n);              /* outerHTML */
char *dom_text_content(const struct dnode *n);
void dom_set_text_content(struct dnode *n, const char *t);
void dom_set_inner_html(struct dnode *n, const char *html);
struct dnode *dom_by_id(struct dnode *root, const char *id);
struct dnode *dom_next_pre(struct dnode *n, struct dnode *stop);   /* pre-order walk inside stop */
int dom_has_class(const struct dnode *n, const char *cls);
char *web_decode_entities(const char *s, size_t n);
char *web_read_file(const char *path, size_t *len);
char *web_file_path_of(const char *url);
int web_ieq(const char *a, const char *b);

/* selectors (webdom.c) */
struct wsel;
struct wsel *sel_parse(const char *text, int *ok);   /* a selector list */
void sel_free(struct wsel *s);
int sel_matches(const struct wsel *s, const struct dnode *el, int *specificity);
size_t sel_count(const struct wsel *s);
int sel_matches_one(const struct wsel *s, size_t i, const struct dnode *el);
int sel_specificity(const struct wsel *s, size_t i);

/* webcss.c */
struct css *css_new(void);
void css_free(struct css *c);
void css_add_sheet(struct css *c, const char *text, const char *base_url);   /* url() resolved against base */
void css_cascade(struct wdocument *d);
void css_apply_inline(struct cstyle *st, const char *decl, const struct cstyle *parent);
int css_parse_color(const char *v, float out[4]);
char *css_get_inline(const char *style, const char *prop);           /* value of one property in a style attribute */
char *css_set_inline(const char *style, const char *prop, const char *val);   /* new style attribute text */

/* weblayout.c */
void web_layout(struct wdocument *d);
void web_paint(struct wdocument *d, struct cgctx *c, CGRect visible);
struct dnode *web_hit(struct wdocument *d, double x, double y);      /* document coordinates; NULL = nothing */
void web_ensure_layout(struct wdocument *d);

/* webjs.c */
struct webjs *webjs_new(struct wdocument *d);
void webjs_free(struct webjs *j);
void webjs_run_scripts(struct webjs *j);                 /* the document's <script> elements, then onload */
char *webjs_eval(struct webjs *j, const char *src);      /* result as a string (malloc) */
int webjs_click(struct webjs *j, struct dnode *target);  /* dispatches a click; 1 = default prevented */
void webjs_node_freed(struct webjs *j, struct dnode *n);

/* webview.c: called by the engine */
void webview_navigate(id web, const char *url, long nav_type, int main_frame);
void webview_doc_changed(id web);
