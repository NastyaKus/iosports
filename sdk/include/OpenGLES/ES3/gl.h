/* OpenGL ES 3.0 subset, for the iosrt mini-SDK. */
#ifndef IOSRT_ES3_GL_H
#define IOSRT_ES3_GL_H
#include <OpenGLES/ES2/gl.h>
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RGBA8 0x8058
#define GL_DEPTH24_STENCIL8 0x88F0
GL_API void glBindVertexArray(GLuint array);
GL_API void glGenVertexArrays(GLsizei n, GLuint *arrays);
GL_API void glDeleteVertexArrays(GLsizei n, const GLuint *arrays);
GL_API void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
GL_API void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels);
GL_API void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
GL_API void *glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
GL_API GLboolean glUnmapBuffer(GLenum target);
GL_API void glInvalidateFramebuffer(GLenum target, GLsizei numAttachments, const GLenum *attachments);
#endif
