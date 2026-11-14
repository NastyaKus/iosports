/*
 * Compiled asset catalogs (Assets.car), read for -[UIImage imageNamed:].
 *
 * The file is a BOM store (big-endian block table and named variables). Its
 * FACETKEYS tree maps image names to attribute lists (the identifier), the
 * RENDITIONS tree maps rendition keys (attribute values in KEYFORMAT order)
 * to "CSI" records: a header (size, scale, pixel format, layout), TLV
 * metadata and the pixels - LZFSE/LZVN-compressed BGRA or gray+alpha rows
 * (MLEC, possibly in KCBC row chunks), or raw JPEG data (RAWD). Since Xcode 8
 * small images are packed into atlases: their renditions are internal links
 * (layout 1003) naming the atlas rendition and a rectangle in it.
 * Written from the observed structure of catalogs; no Apple code is used.
 */
#include "uikit_internal.h"
#include "gfx.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

long rt_lzfse_decode(const uint8_t *src, size_t n, uint8_t *out, size_t cap);
id rt_cgimage_new(struct gfx_image *g);
id rt_uiimage_from_cg(id cg, double scale);
extern float rt_scr_scale;

enum { A_ELEMENT = 1, A_PART = 2, A_DIRECTION = 4, A_VALUE = 6, A_DIM1 = 8, A_DIM2 = 9, A_STATE = 10, A_SCALE = 12,
       A_IDIOM = 15, A_SUBTYPE = 16, A_IDENTIFIER = 17, A_SIZECLASS_H = 22, A_SIZECLASS_V = 23, A_MEMORY = 24, A_GRAPHICS = 25 };
#define MAX_ATTRS 32

struct facet { const char *name; size_t len; uint16_t ident; };
struct rendition { const uint16_t *key; const uint8_t *csi; size_t csi_len; uint16_t ident; };
struct catalog {
    char *path;
    const uint8_t *map; size_t size;
    uint32_t nattrs; uint32_t attrs[MAX_ATTRS];
    struct facet *facets; size_t nfacets;
    struct rendition *rends; size_t nrends;     /* sorted by identifier */
    struct { size_t idx; struct gfx_image *img; } atlas[48]; int natlas;
};

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t le32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t le16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

/* ------------------------------------------------------------- BOM store */
struct bom { const uint8_t *f; size_t size; uint32_t nblocks; const uint8_t *blocks; const uint8_t *vars; };
static int bom_block(const struct bom *b, uint32_t i, const uint8_t **p, uint32_t *len) {
    if (i >= b->nblocks) return -1;
    uint32_t a = be32(b->blocks + 8 * i), l = be32(b->blocks + 8 * i + 4);
    if ((size_t)a + l > b->size) return -1;
    *p = b->f + a; *len = l;
    return 0;
}
static int bom_var(const struct bom *b, const char *name, uint32_t *idx) {
    uint32_t n = be32(b->vars); const uint8_t *p = b->vars + 4;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t bi = be32(p); uint8_t l = p[4];
        if (l == strlen(name) && !memcmp(p + 5, name, l)) { *idx = bi; return 0; }
        p += 5 + l;
    }
    return -1;
}
typedef void (*tree_fn)(void *ctx, const uint8_t *key, uint32_t klen, const uint8_t *val, uint32_t vlen);
/* B+ tree: index nodes do not list every child, so the entries are read from the
 * leaves, starting at the leftmost one and following the "forward" links */
static void tree_walk(const struct bom *b, const char *name, tree_fn fn, void *ctx) {
    uint32_t idx; const uint8_t *t; uint32_t len;
    if (bom_var(b, name, &idx) || bom_block(b, idx, &t, &len) || len < 21 || memcmp(t, "tree", 4)) return;
    uint32_t node = be32(t + 8);
    const uint8_t *p; uint32_t plen;
    for (int depth = 0; ; depth++) {
        if (depth > 64 || bom_block(b, node, &p, &plen) || plen < 12) return;
        if (be16(p)) break;
        if (!be16(p + 2) || plen < 16) return;
        node = be32(p + 12);   /* first child */
    }
    for (uint32_t guard = 0; node && guard < 1000000; guard++) {
        if (bom_block(b, node, &p, &plen) || plen < 12) return;
        uint16_t cnt = be16(p + 2);
        for (uint32_t i = 0; i < cnt && 12 + 8 * (i + 1) <= plen; i++) {
            const uint8_t *kp, *vp; uint32_t kl, vl;
            if (bom_block(b, be32(p + 16 + 8 * i), &kp, &kl) || bom_block(b, be32(p + 12 + 8 * i), &vp, &vl)) continue;
            fn(ctx, kp, kl, vp, vl);
        }
        node = be32(p + 4);
    }
}

/* ---------------------------------------------------------------- loading */
static void add_facet(void *ctx, const uint8_t *k, uint32_t kl, const uint8_t *v, uint32_t vl) {
    struct catalog *c = ctx;
    if (vl < 6) return;
    uint16_t n = le16(v + 4), ident = 0;
    for (uint16_t i = 0; i < n && 6 + 4 * (i + 1) <= vl; i++) if (le16(v + 6 + 4 * i) == A_IDENTIFIER) ident = le16(v + 8 + 4 * i);
    while (kl && !k[kl - 1]) kl--;
    if ((c->nfacets & 255) == 0) c->facets = realloc(c->facets, (c->nfacets + 256) * sizeof *c->facets);
    c->facets[c->nfacets++] = (struct facet){ (const char *)k, kl, ident };
}
static size_t ident_slot;
static void add_rendition(void *ctx, const uint8_t *k, uint32_t kl, const uint8_t *v, uint32_t vl) {
    struct catalog *c = ctx;
    if (kl < c->nattrs * 2 || vl < 184 || memcmp(v, "ISTC", 4)) return;
    if ((c->nrends & 1023) == 0) c->rends = realloc(c->rends, (c->nrends + 1024) * sizeof *c->rends);
    c->rends[c->nrends++] = (struct rendition){ (const uint16_t *)(const void *)k, v, vl, le16(k + 2 * ident_slot) };
}
static int cmp_facet(const void *a, const void *b) {
    const struct facet *x = a, *y = b;
    size_t n = x->len < y->len ? x->len : y->len;
    int r = strncasecmp(x->name, y->name, n);
    return r ? r : (x->len > y->len) - (x->len < y->len);
}
static int cmp_rend(const void *a, const void *b) { const struct rendition *x = a, *y = b; return (x->ident > y->ident) - (x->ident < y->ident); }

static struct catalog *catalog_open(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) || st.st_size < 64) { close(fd); return NULL; }
    const uint8_t *m = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return NULL;
    struct bom b = { m, (size_t)st.st_size, 0, NULL, NULL };
    if (memcmp(m, "BOMStore", 8)) goto bad;
    uint32_t idxoff = be32(m + 16), varoff = be32(m + 24);
    if ((size_t)idxoff + 4 > b.size || (size_t)varoff + 4 > b.size) goto bad;
    b.nblocks = be32(m + idxoff); b.blocks = m + idxoff + 4; b.vars = m + varoff;
    if ((size_t)idxoff + 4 + 8ull * b.nblocks > b.size) goto bad;
    struct catalog *c = rt_xcalloc(1, sizeof *c);
    c->path = rt_xstrdup(path); c->map = m; c->size = b.size;
    uint32_t kfi; const uint8_t *kf; uint32_t kfl;
    if (bom_var(&b, "KEYFORMAT", &kfi) || bom_block(&b, kfi, &kf, &kfl) || kfl < 12) { free(c); goto bad; }
    c->nattrs = le32(kf + 8);
    if (c->nattrs > MAX_ATTRS || 12 + 4 * c->nattrs > kfl) { free(c); goto bad; }
    ident_slot = 0;
    for (uint32_t i = 0; i < c->nattrs; i++) { c->attrs[i] = le32(kf + 12 + 4 * i); if (c->attrs[i] == A_IDENTIFIER) ident_slot = i; }
    tree_walk(&b, "FACETKEYS", add_facet, c);
    tree_walk(&b, "RENDITIONS", add_rendition, c);
    qsort(c->facets, c->nfacets, sizeof *c->facets, cmp_facet);
    qsort(c->rends, c->nrends, sizeof *c->rends, cmp_rend);
    rt_log("asset catalog %s: %zu names, %zu renditions", path, c->nfacets, c->nrends);
    return c;
bad:
    munmap((void *)m, (size_t)st.st_size);
    return NULL;
}

static uint16_t key_attr(const struct catalog *c, const struct rendition *r, uint32_t attr) {
    for (uint32_t i = 0; i < c->nattrs; i++) if (c->attrs[i] == attr) { uint16_t v; memcpy(&v, (const uint8_t *)r->key + 2 * i, 2); return v; }
    return 0;
}
static const struct facet *find_facet(const struct catalog *c, const char *name) {
    struct facet k = { name, strlen(name), 0 };
    return bsearch(&k, c->facets, c->nfacets, sizeof *c->facets, cmp_facet);
}

/* ------------------------------------------------------------- decoding */
struct csi { uint32_t w, h, scale, pf; uint16_t layout; const uint8_t *tlv; uint32_t tlvlen; const uint8_t *data; size_t datalen; };
static int csi_parse(const struct rendition *r, struct csi *s) {
    const uint8_t *v = r->csi;
    s->w = le32(v + 12); s->h = le32(v + 16); s->scale = le32(v + 20); s->pf = le32(v + 24); s->layout = le16(v + 36);
    s->tlvlen = le32(v + 168);
    if (184 + (size_t)s->tlvlen > r->csi_len) return -1;
    s->tlv = v + 184; s->data = v + 184 + s->tlvlen; s->datalen = r->csi_len - 184 - s->tlvlen;
    return 0;
}
static const uint8_t *tlv_find(const struct csi *s, uint32_t type, uint32_t *len) {
    for (uint32_t p = 0; p + 8 <= s->tlvlen; ) {
        uint32_t t = le32(s->tlv + p), l = le32(s->tlv + p + 4);
        if (p + 8 + l > s->tlvlen) break;
        if (t == type) { *len = l; return s->tlv + p + 8; }
        p += 8 + l;
    }
    return NULL;
}
#define PF_ARGB 0x41524742u
#define PF_GA8  0x47413820u
#define PF_JPEG 0x4a504547u
static struct gfx_image *decode_bitmap(const struct csi *s) {
    if (s->datalen >= 12 && !memcmp(s->data, "DWAR", 4)) {   /* raw data: JPEG (or PNG) bytes */
        uint32_t l = le32(s->data + 8);
        if (12 + (size_t)l > s->datalen) return NULL;
        return gfx_image_decode(s->data + 12, l);
    }
    if (s->datalen < 16 || memcmp(s->data, "MLEC", 4)) return NULL;
    uint32_t ver = le32(s->data + 4), comp = le32(s->data + 8), f4 = le32(s->data + 12);
    int bpp = s->pf == PF_GA8 ? 2 : s->pf == PF_ARGB ? 4 : 0;
    if (!bpp || !s->w || !s->h || s->w > 16384 || s->h > 16384) return NULL;
    uint32_t tl; const uint8_t *t = tlv_find(s, 1007, &tl);
    size_t bpr = t && tl >= 4 ? le32(t) : (size_t)s->w * (size_t)bpp;
    if (bpr < (size_t)s->w * (size_t)bpp) return NULL;
    size_t total = bpr * s->h;
    uint8_t *raw = malloc(total + 64);
    if (!raw) return NULL;
    size_t got = 0;
    const uint8_t *p = s->data + 16, *end = s->data + s->datalen;
    int nchunks = ver == 0 ? 1 : (int)f4;
    for (int i = 0; i < nchunks && got < total; i++) {
        const uint8_t *src; size_t n;
        if (ver == 0) { src = p; n = f4; }
        else {
            if (p + 20 > end || memcmp(p, "KCBC", 4)) break;
            n = le32(p + 16); src = p + 20;
        }
        if (src + n > end) break;
        long r;
        if (comp == 0) { r = (long)(n < total - got ? n : total - got); memcpy(raw + got, src, (size_t)r); }
        else r = rt_lzfse_decode(src, n, raw + got, total - got);
        if (r < 0) { rt_log("asset: cannot decompress (codec %u)", comp); free(raw); return NULL; }
        got += (size_t)r; p = src + n;
    }
    if (got < total) memset(raw + got, 0, total - got);
    struct gfx_image *g = gfx_image_new((int)s->w, (int)s->h);
    for (uint32_t y = 0; y < s->h; y++) {
        const uint8_t *row = raw + bpr * y; uint32_t *out = g->px + (size_t)y * s->w;
        if (bpp == 4) for (uint32_t x = 0; x < s->w; x++) { const uint8_t *q = row + 4 * x; out[x] = (uint32_t)q[2] | (uint32_t)q[1] << 8 | (uint32_t)q[0] << 16 | (uint32_t)q[3] << 24; }
        else for (uint32_t x = 0; x < s->w; x++) { uint32_t gr = row[2 * x], a = row[2 * x + 1]; out[x] = gr | gr << 8 | gr << 16 | a << 24; }
    }
    free(raw);
    return g;
}
/* a rendition whose key has exactly these attribute values (others 0) */
static const struct rendition *find_by_attrs(const struct catalog *c, const uint16_t *want) {
    for (size_t i = 0; i < c->nrends; i++) {
        const struct rendition *r = &c->rends[i]; int ok = 1;
        for (uint32_t a = 0; a < c->nattrs && ok; a++) { uint16_t v; memcpy(&v, (const uint8_t *)r->key + 2 * a, 2); ok = v == want[a]; }
        if (ok) return r;
    }
    return NULL;
}
static struct gfx_image *atlas_image(struct catalog *c, const struct rendition *r) {
    size_t idx = (size_t)(r - c->rends);
    for (int i = 0; i < c->natlas; i++) if (c->atlas[i].idx == idx) return c->atlas[i].img;
    struct csi s;
    if (csi_parse(r, &s)) return NULL;
    struct gfx_image *g = decode_bitmap(&s);
    if (!g) return NULL;
    if (c->natlas == 48) { gfx_image_release(c->atlas[0].img); memmove(c->atlas, c->atlas + 1, sizeof c->atlas[0] * 47); c->natlas--; }
    c->atlas[c->natlas].idx = idx; c->atlas[c->natlas++].img = g;
    return g;
}
static struct gfx_image *decode_rendition(struct catalog *c, const struct rendition *r) {
    struct csi s;
    if (csi_parse(r, &s)) return NULL;
    if (s.layout != 1003) return decode_bitmap(&s);
    /* internal link: KLNI, 0, frame (x, y, w, h), layout (u16), key length (u32), (attribute, value) pairs */
    uint32_t l; const uint8_t *t = tlv_find(&s, 1010, &l);
    if (!t || l < 30 || memcmp(t, "KLNI", 4)) return NULL;
    uint32_t fx = le32(t + 8), fy = le32(t + 12), fw = le32(t + 16), fh = le32(t + 20);
    uint16_t want[MAX_ATTRS] = { 0 };
    for (uint32_t p = 30; p + 4 <= l; p += 4) {
        uint16_t a = le16(t + p), v = le16(t + p + 2);
        if (!a && !v) break;
        for (uint32_t i = 0; i < c->nattrs; i++) if (c->attrs[i] == a) want[i] = v;
    }
    const struct rendition *ar = find_by_attrs(c, want);
    struct gfx_image *atlas = ar ? atlas_image(c, ar) : NULL;
    if (!atlas || fx + fw > (uint32_t)atlas->w || fy + fh > (uint32_t)atlas->h || !fw || !fh) return NULL;
    struct gfx_image *g = gfx_image_new((int)fw, (int)fh);
    for (uint32_t y = 0; y < fh; y++) memcpy(g->px + (size_t)y * fw, atlas->px + (size_t)(fy + y) * atlas->w + fx, fw * 4);
    return g;
}

/* best rendition for the device: screen scale, then phone, then universal, plain variants */
static const struct rendition *choose(const struct catalog *c, uint16_t ident) {
    struct rendition k = { 0 }; k.ident = ident;
    const struct rendition *r = bsearch(&k, c->rends, c->nrends, sizeof *c->rends, cmp_rend);
    if (!r) return NULL;
    while (r > c->rends && r[-1].ident == ident) r--;
    int want = rt_scr_scale >= 2.5f ? 3 : rt_scr_scale >= 1.5f ? 2 : 1;
    const struct rendition *best = NULL; long best_score = -1000000;
    for (; r < c->rends + c->nrends && r->ident == ident; r++) {
        struct csi s;
        if (csi_parse(r, &s) || s.layout == 1004) continue;
        long sc = 0;
        int scale = key_attr(c, r, A_SCALE);
        if (scale == want) sc += 1000; else if (scale > want) sc += 800 - 10 * (scale - want); else if (scale) sc += 600 - 10 * (want - scale); else sc += 700;
        int idiom = key_attr(c, r, A_IDIOM);
        sc += idiom == 1 ? 300 : idiom == 0 ? 200 : 0;
        static const uint32_t plain[] = { A_SUBTYPE, A_STATE, A_VALUE, A_DIRECTION, A_SIZECLASS_H, A_SIZECLASS_V, A_MEMORY, A_GRAPHICS, A_DIM1, A_DIM2 };
        for (size_t i = 0; i < sizeof plain / sizeof *plain; i++) if (!key_attr(c, r, plain[i])) sc += 10;
        if (sc > best_score) { best_score = sc; best = r; }
    }
    return best;
}

/* ------------------------------------------------------------ public API */
static struct { char *dir; struct catalog *c; } g_cats[16]; static int g_ncats;
static pthread_mutex_t g_cat_lock = PTHREAD_MUTEX_INITIALIZER;
static struct catalog *catalog_for(const char *dir) {
    for (int i = 0; i < g_ncats; i++) if (!strcmp(g_cats[i].dir, dir)) return g_cats[i].c;
    char p[4200]; snprintf(p, sizeof p, "%s/Assets.car", dir);
    struct catalog *c = catalog_open(p);
    if (g_ncats < 16) { g_cats[g_ncats].dir = rt_xstrdup(dir); g_cats[g_ncats++].c = c; }
    return c;
}
/* "name.png", "name@2x", "name~iphone" -> "name" */
static void base_name(const char *in, char *out, size_t n) {
    snprintf(out, n, "%s", in);
    char *dot = strrchr(out, '.'), *slash = strrchr(out, '/');
    if (dot && (!slash || dot > slash) && (!strcasecmp(dot, ".png") || !strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg"))) *dot = 0;
    char *t = strrchr(out, '~'); if (t && (!strcmp(t, "~iphone") || !strcmp(t, "~ipad"))) *t = 0;
    char *at = strrchr(out, '@'); if (at && (at[1] == '2' || at[1] == '3') && at[2] == 'x' && !at[3]) *at = 0;
}
/* autoreleased UIImage for `name` from the bundle's Assets.car, or nil */
id rt_asset_image(id bundle, id name) {
    if (!name) return NULL;
    if (!bundle) bundle = RT_SEND0(id, (id)rt_class_named("NSBundle"), "mainBundle");
    const char *dir = rt_str_utf8(RT_SEND0(id, bundle, "bundlePath"));
    char nm[512]; base_name(rt_str_utf8(name), nm, sizeof nm);
    pthread_mutex_lock(&g_cat_lock);
    struct catalog *c = catalog_for(dir);
    const struct facet *f = c ? find_facet(c, nm) : NULL;
    const struct rendition *r = f ? choose(c, f->ident) : NULL;
    struct gfx_image *g = r ? decode_rendition(c, r) : NULL;
    double scale = 1;
    if (g) { struct csi s; csi_parse(r, &s); scale = s.scale ? s.scale / 100.0 : 1; }
    pthread_mutex_unlock(&g_cat_lock);
    if (!g) return NULL;
    id cg = rt_cgimage_new(g); gfx_image_release(g);
    id ui = rt_uiimage_from_cg(cg, scale); objc_release(cg);
    return ui;
}
