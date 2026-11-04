/* OpenGL ES 1.1 (Khronos API), for the iosrt mini-SDK. The functions shared
 * with ES 2.0 and the common constants come from ES2/gl.h. */
#ifndef IOSRT_ES1_GL_H
#define IOSRT_ES1_GL_H
#include <OpenGLES/ES2/gl.h>
#define GL_VERSION_ES_CM_1_0 1
#define GL_VERSION_ES_CL_1_1 1
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_TEXTURE_MATRIX 0x0BA8
#define GL_MATRIX_MODE 0x0BA0
#define GL_VERTEX_ARRAY 0x8074
#define GL_NORMAL_ARRAY 0x8075
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_POINT_SIZE_ARRAY_OES 0x8B9C
#define GL_LIGHTING 0x0B50
#define GL_LIGHT0 0x4000
#define GL_LIGHT1 0x4001
#define GL_AMBIENT 0x1200
#define GL_DIFFUSE 0x1201
#define GL_SPECULAR 0x1202
#define GL_POSITION 0x1203
#define GL_SHININESS 0x1601
#define GL_EMISSION 0x1600
#define GL_AMBIENT_AND_DIFFUSE 0x1602
#define GL_COLOR_MATERIAL 0x0B57
#define GL_NORMALIZE 0x0BA1
#define GL_RESCALE_NORMAL 0x803A
#define GL_SMOOTH 0x1D01
#define GL_FLAT 0x1D00
#define GL_FOG 0x0B60
#define GL_FOG_MODE 0x0B65
#define GL_FOG_DENSITY 0x0B62
#define GL_FOG_START 0x0B63
#define GL_FOG_END 0x0B64
#define GL_FOG_COLOR 0x0B66
#define GL_EXP 0x0800
#define GL_EXP2 0x0801
#define GL_ALPHA_TEST 0x0BC0
#define GL_POINT_SMOOTH 0x0B10
#define GL_LINE_SMOOTH 0x0B20
#define GL_MULTISAMPLE 0x809D
#define GL_COLOR_LOGIC_OP 0x0BF2
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_TEXTURE_ENV_COLOR 0x2201
#define GL_MODULATE 0x2100
#define GL_DECAL 0x2101
#define GL_ADD 0x0104
#define GL_COMBINE 0x8570
#define GL_GENERATE_MIPMAP 0x8191
#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#define GL_POINT_SMOOTH_HINT 0x0C51
#define GL_LINE_SMOOTH_HINT 0x0C52
#define GL_FOG_HINT 0x0C54
#define GL_MAX_LIGHTS 0x0D31
#define GL_MAX_TEXTURE_UNITS 0x84E2
#define GL_MAX_MODELVIEW_STACK_DEPTH 0x0D36
#define GL_CLIENT_ACTIVE_TEXTURE 0x84E1
#define GL_POINT_SPRITE_OES 0x8861
#define GL_COORD_REPLACE_OES 0x8862
GL_API void glAlphaFunc(GLenum func, GLfloat ref);
GL_API void glAlphaFuncx(GLenum func, GLfixed ref);
GL_API void glClearColorx(GLfixed r, GLfixed g, GLfixed b, GLfixed a);
GL_API void glClearDepthx(GLfixed d);
GL_API void glClientActiveTexture(GLenum texture);
GL_API void glClipPlanef(GLenum plane, const GLfloat *equation);
GL_API void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
GL_API void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
GL_API void glColor4x(GLfixed r, GLfixed g, GLfixed b, GLfixed a);
GL_API void glColorPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
GL_API void glDepthRangex(GLfixed n, GLfixed f);
GL_API void glDisableClientState(GLenum array);
GL_API void glEnableClientState(GLenum array);
GL_API void glFogf(GLenum pname, GLfloat param);
GL_API void glFogfv(GLenum pname, const GLfloat *params);
GL_API void glFogx(GLenum pname, GLfixed param);
GL_API void glFrustumf(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f);
GL_API void glFrustumx(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f);
GL_API void glGetFixedv(GLenum pname, GLfixed *params);
GL_API void glGetPointerv(GLenum pname, void **params);
GL_API void glLightModelf(GLenum pname, GLfloat param);
GL_API void glLightModelfv(GLenum pname, const GLfloat *params);
GL_API void glLightf(GLenum light, GLenum pname, GLfloat param);
GL_API void glLightfv(GLenum light, GLenum pname, const GLfloat *params);
GL_API void glLineWidthx(GLfixed width);
GL_API void glLoadIdentity(void);
GL_API void glLoadMatrixf(const GLfloat *m);
GL_API void glLoadMatrixx(const GLfixed *m);
GL_API void glLogicOp(GLenum opcode);
GL_API void glMaterialf(GLenum face, GLenum pname, GLfloat param);
GL_API void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params);
GL_API void glMatrixMode(GLenum mode);
GL_API void glMultMatrixf(const GLfloat *m);
GL_API void glMultMatrixx(const GLfixed *m);
GL_API void glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);
GL_API void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);
GL_API void glNormalPointer(GLenum type, GLsizei stride, const void *pointer);
GL_API void glOrthof(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f);
GL_API void glOrthox(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f);
GL_API void glPointParameterf(GLenum pname, GLfloat param);
GL_API void glPointParameterfv(GLenum pname, const GLfloat *params);
GL_API void glPointSize(GLfloat size);
GL_API void glPointSizex(GLfixed size);
GL_API void glPopMatrix(void);
GL_API void glPushMatrix(void);
GL_API void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
GL_API void glRotatex(GLfixed angle, GLfixed x, GLfixed y, GLfixed z);
GL_API void glScalef(GLfloat x, GLfloat y, GLfloat z);
GL_API void glScalex(GLfixed x, GLfixed y, GLfixed z);
GL_API void glShadeModel(GLenum mode);
GL_API void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
GL_API void glTexEnvf(GLenum target, GLenum pname, GLfloat param);
GL_API void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params);
GL_API void glTexEnvi(GLenum target, GLenum pname, GLint param);
GL_API void glTexEnvx(GLenum target, GLenum pname, GLfixed param);
GL_API void glTexParameterx(GLenum target, GLenum pname, GLfixed param);
GL_API void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
GL_API void glTranslatex(GLfixed x, GLfixed y, GLfixed z);
GL_API void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
#endif
