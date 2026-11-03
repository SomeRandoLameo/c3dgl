// OpenGL: lighting parameters
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// OpenGL: lighting parameters (glLight, glLightModel, glMaterial, glColorMaterial)
//----------------------------------------------------------------------------------
static bool isColorParam(GLenum pname)
{
    return (pname == GL_AMBIENT) || (pname == GL_DIFFUSE) || (pname == GL_SPECULAR) || (pname == GL_EMISSION) ||
           (pname == GL_AMBIENT_AND_DIFFUSE) || (pname == GL_LIGHT_MODEL_AMBIENT);
}

// Integer parameters: colors are mapped like glColor*i (most positive integer = 1.0), everything else converted
static void intParams(GLenum pname, const GLint *in, int count, float *out)
{
    bool color = isColorParam(pname);
    for (int i = 0; i < count; i++) out[i] = color? (float)((2.0*in[i] + 1.0)/4294967295.0) : (float)in[i];
}

static Light *lightFor(GLenum light)
{
    if ((light < GL_LIGHT0) || (light >= GL_LIGHT0 + C3DGL_MAX_LIGHTS)) { setError(GL_INVALID_ENUM); return NULL; }
    return &gl.lighting.lights[light - GL_LIGHT0];
}

// Values of a glLight parameter, 0: not a glLight parameter
static int lightParamCount(GLenum pname)
{
    switch (pname)
    {
        case GL_AMBIENT: case GL_DIFFUSE: case GL_SPECULAR: case GL_POSITION: return 4;
        case GL_SPOT_DIRECTION: return 3;
        case GL_SPOT_EXPONENT: case GL_SPOT_CUTOFF:
        case GL_CONSTANT_ATTENUATION: case GL_LINEAR_ATTENUATION: case GL_QUADRATIC_ATTENUATION: return 1;
        default: return 0;
    }
}

// Position and spot direction are stored in eye coordinates, transformed by the current modelview
static void setLight(GLenum light, GLenum pname, const float *p)
{
    LIST_SAVE(LIGHT, "uuF", light, pname, lightParamCount(pname), p);
    Light *li = lightFor(light);
    if (li == NULL) return;
    litStateChanged();

    const float *m = gl.stack[0][gl.stackDepth[0]].m;
    switch (pname)
    {
        case GL_AMBIENT: memcpy(li->ambient, p, sizeof(li->ambient)); break;
        case GL_DIFFUSE: memcpy(li->diffuse, p, sizeof(li->diffuse)); break;
        case GL_SPECULAR: memcpy(li->specular, p, sizeof(li->specular)); break;
        case GL_POSITION:
            for (int r = 0; r < 4; r++) li->position[r] = m[r]*p[0] + m[4 + r]*p[1] + m[8 + r]*p[2] + m[12 + r]*p[3];
            break;
        case GL_SPOT_DIRECTION:
            for (int r = 0; r < 3; r++) li->spotDirection[r] = m[r]*p[0] + m[4 + r]*p[1] + m[8 + r]*p[2];
            break;
        case GL_SPOT_EXPONENT:
            if ((p[0] < 0.0f) || (p[0] > 128.0f)) { setError(GL_INVALID_VALUE); return; }
            li->spotExponent = p[0];
            break;
        case GL_SPOT_CUTOFF:
            if (((p[0] < 0.0f) || (p[0] > 90.0f)) && (p[0] != 180.0f)) { setError(GL_INVALID_VALUE); return; }
            li->spotCutoff = p[0];
            break;
        case GL_CONSTANT_ATTENUATION: case GL_LINEAR_ATTENUATION: case GL_QUADRATIC_ATTENUATION:
            if (p[0] < 0.0f) { setError(GL_INVALID_VALUE); return; }
            li->attenuation[pname - GL_CONSTANT_ATTENUATION] = p[0];
            break;
        default: setError(GL_INVALID_ENUM); return;
    }
    updateLight(li);
}

void glLightfv(GLenum light, GLenum pname, const GLfloat *params) { setLight(light, pname, params); }

void glLightf(GLenum light, GLenum pname, GLfloat param)
{
    if (lightParamCount(pname) != 1) { setError(GL_INVALID_ENUM); return; }
    setLight(light, pname, &param);
}

void glLightiv(GLenum light, GLenum pname, const GLint *params)
{
    int n = lightParamCount(pname);
    if (n == 0) { setError(GL_INVALID_ENUM); return; }
    float f[4];
    intParams(pname, params, n, f);
    setLight(light, pname, f);
}

void glLighti(GLenum light, GLenum pname, GLint param)
{
    if (lightParamCount(pname) != 1) { setError(GL_INVALID_ENUM); return; }
    glLightiv(light, pname, &param);
}

static int lightModelParamCount(GLenum pname)
{
    if (pname == GL_LIGHT_MODEL_AMBIENT) return 4;
    return ((pname == GL_LIGHT_MODEL_LOCAL_VIEWER) || (pname == GL_LIGHT_MODEL_TWO_SIDE))? 1 : 0;
}

static void setLightModel(GLenum pname, const float *p)
{
    LIST_SAVE(LIGHT_MODEL, "uF", pname, lightModelParamCount(pname), p);
    litStateChanged();
    switch (pname)
    {
        case GL_LIGHT_MODEL_AMBIENT: memcpy(gl.lighting.modelAmbient, p, sizeof(gl.lighting.modelAmbient)); break;
        case GL_LIGHT_MODEL_LOCAL_VIEWER: gl.lighting.localViewer = (p[0] != 0.0f); break;
        case GL_LIGHT_MODEL_TWO_SIDE: gl.lighting.twoSide = (p[0] != 0.0f); break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

void glLightModelfv(GLenum pname, const GLfloat *params) { setLightModel(pname, params); }

void glLightModelf(GLenum pname, GLfloat param)
{
    if (lightModelParamCount(pname) != 1) { setError(GL_INVALID_ENUM); return; }
    setLightModel(pname, &param);
}

void glLightModeliv(GLenum pname, const GLint *params)
{
    int n = lightModelParamCount(pname);
    if (n == 0) { setError(GL_INVALID_ENUM); return; }
    float f[4];
    intParams(pname, params, n, f);
    setLightModel(pname, f);
}

void glLightModeli(GLenum pname, GLint param)
{
    if (lightModelParamCount(pname) != 1) { setError(GL_INVALID_ENUM); return; }
    glLightModeliv(pname, &param);
}

static int materialParamCount(GLenum pname)
{
    switch (pname)
    {
        case GL_AMBIENT: case GL_DIFFUSE: case GL_SPECULAR: case GL_EMISSION: case GL_AMBIENT_AND_DIFFUSE: return 4;
        case GL_COLOR_INDEXES: return 3;
        case GL_SHININESS: return 1;
        default: return 0;
    }
}

static bool faceValid(GLenum face) { return (face == GL_FRONT) || (face == GL_BACK) || (face == GL_FRONT_AND_BACK); }

// Also allowed between glBegin and glEnd: the next vertices are lit with the new material
static void setMaterial(GLenum face, GLenum pname, const float *p)
{
    LIST_SAVE(MATERIAL, "uuF", face, pname, materialParamCount(pname), p);
    if (!faceValid(face) || (materialParamCount(pname) == 0)) { setError(GL_INVALID_ENUM); return; }
    if ((pname == GL_SHININESS) && ((p[0] < 0.0f) || (p[0] > 128.0f))) { setError(GL_INVALID_VALUE); return; }
    litStateChanged();

    for (int f = 0; f < 2; f++)
    {
        if ((face != GL_FRONT_AND_BACK) && (face != (f? GL_BACK : GL_FRONT))) continue;
        Material *m = &gl.lighting.material[f];
        switch (pname)
        {
            case GL_AMBIENT: memcpy(m->ambient, p, sizeof(m->ambient)); break;
            case GL_DIFFUSE: memcpy(m->diffuse, p, sizeof(m->diffuse)); break;
            case GL_AMBIENT_AND_DIFFUSE:
                memcpy(m->ambient, p, sizeof(m->ambient));
                memcpy(m->diffuse, p, sizeof(m->diffuse));
                break;
            case GL_SPECULAR: memcpy(m->specular, p, sizeof(m->specular)); break;
            case GL_EMISSION: memcpy(m->emission, p, sizeof(m->emission)); break;
            case GL_SHININESS: m->shininess = p[0]; break;
            default: memcpy(m->colorIndexes, p, sizeof(m->colorIndexes)); break;    // GL_COLOR_INDEXES
        }
    }
}

void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params) { setMaterial(face, pname, params); }

void glMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    if (pname != GL_SHININESS) { setError(GL_INVALID_ENUM); return; }
    setMaterial(face, pname, &param);
}

void glMaterialiv(GLenum face, GLenum pname, const GLint *params)
{
    int n = materialParamCount(pname);
    if (n == 0) { setError(GL_INVALID_ENUM); return; }
    float f[4];
    intParams(pname, params, n, f);
    setMaterial(face, pname, f);
}

void glMateriali(GLenum face, GLenum pname, GLint param)
{
    if (pname != GL_SHININESS) { setError(GL_INVALID_ENUM); return; }
    glMaterialiv(face, pname, &param);
}

void glColorMaterial(GLenum face, GLenum mode)
{
    LIST_SAVE(COLOR_MATERIAL, "uu", face, mode);
    litStateChanged();
    bool modeValid = (mode == GL_EMISSION) || (mode == GL_AMBIENT) || (mode == GL_DIFFUSE) || (mode == GL_SPECULAR) ||
                     (mode == GL_AMBIENT_AND_DIFFUSE);
    if (!faceValid(face) || !modeValid) { setError(GL_INVALID_ENUM); return; }
    gl.lighting.colorMaterialFace = face;
    gl.lighting.colorMaterialMode = mode;
    if (gl.colorMaterial) applyColorMaterial(gl.current.color);
}

// glGetLight: fills v and returns the number of values (0: error). *color: the values are colors
static int getLight(GLenum light, GLenum pname, float v[4], bool *color)
{
    const Light *li = lightFor(light);
    if (li == NULL) return 0;

    *color = isColorParam(pname);
    switch (pname)
    {
        case GL_AMBIENT: memcpy(v, li->ambient, sizeof(li->ambient)); return 4;
        case GL_DIFFUSE: memcpy(v, li->diffuse, sizeof(li->diffuse)); return 4;
        case GL_SPECULAR: memcpy(v, li->specular, sizeof(li->specular)); return 4;
        case GL_POSITION: memcpy(v, li->position, sizeof(li->position)); return 4;
        case GL_SPOT_DIRECTION: memcpy(v, li->spotDirection, sizeof(li->spotDirection)); return 3;
        case GL_SPOT_EXPONENT: v[0] = li->spotExponent; return 1;
        case GL_SPOT_CUTOFF: v[0] = li->spotCutoff; return 1;
        case GL_CONSTANT_ATTENUATION: case GL_LINEAR_ATTENUATION: case GL_QUADRATIC_ATTENUATION:
            v[0] = li->attenuation[pname - GL_CONSTANT_ATTENUATION];
            return 1;
        default: setError(GL_INVALID_ENUM); return 0;
    }
}

// glGetMaterial: face is GL_FRONT or GL_BACK
static int getMaterial(GLenum face, GLenum pname, float v[4], bool *color)
{
    if ((face != GL_FRONT) && (face != GL_BACK)) { setError(GL_INVALID_ENUM); return 0; }

    const Material *m = &gl.lighting.material[(face == GL_BACK)? 1 : 0];
    *color = isColorParam(pname);
    switch (pname)
    {
        case GL_AMBIENT: memcpy(v, m->ambient, sizeof(m->ambient)); return 4;
        case GL_DIFFUSE: memcpy(v, m->diffuse, sizeof(m->diffuse)); return 4;
        case GL_SPECULAR: memcpy(v, m->specular, sizeof(m->specular)); return 4;
        case GL_EMISSION: memcpy(v, m->emission, sizeof(m->emission)); return 4;
        case GL_SHININESS: v[0] = m->shininess; return 1;
        case GL_COLOR_INDEXES: memcpy(v, m->colorIndexes, sizeof(m->colorIndexes)); return 3;
        default: setError(GL_INVALID_ENUM); return 0;
    }
}

void glGetLightfv(GLenum light, GLenum pname, GLfloat *params)
{
    float v[4];
    bool color;
    int n = getLight(light, pname, v, &color);
    for (int i = 0; i < n; i++) params[i] = v[i];
}

void glGetLightiv(GLenum light, GLenum pname, GLint *params)
{
    float v[4];
    bool color;
    int n = getLight(light, pname, v, &color);
    for (int i = 0; i < n; i++) params[i] = color? normalizedToInt(v[i]) : (GLint)lroundf(v[i]);
}

void glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params)
{
    float v[4];
    bool color;
    int n = getMaterial(face, pname, v, &color);
    for (int i = 0; i < n; i++) params[i] = v[i];
}

void glGetMaterialiv(GLenum face, GLenum pname, GLint *params)
{
    float v[4];
    bool color;
    int n = getMaterial(face, pname, v, &color);
    for (int i = 0; i < n; i++) params[i] = color? normalizedToInt(v[i]) : (GLint)lroundf(v[i]);
}
