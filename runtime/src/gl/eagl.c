/* EAGL: EAGLContext and EAGLSharegroup over EGL (or the null GL), and the
 * drawable side of CAEAGLLayer.
 *
 * iOS has no default framebuffer: apps make a renderbuffer, give it storage
 * from a CAEAGLLayer with -renderbufferStorage:fromDrawable:, draw into an
 * FBO and call -presentRenderbuffer:. Here the context renders into an EGL
 * pbuffer-backed context, the renderbuffer gets ordinary storage of the
 * layer's pixel size, and present reads it back (glReadPixels through a
 * private FBO) into an image that becomes the layer's contents on the main
 * thread, where the normal compositor shows it. */
#include "gl_internal.h"
#include "../uikit/uikit_internal.h"
#include "../uikit/gfx.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

RT_DEFINE_CLASS(EAGLContext);
RT_DEFINE_CLASS(EAGLSharegroup);
RT_DECLARE_CLASS(CAEAGLLayer);
RT_DECLARE_CLASS(NSObject);

struct eaglctx { Class isa; struct rt_glctx *g; long api; id sharegroup, label; long multi; };
struct eaglshare { Class isa; struct rt_glctx *base; id label; };
#define EC(x) ((struct eaglctx *)(x))
#define ES(x) ((struct eaglshare *)(x))

id iosrt_kEAGLDrawablePropertyRetainedBacking, iosrt_kEAGLDrawablePropertyColorFormat,
   iosrt_kEAGLColorFormatRGBA8, iosrt_kEAGLColorFormatRGB565, iosrt_kEAGLColorFormatSRGBA8;

void iosrt_EAGLGetVersion(unsigned *major, unsigned *minor) { if (major) *major = 3; if (minor) *minor = 0; }

/* ------------------------------------------------------------------ GL entry points used here */
static void *fn(struct rt_glctx *c, const char *core, const char *oes) { void *p = c && c->api == 1 && oes ? rt_gl_driver(oes) : NULL; return p ? p : rt_gl_driver(core); }
#define GLF(var, ret, args, core, oes) ret (*var)args; *(void **)&var = fn(g, core, oes)

/* ------------------------------------------------------------------ drawables */
struct drawable {
    id ctx, layer; GLuint rb; int w, h;
    struct gfx_image *pool[3]; struct gfx_image *latest; int pending;
};
static struct drawable *dr[32]; static int ndr;
static pthread_mutex_t dr_lock = PTHREAD_MUTEX_INITIALIZER;

static struct drawable *dr_find(id ctx, GLuint rb) { for (int i = 0; i < ndr; i++) if (dr[i]->ctx == ctx && dr[i]->rb == rb) return dr[i]; return NULL; }
static void dr_free(struct drawable *d) {
    for (int i = 0; i < 3; i++) gfx_image_release(d->pool[i]);
    gfx_image_release(d->latest);
    objc_release(d->layer); free(d);
}
static void dr_remove(id ctx, GLuint rb, int all) {
    pthread_mutex_lock(&dr_lock);
    for (int i = 0; i < ndr;) {
        if (dr[i]->ctx == ctx && (all || dr[i]->rb == rb)) {
            struct drawable *d = dr[i]; dr[i] = dr[--ndr];
            if (d->pending) { d->ctx = NULL; d->rb = 0; }   /* the main-thread publish frees it */
            else dr_free(d);
        } else i++;
    }
    pthread_mutex_unlock(&dr_lock);
}

/* main thread: the newest frame becomes the layer's contents */
static void publish(void *p) {
    struct drawable *d = p;
    pthread_mutex_lock(&dr_lock);
    struct gfx_image *img = d->latest; d->latest = NULL; d->pending = 0;
    int orphan = d->ctx == NULL;
    id layer = objc_retain(d->layer);
    pthread_mutex_unlock(&dr_lock);
    if (img && layer) {
        id cg = rt_cgimage_new(img);
        RT_SEND1(void, layer, "setContents:", id, cg);
        objc_release(cg);
        rt_ui_mark_dirty();
    }
    gfx_image_release(img);
    objc_release(layer);
    if (orphan) dr_free(d);
}

/* ------------------------------------------------------------------ EAGLSharegroup */
static id sg_label(id self, SEL s) { (void)s; return ES(self)->label; }
static void sg_setLabel(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(ES(self)->label); ES(self)->label = v; }

/* ------------------------------------------------------------------ EAGLContext */
static __thread id g_current;

static id ctx_initWithAPI_sharegroup(id self, SEL s, unsigned long api, id share) {
    (void)s;
    if (api < 1 || api > 3) { objc_release(self); return NULL; }
    struct rt_glctx *base = share ? ES(share)->base : NULL;
    struct rt_glctx *g = rt_gl_ctx_new((int)api, base);
    if (!g) { objc_release(self); return NULL; }
    EC(self)->g = g; EC(self)->api = (long)api;
    if (!share) { share = RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(EAGLSharegroup), "alloc"), "init"); ES(share)->base = g; }
    else { objc_retain(share); if (!ES(share)->base) ES(share)->base = g; }
    EC(self)->sharegroup = share;
    return self;
}
static id ctx_initWithAPI(id self, SEL s, unsigned long api) { (void)s; return ctx_initWithAPI_sharegroup(self, rt_sel("initWithAPI:sharegroup:"), api, NULL); }
static id ctx_init(id self, SEL s) { (void)s; return ctx_initWithAPI(self, rt_sel("initWithAPI:"), 2); }
static void ctx_dealloc(id self, SEL s) {
    (void)s;
    dr_remove(self, 0, 1);
    struct eaglctx *c = EC(self);
    if (c->sharegroup && ES(c->sharegroup)->base == c->g) ES(c->sharegroup)->base = NULL;
    rt_gl_ctx_free(c->g);
    objc_release(c->sharegroup); objc_release(c->label);
    rt_dealloc_instance(self);
}
static rt_bool ctx_setCurrent(Class cls, SEL s, id ctx) {
    (void)cls; (void)s;
    if (ctx == g_current) { if (ctx) rt_gl_make_current(EC(ctx)->g); return 1; }
    int ok = rt_gl_make_current(ctx ? EC(ctx)->g : NULL);
    if (!ok) return 0;
    objc_retain(ctx); id old = g_current; g_current = ctx; objc_release(old);
    return 1;
}
static id ctx_current(Class cls, SEL s) { (void)cls; (void)s; return g_current; }
static unsigned long ctx_api(id self, SEL s) { (void)s; return (unsigned long)EC(self)->api; }
static id ctx_sharegroup(id self, SEL s) { (void)s; return EC(self)->sharegroup; }
static id ctx_label(id self, SEL s) { (void)s; return EC(self)->label; }
static void ctx_setLabel(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(EC(self)->label); EC(self)->label = v; }
static rt_bool ctx_multi(id self, SEL s) { (void)s; return EC(self)->multi != 0; }
static void ctx_setMulti(id self, SEL s, long v) { (void)s; EC(self)->multi = v & 0xff; }

/* binds this context on the calling thread for the duration of a call */
static int ensure_current(id self) { if (rt_gl_current() == EC(self)->g) return 1; return rt_gl_make_current(EC(self)->g); }

static rt_bool ctx_storage(id self, SEL s, unsigned long target, id drawable) {
    (void)s; (void)target;
    struct rt_glctx *g = EC(self)->g;
    if (!ensure_current(self)) return 0;
    GLF(getiv, void, (GLenum, GLint *), "glGetIntegerv", NULL);
    GLint rb = 0; if (getiv) getiv(GL_RENDERBUFFER_BINDING, &rb);
    if (!drawable) { dr_remove(self, (GLuint)rb, 0); return 1; }
    if (!rb) { rt_warn("EAGLContext renderbufferStorage:fromDrawable: with no renderbuffer bound"); return 0; }
    CGRect b = RT_SEND0(CGRect, drawable, "bounds");
    double sc = RT_SEND0(double, drawable, "contentsScale"); if (!(sc > 0)) sc = 1;
    int w = (int)(b.size.width * sc + 0.5), h = (int)(b.size.height * sc + 0.5);
    if (w < 1) w = 1; if (h < 1) h = 1;
    GLF(storage, void, (GLenum, GLenum, GLsizei, GLsizei), "glRenderbufferStorage", "glRenderbufferStorageOES");
    GLF(geterr, GLenum, (void), "glGetError", NULL);
    if (geterr) while (geterr()) {}
    if (storage) storage(GL_RENDERBUFFER, GL_RGBA8_OES, w, h);
    if (geterr && geterr() && storage) storage(GL_RENDERBUFFER, GL_RGB565, w, h);
    pthread_mutex_lock(&dr_lock);
    struct drawable *d = dr_find(self, (GLuint)rb);
    if (!d && ndr < 32) { d = calloc(1, sizeof *d); d->ctx = self; d->rb = (GLuint)rb; dr[ndr++] = d; }
    if (d) {
        if (d->layer != drawable) { objc_retain(drawable); objc_release(d->layer); d->layer = drawable; }
        if (d->w != w || d->h != h) { for (int i = 0; i < 3; i++) { gfx_image_release(d->pool[i]); d->pool[i] = NULL; } }
        d->w = w; d->h = h;
    }
    pthread_mutex_unlock(&dr_lock);
    rt_log("EAGL: renderbuffer %d gets %dx%d px from %s", rb, w, h, rt_class_name(((struct { Class isa; } *)drawable)->isa));
    return 1;
}

static rt_bool ctx_present(id self, SEL s, unsigned long target) {
    (void)s; (void)target;
    struct rt_glctx *g = EC(self)->g;
    if (!ensure_current(self)) return 0;
    GLF(getiv, void, (GLenum, GLint *), "glGetIntegerv", NULL);
    GLint rb = 0, fb = 0; if (getiv) { getiv(GL_RENDERBUFFER_BINDING, &rb); getiv(GL_FRAMEBUFFER_BINDING, &fb); }
    pthread_mutex_lock(&dr_lock);
    struct drawable *d = dr_find(self, (GLuint)rb);
    struct gfx_image *img = NULL; int w = 0, h = 0;
    if (d) {
        w = d->w; h = d->h;
        for (int i = 0; i < 3 && !img; i++) {
            if (!d->pool[i]) d->pool[i] = gfx_image_new(w, h);
            if (__atomic_load_n(&d->pool[i]->refs, __ATOMIC_ACQUIRE) == 1) img = d->pool[i];
        }
        if (!img) { gfx_image_release(d->pool[2]); d->pool[2] = gfx_image_new(w, h); img = d->pool[2]; }
        gfx_image_retain(img);
    }
    pthread_mutex_unlock(&dr_lock);
    if (!d || !img) return 1;
    /* read the renderbuffer through a private framebuffer */
    GLF(genfb, void, (GLsizei, GLuint *), "glGenFramebuffers", "glGenFramebuffersOES");
    GLF(bindfb, void, (GLenum, GLuint), "glBindFramebuffer", "glBindFramebufferOES");
    GLF(attach, void, (GLenum, GLenum, GLenum, GLuint), "glFramebufferRenderbuffer", "glFramebufferRenderbufferOES");
    GLF(readpx, void, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *), "glReadPixels", NULL);
    if (!g->read_fbo && genfb) genfb(1, &g->read_fbo);
    if (bindfb && attach && readpx) {
        bindfb(GL_FRAMEBUFFER, g->read_fbo);
        attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, (GLuint)rb);
        readpx(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, img->px);
        bindfb(GL_FRAMEBUFFER, (GLuint)fb);
    }
    /* GL rows start at the bottom; the layer's at the top. The layer is opaque. */
    uint32_t *px = img->px, *tmp = malloc((size_t)w * 4);
    for (int y = 0; y < h / 2; y++) {
        uint32_t *a = px + (size_t)y * w, *b = px + (size_t)(h - 1 - y) * w;
        memcpy(tmp, a, (size_t)w * 4); memcpy(a, b, (size_t)w * 4); memcpy(b, tmp, (size_t)w * 4);
    }
    free(tmp);
    for (size_t i = 0, n = (size_t)w * h; i < n; i++) px[i] |= 0xff000000u;
    pthread_mutex_lock(&dr_lock);
    gfx_image_release(d->latest); d->latest = img;
    int dispatch = !d->pending; d->pending = 1;
    pthread_mutex_unlock(&dr_lock);
    if (dispatch) rt_after(0, publish, d);
    return 1;
}
static rt_bool ctx_presentAt(id self, SEL s, unsigned long target, double t) { (void)s; (void)t; return ctx_present(self, rt_sel("presentRenderbuffer:"), target); }

/* ------------------------------------------------------------------ CAEAGLLayer */
struct props_holder { id dict; };
static id g_layer_props[64]; static id g_layer_keys[64];
static id ly_props(id self, SEL s) { (void)s; for (int i = 0; i < 64; i++) if (g_layer_keys[i] == self) return g_layer_props[i]; return NULL; }
static void ly_setProps(id self, SEL s, id d) {
    (void)s; int slot = -1;
    for (int i = 0; i < 64; i++) if (g_layer_keys[i] == self) { slot = i; break; }
    if (slot < 0) for (int i = 0; i < 64; i++) if (!g_layer_keys[i]) { slot = i; break; }
    if (slot < 0) return;
    objc_retain(d); objc_release(g_layer_props[slot]); g_layer_props[slot] = d; g_layer_keys[slot] = d ? self : NULL;
}
static rt_bool ly_no(id self, SEL s) { (void)self; (void)s; return 0; }
static void ly_noopL(id self, SEL s, long v) { (void)self; (void)s; (void)v; }

void rt_eagl_init(void) {
    Class o = RT_CLASS(NSObject);
    NEWCLASS(EAGLSharegroup, o, sizeof(struct eaglshare) - sizeof(void *));
    Class sg = RT_CLASS(EAGLSharegroup);
    IM(sg, "debugLabel", sg_label); IM(sg, "setDebugLabel:", sg_setLabel);
    NEWCLASS(EAGLContext, o, sizeof(struct eaglctx) - sizeof(void *));
    Class c = RT_CLASS(EAGLContext);
    IM(c, "init", ctx_init); IM(c, "initWithAPI:", ctx_initWithAPI); IM(c, "initWithAPI:sharegroup:", ctx_initWithAPI_sharegroup);
    IM(c, "dealloc", ctx_dealloc);
    CM(c, "setCurrentContext:", ctx_setCurrent); CM(c, "currentContext", ctx_current);
    IM(c, "API", ctx_api); IM(c, "sharegroup", ctx_sharegroup); IM(c, "debugLabel", ctx_label); IM(c, "setDebugLabel:", ctx_setLabel);
    IM(c, "isMultiThreaded", ctx_multi); IM(c, "setMultiThreaded:", ctx_setMulti);
    IM(c, "renderbufferStorage:fromDrawable:", ctx_storage);
    IM(c, "presentRenderbuffer:", ctx_present); IM(c, "presentRenderbuffer:atTime:", ctx_presentAt); IM(c, "presentRenderbuffer:afterMinimumDuration:", ctx_presentAt);
    Class l = RT_CLASS(CAEAGLLayer);
    IM(l, "drawableProperties", ly_props); IM(l, "setDrawableProperties:", ly_setProps);
    IM(l, "presentsWithTransaction", ly_no); IM(l, "setPresentsWithTransaction:", ly_noopL);
    iosrt_kEAGLDrawablePropertyRetainedBacking = rt_str_new("EAGLDrawablePropertyRetainedBacking");
    iosrt_kEAGLDrawablePropertyColorFormat = rt_str_new("EAGLDrawablePropertyColorFormat");
    iosrt_kEAGLColorFormatRGBA8 = rt_str_new("EAGLColorFormatRGBA8");
    iosrt_kEAGLColorFormatRGB565 = rt_str_new("EAGLColorFormatRGB565");
    iosrt_kEAGLColorFormatSRGBA8 = rt_str_new("EAGLColorFormatSRGBA8");
}
