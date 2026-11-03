// c3dgl: OpenGL ES 1.1 header. c3dgl implements desktop OpenGL 1.1 and OpenGL ES 1.1 in one API, so this is
// <GL/gl.h> plus the ES platform macros; code written against <GLES/gl.h> compiles unchanged.
#ifndef C3DGL_GLES_GL_H
#define C3DGL_GLES_GL_H

#ifndef GL_API
#define GL_API extern
#endif
#ifndef GL_APIENTRY
#define GL_APIENTRY
#endif

#include "../GL/gl.h"

#define GL_VERSION_ES_CM_1_0    1
#define GL_VERSION_ES_CL_1_0    1
#define GL_VERSION_ES_CM_1_1    1
#define GL_VERSION_ES_CL_1_1    1

#endif // C3DGL_GLES_GL_H
