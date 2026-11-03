/*
 * Text: UIFont/UIFontDescriptor (system font = the device's TrueType font,
 * custom fonts registered from UIAppFonts / CGFont / CTFontManager),
 * NSParagraphStyle, NSString/NSAttributedString drawing and measuring with
 * word wrap, line limits and tail truncation.
 */
#include "uikit_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSString);
RT_DECLARE_CLASS(NSAttributedString);
RT_DECLARE_CLASS(NSBundle);
RT_DEFINE_CLASS(UIFont);
RT_DEFINE_CLASS(UIFontDescriptor);
RT_DEFINE_CLASS(NSParagraphStyle);
RT_DEFINE_CLASS(NSMutableParagraphStyle);

id iosrt_NSFontAttributeName, iosrt_NSForegroundColorAttributeName, iosrt_NSParagraphStyleAttributeName, iosrt_NSBackgroundColorAttributeName,
   iosrt_NSKernAttributeName, iosrt_NSShadowAttributeName, iosrt_NSStrokeColorAttributeName, iosrt_NSStrokeWidthAttributeName,
   iosrt_NSUnderlineStyleAttributeName, iosrt_NSBaselineOffsetAttributeName, iosrt_NSLinkAttributeName, iosrt_NSAttachmentAttributeName,
   iosrt_NSStrikethroughStyleAttributeName, iosrt_NSLigatureAttributeName, iosrt_NSObliquenessAttributeName,
   iosrt_UIFontTextStyleBody, iosrt_UIFontTextStyleHeadline, iosrt_UIFontTextStyleSubheadline, iosrt_UIFontTextStyleFootnote,
   iosrt_UIFontTextStyleCaption1, iosrt_UIFontTextStyleCaption2, iosrt_UIFontTextStyleTitle1, iosrt_UIFontTextStyleTitle2, iosrt_UIFontTextStyleTitle3,
   iosrt_UIFontDescriptorNameAttribute, iosrt_UIFontDescriptorSizeAttribute, iosrt_UIFontDescriptorFamilyAttribute, iosrt_UIFontDescriptorTraitsAttribute,
   iosrt_UIFontSymbolicTrait, iosrt_UIFontWeightTrait;
double iosrt_UIFontWeightUltraLight = -0.8, iosrt_UIFontWeightThin = -0.6, iosrt_UIFontWeightLight = -0.4, iosrt_UIFontWeightRegular = 0,
       iosrt_UIFontWeightMedium = 0.23, iosrt_UIFontWeightSemibold = 0.3, iosrt_UIFontWeightBold = 0.4, iosrt_UIFontWeightHeavy = 0.56, iosrt_UIFontWeightBlack = 0.62;

/* ------------------------------------------------------------ registry */
struct reg { char *name; struct gfx_font *gf; };
static struct reg regs[64]; static int nregs, app_fonts_loaded;
void rt_font_register(const char *name, struct gfx_font *gf) {
    if (!name || !gf || nregs >= 64) return;
    for (int i = 0; i < nregs; i++) if (!strcmp(regs[i].name, name)) return;
    regs[nregs].name = rt_xstrdup(name); regs[nregs].gf = gf; nregs++;
}
static struct gfx_font *reg_find(const char *name) {
    for (int i = 0; i < nregs; i++) if (!strcasecmp(regs[i].name, name)) return regs[i].gf;
    return NULL;
}
static void load_app_fonts(void) {
    if (app_fonts_loaded) return;
    app_fonts_loaded = 1;
    id bundle = RT_SEND0(id, (id)RT_CLASS(NSBundle), "mainBundle");
    id list = RT_SEND1(id, bundle, "objectForInfoDictionaryKey:", id, rt_str("UIAppFonts"));
    if (!list) return;
    const char *dir = rt_str_utf8(RT_SEND0(id, bundle, "bundlePath"));
    for (size_t i = 0; i < rt_array_count(list); i++) {
        char p[4096]; snprintf(p, sizeof p, "%s/%s", dir, rt_str_utf8(rt_array_at(list, i)));
        FILE *f = fopen(p, "rb"); if (!f) continue;
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        unsigned char *b = n > 0 ? malloc((size_t)n) : NULL;
        if (b && fread(b, 1, (size_t)n, f) == (size_t)n) {
            struct gfx_font *gf = gfx_font_from_memory(b, (size_t)n);
            if (gf) {
                char nm[256];
                if (gfx_font_name(gf, 6, nm, sizeof nm)) rt_font_register(nm, gf);
                if (gfx_font_name(gf, 4, nm, sizeof nm)) rt_font_register(nm, gf);
                const char *base = strrchr(p, '/') ? strrchr(p, '/') + 1 : p; snprintf(nm, sizeof nm, "%s", base);
                char *dot = strrchr(nm, '.'); if (dot) *dot = 0;
                rt_font_register(nm, gf);
            } else free(b);
        } else free(b);
        fclose(f);
    }
}

/* ---------------------------------------------------------------- UIFont */
id rt_font_make(double size, int bold, id name) {
    id f = rt_alloc_instance(RT_CLASS(UIFont), 0); struct uifont *u = (struct uifont *)f;
    u->size = size; u->bold = bold; u->name = objc_retain(name);
    return objc_autorelease(f);
}
double rt_font_size(id f) { return f ? ((struct uifont *)f)->size : 17; }
struct gfx_font *rt_font_gfx(id font, int *fake_bold) {
    struct uifont *u = (struct uifont *)font;
    if (fake_bold) *fake_bold = 0;
    if (u && u->gf) return u->gf;
    int bold = u ? (int)u->bold : 0;
    struct gfx_font *f = gfx_font_get(bold);
    if (bold && fake_bold && f == gfx_font_get(0)) *fake_bold = 1;
    return f;
}
static void f_dealloc(id self, SEL s) { (void)s; objc_release(((struct uifont *)self)->name); rt_dealloc_instance(self); }
static id f_system(Class c, SEL s, double size) { (void)c; (void)s; return rt_font_make(size, 0, NULL); }
static id f_bold(Class c, SEL s, double size) { (void)c; (void)s; return rt_font_make(size, 1, NULL); }
static id f_systemWeight(Class c, SEL s, double size, double w) { (void)c; (void)s; return rt_font_make(size, w >= 0.3, NULL); }
static int name_is_bold(const char *n) { return strstr(n, "Bold") || strstr(n, "bold") || strstr(n, "Heavy") || strstr(n, "Black") || strstr(n, "Semibold"); }
static id f_named(Class c, SEL s, id name, double size) {
    (void)c; (void)s;
    if (!name) return rt_font_make(size, 0, NULL);
    const char *n = rt_str_utf8(name);
    struct gfx_font *gf = reg_find(n);
    if (!gf) { load_app_fonts(); gf = reg_find(n); }
    id f = rt_font_make(size, name_is_bold(n) ? 1 : 0, name);
    ((struct uifont *)f)->gf = gf;
    return f;
}
static id f_withSize(id self, SEL s, double size) {
    (void)s; struct uifont *u = (struct uifont *)self;
    id f = rt_font_make(size, (int)u->bold, u->name); ((struct uifont *)f)->gf = u->gf; return f;
}
static double f_pointSize(id self, SEL s) { (void)s; return ((struct uifont *)self)->size; }
static id f_fontName(id self, SEL s) { (void)s; struct uifont *u = (struct uifont *)self; return u->name ? u->name : rt_str(u->bold ? ".SFUI-Semibold" : ".SFUI-Regular"); }
static id f_familyName(id self, SEL s) { (void)s; struct uifont *u = (struct uifont *)self; return u->name ? u->name : rt_str(".SF UI Text"); }
static void metrics(id self, float *a, float *d, float *g) { struct gfx_font *gf = rt_font_gfx(self, NULL); gfx_font_vmetrics(gf, a, d, g); }
static double f_ascender(id self, SEL s) { (void)s; float a, d, g; metrics(self, &a, &d, &g); return a * ((struct uifont *)self)->size; }
static double f_descender(id self, SEL s) { (void)s; float a, d, g; metrics(self, &a, &d, &g); return -d * ((struct uifont *)self)->size; }
static double f_lineHeight(id self, SEL s) { (void)s; float a, d, g; metrics(self, &a, &d, &g); return (a + d + g) * ((struct uifont *)self)->size; }
static double f_leading(id self, SEL s) { (void)s; float a, d, g; metrics(self, &a, &d, &g); return g * ((struct uifont *)self)->size; }
static double f_capHeight(id self, SEL s) { (void)s; return 0.7 * ((struct uifont *)self)->size; }
static double f_xHeight(id self, SEL s) { (void)s; return 0.52 * ((struct uifont *)self)->size; }
static id f_preferred(Class c, SEL s, id style) {
    (void)c; (void)s; const char *n = style ? rt_str_utf8(style) : "";
    double size = strstr(n, "Headline") ? 17 : strstr(n, "Subhead") ? 15 : strstr(n, "Footnote") ? 13 : strstr(n, "Caption1") ? 12 :
                  strstr(n, "Caption2") ? 11 : strstr(n, "Title1") ? 28 : strstr(n, "Title2") ? 22 : strstr(n, "Title3") ? 20 : 17;
    return rt_font_make(size, strstr(n, "Headline") != NULL, NULL);
}
static id f_familyNames(Class c, SEL s) {
    (void)c; (void)s; load_app_fonts();
    id a = RT_SEND0(id, (id)rt_class_named("NSMutableArray"), "array");
    RT_SEND1(void, a, "addObject:", id, rt_str("Helvetica Neue"));
    for (int i = 0; i < nregs; i++) RT_SEND1(void, a, "addObject:", id, rt_str(regs[i].name));
    return a;
}
static id f_namesForFamily(Class c, SEL s, id fam) { (void)c; (void)s; return fam ? objc_autorelease(rt_array_new(&fam, 1)) : NULL; }
static id f_descriptor(id self, SEL s);
static id f_withDescriptor(Class c, SEL s, id d, double size);
static id f_desc(id self, SEL s) { (void)s; char b[256]; snprintf(b, sizeof b, "<UIFont: %p> font-family: \"%s\"; font-size: %gpt", (void *)self, rt_str_utf8(f_familyName(self, NULL)), ((struct uifont *)self)->size); return rt_str(b); }
static id f_copy(id self, SEL s, void *z) { (void)s; (void)z; return objc_retain(self); }
static rt_bool f_isEqual(id self, SEL s, id o) {
    (void)s; if (self == o) return 1;
    if (!o || !rt_is_kind_of(o, RT_CLASS(UIFont))) return 0;
    struct uifont *a = (struct uifont *)self, *b = (struct uifont *)o;
    return a->size == b->size && a->bold == b->bold && a->gf == b->gf;
}

/* UIFontDescriptor: just name + size + bold */
struct uifd { Class isa; id name; double size; long bold; };
static id fd_make(id name, double size, long bold) {
    id d = rt_alloc_instance(RT_CLASS(UIFontDescriptor), 0); struct uifd *x = (struct uifd *)d;
    x->name = objc_retain(name); x->size = size; x->bold = bold; return objc_autorelease(d);
}
static id fd_withName(Class c, SEL s, id name, double size) { (void)c; (void)s; return fd_make(name, size, name && name_is_bold(rt_str_utf8(name))); }
static id fd_withAttrs(Class c, SEL s, id attrs) {
    (void)c; (void)s;
    id name = attrs ? rt_dict_get(attrs, rt_str("NSFontNameAttribute")) : NULL;
    id size = attrs ? rt_dict_get(attrs, rt_str("NSFontSizeAttribute")) : NULL;
    return fd_make(name, size ? RT_SEND0(double, size, "doubleValue") : 0, name && name_is_bold(rt_str_utf8(name)));
}
static id fd_preferred(Class c, SEL s, id style) { id f = f_preferred(RT_CLASS(UIFont), s, style); (void)c; return f_descriptor(f, s); }
static double fd_size(id self, SEL s) { (void)s; return ((struct uifd *)self)->size; }
static id fd_name(id self, SEL s) { (void)s; return ((struct uifd *)self)->name; }
static id fd_withTraits(id self, SEL s, unsigned traits) { (void)s; struct uifd *x = (struct uifd *)self; return fd_make(x->name, x->size, (traits & 2) ? 1 : x->bold); }
static unsigned fd_traits(id self, SEL s) { (void)s; return ((struct uifd *)self)->bold ? 2 : 0; }
static id fd_withSize(id self, SEL s, double size) { (void)s; struct uifd *x = (struct uifd *)self; return fd_make(x->name, size, x->bold); }
static id fd_attrs(id self, SEL s) {
    (void)s; struct uifd *x = (struct uifd *)self;
    id keys[2] = { rt_str("NSFontNameAttribute"), rt_str("NSFontSizeAttribute") };
    id vals[2] = { x->name ? x->name : rt_str(".SFUI-Regular"), RT_SEND1(id, (id)rt_class_named("NSNumber"), "numberWithDouble:", double, x->size) };
    return RT_SEND3(id, (id)rt_class_named("NSDictionary"), "dictionaryWithObjects:forKeys:count:", id *, vals, id *, keys, unsigned long, 2);
}
static id f_descriptor(id self, SEL s) { (void)s; struct uifont *u = (struct uifont *)self; return fd_make(u->name, u->size, u->bold); }
static id f_withDescriptor(Class c, SEL s, id d, double size) {
    struct uifd *x = (struct uifd *)d;
    if (!x) return f_system(c, s, size);
    if (size <= 0) size = x->size > 0 ? x->size : 17;
    if (x->name) { id f = f_named(c, s, x->name, size); if (x->bold) ((struct uifont *)f)->bold = 1; return f; }
    return rt_font_make(size, (int)x->bold, NULL);
}

/* -------------------------------------------------------- paragraph style */
struct nspara { Class isa; long align, lineBreak; double lineSpacing, paraSpacing, firstIndent, headIndent, tailIndent, minLH, maxLH, lhMultiple; long dir; };
static id ps_default(Class c, SEL s) { (void)c; (void)s; id p = rt_alloc_instance(RT_CLASS(NSParagraphStyle), 0); ((struct nspara *)p)->align = 4; return objc_autorelease(p); }
static id ps_init(id self, SEL s) { (void)s; ((struct nspara *)self)->align = 4; return self; }
static id ps_copyAs(id self, Class c) { id p = rt_alloc_instance(c, 0); struct nspara *d = (struct nspara *)p; *d = *(struct nspara *)self; d->isa = c; return p; }
static id ps_copy(id self, SEL s, void *z) { (void)s; (void)z; return ps_copyAs(self, RT_CLASS(NSParagraphStyle)); }
static id ps_mcopy(id self, SEL s, void *z) { (void)s; (void)z; return ps_copyAs(self, RT_CLASS(NSMutableParagraphStyle)); }
#define PS_PROP(get, set, field, T) \
    static T get(id self, SEL s) { (void)s; return (T)((struct nspara *)self)->field; } \
    static void set(id self, SEL s, T v) { (void)s; ((struct nspara *)self)->field = v; }
PS_PROP(ps_align, ps_setAlign, align, long)
PS_PROP(ps_lineBreak, ps_setLineBreak, lineBreak, long)
PS_PROP(ps_lineSpacing, ps_setLineSpacing, lineSpacing, double)
PS_PROP(ps_paraSpacing, ps_setParaSpacing, paraSpacing, double)
PS_PROP(ps_firstIndent, ps_setFirstIndent, firstIndent, double)
PS_PROP(ps_headIndent, ps_setHeadIndent, headIndent, double)
PS_PROP(ps_tailIndent, ps_setTailIndent, tailIndent, double)
PS_PROP(ps_minLH, ps_setMinLH, minLH, double)
PS_PROP(ps_maxLH, ps_setMaxLH, maxLH, double)
PS_PROP(ps_lhMul, ps_setLhMul, lhMultiple, double)

/* ----------------------------------------------------------------- layout */
id rt_attrs_font(id a) { return a ? rt_dict_get(a, iosrt_NSFontAttributeName) : NULL; }
id rt_attrs_color(id a) { return a ? rt_dict_get(a, iosrt_NSForegroundColorAttributeName) : NULL; }
static id attrs_para(id a) { return a ? rt_dict_get(a, iosrt_NSParagraphStyleAttributeName) : NULL; }
long rt_attrs_align(id a) {
    id p = attrs_para(a); long al = p ? ((struct nspara *)p)->align : 0;
    return al == 4 ? 0 : al;   /* natural -> left; 1 center (iOS), 2 right, 3 justified */
}
struct tline { const char *s; size_t n; float w; };
struct tlayout { struct tline *l; int n, cap; float lh, asc, size; struct gfx_font *gf; char *owned; };
static void tl_add(struct tlayout *t, const char *s, size_t n, float w) {
    if (t->n == t->cap) { t->cap = t->cap ? t->cap * 2 : 8; t->l = realloc(t->l, sizeof *t->l * (size_t)t->cap); }
    t->l[t->n++] = (struct tline){ s, n, w };
}
static size_t u8len(const char *s) { unsigned char c = (unsigned char)*s; return c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1; }
/* break `text` into lines no wider than maxw (<= 0: no wrapping); maxlines 0 = unlimited; truncate tail with an ellipsis */
static void layout(struct tlayout *t, const char *text, id font, double maxw, long maxlines, int truncate) {
    memset(t, 0, sizeof *t);
    t->size = (float)rt_font_size(font); t->gf = rt_font_gfx(font, NULL);
    float a, d, g; gfx_font_vmetrics(t->gf, &a, &d, &g);
    t->asc = a * t->size; t->lh = (a + d + g) * t->size;
    if (t->lh <= 0) t->lh = t->size * 1.2f;
    const char *p = text;
    while (1) {
        const char *nl = strchr(p, '\n'); size_t plen = nl ? (size_t)(nl - p) : strlen(p);
        const char *ls = p, *end = p + plen;
        if (maxw <= 0) tl_add(t, ls, plen, gfx_text_width(t->gf, ls, plen, t->size));
        else {
            while (ls < end || ls == p) {
                /* greedy: extend word by word */
                const char *q = ls, *lastbreak = NULL;
                while (q < end) {
                    const char *nq = q + u8len(q);
                    if (gfx_text_width(t->gf, ls, (size_t)(nq - ls), t->size) > maxw + 0.01) break;
                    if (*q == ' ') lastbreak = q;
                    q = nq;
                }
                if (q >= end) { tl_add(t, ls, (size_t)(end - ls), gfx_text_width(t->gf, ls, (size_t)(end - ls), t->size)); break; }
                const char *cut = lastbreak && lastbreak > ls ? lastbreak : (q > ls ? q : q + u8len(q));
                size_t n = (size_t)(cut - ls);
                while (n && ls[n - 1] == ' ') n--;
                tl_add(t, ls, n, gfx_text_width(t->gf, ls, n, t->size));
                ls = cut; while (ls < end && *ls == ' ') ls++;
                if (ls >= end) break;
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
    if (maxlines > 0 && t->n > maxlines) {
        t->n = (int)maxlines;
        if (truncate) {
            struct tline *last = &t->l[t->n - 1];
            /* rest of the text joins the last line, then gets cut to fit with "…" */
            const char *rest_end = text + strlen(text);
            size_t full = (size_t)(rest_end - last->s);
            char *buf = malloc(full + 4); size_t k = 0;
            for (size_t i = 0; i < full; i++) buf[k++] = last->s[i] == '\n' ? ' ' : last->s[i];
            buf[k] = 0;
            float lim = maxw > 0 ? (float)maxw : 1e9f;
            float ew = gfx_text_width(t->gf, "\xe2\x80\xa6", 3, t->size);
            while (k > 0 && gfx_text_width(t->gf, buf, k, t->size) + ew > lim) { k--; while (k > 0 && ((unsigned char)buf[k] & 0xc0) == 0x80) k--; }
            while (k > 0 && buf[k - 1] == ' ') k--;
            memcpy(buf + k, "\xe2\x80\xa6", 3); k += 3; buf[k] = 0;
            t->owned = buf; last->s = buf; last->n = k; last->w = gfx_text_width(t->gf, buf, k, t->size);
        }
    }
}
static void tl_free(struct tlayout *t) { free(t->l); free(t->owned); }
static CGSize tl_size(const struct tlayout *t) {
    float w = 0; for (int i = 0; i < t->n; i++) if (t->l[i].w > w) w = t->l[i].w;
    return (CGSize){ ceil(w), ceil(t->n * t->lh) };
}
CGSize rt_text_measure(const char *utf8, id font, double maxw, long lines) {
    struct tlayout t; layout(&t, utf8 ? utf8 : "", font, maxw, lines, 1);
    CGSize s = tl_size(&t); tl_free(&t); return s;
}
static double clean(double v) { double r = round(v * 1000) / 1000; return r == 0 ? 0 : r; }
static void draw_layout(struct cgctx *c, const struct tlayout *t, const float *rgba, CGRect r, long align, int vcenter, id font) {
    double top = r.origin.y;
    if (vcenter) top += (r.size.height - t->n * t->lh) / 2;
    gfx_mat m = rt_cg_ctm(c);
    double k = sqrt(fabs(m.a * m.d - m.b * m.c));
    int fake_bold; struct gfx_font *gf = rt_font_gfx(font, &fake_bold);
    float col[4] = { rgba[0], rgba[1], rgba[2], rgba[3] * rt_cg_alpha(c) };
    for (int i = 0; i < t->n; i++) {
        double x = r.origin.x;
        if (align == 1) x += (r.size.width - t->l[i].w) / 2;
        else if (align == 2) x += r.size.width - t->l[i].w;
        double base = top + t->asc + i * t->lh, dx, dy;
        gfx_mat_apply(&m, x, base, &dx, &dy);
        gfx_draw_text(rt_cg_surface(c), gf, t->l[i].s, t->l[i].n, (float)dx, (float)dy, (float)(t->size * k), col, fake_bold && k * t->size > 6);
    }
}
void rt_text_draw(struct cgctx *c, const char *utf8, id font, const float *rgba, CGRect r, long align, long lines, int vcenter) {
    if (!c || !utf8) return;
    struct tlayout t; layout(&t, utf8, font, r.size.width, lines, 1);
    draw_layout(c, &t, rgba, r, align, vcenter, font);
    double sc; rt_sb *tr = rt_cg_trace(c, &sc);
    if (tr) {
        CGRect pr;
        if (rt_cg_device_rect_points(c, r, &pr))
            rt_cg_trace_line(c, "text %g,%g %gx%g rgba(%d,%d,%d,%.2f) size=%g align=%ld \"%s\"", clean(pr.origin.x), clean(pr.origin.y), clean(pr.size.width), clean(pr.size.height),
                             (int)lroundf(rgba[0] * 255), (int)lroundf(rgba[1] * 255), (int)lroundf(rgba[2] * 255), rgba[3] * rt_cg_alpha(c), clean(t.size), align, utf8);
    }
    tl_free(&t);
}

/* ------------------------------------------------- NSString (UIKit additions) */
static id default_font(void) { return rt_font_make(12, 0, NULL); }   /* NSString drawing default is Helvetica 12 */
static void color_or_black(id col, float out[4]) { if (col) rt_color_rgba(col, out); else { out[0] = out[1] = out[2] = 0; out[3] = 1; } }
static const char *sstr(id self) { return rt_str_utf8(self); }
static CGSize s_sizeWithAttrs(id self, SEL s, id attrs) { (void)s; id f = rt_attrs_font(attrs); return rt_text_measure(sstr(self), f ? f : default_font(), 0, 0); }
static CGSize s_sizeWithFont(id self, SEL s, id f) { (void)s; return rt_text_measure(sstr(self), f ? f : default_font(), 0, 1); }
static CGSize s_sizeWithFontCons(id self, SEL s, id f, CGSize cs) { (void)s; CGSize r = rt_text_measure(sstr(self), f ? f : default_font(), cs.width, 0); if (cs.height > 0 && r.height > cs.height) r.height = cs.height; return r; }
static CGSize s_sizeWithFontConsLB(id self, SEL s, id f, CGSize cs, long lb) { (void)lb; return s_sizeWithFontCons(self, s, f, cs); }
static CGSize s_sizeWithFontWidth(id self, SEL s, id f, double w, long lb) { (void)lb; (void)s; CGSize r = rt_text_measure(sstr(self), f ? f : default_font(), 0, 1); if (r.width > w) r.width = w; return r; }
static CGSize s_sizeWithFontMin(id self, SEL s, id f, double minsz, double *actual, double w, long lb) { (void)minsz; if (actual) *actual = rt_font_size(f); return s_sizeWithFontWidth(self, s, f, w, lb); }
static CGRect s_boundingRect(id self, SEL s, CGSize sz, long opts, id attrs, id ctx) {
    (void)s; (void)ctx; id f = rt_attrs_font(attrs);
    CGSize r = rt_text_measure(sstr(self), f ? f : default_font(), (opts & 1) ? sz.width : 0, (opts & 1) ? 0 : 1);
    if (sz.height > 0 && r.height > sz.height && (opts & 1)) r.height = sz.height;
    return (CGRect){ { 0, 0 }, r };
}
static void s_drawInRectAttrs(id self, SEL s, CGRect r, id attrs) {
    (void)s; struct cgctx *c = rt_cg_top(); if (!c) return;
    id f = rt_attrs_font(attrs); float col[4]; color_or_black(rt_attrs_color(attrs), col);
    id para = attrs_para(attrs);
    long lines = para && (((struct nspara *)para)->lineBreak >= 3) ? 1 : 0;   /* clipping/truncating modes draw one line */
    rt_text_draw(c, sstr(self), f ? f : default_font(), col, r, rt_attrs_align(attrs), lines, 0);
}
static void s_drawAtPointAttrs(id self, SEL s, CGPoint p, id attrs) {
    id f = rt_attrs_font(attrs); CGSize sz = rt_text_measure(sstr(self), f ? f : default_font(), 0, 0);
    s_drawInRectAttrs(self, s, (CGRect){ p, sz }, attrs);
}
static void s_drawWithRect(id self, SEL s, CGRect r, long opts, id attrs, id ctx) { (void)opts; (void)ctx; s_drawInRectAttrs(self, s, r, attrs); }
static CGSize draw_font(id self, CGRect r, id f, long align, long lines) {
    struct cgctx *c = rt_cg_top(); f = f ? f : default_font();
    CGSize sz = rt_text_measure(sstr(self), f, r.size.width, lines);
    if (!c) return sz;
    /* legacy API draws with the context's fill color */
    float col[4] = { 0, 0, 0, 1 };
    extern void rt_cg_fill_color(struct cgctx *, float *);
    rt_cg_fill_color(c, col);
    rt_text_draw(c, sstr(self), f, col, r, align, lines, 0);
    return sz;
}
static CGSize s_drawAtPointFont(id self, SEL s, CGPoint p, id f) { (void)s; CGSize sz = rt_text_measure(sstr(self), f ? f : default_font(), 0, 1); return draw_font(self, (CGRect){ p, sz }, f, 0, 1); }
static CGSize s_drawAtPointWidth(id self, SEL s, CGPoint p, double w, id f, long lb) { (void)s; (void)lb; CGSize sz = rt_text_measure(sstr(self), f ? f : default_font(), 0, 1); return draw_font(self, (CGRect){ p, { w, sz.height } }, f, 0, 1); }
static CGSize s_drawInRectFont(id self, SEL s, CGRect r, id f) { (void)s; return draw_font(self, r, f, 0, 0); }
static CGSize s_drawInRectFontLB(id self, SEL s, CGRect r, id f, long lb) { (void)s; (void)lb; return draw_font(self, r, f, 0, 0); }
static CGSize s_drawInRectFontLBA(id self, SEL s, CGRect r, id f, long lb, long al) { (void)s; (void)lb; return draw_font(self, r, f, al, 0); }

/* NSAttributedString: first run's attributes apply to the whole string */
static id as_attrs0(id self) { return RT_SEND0(unsigned long, self, "length") ? RT_SEND2(id, self, "attributesAtIndex:effectiveRange:", unsigned long, 0, void *, NULL) : NULL; }
static CGSize as_size(id self, SEL s) { return s_sizeWithAttrs(RT_SEND0(id, self, "string"), s, as_attrs0(self)); }
static void as_drawInRect(id self, SEL s, CGRect r) { s_drawInRectAttrs(RT_SEND0(id, self, "string"), s, r, as_attrs0(self)); }
static void as_drawAtPoint(id self, SEL s, CGPoint p) { s_drawAtPointAttrs(RT_SEND0(id, self, "string"), s, p, as_attrs0(self)); }
static CGRect as_boundingRect(id self, SEL s, CGSize sz, long opts, id ctx) { return s_boundingRect(RT_SEND0(id, self, "string"), s, sz, opts, as_attrs0(self), ctx); }
static void as_drawWithRect(id self, SEL s, CGRect r, long opts, id ctx) { (void)opts; (void)ctx; as_drawInRect(self, s, r); }

void rt_text_init(void) {
    Class o = RT_CLASS(NSObject);
    NEWCLASS(UIFont, o, sizeof(struct uifont) - sizeof(void *));
    Class f = RT_CLASS(UIFont);
    CM(f, "systemFontOfSize:", f_system); CM(f, "boldSystemFontOfSize:", f_bold); CM(f, "italicSystemFontOfSize:", f_system);
    CM(f, "systemFontOfSize:weight:", f_systemWeight); CM(f, "monospacedDigitSystemFontOfSize:weight:", f_systemWeight);
    CM(f, "fontWithName:size:", f_named); CM(f, "preferredFontForTextStyle:", f_preferred); CM(f, "fontWithDescriptor:size:", f_withDescriptor);
    CM(f, "familyNames", f_familyNames); CM(f, "fontNamesForFamilyName:", f_namesForFamily);
    IM(f, "dealloc", f_dealloc); IM(f, "fontWithSize:", f_withSize); IM(f, "pointSize", f_pointSize); IM(f, "fontName", f_fontName);
    IM(f, "familyName", f_familyName); IM(f, "ascender", f_ascender); IM(f, "descender", f_descender); IM(f, "lineHeight", f_lineHeight);
    IM(f, "leading", f_leading); IM(f, "capHeight", f_capHeight); IM(f, "xHeight", f_xHeight); IM(f, "fontDescriptor", f_descriptor);
    IM(f, "description", f_desc); IM(f, "copyWithZone:", f_copy); IM(f, "copy", f_copy); IM(f, "isEqual:", f_isEqual);
    NEWCLASS(UIFontDescriptor, o, sizeof(struct uifd) - sizeof(void *));
    Class fd = RT_CLASS(UIFontDescriptor);
    CM(fd, "fontDescriptorWithName:size:", fd_withName); CM(fd, "fontDescriptorWithFontAttributes:", fd_withAttrs);
    CM(fd, "preferredFontDescriptorWithTextStyle:", fd_preferred);
    IM(fd, "pointSize", fd_size); IM(fd, "postscriptName", fd_name); IM(fd, "fontDescriptorWithSymbolicTraits:", fd_withTraits);
    IM(fd, "symbolicTraits", fd_traits); IM(fd, "fontDescriptorWithSize:", fd_withSize); IM(fd, "fontAttributes", fd_attrs);

    NEWCLASS(NSParagraphStyle, o, sizeof(struct nspara) - sizeof(void *));
    NEWCLASS(NSMutableParagraphStyle, RT_CLASS(NSParagraphStyle), 0);
    Class ps = RT_CLASS(NSParagraphStyle), mps = RT_CLASS(NSMutableParagraphStyle);
    CM(ps, "defaultParagraphStyle", ps_default); IM(ps, "init", ps_init); IM(ps, "copyWithZone:", ps_copy); IM(ps, "copy", ps_copy);
    IM(ps, "mutableCopyWithZone:", ps_mcopy); IM(ps, "mutableCopy", ps_mcopy);
    IM(ps, "alignment", ps_align); IM(ps, "lineBreakMode", ps_lineBreak); IM(ps, "lineSpacing", ps_lineSpacing);
    IM(ps, "paragraphSpacing", ps_paraSpacing); IM(ps, "firstLineHeadIndent", ps_firstIndent); IM(ps, "headIndent", ps_headIndent);
    IM(ps, "tailIndent", ps_tailIndent); IM(ps, "minimumLineHeight", ps_minLH); IM(ps, "maximumLineHeight", ps_maxLH); IM(ps, "lineHeightMultiple", ps_lhMul);
    IM(mps, "setAlignment:", ps_setAlign); IM(mps, "setLineBreakMode:", ps_setLineBreak); IM(mps, "setLineSpacing:", ps_setLineSpacing);
    IM(mps, "setParagraphSpacing:", ps_setParaSpacing); IM(mps, "setFirstLineHeadIndent:", ps_setFirstIndent); IM(mps, "setHeadIndent:", ps_setHeadIndent);
    IM(mps, "setTailIndent:", ps_setTailIndent); IM(mps, "setMinimumLineHeight:", ps_setMinLH); IM(mps, "setMaximumLineHeight:", ps_setMaxLH);
    IM(mps, "setLineHeightMultiple:", ps_setLhMul);

    Class st = RT_CLASS(NSString);
    IM(st, "sizeWithAttributes:", s_sizeWithAttrs); IM(st, "sizeWithFont:", s_sizeWithFont); IM(st, "sizeWithFont:constrainedToSize:", s_sizeWithFontCons);
    IM(st, "sizeWithFont:constrainedToSize:lineBreakMode:", s_sizeWithFontConsLB); IM(st, "sizeWithFont:forWidth:lineBreakMode:", s_sizeWithFontWidth);
    IM(st, "sizeWithFont:minFontSize:actualFontSize:forWidth:lineBreakMode:", s_sizeWithFontMin);
    IM(st, "boundingRectWithSize:options:attributes:context:", s_boundingRect);
    IM(st, "drawInRect:withAttributes:", s_drawInRectAttrs); IM(st, "drawAtPoint:withAttributes:", s_drawAtPointAttrs);
    IM(st, "drawWithRect:options:attributes:context:", s_drawWithRect);
    IM(st, "drawAtPoint:withFont:", s_drawAtPointFont); IM(st, "drawAtPoint:forWidth:withFont:lineBreakMode:", s_drawAtPointWidth);
    IM(st, "drawInRect:withFont:", s_drawInRectFont); IM(st, "drawInRect:withFont:lineBreakMode:", s_drawInRectFontLB);
    IM(st, "drawInRect:withFont:lineBreakMode:alignment:", s_drawInRectFontLBA);
    Class as = RT_CLASS(NSAttributedString);
    IM(as, "size", as_size); IM(as, "drawInRect:", as_drawInRect); IM(as, "drawAtPoint:", as_drawAtPoint);
    IM(as, "boundingRectWithSize:options:context:", as_boundingRect); IM(as, "drawWithRect:options:context:", as_drawWithRect);

#define K(var, str) iosrt_##var = rt_str_new(str);
    K(NSFontAttributeName, "NSFont") K(NSForegroundColorAttributeName, "NSColor") K(NSParagraphStyleAttributeName, "NSParagraphStyle")
    K(NSBackgroundColorAttributeName, "NSBackgroundColor") K(NSKernAttributeName, "NSKern") K(NSShadowAttributeName, "NSShadow")
    K(NSStrokeColorAttributeName, "NSStrokeColor") K(NSStrokeWidthAttributeName, "NSStrokeWidth") K(NSUnderlineStyleAttributeName, "NSUnderline")
    K(NSBaselineOffsetAttributeName, "NSBaselineOffset") K(NSLinkAttributeName, "NSLink") K(NSAttachmentAttributeName, "NSAttachment")
    K(NSStrikethroughStyleAttributeName, "NSStrikethrough") K(NSLigatureAttributeName, "NSLigature") K(NSObliquenessAttributeName, "NSObliqueness")
    K(UIFontTextStyleBody, "UICTFontTextStyleBody") K(UIFontTextStyleHeadline, "UICTFontTextStyleHeadline") K(UIFontTextStyleSubheadline, "UICTFontTextStyleSubhead")
    K(UIFontTextStyleFootnote, "UICTFontTextStyleFootnote") K(UIFontTextStyleCaption1, "UICTFontTextStyleCaption1") K(UIFontTextStyleCaption2, "UICTFontTextStyleCaption2")
    K(UIFontTextStyleTitle1, "UICTFontTextStyleTitle1") K(UIFontTextStyleTitle2, "UICTFontTextStyleTitle2") K(UIFontTextStyleTitle3, "UICTFontTextStyleTitle3")
    K(UIFontDescriptorNameAttribute, "NSFontNameAttribute") K(UIFontDescriptorSizeAttribute, "NSFontSizeAttribute")
    K(UIFontDescriptorFamilyAttribute, "NSFontFamilyAttribute") K(UIFontDescriptorTraitsAttribute, "NSCTFontTraitsAttribute")
    K(UIFontSymbolicTrait, "NSCTFontSymbolicTrait") K(UIFontWeightTrait, "NSCTFontWeightTrait")
#undef K
}
