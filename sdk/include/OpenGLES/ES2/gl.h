/* OpenGL ES 2.0 (Khronos API), for the iosrt mini-SDK. */
#ifndef IOSRT_ES2_GL_H
#define IOSRT_ES2_GL_H
#include <OpenGLES/gltypes.h>
#define GL_ES_VERSION_2_0 1
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE 0x0308
#define GL_FUNC_ADD 0x8006
#define GL_BLEND_EQUATION 0x8009
#define GL_FUNC_SUBTRACT 0x800A
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_STREAM_DRAW 0x88E0
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_BUFFER_SIZE 0x8764
#define GL_BUFFER_USAGE 0x8765
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_TEXTURE_2D 0x0DE1
#define GL_CULL_FACE 0x0B44
#define GL_BLEND 0x0BE2
#define GL_DITHER 0x0BD0
#define GL_STENCIL_TEST 0x0B90
#define GL_DEPTH_TEST 0x0B71
#define GL_SCISSOR_TEST 0x0C11
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#define GL_SAMPLE_COVERAGE 0x80A0
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_OUT_OF_MEMORY 0x0505
#define GL_INVALID_FRAMEBUFFER_OPERATION 0x0506
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_LINE_WIDTH 0x0B21
#define GL_VIEWPORT 0x0BA2
#define GL_SCISSOR_BOX 0x0C10
#define GL_COLOR_CLEAR_VALUE 0x0C22
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_VIEWPORT_DIMS 0x0D3A
#define GL_DONT_CARE 0x1100
#define GL_FASTEST 0x1101
#define GL_NICEST 0x1102
#define GL_GENERATE_MIPMAP_HINT 0x8192
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_FIXED 0x140C
#define GL_DEPTH_COMPONENT 0x1902
#define GL_ALPHA 0x1906
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_MAX_VERTEX_ATTRIBS 0x8869
#define GL_MAX_TEXTURE_IMAGE_UNITS 0x8872
#define GL_SHADER_TYPE 0x8B4F
#define GL_DELETE_STATUS 0x8B80
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_VALIDATE_STATUS 0x8B83
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_ATTACHED_SHADERS 0x8B85
#define GL_ACTIVE_UNIFORMS 0x8B86
#define GL_ACTIVE_ATTRIBUTES 0x8B89
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207
#define GL_KEEP 0x1E00
#define GL_REPLACE 0x1E01
#define GL_INCR 0x1E02
#define GL_DECR 0x1E03
#define GL_INVERT 0x150A
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE 0x1702
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MIRRORED_REPEAT 0x8370
#define GL_FLOAT_VEC2 0x8B50
#define GL_FLOAT_VEC3 0x8B51
#define GL_FLOAT_VEC4 0x8B52
#define GL_FLOAT_MAT4 0x8B5C
#define GL_SAMPLER_2D 0x8B5E
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_RGBA4 0x8056
#define GL_RGB5_A1 0x8057
#define GL_RGB565 0x8D62
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_STENCIL_INDEX8 0x8D48
#define GL_RENDERBUFFER_WIDTH 0x8D42
#define GL_RENDERBUFFER_HEIGHT 0x8D43
#define GL_RENDERBUFFER_INTERNAL_FORMAT 0x8D44
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_STENCIL_ATTACHMENT 0x8D20
#define GL_NONE 0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT 0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#define GL_FRAMEBUFFER_UNSUPPORTED 0x8CDD
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_RENDERBUFFER_BINDING 0x8CA7
#define GL_MAX_RENDERBUFFER_SIZE 0x84E8
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME 0x8CD1

GL_API void glActiveTexture(GLenum texture);
GL_API void glAttachShader(GLuint program, GLuint shader);
GL_API void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name);
GL_API void glBindBuffer(GLenum target, GLuint buffer);
GL_API void glBindFramebuffer(GLenum target, GLuint framebuffer);
GL_API void glBindRenderbuffer(GLenum target, GLuint renderbuffer);
GL_API void glBindTexture(GLenum target, GLuint texture);
GL_API void glBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
GL_API void glBlendEquation(GLenum mode);
GL_API void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);
GL_API void glBlendFunc(GLenum sfactor, GLenum dfactor);
GL_API void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
GL_API void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
GL_API void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
GL_API GLenum glCheckFramebufferStatus(GLenum target);
GL_API void glClear(GLbitfield mask);
GL_API void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
GL_API void glClearDepthf(GLfloat d);
GL_API void glClearStencil(GLint s);
GL_API void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
GL_API void glCompileShader(GLuint shader);
GL_API void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data);
GL_API void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data);
GL_API void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
GL_API void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
GL_API GLuint glCreateProgram(void);
GL_API GLuint glCreateShader(GLenum type);
GL_API void glCullFace(GLenum mode);
GL_API void glDeleteBuffers(GLsizei n, const GLuint *buffers);
GL_API void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers);
GL_API void glDeleteProgram(GLuint program);
GL_API void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers);
GL_API void glDeleteShader(GLuint shader);
GL_API void glDeleteTextures(GLsizei n, const GLuint *textures);
GL_API void glDepthFunc(GLenum func);
GL_API void glDepthMask(GLboolean flag);
GL_API void glDepthRangef(GLfloat n, GLfloat f);
GL_API void glDetachShader(GLuint program, GLuint shader);
GL_API void glDisable(GLenum cap);
GL_API void glDisableVertexAttribArray(GLuint index);
GL_API void glDrawArrays(GLenum mode, GLint first, GLsizei count);
GL_API void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
GL_API void glEnable(GLenum cap);
GL_API void glEnableVertexAttribArray(GLuint index);
GL_API void glFinish(void);
GL_API void glFlush(void);
GL_API void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
GL_API void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
GL_API void glFrontFace(GLenum mode);
GL_API void glGenBuffers(GLsizei n, GLuint *buffers);
GL_API void glGenerateMipmap(GLenum target);
GL_API void glGenFramebuffers(GLsizei n, GLuint *framebuffers);
GL_API void glGenRenderbuffers(GLsizei n, GLuint *renderbuffers);
GL_API void glGenTextures(GLsizei n, GLuint *textures);
GL_API void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
GL_API void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
GL_API void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei *count, GLuint *shaders);
GL_API GLint glGetAttribLocation(GLuint program, const GLchar *name);
GL_API void glGetBooleanv(GLenum pname, GLboolean *data);
GL_API void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params);
GL_API GLenum glGetError(void);
GL_API void glGetFloatv(GLenum pname, GLfloat *data);
GL_API void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params);
GL_API void glGetIntegerv(GLenum pname, GLint *data);
GL_API void glGetProgramiv(GLuint program, GLenum pname, GLint *params);
GL_API void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
GL_API void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params);
GL_API void glGetShaderiv(GLuint shader, GLenum pname, GLint *params);
GL_API void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
GL_API void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision);
GL_API void glGetShaderSource(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source);
GL_API const GLubyte *glGetString(GLenum name);
GL_API void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params);
GL_API void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params);
GL_API void glGetUniformfv(GLuint program, GLint location, GLfloat *params);
GL_API void glGetUniformiv(GLuint program, GLint location, GLint *params);
GL_API GLint glGetUniformLocation(GLuint program, const GLchar *name);
GL_API void glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params);
GL_API void glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params);
GL_API void glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer);
GL_API void glHint(GLenum target, GLenum mode);
GL_API GLboolean glIsBuffer(GLuint buffer);
GL_API GLboolean glIsEnabled(GLenum cap);
GL_API GLboolean glIsFramebuffer(GLuint framebuffer);
GL_API GLboolean glIsProgram(GLuint program);
GL_API GLboolean glIsRenderbuffer(GLuint renderbuffer);
GL_API GLboolean glIsShader(GLuint shader);
GL_API GLboolean glIsTexture(GLuint texture);
GL_API void glLineWidth(GLfloat width);
GL_API void glLinkProgram(GLuint program);
GL_API void glPixelStorei(GLenum pname, GLint param);
GL_API void glPolygonOffset(GLfloat factor, GLfloat units);
GL_API void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels);
GL_API void glReleaseShaderCompiler(void);
GL_API void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
GL_API void glSampleCoverage(GLfloat value, GLboolean invert);
GL_API void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
GL_API void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryformat, const void *binary, GLsizei length);
GL_API void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
GL_API void glStencilFunc(GLenum func, GLint ref, GLuint mask);
GL_API void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
GL_API void glStencilMask(GLuint mask);
GL_API void glStencilMaskSeparate(GLenum face, GLuint mask);
GL_API void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
GL_API void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass);
GL_API void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
GL_API void glTexParameterf(GLenum target, GLenum pname, GLfloat param);
GL_API void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
GL_API void glTexParameteri(GLenum target, GLenum pname, GLint param);
GL_API void glTexParameteriv(GLenum target, GLenum pname, const GLint *params);
GL_API void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels);
GL_API void glUniform1f(GLint location, GLfloat v0);
GL_API void glUniform1fv(GLint location, GLsizei count, const GLfloat *value);
GL_API void glUniform1i(GLint location, GLint v0);
GL_API void glUniform1iv(GLint location, GLsizei count, const GLint *value);
GL_API void glUniform2f(GLint location, GLfloat v0, GLfloat v1);
GL_API void glUniform2fv(GLint location, GLsizei count, const GLfloat *value);
GL_API void glUniform2i(GLint location, GLint v0, GLint v1);
GL_API void glUniform2iv(GLint location, GLsizei count, const GLint *value);
GL_API void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
GL_API void glUniform3fv(GLint location, GLsizei count, const GLfloat *value);
GL_API void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2);
GL_API void glUniform3iv(GLint location, GLsizei count, const GLint *value);
GL_API void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
GL_API void glUniform4fv(GLint location, GLsizei count, const GLfloat *value);
GL_API void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
GL_API void glUniform4iv(GLint location, GLsizei count, const GLint *value);
GL_API void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
GL_API void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
GL_API void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
GL_API void glUseProgram(GLuint program);
GL_API void glValidateProgram(GLuint program);
GL_API void glVertexAttrib1f(GLuint index, GLfloat x);
GL_API void glVertexAttrib1fv(GLuint index, const GLfloat *v);
GL_API void glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y);
GL_API void glVertexAttrib2fv(GLuint index, const GLfloat *v);
GL_API void glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z);
GL_API void glVertexAttrib3fv(GLuint index, const GLfloat *v);
GL_API void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
GL_API void glVertexAttrib4fv(GLuint index, const GLfloat *v);
GL_API void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
GL_API void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
#endif
