/* OpenGL ES for guest apps.
 *
 * Guest code calls GL entry points directly: the loader binds OpenGLES
 * imports to the device driver (libGLESv2 / libGLESv1_CM / eglGetProcAddress)
 * through rt_gl_resolve(). Only functions whose Darwin calling convention
 * differs from AAPCS64, Apple-only extensions and formats the Android drivers
 * lack (PVRTC, BGRA uploads) have wrappers here (gl.def). Without a driver
 * (static CLI builds, the host) a small software "null GL" stands in: it
 * keeps objects and renderbuffer pixels, clears, scissors and reads back, so
 * the EAGL path can be tested end to end.
 *
 * Constants are written from the Khronos specifications. */
#pragma once
#include "../internal.h"
#include <stdint.h>

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef intptr_t GLintptr, GLsizeiptr;

#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_OPERATION 0x0502
#define GL_SCISSOR_TEST 0x0C11
#define GL_SCISSOR_BOX 0x0C10
#define GL_VIEWPORT 0x0BA2
#define GL_COLOR_CLEAR_VALUE 0x0C22
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_VIEWPORT_DIMS 0x0D3A
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_STENCIL_BUFFER_BIT 0x0400
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RGBA 0x1908
#define GL_RGB 0x1907
#define GL_BGRA_EXT 0x80E1
#define GL_NEAREST 0x2600
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_TEXTURE_2D 0x0DE1
#define GL_ARRAY_BUFFER 0x8892
#define GL_BUFFER_SIZE 0x8764
#define GL_MAP_WRITE_BIT 0x0002
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_RENDERBUFFER 0x8D41
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_RENDERBUFFER_BINDING 0x8CA7
#define GL_RENDERBUFFER_WIDTH 0x8D42
#define GL_RENDERBUFFER_HEIGHT 0x8D43
#define GL_RENDERBUFFER_INTERNAL_FORMAT 0x8D44
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_STENCIL_ATTACHMENT 0x8D20
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME 0x8CD1
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT 0x8CD6
#define GL_RGBA8_OES 0x8058
#define GL_RGB565 0x8D62
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VALIDATE_STATUS 0x8B83
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG 0x8C00
#define GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG 0x8C01
#define GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG 0x8C02
#define GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG 0x8C03

/* a rendering context: EGL on a device, null GL otherwise */
struct rt_glctx {
    int api;                       /* EAGLRenderingAPI: 1, 2, 3 */
    void *egl_ctx, *egl_surf;      /* driver mode */
    struct nullgl *null;           /* null mode */
    char *extensions;              /* GL_EXTENSIONS as reported to the app */
    GLuint read_fbo;               /* presentRenderbuffer: reads the drawable through this */
};

int rt_gl_real(void);                                  /* 1: a device driver is in use */
void *rt_gl_driver(const char *name);                  /* entry point without the iosrt wrappers */
struct rt_glctx *rt_gl_ctx_new(int api, struct rt_glctx *share);
void rt_gl_ctx_free(struct rt_glctx *c);
int rt_gl_make_current(struct rt_glctx *c);           /* NULL unbinds this thread */
struct rt_glctx *rt_gl_current(void);

/* gl_null.c */
struct nullgl *nullgl_new(int api);
void nullgl_free(struct nullgl *n);
void nullgl_set_current(struct nullgl *n);
void *nullgl_lookup(const char *name);
void *nullgl_noop(void);

/* pvrtc.c: decodes PVRTC 2/4 bpp (v1) into RGBA8 rows (top row first as stored) */
size_t rt_pvrtc_size(int w, int h, int bpp2);
void rt_pvrtc_decode(const uint8_t *src, int w, int h, int bpp2, uint8_t *rgba);
