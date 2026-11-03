/*
 * CoreText on top of the runtime's text engine (stb_truetype fonts).
 *
 * CTFontRef is a UIFont (they are toll-free bridged on iOS). Glyph ids are
 * the UTF-16 code units of the text: there is no shaping, so one unit is
 * one glyph, which keeps index <-> glyph mappings trivial for callers.
 * Lines are split into runs of equal attributes (font, color); frames wrap
 * lines greedily at spaces inside the path's bounding box, in CoreText's
 * y-up coordinates (first baseline at the top). Drawing goes through the
 * context's text matrix and CTM; glyphs are always drawn upright.
 */
#include "uikit_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

RT_DECLARE_CLASS(NSObject);
RT_DEFINE_CLASS(CTLineImpl);
RT_DEFINE_CLASS(CTRunImpl);
RT_DEFINE_CLASS(CTFramesetterImpl);
RT_DEFINE_CLASS(CTFrameImpl);
RT_DEFINE_CLASS(CTParagraphStyleImpl);
typedef struct { long location, length; } CFRange;
typedef uint16_t CGGlyph;

id iosrt_kCTFontAttributeName, iosrt_kCTForegroundColorAttributeName, iosrt_kCTForegroundColorFromContextAttributeName,
   iosrt_kCTParagraphStyleAttributeName, iosrt_kCTKernAttributeName, iosrt_kCTUnderlineStyleAttributeName,
   iosrt_kCTStrokeWidthAttributeName, iosrt_kCTStrokeColorAttributeName, iosrt_kCTLigatureAttributeName,
   iosrt_kCTFontNameAttribute, iosrt_kCTFontSizeAttribute, iosrt_kCTFontFamilyNameAttribute, iosrt_kCTFontTraitsAttribute,
   iosrt_kCTFontSymbolicTrait, iosrt_kCTFrameProgressionAttributeName;
extern id iosrt_NSFontAttributeName, iosrt_NSForegroundColorAttributeName, iosrt_NSParagraphStyleAttributeName;

gfx_mat iosrt_CGContextGetTextMatrix(struct cgctx *c);
void iosrt_CGContextSetTextPosition(struct cgctx *c, double x, double y);
CGRect iosrt_CGPathGetBoundingBox(id p);
void rt_color_rgba(id color, float out[4]);
void rt_cg_fill_color(struct cgctx *c, float out[4]);

/* ================================================================ fonts */
static id default_ct_font(void) { return rt_font_make(12, 0, rt_str("Helvetica")); }
static void fmetrics(id font, double *asc, double *desc, double *lead) {
    float a = 0.8f, d = 0.2f, g = 0;
    struct gfx_font *gf = rt_font_gfx(font, NULL);
    if (gf) gfx_font_vmetrics(gf, &a, &d, &g);
    double sz = rt_font_size(font);
    if (asc) *asc = a * sz;
    if (desc) *desc = d * sz;
    if (lead) *lead = g * sz;
}
id iosrt_CTFontCreateWithName(id name, double size, const void *matrix) {
    (void)matrix; if (size <= 0) size = 12;
    const char *n = name ? rt_str_utf8(name) : "";
    int bold = strstr(n, "Bold") || strstr(n, "Heavy") || strstr(n, "Black") || strstr(n, "Semibold");
    return objc_retain(rt_font_make(size, bold, name));
}
id iosrt_CTFontCreateWithFontDescriptor(id desc, double size, const void *matrix) {
    id name = desc && rt_responds(desc, rt_sel("postscriptName")) ? RT_SEND0(id, desc, "postscriptName") : NULL;
    if (size <= 0 && desc && rt_responds(desc, rt_sel("pointSize"))) size = RT_SEND0(double, desc, "pointSize");
    return iosrt_CTFontCreateWithName(name, size, matrix);
}
id iosrt_CTFontCreateUIFontForLanguage(uint32_t type, double size, id lang) { (void)lang; return objc_retain(rt_font_make(size > 0 ? size : 12, type == 3 || type == 4, NULL)); }
id iosrt_CTFontCreateCopyWithAttributes(id font, double size, const void *m, id desc) { (void)m; (void)desc; return objc_retain(rt_font_make(size > 0 ? size : rt_font_size(font), ((struct uifont *)font)->bold, ((struct uifont *)font)->name)); }
id iosrt_CTFontCreateCopyWithSymbolicTraits(id font, double size, const void *m, uint32_t value, uint32_t mask) {
    (void)m; struct uifont *f = (struct uifont *)font;
    int bold = (mask & 2) ? (value & 2) != 0 : (int)f->bold;
    return objc_retain(rt_font_make(size > 0 ? size : f->size, bold, f->name));
}
double iosrt_CTFontGetSize(id f) { return f ? rt_font_size(f) : 0; }
double iosrt_CTFontGetAscent(id f) { double a; fmetrics(f, &a, NULL, NULL); return a; }
double iosrt_CTFontGetDescent(id f) { double d; fmetrics(f, NULL, &d, NULL); return d; }
double iosrt_CTFontGetLeading(id f) { double l; fmetrics(f, NULL, NULL, &l); return l; }
double iosrt_CTFontGetXHeight(id f) { return 0.52 * rt_font_size(f); }
double iosrt_CTFontGetCapHeight(id f) { return 0.7 * rt_font_size(f); }
unsigned iosrt_CTFontGetUnitsPerEm(id f) { (void)f; return 2048; }
double iosrt_CTFontGetUnderlinePosition(id f) { return -0.1 * rt_font_size(f); }
double iosrt_CTFontGetUnderlineThickness(id f) { return 0.05 * rt_font_size(f); }
double iosrt_CTFontGetSlantAngle(id f) { (void)f; return 0; }
CGRect iosrt_CTFontGetBoundingBox(id f) { double a, d; fmetrics(f, &a, &d, NULL); double s = rt_font_size(f); return (CGRect){ { -0.1 * s, -d }, { 1.2 * s, a + d } }; }
uint32_t iosrt_CTFontGetSymbolicTraits(id f) { return f && ((struct uifont *)f)->bold ? 2 : 0; }
static id fname(id f) { return f ? RT_SEND0(id, f, "fontName") : rt_str("Helvetica"); }
id iosrt_CTFontCopyPostScriptName(id f) { return objc_retain(fname(f)); }
id iosrt_CTFontCopyFamilyName(id f) { return objc_retain(f ? RT_SEND0(id, f, "familyName") : rt_str("Helvetica")); }
id iosrt_CTFontCopyFullName(id f) { return objc_retain(fname(f)); }
id iosrt_CTFontCopyDisplayName(id f) { return objc_retain(fname(f)); }
id iosrt_CTFontCopyName(id f, id key) { (void)key; return objc_retain(fname(f)); }
id iosrt_CTFontCopyFontDescriptor(id f) { return objc_retain(RT_SEND0(id, f, "fontDescriptor")); }
id iosrt_CTFontDescriptorCreateWithNameAndSize(id name, double size) { return objc_retain(RT_SEND2(id, (id)rt_class_named("UIFontDescriptor"), "fontDescriptorWithName:size:", id, name, double, size)); }
id iosrt_CTFontDescriptorCreateWithAttributes(id attrs) { return objc_retain(RT_SEND1(id, (id)rt_class_named("UIFontDescriptor"), "fontDescriptorWithFontAttributes:", id, attrs)); }
id iosrt_CTFontDescriptorCopyAttribute(id d, id key) { id attrs = d && rt_responds(d, rt_sel("fontAttributes")) ? RT_SEND0(id, d, "fontAttributes") : NULL; return objc_retain(attrs && key ? rt_dict_get(attrs, key) : NULL); }
/* glyphs are UTF-16 code units (see the header comment) */
rt_bool iosrt_CTFontGetGlyphsForCharacters(id f, const unichar *chars, CGGlyph *glyphs, long n) {
    (void)f; for (long i = 0; i < n; i++) glyphs[i] = chars[i];
    return 1;
}
static double glyph_advance(id font, CGGlyph g) {
    char b[8]; size_t k = 0; unsigned cp = g;
    if (cp < 0x80) b[k++] = (char)cp;
    else if (cp < 0x800) { b[k++] = (char)(0xc0 | cp >> 6); b[k++] = (char)(0x80 | (cp & 63)); }
    else { b[k++] = (char)(0xe0 | cp >> 12); b[k++] = (char)(0x80 | (cp >> 6 & 63)); b[k++] = (char)(0x80 | (cp & 63)); }
    return gfx_text_width(rt_font_gfx(font, NULL), b, k, (float)rt_font_size(font));
}
double iosrt_CTFontGetAdvancesForGlyphs(id f, int orient, const CGGlyph *g, CGSize *adv, long n) {
    (void)orient; double total = 0;
    for (long i = 0; i < n; i++) { double w = glyph_advance(f, g[i]); if (adv) adv[i] = (CGSize){ w, 0 }; total += w; }
    return total;
}
CGRect iosrt_CTFontGetBoundingRectsForGlyphs(id f, int orient, const CGGlyph *g, CGRect *rects, long n) {
    (void)orient; double a, d; fmetrics(f, &a, &d, NULL); double total = 0;
    for (long i = 0; i < n; i++) { double w = glyph_advance(f, g[i]); if (rects) rects[i] = (CGRect){ { 0, -d }, { w, a + d } }; total = fmax(total, w); }
    return (CGRect){ { 0, -d }, { total, a + d } };
}
/* glyph outlines are not exposed by the text engine: an empty path keeps callers going */
id iosrt_CTFontCreatePathForGlyph(id f, CGGlyph g, const void *m) { (void)f; (void)g; (void)m; return iosrt_CGPathCreateMutable(); }

/* ======================================================== paragraph style */
struct ctpara { Class isa; long align; double lineSpacing, lineHeightMultiple, paragraphSpacing; };
#define PS(x) ((struct ctpara *)(x))
struct ct_para_setting { uint32_t spec; size_t size; const void *value; };
id iosrt_CTParagraphStyleCreate(const void *raw_settings, size_t n) {
    const struct ct_para_setting *settings = raw_settings;
    id p = rt_alloc_instance(RT_CLASS(CTParagraphStyleImpl), 0);
    PS(p)->align = 4; PS(p)->lineHeightMultiple = 0;
    for (size_t i = 0; settings && i < n; i++) {
        const void *v = settings[i].value; if (!v) continue;
        switch (settings[i].spec) {
        case 0: PS(p)->align = *(const uint8_t *)v; break;                               /* kCTParagraphStyleSpecifierAlignment */
        case 6: PS(p)->lineHeightMultiple = *(const double *)v; break;
        case 9: case 14: case 15: PS(p)->lineSpacing = *(const double *)v; break;       /* line spacing variants */
        case 10: PS(p)->paragraphSpacing = *(const double *)v; break;
        }
    }
    return p;
}
rt_bool iosrt_CTParagraphStyleGetValueForSpecifier(id p, uint32_t spec, size_t size, void *out) {
    if (!p || !rt_is_kind_of(p, RT_CLASS(CTParagraphStyleImpl)) || !out) return 0;
    if (spec == 0 && size >= 1) { *(uint8_t *)out = (uint8_t)PS(p)->align; return 1; }
    if ((spec == 9 || spec == 14 || spec == 15) && size >= sizeof(double)) { *(double *)out = PS(p)->lineSpacing; return 1; }
    return 0;
}
/* alignment as a flush factor (0 left, .5 center, 1 right) for CT or NS paragraph styles */
static double para_flush(id para) {
    if (!para) return 0;
    if (rt_is_kind_of(para, RT_CLASS(CTParagraphStyleImpl))) { long a = PS(para)->align; return a == 1 ? 1 : a == 2 ? .5 : 0; }
    long a = rt_responds(para, rt_sel("alignment")) ? RT_SEND0(long, para, "alignment") : 0;   /* NSTextAlignment: 1 center, 2 right */
    return a == 1 ? .5 : a == 2 ? 1 : 0;
}
static double para_spacing(id para) {
    if (para && rt_is_kind_of(para, RT_CLASS(CTParagraphStyleImpl))) return PS(para)->lineSpacing;
    return para && rt_responds(para, rt_sel("lineSpacing")) ? RT_SEND0(double, para, "lineSpacing") : 0;
}

/* ==================================================================== runs */
struct ctrun {
    Class isa; id attrs, font; CFRange range; char *utf8; float col[4]; int has_color;
    double x, width, ascent, descent, leading; long n; CGGlyph *glyphs; CGPoint *pos; CGSize *adv; long *indices;
};
struct ctline { Class isa; id runs; CFRange range; double width, ascent, descent, leading, trailingWS; };
#define RUN(x) ((struct ctrun *)(x))
#define LINE(x) ((struct ctline *)(x))
static id attr_font(id attrs) { id f = attrs ? rt_dict_get(attrs, iosrt_kCTFontAttributeName) : NULL; return f ? f : default_ct_font(); }
static int attr_color(id attrs, float out[4]) {
    id c = attrs ? rt_dict_get(attrs, iosrt_kCTForegroundColorAttributeName) : NULL;
    if (!c && attrs) c = rt_dict_get(attrs, iosrt_NSForegroundColorAttributeName);
    if (!c) return 0;
    rt_color_rgba(c, out); return 1;
}
static void run_dealloc(id self, SEL s) {
    (void)s; struct ctrun *r = RUN(self);
    objc_release(r->attrs); objc_release(r->font); free(r->utf8); free(r->glyphs); free(r->pos); free(r->adv); free(r->indices);
    rt_dealloc_instance(self);
}
static void line_dealloc(id self, SEL s) { (void)s; objc_release(LINE(self)->runs); rt_dealloc_instance(self); }
static id substring16(id str, long loc, long len) {
    struct { unsigned long loc, len; } r = { (unsigned long)loc, (unsigned long)len };
    return RT_SEND1(id, str, "substringWithRange:", __typeof__(r), r);
}
/* a line over [loc, loc+len) of an attributed string */
static id make_line(id astr, long loc, long len) {
    id str = RT_SEND0(id, astr, "string");
    id line = rt_alloc_instance(RT_CLASS(CTLineImpl), 0);
    struct ctline *L = LINE(line);
    L->runs = RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableArray"), "alloc"), "init");
    L->range = (CFRange){ loc, len };
    double x = 0;
    for (long i = loc; i < loc + len;) {
        struct { unsigned long loc, len; } eff = { 0, 0 };
        id attrs = ((id (*)(id, SEL, unsigned long, void *))objc_msgSend)(astr, rt_sel("attributesAtIndex:effectiveRange:"), (unsigned long)i, &eff);
        long end = (long)(eff.loc + eff.len); if (end > loc + len || end <= i) end = loc + len;
        id run = rt_alloc_instance(RT_CLASS(CTRunImpl), 0);
        struct ctrun *R = RUN(run);
        R->attrs = objc_retain(attrs); R->font = objc_retain(attr_font(attrs)); R->has_color = attr_color(attrs, R->col);
        R->range = (CFRange){ i, end - i };
        id sub = substring16(str, i, end - i);
        R->utf8 = strdup(rt_str_utf8(sub));
        R->n = end - i;
        R->glyphs = calloc((size_t)R->n + 1, sizeof(CGGlyph)); R->pos = calloc((size_t)R->n + 1, sizeof(CGPoint));
        R->adv = calloc((size_t)R->n + 1, sizeof(CGSize)); R->indices = calloc((size_t)R->n + 1, sizeof(long));
        size_t n16; unichar *tofree = NULL; const unichar *u = rt_str_utf16(sub, &n16, &tofree);
        double gx = 0;
        for (long k = 0; k < R->n && (size_t)k < n16; k++) {
            R->glyphs[k] = u[k]; R->indices[k] = i + k;
            double w = (u[k] >= 0xdc00 && u[k] < 0xe000) ? 0 : glyph_advance(R->font, u[k]);
            R->pos[k] = (CGPoint){ x + gx, 0 }; R->adv[k] = (CGSize){ w, 0 };
            gx += w;
        }
        free(tofree);
        R->x = x; R->width = gx;
        fmetrics(R->font, &R->ascent, &R->descent, &R->leading);
        if (R->ascent > L->ascent) L->ascent = R->ascent;
        if (R->descent > L->descent) L->descent = R->descent;
        if (R->leading > L->leading) L->leading = R->leading;
        x += gx;
        RT_SEND1(void, L->runs, "addObject:", id, run); objc_release(run);
        i = end;
    }
    L->width = x;
    /* trailing whitespace width */
    double tw = 0;
    for (size_t ri = rt_array_count(L->runs); ri-- > 0;) {
        struct ctrun *R = RUN(rt_array_at(L->runs, ri)); long k = R->n;
        while (k > 0 && (R->glyphs[k - 1] == ' ' || R->glyphs[k - 1] == '\n' || R->glyphs[k - 1] == '\t')) { tw += R->adv[k - 1].width; k--; }
        if (k > 0) break;
    }
    L->trailingWS = tw;
    if (!rt_array_count(L->runs)) { id f = default_ct_font(); fmetrics(f, &L->ascent, &L->descent, &L->leading); }
    return line;
}
id iosrt_CTLineCreateWithAttributedString(id astr) {
    long len = astr ? (long)RT_SEND0(unsigned long, RT_SEND0(id, astr, "string"), "length") : 0;
    return make_line(astr, 0, len);
}
double iosrt_CTLineGetTypographicBounds(id line, double *asc, double *desc, double *lead) {
    struct ctline *L = LINE(line);
    if (asc) *asc = L->ascent;
    if (desc) *desc = L->descent;
    if (lead) *lead = L->leading;
    return L->width;
}
CGRect iosrt_CTLineGetImageBounds(id line, struct cgctx *c) {
    (void)c; struct ctline *L = LINE(line);
    gfx_mat tm = c ? iosrt_CGContextGetTextMatrix(c) : gfx_mat_identity();
    return (CGRect){ { tm.tx, tm.ty - L->descent }, { L->width - L->trailingWS, L->ascent + L->descent } };
}
CGRect iosrt_CTLineGetBoundsWithOptions(id line, unsigned long opts) { (void)opts; struct ctline *L = LINE(line); return (CGRect){ { 0, -L->descent }, { L->width, L->ascent + L->descent } }; }
double iosrt_CTLineGetTrailingWhitespaceWidth(id line) { return LINE(line)->trailingWS; }
long iosrt_CTLineGetGlyphCount(id line) { long n = 0; struct ctline *L = LINE(line); for (size_t i = 0; i < rt_array_count(L->runs); i++) n += RUN(rt_array_at(L->runs, i))->n; return n; }
id iosrt_CTLineGetGlyphRuns(id line) { return LINE(line)->runs; }
CFRange iosrt_CTLineGetStringRange(id line) { return LINE(line)->range; }
double iosrt_CTLineGetPenOffsetForFlush(id line, double flush, double width) { struct ctline *L = LINE(line); double w = L->width - L->trailingWS; return flush <= 0 ? 0 : fmax(0, (width - w) * (flush > 1 ? 1 : flush)); }
double iosrt_CTLineGetOffsetForStringIndex(id line, long idx, double *secondary) {
    struct ctline *L = LINE(line); double off = L->width;
    for (size_t i = 0; i < rt_array_count(L->runs); i++) {
        struct ctrun *R = RUN(rt_array_at(L->runs, i));
        if (idx >= R->range.location && idx < R->range.location + R->range.length) { off = R->pos[idx - R->range.location].x; break; }
    }
    if (idx <= L->range.location) off = 0;
    if (secondary) *secondary = off;
    return off;
}
long iosrt_CTLineGetStringIndexForPosition(id line, CGPoint p) {
    struct ctline *L = LINE(line);
    for (size_t i = 0; i < rt_array_count(L->runs); i++) {
        struct ctrun *R = RUN(rt_array_at(L->runs, i));
        for (long k = 0; k < R->n; k++) if (p.x < R->pos[k].x + R->adv[k].width / 2) return R->indices[k];
    }
    return L->range.location + L->range.length;
}
/* truncation: the line cut to width with the token (or "…") at the end */
id iosrt_CTLineCreateTruncatedLine(id line, double width, uint32_t type, id token) {
    (void)type; struct ctline *L = LINE(line);
    if (L->width - L->trailingWS <= width || !rt_array_count(L->runs)) return objc_retain(line);
    struct ctrun *R0 = RUN(rt_array_at(L->runs, 0));
    double ew = token ? LINE(token)->width : glyph_advance(R0->font, 0x2026);
    /* rebuild as a plain attributed string with the first run's attributes */
    rt_sb sb; rt_sb_init(&sb); double x = 0;
    for (size_t i = 0; i < rt_array_count(L->runs); i++) {
        struct ctrun *R = RUN(rt_array_at(L->runs, i));
        const char *p = R->utf8;
        for (long k = 0; k < R->n && *p; k++) {
            if (x + R->adv[k].width + ew > width) goto done;
            size_t cl = ((unsigned char)*p >= 0xf0) ? 4 : ((unsigned char)*p >= 0xe0) ? 3 : ((unsigned char)*p >= 0xc0) ? 2 : 1;
            rt_sb_append(&sb, p, cl); p += cl; x += R->adv[k].width;
        }
    }
done:
    rt_sb_puts(&sb, "\xe2\x80\xa6");
    char *t = rt_sb_take(&sb);
    id as = RT_SEND2(id, RT_SEND0(id, (id)rt_class_named("NSAttributedString"), "alloc"), "initWithString:attributes:", id, rt_str(t), id, R0->attrs);
    free(t);
    id r = iosrt_CTLineCreateWithAttributedString(as);
    objc_release(as);
    return r;
}
id iosrt_CTLineCreateJustifiedLine(id line, double factor, double width) { (void)factor; (void)width; return objc_retain(line); }

/* drawing: baseline at the text position, through the text matrix and the CTM */
static void draw_run(struct cgctx *c, struct ctrun *R, double ox, double oy, long from, long count) {
    if (!c || !R->utf8) return;
    float col[4];
    if (R->has_color) memcpy(col, R->col, sizeof col);
    else { col[0] = col[1] = col[2] = 0; col[3] = 1; }
    col[3] *= rt_cg_alpha(c);
    gfx_mat tm = iosrt_CGContextGetTextMatrix(c), m = rt_cg_ctm(c);
    double k = sqrt(fabs(m.a * m.d - m.b * m.c)) * sqrt(fabs(tm.a * tm.d - tm.b * tm.c));
    if (k <= 0) k = 1;
    /* the byte range of glyphs [from, from+count) */
    const char *p = R->utf8; long gi = 0; size_t b0 = 0, b1 = strlen(R->utf8);
    for (const char *q = p; *q; ) {
        size_t cl = ((unsigned char)*q >= 0xf0) ? 4 : ((unsigned char)*q >= 0xe0) ? 3 : ((unsigned char)*q >= 0xc0) ? 2 : 1;
        if (gi == from) b0 = (size_t)(q - p);
        gi += cl == 4 ? 2 : 1; q += cl;
        if (gi >= from + count) { b1 = (size_t)(q - p); break; }
    }
    if (from >= R->n || b1 <= b0) return;
    double ux = ox + R->pos[from].x, uy = oy, tx, ty, dx, dy;
    gfx_mat_apply(&tm, ux, uy, &tx, &ty);
    gfx_mat_apply(&m, tx, ty, &dx, &dy);
    int fake_bold; struct gfx_font *gf = rt_font_gfx(R->font, &fake_bold);
    float size = (float)(rt_font_size(R->font) * k);
    gfx_draw_text(rt_cg_surface(c), gf, R->utf8 + b0, b1 - b0, (float)dx, (float)dy, size, col, fake_bold && size > 6);
    double sc; rt_sb *tr = rt_cg_trace(c, &sc);
    if (tr) {
        if (sc <= 0) sc = 1;
        char *t = strndup(R->utf8 + b0, b1 - b0);
        rt_cg_trace_line(c, "ctrun %g,%g size=%g rgba(%d,%d,%d,%.2f) \"%s\"", round(dx / sc * 100) / 100, round(dy / sc * 100) / 100, round(size / sc * 100) / 100,
                         (int)lroundf(col[0] * 255), (int)lroundf(col[1] * 255), (int)lroundf(col[2] * 255), col[3], t);
        free(t);
    }
}
void iosrt_CTLineDraw(id line, struct cgctx *c) {
    if (!line || !c) return;
    struct ctline *L = LINE(line);
    gfx_mat tm = iosrt_CGContextGetTextMatrix(c);
    /* runs are positioned from the text position; the matrix's translation is the pen */
    for (size_t i = 0; i < rt_array_count(L->runs); i++) {
        struct ctrun *R = RUN(rt_array_at(L->runs, i));
        draw_run(c, R, 0, 0, 0, R->n);
    }
    iosrt_CGContextSetTextPosition(c, tm.tx + L->width * tm.a, tm.ty + L->width * tm.b);
}
long iosrt_CTRunGetGlyphCount(id run) { return RUN(run)->n; }
id iosrt_CTRunGetAttributes(id run) { return RUN(run)->attrs; }
CFRange iosrt_CTRunGetStringRange(id run) { return RUN(run)->range; }
uint32_t iosrt_CTRunGetStatus(id run) { (void)run; return 0; }
const CGGlyph *iosrt_CTRunGetGlyphsPtr(id run) { return RUN(run)->glyphs; }
const CGPoint *iosrt_CTRunGetPositionsPtr(id run) { return RUN(run)->pos; }
const CGSize *iosrt_CTRunGetAdvancesPtr(id run) { return RUN(run)->adv; }
const long *iosrt_CTRunGetStringIndicesPtr(id run) { return RUN(run)->indices; }
static void clamp_range(id run, CFRange *r) { if (r->length == 0) { r->location = 0; r->length = RUN(run)->n; } if (r->location < 0) r->location = 0; if (r->location + r->length > RUN(run)->n) r->length = RUN(run)->n - r->location; }
void iosrt_CTRunGetGlyphs(id run, CFRange r, CGGlyph *out) { clamp_range(run, &r); memcpy(out, RUN(run)->glyphs + r.location, sizeof(CGGlyph) * (size_t)r.length); }
void iosrt_CTRunGetPositions(id run, CFRange r, CGPoint *out) { clamp_range(run, &r); memcpy(out, RUN(run)->pos + r.location, sizeof(CGPoint) * (size_t)r.length); }
void iosrt_CTRunGetAdvances(id run, CFRange r, CGSize *out) { clamp_range(run, &r); memcpy(out, RUN(run)->adv + r.location, sizeof(CGSize) * (size_t)r.length); }
void iosrt_CTRunGetStringIndices(id run, CFRange r, long *out) { clamp_range(run, &r); memcpy(out, RUN(run)->indices + r.location, sizeof(long) * (size_t)r.length); }
double iosrt_CTRunGetTypographicBounds(id run, CFRange r, double *asc, double *desc, double *lead) {
    clamp_range(run, &r); struct ctrun *R = RUN(run);
    if (asc) *asc = R->ascent;
    if (desc) *desc = R->descent;
    if (lead) *lead = R->leading;
    double w = 0; for (long k = r.location; k < r.location + r.length; k++) w += R->adv[k].width;
    return w;
}
CGRect iosrt_CTRunGetImageBounds(id run, struct cgctx *c, CFRange r) { (void)c; double a, d; double w = iosrt_CTRunGetTypographicBounds(run, r, &a, &d, NULL); return (CGRect){ { RUN(run)->x, -d }, { w, a + d } }; }
gfx_mat iosrt_CTRunGetTextMatrix(id run) { (void)run; return gfx_mat_identity(); }
void iosrt_CTRunDraw(id run, struct cgctx *c, CFRange r) { clamp_range(run, &r); draw_run(c, RUN(run), 0, 0, r.location, r.length); }

/* glyph drawing (CTFontDrawGlyphs / CGContextShowGlyphsAtPositions): glyph ids are UTF-16 units */
void iosrt_CTFontDrawGlyphs(id font, const CGGlyph *g, const CGPoint *pos, size_t n, struct cgctx *c) {
    if (!c || !g) return;
    for (size_t i = 0; i < n; i++) {
        unichar u = g[i];
        id s = objc_autorelease(rt_str_new_utf16(&u, 1));
        struct ctrun R; memset(&R, 0, sizeof R);
        R.font = font ? font : default_ct_font(); R.utf8 = (char *)rt_str_utf8(s); R.n = 1;
        CGPoint p0 = { 0, 0 }; R.pos = pos ? (CGPoint *)&pos[i] : &p0;
        float fc[4]; rt_cg_fill_color(c, fc); memcpy(R.col, fc, sizeof fc); R.has_color = 1;
        draw_run(c, &R, 0, 0, 0, 1);
    }
}

/* =========================================================== framesetter */
struct ctfs { Class isa; id astr; };
struct ctframe { Class isa; id lines; CGPoint *origins; long n; CFRange range, visible; id path; };
#define FS(x) ((struct ctfs *)(x))
#define FR(x) ((struct ctframe *)(x))
static void fs_dealloc(id self, SEL s) { (void)s; objc_release(FS(self)->astr); rt_dealloc_instance(self); }
static void frame_dealloc(id self, SEL s) { (void)s; objc_release(FR(self)->lines); objc_release(FR(self)->path); free(FR(self)->origins); rt_dealloc_instance(self); }
id iosrt_CTFramesetterCreateWithAttributedString(id astr) { id f = rt_alloc_instance(RT_CLASS(CTFramesetterImpl), 0); FS(f)->astr = RT_SEND0(id, astr, "copy"); return f; }
id iosrt_CTTypesetterCreateWithAttributedString(id astr) { return iosrt_CTFramesetterCreateWithAttributedString(astr); }
id iosrt_CTFramesetterGetTypesetter(id fs) { return fs; }
/* greedy line breaking of [loc, loc+len) at width (<= 0: paragraphs only); calls emit(start, length) */
static long break_lines(id astr, long loc, long len, double width, void (*emit)(void *, long, long), void *ctx, long maxlines) {
    id full = make_line(astr, loc, len);   /* per-unit advances over the whole range */
    struct ctline *L = LINE(full);
    long total = len, nlines = 0;
    double *adv = calloc((size_t)total + 1, sizeof(double)); CGGlyph *ch = calloc((size_t)total + 1, sizeof(CGGlyph));
    for (size_t i = 0; i < rt_array_count(L->runs); i++) {
        struct ctrun *R = RUN(rt_array_at(L->runs, i));
        for (long k = 0; k < R->n; k++) { long at = R->range.location + k - loc; if (at >= 0 && at < total) { adv[at] = R->adv[k].width; ch[at] = R->glyphs[k]; } }
    }
    long start = 0;
    while (start < total && (maxlines <= 0 || nlines < maxlines)) {
        double x = 0; long i = start, lastSpace = -1, end = -1;
        for (; i < total; i++) {
            if (ch[i] == '\n' || ch[i] == 0x2028 || ch[i] == 0x2029) { end = i + 1; break; }
            if (ch[i] == ' ' || ch[i] == '\t') lastSpace = i;
            if (width > 0 && x + adv[i] > width + 0.01 && ch[i] != ' ' && i > start) {
                if (lastSpace >= start) { end = lastSpace + 1; while (end < total && ch[end] == ' ') end++; }
                else end = i;
                break;
            }
            x += adv[i];
        }
        if (end < 0) end = total;
        emit(ctx, loc + start, end - start);
        nlines++;
        start = end;
    }
    free(adv); free(ch); objc_release(full);
    return start;   /* units consumed */
}
struct collect { id astr; id lines; };
static void collect_line(void *p, long s, long n) {
    struct collect *c = p;
    id line = make_line(c->astr, s, n);
    RT_SEND1(void, c->lines, "addObject:", id, line); objc_release(line);
}
static CFRange norm_range(id astr, CFRange r) {
    long len = (long)RT_SEND0(unsigned long, RT_SEND0(id, astr, "string"), "length");
    if (r.length == 0 || r.location + r.length > len) r.length = len - r.location;
    if (r.location < 0) r.location = 0;
    return r;
}
static id frame_para(id astr) {
    long len = (long)RT_SEND0(unsigned long, RT_SEND0(id, astr, "string"), "length");
    if (!len) return NULL;
    id attrs = ((id (*)(id, SEL, unsigned long, void *))objc_msgSend)(astr, rt_sel("attributesAtIndex:effectiveRange:"), 0, NULL);
    return attrs ? rt_dict_get(attrs, iosrt_kCTParagraphStyleAttributeName) : NULL;
}
id iosrt_CTFramesetterCreateFrame(id fs, CFRange range, id path, id attrs) {
    (void)attrs; id astr = FS(fs)->astr;
    range = norm_range(astr, range);
    CGRect box = path ? iosrt_CGPathGetBoundingBox(path) : (CGRect){ { 0, 0 }, { 0, 0 } };
    struct collect col = { astr, RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableArray"), "alloc"), "init") };
    break_lines(astr, range.location, range.length, box.size.width, collect_line, &col, 0);
    id frame = rt_alloc_instance(RT_CLASS(CTFrameImpl), 0);
    struct ctframe *F = FR(frame);
    F->path = objc_retain(path); F->range = range;
    id para = frame_para(astr);
    double flush = para_flush(para), spacing = para_spacing(para);
    /* keep the lines that fit, top to bottom (y-up: first baseline below the top edge) */
    F->lines = RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableArray"), "alloc"), "init");
    size_t n = rt_array_count(col.lines);
    F->origins = calloc(n + 1, sizeof(CGPoint));
    double y = box.origin.y + box.size.height;
    long visEnd = range.location;
    for (size_t i = 0; i < n; i++) {
        id line = rt_array_at(col.lines, i); struct ctline *L = LINE(line);
        double base = y - L->ascent;
        if (i > 0 && base - L->descent < box.origin.y - 0.5) break;
        F->origins[F->n] = (CGPoint){ box.origin.x + iosrt_CTLineGetPenOffsetForFlush(line, flush, box.size.width), base };
        F->n++;
        RT_SEND1(void, F->lines, "addObject:", id, line);
        visEnd = L->range.location + L->range.length;
        y = base - L->descent - L->leading - spacing;
    }
    F->visible = (CFRange){ range.location, visEnd - range.location };
    objc_release(col.lines);
    return frame;
}
CGSize iosrt_CTFramesetterSuggestFrameSizeWithConstraints(id fs, CFRange range, id attrs, CGSize cons, CFRange *fit) {
    (void)attrs; id astr = FS(fs)->astr;
    range = norm_range(astr, range);
    double w = cons.width > 0 && cons.width < 1e7 ? cons.width : 0;
    struct collect col = { astr, RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableArray"), "alloc"), "init") };
    break_lines(astr, range.location, range.length, w, collect_line, &col, 0);
    double spacing = para_spacing(frame_para(astr));
    double h = 0, maxw = 0; long end = range.location;
    for (size_t i = 0; i < rt_array_count(col.lines); i++) {
        struct ctline *L = LINE(rt_array_at(col.lines, i));
        double lh = L->ascent + L->descent + L->leading + (i ? spacing : 0);
        if (cons.height > 0 && cons.height < 1e7 && h + lh > cons.height + 0.5 && i > 0) break;
        h += lh; maxw = fmax(maxw, L->width - L->trailingWS);
        end = L->range.location + L->range.length;
    }
    objc_release(col.lines);
    if (fit) *fit = (CFRange){ range.location, end - range.location };
    return (CGSize){ ceil(maxw), ceil(h) };
}
id iosrt_CTFrameGetLines(id frame) { return FR(frame)->lines; }
void iosrt_CTFrameGetLineOrigins(id frame, CFRange r, CGPoint *out) {
    struct ctframe *F = FR(frame);
    if (r.length == 0) { r.location = 0; r.length = F->n; }
    for (long i = 0; i < r.length && r.location + i < F->n; i++) {
        /* origins are relative to the path's bounding box origin */
        CGRect box = F->path ? iosrt_CGPathGetBoundingBox(F->path) : (CGRect){ { 0, 0 }, { 0, 0 } };
        out[i] = (CGPoint){ F->origins[r.location + i].x - box.origin.x, F->origins[r.location + i].y - box.origin.y };
    }
}
CFRange iosrt_CTFrameGetStringRange(id frame) { return FR(frame)->range; }
CFRange iosrt_CTFrameGetVisibleStringRange(id frame) { return FR(frame)->visible; }
id iosrt_CTFrameGetPath(id frame) { return FR(frame)->path; }
void iosrt_CTFrameDraw(id frame, struct cgctx *c) {
    struct ctframe *F = FR(frame);
    for (long i = 0; i < F->n; i++) {
        iosrt_CGContextSetTextPosition(c, F->origins[i].x, F->origins[i].y);
        iosrt_CTLineDraw(rt_array_at(F->lines, (size_t)i), c);
    }
}
/* typesetter: line breaking helpers */
long iosrt_CTTypesetterSuggestLineBreak(id ts, long start, double width) {
    id astr = FS(ts)->astr; long len = (long)RT_SEND0(unsigned long, RT_SEND0(id, astr, "string"), "length");
    struct { long first; } st = { -1 };
    struct collect col = { astr, RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableArray"), "alloc"), "init") };
    break_lines(astr, start, len - start, width, collect_line, &col, 1);
    long n = rt_array_count(col.lines) ? LINE(rt_array_at(col.lines, 0))->range.length : len - start;
    objc_release(col.lines); (void)st;
    return n;
}
id iosrt_CTTypesetterCreateLine(id ts, CFRange r) { id astr = FS(ts)->astr; r = norm_range(astr, r); return make_line(astr, r.location, r.length); }

void rt_coretext_init(void) {
    Class o = RT_CLASS(NSObject);
    NEWCLASS(CTLineImpl, o, sizeof(struct ctline) - sizeof(void *)); IM(RT_CLASS(CTLineImpl), "dealloc", line_dealloc);
    NEWCLASS(CTRunImpl, o, sizeof(struct ctrun) - sizeof(void *)); IM(RT_CLASS(CTRunImpl), "dealloc", run_dealloc);
    NEWCLASS(CTFramesetterImpl, o, sizeof(struct ctfs) - sizeof(void *)); IM(RT_CLASS(CTFramesetterImpl), "dealloc", fs_dealloc);
    NEWCLASS(CTFrameImpl, o, sizeof(struct ctframe) - sizeof(void *)); IM(RT_CLASS(CTFrameImpl), "dealloc", frame_dealloc);
    NEWCLASS(CTParagraphStyleImpl, o, sizeof(struct ctpara) - sizeof(void *));
    /* attribute names CoreText shares with UIKit, and its own */
    iosrt_kCTFontAttributeName = iosrt_NSFontAttributeName;
    iosrt_kCTParagraphStyleAttributeName = iosrt_NSParagraphStyleAttributeName;
    iosrt_kCTForegroundColorAttributeName = rt_str_new("CTForegroundColor");
    iosrt_kCTForegroundColorFromContextAttributeName = rt_str_new("CTForegroundColorFromContext");
    iosrt_kCTKernAttributeName = rt_str_new("NSKern");
    iosrt_kCTUnderlineStyleAttributeName = rt_str_new("NSUnderline");
    iosrt_kCTStrokeWidthAttributeName = rt_str_new("NSStrokeWidth");
    iosrt_kCTStrokeColorAttributeName = rt_str_new("CTStrokeColor");
    iosrt_kCTLigatureAttributeName = rt_str_new("NSLigature");
    iosrt_kCTFontNameAttribute = rt_str_new("NSFontNameAttribute");
    iosrt_kCTFontSizeAttribute = rt_str_new("NSFontSizeAttribute");
    iosrt_kCTFontFamilyNameAttribute = rt_str_new("NSFontFamilyAttribute");
    iosrt_kCTFontTraitsAttribute = rt_str_new("NSCTFontTraitsAttribute");
    iosrt_kCTFontSymbolicTrait = rt_str_new("NSCTFontSymbolicTrait");
    iosrt_kCTFrameProgressionAttributeName = rt_str_new("CTFrameProgression");
}
