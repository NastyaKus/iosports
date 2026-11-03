/* The null GL: what stands in for a GPU driver where there is none (the
 * static CLI builds and the host). It is not a renderer: it keeps object
 * names, renderbuffer storage with real pixels, framebuffer attachments,
 * clear color, scissor and viewport, and implements glClear and
 * glReadPixels on them, so an app's EAGL setup, clears and presents can be
 * followed end to end. Every other entry point accepts its arguments and
 * does nothing (queries answer zero or "success"). */
#include "gl_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

struct nrb { GLuint id; GLsizei w, h; GLenum fmt; uint8_t *px; };          /* RGBA8, rows bottom-up like GL */
struct nfb { GLuint id; GLuint color, depth; };
struct nbuf { GLuint id; void *data; GLsizeiptr size; };
struct nullgl {
    int api;
    GLuint next_name;
    struct nrb rb[64]; int nrb;
    struct nfb fb[64]; int nfb;
    struct nbuf buf[256]; int nbuf;
    GLuint bound_rb, bound_fb, bound_array, bound_element;
    float clear[4]; GLint scissor[4], viewport[4]; int scissor_on;
    GLenum error;
};
static __thread struct nullgl *N;

struct nullgl *nullgl_new(int api) { struct nullgl *n = calloc(1, sizeof *n); n->api = api; n->next_name = 1; return n; }
void nullgl_free(struct nullgl *n) {
    if (!n) return;
    for (int i = 0; i < n->nrb; i++) free(n->rb[i].px);
    for (int i = 0; i < n->nbuf; i++) free(n->buf[i].data);
    if (N == n) N = NULL;
    free(n);
}
void nullgl_set_current(struct nullgl *n) { N = n; }

static struct nrb *rb_of(GLuint id) { if (!N || !id) return NULL; for (int i = 0; i < N->nrb; i++) if (N->rb[i].id == id) return &N->rb[i]; return NULL; }
static struct nfb *fb_of(GLuint id) { if (!N || !id) return NULL; for (int i = 0; i < N->nfb; i++) if (N->fb[i].id == id) return &N->fb[i]; return NULL; }
static struct nbuf *buf_of(GLuint id) { if (!N || !id) return NULL; for (int i = 0; i < N->nbuf; i++) if (N->buf[i].id == id) return &N->buf[i]; return NULL; }

/* ------------------------------------------------------------------ names */
static void gen(GLsizei n, GLuint *out) { for (GLsizei i = 0; i < n && out; i++) out[i] = N ? N->next_name++ : (GLuint)(i + 1); }
static void n_GenRenderbuffers(GLsizei n, GLuint *out) {
    gen(n, out);
    for (GLsizei i = 0; N && out && i < n && N->nrb < 64; i++) { struct nrb *r = &N->rb[N->nrb++]; memset(r, 0, sizeof *r); r->id = out[i]; }
}
static void n_GenFramebuffers(GLsizei n, GLuint *out) {
    gen(n, out);
    for (GLsizei i = 0; N && out && i < n && N->nfb < 64; i++) { struct nfb *f = &N->fb[N->nfb++]; memset(f, 0, sizeof *f); f->id = out[i]; }
}
static void n_GenBuffers(GLsizei n, GLuint *out) {
    gen(n, out);
    for (GLsizei i = 0; N && out && i < n && N->nbuf < 256; i++) { struct nbuf *b = &N->buf[N->nbuf++]; memset(b, 0, sizeof *b); b->id = out[i]; }
}
static void n_Gen(GLsizei n, GLuint *out) { gen(n, out); }
static GLuint n_Create(void) { return N ? N->next_name++ : 1; }
static GLuint n_CreateShader(GLenum t) { (void)t; return n_Create(); }
static void n_DeleteRenderbuffers(GLsizei n, const GLuint *ids) {
    for (GLsizei i = 0; N && ids && i < n; i++) { struct nrb *r = rb_of(ids[i]); if (r) { free(r->px); *r = N->rb[--N->nrb]; } }
}
static void n_DeleteFramebuffers(GLsizei n, const GLuint *ids) {
    for (GLsizei i = 0; N && ids && i < n; i++) { struct nfb *f = fb_of(ids[i]); if (f) { if (N->bound_fb == f->id) N->bound_fb = 0; *f = N->fb[--N->nfb]; } }
}
static void n_DeleteBuffers(GLsizei n, const GLuint *ids) {
    for (GLsizei i = 0; N && ids && i < n; i++) { struct nbuf *b = buf_of(ids[i]); if (b) { free(b->data); *b = N->buf[--N->nbuf]; } }
}

/* ------------------------------------------------------------------ renderbuffers and framebuffers */
static void n_BindRenderbuffer(GLenum t, GLuint id) { (void)t; if (N) { N->bound_rb = id; if (id && !rb_of(id) && N->nrb < 64) { struct nrb *r = &N->rb[N->nrb++]; memset(r, 0, sizeof *r); r->id = id; } } }
static void n_BindFramebuffer(GLenum t, GLuint id) { (void)t; if (N) { N->bound_fb = id; if (id && !fb_of(id) && N->nfb < 64) { struct nfb *f = &N->fb[N->nfb++]; memset(f, 0, sizeof *f); f->id = id; } } }
static void n_RenderbufferStorage(GLenum t, GLenum fmt, GLsizei w, GLsizei h) {
    (void)t; struct nrb *r = rb_of(N ? N->bound_rb : 0);
    if (!r || w < 0 || h < 0 || w > 8192 || h > 8192) { if (N) N->error = GL_INVALID_OPERATION; return; }
    free(r->px); r->w = w; r->h = h; r->fmt = fmt; r->px = calloc((size_t)w * (size_t)h, 4);
}
static void n_RenderbufferStorageMultisample(GLenum t, GLsizei s, GLenum fmt, GLsizei w, GLsizei h) { (void)s; n_RenderbufferStorage(t, fmt, w, h); }
static void n_GetRenderbufferParameteriv(GLenum t, GLenum p, GLint *out) {
    (void)t; struct nrb *r = rb_of(N ? N->bound_rb : 0); if (!out) return;
    *out = !r ? 0 : p == GL_RENDERBUFFER_WIDTH ? r->w : p == GL_RENDERBUFFER_HEIGHT ? r->h : p == GL_RENDERBUFFER_INTERNAL_FORMAT ? (GLint)r->fmt : 0;
}
static void n_FramebufferRenderbuffer(GLenum t, GLenum att, GLenum rbt, GLuint rb) {
    (void)t; (void)rbt; struct nfb *f = fb_of(N ? N->bound_fb : 0); if (!f) return;
    if (att == GL_COLOR_ATTACHMENT0) f->color = rb; else if (att == GL_DEPTH_ATTACHMENT) f->depth = rb;
}
static GLenum n_CheckFramebufferStatus(GLenum t) {
    (void)t; struct nfb *f = fb_of(N ? N->bound_fb : 0);
    return f && (f->color || f->depth) ? GL_FRAMEBUFFER_COMPLETE : GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
}
static void n_GetFramebufferAttachmentParameteriv(GLenum t, GLenum att, GLenum p, GLint *out) {
    (void)t; struct nfb *f = fb_of(N ? N->bound_fb : 0); if (!out) return;
    *out = f && p == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME ? (GLint)(att == GL_COLOR_ATTACHMENT0 ? f->color : att == GL_DEPTH_ATTACHMENT ? f->depth : 0) : 0;
}

/* ------------------------------------------------------------------ clear and read back */
static void n_ClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { if (N) { N->clear[0] = r; N->clear[1] = g; N->clear[2] = b; N->clear[3] = a; } }
static void n_ClearColorx(GLint r, GLint g, GLint b, GLint a) { n_ClearColor(r / 65536.f, g / 65536.f, b / 65536.f, a / 65536.f); }
static void n_Scissor(GLint x, GLint y, GLsizei w, GLsizei h) { if (N) { N->scissor[0] = x; N->scissor[1] = y; N->scissor[2] = w; N->scissor[3] = h; } }
static void n_Viewport(GLint x, GLint y, GLsizei w, GLsizei h) { if (N) { N->viewport[0] = x; N->viewport[1] = y; N->viewport[2] = w; N->viewport[3] = h; } }
static void n_Enable(GLenum c) { if (N && c == GL_SCISSOR_TEST) N->scissor_on = 1; }
static void n_Disable(GLenum c) { if (N && c == GL_SCISSOR_TEST) N->scissor_on = 0; }
static GLboolean n_IsEnabled(GLenum c) { return N && c == GL_SCISSOR_TEST && N->scissor_on; }
static uint8_t to8(float v) { return (uint8_t)lroundf(fminf(1, fmaxf(0, v)) * 255); }
static void n_Clear(GLbitfield mask) {
    if (!N || !(mask & GL_COLOR_BUFFER_BIT)) return;
    struct nfb *f = fb_of(N->bound_fb); struct nrb *r = f ? rb_of(f->color) : NULL;
    if (!r || !r->px) return;
    int x0 = 0, y0 = 0, x1 = r->w, y1 = r->h;
    if (N->scissor_on) { x0 = N->scissor[0]; y0 = N->scissor[1]; x1 = x0 + N->scissor[2]; y1 = y0 + N->scissor[3]; }
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > r->w) x1 = r->w; if (y1 > r->h) y1 = r->h;
    uint8_t c[4] = { to8(N->clear[0]), to8(N->clear[1]), to8(N->clear[2]), to8(N->clear[3]) };
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) memcpy(r->px + ((size_t)y * r->w + x) * 4, c, 4);
}
static void n_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void *out) {
    if (!N || !out || fmt != GL_RGBA || type != GL_UNSIGNED_BYTE) return;
    struct nfb *f = fb_of(N->bound_fb); struct nrb *r = f ? rb_of(f->color) : NULL;
    uint8_t *o = out;
    for (GLsizei j = 0; j < h; j++) for (GLsizei i = 0; i < w; i++) {
        int sx = x + i, sy = y + j; uint8_t *d = o + ((size_t)j * w + i) * 4;
        if (r && r->px && sx >= 0 && sy >= 0 && sx < r->w && sy < r->h) memcpy(d, r->px + ((size_t)sy * r->w + sx) * 4, 4);
        else memset(d, 0, 4);
    }
}

/* ------------------------------------------------------------------ buffers */
static void n_BindBuffer(GLenum t, GLuint id) { if (!N) return; if (t == GL_ARRAY_BUFFER) N->bound_array = id; else N->bound_element = id; }
static void n_BufferData(GLenum t, GLsizeiptr size, const void *data, GLenum usage) {
    (void)usage; struct nbuf *b = buf_of(N ? (t == GL_ARRAY_BUFFER ? N->bound_array : N->bound_element) : 0);
    if (!b || size < 0 || size > (1 << 28)) return;
    free(b->data); b->data = calloc(1, (size_t)size + 1); b->size = size;
    if (data) memcpy(b->data, data, (size_t)size);
}
static void *n_MapBuffer(GLenum t, GLenum access) { (void)access; struct nbuf *b = buf_of(N ? (t == GL_ARRAY_BUFFER ? N->bound_array : N->bound_element) : 0); return b ? b->data : NULL; }
static void *n_MapBufferRange(GLenum t, GLintptr off, GLsizeiptr len, GLbitfield a) { (void)len; (void)a; uint8_t *p = n_MapBuffer(t, 0); return p ? p + off : NULL; }
static GLboolean n_Unmap(GLenum t) { (void)t; return 1; }
static void n_GetBufferParameteriv(GLenum t, GLenum p, GLint *out) {
    struct nbuf *b = buf_of(N ? (t == GL_ARRAY_BUFFER ? N->bound_array : N->bound_element) : 0);
    if (out) *out = b && p == GL_BUFFER_SIZE ? (GLint)b->size : 0;
}

/* ------------------------------------------------------------------ queries */
static GLenum n_GetError(void) { if (!N) return 0; GLenum e = N->error; N->error = 0; return e; }
static void n_GetIntegerv(GLenum p, GLint *out) {
    if (!out) return;
    switch (p) {
    case GL_FRAMEBUFFER_BINDING: *out = N ? (GLint)N->bound_fb : 0; break;
    case GL_RENDERBUFFER_BINDING: *out = N ? (GLint)N->bound_rb : 0; break;
    case GL_VIEWPORT: for (int i = 0; i < 4; i++) out[i] = N ? N->viewport[i] : 0; break;
    case GL_SCISSOR_BOX: for (int i = 0; i < 4; i++) out[i] = N ? N->scissor[i] : 0; break;
    case GL_MAX_TEXTURE_SIZE: *out = 4096; break;
    case GL_MAX_VIEWPORT_DIMS: out[0] = out[1] = 4096; break;
    case 0x8869: *out = 16; break;      /* GL_MAX_VERTEX_ATTRIBS */
    case 0x8872: *out = 8; break;       /* GL_MAX_TEXTURE_IMAGE_UNITS */
    case 0x84E2: *out = 8; break;       /* GL_MAX_TEXTURE_UNITS (ES 1) */
    case 0x8D57: *out = 4; break;       /* GL_MAX_SAMPLES */
    case 0x86A2: *out = 0; break;       /* GL_NUM_COMPRESSED_TEXTURE_FORMATS */
    default: *out = 0;
    }
}
static void n_GetFloatv(GLenum p, GLfloat *out) {
    if (!out) return;
    if (p == GL_COLOR_CLEAR_VALUE) { for (int i = 0; i < 4; i++) out[i] = N ? N->clear[i] : 0; return; }
    GLint v[4] = { 0 }; n_GetIntegerv(p, v); out[0] = (GLfloat)v[0];
}
static void n_GetBooleanv(GLenum p, GLboolean *out) { if (out) { GLint v[4] = { 0 }; n_GetIntegerv(p, v); *out = v[0] != 0; } }
static const unsigned char *n_GetString(GLenum e) {
    int es1 = N && N->api == 1;
    switch (e) {
    case GL_VENDOR: return (const unsigned char *)"iosrt";
    case GL_RENDERER: return (const unsigned char *)"iosrt null GL (no device driver)";
    case GL_VERSION: return (const unsigned char *)(es1 ? "OpenGL ES-CM 1.1 iosrt-null" : "OpenGL ES 2.0 iosrt-null");
    case GL_SHADING_LANGUAGE_VERSION: return (const unsigned char *)(es1 ? "" : "OpenGL ES GLSL ES 1.00");
    case GL_EXTENSIONS: return (const unsigned char *)(es1 ? "GL_OES_framebuffer_object GL_OES_rgb8_rgba8 GL_OES_mapbuffer GL_OES_draw_texture"
                                                           : "GL_OES_rgb8_rgba8 GL_OES_depth24 GL_OES_mapbuffer GL_OES_vertex_array_object GL_OES_packed_depth_stencil");
    }
    return NULL;
}
static void n_GetShaderiv(GLuint s, GLenum p, GLint *out) { (void)s; if (out) *out = p == GL_COMPILE_STATUS ? 1 : 0; }
static void n_GetProgramiv(GLuint s, GLenum p, GLint *out) { (void)s; if (out) *out = p == GL_LINK_STATUS || p == GL_VALIDATE_STATUS ? 1 : 0; }
static void n_GetInfoLog(GLuint o, GLsizei max, GLsizei *len, char *log) { (void)o; if (len) *len = 0; if (log && max > 0) log[0] = 0; }
static GLint n_GetLocation(GLuint prog, const char *name) { (void)prog; unsigned h = 0; for (const char *p = name ? name : ""; *p; p++) h = h * 31 + (unsigned char)*p; return (GLint)(h % 15); }

/* ------------------------------------------------------------------ table */
static long noop(void) { return 0; }
void *nullgl_noop(void) { return (void *)noop; }
static const struct { const char *name; void *fn; } T[] = {
    { "glGenRenderbuffers", n_GenRenderbuffers }, { "glGenRenderbuffersOES", n_GenRenderbuffers },
    { "glGenFramebuffers", n_GenFramebuffers }, { "glGenFramebuffersOES", n_GenFramebuffers },
    { "glGenBuffers", n_GenBuffers }, { "glGenTextures", n_Gen }, { "glGenVertexArrays", n_Gen }, { "glGenVertexArraysOES", n_Gen },
    { "glGenQueries", n_Gen }, { "glGenQueriesEXT", n_Gen }, { "glGenSamplers", n_Gen },
    { "glCreateProgram", n_Create }, { "glCreateShader", n_CreateShader },
    { "glDeleteRenderbuffers", n_DeleteRenderbuffers }, { "glDeleteRenderbuffersOES", n_DeleteRenderbuffers },
    { "glDeleteFramebuffers", n_DeleteFramebuffers }, { "glDeleteFramebuffersOES", n_DeleteFramebuffers }, { "glDeleteBuffers", n_DeleteBuffers },
    { "glBindRenderbuffer", n_BindRenderbuffer }, { "glBindRenderbufferOES", n_BindRenderbuffer },
    { "glBindFramebuffer", n_BindFramebuffer }, { "glBindFramebufferOES", n_BindFramebuffer },
    { "glRenderbufferStorage", n_RenderbufferStorage }, { "glRenderbufferStorageOES", n_RenderbufferStorage },
    { "glRenderbufferStorageMultisample", n_RenderbufferStorageMultisample }, { "glRenderbufferStorageMultisampleEXT", n_RenderbufferStorageMultisample },
    { "glGetRenderbufferParameteriv", n_GetRenderbufferParameteriv }, { "glGetRenderbufferParameterivOES", n_GetRenderbufferParameteriv },
    { "glFramebufferRenderbuffer", n_FramebufferRenderbuffer }, { "glFramebufferRenderbufferOES", n_FramebufferRenderbuffer },
    { "glCheckFramebufferStatus", n_CheckFramebufferStatus }, { "glCheckFramebufferStatusOES", n_CheckFramebufferStatus },
    { "glGetFramebufferAttachmentParameteriv", n_GetFramebufferAttachmentParameteriv }, { "glGetFramebufferAttachmentParameterivOES", n_GetFramebufferAttachmentParameteriv },
    { "glClearColor", n_ClearColor }, { "glClearColorx", n_ClearColorx }, { "glClear", n_Clear }, { "glScissor", n_Scissor }, { "glViewport", n_Viewport },
    { "glEnable", n_Enable }, { "glDisable", n_Disable }, { "glIsEnabled", n_IsEnabled }, { "glReadPixels", n_ReadPixels },
    { "glBindBuffer", n_BindBuffer }, { "glBufferData", n_BufferData }, { "glMapBufferOES", n_MapBuffer }, { "glMapBufferRange", n_MapBufferRange },
    { "glMapBufferRangeEXT", n_MapBufferRange }, { "glUnmapBufferOES", n_Unmap }, { "glUnmapBuffer", n_Unmap }, { "glGetBufferParameteriv", n_GetBufferParameteriv },
    { "glGetError", n_GetError }, { "glGetIntegerv", n_GetIntegerv }, { "glGetFloatv", n_GetFloatv }, { "glGetBooleanv", n_GetBooleanv },
    { "glGetString", n_GetString }, { "glGetShaderiv", n_GetShaderiv }, { "glGetProgramiv", n_GetProgramiv },
    { "glGetShaderInfoLog", n_GetInfoLog }, { "glGetProgramInfoLog", n_GetInfoLog },
    { "glGetUniformLocation", n_GetLocation }, { "glGetAttribLocation", n_GetLocation },
};
void *nullgl_lookup(const char *name) {
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) if (!strcmp(T[i].name, name)) return T[i].fn;
    return !strncmp(name, "gl", 2) ? (void *)noop : NULL;
}
