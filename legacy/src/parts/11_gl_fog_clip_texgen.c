// OpenGL: fog, user clip planes, glTexGen parameters
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// OpenGL: fog (rendered in applyState(), see updateFogLut())
//----------------------------------------------------------------------------------
// p holds 4 values for GL_FOG_COLOR, 1 otherwise; GL_FOG_MODE is the enum value
static void setFog(GLenum pname, const float *p)
{
    LIST_SAVE(FOG, "uF", pname, (pname == GL_FOG_COLOR)? 4 : 1, p);
    switch (pname)
    {
        case GL_FOG_MODE:
        {
            GLenum mode = (GLenum)p[0];
            if ((mode != GL_LINEAR) && (mode != GL_EXP) && (mode != GL_EXP2)) { setError(GL_INVALID_ENUM); return; }
            gl.fogMode = mode;
            break;
        }
        case GL_FOG_DENSITY:
            if (p[0] < 0.0f) { setError(GL_INVALID_VALUE); return; }
            gl.fogDensity = p[0];
            break;
        case GL_FOG_START: gl.fogStart = p[0]; break;
        case GL_FOG_END: gl.fogEnd = p[0]; break;
        case GL_FOG_INDEX: gl.fogIndex = p[0]; break;
        case GL_FOG_COLOR:      // Clamped, like all GLclampf colors
            for (int i = 0; i < 4; i++) gl.fogColor[i] = (p[i] < 0.0f)? 0.0f : (p[i] > 1.0f)? 1.0f : p[i];
            break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

void glFogfv(GLenum pname, const GLfloat *params) { setFog(pname, params); }

void glFogf(GLenum pname, GLfloat param)
{
    if (pname == GL_FOG_COLOR) { setError(GL_INVALID_ENUM); return; }
    setFog(pname, &param);
}

void glFogiv(GLenum pname, const GLint *params)
{
    float f[4];
    if (pname == GL_FOG_COLOR) for (int i = 0; i < 4; i++) f[i] = (float)((2.0*params[i] + 1.0)/4294967295.0);
    else f[0] = (float)params[0];
    setFog(pname, f);
}

void glFogi(GLenum pname, GLint param)
{
    if (pname == GL_FOG_COLOR) { setError(GL_INVALID_ENUM); return; }
    glFogiv(pname, &param);
}

//----------------------------------------------------------------------------------
// OpenGL: user clip planes (clipped on the CPU, see clipPolygon())
//----------------------------------------------------------------------------------
static float *clipPlane(GLenum plane)
{
    if ((plane < GL_CLIP_PLANE0) || (plane >= GL_CLIP_PLANE0 + C3DGL_MAX_CLIP_PLANES)) { setError(GL_INVALID_ENUM); return NULL; }
    return gl.clipPlanes[plane - GL_CLIP_PLANE0];
}

// The plane is stored in eye coordinates: p_eye = p * M^-1 with the current modelview
static void setClipPlane(GLenum plane, const double equation[4])
{
    LIST_SAVE(CLIP_PLANE, "uD", plane, 4, equation);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    float *p = clipPlane(plane);
    if (p == NULL) return;

    Mat4 inv;
    mat4Invert(&gl.stack[0][gl.stackDepth[0]], &inv);
    for (int c = 0; c < 4; c++)
        p[c] = (float)(equation[0]*inv.m[c*4] + equation[1]*inv.m[c*4 + 1] + equation[2]*inv.m[c*4 + 2] + equation[3]*inv.m[c*4 + 3]);
    gl.clipObjectSerial = 0;
}

static const float *getClipPlane(GLenum plane)
{
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return NULL; }
    return clipPlane(plane);
}

void glClipPlane(GLenum plane, const GLdouble *equation) { setClipPlane(plane, equation); }

void glGetClipPlane(GLenum plane, GLdouble *equation)
{
    const float *p = getClipPlane(plane);
    if (p != NULL) for (int i = 0; i < 4; i++) equation[i] = p[i];
}

//----------------------------------------------------------------------------------
// OpenGL: texture coordinate generation (glTexGen) of the active texture unit, see generateTexCoords()
//----------------------------------------------------------------------------------
static int texGenParamCount(GLenum pname) { return ((pname == GL_OBJECT_PLANE) || (pname == GL_EYE_PLANE))? 4 : 1; }

// p: 1 value for GL_TEXTURE_GEN_MODE, 4 for the planes. scalar: from glTexGen{i,f,d}, which only take the mode
static void setTexGen(GLenum coord, GLenum pname, const float *p, bool scalar)
{
    LIST_SAVE(TEX_GEN, "uuiF", coord, pname, (int)scalar, texGenParamCount(pname), p);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if ((coord < GL_S) || (coord > GL_Q)) { setError(GL_INVALID_ENUM); return; }
    int c = coord - GL_S;
    TexGenState *g = &gl.texGen[gl.activeTexture];
    gl.texGenSerial++;

    switch (pname)
    {
        case GL_TEXTURE_GEN_MODE:
        {
            GLenum mode = (GLenum)p[0];
            if ((mode != GL_OBJECT_LINEAR) && (mode != GL_EYE_LINEAR) && ((mode != GL_SPHERE_MAP) || (c >= 2)))
            {
                setError(GL_INVALID_ENUM);
                return;
            }
            g->mode[c] = mode;
            return;
        }
        case GL_OBJECT_PLANE:
            if (scalar) break;
            memcpy(g->objectPlane[c], p, sizeof(g->objectPlane[c]));
            return;
        case GL_EYE_PLANE:
        {
            if (scalar) break;
            // Stored in eye coordinates: p_eye = p * M^-1 with the current modelview, like clip planes
            Mat4 inv;
            mat4Invert(&gl.stack[0][gl.stackDepth[0]], &inv);
            for (int i = 0; i < 4; i++)
                g->eyePlane[c][i] = p[0]*inv.m[i*4] + p[1]*inv.m[i*4 + 1] + p[2]*inv.m[i*4 + 2] + p[3]*inv.m[i*4 + 3];
            return;
        }
        default: break;
    }
    setError(GL_INVALID_ENUM);
}

void glTexGenf(GLenum coord, GLenum pname, GLfloat param)
{
    const float p[4] = { param, 0.0f, 0.0f, 0.0f };     // Padded: a list records 4 values for the plane names
    setTexGen(coord, pname, p, true);
}

void glTexGeni(GLenum coord, GLenum pname, GLint param) { glTexGenf(coord, pname, (GLfloat)param); }
void glTexGend(GLenum coord, GLenum pname, GLdouble param) { glTexGenf(coord, pname, (GLfloat)param); }
void glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params) { setTexGen(coord, pname, params, false); }

void glTexGeniv(GLenum coord, GLenum pname, const GLint *params)
{
    float p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < texGenParamCount(pname); i++) p[i] = (float)params[i];
    setTexGen(coord, pname, p, false);
}

void glTexGendv(GLenum coord, GLenum pname, const GLdouble *params)
{
    float p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < texGenParamCount(pname); i++) p[i] = (float)params[i];
    setTexGen(coord, pname, p, false);
}

// Returns the count, 0 on error
static int getTexGen(GLenum coord, GLenum pname, float v[4])
{
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return 0; }
    if ((coord < GL_S) || (coord > GL_Q)) { setError(GL_INVALID_ENUM); return 0; }
    int c = coord - GL_S;
    const TexGenState *g = &gl.texGen[gl.activeTexture];
    switch (pname)
    {
        case GL_TEXTURE_GEN_MODE: v[0] = g->mode[c]; return 1;
        case GL_OBJECT_PLANE: memcpy(v, g->objectPlane[c], 4*sizeof(float)); return 4;
        case GL_EYE_PLANE: memcpy(v, g->eyePlane[c], 4*sizeof(float)); return 4;
        default: setError(GL_INVALID_ENUM); return 0;
    }
}

void glGetTexGenfv(GLenum coord, GLenum pname, GLfloat *params)
{
    float v[4];
    int n = getTexGen(coord, pname, v);
    for (int i = 0; i < n; i++) params[i] = v[i];
}

void glGetTexGeniv(GLenum coord, GLenum pname, GLint *params)
{
    float v[4];
    int n = getTexGen(coord, pname, v);
    for (int i = 0; i < n; i++) params[i] = (GLint)lroundf(v[i]);
}

void glGetTexGendv(GLenum coord, GLenum pname, GLdouble *params)
{
    float v[4];
    int n = getTexGen(coord, pname, v);
    for (int i = 0; i < n; i++) params[i] = v[i];
}
