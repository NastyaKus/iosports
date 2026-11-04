/* OpenGL ES 1.1 extensions iOS apps use (OES framebuffer objects and friends), for the iosrt mini-SDK. */
#ifndef IOSRT_ES1_GLEXT_H
#define IOSRT_ES1_GLEXT_H
#include <OpenGLES/ES1/gl.h>
#include <OpenGLES/ES2/glext.h>
#define GL_FRAMEBUFFER_OES 0x8D40
#define GL_RENDERBUFFER_OES 0x8D41
#define GL_RENDERBUFFER_WIDTH_OES 0x8D42
#define GL_RENDERBUFFER_HEIGHT_OES 0x8D43
#define GL_COLOR_ATTACHMENT0_OES 0x8CE0
#define GL_DEPTH_ATTACHMENT_OES 0x8D00
#define GL_DEPTH_COMPONENT16_OES 0x81A5
#define GL_FRAMEBUFFER_COMPLETE_OES 0x8CD5
#define GL_FRAMEBUFFER_BINDING_OES 0x8CA6
#define GL_RENDERBUFFER_BINDING_OES 0x8CA7
#define GL_TEXTURE_CROP_RECT_OES 0x8B9D
GL_API void glGenFramebuffersOES(GLsizei n, GLuint *framebuffers);
GL_API void glBindFramebufferOES(GLenum target, GLuint framebuffer);
GL_API void glDeleteFramebuffersOES(GLsizei n, const GLuint *framebuffers);
GL_API GLboolean glIsFramebufferOES(GLuint framebuffer);
GL_API void glGenRenderbuffersOES(GLsizei n, GLuint *renderbuffers);
GL_API void glBindRenderbufferOES(GLenum target, GLuint renderbuffer);
GL_API void glDeleteRenderbuffersOES(GLsizei n, const GLuint *renderbuffers);
GL_API GLboolean glIsRenderbufferOES(GLuint renderbuffer);
GL_API void glRenderbufferStorageOES(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
GL_API void glGetRenderbufferParameterivOES(GLenum target, GLenum pname, GLint *params);
GL_API GLenum glCheckFramebufferStatusOES(GLenum target);
GL_API void glFramebufferRenderbufferOES(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
GL_API void glFramebufferTexture2DOES(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
GL_API void glGenerateMipmapOES(GLenum target);
GL_API void glDrawTexfOES(GLfloat x, GLfloat y, GLfloat z, GLfloat width, GLfloat height);
GL_API void glDrawTexiOES(GLint x, GLint y, GLint z, GLint width, GLint height);
#endif
