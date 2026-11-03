/* Layout for the UIWebView engine. Walks the styled DOM and produces the
 * display list (backgrounds/borders, text runs, images, rules) and a border
 * box per element:
 *   - block flow with sibling margin collapsing, explicit/auto widths,
 *     auto-margin centering, heights and min-height, padding and borders;
 *   - inline flow: word wrapping, text-align, line-height, white-space,
 *     inline images and inline-blocks, list bullets, <br>;
 *   - floats (left/right, shrink-to-fit, shorten the lines beside them);
 *   - relative offsets; absolute and fixed boxes laid out against their
 *     containing block once it is final;
 *   - paint order by stacking (z-index, positioned, document order), opacity.
 * Hit testing picks the topmost element box, the way elementFromPoint does. */
#include "webengine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

char *rt_url_resolve(const char *base, const char *ref);
void iosrt_CGContextAddArcToPoint(struct cgctx *c, double x1, double y1, double x2, double y2, double r);
void iosrt_CGContextClosePath(struct cgctx *c);

struct flt { double x0, x1, bottom; int right; };
struct absent { struct dnode *el; double sx, sy; int zk; float alpha; };
struct cbframe { struct dnode *el; struct absent *a; size_t n, cap; };
struct lctx {
    struct wdocument *d;
    struct cbframe cbs[64]; int ncb;
    struct { char *url; id img; } imgs[64]; int nimg;
    long sub;
    int measuring;
    struct flt *pfl; int npfl;   /* floats of the parent flow, handed to the next block's flow */
    struct dnode *nomt[32]; int nnomt;   /* first children whose top margin collapsed into an ancestor's */
};

struct pitem {
    int kind;            /* 0 text/image item, 1 inline-block (items [rs,re) of el) */
    struct witem it;     /* kind 0: r.size is the item size, r.origin.x the pen x */
    double w, h, contrib;   /* contrib: the item's share of the line height */
    size_t rs, re; struct dnode *ref;
    int lh_set;
};
struct bctx {
    struct lctx *L; struct dnode *el; const struct cstyle *bs;
    double cl, cr, y, pend;
    struct pitem *line; size_t nl, lcap;
    double x, ll, lr; int line_open, space;
    struct flt fl[16]; int nfl;
    double maxright;
    char bullet[16]; int bullet_pending; const struct cstyle *bullet_st;
    int zk, posk; float alpha;
};

static void flow_children(struct bctx *bc, struct dnode *el, int zk, int posk, float alpha);
static void layout_block(struct bctx *parent, struct dnode *el, int zk, int posk, float alpha);
static double layout_contents(struct lctx *L, struct dnode *el, double cl, double cr, double top, double *maxright, int zk, int posk, float alpha, double *trail);

/* ------------------------------------------------------------------ helpers */
static double len_px(struct wlen l, double ref, double dflt) {
    switch (l.u) { case 'x': return l.v; case '%': return l.v * ref / 100; case 'n': return l.v; default: return dflt; }
}
static int is_auto(struct wlen l) { return l.u == 0; }
static struct witem *push_item(struct lctx *L, const struct witem *it) {
    struct wdocument *d = L->d;
    if (d->nitems == d->icap) { d->icap = d->icap ? d->icap * 2 : 256; d->items = realloc(d->items, d->icap * sizeof *d->items); }
    struct witem *r = &d->items[d->nitems++]; *r = *it; r->sub = L->sub++;
    return r;
}
static void truncate_items(struct lctx *L, size_t n) {
    struct wdocument *d = L->d;
    for (size_t i = n; i < d->nitems; i++) { free(d->items[i].text); objc_release(d->items[i].image); }
    if (d->nitems > n) d->nitems = n;
}
static void union_box(struct dnode *e, CGRect r) {
    if (!e->laid) { e->box = r; e->cbox = r; e->laid = 1; return; }
    double x0 = fmin(e->box.origin.x, r.origin.x), y0 = fmin(e->box.origin.y, r.origin.y);
    double x1 = fmax(e->box.origin.x + e->box.size.width, r.origin.x + r.size.width), y1 = fmax(e->box.origin.y + e->box.size.height, r.origin.y + r.size.height);
    e->box = (CGRect){ { x0, y0 }, { x1 - x0, y1 - y0 } }; e->cbox = e->box;
}
static void shift_subtree(struct lctx *L, struct dnode *el, size_t rs, size_t re, double dx, double dy) {
    if (fabs(dx) < 1e-9 && fabs(dy) < 1e-9) return;
    for (size_t i = rs; i < re && i < L->d->nitems; i++) { L->d->items[i].r.origin.x += dx; L->d->items[i].r.origin.y += dy; }
    for (struct dnode *n = el; n; n = dom_next_pre(n, el)) if (n->laid) { n->box.origin.x += dx; n->box.origin.y += dy; n->cbox.origin.x += dx; n->cbox.origin.y += dy; }
}
static id load_image(struct lctx *L, const char *src) {
    if (!src || !*src) return NULL;
    char *url = rt_url_resolve(L->d->url ? L->d->url : "", src);
    for (int i = 0; i < L->nimg; i++) if (!strcmp(L->imgs[i].url, url)) { free(url); return L->imgs[i].img; }
    char *path = web_file_path_of(url);
    id img = path ? RT_SEND1(id, (id)rt_class_named("UIImage"), "imageWithContentsOfFile:", id, rt_str(path)) : NULL;
    free(path);
    if (L->nimg < 64) { L->imgs[L->nimg].url = url; L->imgs[L->nimg].img = objc_retain(img); L->nimg++; } else free(url);
    return img;
}
static double space_w(double size, int bold) { return ceil(rt_text_measure(" ", rt_font_make(size, bold, NULL), 1e9, 1).width); }
static double glyph_h(const struct cstyle *st) { return ceil(st->size * 1.25); }
static double line_h(const struct cstyle *st) {
    if (st->lh.u == 'x') return st->lh.v;
    if (st->lh.u == 'n') return st->lh.v * st->size;
    return glyph_h(st);
}
static int elem_paint_key(const struct cstyle *cs, int *zk, int *posk) {
    if (cs->position != P_STATIC) { *posk = 1; if (!cs->zauto) *zk = cs->z; }
    return 0;
}

/* ------------------------------------------------------------------ boxes */
static int has_box_paint(const struct cstyle *cs) {
    return cs->bg[3] > 0 || cs->bgimg || cs->has_grad || cs->border[0] > 0 || cs->border[1] > 0 || cs->border[2] > 0 || cs->border[3] > 0;
}
static void emit_box(struct lctx *L, struct dnode *el, CGRect r, int zk, int posk, float alpha) {
    if (L->measuring || el->cs.vis_hidden || !has_box_paint(&el->cs)) return;
    struct witem it = { 0 }; it.kind = IT_BOX; it.el = el; it.r = r; it.alpha = alpha; it.zk = zk; it.posk = posk; it.seq = el->seq;
    if (el->cs.bgimg) it.image = objc_retain(load_image(L, el->cs.bgimg));
    push_item(L, &it);
}

/* ------------------------------------------------------------------ lines */
static void bc_floats_at(struct bctx *bc, double y, double h, double *ll, double *lr) {
    double l = bc->cl, r = bc->cr;
    for (int i = 0; i < bc->nfl; i++) {
        struct flt *f = &bc->fl[i];
        if (f->bottom <= y + 0.01 || y + h <= 0) continue;
        if (!f->right) l = fmax(l, f->x1); else r = fmin(r, f->x0);
    }
    *ll = l; *lr = r;
}
static void begin_line(struct bctx *bc) {
    if (bc->line_open) return;
    if (bc->pend > 0) { bc->y += bc->pend; bc->pend = 0; }
    bc_floats_at(bc, bc->y, 1, &bc->ll, &bc->lr);
    bc->x = bc->ll; bc->line_open = 1; bc->space = 0;
}
static void add_pitem(struct bctx *bc, struct pitem *p) {
    if (bc->nl == bc->lcap) { bc->lcap = bc->lcap ? bc->lcap * 2 : 16; bc->line = realloc(bc->line, bc->lcap * sizeof *bc->line); }
    bc->line[bc->nl++] = *p;
}
static void flush_line(struct bctx *bc, int force) {
    if (!bc->line_open && !force) return;
    if (!bc->line_open) begin_line(bc);
    if (!bc->nl && !force) { bc->line_open = 0; return; }
    struct lctx *L = bc->L;
    int lh_mode = bc->bs->lh.u != 0;
    double strut = line_h(bc->bs), h = bc->nl ? 0 : strut;
    if (lh_mode && bc->nl) h = strut;
    double right = bc->ll;
    for (size_t i = 0; i < bc->nl; i++) {
        struct pitem *p = &bc->line[i];
        double c = p->contrib; if (c > h) h = c;
        double e = p->it.r.origin.x + p->w; if (e > right) right = e;
    }
    double avail = bc->lr - bc->ll, used = right - bc->ll;
    double shift = bc->bs->align == 1 ? (avail - used) / 2 : bc->bs->align == 2 ? avail - used : 0;
    if (shift < 0) shift = 0;
    double top = bc->y;
    for (size_t i = 0; i < bc->nl; i++) {
        struct pitem *p = &bc->line[i];
        double ih = p->h;
        double y = lh_mode || p->lh_set ? top + (h - ih) / 2 : top + h - ih;
        if (p->kind == 0) {
            struct witem it = p->it; it.r.origin.x += shift; it.r.origin.y = y; it.r.size = (CGSize){ p->w, ih };
            if (!L->measuring && it.el) for (struct dnode *e = it.el; e && e != bc->el; e = e->parent) union_box(e, it.r);
            if (it.kind == IT_TEXT && it.el && it.el->cs.vis_hidden) { free(it.text); continue; }
            if (L->measuring) { free(it.text); objc_release(it.image); continue; }
            push_item(L, &it);
        } else {
            /* the box was laid out with its margin box at (0, 0) */
            shift_subtree(L, p->ref, p->rs, p->re, p->it.r.origin.x + shift, y);
            for (struct dnode *e = p->ref->parent; e && e != bc->el; e = e->parent) if (!L->measuring) union_box(e, p->ref->box);
        }
    }
    if (bc->bullet_pending && !L->measuring) {
        const struct cstyle *st = bc->bullet_st ? bc->bullet_st : bc->bs;
        id font = rt_font_make(st->size, st->bold, NULL); double bw = ceil(rt_text_measure(bc->bullet, font, 1e9, 1).width), gh = glyph_h(st);
        struct witem it = { 0 }; it.kind = IT_TEXT; it.el = bc->el; it.text = strdup(bc->bullet); it.size = st->size; it.bold = st->bold;
        memcpy(it.col, st->color, sizeof it.col); it.alpha = bc->alpha; it.zk = bc->zk; it.posk = bc->posk; it.seq = bc->el->seq;
        it.r = (CGRect){ { bc->cl - bw - 6, lh_mode ? top + (h - gh) / 2 : top + h - gh }, { bw, gh } };
        if (!bc->el->cs.vis_hidden) push_item(L, &it); else free(it.text);
    }
    bc->bullet_pending = 0;
    if (right > bc->maxright) bc->maxright = right;
    bc->y = top + h; bc->nl = 0; bc->line_open = 0; bc->space = 0;
}
static void place_word(struct bctx *bc, struct dnode *el, const char *w, size_t n, int zk, int posk, float alpha, long seq) {
    const struct cstyle *st = &el->cs;
    begin_line(bc);
    id font = rt_font_make(st->size, st->bold, NULL);
    char *word = strndup(w, n);
    double ww = ceil(rt_text_measure(word, font, 1e9, 1).width), sw = bc->space && bc->x > bc->ll ? space_w(st->size, st->bold) : 0;
    if (!st->nowrap && bc->x > bc->ll && bc->x + sw + ww > bc->lr + 0.01) { flush_line(bc, 0); begin_line(bc); sw = 0; }
    double gh = glyph_h(st), contrib = st->lh.u ? line_h(st) : gh;
    /* extend the previous run when the style is the same */
    if (bc->nl) {
        struct pitem *last = &bc->line[bc->nl - 1];
        if (last->kind == 0 && last->it.kind == IT_TEXT && last->it.el == el && fabs(last->it.r.origin.x + last->w - bc->x) < 0.01) {
            size_t ol = strlen(last->it.text); last->it.text = realloc(last->it.text, ol + n + 2);
            if (sw > 0) last->it.text[ol++] = ' ';
            memcpy(last->it.text + ol, word, n + 1);
            last->w = ceil(rt_text_measure(last->it.text, font, 1e9, 1).width);
            bc->x = last->it.r.origin.x + last->w; bc->space = 0;
            free(word);
            return;
        }
    }
    bc->x += sw;
    struct pitem p = { 0 };
    p.it.kind = IT_TEXT; p.it.el = el; p.it.text = word; p.it.size = st->size; p.it.bold = st->bold; memcpy(p.it.col, st->color, sizeof p.it.col);
    p.it.alpha = alpha; p.it.zk = zk; p.it.posk = posk; p.it.seq = seq;
    p.it.r.origin.x = bc->x; p.w = ww; p.h = gh; p.contrib = contrib; p.lh_set = st->lh.u != 0;
    add_pitem(bc, &p);
    bc->x += ww; bc->space = 0;
}
static void place_text(struct bctx *bc, struct dnode *el, const char *s, int zk, int posk, float alpha, long seq) {
    const struct cstyle *st = &el->cs;
    char *t = strdup(s);
    if (st->upper) for (char *u = t; *u; u++) if (*u >= 'a' && *u <= 'z') *u -= 32;
    if (st->pre) {
        char *line = t;
        for (;;) {
            char *nl = strchr(line, '\n');
            size_t k = nl ? (size_t)(nl - line) : strlen(line);
            if (k) { int save = bc->space; bc->space = 0; place_word(bc, el, line, k, zk, posk, alpha, seq); bc->space = save; }
            if (!nl) break;
            begin_line(bc); flush_line(bc, 1);
            line = nl + 1;
        }
        free(t); return;
    }
    const char *p = t;
    while (*p) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f') { if (bc->line_open) bc->space = 1; p++; continue; }
        size_t k = 0; while (p[k] && !(p[k] == ' ' || p[k] == '\t' || p[k] == '\n' || p[k] == '\r' || p[k] == '\f')) k++;
        place_word(bc, el, p, k, zk, posk, alpha, seq); p += k;
        if (*p) bc->space = 1;
    }
    free(t);
}
static void place_atomic(struct bctx *bc, struct pitem *p) {
    begin_line(bc);
    double sw = bc->space && bc->x > bc->ll ? space_w(bc->bs->size, bc->bs->bold) : 0;
    if (bc->x > bc->ll && bc->x + sw + p->w > bc->lr + 0.01) { flush_line(bc, 0); begin_line(bc); sw = 0; }
    bc->x += sw; p->it.r.origin.x = bc->x;
    add_pitem(bc, p);
    bc->x += p->w; bc->space = 0;
}

/* ------------------------------------------------------------------ box geometry */
struct geom { double mt, mr, mb, ml, pt, pr, pb, pl, bt, br, bb, bl; };
static struct geom geom_of(const struct cstyle *cs, double cbw) {
    struct geom g;
    g.mt = len_px(cs->margin[0], cbw, 0); g.mr = len_px(cs->margin[1], cbw, 0); g.mb = len_px(cs->margin[2], cbw, 0); g.ml = len_px(cs->margin[3], cbw, 0);
    g.pt = fmax(0, len_px(cs->pad[0], cbw, 0)); g.pr = fmax(0, len_px(cs->pad[1], cbw, 0)); g.pb = fmax(0, len_px(cs->pad[2], cbw, 0)); g.pl = fmax(0, len_px(cs->pad[3], cbw, 0));
    g.bt = cs->border[0]; g.br = cs->border[1]; g.bb = cs->border[2]; g.bl = cs->border[3];
    return g;
}
/* height of the containing block if it is definite, else -1 */
static double definite_height(struct dnode *el) {
    for (struct dnode *p = el->parent; p && p->type == N_ELEMENT; p = p->parent) {
        if (p->cs.height.u == 'x') return p->cs.height.v;
        if (p->cs.position == P_ABSOLUTE || p->cs.position == P_FIXED) return p->laid ? p->cbox.size.height : -1;
        if (p->cs.height.u != '%') return -1;
    }
    return -1;
}

/* lays out el's content (children flow) with content edges [cl, cr] starting at top; returns content height */
static double layout_contents(struct lctx *L, struct dnode *el, double cl, double cr, double top, double *maxright, int zk, int posk, float alpha, double *trail) {
    struct bctx bc = { 0 };
    bc.L = L; bc.el = el; bc.bs = &el->cs; bc.cl = cl; bc.cr = cr; bc.y = top; bc.maxright = cl; bc.zk = zk; bc.posk = posk; bc.alpha = alpha;
    for (int i = 0; i < L->npfl && bc.nfl < 16; i++) bc.fl[bc.nfl++] = L->pfl[i];
    L->pfl = NULL; L->npfl = 0;
    if (el->cs.display == D_LIST_ITEM && !el->cs.nobullet) {
        bc.bullet_pending = 1; bc.bullet_st = &el->cs;
        if (el->cs.list_decimal) { long k = 1; for (struct dnode *s = el->prev; s; s = s->prev) if (s->type == N_ELEMENT && s->cs.display == D_LIST_ITEM) k++;
            const char *st = el->parent ? dom_attr(el->parent, "start") : NULL; if (st) k += atol(st) - 1;
            snprintf(bc.bullet, sizeof bc.bullet, "%ld.", k); }
        else snprintf(bc.bullet, sizeof bc.bullet, "\xe2\x80\xa2");
    }
    flow_children(&bc, el, zk, posk, alpha);
    flush_line(&bc, 0);
    if (bc.bullet_pending) { begin_line(&bc); flush_line(&bc, 1); }
    double h = bc.y - top;
    if (trail) *trail = bc.pend; else h += bc.pend;
    for (int i = 0; i < bc.nfl; i++) if (bc.fl[i].bottom - top > h && (el->cs.overflow_hidden || el->cs.display == D_INLINE_BLOCK || el->cs.floatv || el->cs.position >= P_ABSOLUTE)) h = bc.fl[i].bottom - top;
    if (maxright) *maxright = bc.maxright;
    free(bc.line);
    return h;
}

/* shrink-to-fit width of el's content within avail (content width) */
static double shrink_width(struct lctx *L, struct dnode *el, double avail) {
    size_t mark = L->d->nitems; int ncb = L->ncb; size_t cbn[64]; for (int i = 0; i < ncb; i++) cbn[i] = L->cbs[i].n;
    int was = L->measuring; L->measuring = 1; L->pfl = NULL; L->npfl = 0;
    double mr = 0; layout_contents(L, el, 0, avail, 0, &mr, 0, 0, 1, NULL);
    L->measuring = was;
    truncate_items(L, mark);
    for (int i = 0; i < ncb; i++) L->cbs[i].n = cbn[i];
    L->ncb = ncb;
    return fmin(fmax(mr, 0), avail);
}

static void cb_push(struct lctx *L, struct dnode *el) { if (L->ncb < 64) { struct cbframe *f = &L->cbs[L->ncb++]; memset(f, 0, sizeof *f); f->el = el; } }
static void cb_defer(struct lctx *L, struct dnode *el, double sx, double sy, int zk, float alpha) {
    if (L->measuring || !L->ncb) return;
    struct cbframe *f = &L->cbs[el->cs.position == P_FIXED ? 0 : L->ncb - 1];
    if (f->n == f->cap) { f->cap = f->cap ? f->cap * 2 : 8; f->a = realloc(f->a, f->cap * sizeof *f->a); }
    f->a[f->n++] = (struct absent){ el, sx, sy, zk, alpha };
}
static void layout_abs(struct lctx *L, struct absent *a, CGRect cb);
static void cb_pop(struct lctx *L, CGRect cb) {
    if (!L->ncb) return;
    struct cbframe f = L->cbs[--L->ncb];
    for (size_t i = 0; i < f.n; i++) layout_abs(L, &f.a[i], cb);
    free(f.a);
}

/* places a box whose border-box x/width are known: content layout, height, items */
static CGRect finish_box(struct lctx *L, struct dnode *el, double bx, double by, double cw, struct geom g, double cbh, int zk, int posk, float alpha, double *mr_out, double *mb_carry) {
    const struct cstyle *cs = &el->cs;
    double cl = bx + g.bl + g.pl, ctop = by + g.bt + g.pt;
    size_t box_index = L->d->nitems;
    emit_box(L, el, (CGRect){ { bx, by }, { cw + g.pl + g.pr + g.bl + g.br, 0 } }, zk, posk, alpha);
    int has_box = L->d->nitems > box_index;
    el->laid = 1; el->box = (CGRect){ { bx, by }, { cw + g.pl + g.pr + g.bl + g.br, 0 } }; el->cbox = (CGRect){ { cl, ctop }, { cw, 0 } };
    int is_cb = cs->position != P_STATIC;
    if (is_cb) cb_push(L, el);
    double ch, mr = cl;
    if (!strcmp(el->tag, "img") || !strcmp(el->tag, "iframe")) {
        ch = 0;
        if (!strcmp(el->tag, "img")) {
            id img = load_image(L, dom_attr(el, "src"));
            CGSize sz = img ? rt_uiimage_size(img) : (CGSize){ 0, 0 };
            double h = len_px(cs->height, cbh, -1);
            if (h < 0) h = sz.width > 0 ? cw * sz.height / sz.width : 0;
            ch = h;
            if (img && !L->measuring && !cs->vis_hidden) { struct witem it = { 0 }; it.kind = IT_IMAGE; it.el = el; it.image = objc_retain(img); it.alpha = alpha; it.zk = zk; it.posk = posk; it.seq = el->seq; it.r = (CGRect){ { cl, ctop }, { cw, h } }; push_item(L, &it); }
        }
        mr = cl + cw;
    } else if (!strcmp(el->tag, "hr")) {
        ch = 1; mr = cl + cw;
        if (!L->measuring && !cs->vis_hidden && !has_box_paint(cs)) { struct witem it = { 0 }; it.kind = IT_RULE; it.el = el; memcpy(it.col, cs->color, sizeof it.col); it.col[3] *= .4f; it.alpha = alpha; it.zk = zk; it.posk = posk; it.seq = el->seq; it.r = (CGRect){ { cl, ctop }, { cw, 1 } }; push_item(L, &it); }
    } else {
        /* the last child's bottom margin collapses through a box without bottom padding/border and auto height */
        double trail = 0;
        ch = layout_contents(L, el, cl, cl + cw, ctop, &mr, zk, posk, alpha, &trail);
        int through = mb_carry && g.pb == 0 && g.bb == 0 && is_auto(cs->height) && !cs->overflow_hidden;
        if (through) *mb_carry = trail; else ch += trail;
    }
    double h = len_px(cs->height, cbh >= 0 ? cbh : 0, -1);
    if (cs->height.u == '%' && cbh < 0) h = -1;
    if (h < 0) h = ch;
    double minh = len_px(cs->minh, cbh >= 0 ? cbh : 0, 0); if (h < minh) h = minh;
    double bh = h + g.pt + g.pb + g.bt + g.bb;
    el->box.size.height = bh; el->cbox.size.height = h;
    if (has_box) L->d->items[box_index].r.size.height = bh;
    if (mr_out) *mr_out = mr + g.pr + g.br;
    if (is_cb && cs->position != P_ABSOLUTE && cs->position != P_FIXED) {
        /* relative: offsets move the box and everything in it, then its abs children */
        double dx = !is_auto(cs->left) ? len_px(cs->left, cw, 0) : !is_auto(cs->right) ? -len_px(cs->right, cw, 0) : 0;
        double dy = !is_auto(cs->top) ? len_px(cs->top, cbh > 0 ? cbh : 0, 0) : !is_auto(cs->bottom) ? -len_px(cs->bottom, cbh > 0 ? cbh : 0, 0) : 0;
        shift_subtree(L, el, box_index, L->d->nitems, dx, dy);
    }
    if (is_cb) cb_pop(L, (CGRect){ { el->box.origin.x + g.bl, el->box.origin.y + g.bt }, { el->box.size.width - g.bl - g.br, el->box.size.height - g.bt - g.bb } });
    return el->box;
}

static void layout_abs(struct lctx *L, struct absent *a, CGRect cb) {
    struct dnode *el = a->el; const struct cstyle *cs = &el->cs;
    if (el->cs.position == P_FIXED) cb = (CGRect){ { 0, 0 }, { L->d->vw, L->d->vh } };
    struct geom g = geom_of(cs, cb.size.width);
    int lset = !is_auto(cs->left), rset = !is_auto(cs->right), tset = !is_auto(cs->top), bset = !is_auto(cs->bottom);
    double lv = len_px(cs->left, cb.size.width, 0), rv = len_px(cs->right, cb.size.width, 0), tv = len_px(cs->top, cb.size.height, 0), bv = len_px(cs->bottom, cb.size.height, 0);
    double hext = g.ml + g.mr + g.pl + g.pr + g.bl + g.br;
    double cw;
    if (!is_auto(cs->width)) cw = len_px(cs->width, cb.size.width, 0);
    else if (lset && rset) cw = cb.size.width - lv - rv - hext;
    else cw = shrink_width(L, el, fmax(0, cb.size.width - (lset ? lv : 0) - (rset ? rv : 0) - hext));
    if (cw < 0) cw = 0;
    double bw = cw + g.pl + g.pr + g.bl + g.br;
    double bx = lset ? cb.origin.x + lv + g.ml : rset ? cb.origin.x + cb.size.width - rv - g.mr - bw : a->sx + g.ml;
    struct wlen saved_h = el->cs.height;
    if (is_auto(cs->height) && tset && bset) { el->cs.height = (struct wlen){ cb.size.height - tv - bv - g.mt - g.mb - g.pt - g.pb - g.bt - g.bb, 'x' }; if (el->cs.height.v < 0) el->cs.height.v = 0; }
    int zk = a->zk, posk = 1; if (!cs->zauto) zk = cs->z;
    float alpha = a->alpha * (float)cs->opacity;
    double by0 = tset ? cb.origin.y + tv + g.mt : 0;
    size_t s = L->d->nitems;
    /* positioned: abs children of this box come back to its own frame inside finish_box */
    CGRect box = finish_box(L, el, bx, by0, cw, g, cb.size.height, zk, posk, alpha, NULL, NULL);
    double by = tset ? by0 : bset ? cb.origin.y + cb.size.height - bv - g.mb - box.size.height : a->sy + g.mt;
    if (!tset) shift_subtree(L, el, s, L->d->nitems, 0, by - by0);
    el->cs.height = saved_h;
}

/* top margins of a box without top padding/border collapse with its first in-flow block child's (and so on) */
static int collapses_top(const struct cstyle *cs) {
    return cs->pad[0].v == 0 && cs->border[0] == 0 && !cs->overflow_hidden && cs->display != D_INLINE_BLOCK && !cs->floatv && cs->position < P_ABSOLUTE;
}
static struct dnode *first_block_child(struct dnode *el) {
    for (struct dnode *k = el->first; k; k = k->next) {
        if (k->type == N_COMMENT) continue;
        if (k->type == N_TEXT) { if (k->text && strspn(k->text, " \t\r\n\f") == strlen(k->text) && !el->cs.pre) continue; return NULL; }
        if (k->type != N_ELEMENT) continue;
        if (k->cs.display == D_NONE || k->cs.position >= P_ABSOLUTE) continue;
        if (k->cs.floatv) return NULL;
        if (k->cs.display == D_BLOCK || k->cs.display == D_LIST_ITEM) return strcmp(k->tag, "hr") && strcmp(k->tag, "img") ? k : k;
        return NULL;
    }
    return NULL;
}

/* an in-flow block child of parent */
static void layout_block(struct bctx *bc, struct dnode *el, int zk, int posk, float alpha) {
    struct lctx *L = bc->L; const struct cstyle *cs = &el->cs;
    flush_line(bc, 0);
    double avail = bc->cr - bc->cl;
    struct geom g = geom_of(cs, avail);
    int collapsed_into_parent = 0;
    for (int i = 0; i < L->nnomt; i++) if (L->nomt[i] == el) { g.mt = 0; L->nomt[i] = L->nomt[--L->nnomt]; collapsed_into_parent = 1; break; }
    double m = fmax(bc->pend, g.mt); if (g.mt < 0 || bc->pend < 0) m = bc->pend + g.mt;
    /* margins of first children collapse into this one: the children then start without theirs */
    struct dnode *chain_last = NULL;
    if (!collapsed_into_parent && collapses_top(cs) && !(cs->display == D_LIST_ITEM && !cs->nobullet)) {
        for (struct dnode *c = first_block_child(el), *p = el; c; p = c, c = collapses_top(&c->cs) ? first_block_child(c) : NULL) {
            (void)p; double cm = len_px(c->cs.margin[0], bc->cr - bc->cl, 0); if (cm > m) m = cm; chain_last = c;
            if (L->nnomt < 32) L->nomt[L->nnomt++] = c;
            if (!collapses_top(&c->cs)) break;
        }
    }
    (void)chain_last;
    double by = bc->y + m;
    bc->pend = 0;
    double cw, bx;
    double hext = g.pl + g.pr + g.bl + g.br;
    int is_img = !strcmp(el->tag, "img");
    int lauto = cs->margin[3].u == 'A', rauto = cs->margin[1].u == 'A';
    if (!is_auto(cs->width) || is_img) {
        if (!is_auto(cs->width)) cw = len_px(cs->width, avail, 0);
        else { id img = load_image(L, dom_attr(el, "src")); CGSize sz = img ? rt_uiimage_size(img) : (CGSize){ 0, 0 };
               double h = len_px(cs->height, 0, -1); cw = h > 0 && sz.height > 0 ? h * sz.width / sz.height : sz.width; if (cw > avail - hext) cw = fmax(0, avail - hext); }
        double used = cw + hext;
        if (lauto && rauto) bx = bc->cl + fmax(0, (avail - used) / 2);
        else if (lauto) bx = bc->cl + fmax(0, avail - used - g.mr);
        else bx = bc->cl + g.ml;
    } else {
        cw = avail - g.ml - g.mr - hext; if (cw < 0) cw = 0;
        bx = bc->cl + g.ml;
    }
    double cbh = definite_height(el);
    double mr = 0;
    /* the child's lines flow around the parent's floats */
    L->pfl = bc->fl; L->npfl = bc->nfl;
    double carry = 0;
    finish_box(L, el, bx, by, cw, g, cbh, zk, posk, alpha, &mr, &carry);
    L->pfl = NULL; L->npfl = 0;
    double bottom = el->box.origin.y + el->box.size.height;
    if (cs->position == P_RELATIVE) bottom -= !is_auto(cs->top) ? len_px(cs->top, cbh > 0 ? cbh : 0, 0) : !is_auto(cs->bottom) ? -len_px(cs->bottom, cbh > 0 ? cbh : 0, 0) : 0;
    bc->y = bottom;
    bc->pend = fmax(g.mb, carry);
    double right = is_auto(cs->width) ? mr + g.mr : el->box.origin.x + el->box.size.width + g.mr;
    if (right > bc->maxright) bc->maxright = right;
}

static void layout_float(struct bctx *bc, struct dnode *el, int zk, int posk, float alpha) {
    struct lctx *L = bc->L; const struct cstyle *cs = &el->cs;
    double avail = bc->cr - bc->cl;
    struct geom g = geom_of(cs, avail);
    double hext = g.pl + g.pr + g.bl + g.br;
    double cw = !is_auto(cs->width) ? len_px(cs->width, avail, 0) : shrink_width(L, el, fmax(0, avail - hext - g.ml - g.mr));
    double ow = cw + hext + g.ml + g.mr;
    double y = bc->y + (bc->line_open || bc->nl ? 0 : bc->pend);
    double ll, lr; bc_floats_at(bc, y, 1, &ll, &lr);
    int right = cs->floatv == 2;
    double ox = right ? lr - ow : ll;
    size_t s = L->d->nitems;
    finish_box(L, el, ox + g.ml, y + g.mt, cw, g, -1, zk, posk, alpha, NULL, NULL);
    (void)s;
    double bottom = el->box.origin.y + el->box.size.height + g.mb;
    if (bc->nfl < 16) bc->fl[bc->nfl++] = (struct flt){ ox, ox + ow, bottom, right };
    if (bc->line_open) { if (!right && bc->x < ox + ow) { bc->ll = ox + ow; if (!bc->nl) bc->x = bc->ll; } if (right) bc->lr = fmin(bc->lr, ox); }
    if (ox + ow > bc->maxright) bc->maxright = ox + ow;
}

static void layout_inline_block(struct bctx *bc, struct dnode *el, int zk, int posk, float alpha) {
    struct lctx *L = bc->L; const struct cstyle *cs = &el->cs;
    double avail = bc->cr - bc->cl;
    struct geom g = geom_of(cs, avail);
    double hext = g.pl + g.pr + g.bl + g.br;
    double cw;
    int is_img = !strcmp(el->tag, "img");
    id img = is_img ? load_image(L, dom_attr(el, "src")) : NULL;
    CGSize nat = img ? rt_uiimage_size(img) : (CGSize){ 0, 0 };
    if (!is_auto(cs->width)) cw = len_px(cs->width, avail, 0);
    else if (is_img) { double h = len_px(cs->height, 0, -1); cw = h > 0 && nat.height > 0 ? h * nat.width / nat.height : nat.width; }
    else if (!strcmp(el->tag, "iframe")) cw = 0;
    else cw = shrink_width(L, el, fmax(0, avail - hext - g.ml - g.mr));
    if (is_img && !img && is_auto(cs->height)) return;   /* broken image without a size: nothing */
    if (is_img && cw > avail - hext) { cw = fmax(10, avail - hext); }
    size_t s = L->d->nitems;
    struct cstyle saved = el->cs;
    if (is_img && is_auto(cs->height) && nat.width > 0) el->cs.height = (struct wlen){ cw * nat.height / nat.width, 'x' };
    finish_box(L, el, g.ml, g.mt, cw, g, -1, zk, posk, alpha, NULL, NULL);
    el->cs.height = saved.height;
    struct pitem p = { 0 };
    p.kind = 1; p.ref = el; p.rs = s; p.re = L->d->nitems;
    p.w = el->box.size.width + g.ml + g.mr; p.h = el->box.size.height + g.mt + g.mb; p.contrib = p.h;
    /* finish_box placed its margin box at (0, 0); flush_line moves it into the line */
    place_atomic(bc, &p);
}

static void flow_children(struct bctx *bc, struct dnode *el, int zk, int posk, float alpha) {
    struct lctx *L = bc->L;
    for (struct dnode *k = el->first; k; k = k->next) {
        if (k->type == N_TEXT) { if (k->text && *k->text) place_text(bc, el, k->text, zk, posk, alpha, k->seq); continue; }
        if (k->type != N_ELEMENT) continue;
        const struct cstyle *cs = &k->cs;
        k->laid = 0;
        for (struct dnode *n = k->first; n; n = dom_next_pre(n, k)) n->laid = 0;
        if (cs->display == D_NONE) continue;
        int kz = zk, kp = posk; elem_paint_key(cs, &kz, &kp);
        float ka = alpha * (float)cs->opacity;
        if (cs->position == P_ABSOLUTE || cs->position == P_FIXED) {
            double sx = bc->line_open ? bc->x : bc->cl, sy = bc->line_open ? bc->y : bc->y + bc->pend;
            if (cs->display != D_INLINE && cs->display != D_INLINE_BLOCK && bc->line_open) { sx = bc->cl; sy = bc->y + line_h(bc->bs); }
            cb_defer(L, k, sx, sy, zk, alpha);
            continue;
        }
        if (cs->floatv) { layout_float(bc, k, kz, kp, ka); continue; }
        if (!strcmp(k->tag, "br")) { begin_line(bc); flush_line(bc, 1); continue; }
        if (cs->display == D_BLOCK || cs->display == D_LIST_ITEM) { layout_block(bc, k, kz, kp, ka); continue; }
        if (cs->display == D_INLINE_BLOCK || !strcmp(k->tag, "img") || !strcmp(k->tag, "iframe")) { layout_inline_block(bc, k, kz, kp, ka); continue; }
        /* inline element: its padding/border left/right take room in the line */
        double pl = len_px(cs->pad[3], 0, 0) + cs->border[3] + len_px(cs->margin[3], 0, 0), pr = len_px(cs->pad[1], 0, 0) + cs->border[1] + len_px(cs->margin[1], 0, 0);
        if (pl > 0) { begin_line(bc); if (bc->space && bc->x > bc->ll) bc->x += space_w(el->cs.size, el->cs.bold); bc->space = 0; bc->x += pl; }
        flow_children(bc, k, kz, kp, ka);
        if (pr > 0 && bc->line_open) bc->x += pr;
    }
}

/* ------------------------------------------------------------------ entry points */
static int cmp_items(const void *a, const void *b) {
    const struct witem *x = a, *y = b;
    if (x->zk != y->zk) return x->zk < y->zk ? -1 : 1;
    if (x->posk != y->posk) return x->posk - y->posk;
    if (x->seq != y->seq) return x->seq < y->seq ? -1 : 1;
    return x->sub < y->sub ? -1 : x->sub > y->sub;
}
void web_layout(struct wdocument *d) {
    for (size_t i = 0; i < d->nitems; i++) { free(d->items[i].text); objc_release(d->items[i].image); }
    d->nitems = 0;
    if (!d->sheet) d->sheet = css_new();
    css_cascade(d);
    struct lctx *L = calloc(1, sizeof *L); L->d = d;
    for (struct dnode *n = d->root; n; n = dom_next_pre(n, d->root)) n->laid = 0;
    struct dnode *html = dom_html(d);
    cb_push(L, NULL);   /* the initial containing block */
    double h = 0;
    if (html && html->cs.display != D_NONE) {
        struct bctx root = { 0 };
        root.L = L; root.el = d->root; root.bs = &d->root->cs; root.cl = 0; root.cr = d->vw; root.y = 0; root.alpha = 1;
        int zk = 0, pk = 0; elem_paint_key(&html->cs, &zk, &pk);
        layout_block(&root, html, zk, pk, (float)html->cs.opacity);
        h = html->box.origin.y + html->box.size.height + root.pend;
        free(root.line);
    }
    cb_pop(L, (CGRect){ { 0, 0 }, { d->vw, d->vh } });
    /* inline elements with backgrounds or borders: one box around their runs, under them */
    for (size_t i = 0; i < d->nall; i++) {
        struct dnode *e = d->all[i];
        if (e->type != N_ELEMENT || !e->laid || e->cs.display != D_INLINE || e->cs.floatv || e->cs.position >= P_ABSOLUTE || e->cs.vis_hidden) continue;
        if (!strcmp(e->tag, "img") || !strcmp(e->tag, "iframe") || !has_box_paint(&e->cs) || !dom_connected(e)) continue;
        struct geom g = geom_of(&e->cs, 0);
        e->box = (CGRect){ { e->box.origin.x - g.pl - g.bl, e->box.origin.y - g.pt - g.bt }, { e->box.size.width + g.pl + g.pr + g.bl + g.br, e->box.size.height + g.pt + g.pb + g.bt + g.bb } };
        float a = 1; for (struct dnode *x = e; x && x->type == N_ELEMENT; x = x->parent) a *= (float)x->cs.opacity;
        struct witem it = { 0 }; it.kind = IT_BOX; it.el = e; it.r = e->box; it.alpha = a; it.seq = e->seq;
        int zk = 0, pk = 0; for (struct dnode *x = e; x && x->type == N_ELEMENT; x = x->parent) if (x->cs.position != P_STATIC) { pk = 1; if (!x->cs.zauto) { zk = x->cs.z; break; } }
        it.zk = zk; it.posk = pk;
        if (e->cs.bgimg) it.image = objc_retain(load_image(L, e->cs.bgimg));
        push_item(L, &it);
    }
    for (int i = 0; i < L->nimg; i++) { free(L->imgs[i].url); objc_release(L->imgs[i].img); }
    free(L);
    struct dnode *body = dom_body(d);
    int clip = (html && html->cs.overflow_hidden) || (body && body->cs.overflow_hidden);
    if (!clip) for (size_t i = 0; i < d->nitems; i++) {
        struct witem *it = &d->items[i];
        if (it->el && it->el->cs.position == P_FIXED) continue;
        double b = it->r.origin.y + it->r.size.height;
        if (it->kind != IT_BOX && b > h) h = b;
    }
    d->height = clip ? d->vh : h;
    qsort(d->items, d->nitems, sizeof *d->items, cmp_items);
    d->dirty = 0;
}
void web_ensure_layout(struct wdocument *d) { if (d && d->dirty) web_layout(d); }

/* ------------------------------------------------------------------ painting */
static void rounded_path(struct cgctx *c, CGRect r, double rad) {
    rad = fmin(rad, fmin(r.size.width, r.size.height) / 2);
    double x0 = r.origin.x, y0 = r.origin.y, x1 = x0 + r.size.width, y1 = y0 + r.size.height;
    iosrt_CGContextBeginPath(c);
    iosrt_CGContextMoveToPoint(c, x0 + rad, y0);
    iosrt_CGContextAddArcToPoint(c, x1, y0, x1, y1, rad);
    iosrt_CGContextAddArcToPoint(c, x1, y1, x0, y1, rad);
    iosrt_CGContextAddArcToPoint(c, x0, y1, x0, y0, rad);
    iosrt_CGContextAddArcToPoint(c, x0, y0, x1, y0, rad);
    iosrt_CGContextClosePath(c);
}
static void paint_box(struct cgctx *c, struct witem *it) {
    const struct cstyle *cs = &it->el->cs; CGRect r = it->r; float a = it->alpha;
    double rad = cs->radius;
    if (cs->bg[3] > 0) {
        iosrt_CGContextSetRGBFillColor(c, cs->bg[0], cs->bg[1], cs->bg[2], cs->bg[3] * a);
        if (rad > 0) { rounded_path(c, r, rad); iosrt_CGContextFillPath(c); } else iosrt_CGContextFillRect(c, r);
    }
    if (cs->has_grad && r.size.height > 0) {
        /* vertical two-stop gradient, drawn in bands */
        int bands = (int)fmin(64, fmax(2, r.size.height / 2));
        iosrt_CGContextSaveGState(c);
        if (rad > 0) { rounded_path(c, r, rad); iosrt_CGContextClip(c); }
        for (int i = 0; i < bands; i++) {
            float t = (i + .5f) / bands; float col[4];
            for (int k = 0; k < 4; k++) col[k] = cs->grad[0][k] + (cs->grad[1][k] - cs->grad[0][k]) * t;
            iosrt_CGContextSetRGBFillColor(c, col[0], col[1], col[2], col[3] * a);
            double y0 = r.origin.y + r.size.height * i / bands, y1 = r.origin.y + r.size.height * (i + 1) / bands;
            iosrt_CGContextFillRect(c, (CGRect){ { r.origin.x, floor(y0) }, { r.size.width, ceil(y1) - floor(y0) } });
        }
        iosrt_CGContextRestoreGState(c);
    }
    double bt = cs->border[0], brt = cs->border[1], bb = cs->border[2], bl = cs->border[3];
    if (it->image) {
        CGRect pad = { { r.origin.x + bl, r.origin.y + bt }, { r.size.width - bl - brt, r.size.height - bt - bb } };
        CGSize nat = rt_uiimage_size(it->image), sz = nat;
        if (cs->bgsize_kind == 1) {
            double w = len_px(cs->bgsize[0], pad.size.width, -1), h = len_px(cs->bgsize[1], pad.size.height, -1);
            if (w < 0 && h < 0) { w = nat.width; h = nat.height; } else if (w < 0) w = nat.height > 0 ? h * nat.width / nat.height : 0; else if (h < 0) h = nat.width > 0 ? w * nat.height / nat.width : 0;
            sz = (CGSize){ w, h };
        } else if (cs->bgsize_kind >= 2 && nat.width > 0 && nat.height > 0) {
            double sx = pad.size.width / nat.width, sy = pad.size.height / nat.height, s = cs->bgsize_kind == 2 ? fmax(sx, sy) : fmin(sx, sy);
            sz = (CGSize){ nat.width * s, nat.height * s };
        }
        if (sz.width >= 0.5 && sz.height >= 0.5) {
            double ox = pad.origin.x + (pad.size.width - sz.width) * cs->bgpos[0], oy = pad.origin.y + (pad.size.height - sz.height) * cs->bgpos[1];
            iosrt_CGContextSaveGState(c);
            if (rad > 0) { rounded_path(c, r, rad); iosrt_CGContextClip(c); } else iosrt_CGContextClipToRect(c, pad);
            if (cs->bg_norepeat) rt_uiimage_draw(it->image, c, (CGRect){ { ox, oy }, sz }, a, NULL);
            else {
                double sx0 = ox - ceil((ox - pad.origin.x) / sz.width) * sz.width, sy0 = oy - ceil((oy - pad.origin.y) / sz.height) * sz.height;
                int count = 0;
                for (double y = sy0; y < pad.origin.y + pad.size.height && count < 2000; y += sz.height)
                    for (double x = sx0; x < pad.origin.x + pad.size.width && count < 2000; x += sz.width, count++)
                        rt_uiimage_draw(it->image, c, (CGRect){ { x, y }, sz }, a, NULL);
            }
            iosrt_CGContextRestoreGState(c);
        }
    }
    if (bt > 0 || brt > 0 || bb > 0 || bl > 0) {
        if (rad > 0 && bt == brt && bt == bb && bt == bl) {
            const float *col = cs->bcol[0];
            iosrt_CGContextSetRGBStrokeColor(c, col[0], col[1], col[2], col[3] * a);
            iosrt_CGContextSetLineWidth(c, bt);
            rounded_path(c, (CGRect){ { r.origin.x + bt / 2, r.origin.y + bt / 2 }, { r.size.width - bt, r.size.height - bt } }, fmax(0, rad - bt / 2));
            iosrt_CGContextStrokePath(c);
        } else {
            CGRect side[4] = {
                { { r.origin.x, r.origin.y }, { r.size.width, bt } },
                { { r.origin.x + r.size.width - brt, r.origin.y + bt }, { brt, r.size.height - bt - bb } },
                { { r.origin.x, r.origin.y + r.size.height - bb }, { r.size.width, bb } },
                { { r.origin.x, r.origin.y + bt }, { bl, r.size.height - bt - bb } } };
            for (int s = 0; s < 4; s++) if (cs->border[s] > 0 && cs->bcol[s][3] > 0) {
                iosrt_CGContextSetRGBFillColor(c, cs->bcol[s][0], cs->bcol[s][1], cs->bcol[s][2], cs->bcol[s][3] * a);
                iosrt_CGContextFillRect(c, side[s]);
            }
        }
    }
}
void web_paint(struct wdocument *d, struct cgctx *c, CGRect vis) {
    double y0 = vis.origin.y - 64, y1 = vis.origin.y + vis.size.height + 64;
    for (size_t i = 0; i < d->nitems; i++) {
        struct witem *it = &d->items[i];
        if (it->r.origin.y + it->r.size.height < y0 || it->r.origin.y > y1) continue;
        if (it->alpha <= 0.001f) continue;
        if (it->kind == IT_TEXT) {
            float col[4] = { it->col[0], it->col[1], it->col[2], it->col[3] * it->alpha };
            rt_text_draw(c, it->text, rt_font_make(it->size, it->bold, NULL), col, (CGRect){ it->r.origin, { it->r.size.width + 2, it->r.size.height } }, 0, 1, 0);
        } else if (it->kind == IT_IMAGE && it->image) rt_uiimage_draw(it->image, c, it->r, it->alpha, NULL);
        else if (it->kind == IT_RULE) { iosrt_CGContextSetRGBFillColor(c, it->col[0], it->col[1], it->col[2], it->col[3] * it->alpha); iosrt_CGContextFillRect(c, it->r); }
        else if (it->kind == IT_BOX) paint_box(c, it);
    }
}

/* ------------------------------------------------------------------ hit testing */
static void key_of(struct dnode *e, int *zk, int *posk) {
    /* the same stacking key the layout used: nearest positioned ancestor with a z-index */
    int z = 0, p = 0, zset = 0;
    for (struct dnode *a = e; a && a->type == N_ELEMENT; a = a->parent) {
        if (a->cs.position != P_STATIC) { p = 1; if (!a->cs.zauto && !zset) { z = a->cs.z; zset = 1; } }
    }
    *zk = z; *posk = p;
}
struct dnode *web_hit(struct wdocument *d, double x, double y) {
    web_ensure_layout(d);
    struct dnode *best = NULL; int bz = 0, bp = 0; long bs = 0;
    for (size_t i = 0; i < d->nall; i++) {
        struct dnode *e = d->all[i];
        if (e->type != N_ELEMENT || !e->laid || !dom_connected(e)) continue;
        if (e->cs.vis_hidden || e->cs.no_pointer || e->cs.display == D_NONE) continue;
        int hidden = 0; for (struct dnode *a = e->parent; a && a->type == N_ELEMENT; a = a->parent) if (a->cs.display == D_NONE) { hidden = 1; break; }
        if (hidden) continue;
        CGRect b = e->box;
        if (x < b.origin.x || y < b.origin.y || x >= b.origin.x + b.size.width || y >= b.origin.y + b.size.height) continue;
        int zk, pk; key_of(e, &zk, &pk);
        if (!best || zk > bz || (zk == bz && (pk > bp || (pk == bp && e->seq > bs)))) { best = e; bz = zk; bp = pk; bs = e->seq; }
    }
    return best;
}
