#ifndef C3DGL_NATIVE_GL_H
#define C3DGL_NATIVE_GL_H
/* Implemented native subset only; additional GL groups follow incrementally. */
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLdouble;
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
