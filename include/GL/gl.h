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

// Errors
#define GL_NO_ERROR                         0
#define GL_INVALID_ENUM                     0x0500
#define GL_INVALID_VALUE                    0x0501
#define GL_INVALID_OPERATION                0x0502
#define GL_STACK_OVERFLOW                   0x0503
#define GL_STACK_UNDERFLOW                  0x0504
#define GL_OUT_OF_MEMORY                    0x0505

// Primitives
#define GL_POINTS                           0x0000
#define GL_LINES                            0x0001
#define GL_LINE_LOOP                        0x0002
#define GL_LINE_STRIP                       0x0003
#define GL_TRIANGLES                        0x0004
#define GL_TRIANGLE_STRIP                   0x0005
#define GL_TRIANGLE_FAN                     0x0006
#define GL_QUADS                            0x0007
#define GL_QUAD_STRIP                       0x0008
#define GL_POLYGON                          0x0009

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

// Stencil operations
#define GL_KEEP                             0x1E00
#define GL_INCR                             0x1E02
#define GL_DECR                             0x1E03
#define GL_INVERT                           0x150A
#define GL_INCR_WRAP                        0x8507
#define GL_DECR_WRAP                        0x8508

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
#define GL_ALPHA_TEST                       0x0BC0
#define GL_STENCIL_TEST                     0x0B90
#define GL_BLEND                            0x0BE2
#define GL_SCISSOR_TEST                     0x0C11
#define GL_TEXTURE_2D                       0x0DE1

// Capabilities that are accepted and reported by glIsEnabled, but have no effect (enabling the
// non-cosmetic ones logs a warning)
#define GL_POINT_SMOOTH                     0x0B10
#define GL_POLYGON_SMOOTH                   0x0B41
#define GL_LIGHTING                         0x0B50
#define GL_COLOR_MATERIAL                   0x0B57
#define GL_FOG                              0x0B60
#define GL_NORMALIZE                        0x0BA1
#define GL_DITHER                           0x0BD0
#define GL_POLYGON_OFFSET_FILL              0x8037
#define GL_LIGHT0                           0x4000
#define GL_LIGHT1                           0x4001
#define GL_LIGHT2                           0x4002
#define GL_LIGHT3                           0x4003
#define GL_LIGHT4                           0x4004
#define GL_LIGHT5                           0x4005
#define GL_LIGHT6                           0x4006
#define GL_LIGHT7                           0x4007

// Client arrays
#define GL_VERTEX_ARRAY                     0x8074
#define GL_NORMAL_ARRAY                     0x8075
#define GL_COLOR_ARRAY                      0x8076
#define GL_TEXTURE_COORD_ARRAY              0x8078

// Queries (glGet*)
#define GL_CURRENT_COLOR                    0x0B00
#define GL_CURRENT_NORMAL                   0x0B02
#define GL_CURRENT_TEXTURE_COORDS           0x0B03
#define GL_POINT_SIZE                       0x0B11
#define GL_LINE_WIDTH                       0x0B21
#define GL_CULL_FACE_MODE                   0x0B45
#define GL_FRONT_FACE                       0x0B46
#define GL_SHADE_MODEL                      0x0B54
#define GL_DEPTH_RANGE                      0x0B70
#define GL_DEPTH_WRITEMASK                  0x0B72
#define GL_DEPTH_CLEAR_VALUE                0x0B73
#define GL_DEPTH_FUNC                       0x0B74
#define GL_STENCIL_CLEAR_VALUE              0x0B91
#define GL_STENCIL_FUNC                     0x0B92
#define GL_STENCIL_VALUE_MASK               0x0B93
#define GL_STENCIL_FAIL                     0x0B94
#define GL_STENCIL_PASS_DEPTH_FAIL          0x0B95
#define GL_STENCIL_PASS_DEPTH_PASS          0x0B96
#define GL_STENCIL_REF                      0x0B97
#define GL_STENCIL_WRITEMASK                0x0B98
#define GL_MATRIX_MODE                      0x0BA0
#define GL_VIEWPORT                         0x0BA2
#define GL_MODELVIEW_STACK_DEPTH            0x0BA3
#define GL_PROJECTION_STACK_DEPTH           0x0BA4
#define GL_TEXTURE_STACK_DEPTH              0x0BA5
#define GL_MODELVIEW_MATRIX                 0x0BA6
#define GL_PROJECTION_MATRIX                0x0BA7
#define GL_TEXTURE_MATRIX                   0x0BA8
#define GL_ALPHA_TEST_FUNC                  0x0BC1
#define GL_ALPHA_TEST_REF                   0x0BC2
#define GL_BLEND_DST                        0x0BE0
#define GL_BLEND_SRC                        0x0BE1
#define GL_SCISSOR_BOX                      0x0C10
#define GL_COLOR_CLEAR_VALUE                0x0C22
#define GL_COLOR_WRITEMASK                  0x0C23
#define GL_MAX_TEXTURE_SIZE                 0x0D33
#define GL_MAX_MODELVIEW_STACK_DEPTH        0x0D36
#define GL_MAX_PROJECTION_STACK_DEPTH       0x0D38
#define GL_MAX_TEXTURE_STACK_DEPTH          0x0D39
#define GL_MAX_VIEWPORT_DIMS                0x0D3A
#define GL_RED_BITS                         0x0D52
#define GL_GREEN_BITS                       0x0D53
#define GL_BLUE_BITS                        0x0D54
#define GL_ALPHA_BITS                       0x0D55
#define GL_DEPTH_BITS                       0x0D56
#define GL_STENCIL_BITS                     0x0D57
#define GL_TEXTURE_BINDING_2D               0x8069
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

// Texture environment
#define GL_TEXTURE_ENV                      0x2300
#define GL_TEXTURE_ENV_MODE                 0x2200
#define GL_TEXTURE_ENV_COLOR                0x2201
#define GL_MODULATE                         0x2100
#define GL_DECAL                            0x2101
#define GL_REPLACE                          0x1E01
#define GL_ADD                              0x0104

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
GLboolean glIsEnabled(GLenum cap);
void glEnableClientState(GLenum array);
void glDisableClientState(GLenum array);
void glHint(GLenum target, GLenum mode);
void glShadeModel(GLenum mode);         // GL_FLAT is accepted but colors are always interpolated
void glPixelStorei(GLenum pname, GLint param);
void glGetBooleanv(GLenum pname, GLboolean *params);
void glGetIntegerv(GLenum pname, GLint *params);
void glGetFloatv(GLenum pname, GLfloat *params);
void glGetDoublev(GLenum pname, GLdouble *params);
const GLubyte *glGetString(GLenum name);
GLenum glGetError(void);
void glFlush(void);                     // Drawing is submitted by c3dglSwapBuffers(); these only flush the batch
void glFinish(void);

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void glClearDepth(GLclampd depth);
void glClearStencil(GLint s);
void glClear(GLbitfield mask);
void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void glDepthMask(GLboolean flag);
void glDepthFunc(GLenum func);
void glAlphaFunc(GLenum func, GLclampf ref);
void glStencilFunc(GLenum func, GLint ref, GLuint mask);
void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass);
void glStencilMask(GLuint mask);
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
void glLoadMatrixf(const GLfloat *m);
void glLoadMatrixd(const GLdouble *m);
void glMultMatrixf(const GLfloat *m);
void glMultMatrixd(const GLdouble *m);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glTranslated(GLdouble x, GLdouble y, GLdouble z);
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void glScalef(GLfloat x, GLfloat y, GLfloat z);
void glScaled(GLdouble x, GLdouble y, GLdouble z);
void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);

// Immediate mode
void glBegin(GLenum mode);
void glEnd(void);
void glVertex2f(GLfloat x, GLfloat y);
void glVertex2d(GLdouble x, GLdouble y);
void glVertex2i(GLint x, GLint y);
void glVertex2s(GLshort x, GLshort y);
void glVertex2fv(const GLfloat *v);
void glVertex2dv(const GLdouble *v);
void glVertex2iv(const GLint *v);
void glVertex2sv(const GLshort *v);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glVertex3d(GLdouble x, GLdouble y, GLdouble z);
void glVertex3i(GLint x, GLint y, GLint z);
void glVertex3s(GLshort x, GLshort y, GLshort z);
void glVertex3fv(const GLfloat *v);
void glVertex3dv(const GLdouble *v);
void glVertex3iv(const GLint *v);
void glVertex3sv(const GLshort *v);
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);    // Divided by w, which must be > 0
void glVertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void glVertex4fv(const GLfloat *v);

void glTexCoord1f(GLfloat s);
void glTexCoord2f(GLfloat s, GLfloat t);
void glTexCoord2d(GLdouble s, GLdouble t);
void glTexCoord2i(GLint s, GLint t);
void glTexCoord2s(GLshort s, GLshort t);
void glTexCoord2fv(const GLfloat *v);
void glTexCoord2dv(const GLdouble *v);

void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);    // Stored only, there is no lighting
void glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz);
void glNormal3fv(const GLfloat *v);
void glNormal3dv(const GLdouble *v);

void glColor3f(GLfloat red, GLfloat green, GLfloat blue);
void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void glColor3d(GLdouble red, GLdouble green, GLdouble blue);
void glColor4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha);
void glColor3ub(GLubyte red, GLubyte green, GLubyte blue);
void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
void glColor3fv(const GLfloat *v);
void glColor4fv(const GLfloat *v);
void glColor3dv(const GLdouble *v);
void glColor4dv(const GLdouble *v);
void glColor3ubv(const GLubyte *v);
void glColor4ubv(const GLubyte *v);

void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
void glRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2);
void glRecti(GLint x1, GLint y1, GLint x2, GLint y2);
void glRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2);
void glRectfv(const GLfloat *v1, const GLfloat *v2);
void glRectiv(const GLint *v1, const GLint *v2);

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
GLboolean glIsTexture(GLuint texture);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glTexParameterf(GLenum target, GLenum pname, GLfloat param);
void glTexParameteriv(GLenum target, GLenum pname, const GLint *params);
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);
void glTexEnvi(GLenum target, GLenum pname, GLint param);
void glTexEnvf(GLenum target, GLenum pname, GLfloat param);
void glTexEnviv(GLenum target, GLenum pname, const GLint *params);
void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params);
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
