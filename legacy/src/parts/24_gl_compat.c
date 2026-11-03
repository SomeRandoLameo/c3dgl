// Compatibility: GLU link stubs, GL 1.2 stubs, OpenGL ES 1.1 float and fixed-point API
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// Declared only so that code like GLU links (see gl.h)
//----------------------------------------------------------------------------------
// GL 1.2: no 3D textures
void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    (void)target; (void)level; (void)internalformat; (void)width; (void)height; (void)depth; (void)border;
    (void)format; (void)type; (void)pixels;
    setError(GL_INVALID_ENUM);
}

//----------------------------------------------------------------------------------
// OpenGL ES 1.1: float variants of the double functions and the fixed-point (16.16) API.
// Enum-valued parameters are passed unscaled in the x functions, like in ES
//----------------------------------------------------------------------------------
void glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar) { glOrtho(left, right, bottom, top, zNear, zFar); }
void glFrustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar) { glFrustum(left, right, bottom, top, zNear, zFar); }
void glDepthRangef(GLclampf zNear, GLclampf zFar) { glDepthRange(zNear, zFar); }
void glClearDepthf(GLclampf depth) { glClearDepth(depth); }

void glClipPlanef(GLenum plane, const GLfloat *equation)
{
    double e[4] = { equation[0], equation[1], equation[2], equation[3] };
    setClipPlane(plane, e);
}

void glGetClipPlanef(GLenum plane, GLfloat *equation)
{
    const float *p = getClipPlane(plane);
    if (p != NULL) memcpy(equation, p, 4*sizeof(float));
}

static float fixedToFloat(GLfixed x) { return x/65536.0f; }

static GLfixed floatToFixed(double f)
{
    double v = f*65536.0;
    if (v >= 2147483647.0) return 0x7FFFFFFF;
    if (v <= -2147483648.0) return (GLfixed)0x80000000;
    return (GLfixed)lround(v);
}

static bool fixedParamIsEnum(GLenum pname)
{
    return (pname == GL_TEXTURE_ENV_MODE) || (pname == GL_TEXTURE_MIN_FILTER) || (pname == GL_TEXTURE_MAG_FILTER) ||
           (pname == GL_TEXTURE_WRAP_S) || (pname == GL_TEXTURE_WRAP_T) || (pname == GL_GENERATE_MIPMAP) || (pname == GL_COORD_REPLACE_OES) ||
           ((pname >= GL_COMBINE_RGB) && (pname <= GL_COMBINE_ALPHA)) || ((pname >= GL_SRC0_RGB) && (pname <= GL_OPERAND2_ALPHA));
}

void glAlphaFuncx(GLenum func, GLclampx ref) { glAlphaFunc(func, fixedToFloat(ref)); }
void glClearColorx(GLclampx red, GLclampx green, GLclampx blue, GLclampx alpha) { glClearColor(fixedToFloat(red), fixedToFloat(green), fixedToFloat(blue), fixedToFloat(alpha)); }
void glClearDepthx(GLclampx depth) { glClearDepth(fixedToFloat(depth)); }
void glColor4x(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha) { glColor4f(fixedToFloat(red), fixedToFloat(green), fixedToFloat(blue), fixedToFloat(alpha)); }
void glDepthRangex(GLclampx zNear, GLclampx zFar) { glDepthRange(fixedToFloat(zNear), fixedToFloat(zFar)); }
void glLineWidthx(GLfixed width) { glLineWidth(fixedToFloat(width)); }
void glNormal3x(GLfixed nx, GLfixed ny, GLfixed nz) { glNormal3f(fixedToFloat(nx), fixedToFloat(ny), fixedToFloat(nz)); }
void glPointSizex(GLfixed size) { glPointSize(fixedToFloat(size)); }
void glSampleCoveragex(GLclampx value, GLboolean invert) { glSampleCoverage(fixedToFloat(value), invert); }
void glPointParameterx(GLenum pname, GLfixed param) { glPointParameterf(pname, fixedToFloat(param)); }

void glPointParameterxv(GLenum pname, const GLfixed *params)
{
    GLfloat f[3] = { fixedToFloat(params[0]), 0.0f, 0.0f };
    if (pname == GL_POINT_DISTANCE_ATTENUATION) { f[1] = fixedToFloat(params[1]); f[2] = fixedToFloat(params[2]); }
    glPointParameterfv(pname, f);
}
void glPolygonOffsetx(GLfixed factor, GLfixed units) { glPolygonOffset(fixedToFloat(factor), fixedToFloat(units)); }
void glRotatex(GLfixed angle, GLfixed x, GLfixed y, GLfixed z) { glRotatef(fixedToFloat(angle), fixedToFloat(x), fixedToFloat(y), fixedToFloat(z)); }
void glScalex(GLfixed x, GLfixed y, GLfixed z) { glScalef(fixedToFloat(x), fixedToFloat(y), fixedToFloat(z)); }
void glTranslatex(GLfixed x, GLfixed y, GLfixed z) { glTranslatef(fixedToFloat(x), fixedToFloat(y), fixedToFloat(z)); }

void glOrthox(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar)
{
    glOrtho(fixedToFloat(left), fixedToFloat(right), fixedToFloat(bottom), fixedToFloat(top), fixedToFloat(zNear), fixedToFloat(zFar));
}

void glFrustumx(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar)
{
    glFrustum(fixedToFloat(left), fixedToFloat(right), fixedToFloat(bottom), fixedToFloat(top), fixedToFloat(zNear), fixedToFloat(zFar));
}

void glLoadMatrixx(const GLfixed *m)
{
    GLfloat f[16];
    for (int i = 0; i < 16; i++) f[i] = fixedToFloat(m[i]);
    glLoadMatrixf(f);
}

void glMultMatrixx(const GLfixed *m)
{
    GLfloat f[16];
    for (int i = 0; i < 16; i++) f[i] = fixedToFloat(m[i]);
    glMultMatrixf(f);
}

void glTexEnvx(GLenum target, GLenum pname, GLfixed param) { glTexEnvi(target, pname, fixedParamIsEnum(pname)? param : (GLint)fixedToFloat(param)); }

void glTexEnvxv(GLenum target, GLenum pname, const GLfixed *params)
{
    if (pname == GL_TEXTURE_ENV_COLOR)
    {
        GLfloat color[4];
        for (int i = 0; i < 4; i++) color[i] = fixedToFloat(params[i]);
        glTexEnvfv(target, pname, color);
    }
    else glTexEnvx(target, pname, params[0]);
}

void glMultiTexCoord4x(GLenum target, GLfixed s, GLfixed t, GLfixed r, GLfixed q)
{
    glMultiTexCoord4f(target, fixedToFloat(s), fixedToFloat(t), fixedToFloat(r), fixedToFloat(q));
}

void glGetTexEnvxv(GLenum target, GLenum pname, GLfixed *params)
{
    float v[4];
    int n = getTexEnv(target, pname, v);
    bool isEnum = (pname != GL_TEXTURE_ENV_COLOR) && (pname != GL_RGB_SCALE) && (pname != GL_ALPHA_SCALE);
    for (int i = 0; i < n; i++) params[i] = isEnum? (GLfixed)v[i] : floatToFixed(v[i]);
}

void glTexParameterx(GLenum target, GLenum pname, GLfixed param) { glTexParameteri(target, pname, fixedParamIsEnum(pname)? param : (GLint)fixedToFloat(param)); }
void glTexParameterxv(GLenum target, GLenum pname, const GLfixed *params) { glTexParameterx(target, pname, params[0]); }

void glGetTexParameterxv(GLenum target, GLenum pname, GLfixed *params)
{
    float v[4];
    int n = getTexParameter(target, pname, v);
    for (int i = 0; i < n; i++) params[i] = (GLfixed)v[i];     // All values are enums or booleans
}

void glLightx(GLenum light, GLenum pname, GLfixed param) { glLightf(light, pname, fixedToFloat(param)); }
void glLightModelx(GLenum pname, GLfixed param) { glLightModelf(pname, fixedToFloat(param)); }
void glMaterialx(GLenum face, GLenum pname, GLfixed param) { glMaterialf(face, pname, fixedToFloat(param)); }

void glLightxv(GLenum light, GLenum pname, const GLfixed *params)
{
    int n = lightParamCount(pname);
    if (n == 0) { setError(GL_INVALID_ENUM); return; }
    float f[4];
    for (int i = 0; i < n; i++) f[i] = fixedToFloat(params[i]);
    setLight(light, pname, f);
}

void glLightModelxv(GLenum pname, const GLfixed *params)
{
    int n = lightModelParamCount(pname);
    if (n == 0) { setError(GL_INVALID_ENUM); return; }
    float f[4];
    for (int i = 0; i < n; i++) f[i] = fixedToFloat(params[i]);
    setLightModel(pname, f);
}

void glMaterialxv(GLenum face, GLenum pname, const GLfixed *params)
{
    int n = materialParamCount(pname);
    if (n == 0) { setError(GL_INVALID_ENUM); return; }
    float f[4];
    for (int i = 0; i < n; i++) f[i] = fixedToFloat(params[i]);
    setMaterial(face, pname, f);
}

void glGetLightxv(GLenum light, GLenum pname, GLfixed *params)
{
    float v[4];
    bool color;
    int n = getLight(light, pname, v, &color);
    for (int i = 0; i < n; i++) params[i] = floatToFixed(v[i]);
}

void glGetMaterialxv(GLenum face, GLenum pname, GLfixed *params)
{
    float v[4];
    bool color;
    int n = getMaterial(face, pname, v, &color);
    for (int i = 0; i < n; i++) params[i] = floatToFixed(v[i]);
}

void glFogx(GLenum pname, GLfixed param)
{
    if (pname == GL_FOG_COLOR) { setError(GL_INVALID_ENUM); return; }
    float f = (pname == GL_FOG_MODE)? (float)param : fixedToFloat(param);    // The mode is an enum, passed unscaled
    setFog(pname, &f);
}

void glFogxv(GLenum pname, const GLfixed *params)
{
    float f[4];
    if (pname == GL_FOG_COLOR) for (int i = 0; i < 4; i++) f[i] = fixedToFloat(params[i]);
    else f[0] = (pname == GL_FOG_MODE)? (float)params[0] : fixedToFloat(params[0]);
    setFog(pname, f);
}

void glClipPlanex(GLenum plane, const GLfixed *equation)
{
    double e[4];
    for (int i = 0; i < 4; i++) e[i] = equation[i]/65536.0;
    setClipPlane(plane, e);
}

void glGetClipPlanex(GLenum plane, GLfixed *equation)
{
    const float *p = getClipPlane(plane);
    if (p != NULL) for (int i = 0; i < 4; i++) equation[i] = floatToFixed(p[i]);
}

void glGetFixedv(GLenum pname, GLfixed *params)
{
    double v[16];
    bool normalized;
    int n = getState(pname, v, &normalized);
    for (int i = 0; i < n; i++) params[i] = floatToFixed(v[i]);
}
