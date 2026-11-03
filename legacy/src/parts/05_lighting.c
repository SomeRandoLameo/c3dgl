// CPU lighting and texture coordinate generation, per vertex
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// Lighting (GL 1.1 section 2.13): per vertex on the CPU, when the vertex is submitted
//----------------------------------------------------------------------------------
static void setColor4(float out[4], float r, float g, float b, float a)
{
    out[0] = r; out[1] = g; out[2] = b; out[3] = a;
}

static float dot3(const float a[3], const float b[3]) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

static void normalize3(float v[3])
{
    float len = sqrtf(dot3(v, v));
    if (len > 0.0f) for (int i = 0; i < 3; i++) v[i] /= len;
}

// Recompute the derived values after a glLight change
static void updateLight(Light *li)
{
    memcpy(li->unitPosition, li->position, sizeof(li->unitPosition));
    normalize3(li->unitPosition);
    for (int k = 0; k < 3; k++) li->halfVector[k] = li->unitPosition[k] + ((k == 2)? 1.0f : 0.0f);
    normalize3(li->halfVector);
    memcpy(li->spotUnit, li->spotDirection, sizeof(li->spotUnit));
    normalize3(li->spotUnit);
    li->spotCos = cosf(li->spotCutoff*(float)M_PI/180.0f);
}

// GL defaults (GL 1.1 table 6.9)
static void initLighting(void)
{
    LightingState *l = &gl.lighting;
    memset(l, 0, sizeof(*l));
    for (int i = 0; i < C3DGL_MAX_LIGHTS; i++)
    {
        Light *li = &l->lights[i];
        float c = (i == 0)? 1.0f : 0.0f;    // Only light 0 is white
        setColor4(li->ambient, 0.0f, 0.0f, 0.0f, 1.0f);
        setColor4(li->diffuse, c, c, c, 1.0f);
        setColor4(li->specular, c, c, c, 1.0f);
        li->position[2] = 1.0f;             // Directional, along +z
        li->spotDirection[2] = -1.0f;
        li->spotCutoff = 180.0f;
        li->attenuation[0] = 1.0f;
        updateLight(li);
    }
    for (int f = 0; f < 2; f++)
    {
        Material *m = &l->material[f];
        setColor4(m->ambient, 0.2f, 0.2f, 0.2f, 1.0f);
        setColor4(m->diffuse, 0.8f, 0.8f, 0.8f, 1.0f);
        setColor4(m->specular, 0.0f, 0.0f, 0.0f, 1.0f);
        setColor4(m->emission, 0.0f, 0.0f, 0.0f, 1.0f);
        m->colorIndexes[1] = m->colorIndexes[2] = 1.0f;
    }
    setColor4(l->modelAmbient, 0.2f, 0.2f, 0.2f, 1.0f);
    l->colorMaterialFace = GL_FRONT_AND_BACK;
    l->colorMaterialMode = GL_AMBIENT_AND_DIFFUSE;
}

// GL_COLOR_MATERIAL: the tracked material properties take the color (and keep it, like in GL)
static void applyColorMaterial(const u8 color[4])
{
    float c[4];
    for (int i = 0; i < 4; i++) c[i] = color[i]*(1.0f/255.0f);

    GLenum face = gl.lighting.colorMaterialFace, mode = gl.lighting.colorMaterialMode;
    for (int f = 0; f < 2; f++)
    {
        if ((face != GL_FRONT_AND_BACK) && (face != (f? GL_BACK : GL_FRONT))) continue;
        Material *m = &gl.lighting.material[f];
        if ((mode == GL_AMBIENT) || (mode == GL_AMBIENT_AND_DIFFUSE)) memcpy(m->ambient, c, sizeof(c));
        if ((mode == GL_DIFFUSE) || (mode == GL_AMBIENT_AND_DIFFUSE)) memcpy(m->diffuse, c, sizeof(c));
        if (mode == GL_SPECULAR) memcpy(m->specular, c, sizeof(c));
        if (mode == GL_EMISSION) memcpy(m->emission, c, sizeof(c));
    }
}

// Normals go to eye space with the inverse transpose of the modelview's upper 3x3 (n' = n M^-1), which is the
// cofactor matrix divided by the determinant. Also the GL_RESCALE_NORMAL factor: 1/length of M^-1's third row
static void updateNormalMatrix(void)
{
    if (gl.normalSerial == gl.matrixSerial) return;
    gl.normalSerial = gl.matrixSerial;

    const float *m = gl.stack[0][gl.stackDepth[0]].m;
    #define M(r, c) m[(c)*4 + (r)]
    float cof[9] = {
        M(1,1)*M(2,2) - M(1,2)*M(2,1), M(1,2)*M(2,0) - M(1,0)*M(2,2), M(1,0)*M(2,1) - M(1,1)*M(2,0),
        M(0,2)*M(2,1) - M(0,1)*M(2,2), M(0,0)*M(2,2) - M(0,2)*M(2,0), M(0,1)*M(2,0) - M(0,0)*M(2,1),
        M(0,1)*M(1,2) - M(0,2)*M(1,1), M(0,2)*M(1,0) - M(0,0)*M(1,2), M(0,0)*M(1,1) - M(0,1)*M(1,0),
    };
    float det = M(0,0)*cof[0] + M(0,1)*cof[1] + M(0,2)*cof[2];
    #undef M

    float inv = (det != 0.0f)? 1.0f/det : 0.0f;     // Singular: normals collapse to 0 (ambient and emission only)
    for (int i = 0; i < 9; i++) gl.normalMatrix[i] = cof[i]*inv;
    const float *n = gl.normalMatrix;
    float len = sqrtf(n[2]*n[2] + n[5]*n[5] + n[8]*n[8]);
    gl.normalRescale = (len > 0.0f)? 1.0f/len : 1.0f;
}

// Object space normal -> eye space. Not normalized unless asked for, like GL (a scaling modelview changes the brightness)
static void eyeNormal(const float normal[3], float n[3])
{
    updateNormalMatrix();
    const float *nm = gl.normalMatrix;
    for (int k = 0; k < 3; k++) n[k] = nm[k*3]*normal[0] + nm[k*3 + 1]*normal[1] + nm[k*3 + 2]*normal[2];
    if (gl.normalize) normalize3(n);
    else if (gl.rescaleNormal) for (int k = 0; k < 3; k++) n[k] *= gl.normalRescale;
}

// Lit color of one side: eye = vertex in eye space, n = eye space normal of that side
static void shadeFace(const Material *m, const float eye[3], const float n[3], u8 out[4])
{
    const LightingState *l = &gl.lighting;
    float c[3];
    for (int k = 0; k < 3; k++) c[k] = m->emission[k] + m->ambient[k]*l->modelAmbient[k];

    for (int i = 0; i < C3DGL_MAX_LIGHTS; i++)
    {
        if (!(gl.lightEnabled & (1u << i))) continue;
        const Light *li = &l->lights[i];

        // vp: unit vector from the vertex to the light; distance attenuation for positional lights only
        float vp[3], att = 1.0f;
        if (li->position[3] != 0.0f)
        {
            for (int k = 0; k < 3; k++) vp[k] = li->position[k]/li->position[3] - eye[k];
            float d2 = dot3(vp, vp), d = sqrtf(d2);
            if (d > 0.0f) for (int k = 0; k < 3; k++) vp[k] /= d;
            float denom = li->attenuation[0] + li->attenuation[1]*d + li->attenuation[2]*d2;
            if (denom > 0.0f) att = 1.0f/denom;
        }
        else memcpy(vp, li->unitPosition, sizeof(vp));

        if (li->spotCutoff != 180.0f)
        {
            // Outside the cone the light contributes nothing, not even ambient
            float cosAngle = -dot3(vp, li->spotUnit);
            if (cosAngle < li->spotCos) continue;
            att *= powf(cosAngle, li->spotExponent);
        }

        float r[3];
        for (int k = 0; k < 3; k++) r[k] = m->ambient[k]*li->ambient[k];

        float ndotl = dot3(n, vp);
        if (ndotl > 0.0f)
        {
            for (int k = 0; k < 3; k++) r[k] += ndotl*m->diffuse[k]*li->diffuse[k];

            float spec[3];
            for (int k = 0; k < 3; k++) spec[k] = m->specular[k]*li->specular[k];
            if ((spec[0] != 0.0f) || (spec[1] != 0.0f) || (spec[2] != 0.0f))
            {
                // Half vector between vp and the direction to the eye ((0, 0, 1) without local viewer)
                float h[3], toEye[3] = { 0.0f, 0.0f, 1.0f };
                if (!l->localViewer && (li->position[3] == 0.0f)) memcpy(h, li->halfVector, sizeof(h));
                else
                {
                    if (l->localViewer)
                    {
                        for (int k = 0; k < 3; k++) toEye[k] = -eye[k];
                        normalize3(toEye);
                    }
                    for (int k = 0; k < 3; k++) h[k] = vp[k] + toEye[k];
                    normalize3(h);
                }
                float ndoth = dot3(n, h);
                if (ndoth > 0.0f)
                {
                    float f = powf(ndoth, m->shininess);
                    for (int k = 0; k < 3; k++) r[k] += f*spec[k];
                }
            }
        }

        for (int k = 0; k < 3; k++) c[k] += att*r[k];
    }

    for (int k = 0; k < 3; k++) out[k] = colorByte(c[k]);
    out[3] = colorByte(m->diffuse[3]);
}

// Replace v's color by the lit color, also the back color with two-sided lighting. normal: in object space
static void lightVertex(Vertex *v, const float normal[3])
{
    if (gl.colorMaterial) applyColorMaterial(v->color);

    // The eye space position is only needed for positional lights and the local viewer
    bool needEye = gl.lighting.localViewer;
    for (int i = 0; i < C3DGL_MAX_LIGHTS; i++)
        if ((gl.lightEnabled & (1u << i)) && (gl.lighting.lights[i].position[3] != 0.0f)) needEye = true;
    float eye[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    if (needEye)
    {
        mat4Transform(&gl.stack[0][gl.stackDepth[0]], v->pos, eye);
        if ((eye[3] != 1.0f) && (eye[3] != 0.0f)) for (int k = 0; k < 3; k++) eye[k] /= eye[3];
    }

    float n[3];
    eyeNormal(normal, n);

    shadeFace(&gl.lighting.material[0], eye, n, v->color);
    if (gl.lighting.twoSide)
    {
        float back[3] = { -n[0], -n[1], -n[2] };
        shadeFace(&gl.lighting.material[1], eye, back, v->backColor);
    }
}

// Lit colors of recent vertices. Meshes submit shared vertices several times (glDrawElements, triangle lists, strips
// next to each other), the lit result only depends on position, normal and color while the lighting state is
// unchanged. Every call that changes lighting state starts a new generation (litStateChanged()), a modelview change
// too; GL_COLOR_MATERIAL needs nothing: the tracked material values follow the color, which is part of the key
#define LIT_CACHE_SIZE  512         // Power of two; direct mapped
typedef struct {
    u32 key[7];                 // Position, normal, color
    u32 gen;
    u8 color[4], backColor[4];
} LitCacheEntry;
static LitCacheEntry litCache[LIT_CACHE_SIZE];

static void lightVertexCached(Vertex *v, const float normal[3])
{
    u32 key[7];
    memcpy(key, v->pos, 3*sizeof(float));
    memcpy(key + 3, normal, 3*sizeof(float));
    memcpy(key + 6, v->color, 4);
    if (gl.litCacheSerial != gl.matrixSerial) { gl.litCacheGen++; gl.litCacheSerial = gl.matrixSerial; }

    u32 h = 2166136261u;
    for (int i = 0; i < 7; i++) h = (h ^ key[i])*16777619u;
    LitCacheEntry *e = &litCache[(h ^ (h >> 16)) & (LIT_CACHE_SIZE - 1)];
    if ((e->gen == gl.litCacheGen) && (memcmp(e->key, key, sizeof(key)) == 0))
    {
        if (gl.colorMaterial) applyColorMaterial(v->color);     // The material still follows the color
        memcpy(v->color, e->color, 4);
        memcpy(v->backColor, e->backColor, 4);
        return;
    }

    lightVertex(v, normal);
    memcpy(e->key, key, sizeof(key));
    e->gen = gl.litCacheGen;
    memcpy(e->color, v->color, 4);
    memcpy(e->backColor, v->backColor, 4);
}

//----------------------------------------------------------------------------------
// Texture coordinate generation (GL 1.1 section 2.10.4): per vertex on the CPU, when the vertex is submitted
//----------------------------------------------------------------------------------
// Generated coordinates of the units with texgen replace the vertex's ones; the other coordinates of such a unit stay
// (r is 0 then: vertices keep no r). The unit's texture matrix is applied here too, with all of s, t, r, q, so that a
// generated r takes part (projective texturing: eye linear s, t, r, q times the light's matrices); prepareDraw() gives
// these units an identity matrix in the shader.
// Everything linear is folded into one TexGenTransform per unit: eye planes times the modelview are object planes,
// and the texture matrix rows s, t, q times the per-coordinate inputs give the final rows
static void updateTexGenTransforms(void)
{
    u32 serials[3] = { gl.matrixSerial, gl.texMatrixSerial, gl.texGenSerial };
    if (memcmp(serials, gl.texGenTransformSerials, sizeof(serials)) == 0) return;
    memcpy(gl.texGenTransformSerials, serials, sizeof(serials));

    const float *mv = gl.stack[0][gl.stackDepth[0]].m;
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        const TexGenState *g = &gl.texGen[unit];
        TexGenTransform *x = &gl.texGenTransform[unit];
        memset(x, 0, sizeof(*x));
        if (!g->enabled) continue;

        // Input coordinate c (s, t, r, q) = gen[c] . (x, y, z, 1) + pass[c] . (s, t, q) + sphere[c] . (s, t)
        float gen[4][4] = {{ 0 }}, pass[4][3] = {{ 0 }}, sphere[4][2] = {{ 0 }};
        for (int c = 0; c < 4; c++)
        {
            if (!((g->enabled >> c) & 1))
            {
                if (c != 2) { pass[c][(c == 3)? 2 : c] = 1.0f; x->usesPass = true; }     // r stays 0
                continue;
            }
            switch (g->mode[c])
            {
                case GL_OBJECT_LINEAR: memcpy(gen[c], g->objectPlane[c], sizeof(gen[c])); break;
                case GL_EYE_LINEAR:
                    // plane . (MV p) = (plane^T MV) . p
                    for (int j = 0; j < 4; j++)
                    {
                        const float *p = g->eyePlane[c];
                        gen[c][j] = p[0]*mv[j*4] + p[1]*mv[j*4 + 1] + p[2]*mv[j*4 + 2] + p[3]*mv[j*4 + 3];
                    }
                    break;
                default: sphere[c][c] = 1.0f; x->usesSphere = true; break;     // GL_SPHERE_MAP: s, t only
            }
        }

        const float *tm = gl.stack[2 + unit][gl.stackDepth[2 + unit]].m;
        static const int rows[3] = { 0, 1, 3 };     // s, t, q
        for (int i = 0; i < 3; i++)
        {
            const float r[4] = { tm[rows[i]], tm[4 + rows[i]], tm[8 + rows[i]], tm[12 + rows[i]] };
            for (int c = 0; c < 4; c++)
            {
                for (int j = 0; j < 4; j++) x->gen[i][j] += r[c]*gen[c][j];
                for (int j = 0; j < 3; j++) x->pass[i][j] += r[c]*pass[c][j];
                for (int j = 0; j < 2; j++) x->sphere[i][j] += r[c]*sphere[c][j];
            }
        }
    }
}

// GL_SPHERE_MAP s, t: r = u - 2 n (n.u) with u the unit vector from the eye to the vertex and n the eye space normal,
// then s, t = r_x/m + 1/2, r_y/m + 1/2 with m = 2 sqrt(r_x^2 + r_y^2 + (r_z + 1)^2)
static void sphereMap(const Vertex *v, const float normal[3], float out[2])
{
    float u[4], n[3], r[3];
    mat4Transform(&gl.stack[0][gl.stackDepth[0]], v->pos, u);
    if ((u[3] != 1.0f) && (u[3] != 0.0f)) for (int k = 0; k < 3; k++) u[k] /= u[3];
    normalize3(u);
    eyeNormal(normal, n);
    float nu = 2.0f*dot3(n, u);
    for (int k = 0; k < 3; k++) r[k] = u[k] - nu*n[k];
    float m = 2.0f*sqrtf(r[0]*r[0] + r[1]*r[1] + (r[2] + 1.0f)*(r[2] + 1.0f));
    float inv = (m > 0.0f)? 1.0f/m : 0.0f;
    out[0] = r[0]*inv + 0.5f;
    out[1] = r[1]*inv + 0.5f;
}

static void generateTexCoords(Vertex *v, const float normal[3])
{
    updateTexGenTransforms();
    float sphere[2];
    bool haveSphere = false;
    const float *p = v->pos;

    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        if (!gl.texGen[unit].enabled) continue;
        const TexGenTransform *x = &gl.texGenTransform[unit];
        float *tc = (unit == 0)? v->tex : v->texExtra[unit - 1];
        float out[3];
        for (int i = 0; i < 3; i++) out[i] = x->gen[i][0]*p[0] + x->gen[i][1]*p[1] + x->gen[i][2]*p[2] + x->gen[i][3];
        if (x->usesPass)
            for (int i = 0; i < 3; i++) out[i] += x->pass[i][0]*tc[0] + x->pass[i][1]*tc[1] + x->pass[i][2]*tc[2];
        if (x->usesSphere)
        {
            if (!haveSphere) { sphereMap(v, normal, sphere); haveSphere = true; }
            for (int i = 0; i < 3; i++) out[i] += x->sphere[i][0]*sphere[0] + x->sphere[i][1]*sphere[1];
        }
        memcpy(tc, out, sizeof(out));
        if (unit == 0) v->texR = 0.0f;      // Not generated (see Vertex)
    }
}

// Every vertex goes through here: texgen, lighting, then primitive assembly
static void submitLitVertex(Vertex *v, const float normal[3], bool edge)
{
    if (gl.texGen[0].enabled | gl.texGen[1].enabled | gl.texGen[2].enabled) generateTexCoords(v, normal);
    if (gl.lightingEnabled) lightVertexCached(v, normal);
    submitVertex(v, edge);
}

// Capabilities that programs commonly toggle but c3dgl does not implement or that have no effect on this
// framebuffer (color index mode, no sample buffers): stored for glIsEnabled
static const GLenum ignoredCaps[] = {
    GL_DITHER, GL_LINE_SMOOTH, GL_POINT_SMOOTH, GL_POLYGON_SMOOTH, GL_INDEX_LOGIC_OP,
    GL_MULTISAMPLE, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_ALPHA_TO_ONE, GL_SAMPLE_COVERAGE,
};

static int ignoredCapBit(GLenum cap)
{
    for (int i = 0; i < (int)(sizeof(ignoredCaps)/sizeof(ignoredCaps[0])); i++) if (ignoredCaps[i] == cap) return i;
    return -1;
}
