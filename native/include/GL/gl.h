#ifndef C3DGL_NATIVE_GL_H
#define C3DGL_NATIVE_GL_H
#include <stddef.h>
/* Implemented native subset only; additional GL groups follow incrementally. */
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef short GLshort;
typedef unsigned int GLuint;
typedef void GLvoid;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLdouble;
#define GL_DOUBLE 0x140a
#define GL_FIXED 0x140c
#define GL_TEXTURE 0x1702
#define GL_TEXTURE_MATRIX 0x0ba8
#define GL_TEXTURE_STACK_DEPTH 0x0ba5
#define GL_MAX_TEXTURE_STACK_DEPTH 0x0d39
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_TEXTURE_2D 0x0de1
#define GL_TEXTURE0 0x84c0
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_MODULATE 0x2100
#define GL_REPLACE 0x1e01
void glTexEnvi(GLenum target, GLenum pname, GLint param);
#define GL_ACTIVE_TEXTURE 0x84e0
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_MAX_TEXTURE_SIZE 0x0d33
#define GL_UNPACK_ALIGNMENT 0x0cf5
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812f
#define GL_MIRRORED_REPEAT 0x8370
void glActiveTexture(GLenum unit);
void glGenTextures(GLsizei n, GLuint *textures);
void glBindTexture(GLenum target, GLuint texture);
void glDeleteTextures(GLsizei n, const GLuint *textures);
GLboolean glIsTexture(GLuint texture);
void glPixelStorei(GLenum pname, GLint param);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params);
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void glTexCoord2f(GLfloat s, GLfloat t);
void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r);
void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void glTexCoord3fv(const GLfloat *v);
void glTexCoord4fv(const GLfloat *v);
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
#define GL_BLEND 0x0be2
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
#define GL_CONSTANT_COLOR 0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#define GL_CONSTANT_ALPHA 0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#define GL_BLEND_COLOR 0x8005
#define GL_FUNC_ADD 0x8006
#define GL_MIN 0x8007
#define GL_MAX 0x8008
#define GL_BLEND_EQUATION 0x8009
#define GL_BLEND_EQUATION_RGB 0x8009
#define GL_BLEND_EQUATION_ALPHA 0x883d
#define GL_FUNC_SUBTRACT 0x800a
#define GL_FUNC_REVERSE_SUBTRACT 0x800b
#define GL_BLEND_SRC 0x0be1
#define GL_BLEND_DST 0x0be0
#define GL_BLEND_SRC_RGB 0x80c9
#define GL_BLEND_DST_RGB 0x80c8
#define GL_BLEND_SRC_ALPHA 0x80cb
#define GL_BLEND_DST_ALPHA 0x80ca
void glBlendFunc(GLenum src, GLenum dst);
void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
void glBlendEquation(GLenum mode);
void glBlendEquationSeparate(GLenum rgb, GLenum alpha);
void glBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
#define GL_DEPTH_TEST 0x0b71
#define GL_FLAT 0x1d00
#define GL_SMOOTH 0x1d01
#define GL_SHADE_MODEL 0x0b54
#define GL_POINT 0x1b00
#define GL_LINE 0x1b01
#define GL_FILL 0x1b02
#define GL_POLYGON_MODE 0x0b40
#define GL_POLYGON_OFFSET_FACTOR 0x8038
#define GL_POLYGON_OFFSET_UNITS 0x2a00
#define GL_POLYGON_OFFSET_POINT 0x2a01
#define GL_POLYGON_OFFSET_LINE 0x2a02
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_POINT_SIZE 0x0b11
#define GL_LINE_WIDTH 0x0b21
#define GL_SCISSOR_BOX 0x0c10
#define GL_SCISSOR_TEST 0x0c11
#define GL_ALPHA_TEST 0x0bc0
#define GL_ALPHA_TEST_FUNC 0x0bc1
#define GL_ALPHA_TEST_REF 0x0bc2
#define GL_DEPTH_WRITEMASK 0x0b72
#define GL_DEPTH_FUNC 0x0b74
#define GL_DEPTH_RANGE 0x0b70
#define GL_CULL_FACE 0x0b44
#define GL_CULL_FACE_MODE 0x0b45
#define GL_FRONT_FACE 0x0b46
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207
void glEnable(GLenum cap);
void glDisable(GLenum cap);
GLboolean glIsEnabled(GLenum cap);
void glDepthMask(GLboolean flag);
void glDepthFunc(GLenum func);
void glAlphaFunc(GLenum func, GLfloat ref);
void glLineWidth(GLfloat width);
void glShadeModel(GLenum mode);
void glPolygonMode(GLenum face, GLenum mode);
void glPolygonOffset(GLfloat factor, GLfloat units);
void glEdgeFlag(GLboolean flag);
void glEdgeFlagv(const GLboolean *flag);
void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
void glRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2);
void glRecti(GLint x1, GLint y1, GLint x2, GLint y2);
void glRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2);
void glRectfv(const GLfloat *v1, const GLfloat *v2);
#define GL_AUTO_NORMAL 0x0d80
#define GL_MAP1_COLOR_4 0x0d90
#define GL_MAP1_VERTEX_3 0x0d97
#define GL_MAP1_VERTEX_4 0x0d98
#define GL_MAP2_COLOR_4 0x0db0
#define GL_MAP2_TEXTURE_COORD_2 0x0db4
#define GL_MAP2_VERTEX_3 0x0db7
#define GL_MAP2_VERTEX_4 0x0db8
#define GL_COEFF 0x0a00
#define GL_ORDER 0x0a01
#define GL_DOMAIN 0x0a02
void glMap1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points);
void glMap1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points);
void glMap2f(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points);
void glMap2d(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points);
void glMapGrid1f(GLint un, GLfloat u1, GLfloat u2);
void glMapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2);
void glEvalCoord1f(GLfloat u);
void glEvalCoord2f(GLfloat u, GLfloat v);
void glEvalPoint1(GLint i);
void glEvalPoint2(GLint i, GLint j);
void glEvalMesh1(GLenum mode, GLint i1, GLint i2);
void glEvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2);
void glGetMapfv(GLenum target, GLenum query, GLfloat *v);
void glPointSize(GLfloat size);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glDepthRange(GLdouble nearValue, GLdouble farValue);
void glDepthRangef(GLfloat nearValue, GLfloat farValue);
void glCullFace(GLenum face);
void glFrontFace(GLenum front);
void glGetBooleanv(GLenum pname, GLboolean *out);
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_QUADS 0x0007
#define GL_QUAD_STRIP 0x0008
#define GL_POLYGON 0x0009
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STREAM_DRAW 0x88e0
#define GL_STATIC_DRAW 0x88e4
#define GL_DYNAMIC_DRAW 0x88e8
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void glEnableClientState(GLenum array);
void glDisableClientState(GLenum array);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void glGenBuffers(GLsizei n, GLuint *buffers);
void glBindBuffer(GLenum target, GLuint buffer);
void glDeleteBuffers(GLsizei n, const GLuint *buffers);
void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data);
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_VIEWPORT 0x0ba2
#define GL_COLOR_CLEAR_VALUE 0x0c22
#define GL_DEPTH_CLEAR_VALUE 0x0b73
#define GL_MAX_VIEWPORT_DIMS 0x0d3a
#define GL_MATRIX_MODE 0x0ba0
#define GL_MODELVIEW_MATRIX 0x0ba6
#define GL_PROJECTION_MATRIX 0x0ba7
#define GL_MODELVIEW_STACK_DEPTH 0x0ba3
#define GL_PROJECTION_STACK_DEPTH 0x0ba4
#define GL_MAX_MODELVIEW_STACK_DEPTH 0x0d36
#define GL_MAX_PROJECTION_STACK_DEPTH 0x0d38
#define GL_INVALID_OPERATION 0x0502
#define GL_OUT_OF_MEMORY 0x0505
void glClearColor(GLfloat r,GLfloat g,GLfloat b,GLfloat a);
void glClearDepth(GLdouble depth);
void glClearDepthf(GLfloat depth);
void glClear(GLbitfield mask);
void glViewport(GLint x,GLint y,GLsizei w,GLsizei h);
void glGetFloatv(GLenum pname,GLfloat *out);
void glGetIntegerv(GLenum pname,GLint *out);
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_STACK_OVERFLOW 0x0503
#define GL_STACK_UNDERFLOW 0x0504
GLenum glGetError(void);
/* Filled triangle/strip/fan/quad/quad-strip/convex polygon batches; up to 4096 vertices. */
void glBegin(GLenum mode);
void glEnd(void);
void glVertex2fv(const GLfloat *v);
void glVertex3fv(const GLfloat *v);
void glVertex4fv(const GLfloat *v);
void glColor3fv(const GLfloat *v);
void glColor4fv(const GLfloat *v);
void glColor3ub(GLubyte r, GLubyte g, GLubyte b);
void glColor3ubv(const GLubyte *v);
void glColor4ubv(const GLubyte *v);
void glTexCoord2fv(const GLfloat *v);
void glVertex2f(GLfloat x, GLfloat y);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void glColor3f(GLfloat r, GLfloat g, GLfloat b);
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a);
void glMatrixMode(GLenum mode);
void glLoadIdentity(void);
void glLoadMatrixf(const GLfloat *m);
void glMultMatrixf(const GLfloat *m);
void glPushMatrix(void);
void glPopMatrix(void);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glScalef(GLfloat x, GLfloat y, GLfloat z);
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
#ifdef __cplusplus
}
#endif
#endif
