/* CSS for the UIWebView engine: stylesheet parsing (rules, @media with
 * device/viewport features, nested at-rules skipped), the cascade (UA sheet,
 * author sheets, style attributes, !important) and computed values. */
#include "webengine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

struct decl { char prop[40]; char *val; int important; };
struct rule { struct wsel *sel; struct decl *d; int nd; long order; int origin; char *media; };
struct css { struct rule *r; size_t n, cap; long order; };

static int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static char *trim(char *s) { while (is_space(*s)) s++; size_t n = strlen(s); while (n && is_space(s[n - 1])) s[--n] = 0; return s; }

static const char UA_SHEET[] =
    "html,body,div,section,article,header,footer,nav,main,aside,p,h1,h2,h3,h4,h5,h6,ul,ol,blockquote,pre,dl,dt,dd,figure,figcaption,"
    "form,center,address,fieldset,menu,hgroup,details,summary,table,tbody,thead,tfoot,tr,caption,hr,legend,dir{display:block}"
    "li{display:list-item}td,th{display:inline-block;padding:1px}"
    "head,script,style,title,meta,link,template,noscript,base,param,datalist,area,map{display:none}"
    "body{margin:8px}p,blockquote,ul,ol,dl,pre,figure{margin:1em 0}ul ul,ul ol,ol ol,ol ul{margin:0}"
    "h1{font-size:2em;margin:.67em 0;font-weight:bold}h2{font-size:1.5em;margin:.83em 0;font-weight:bold}"
    "h3{font-size:1.17em;margin:1em 0;font-weight:bold}h4{margin:1.33em 0;font-weight:bold}"
    "h5{font-size:.83em;margin:1.67em 0;font-weight:bold}h6{font-size:.67em;margin:2.33em 0;font-weight:bold}"
    "ul,ol,blockquote,dd{padding-left:30px}ol{list-style-type:decimal}pre{white-space:pre}"
    "b,strong,th{font-weight:bold}i,em,cite,var,dfn{font-style:italic}small,sub,sup{font-size:.83em}big{font-size:1.2em}"
    "a{color:rgb(0,102,237)}center{text-align:center}hr{margin:8px 0}"
    "button{display:inline-block;border:1px solid #999;border-radius:4px;padding:2px 8px;background-color:#eee;color:#000}"
    "input,select,textarea{display:inline-block;border:1px solid #999;padding:2px;background-color:#fff;color:#000}"
    "input{width:150px;height:18px}textarea{width:200px;height:40px}input[type=hidden]{display:none}"
    "iframe{display:inline-block}";

struct css *css_new(void) {
    struct css *c = calloc(1, sizeof *c);
    css_add_sheet(c, UA_SHEET, NULL);
    for (size_t i = 0; i < c->n; i++) c->r[i].origin = 0;
    return c;
}
void css_free(struct css *c) {
    for (size_t i = 0; i < c->n; i++) { sel_free(c->r[i].sel); for (int k = 0; k < c->r[i].nd; k++) free(c->r[i].d[k].val); free(c->r[i].d); free(c->r[i].media); }
    free(c->r); free(c);
}

/* "a:b; c:d !important" -> decls */
static int parse_decls(const char *text, struct decl **out) {
    int n = 0, cap = 0; struct decl *d = NULL;
    const char *p = text;
    while (*p) {
        while (is_space(*p) || *p == ';') p++;
        if (!*p) break;
        const char *colon = p; while (*colon && *colon != ':' && *colon != ';') colon++;
        if (*colon != ':') { p = *colon ? colon + 1 : colon; continue; }
        /* the value ends at ';' outside parentheses and quotes */
        const char *e = colon + 1; int depth = 0; char q = 0;
        while (*e) { if (q) { if (*e == q) q = 0; } else if (*e == '"' || *e == '\'') q = *e; else if (*e == '(') depth++; else if (*e == ')') depth--; else if (*e == ';' && depth <= 0) break; e++; }
        size_t pn = (size_t)(colon - p); while (pn && is_space(p[pn - 1])) pn--;
        if (pn && pn < 40) {
            if (n == cap) { cap = cap ? cap * 2 : 8; d = realloc(d, (size_t)cap * sizeof *d); }
            memset(&d[n], 0, sizeof d[n]);
            for (size_t i = 0; i < pn; i++) d[n].prop[i] = (char)tolower((unsigned char)p[i]);
            char *v = strndup(colon + 1, (size_t)(e - colon - 1)); char *t = trim(v);
            char *imp = strstr(t, "!important"); if (!imp) imp = strstr(t, "! important");
            if (imp) { d[n].important = 1; *imp = 0; t = trim(t); }
            d[n].val = strdup(t); free(v);
            n++;
        }
        p = *e ? e + 1 : e;
    }
    *out = d;
    return n;
}

/* finds the matching close brace of the block starting at '{' */
static const char *block_end(const char *ob) {
    int depth = 0; char q = 0;
    for (const char *p = ob; *p; p++) {
        if (q) { if (*p == q) q = 0; continue; }
        if (*p == '"' || *p == '\'') q = *p;
        else if (*p == '{') depth++;
        else if (*p == '}' && --depth == 0) return p;
    }
    return ob + strlen(ob);
}
char *rt_url_resolve(const char *base, const char *ref);
/* url(x) -> url("absolute") against the stylesheet's URL */
static char *absolutize(const char *v, const char *base) {
    if (!base || !strstr(v, "url(")) return strdup(v);
    size_t cap = strlen(v) * 2 + 256; char *out = malloc(cap); size_t o = 0;
    for (const char *p = v; *p;) {
        if (!strncmp(p, "url(", 4)) {
            const char *u = p + 4; while (is_space(*u)) u++;
            char q = *u == '"' || *u == '\'' ? *u++ : 0;
            const char *e = u; while (*e && (q ? *e != q : *e != ')')) e++;
            char *ref = strndup(u, (size_t)(e - u)), *abs = rt_url_resolve(base, ref);
            size_t need = o + strlen(abs) + 16; if (need > cap) { cap = need * 2; out = realloc(out, cap); }
            o += (size_t)sprintf(out + o, "url(\"%s\")", abs);
            free(ref); free(abs);
            if (q && *e) e++;
            while (*e && *e != ')') e++;
            p = *e ? e + 1 : e;
            continue;
        }
        if (o + 2 > cap) { cap *= 2; out = realloc(out, cap); }
        out[o++] = *p++;
    }
    out[o] = 0;
    return out;
}
static const char *g_sheet_base;
static void add_rules(struct css *c, const char *text, const char *media) {
    const char *p = text;
    while (*p) {
        while (is_space(*p)) p++;
        if (!*p) break;
        if (*p == '}') { p++; continue; }
        const char *ob = p; while (*ob && *ob != '{' && *ob != ';') ob++;
        if (!*ob) break;
        if (*ob == ';') { p = ob + 1; continue; }      /* @import, @charset ... */
        const char *cb = block_end(ob);
        char *prelude = strndup(p, (size_t)(ob - p)); char *pre = trim(prelude);
        if (*pre == '@') {
            if (!strncmp(pre, "@media", 6)) {
                char *inner = strndup(ob + 1, (size_t)(cb - ob - 1));
                char *mq = trim(pre + 6);
                char *combined = media ? NULL : strdup(mq);
                if (media) { size_t l = strlen(media) + strlen(mq) + 8; combined = malloc(l); snprintf(combined, l, "%s and %s", media, mq); }
                add_rules(c, inner, combined);
                free(combined); free(inner);
            }
            /* @font-face, @keyframes, @supports, @page: skipped */
        } else if (*pre) {
            int ok; struct wsel *s = sel_parse(pre, &ok);
            if (sel_count(s)) {
                char *body = strndup(ob + 1, (size_t)(cb - ob - 1));
                if (c->n == c->cap) { c->cap = c->cap ? c->cap * 2 : 64; c->r = realloc(c->r, c->cap * sizeof *c->r); }
                struct rule *r = &c->r[c->n++]; memset(r, 0, sizeof *r);
                r->sel = s; r->nd = parse_decls(body, &r->d); r->order = c->order++; r->origin = 1; r->media = media ? strdup(media) : NULL;
                for (int k = 0; k < r->nd; k++) if (strstr(r->d[k].val, "url(")) { char *a = absolutize(r->d[k].val, g_sheet_base); free(r->d[k].val); r->d[k].val = a; }
                free(body);
            } else sel_free(s);
        }
        free(prelude);
        p = *cb ? cb + 1 : cb;
    }
}
void css_add_sheet(struct css *c, const char *text, const char *base) {
    g_sheet_base = base;
    /* strip comments */
    size_t n = strlen(text); char *t = malloc(n + 1), *o = t;
    for (const char *p = text; *p;) { if (p[0] == '/' && p[1] == '*') { const char *e = strstr(p + 2, "*/"); p = e ? e + 2 : p + strlen(p); } else *o++ = *p++; }
    *o = 0;
    add_rules(c, t, NULL);
    free(t); g_sheet_base = NULL;
}

/* ------------------------------------------------------------------ media queries */
static int mq_feature(struct wdocument *d, const char *f) {
    char name[48] = { 0 }, val[48] = { 0 };
    const char *colon = strchr(f, ':');
    size_t nl = colon ? (size_t)(colon - f) : strlen(f); if (nl > 47) nl = 47;
    memcpy(name, f, nl);
    if (colon) snprintf(val, sizeof val, "%s", colon + 1);
    char *nm = trim(name), *v = trim(val);
    if (!strncmp(nm, "-webkit-", 8)) nm += 8;
    int mode = 0;
    if (!strncmp(nm, "min-", 4)) { mode = 1; nm += 4; } else if (!strncmp(nm, "max-", 4)) { mode = 2; nm += 4; }
    double sw = d->screen_w > 0 ? d->screen_w : 320, sh = d->screen_h > 0 ? d->screen_h : 480;
    int land = d->orientation_landscape;
    if ((land && sw < sh) || (!land && sw > sh)) { double t = sw; sw = sh; sh = t; }
    double vw = d->vw > 0 ? d->vw : sw, vh = d->vh > 0 ? d->vh : sh;
    if (!strcmp(nm, "orientation")) return !colon || !strcmp(v, land ? "landscape" : "portrait");
    double have;
    if (!strcmp(nm, "width")) have = vw; else if (!strcmp(nm, "height")) have = vh;
    else if (!strcmp(nm, "device-width")) have = sw; else if (!strcmp(nm, "device-height")) have = sh;
    else if (!strcmp(nm, "device-pixel-ratio") || !strcmp(nm, "pixel-ratio")) have = 2;
    else if (!strcmp(nm, "resolution")) have = strstr(v, "dppx") ? 2 : 192;
    else if (!strcmp(nm, "color")) have = 8;
    else return 0;
    if (!colon) return have > 0;
    double want = strtod(v, NULL);
    if (strstr(v, "em")) want *= 16;
    return mode == 1 ? have >= want : mode == 2 ? have <= want : fabs(have - want) < 0.5;
}
static int mq_one(struct wdocument *d, const char *q) {
    char buf[256]; snprintf(buf, sizeof buf, "%s", q); char *s = trim(buf);
    int neg = 0;
    if (!strncmp(s, "only ", 5)) s += 5;
    if (!strncmp(s, "not ", 4)) { neg = 1; s += 4; }
    int ok = 1;
    for (char *p = s; *p && ok;) {
        while (is_space(*p)) p++;
        if (!strncmp(p, "and", 3) && (is_space(p[3]) || p[3] == '(')) { p += 3; continue; }
        if (*p == '(') {
            char *e = strchr(p, ')'); if (!e) { ok = 0; break; }
            *e = 0; ok = mq_feature(d, p + 1); p = e + 1;
        } else {
            size_t k = strcspn(p, " \t(");
            char t[24] = { 0 }; memcpy(t, p, k < 23 ? k : 23);
            if (strcmp(t, "all") && strcmp(t, "screen") && strcmp(t, "handheld")) ok = 0;
            p += k;
        }
    }
    return neg ? !ok : ok;
}
static int media_ok(struct wdocument *d, const char *m) {
    if (!m) return 1;
    /* "A and B" combined from nested @media: every comma list must hold; within one, any alternative */
    char *copy = strdup(m); int res = 0;
    char *save; for (char *alt = strtok_r(copy, ",", &save); alt; alt = strtok_r(NULL, ",", &save)) if (mq_one(d, alt)) { res = 1; break; }
    free(copy);
    return res;
}

/* ------------------------------------------------------------------ values */
int css_parse_color(const char *v, float out[4]) {
    while (is_space(*v)) v++;
    if (*v == '#') {
        unsigned x = (unsigned)strtoul(v + 1, NULL, 16); size_t n = strspn(v + 1, "0123456789abcdefABCDEF");
        if (n == 3) { out[0] = ((x >> 8) & 15) / 15.f; out[1] = ((x >> 4) & 15) / 15.f; out[2] = (x & 15) / 15.f; out[3] = 1; return 1; }
        if (n == 4) { out[0] = ((x >> 12) & 15) / 15.f; out[1] = ((x >> 8) & 15) / 15.f; out[2] = ((x >> 4) & 15) / 15.f; out[3] = (x & 15) / 15.f; return 1; }
        if (n == 6) { out[0] = ((x >> 16) & 255) / 255.f; out[1] = ((x >> 8) & 255) / 255.f; out[2] = (x & 255) / 255.f; out[3] = 1; return 1; }
        if (n == 8) { out[0] = ((x >> 24) & 255) / 255.f; out[1] = ((x >> 16) & 255) / 255.f; out[2] = ((x >> 8) & 255) / 255.f; out[3] = (x & 255) / 255.f; return 1; }
        return 0;
    }
    if (!strncmp(v, "rgb", 3) || !strncmp(v, "hsl", 3)) {
        int hsl = v[0] == 'h';
        const char *p = strchr(v, '('); if (!p) return 0;
        double c[4] = { 0, 0, 0, 1 }; int k = 0; p++;
        while (k < 4 && *p && *p != ')') {
            char *e; c[k] = strtod(p, &e); if (e == p) break;
            if (*e == '%') { c[k] *= hsl ? 0.01 : (k < 3 ? 2.55 : 0.01); e++; }
            k++; p = e; while (*p == ',' || *p == '/' || is_space(*p)) p++;
        }
        if (k < 3) return 0;
        if (hsl) {
            double h = fmod(c[0], 360) / 360, s = c[1], l = c[2];
            double q = l < .5 ? l * (1 + s) : l + s - l * s, pp = 2 * l - q;
            double t[3] = { h + 1 / 3.0, h, h - 1 / 3.0 };
            for (int i = 0; i < 3; i++) { double x = t[i]; if (x < 0) x += 1; if (x > 1) x -= 1;
                out[i] = (float)(x < 1 / 6.0 ? pp + (q - pp) * 6 * x : x < .5 ? q : x < 2 / 3.0 ? pp + (q - pp) * (2 / 3.0 - x) * 6 : pp); }
        } else { out[0] = (float)(c[0] / 255); out[1] = (float)(c[1] / 255); out[2] = (float)(c[2] / 255); }
        out[3] = (float)c[3];
        return 1;
    }
    static const struct { const char *n; unsigned rgb; } named[] = {
        { "white", 0xffffff }, { "black", 0 }, { "gray", 0x808080 }, { "grey", 0x808080 }, { "silver", 0xc0c0c0 }, { "red", 0xff0000 },
        { "green", 0x008000 }, { "blue", 0x0000ff }, { "yellow", 0xffff00 }, { "orange", 0xffa500 }, { "purple", 0x800080 }, { "navy", 0x000080 },
        { "maroon", 0x800000 }, { "olive", 0x808000 }, { "teal", 0x008080 }, { "aqua", 0x00ffff }, { "cyan", 0x00ffff }, { "fuchsia", 0xff00ff },
        { "magenta", 0xff00ff }, { "lime", 0x00ff00 }, { "pink", 0xffc0cb }, { "brown", 0xa52a2a }, { "gold", 0xffd700 }, { "darkgray", 0xa9a9a9 },
        { "darkgrey", 0xa9a9a9 }, { "lightgray", 0xd3d3d3 }, { "lightgrey", 0xd3d3d3 }, { "dimgray", 0x696969 }, { "whitesmoke", 0xf5f5f5 },
        { "darkblue", 0x00008b }, { "darkred", 0x8b0000 }, { "darkgreen", 0x006400 }, { "lightblue", 0xadd8e6 }, { "skyblue", 0x87ceeb },
        { "steelblue", 0x4682b4 }, { "indigo", 0x4b0082 }, { "violet", 0xee82ee }, { "beige", 0xf5f5dc }, { "ivory", 0xfffff0 }, { "khaki", 0xf0e68c },
        { "coral", 0xff7f50 }, { "salmon", 0xfa8072 }, { "tomato", 0xff6347 }, { "crimson", 0xdc143c }, { "chocolate", 0xd2691e }, { "tan", 0xd2b48c },
    };
    char w[32]; size_t n = strcspn(v, " ;!\t\n,)"); if (n >= sizeof w) return 0; memcpy(w, v, n); w[n] = 0;
    for (char *q = w; *q; q++) *q = (char)tolower((unsigned char)*q);
    if (!strcmp(w, "transparent")) { out[0] = out[1] = out[2] = out[3] = 0; return 1; }
    for (size_t i = 0; i < sizeof named / sizeof named[0]; i++) if (!strcmp(w, named[i].n)) {
        out[0] = (named[i].rgb >> 16 & 255) / 255.f; out[1] = (named[i].rgb >> 8 & 255) / 255.f; out[2] = (named[i].rgb & 255) / 255.f; out[3] = 1; return 1; }
    return 0;
}
static struct wlen parse_len(const char *v, int *ok) {
    struct wlen l = { 0, 0 }; *ok = 0;
    while (is_space(*v)) v++;
    if (!strncmp(v, "auto", 4)) { *ok = 1; return l; }
    char *e; double x = strtod(v, &e);
    if (e == v) return l;
    *ok = 1; l.v = x;
    if (!strncmp(e, "px", 2)) l.u = 'x';
    else if (!strncmp(e, "rem", 3)) { l.u = 'x'; l.v = x * 16; }
    else if (!strncmp(e, "em", 2)) l.u = 'e';
    else if (!strncmp(e, "ex", 2)) { l.u = 'e'; l.v = x * 0.5; }
    else if (*e == '%') l.u = '%';
    else if (!strncmp(e, "pt", 2)) { l.u = 'x'; l.v = x * 4 / 3; }
    else if (!strncmp(e, "vw", 2) || !strncmp(e, "vh", 2) || !strncmp(e, "vmin", 4) || !strncmp(e, "vmax", 4)) { l.u = e[1] == 'w' ? 'w' : 'h'; }
    else if (!strncmp(e, "cm", 2)) { l.u = 'x'; l.v = x * 37.8; }
    else if (!strncmp(e, "mm", 2)) { l.u = 'x'; l.v = x * 3.78; }
    else if (!strncmp(e, "in", 2)) { l.u = 'x'; l.v = x * 96; }
    else l.u = 'x';   /* unitless */
    return l;
}
/* splits a value into up to n whitespace-separated tokens (parentheses kept together) */
static int tokens(const char *v, char out[][64], int n) {
    int k = 0; const char *p = v;
    while (*p && k < n) {
        while (is_space(*p)) p++;
        if (!*p) break;
        const char *s = p; int depth = 0;
        while (*p && (depth || !is_space(*p))) { if (*p == '(') depth++; else if (*p == ')') depth--; p++; }
        size_t l = (size_t)(p - s); if (l > 63) l = 63;
        memcpy(out[k], s, l); out[k][l] = 0; k++;
    }
    return k;
}
static void four(struct wlen dst[4], const char *v) {
    char t[4][64]; int n = tokens(v, t, 4), ok;
    struct wlen x[4];
    for (int i = 0; i < n; i++) { x[i] = parse_len(t[i], &ok); if (!strcmp(t[i], "auto")) x[i].u = 'A'; }
    if (n == 1) x[1] = x[2] = x[3] = x[0];
    else if (n == 2) { x[2] = x[0]; x[3] = x[1]; }
    else if (n == 3) x[3] = x[1];
    if (n) for (int i = 0; i < 4; i++) dst[i] = x[i];
}
static double border_width(const char *t, int *ok) {
    if (!strcmp(t, "thin")) { *ok = 1; return 1; }
    if (!strcmp(t, "medium")) { *ok = 1; return 3; }
    if (!strcmp(t, "thick")) { *ok = 1; return 5; }
    struct wlen l = parse_len(t, ok);
    return *ok && l.u ? l.v : (*ok = 0, 0);
}
static int is_style_kw(const char *t) {
    static const char *k[] = { "none", "hidden", "solid", "dashed", "dotted", "double", "groove", "ridge", "inset", "outset", NULL };
    for (int i = 0; k[i]; i++) if (!strcmp(t, k[i])) return 1;
    return 0;
}
/* "1px solid #000" onto side s (0..3), -1 = all */
static void border_short(struct cstyle *st, const char *v, int side, double em) {
    char t[4][64]; int n = tokens(v, t, 4);
    double w = 3; int haswidth = 0, none = 0; float col[4]; int hascol = 0;
    for (int i = 0; i < n; i++) {
        int ok; double x = border_width(t[i], &ok);
        if (ok) { w = x; haswidth = 1; continue; }
        if (is_style_kw(t[i])) { if (!strcmp(t[i], "none") || !strcmp(t[i], "hidden")) none = 1; continue; }
        if (css_parse_color(t[i], col)) hascol = 1;
    }
    (void)haswidth; (void)em;
    for (int s = 0; s < 4; s++) if (side < 0 || side == s) {
        st->border[s] = none ? 0 : w;
        if (hascol) { memcpy(st->bcol[s], col, sizeof col); st->bcol_cur[s] = 0; } else st->bcol_cur[s] = 1;
    }
}
/* -webkit-gradient(linear, ..., from(c), to(c)) / linear-gradient(c1, c2) -> two colors */
static int parse_gradient(const char *v, float g[2][4]) {
    const char *f = strstr(v, "from("), *t = strstr(v, "to(");
    if (f && t) return css_parse_color(f + 5, g[0]) && css_parse_color(t + 3, g[1]);
    const char *lg = strstr(v, "linear-gradient(");
    if (!lg) return 0;
    const char *p = lg + 16; int n = 0;
    while (*p && n < 2) {
        while (is_space(*p) || *p == ',') p++;
        if (css_parse_color(p, g[n])) n++;
        int depth = 0; while (*p && (depth || (*p != ',' && *p != ')'))) { if (*p == '(') depth++; else if (*p == ')') depth--; p++; }
        if (*p == ')') break;
    }
    return n == 2;
}
static char *parse_url(const char *v) {
    const char *u = strstr(v, "url("); if (!u) return NULL;
    u += 4; while (is_space(*u)) u++;
    char q = *u == '"' || *u == '\'' ? *u++ : 0;
    const char *e = u; while (*e && (q ? *e != q : *e != ')')) e++;
    char *r = strndup(u, (size_t)(e - u)); return r;
}
static void background(struct cstyle *st, const char *v, int only_color) {
    float c[4];
    if (!only_color) { memset(st->bg, 0, sizeof st->bg); free(st->bgimg); st->bgimg = NULL; st->has_grad = 0; }
    if (!strcmp(v, "none")) return;
    if (!only_color) {
        char *u = parse_url(v); if (u) { free(st->bgimg); st->bgimg = u; }
        if (strstr(v, "gradient(")) st->has_grad = parse_gradient(v, st->grad);
    }
    /* the color is the token that parses as one, outside url()/gradient() */
    char t[8][64]; int n = tokens(v, t, 8);
    if (!only_color) {
        st->bg_norepeat = 0; st->bgpos[0] = st->bgpos[1] = 0; int npos = 0;
        for (int i = 0; i < n; i++) {
            if (!strcmp(t[i], "no-repeat")) st->bg_norepeat = 1;
            else if (!strcmp(t[i], "repeat-x") || !strcmp(t[i], "repeat-y") || !strcmp(t[i], "repeat")) st->bg_norepeat = 0;
            else if (!strcmp(t[i], "left") || !strcmp(t[i], "top")) { if (!strcmp(t[i], "left")) st->bgpos[0] = 0; else st->bgpos[1] = 0; npos++; }
            else if (!strcmp(t[i], "right")) { st->bgpos[0] = 1; npos++; }
            else if (!strcmp(t[i], "bottom")) { st->bgpos[1] = 1; npos++; }
            else if (!strcmp(t[i], "center")) { if (npos == 0) { st->bgpos[0] = st->bgpos[1] = .5f; } else st->bgpos[1] = .5f; npos++; }
            else if (strchr(t[i], '%') && isdigit((unsigned char)t[i][0])) { st->bgpos[npos < 2 ? npos : 1] = (float)(atof(t[i]) / 100); if (npos == 0) st->bgpos[1] = .5f; npos++; }
        }
    }
    for (int i = 0; i < n; i++) if (!strstr(t[i], "(") || !strncmp(t[i], "rgb", 3) || !strncmp(t[i], "hsl", 3)) { if (css_parse_color(t[i], c)) { memcpy(st->bg, c, sizeof c); return; } }
    if (only_color && css_parse_color(v, c)) memcpy(st->bg, c, sizeof c);
}

static void apply_one(struct cstyle *st, const char *prop, const char *v, const struct cstyle *parent) {
    int ok; double pem = parent ? parent->size : 16;
    #define IS(x) (!strcmp(prop, x))
    if (!strcmp(v, "inherit") && parent) {
        if (IS("color")) memcpy(st->color, parent->color, sizeof st->color);
        else if (IS("font-size")) st->size = parent->size;
        else if (IS("display")) st->display = parent->display;
        else if (IS("position")) st->position = parent->position;
        return;
    }
    if (IS("color")) { if (!strcmp(v, "currentColor") || !strcmp(v, "currentcolor")) return; css_parse_color(v, st->color); }
    else if (IS("font-size")) {
        static const struct { const char *n; double px; } kw[] = { { "xx-small", 9 }, { "x-small", 10 }, { "small", 13 }, { "medium", 16 }, { "large", 18 }, { "x-large", 24 }, { "xx-large", 32 } };
        for (size_t i = 0; i < sizeof kw / sizeof kw[0]; i++) if (!strcmp(v, kw[i].n)) { st->size = kw[i].px; return; }
        if (!strcmp(v, "smaller")) { st->size = pem * 0.83; return; }
        if (!strcmp(v, "larger")) { st->size = pem * 1.2; return; }
        struct wlen l = parse_len(v, &ok);
        if (ok && l.u) st->size = l.u == 'e' ? l.v * pem : l.u == '%' ? l.v * pem / 100 : l.v;
    }
    else if (IS("font-weight")) st->bold = !strncmp(v, "bold", 4) || !strncmp(v, "bolder", 6) || atoi(v) >= 600;
    else if (IS("font-style")) st->italic = !strncmp(v, "italic", 6) || !strncmp(v, "oblique", 7);
    else if (IS("font")) {   /* [style] [weight] size[/line-height] family */
        char t[8][64]; int n = tokens(v, t, 8);
        for (int i = 0; i < n; i++) {
            if (!strcmp(t[i], "bold") || atoi(t[i]) >= 600) st->bold = 1;
            else if (!strcmp(t[i], "normal")) {}
            else if (!strcmp(t[i], "italic")) st->italic = 1;
            else if (isdigit((unsigned char)t[i][0]) || t[i][0] == '.') {
                char *slash = strchr(t[i], '/'); if (slash) { *slash = 0; apply_one(st, "line-height", slash + 1, parent); }
                if (strpbrk(t[i], "px%em") || strchr(t[i], 't')) apply_one(st, "font-size", t[i], parent);
            }
        }
    }
    else if (IS("line-height")) {
        if (!strcmp(v, "normal")) { st->lh.u = 0; return; }
        struct wlen l = parse_len(v, &ok);
        if (!ok || !l.u) return;
        char *e; strtod(v, &e); while (is_space(*e)) e++;
        if (!*e) { st->lh.u = 'n'; st->lh.v = l.v; }
        else if (l.u == 'e') { st->lh.u = 'x'; st->lh.v = l.v * st->size; }
        else if (l.u == '%') { st->lh.u = 'x'; st->lh.v = l.v * st->size / 100; }
        else st->lh = l;
    }
    else if (IS("text-align")) st->align = !strncmp(v, "center", 6) || !strncmp(v, "-webkit-center", 14) ? 1 : !strncmp(v, "right", 5) ? 2 : 0;
    else if (IS("text-transform")) st->upper = !strncmp(v, "uppercase", 9);
    else if (IS("white-space")) { st->pre = !strncmp(v, "pre", 3) && strcmp(v, "pre-line"); st->nowrap = !strcmp(v, "nowrap") || !strcmp(v, "pre"); }
    else if (IS("visibility")) st->vis_hidden = !strcmp(v, "hidden") || !strcmp(v, "collapse");
    else if (IS("pointer-events")) st->no_pointer = !strcmp(v, "none");
    else if (IS("list-style-type") || IS("list-style")) { st->nobullet = !!strstr(v, "none"); st->list_decimal = !!strstr(v, "decimal"); }
    else if (IS("display")) {
        st->display = !strcmp(v, "none") ? D_NONE : !strcmp(v, "inline") ? D_INLINE : !strcmp(v, "inline-block") || !strcmp(v, "inline-flex") || !strcmp(v, "inline-table") ? D_INLINE_BLOCK
                    : !strcmp(v, "list-item") ? D_LIST_ITEM : D_BLOCK;   /* block, flex, table, -webkit-box ... lay out as blocks */
    }
    else if (IS("position")) st->position = !strcmp(v, "relative") ? P_RELATIVE : !strcmp(v, "absolute") ? P_ABSOLUTE : !strcmp(v, "fixed") ? P_FIXED : !strcmp(v, "sticky") ? P_RELATIVE : P_STATIC;
    else if (IS("float")) st->floatv = !strcmp(v, "left") ? 1 : !strcmp(v, "right") ? 2 : 0;
    else if (IS("left")) { struct wlen l = parse_len(v, &ok); if (ok) st->left = l; }
    else if (IS("top")) { struct wlen l = parse_len(v, &ok); if (ok) st->top = l; }
    else if (IS("right")) { struct wlen l = parse_len(v, &ok); if (ok) st->right = l; }
    else if (IS("bottom")) { struct wlen l = parse_len(v, &ok); if (ok) st->bottom = l; }
    else if (IS("width")) { struct wlen l = parse_len(v, &ok); if (ok) st->width = l; }
    else if (IS("height")) { struct wlen l = parse_len(v, &ok); if (ok) st->height = l; }
    else if (IS("min-height")) { struct wlen l = parse_len(v, &ok); if (ok) st->minh = l; }
    else if (IS("margin")) four(st->margin, v);
    else if (IS("padding")) { four(st->pad, v); for (int i = 0; i < 4; i++) if (st->pad[i].u == 'A') st->pad[i].u = 0; }
    else if (IS("margin-top") || IS("margin-right") || IS("margin-bottom") || IS("margin-left") ||
             IS("padding-top") || IS("padding-right") || IS("padding-bottom") || IS("padding-left")) {
        struct wlen l = parse_len(v, &ok); if (!ok) return;
        if (!strcmp(v, "auto")) l.u = 'A';
        int side = strstr(prop, "top") ? 0 : strstr(prop, "right") ? 1 : strstr(prop, "bottom") ? 2 : 3;
        if (prop[0] == 'm') st->margin[side] = l; else st->pad[side] = l;
    }
    else if (IS("border")) border_short(st, v, -1, st->size);
    else if (IS("border-top")) border_short(st, v, 0, st->size);
    else if (IS("border-right")) border_short(st, v, 1, st->size);
    else if (IS("border-bottom")) border_short(st, v, 2, st->size);
    else if (IS("border-left")) border_short(st, v, 3, st->size);
    else if (IS("border-width") || IS("border-top-width") || IS("border-right-width") || IS("border-bottom-width") || IS("border-left-width")) {
        if (IS("border-width")) { struct wlen w[4]; for (int i = 0; i < 4; i++) w[i] = (struct wlen){ st->border[i], 'x' }; four(w, v); for (int i = 0; i < 4; i++) st->border[i] = w[i].v; }
        else { int side = strstr(prop, "top") ? 0 : strstr(prop, "right") ? 1 : strstr(prop, "bottom") ? 2 : 3; double x = border_width(v, &ok); if (ok) st->border[side] = x; }
    }
    else if (IS("border-color")) { float c[4]; if (css_parse_color(v, c)) for (int i = 0; i < 4; i++) { memcpy(st->bcol[i], c, sizeof c); st->bcol_cur[i] = 0; } }
    else if (IS("border-style")) { if (!strcmp(v, "none") || !strcmp(v, "hidden")) for (int i = 0; i < 4; i++) st->border[i] = 0; }
    else if (IS("border-radius") || IS("-webkit-border-radius")) { struct wlen l = parse_len(v, &ok); if (ok) st->radius = l.u == 'e' ? l.v * st->size : l.v; }
    else if (IS("background")) background(st, v, 0);
    else if (IS("background-color")) background(st, v, 1);
    else if (IS("background-image")) { if (!strcmp(v, "none")) { free(st->bgimg); st->bgimg = NULL; st->has_grad = 0; } else { char *u = parse_url(v); if (u) { free(st->bgimg); st->bgimg = u; } if (strstr(v, "gradient(")) st->has_grad = parse_gradient(v, st->grad); } }
    else if (IS("background-repeat")) st->bg_norepeat = !strcmp(v, "no-repeat");
    else if (IS("background-position")) { float keep[4]; memcpy(keep, st->bg, sizeof keep); char *u = st->bgimg; st->bgimg = NULL; int g = st->has_grad, nr = st->bg_norepeat;
        background(st, v, 0); free(st->bgimg); st->bgimg = u; st->has_grad = g; st->bg_norepeat = nr; memcpy(st->bg, keep, sizeof keep); }
    else if (IS("background-size") || IS("-webkit-background-size")) {
        if (!strcmp(v, "cover")) st->bgsize_kind = 2;
        else if (!strcmp(v, "contain")) st->bgsize_kind = 3;
        else { char t[2][64]; int n = tokens(v, t, 2); if (n) { st->bgsize[0] = parse_len(t[0], &ok); st->bgsize[1] = n > 1 ? parse_len(t[1], &ok) : (struct wlen){ 0, 0 }; st->bgsize_kind = 1; } }
    }
    else if (IS("opacity")) { double x = atof(v); st->opacity = x < 0 ? 0 : x > 1 ? 1 : x; }
    else if (IS("z-index")) { if (!strcmp(v, "auto")) st->zauto = 1; else { st->z = atoi(v); st->zauto = 0; } }
    else if (IS("overflow")) st->overflow_hidden = !strcmp(v, "hidden");
    #undef IS
}

void css_apply_inline(struct cstyle *st, const char *decl, const struct cstyle *parent) {
    struct decl *d; int n = parse_decls(decl, &d);
    for (int i = 0; i < n; i++) { apply_one(st, d[i].prop, d[i].val, parent); free(d[i].val); }
    free(d);
}

/* ------------------------------------------------------------------ inline style editing (element.style) */
char *css_get_inline(const char *style, const char *prop) {
    struct decl *d; int n = parse_decls(style ? style : "", &d); char *r = NULL;
    for (int i = 0; i < n; i++) { if (!strcmp(d[i].prop, prop)) { free(r); r = strdup(d[i].val); } free(d[i].val); }
    free(d);
    return r;
}
char *css_set_inline(const char *style, const char *prop, const char *val) {
    struct decl *d; int n = parse_decls(style ? style : "", &d);
    size_t cap = 64; for (int i = 0; i < n; i++) cap += strlen(d[i].prop) + strlen(d[i].val) + 16;
    cap += strlen(prop) + (val ? strlen(val) : 0) + 16;
    char *out = malloc(cap); out[0] = 0; int found = 0;
    for (int i = 0; i < n; i++) {
        if (!strcmp(d[i].prop, prop)) { if (!found && val && *val) { found = 1; snprintf(out + strlen(out), cap - strlen(out), "%s: %s; ", prop, val); } else found = 1; }
        else snprintf(out + strlen(out), cap - strlen(out), "%s: %s%s; ", d[i].prop, d[i].val, d[i].important ? " !important" : "");
        free(d[i].val);
    }
    free(d);
    if (!found && val && *val) snprintf(out + strlen(out), cap - strlen(out), "%s: %s; ", prop, val);
    size_t l = strlen(out); while (l && out[l - 1] == ' ') out[--l] = 0;
    return out;
}

/* ------------------------------------------------------------------ cascade */
struct match { const struct rule *r; int spec; };
static int cmp_match(const void *a, const void *b) {
    const struct match *x = a, *y = b;
    if (x->r->origin != y->r->origin) return x->r->origin - y->r->origin;
    if (x->spec != y->spec) return x->spec - y->spec;
    return x->r->order < y->r->order ? -1 : x->r->order > y->r->order;
}
static void init_style(struct cstyle *st, const struct cstyle *parent) {
    char *keep = NULL; (void)keep;
    memset(st, 0, sizeof *st);
    if (parent) {
        st->size = parent->size; st->lh = parent->lh; memcpy(st->color, parent->color, sizeof st->color);
        st->bold = parent->bold; st->italic = parent->italic; st->align = parent->align; st->upper = parent->upper; st->pre = parent->pre;
        st->nowrap = parent->nowrap; st->nobullet = parent->nobullet; st->vis_hidden = parent->vis_hidden; st->no_pointer = parent->no_pointer;
        st->list_decimal = parent->list_decimal;
    } else { st->size = 16; st->color[3] = 1; }
    st->display = D_INLINE; st->opacity = 1; st->zauto = 1;
    for (int i = 0; i < 4; i++) st->bcol_cur[i] = 1;
}
static double resolve_em(struct wlen *l, double size) { if (l->u == 'e') { l->u = 'x'; l->v *= size; } return l->v; }
static void finish_style(struct cstyle *st) {
    resolve_em(&st->left, st->size); resolve_em(&st->top, st->size); resolve_em(&st->right, st->size); resolve_em(&st->bottom, st->size);
    resolve_em(&st->width, st->size); resolve_em(&st->height, st->size); resolve_em(&st->minh, st->size);
    for (int i = 0; i < 4; i++) { resolve_em(&st->margin[i], st->size); resolve_em(&st->pad[i], st->size); }
    for (int i = 0; i < 2; i++) resolve_em(&st->bgsize[i], st->size);
    if (st->lh.u == 'e') { st->lh.u = 'x'; st->lh.v *= st->size; }
    for (int i = 0; i < 4; i++) if (st->bcol_cur[i]) memcpy(st->bcol[i], st->color, sizeof st->color);
    if (st->size < 1) st->size = 1; if (st->size > 400) st->size = 400;
    if (st->position == P_ABSOLUTE || st->position == P_FIXED) st->floatv = 0;
}
void css_cascade(struct wdocument *d) {
    struct css *c = d->sheet;
    struct match *m = malloc((c ? c->n : 0) * sizeof *m + 1);
    char *mediaok = malloc((c ? c->n : 0) + 1);
    for (size_t i = 0; c && i < c->n; i++) mediaok[i] = (char)media_ok(d, c->r[i].media);
    long seq = 0;
    d->root->cs.size = 16; d->root->cs.color[3] = 1; d->root->cs.display = D_BLOCK; d->root->cs.opacity = 1;
    for (struct dnode *n = d->root->first ? d->root->first : NULL; n; n = dom_next_pre(n, d->root)) {
        n->seq = ++seq;
        if (n->type != N_ELEMENT) continue;
        const struct cstyle *parent = n->parent ? &n->parent->cs : NULL;
        free(n->cs.bgimg);
        init_style(&n->cs, parent);
        size_t nm = 0;
        for (size_t i = 0; c && i < c->n; i++) {
            if (!mediaok[i]) continue;
            int spec; if (sel_matches(c->r[i].sel, n, &spec)) { m[nm].r = &c->r[i]; m[nm].spec = spec; nm++; }
        }
        qsort(m, nm, sizeof *m, cmp_match);
        for (size_t i = 0; i < nm; i++) for (int k = 0; k < m[i].r->nd; k++) if (!m[i].r->d[k].important) apply_one(&n->cs, m[i].r->d[k].prop, m[i].r->d[k].val, parent);
        const char *inl = dom_attr(n, "style"); if (inl) css_apply_inline(&n->cs, inl, parent);
        for (size_t i = 0; i < nm; i++) for (int k = 0; k < m[i].r->nd; k++) if (m[i].r->d[k].important) apply_one(&n->cs, m[i].r->d[k].prop, m[i].r->d[k].val, parent);
        /* presentational attributes */
        if (!strcmp(n->tag, "img") || !strcmp(n->tag, "iframe") || !strcmp(n->tag, "canvas") || !strcmp(n->tag, "video")) {
            const char *w = dom_attr(n, "width"), *h = dom_attr(n, "height");
            if (w && !n->cs.width.u) n->cs.width = (struct wlen){ atof(w), strchr(w, '%') ? '%' : 'x' };
            if (h && !n->cs.height.u) n->cs.height = (struct wlen){ atof(h), strchr(h, '%') ? '%' : 'x' };
        }
        if (!strcmp(n->tag, "font")) { const char *col = dom_attr(n, "color"); if (col) css_parse_color(col, n->cs.color); }
        if (dom_attr(n, "hidden")) n->cs.display = D_NONE;
        const char *al = dom_attr(n, "align"); if (al && n->cs.display != D_INLINE) n->cs.align = web_ieq(al, "center") ? 1 : web_ieq(al, "right") ? 2 : n->cs.align;
        const char *bgc = dom_attr(n, "bgcolor"); if (bgc) css_parse_color(bgc, n->cs.bg);
        finish_style(&n->cs);
    }
    d->seqctr = seq;
    free(m); free(mediaok);
}
