/* OpenGL ES 2.0 extensions iOS apps use, for the iosrt mini-SDK. */
#ifndef IOSRT_ES2_GLEXT_H
#define IOSRT_ES2_GLEXT_H
#include <OpenGLES/ES2/gl.h>
#define GL_RGBA8_OES 0x8058
#define GL_RGB8_OES 0x8051
#define GL_DEPTH_COMPONENT24_OES 0x81A6
#define GL_DEPTH24_STENCIL8_OES 0x88F0
#define GL_BGRA_EXT 0x80E1
#define GL_BGRA 0x80E1
#define GL_WRITE_ONLY_OES 0x88B9
#define GL_VERTEX_ARRAY_BINDING_OES 0x85B5
#define GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG 0x8C00
#define GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG 0x8C01
#define GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG 0x8C02
#define GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG 0x8C03
#define GL_RENDERBUFFER_SAMPLES_APPLE 0x8CAB
#define GL_MAX_SAMPLES_APPLE 0x8D57
#define GL_READ_FRAMEBUFFER_APPLE 0x8CA8
#define GL_DRAW_FRAMEBUFFER_APPLE 0x8CA9
#define GL_COLOR_EXT 0x1800
#define GL_DEPTH_EXT 0x1801
#define GL_STENCIL_EXT 0x1802
GL_API void glGenVertexArraysOES(GLsizei n, GLuint *arrays);
GL_API void glBindVertexArrayOES(GLuint array);
GL_API void glDeleteVertexArraysOES(GLsizei n, const GLuint *arrays);
GL_API GLboolean glIsVertexArrayOES(GLuint array);
GL_API void *glMapBufferOES(GLenum target, GLenum access);
GL_API GLboolean glUnmapBufferOES(GLenum target);
GL_API void glRenderbufferStorageMultisampleAPPLE(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
GL_API void glResolveMultisampleFramebufferAPPLE(void);
GL_API void glDiscardFramebufferEXT(GLenum target, GLsizei numAttachments, const GLenum *attachments);
GL_API void glLabelObjectEXT(GLenum type, GLuint object, GLsizei length, const GLchar *label);
GL_API void glInsertEventMarkerEXT(GLsizei length, const GLchar *marker);
GL_API void glPushGroupMarkerEXT(GLsizei length, const GLchar *marker);
GL_API void glPopGroupMarkerEXT(void);
#endif
