/*
 * UIImage: bundle lookup with @2x/@3x/~iphone variants (imageNamed: caches),
 * file/data decoding through stb_image (handles Xcode's "crushed" CgBI PNGs),
 * CGImage interop, resizable (9-slice) images, template rendering, drawing,
 * and PNG/JPEG encoding.
 */
#include "uikit_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSBundle);
RT_DEFINE_CLASS(UIImage);

id rt_data_new(const void *b, size_t n);
const void *rt_data_bytes(id d, size_t *n);

struct uiimage { Class isa; id cg; double scale; long orientation; UIEdgeInsets caps; long resizable, resizingMode, renderingMode; id name; };

static id img_alloc(Class c) { id r = rt_alloc_instance(c ? c : RT_CLASS(UIImage), 0); ((struct uiimage *)r)->scale = 1; return r; }
static void img_dealloc(id self, SEL s) { (void)s; struct uiimage *u = (struct uiimage *)self; objc_release(u->cg); objc_release(u->name); rt_dealloc_instance(self); }
id rt_uiimage_from_cg(id cg, double scale) {
    if (!cg) return NULL;
    id r = img_alloc(NULL); struct uiimage *u = (struct uiimage *)r;
    u->cg = objc_retain(cg); u->scale = scale > 0 ? scale : 1;
    return objc_autorelease(r);
}
id rt_uiimage_cgimage(id img) { return img && rt_is_kind_of(img, RT_CLASS(UIImage)) ? ((struct uiimage *)img)->cg : NULL; }
double rt_uiimage_scale(id img) { return img ? ((struct uiimage *)img)->scale : 1; }
CGSize rt_uiimage_size(id img) {
    struct uiimage *u = (struct uiimage *)img;
    struct gfx_image *g = u ? rt_cgimage_gfx(u->cg) : NULL;
    if (!g) return (CGSize){ 0, 0 };
    CGSize s = { g->w / u->scale, g->h / u->scale };
    if (u->orientation == 2 || u->orientation == 3 || u->orientation == 6 || u->orientation == 7) { double t = s.width; s.width = s.height; s.height = t; }  /* Left/Right(+Mirrored) */
    return s;
}

/* ---- loading */
static int file_exists(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISREG(st.st_mode); }
static id load_file(const char *path, double scale) {
    struct gfx_image *g = gfx_image_load_file(path);
    if (!g) return NULL;
    id cg = rt_cgimage_new(g); gfx_image_release(g);
    id ui = rt_uiimage_from_cg(cg, scale); objc_release(cg);
    return ui;
}
static double scale_from_name(const char *p) {
    const char *at = strrchr(p, '@');
    if (at && (at[1] == '2' || at[1] == '3') && at[2] == 'x') return at[1] - '0';
    return 1;
}
/* "dir/name[.ext]" -> try name@Nx~iphone.ext etc. in `dir`; returns autoreleased UIImage */
static id lookup(const char *dir, const char *name) {
    char base[1024], ext[32] = "";
    snprintf(base, sizeof base, "%s", name);
    char *slash = strrchr(base, '/'), *dot = strrchr(base, '.');
    if (dot && (!slash || dot > slash) && strlen(dot) <= 5) { snprintf(ext, sizeof ext, "%s", dot + 1); *dot = 0; }
    /* explicit scale suffix in the name */
    char *at = strrchr(base, '@'); double fixed = 0;
    if (at && (at[1] == '2' || at[1] == '3') && at[2] == 'x' && !at[3]) { fixed = at[1] - '0'; *at = 0; }
    int screen = (int)rt_scr_scale; if (screen < 1) screen = 1; if (screen > 3) screen = 3;
    int scales[4] = { screen, screen == 3 ? 2 : 3, 1, 0 };
    if (fixed) { scales[0] = (int)fixed; scales[1] = 0; }
    const char *exts[4] = { ext[0] ? ext : "png", ext[0] ? NULL : "jpg", ext[0] ? NULL : "jpeg", NULL };
    const char *idioms[2] = { "~iphone", "" };
    char p[4096];
    for (int si = 0; si < 4 && (scales[si] || si == 3); si++) {
        int sc = scales[si] ? scales[si] : 1;
        if (si == 3 && fixed) break;
        for (int ii = 0; ii < 2; ii++)
            for (int ei = 0; ei < 3 && exts[ei]; ei++) {
                char suf[8] = ""; if (sc > 1 || fixed) snprintf(suf, sizeof suf, "@%dx", sc);
                if (si == 3) suf[0] = 0;
                snprintf(p, sizeof p, "%s/%s%s%s.%s", dir, base, suf, idioms[ii], exts[ei]);
                if (file_exists(p)) return load_file(p, suf[0] ? sc : 1);
            }
        if (si == 3) break;
    }
    return NULL;
}
static id g_cache;   /* name -> UIImage */
static id im_named(Class c, SEL s, id name) {
    (void)c; (void)s;
    if (!name || !RT_SEND0(unsigned long, name, "length")) return NULL;
    if (!g_cache) g_cache = RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableDictionary"), "alloc"), "init");
    id hit = rt_dict_get(g_cache, name);
    if (hit) return hit;
    id bundle = RT_SEND0(id, (id)RT_CLASS(NSBundle), "mainBundle");
    const char *dir = rt_str_utf8(RT_SEND0(id, bundle, "bundlePath"));
    id img = lookup(dir, rt_str_utf8(name));
    if (!img) {
        /* localized resources */
        char d2[2048]; snprintf(d2, sizeof d2, "%s/Base.lproj", dir); img = lookup(d2, rt_str_utf8(name));
        if (!img) { snprintf(d2, sizeof d2, "%s/en.lproj", dir); img = lookup(d2, rt_str_utf8(name)); }
    }
    if (!img) {
        if (rt_first_time(rt_str_utf8(name))) rt_log("UIImage imageNamed: '%s' not found (asset catalogs are not supported)", rt_str_utf8(name));
        return NULL;
    }
    ((struct uiimage *)img)->name = objc_retain(name);
    RT_SEND2(void, g_cache, "setObject:forKey:", id, img, id, name);
    return img;
}
static id im_namedBundle(Class c, SEL s, id name, id bundle, id traits) { (void)bundle; (void)traits; return im_named(c, s, name); }
static id im_withContentsOfFile(Class c, SEL s, id path) {
    (void)c; (void)s;
    if (!path) return NULL;
    const char *p = rt_str_utf8(path);
    /* on a retina screen iOS prefers name@2x.ext next to the given file */
    if (!strchr(strrchr(p, '/') ? strrchr(p, '/') : p, '@')) {
        char alt[4096]; const char *dot = strrchr(p, '.');
        int sc = (int)rt_scr_scale;
        for (int k = sc; k >= 2; k--) {
            if (dot) snprintf(alt, sizeof alt, "%.*s@%dx%s", (int)(dot - p), p, k, dot); else snprintf(alt, sizeof alt, "%s@%dx", p, k);
            if (file_exists(alt)) return load_file(alt, k);
        }
    }
    return file_exists(p) ? load_file(p, scale_from_name(p)) : NULL;
}
static id im_initWithContentsOfFile(id self, SEL s, id path) { objc_release(self); return objc_retain(im_withContentsOfFile(RT_CLASS(UIImage), s, path)); }
static id im_withDataScale(Class c, SEL s, id data, double scale) {
    (void)s;
    if (!data) return NULL;
    size_t n; const void *b = rt_data_bytes(data, &n);
    struct gfx_image *g = gfx_image_decode(b, n);
    if (!g) return NULL;
    id cg = rt_cgimage_new(g); gfx_image_release(g);
    id r = img_alloc(c); struct uiimage *u = (struct uiimage *)r; u->cg = cg; u->scale = scale > 0 ? scale : 1;
    return objc_autorelease(r);
}
static id im_withData(Class c, SEL s, id data) { return im_withDataScale(c, s, data, 1); }
static id im_initWithData(id self, SEL s, id data) { Class c = self->isa; objc_release(self); return objc_retain(im_withData(c, s, data)); }
static id im_initWithDataScale(id self, SEL s, id data, double sc) { Class c = self->isa; objc_release(self); return objc_retain(im_withDataScale(c, s, data, sc)); }
static id im_initWithCGImageSO(id self, SEL s, id cg, double scale, long orient) {
    (void)s; struct uiimage *u = (struct uiimage *)self;
    if (!cg) { objc_release(self); return NULL; }
    u->cg = objc_retain(cg); u->scale = scale > 0 ? scale : 1; u->orientation = orient; return self;
}
static id im_initWithCGImage(id self, SEL s, id cg) { return im_initWithCGImageSO(self, s, cg, 1, 0); }
static id im_withCGImage(Class c, SEL s, id cg) { return objc_autorelease(im_initWithCGImageSO(img_alloc(c), s, cg, 1, 0)); }
static id im_withCGImageSO(Class c, SEL s, id cg, double sc, long o) { return objc_autorelease(im_initWithCGImageSO(img_alloc(c), s, cg, sc, o)); }
static id im_CGImage(id self, SEL s) { (void)s; return ((struct uiimage *)self)->cg; }
static CGSize im_size(id self, SEL s) { (void)s; return rt_uiimage_size(self); }
static double im_scale(id self, SEL s) { (void)s; return ((struct uiimage *)self)->scale; }
static long im_orientation(id self, SEL s) { (void)s; return ((struct uiimage *)self)->orientation; }
static UIEdgeInsets im_capInsets(id self, SEL s) { (void)s; return ((struct uiimage *)self)->caps; }
static long im_resizingMode(id self, SEL s) { (void)s; return ((struct uiimage *)self)->resizingMode; }
static long im_renderingMode(id self, SEL s) { (void)s; return ((struct uiimage *)self)->renderingMode; }
static id im_clone(id self) {
    struct uiimage *u = (struct uiimage *)self;
    id r = img_alloc(self->isa); struct uiimage *v = (struct uiimage *)r;
    *v = *u; v->isa = self->isa; objc_retain(v->cg); objc_retain(v->name);
    return objc_autorelease(r);
}
static id im_resizable2(id self, SEL s, UIEdgeInsets e, long mode) {
    (void)s; id r = im_clone(self); struct uiimage *v = (struct uiimage *)r;
    v->caps = e; v->resizable = 1; v->resizingMode = mode; return r;
}
static id im_resizable(id self, SEL s, UIEdgeInsets e) { return im_resizable2(self, s, e, 0); }
static id im_stretchable(id self, SEL s, long left, long top) {
    CGSize sz = rt_uiimage_size(self);
    UIEdgeInsets e = { (double)top, (double)left, top ? sz.height - top - 1 : 0, left ? sz.width - left - 1 : 0 };
    if (e.bottom < 0) e.bottom = 0;
    if (e.right < 0) e.right = 0;
    return im_resizable2(self, s, e, 1);
}
static long im_leftCap(id self, SEL s) { (void)s; return (long)((struct uiimage *)self)->caps.left; }
static long im_topCap(id self, SEL s) { (void)s; return (long)((struct uiimage *)self)->caps.top; }
static id im_withRenderingMode(id self, SEL s, long mode) { (void)s; id r = im_clone(self); ((struct uiimage *)r)->renderingMode = mode; return r; }
static id im_self(id self, SEL s) { (void)s; return self; }
static id im_self1(id self, SEL s, UIEdgeInsets e) { (void)s; (void)e; return self; }
static id im_withTint(id self, SEL s, id color) {
    (void)s;
    struct gfx_image *g = rt_cgimage_gfx(((struct uiimage *)self)->cg);
    if (!g) return self;
    float c[4]; rt_color_rgba(color, c);
    struct gfx_image *n = gfx_image_new(g->w, g->h);
    for (size_t i = 0; i < (size_t)g->w * g->h; i++) {
        float a = (float)(g->px[i] >> 24) / 255.f * c[3];
        n->px[i] = (uint32_t)(c[0] * a * 255 + .5f) | (uint32_t)(c[1] * a * 255 + .5f) << 8 | (uint32_t)(c[2] * a * 255 + .5f) << 16 | (uint32_t)(a * 255 + .5f) << 24;
    }
    id cg = rt_cgimage_new(n); gfx_image_release(n);
    id r = rt_uiimage_from_cg(cg, ((struct uiimage *)self)->scale); objc_release(cg); return r;
}

/* ---- drawing */
void rt_uiimage_draw(id img, struct cgctx *c, CGRect r, float alpha, const float *tint) {
    struct uiimage *u = (struct uiimage *)img;
    if (!u || !c || !u->cg) return;
    if (u->renderingMode != 2) tint = NULL;   /* only AlwaysTemplate images are tinted */
    if (u->resizable && (u->caps.left > 0 || u->caps.right > 0 || u->caps.top > 0 || u->caps.bottom > 0)) {
        UIEdgeInsets px = { u->caps.top * u->scale, u->caps.left * u->scale, u->caps.bottom * u->scale, u->caps.right * u->scale };
        rt_cg_draw_image_9slice(c, u->cg, r, px, u->scale, alpha, tint, u->resizingMode == 0);
    } else rt_cg_draw_image_uikit(c, u->cg, r, alpha, rt_cg_blend(c), tint);
}
static void im_drawInRectBA(id self, SEL s, CGRect r, int mode, double a) {
    (void)s; struct cgctx *c = rt_cg_top(); if (!c) return;
    iosrt_CGContextSaveGState(c); iosrt_CGContextSetBlendMode(c, mode);
    rt_uiimage_draw(self, c, r, (float)a, NULL);
    iosrt_CGContextRestoreGState(c);
}
static void im_drawInRect(id self, SEL s, CGRect r) { im_drawInRectBA(self, s, r, 0, 1); }
static void im_drawAtPoint(id self, SEL s, CGPoint p) { CGSize sz = rt_uiimage_size(self); im_drawInRect(self, s, (CGRect){ p, sz }); }
static void im_drawAtPointBA(id self, SEL s, CGPoint p, int mode, double a) { CGSize sz = rt_uiimage_size(self); im_drawInRectBA(self, s, (CGRect){ p, sz }, mode, a); }
static void im_drawAsPattern(id self, SEL s, CGRect r) {
    (void)s; struct cgctx *c = rt_cg_top(); CGSize sz = rt_uiimage_size(self);
    if (!c || sz.width <= 0 || sz.height <= 0) return;
    iosrt_CGContextSaveGState(c); iosrt_CGContextClipToRect(c, r);
    for (double y = r.origin.y; y < r.origin.y + r.size.height; y += sz.height)
        for (double x = r.origin.x; x < r.origin.x + r.size.width; x += sz.width) rt_uiimage_draw(self, c, (CGRect){ { x, y }, sz }, 1, NULL);
    iosrt_CGContextRestoreGState(c);
}
static id im_description(id self, SEL s) {
    (void)s; CGSize sz = rt_uiimage_size(self); char b[160];
    snprintf(b, sizeof b, "<UIImage: %p>, {%g, %g} scale %g", (void *)self, sz.width, sz.height, ((struct uiimage *)self)->scale);
    return rt_str(b);
}
static id im_images(id self, SEL s) { (void)self; (void)s; return NULL; }
static double im_duration(id self, SEL s) { (void)self; (void)s; return 0; }
static id im_animated(Class c, SEL s, id images, double d) { (void)c; (void)s; (void)d; return images && rt_array_count(images) ? rt_array_at(images, 0) : NULL; }
static id im_animatedNamed(Class c, SEL s, id name, double d) {
    (void)d; char b[512]; snprintf(b, sizeof b, "%s0", name ? rt_str_utf8(name) : "");
    id r = im_named(c, s, rt_str(b));
    if (!r) { snprintf(b, sizeof b, "%s1", name ? rt_str_utf8(name) : ""); r = im_named(c, s, rt_str(b)); }
    return r;
}

/* ---- encoding */
id iosrt_UIImagePNGRepresentation(id img) {
    struct gfx_image *g = rt_cgimage_gfx(rt_uiimage_cgimage(img));
    if (!g) return NULL;
    size_t n; void *p = gfx_image_encode_png(g, &n);
    id d = objc_autorelease(rt_data_new(p, n)); free(p); return d;
}
id iosrt_UIImageJPEGRepresentation(id img, double q) {
    struct gfx_image *g = rt_cgimage_gfx(rt_uiimage_cgimage(img));
    if (!g) return NULL;
    size_t n; void *p = gfx_image_encode_jpeg(g, (int)(q * 100 + 0.5), &n);
    id d = objc_autorelease(rt_data_new(p, n)); free(p); return d;
}
void iosrt_UIImageWriteToSavedPhotosAlbum(id img, id target, SEL sel, void *ctx) {
    (void)img;
    if (target && sel) ((void (*)(id, SEL, id, id, void *))objc_msgSend)(target, sel, img, NULL, ctx);
}

void rt_uiimage_init(void) {
    NEWCLASS(UIImage, RT_CLASS(NSObject), sizeof(struct uiimage) - sizeof(void *));
    Class c = RT_CLASS(UIImage);
    CM(c, "imageNamed:", im_named); CM(c, "imageNamed:inBundle:compatibleWithTraitCollection:", im_namedBundle);
    CM(c, "imageWithContentsOfFile:", im_withContentsOfFile); CM(c, "imageWithData:", im_withData); CM(c, "imageWithData:scale:", im_withDataScale);
    CM(c, "imageWithCGImage:", im_withCGImage); CM(c, "imageWithCGImage:scale:orientation:", im_withCGImageSO);
    CM(c, "animatedImageWithImages:duration:", im_animated); CM(c, "animatedImageNamed:duration:", im_animatedNamed);
    IM(c, "initWithContentsOfFile:", im_initWithContentsOfFile); IM(c, "initWithData:", im_initWithData); IM(c, "initWithData:scale:", im_initWithDataScale);
    IM(c, "initWithCGImage:", im_initWithCGImage); IM(c, "initWithCGImage:scale:orientation:", im_initWithCGImageSO);
    IM(c, "dealloc", img_dealloc); IM(c, "CGImage", im_CGImage); IM(c, "size", im_size); IM(c, "scale", im_scale);
    IM(c, "imageOrientation", im_orientation); IM(c, "capInsets", im_capInsets); IM(c, "resizingMode", im_resizingMode);
    IM(c, "renderingMode", im_renderingMode); IM(c, "leftCapWidth", im_leftCap); IM(c, "topCapHeight", im_topCap);
    IM(c, "resizableImageWithCapInsets:", im_resizable); IM(c, "resizableImageWithCapInsets:resizingMode:", im_resizable2);
    IM(c, "stretchableImageWithLeftCapWidth:topCapHeight:", im_stretchable);
    IM(c, "imageWithRenderingMode:", im_withRenderingMode); IM(c, "imageWithAlignmentRectInsets:", im_self1);
    IM(c, "imageFlippedForRightToLeftLayoutDirection", im_self); IM(c, "imageWithTintColor:", im_withTint);
    IM(c, "drawInRect:", im_drawInRect); IM(c, "drawAtPoint:", im_drawAtPoint); IM(c, "drawInRect:blendMode:alpha:", im_drawInRectBA);
    IM(c, "drawAtPoint:blendMode:alpha:", im_drawAtPointBA); IM(c, "drawAsPatternInRect:", im_drawAsPattern);
    IM(c, "description", im_description); IM(c, "images", im_images); IM(c, "duration", im_duration);
}
