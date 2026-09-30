// c3dgl: OpenGL 1.1 subset for the Nintendo 3DS, implemented on citro3d (src/c3dgl.c).
//
// Put c3dgl/include on the include path and code written against <GL/gl.h> picks up this file.
// Standard OpenGL enum values; only the declared functions exist (see c3dgl.h for the list of
// what is supported and what is not). Deliberately free of libctru/citro3d includes, so it can be
// used next to headers that clash with <3ds.h> (e.g. raylib.h: KEY_A, ...).
#ifndef C3DGL_GL_H
#define C3DGL_GL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int    GLenum;
typedef unsigned int    GLbitfield;
typedef unsigned int    GLuint;
typedef int             GLint;
typedef int             GLsizei;
typedef unsigned char   GLboolean;
typedef signed char     GLbyte;
typedef short           GLshort;
typedef unsigned char   GLubyte;
typedef unsigned short  GLushort;
typedef float           GLfloat;
typedef float           GLclampf;
typedef double          GLdouble;
typedef double          GLclampd;
typedef void            GLvoid;

// Boolean
#define GL_FALSE                            0
#define GL_TRUE                             1

// Primitives
#define GL_POINTS                           0x0000
#define GL_LINES                            0x0001
#define GL_LINE_LOOP                        0x0002
#define GL_LINE_STRIP                       0x0003
#define GL_TRIANGLES                        0x0004
#define GL_TRIANGLE_STRIP                   0x0005
#define GL_TRIANGLE_FAN                     0x0006
#define GL_QUADS                            0x0007

// Clear buffer bits
#define GL_DEPTH_BUFFER_BIT                 0x00000100
#define GL_STENCIL_BUFFER_BIT               0x00000400
#define GL_COLOR_BUFFER_BIT                 0x00004000

// Depth/compare functions
#define GL_NEVER                            0x0200
#define GL_LESS                             0x0201
#define GL_EQUAL                            0x0202
#define GL_LEQUAL                           0x0203
#define GL_GREATER                          0x0204
#define GL_NOTEQUAL                         0x0205
#define GL_GEQUAL                           0x0206
#define GL_ALWAYS                           0x0207

// Blend factors
#define GL_ZERO                             0
#define GL_ONE                              1
#define GL_SRC_COLOR                        0x0300
#define GL_ONE_MINUS_SRC_COLOR              0x0301
#define GL_SRC_ALPHA                        0x0302
#define GL_ONE_MINUS_SRC_ALPHA              0x0303
#define GL_DST_ALPHA                        0x0304
#define GL_ONE_MINUS_DST_ALPHA              0x0305
#define GL_DST_COLOR                        0x0306
#define GL_ONE_MINUS_DST_COLOR              0x0307
#define GL_SRC_ALPHA_SATURATE               0x0308

// Faces / winding / polygon mode
#define GL_FRONT                            0x0404
#define GL_BACK                             0x0405
#define GL_FRONT_AND_BACK                   0x0408
#define GL_CW                               0x0900
#define GL_CCW                              0x0901
#define GL_POINT                            0x1B00
#define GL_LINE                             0x1B01
#define GL_FILL                             0x1B02

// Capabilities (glEnable/glDisable)
#define GL_LINE_SMOOTH                      0x0B20
#define GL_CULL_FACE                        0x0B44
#define GL_DEPTH_TEST                       0x0B71
#define GL_BLEND                            0x0BE2
#define GL_SCISSOR_TEST                     0x0C11
#define GL_TEXTURE_2D                       0x0DE1

// Client arrays
#define GL_VERTEX_ARRAY                     0x8074
#define GL_NORMAL_ARRAY                     0x8075
#define GL_COLOR_ARRAY                      0x8076
#define GL_TEXTURE_COORD_ARRAY              0x8078

// Queries
#define GL_LINE_WIDTH                       0x0B21
#define GL_POINT_SIZE                       0x0B11
#define GL_MODELVIEW_MATRIX                 0x0BA6
#define GL_PROJECTION_MATRIX                0x0BA7
#define GL_VENDOR                           0x1F00
#define GL_RENDERER                         0x1F01
#define GL_VERSION                          0x1F02
#define GL_EXTENSIONS                       0x1F03

// Hints / shading
#define GL_PERSPECTIVE_CORRECTION_HINT      0x0C50
#define GL_DONT_CARE                        0x1100
#define GL_FASTEST                          0x1101
#define GL_NICEST                           0x1102
#define GL_FLAT                             0x1D00
#define GL_SMOOTH                           0x1D01

// Matrix modes
#define GL_MODELVIEW                        0x1700
#define GL_PROJECTION                       0x1701
#define GL_TEXTURE                          0x1702

// Pixel store
#define GL_UNPACK_ALIGNMENT                 0x0CF5
#define GL_PACK_ALIGNMENT                   0x0D05

// Data types
#define GL_BYTE                             0x1400
#define GL_UNSIGNED_BYTE                    0x1401
#define GL_SHORT                            0x1402
#define GL_UNSIGNED_SHORT                   0x1403
#define GL_INT                              0x1404
#define GL_UNSIGNED_INT                     0x1405
#define GL_FLOAT                            0x1406
#define GL_UNSIGNED_SHORT_4_4_4_4           0x8033
#define GL_UNSIGNED_SHORT_5_5_5_1           0x8034
#define GL_UNSIGNED_SHORT_5_6_5             0x8363

// Pixel formats
#define GL_ALPHA                            0x1906
#define GL_RGB                              0x1907
#define GL_RGBA                             0x1908
#define GL_LUMINANCE                        0x1909
#define GL_LUMINANCE_ALPHA                  0x190A

// Texture parameters
#define GL_TEXTURE_MAG_FILTER               0x2800
#define GL_TEXTURE_MIN_FILTER               0x2801
#define GL_TEXTURE_WRAP_S                   0x2802
#define GL_TEXTURE_WRAP_T                   0x2803
#define GL_NEAREST                          0x2600
#define GL_LINEAR                           0x2601
#define GL_NEAREST_MIPMAP_NEAREST           0x2700
#define GL_LINEAR_MIPMAP_NEAREST            0x2701
#define GL_NEAREST_MIPMAP_LINEAR            0x2702
#define GL_LINEAR_MIPMAP_LINEAR             0x2703
#define GL_REPEAT                           0x2901
#define GL_CLAMP_TO_EDGE                    0x812F
#define GL_MIRRORED_REPEAT                  0x8370

// State
void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glEnableClientState(GLenum array);
void glDisableClientState(GLenum array);
void glHint(GLenum target, GLenum mode);
void glShadeModel(GLenum mode);
void glPixelStorei(GLenum pname, GLint param);
void glGetFloatv(GLenum pname, GLfloat *params);
const GLubyte *glGetString(GLenum name);

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void glClearDepth(GLclampd depth);
void glClear(GLbitfield mask);
void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void glDepthMask(GLboolean flag);
void glDepthFunc(GLenum func);
void glBlendFunc(GLenum sfactor, GLenum dfactor);
void glCullFace(GLenum mode);
void glFrontFace(GLenum mode);
void glPolygonMode(GLenum face, GLenum mode);
void glLineWidth(GLfloat width);
void glPointSize(GLfloat size);

// Matrices
void glMatrixMode(GLenum mode);
void glPushMatrix(void);
void glPopMatrix(void);
void glLoadIdentity(void);
void glMultMatrixf(const GLfloat *m);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glScalef(GLfloat x, GLfloat y, GLfloat z);
void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);

// Immediate mode
void glBegin(GLenum mode);
void glEnd(void);
void glVertex2i(GLint x, GLint y);
void glVertex2f(GLfloat x, GLfloat y);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glTexCoord2f(GLfloat s, GLfloat t);
void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);
void glColor3f(GLfloat red, GLfloat green, GLfloat blue);
void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);

// Client-side vertex arrays
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer);
void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);

// Textures
void glGenTextures(GLsizei n, GLuint *textures);
void glDeleteTextures(GLsizei n, const GLuint *textures);
void glBindTexture(GLenum target, GLuint texture);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, const GLvoid *pixels);
void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels);
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);

#ifdef __cplusplus
}
#endif

#endif // C3DGL_GL_H
