/* OpenGL ES base types (Khronos), for the iosrt mini-SDK. */
#ifndef IOSRT_GLTYPES_H
#define IOSRT_GLTYPES_H
#include <stdint.h>
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef int GLfixed;
typedef int GLclampx;
typedef void GLvoid;
typedef char GLchar;
typedef long GLintptr;
typedef long GLsizeiptr;
typedef int64_t GLint64;
typedef uint64_t GLuint64;
typedef struct __GLsync *GLsync;
#define GL_API extern
#define GL_APIENTRY
#endif
