/* OpenGL ES binding: driver lookup, EGL contexts, and the few entry points
 * that need wrapping (see gl_internal.h). */
#include "gl_internal.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

/* ------------------------------------------------------------------ driver */
typedef void *EGLDisplay, *EGLConfig, *EGLContext, *EGLSurface;
typedef int EGLint;
#define EGL_DEFAULT_DISPLAY ((void *)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_ALPHA_SIZE 0x3021
#define EGL_BLUE_SIZE 0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE 0x3024
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_SURFACE_TYPE 0x3033
#define EGL_NONE 0x3038
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_PBUFFER_BIT 0x0001
#define EGL_OPENGL_ES_BIT 0x0001
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_OPENGL_ES3_BIT 0x0040
#define EGL_CONTEXT_CLIENT_VERSION 0x3098

static struct {
    int state;                 /* 0 not tried, 1 driver, 2 null */
    void *gles2, *gles1, *egl;
    EGLDisplay dpy;
    void *(*GetProcAddress)(const char *);
    EGLDisplay (*GetDisplay)(void *);
    unsigned (*Initialize)(EGLDisplay, EGLint *, EGLint *);
    unsigned (*ChooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
    EGLContext (*CreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
    EGLSurface (*CreatePbufferSurface)(EGLDisplay, EGLConfig, const EGLint *);
    unsigned (*MakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    unsigned (*DestroyContext)(EGLDisplay, EGLContext);
    unsigned (*DestroySurface)(EGLDisplay, EGLSurface);
    EGLint (*GetError)(void);
    unsigned (*BindAPI)(unsigned);
} E;
static pthread_mutex_t open_lock = PTHREAD_MUTEX_INITIALIZER;

static void gl_open(void) {
    if (E.state) return;
    pthread_mutex_lock(&open_lock);
    if (E.state) { pthread_mutex_unlock(&open_lock); return; }
    const char *force = getenv("IOSRT_GL");
    if (!(force && !strcmp(force, "null"))) {
        E.egl = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
        E.gles2 = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
        E.gles1 = dlopen("libGLESv1_CM.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (E.egl && E.gles2) {
        #define LOAD(f) *(void **)&E.f = dlsym(E.egl, "egl" #f)
        LOAD(GetProcAddress); LOAD(GetDisplay); LOAD(Initialize); LOAD(ChooseConfig); LOAD(CreateContext); LOAD(CreatePbufferSurface);
        LOAD(MakeCurrent); LOAD(DestroyContext); LOAD(DestroySurface); LOAD(GetError); LOAD(BindAPI);
        #undef LOAD
        if (E.GetDisplay && E.Initialize && E.CreateContext && E.MakeCurrent) {
            E.dpy = E.GetDisplay(EGL_DEFAULT_DISPLAY);
            EGLint maj = 0, min = 0;
            if (E.dpy && E.Initialize(E.dpy, &maj, &min)) { E.state = 1; rt_log("OpenGL ES: device driver (EGL %d.%d)", maj, min); }
        }
    }
    if (E.state != 1) { E.state = 2; rt_log("OpenGL ES: no device driver here, using the software null GL"); }
    pthread_mutex_unlock(&open_lock);
}
int rt_gl_real(void) { gl_open(); return E.state == 1; }

/* the name with an extension suffix removed: glFenceSyncAPPLE -> glFenceSync (ES 3 core) */
static int strip_suffix(const char *name, char *out, size_t n) {
    static const char *suf[] = { "APPLE", "OES", "EXT", "ARB", "KHR", "IMG", NULL };
    size_t l = strlen(name);
    for (int i = 0; suf[i]; i++) { size_t k = strlen(suf[i]); if (l > k && !strcmp(name + l - k, suf[i]) && l - k < n) { memcpy(out, name, l - k); out[l - k] = 0; return 1; } }
    return 0;
}
void *rt_gl_driver(const char *name) {
    gl_open();
    if (E.state != 1) return nullgl_lookup(name);
    void *p = dlsym(E.gles2, name);
    if (!p && E.gles1) p = dlsym(E.gles1, name);
    if (!p && E.GetProcAddress) p = E.GetProcAddress(name);
    char core[96];
    if (!p && strip_suffix(name, core, sizeof core)) { p = dlsym(E.gles2, core); if (!p && E.gles1) p = dlsym(E.gles1, core); }
    return p;
}

/* loader hook: OpenGLES imports the export table does not cover */
void *rt_gl_resolve(const char *mangled) {
    const char *name = mangled[0] == '_' ? mangled + 1 : mangled;
    if (strncmp(name, "gl", 2)) return NULL;
    void *p = rt_gl_driver(name);
    if (!p) {
        static int warned; if (warned++ < 32) rt_log("OpenGL ES: %s is not available here; calls do nothing", name);
        p = nullgl_noop();
    }
    return p;
}

/* ------------------------------------------------------------------ contexts */
static __thread struct rt_glctx *cur;
struct rt_glctx *rt_gl_current(void) { return cur; }

struct rt_glctx *rt_gl_ctx_new(int api, struct rt_glctx *share) {
    gl_open();
    struct rt_glctx *c = calloc(1, sizeof *c);
    c->api = api;
    if (E.state == 2) { c->null = nullgl_new(api); return c; }
    for (int attempt = 0; attempt < 2 && !c->egl_ctx; attempt++) {
        int version = api == 1 ? 1 : api == 3 || attempt == 0 ? 3 : 2;
        if (api == 3 && attempt == 1) break;
        if (api == 1 && attempt == 1) break;
        EGLint bit = version == 1 ? EGL_OPENGL_ES_BIT : version == 2 ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_ES3_BIT;
        EGLint attrs[] = { EGL_RENDERABLE_TYPE, bit, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                           EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 16, EGL_NONE };
        EGLConfig cfg = NULL; EGLint n = 0;
        if (!E.ChooseConfig(E.dpy, attrs, &cfg, 1, &n) || n < 1) {
            attrs[13] = EGL_NONE; attrs[12] = EGL_NONE;   /* without depth */
            if (!E.ChooseConfig(E.dpy, attrs, &cfg, 1, &n) || n < 1) continue;
        }
        EGLint cattrs[] = { EGL_CONTEXT_CLIENT_VERSION, version, EGL_NONE };
        c->egl_ctx = E.CreateContext(E.dpy, cfg, share ? share->egl_ctx : EGL_NO_CONTEXT, cattrs);
        if (!c->egl_ctx) continue;
        EGLint pb[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
        c->egl_surf = E.CreatePbufferSurface ? E.CreatePbufferSurface(E.dpy, cfg, pb) : NULL;
    }
    if (!c->egl_ctx) { rt_warn("EAGLContext: the device has no OpenGL ES %d context (EGL error 0x%x)", api, E.GetError ? E.GetError() : 0); free(c); return NULL; }
    return c;
}
void rt_gl_ctx_free(struct rt_glctx *c) {
    if (!c) return;
    if (cur == c) rt_gl_make_current(NULL);
    if (c->null) nullgl_free(c->null);
    if (c->egl_surf && E.DestroySurface) E.DestroySurface(E.dpy, c->egl_surf);
    if (c->egl_ctx && E.DestroyContext) E.DestroyContext(E.dpy, c->egl_ctx);
    free(c->extensions);
    free(c);
}
int rt_gl_make_current(struct rt_glctx *c) {
    gl_open();
    if (E.state == 2) { nullgl_set_current(c ? c->null : NULL); cur = c; return 1; }
    int ok = c ? E.MakeCurrent(E.dpy, c->egl_surf, c->egl_surf, c->egl_ctx) : E.MakeCurrent(E.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (ok) cur = c;
    else rt_warn("EAGLContext: eglMakeCurrent failed (0x%x)", E.GetError ? E.GetError() : 0);
    return ok != 0;
}

/* ------------------------------------------------------------------ wrapped entry points */
#define REAL(ret, name, args) static ret (*p)args; if (!p) *(void **)&p = rt_gl_driver(#name); if (!p) *(void **)&p = nullgl_noop()

/* GL_EXTENSIONS plus what the wrappers provide on every device */
const unsigned char *iosrt_glGetString(GLenum e) {
    REAL(const unsigned char *, glGetString, (GLenum));
    const unsigned char *s = p(e);
    if (e != GL_EXTENSIONS) return s;
    struct rt_glctx *c = cur;
    if (!c) return s;
    if (!c->extensions) {
        static const char *add[] = { "GL_IMG_texture_compression_pvrtc", "GL_APPLE_texture_format_BGRA8888", "GL_EXT_texture_format_BGRA8888",
                                     "GL_EXT_discard_framebuffer", "GL_APPLE_framebuffer_multisample", "GL_OES_vertex_array_object",
                                     "GL_APPLE_sync", "GL_EXT_debug_label", "GL_EXT_debug_marker", "GL_OES_rgb8_rgba8", NULL };
        size_t n = (s ? strlen((const char *)s) : 0) + 512; char *out = malloc(n);
        snprintf(out, n, "%s", s ? (const char *)s : "");
        for (int i = 0; add[i]; i++) if (!strstr(out, add[i])) { size_t l = strlen(out); snprintf(out + l, n - l, "%s%s", l && out[l - 1] != ' ' ? " " : "", add[i]); }
        c->extensions = out;
    }
    return (const unsigned char *)c->extensions;
}

/* BGRA pixel uploads (Apple's GL_APPLE_texture_format_BGRA8888): swizzled to RGBA */
static void *bgra_to_rgba(const void *src, GLsizei w, GLsizei h) {
    if (!src || w <= 0 || h <= 0) return NULL;
    size_t n = (size_t)w * (size_t)h; const uint8_t *s = src; uint8_t *d = malloc(n * 4);
    for (size_t i = 0; i < n; i++) { d[i * 4] = s[i * 4 + 2]; d[i * 4 + 1] = s[i * 4 + 1]; d[i * 4 + 2] = s[i * 4]; d[i * 4 + 3] = s[i * 4 + 3]; }
    return d;
}
void iosrt_glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt, GLenum type, const void *px) {
    REAL(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *));
    if (fmt == GL_BGRA_EXT && type == GL_UNSIGNED_BYTE) {
        void *t = bgra_to_rgba(px, w, h);
        p(target, level, GL_RGBA, w, h, border, GL_RGBA, type, t); free(t); return;
    }
    if (ifmt == (GLint)GL_BGRA_EXT) ifmt = GL_RGBA;
    p(target, level, ifmt, w, h, border, fmt, type, px);
}
void iosrt_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, const void *px) {
    REAL(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *));
    if (fmt == GL_BGRA_EXT && type == GL_UNSIGNED_BYTE) { void *t = bgra_to_rgba(px, w, h); p(target, level, x, y, w, h, GL_RGBA, type, t); free(t); return; }
    p(target, level, x, y, w, h, fmt, type, px);
}
/* PVRTC: Android GPUs other than PowerVR lack it; decode and upload RGBA */
static int has_ext(const char *e) {
    static const unsigned char *(*gs)(GLenum);
    if (!gs) *(void **)&gs = rt_gl_driver("glGetString");
    const char *s = gs ? (const char *)gs(GL_EXTENSIONS) : NULL;
    return s && strstr(s, e) != NULL;
}
void iosrt_glCompressedTexImage2D(GLenum target, GLint level, GLenum ifmt, GLsizei w, GLsizei h, GLint border, GLsizei size, const void *data) {
    REAL(void, glCompressedTexImage2D, (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *));
    if (ifmt >= GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG && ifmt <= GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG && !has_ext("GL_IMG_texture_compression_pvrtc")) {
        int bpp2 = ifmt == GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG || ifmt == GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG;
        if (w <= 0 || h <= 0 || !data || (size_t)size < rt_pvrtc_size(w, h, bpp2)) { p(target, level, ifmt, w, h, border, size, data); return; }
        uint8_t *rgba = malloc((size_t)w * (size_t)h * 4);
        rt_pvrtc_decode(data, w, h, bpp2, rgba);
        void (*ti)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
        *(void **)&ti = rt_gl_driver("glTexImage2D");
        if (ti) ti(target, level, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
        return;
    }
    p(target, level, ifmt, w, h, border, size, data);
}
void iosrt_glCompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLsizei size, const void *data) {
    REAL(void, glCompressedTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void *));
    if (fmt >= GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG && fmt <= GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG && !has_ext("GL_IMG_texture_compression_pvrtc")) return;   /* PVRTC has no sub-updates */
    p(target, level, x, y, w, h, fmt, size, data);
}

/* Darwin packs the 9th and 10th (small) stack arguments into one 8-byte slot */
void iosrt_glTexSubImage3D(GLenum t, GLint l, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, uint64_t fmt_type, const void *px) {
    REAL(void, glTexSubImage3D, (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void *));
    p(t, l, x, y, z, w, h, d, (GLenum)(fmt_type & 0xffffffffu), (GLenum)(fmt_type >> 32), px);
}
void iosrt_glCompressedTexSubImage3D(GLenum t, GLint l, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, uint64_t fmt_size, const void *data) {
    REAL(void, glCompressedTexSubImage3D, (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const void *));
    p(t, l, x, y, z, w, h, d, (GLenum)(fmt_size & 0xffffffffu), (GLsizei)(fmt_size >> 32), data);
}
void iosrt_glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, uint64_t mask_filter) {
    REAL(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum));
    p(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, (GLbitfield)(mask_filter & 0xffffffffu), (GLenum)(mask_filter >> 32));
}

/* GLboolean results: only the low byte is defined on the driver side */
#define BOOL1(name, T) GLboolean iosrt_##name(T a) { REAL(GLboolean, name, (T)); return p(a) & 0xff; }
BOOL1(glIsEnabled, GLenum) BOOL1(glIsBuffer, GLuint) BOOL1(glIsTexture, GLuint) BOOL1(glIsFramebuffer, GLuint) BOOL1(glIsRenderbuffer, GLuint)
BOOL1(glIsFramebufferOES, GLuint) BOOL1(glIsRenderbufferOES, GLuint) BOOL1(glIsProgram, GLuint) BOOL1(glIsShader, GLuint)
BOOL1(glIsVertexArrayOES, GLuint) BOOL1(glUnmapBufferOES, GLenum) BOOL1(glUnmapBuffer, GLenum)

/* Apple multisampling: resolve = blit the read framebuffer into the draw framebuffer */
void iosrt_glResolveMultisampleFramebufferAPPLE(void) {
    void (*gi)(GLenum, GLint *), (*gap)(GLenum, GLenum, GLenum, GLint *), (*grp)(GLenum, GLenum, GLint *), (*brb)(GLenum, GLuint);
    *(void **)&gi = rt_gl_driver("glGetIntegerv"); *(void **)&gap = rt_gl_driver("glGetFramebufferAttachmentParameteriv");
    *(void **)&grp = rt_gl_driver("glGetRenderbufferParameteriv"); *(void **)&brb = rt_gl_driver("glBindRenderbuffer");
    void (*blit)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
    *(void **)&blit = rt_gl_driver("glBlitFramebuffer");
    if (!gi || !gap || !grp || !brb || !blit) return;
    GLint rb = 0, oldrb = 0, w = 0, h = 0;
    gap(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &rb);
    gi(GL_RENDERBUFFER_BINDING, &oldrb);
    brb(GL_RENDERBUFFER, (GLuint)rb); grp(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &w); grp(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &h);
    brb(GL_RENDERBUFFER, (GLuint)oldrb);
    if (w <= 0 || h <= 0) { GLint vp[4] = { 0 }; gi(GL_VIEWPORT, vp); w = vp[2]; h = vp[3]; }
    blit(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}
void iosrt_glRenderbufferStorageMultisampleAPPLE(GLenum t, GLsizei samples, GLenum fmt, GLsizei w, GLsizei h) {
    void (*ms)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
    *(void **)&ms = rt_gl_driver("glRenderbufferStorageMultisample");
    if (!ms) *(void **)&ms = rt_gl_driver("glRenderbufferStorageMultisampleEXT");
    if (ms) { ms(t, samples, fmt, w, h); return; }
    void (*rs)(GLenum, GLenum, GLsizei, GLsizei); *(void **)&rs = rt_gl_driver("glRenderbufferStorage");
    if (rs) rs(t, fmt, w, h);
}
/* discard: ES 3 invalidate, the EXT entry, or nothing (it is only a hint) */
void iosrt_glDiscardFramebufferEXT(GLenum t, GLsizei n, const GLenum *att) {
    static void (*f)(GLenum, GLsizei, const GLenum *); static int tried;
    if (!tried) { tried = 1; if (rt_gl_real()) { *(void **)&f = rt_gl_driver("glDiscardFramebufferEXT"); if (!f) *(void **)&f = rt_gl_driver("glInvalidateFramebuffer"); } }
    struct rt_glctx *c = cur;
    if (f && c && c->api >= 2) f(t, n, att);
}
/* OES mapping on ES 3 drivers without the extension */
void *iosrt_glMapBufferOES(GLenum target, GLenum access) {
    if (!rt_gl_real()) { void *(*nm)(GLenum, GLenum); *(void **)&nm = nullgl_lookup("glMapBufferOES"); return nm ? nm(target, access) : NULL; }
    static void *(*oes)(GLenum, GLenum); static int tried;
    if (!tried) { tried = 1; *(void **)&oes = dlsym(E.gles2, "glMapBufferOES"); if (!oes && E.GetProcAddress && has_ext("GL_OES_mapbuffer")) *(void **)&oes = E.GetProcAddress("glMapBufferOES"); }
    if (oes) return oes(target, access);
    void *(*range)(GLenum, GLintptr, GLsizeiptr, GLbitfield); void (*gbp)(GLenum, GLenum, GLint *);
    *(void **)&range = rt_gl_driver("glMapBufferRange"); *(void **)&gbp = rt_gl_driver("glGetBufferParameteriv");
    if (!range || !gbp) return NULL;
    GLint size = 0; gbp(target, GL_BUFFER_SIZE, &size);
    return size > 0 ? range(target, 0, size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT) : NULL;
}
