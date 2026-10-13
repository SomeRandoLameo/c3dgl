#ifndef C3DGL_NATIVE_GL_H
#define C3DGL_NATIVE_GL_H
#include <stddef.h>
/* Implemented native subset only; additional GL groups follow incrementally. */
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
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
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_DEPTH_TEST 0x0b71
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
void glDepthRange(GLdouble nearValue, GLdouble farValue);
void glDepthRangef(GLfloat nearValue, GLfloat farValue);
void glCullFace(GLenum face);
void glFrontFace(GLenum front);
void glGetBooleanv(GLenum pname, GLboolean *out);
#define GL_TRIANGLES 0x0004
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
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
/* Immediate mode currently supports GL_TRIANGLES, up to 4096 vertices. */
void glBegin(GLenum mode);
void glEnd(void);
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
