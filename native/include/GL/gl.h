#ifndef C3DGL_NATIVE_GL_H
#define C3DGL_NATIVE_GL_H
/* Implemented native subset only; additional GL groups follow incrementally. */
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned int GLenum;
typedef float GLfloat;
typedef double GLdouble;
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
