// OpenGL: evaluators
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// OpenGL: evaluators (GL). Maps are Bezier polynomials in u (and v), evaluated on the CPU with de Casteljau.
// Evaluated values feed the generated vertex only; like in GL they do not change the current color, normal or
// texcoords. Evaluated texcoords go to texture unit 0.
//----------------------------------------------------------------------------------
static const int mapComponents[9] = { 4, 1, 3, 1, 2, 3, 4, 3, 4 };   // COLOR_4, INDEX, NORMAL, TEXCOORD_1..4, VERTEX_3, VERTEX_4
enum { MAP_COLOR, MAP_INDEX, MAP_NORMAL, MAP_TEX1, MAP_TEX2, MAP_TEX3, MAP_TEX4, MAP_VERTEX3, MAP_VERTEX4 };

static struct EvalMap *mapForTarget(GLenum target, bool *twoD)
{
    if ((target >= GL_MAP1_COLOR_4) && (target <= GL_MAP1_VERTEX_4)) { *twoD = false; return &gl.map1[target - GL_MAP1_COLOR_4]; }
    if ((target >= GL_MAP2_COLOR_4) && (target <= GL_MAP2_VERTEX_4)) { *twoD = true; return &gl.map2[target - GL_MAP2_COLOR_4]; }
    return NULL;
}

// Store control points: uorder x vorder points of k components, read with the given strides.
// A display list gets the points packed (strides vorder*k and k); an invalid call is recorded without them and fails
// again, before reading them, when the list is executed
static void defineMap(GLenum target, double u1, double u2, int ustride, int uorder, double v1, double v2, int vstride,
                      int vorder, const void *points, bool isDouble, bool twoD)
{
    bool targetTwoD;
    struct EvalMap *m = mapForTarget(target, &targetTwoD);
    GLenum error = GL_NO_ERROR;
    int k = 0;
    if ((m == NULL) || (targetTwoD != twoD)) error = GL_INVALID_ENUM;
    else
    {
        k = mapComponents[m - (twoD? gl.map2 : gl.map1)];
        if ((uorder < 1) || (uorder > C3DGL_MAX_EVAL_ORDER) || (vorder < 1) || (vorder > C3DGL_MAX_EVAL_ORDER) ||
            (u1 == u2) || (twoD && (v1 == v2)) || (ustride < k) || (twoD && (vstride < k))) error = GL_INVALID_VALUE;
    }
    if (gl.listCompiling && (error != GL_NO_ERROR))
    {
        listSave(LIST_MAP, "uiddiiddiiF", target, twoD, u1, u2, ustride, uorder, v1, v2, vstride, vorder,
                 0, (const float *)NULL);
        return;
    }
    if (error != GL_NO_ERROR) { setError(error); return; }
    if (gl.inBegin && !gl.listCompiling) { setError(GL_INVALID_OPERATION); return; }

    float *data = malloc((size_t)uorder*vorder*k*sizeof(float));
    if (data == NULL) { setError(GL_OUT_OF_MEMORY); return; }
    for (int i = 0; i < uorder; i++)
        for (int j = 0; j < vorder; j++)
            for (int c = 0; c < k; c++)
            {
                size_t src = (size_t)i*ustride + (size_t)j*vstride + c;
                data[(i*vorder + j)*k + c] = isDouble? (float)((const double *)points)[src] : ((const float *)points)[src];
            }

    if (gl.listCompiling)
    {
        listSave(LIST_MAP, "uiddiiddiiF", target, twoD, u1, u2, vorder*k, uorder, v1, v2, k, vorder, uorder*vorder*k, data);
        free(data);
        return;
    }
    free(m->points);
    m->points = data;
    m->uorder = uorder;
    m->vorder = vorder;
    m->u1 = (float)u1; m->u2 = (float)u2;
    m->v1 = (float)v1; m->v2 = (float)v2;
}

void glMap1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points)
{
    defineMap(target, u1, u2, stride, order, 0.0, 1.0, 0, 1, points, false, false);
}
void glMap1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points)
{
    defineMap(target, u1, u2, stride, order, 0.0, 1.0, 0, 1, points, true, false);
}
void glMap2f(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder,
             GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points)
{
    defineMap(target, u1, u2, ustride, uorder, v1, v2, vstride, vorder, points, false, true);
}
void glMap2d(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder,
             GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points)
{
    defineMap(target, u1, u2, ustride, uorder, v1, v2, vstride, vorder, points, true, true);
}

// Bernstein basis of degree n - 1 at t into b[0..n-1], built up degree by degree (stable, like de Casteljau);
// db: its derivative d/dt, from the basis of degree n - 2
static void bernstein(int n, float t, float *b, float *db)
{
    b[0] = 1.0f;
    for (int d = 1; d < n; d++)
    {
        if ((d == n - 1) && (db != NULL))
            for (int i = 0; i < n; i++) db[i] = (float)(n - 1)*(((i > 0)? b[i - 1] : 0.0f) - ((i < n - 1)? b[i] : 0.0f));
        b[d] = t*b[d - 1];
        for (int i = d - 1; i > 0; i--) b[i] = (1.0f - t)*b[i] + t*b[i - 1];
        b[0] *= 1.0f - t;
    }
    if ((n == 1) && (db != NULL)) db[0] = 0.0f;
}

// Evaluate a 1D map at u
static void evalMap1(const struct EvalMap *m, int k, float u, float *out)
{
    float b[C3DGL_MAX_EVAL_ORDER];
    bernstein(m->uorder, (u - m->u1)/(m->u2 - m->u1), b, NULL);
    for (int c = 0; c < k; c++) out[c] = 0.0f;
    for (int i = 0; i < m->uorder; i++)
        for (int c = 0; c < k; c++) out[c] += b[i]*m->points[i*k + c];
}

// Evaluate a 2D map at (u, v): the value and, if wanted, the partial derivatives d/du and d/dv
static void evalMap2(const struct EvalMap *m, int k, float u, float v, float *out, float *du, float *dv)
{
    float bu[C3DGL_MAX_EVAL_ORDER], bv[C3DGL_MAX_EVAL_ORDER], dbu[C3DGL_MAX_EVAL_ORDER], dbv[C3DGL_MAX_EVAL_ORDER];
    bool derivs = (du != NULL);
    bernstein(m->uorder, (u - m->u1)/(m->u2 - m->u1), bu, derivs? dbu : NULL);
    bernstein(m->vorder, (v - m->v1)/(m->v2 - m->v1), bv, derivs? dbv : NULL);

    // Sum over v first: q_i = sum_j bv_j*p_ij (and r_i with the v derivative), then over u
    for (int c = 0; c < k; c++) out[c] = 0.0f;
    if (derivs) for (int c = 0; c < k; c++) du[c] = dv[c] = 0.0f;
    float su = derivs? 1.0f/(m->u2 - m->u1) : 0.0f, sv = derivs? 1.0f/(m->v2 - m->v1) : 0.0f;
    const float *p = m->points;
    for (int i = 0; i < m->uorder; i++)
    {
        float q[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, r[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (int j = 0; j < m->vorder; j++, p += k)
        {
            for (int c = 0; c < k; c++) q[c] += bv[j]*p[c];
            if (derivs) for (int c = 0; c < k; c++) r[c] += dbv[j]*p[c];
        }
        for (int c = 0; c < k; c++) out[c] += bu[i]*q[c];
        if (!derivs) continue;
        float wu = dbu[i]*su, wv = bu[i]*sv;
        for (int c = 0; c < k; c++) { du[c] += wu*q[c]; dv[c] += wv*r[c]; }
    }
}

// Highest enabled texcoord map (GL: the one with the most components wins)
static int texcoordMap(const struct EvalMap *maps)
{
    for (int i = MAP_TEX4; i >= MAP_TEX1; i--) if (maps[i].enabled && maps[i].points) return i;
    return -1;
}

// An evaluated vertex before lighting, with its normal (evaluated or the current one)
typedef struct {
    Vertex v;
    float normal[3];
    bool valid;                 // False: w = 0, no vertex
} EvalVertex;

// Build the vertex for evaluated values. normal: evaluated normal, NULL: the current normal
static void buildEvaluated(EvalVertex *e, const float *pos, int posSize, const float *color, const float *tex,
                           int texSize, const float *normal)
{
    Vertex *v = &e->v;
    *v = gl.current;            // Values without a map come from the current state
    if (color != NULL) for (int c = 0; c < 4; c++) v->color[c] = colorByte(color[c]);
    if (tex != NULL)
    {
        v->tex[0] = tex[0];
        v->tex[1] = (texSize > 1)? tex[1] : 0.0f;
        v->tex[2] = (texSize > 3)? tex[3] : 1.0f;
        v->texR = (texSize > 2)? tex[2] : 0.0f;
        if (v->tex[2] != 1.0f) markTexQ();
    }
    memcpy(e->normal, (normal != NULL)? normal : gl.currentNormal, sizeof(e->normal));

    float w = (posSize == 4)? pos[3] : 1.0f;
    e->valid = (w != 0.0f);
    if (!e->valid) { WARN_ONCE("Evaluator: w = 0 (point at infinity) not supported\n"); return; }
    for (int c = 0; c < 3; c++) v->pos[c] = pos[c]/w;
}

static void submitEvaluated(const EvalVertex *e)
{
    if (!e->valid) return;
    Vertex v = e->v;            // Lighting replaces the color
    submitLitVertex(&v, e->normal, gl.currentEdge);
}

void glEvalCoord1f(GLfloat u)
{
    LIST_SAVE(EVAL_COORD1, "f", u);
    const struct EvalMap *maps = gl.map1;
    int vertexMap = (maps[MAP_VERTEX4].enabled && maps[MAP_VERTEX4].points)? MAP_VERTEX4 :
                    (maps[MAP_VERTEX3].enabled && maps[MAP_VERTEX3].points)? MAP_VERTEX3 : -1;
    if (!gl.inBegin || (vertexMap < 0)) return;     // No vertex map: no vertex (GL)

    float pos[4], color[4], tex[4], normal[3];
    #define EVAL1(index, out) evalMap1(&maps[index], mapComponents[index], u, out)
    EVAL1(vertexMap, pos);
    bool hasColor = maps[MAP_COLOR].enabled && maps[MAP_COLOR].points;
    if (hasColor) EVAL1(MAP_COLOR, color);
    int texMap = texcoordMap(maps);
    if (texMap >= 0) EVAL1(texMap, tex);
    bool hasNormal = maps[MAP_NORMAL].enabled && maps[MAP_NORMAL].points;
    if (hasNormal) EVAL1(MAP_NORMAL, normal);
    #undef EVAL1

    EvalVertex e;
    buildEvaluated(&e, pos, mapComponents[vertexMap], hasColor? color : NULL, (texMap >= 0)? tex : NULL,
                   mapComponents[texMap >= 0? texMap : 0], hasNormal? normal : NULL);
    submitEvaluated(&e);
}

// Evaluate the 2D maps at (u, v); false without a vertex map (no vertex, like GL)
static bool evalCoord2(float u, float v, EvalVertex *e)
{
    const struct EvalMap *maps = gl.map2;
    int vertexMap = (maps[MAP_VERTEX4].enabled && maps[MAP_VERTEX4].points)? MAP_VERTEX4 :
                    (maps[MAP_VERTEX3].enabled && maps[MAP_VERTEX3].points)? MAP_VERTEX3 : -1;
    if (vertexMap < 0) return false;

    int k = mapComponents[vertexMap];
    float pos[4], du[4], dv[4], color[4], tex[4], normal[3];
    bool autoNormal = gl.autoNormal;
    evalMap2(&maps[vertexMap], k, u, v, pos, autoNormal? du : NULL, autoNormal? dv : NULL);

    bool hasNormal = false;
    if (autoNormal)
    {
        // Normal = dP/du x dP/dv of the 3D position (for VERTEX_4: of (x, y, z)/w, quotient rule)
        if (k == 4)
        {
            float w = pos[3], w2 = w*w;
            for (int c = 0; c < 3; c++)
            {
                du[c] = (du[c]*w - pos[c]*du[3])/w2;
                dv[c] = (dv[c]*w - pos[c]*dv[3])/w2;
            }
        }
        normal[0] = du[1]*dv[2] - du[2]*dv[1];
        normal[1] = du[2]*dv[0] - du[0]*dv[2];
        normal[2] = du[0]*dv[1] - du[1]*dv[0];
        hasNormal = true;
    }
    else if (maps[MAP_NORMAL].enabled && maps[MAP_NORMAL].points)
    {
        evalMap2(&maps[MAP_NORMAL], 3, u, v, normal, NULL, NULL);
        hasNormal = true;
    }

    bool hasColor = maps[MAP_COLOR].enabled && maps[MAP_COLOR].points;
    if (hasColor) evalMap2(&maps[MAP_COLOR], 4, u, v, color, NULL, NULL);
    int texMap = texcoordMap(maps);
    if (texMap >= 0) evalMap2(&maps[texMap], mapComponents[texMap], u, v, tex, NULL, NULL);

    buildEvaluated(e, pos, k, hasColor? color : NULL, (texMap >= 0)? tex : NULL, mapComponents[texMap >= 0? texMap : 0],
                   hasNormal? normal : NULL);
    return true;
}

void glEvalCoord2f(GLfloat u, GLfloat v)
{
    LIST_SAVE(EVAL_COORD2, "ff", u, v);
    EvalVertex e;
    if (gl.inBegin && evalCoord2(u, v, &e)) submitEvaluated(&e);
}

void glEvalCoord1d(GLdouble u) { glEvalCoord1f((float)u); }
void glEvalCoord1fv(const GLfloat *u) { glEvalCoord1f(u[0]); }
void glEvalCoord1dv(const GLdouble *u) { glEvalCoord1f((float)u[0]); }
void glEvalCoord2d(GLdouble u, GLdouble v) { glEvalCoord2f((float)u, (float)v); }
void glEvalCoord2fv(const GLfloat *u) { glEvalCoord2f(u[0], u[1]); }
void glEvalCoord2dv(const GLdouble *u) { glEvalCoord2f((float)u[0], (float)u[1]); }

void glMapGrid1f(GLint un, GLfloat u1, GLfloat u2)
{
    LIST_SAVE(MAP_GRID1, "iff", un, u1, u2);
    if (un <= 0) { setError(GL_INVALID_VALUE); return; }
    gl.grid1n = un;
    gl.grid1u1 = u1;
    gl.grid1u2 = u2;
}

void glMapGrid1d(GLint un, GLdouble u1, GLdouble u2) { glMapGrid1f(un, (float)u1, (float)u2); }

void glMapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2)
{
    LIST_SAVE(MAP_GRID2, "iffiff", un, u1, u2, vn, v1, v2);
    if ((un <= 0) || (vn <= 0)) { setError(GL_INVALID_VALUE); return; }
    gl.grid2un = un; gl.grid2u1 = u1; gl.grid2u2 = u2;
    gl.grid2vn = vn; gl.grid2v1 = v1; gl.grid2v2 = v2;
}

void glMapGrid2d(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2)
{
    glMapGrid2f(un, (float)u1, (float)u2, vn, (float)v1, (float)v2);
}

// Grid coordinate i of n segments over [a, b]; exactly b at i = n (as required by the spec)
static float gridCoord(int i, int n, float a, float b) { return (i == n)? b : a + (b - a)*i/n; }

// Evaluated at execution: a display list uses the grid of the time it is executed
void glEvalPoint1(GLint i)
{
    LIST_SAVE(EVAL_POINT1, "i", i);
    glEvalCoord1f(gridCoord(i, gl.grid1n, gl.grid1u1, gl.grid1u2));
}

void glEvalPoint2(GLint i, GLint j)
{
    LIST_SAVE(EVAL_POINT2, "ii", i, j);
    glEvalCoord2f(gridCoord(i, gl.grid2un, gl.grid2u1, gl.grid2u2), gridCoord(j, gl.grid2vn, gl.grid2v1, gl.grid2v2));
}

// Scratch grid of glEvalMesh2; false (the mesh is evaluated point by point instead) if it cannot grow
static bool reserveEvalGrid(int count)
{
    if (count <= gl.evalGridCapacity) return true;
    void *grid = realloc(gl.evalGrid, (size_t)count*sizeof(EvalVertex));
    if (grid == NULL) return false;
    gl.evalGrid = grid;
    gl.evalGridCapacity = count;
    return true;
}

void glEvalMesh1(GLenum mode, GLint i1, GLint i2)
{
    LIST_SAVE(EVAL_MESH1, "uii", mode, i1, i2);
    if ((mode != GL_POINT) && (mode != GL_LINE)) { setError(GL_INVALID_ENUM); return; }
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }

    glBegin((mode == GL_POINT)? GL_POINTS : GL_LINE_STRIP);
    for (int i = i1; i <= i2; i++) glEvalPoint1(i);
    glEnd();
}

void glEvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2)
{
    LIST_SAVE(EVAL_MESH2, "uiiii", mode, i1, i2, j1, j2);
    if ((mode != GL_POINT) && (mode != GL_LINE) && (mode != GL_FILL)) { setError(GL_INVALID_ENUM); return; }
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }

    // FILL and LINE use every grid point more than once: the grid is evaluated once up front (the same vertices
    // glEvalPoint2 gives, the maps cannot change during the mesh)
    int nu = i2 - i1 + 1, nv = j2 - j1 + 1;
    if ((mode != GL_POINT) && (nu > 0) && (nv > 0) && reserveEvalGrid(nu*nv))
    {
        EvalVertex *g = gl.evalGrid;
        for (int j = 0; j < nv; j++)
        {
            float v = gridCoord(j1 + j, gl.grid2vn, gl.grid2v1, gl.grid2v2);
            for (int i = 0; i < nu; i++)
                if (!evalCoord2(gridCoord(i1 + i, gl.grid2un, gl.grid2u1, gl.grid2u2), v, &g[j*nu + i])) return;
        }
        #define G(i, j) (&g[((j) - j1)*nu + (i) - i1])
        if (mode == GL_FILL)
        {
            for (int j = j1; j < j2; j++)
            {
                glBegin(GL_QUAD_STRIP);
                for (int i = i1; i <= i2; i++) { submitEvaluated(G(i, j)); submitEvaluated(G(i, j + 1)); }
                glEnd();
            }
        }
        else
        {
            for (int j = j1; j <= j2; j++)
            {
                glBegin(GL_LINE_STRIP);
                for (int i = i1; i <= i2; i++) submitEvaluated(G(i, j));
                glEnd();
            }
            for (int i = i1; i <= i2; i++)
            {
                glBegin(GL_LINE_STRIP);
                for (int j = j1; j <= j2; j++) submitEvaluated(G(i, j));
                glEnd();
            }
        }
        #undef G
        return;
    }

    switch (mode)
    {
        case GL_FILL:
            for (int j = j1; j < j2; j++)
            {
                glBegin(GL_QUAD_STRIP);
                for (int i = i1; i <= i2; i++) { glEvalPoint2(i, j); glEvalPoint2(i, j + 1); }
                glEnd();
            }
            break;
        case GL_LINE:
            for (int j = j1; j <= j2; j++)
            {
                glBegin(GL_LINE_STRIP);
                for (int i = i1; i <= i2; i++) glEvalPoint2(i, j);
                glEnd();
            }
            for (int i = i1; i <= i2; i++)
            {
                glBegin(GL_LINE_STRIP);
                for (int j = j1; j <= j2; j++) glEvalPoint2(i, j);
                glEnd();
            }
            break;
        default:
            glBegin(GL_POINTS);
            for (int j = j1; j <= j2; j++)
                for (int i = i1; i <= i2; i++) glEvalPoint2(i, j);
            glEnd();
            break;
    }
}

// glGetMap: GL_COEFF (control points), GL_ORDER, GL_DOMAIN; returns the count
static int getMap(GLenum target, GLenum query, double *out)
{
    bool twoD;
    const struct EvalMap *m = mapForTarget(target, &twoD);
    if (m == NULL) { setError(GL_INVALID_ENUM); return 0; }
    int k = mapComponents[m - (twoD? gl.map2 : gl.map1)];
    switch (query)
    {
        case GL_COEFF:
        {
            if (m->points == NULL) return 0;
            int n = m->uorder*m->vorder*k;
            for (int i = 0; i < n; i++) out[i] = m->points[i];
            return n;
        }
        case GL_ORDER:
            out[0] = m->points? m->uorder : 1;
            out[1] = m->points? m->vorder : 1;
            return twoD? 2 : 1;
        case GL_DOMAIN:
            out[0] = m->points? m->u1 : 0.0; out[1] = m->points? m->u2 : 1.0;
            out[2] = m->points? m->v1 : 0.0; out[3] = m->points? m->v2 : 1.0;
            return twoD? 4 : 2;
        default: setError(GL_INVALID_ENUM); return 0;
    }
}

static double mapValues[C3DGL_MAX_EVAL_ORDER*C3DGL_MAX_EVAL_ORDER*4];

void glGetMapdv(GLenum target, GLenum query, GLdouble *v)
{
    int n = getMap(target, query, mapValues);
    for (int i = 0; i < n; i++) v[i] = mapValues[i];
}

void glGetMapfv(GLenum target, GLenum query, GLfloat *v)
{
    int n = getMap(target, query, mapValues);
    for (int i = 0; i < n; i++) v[i] = (GLfloat)mapValues[i];
}

void glGetMapiv(GLenum target, GLenum query, GLint *v)
{
    int n = getMap(target, query, mapValues);
    for (int i = 0; i < n; i++) v[i] = (GLint)lround(mapValues[i]);
}
