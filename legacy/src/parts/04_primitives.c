// Primitive assembly: triangles, lines, points, user clip planes, feedback/selection output
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// Primitive assembly
//----------------------------------------------------------------------------------
static void emitTriangle(const Vertex *a, const Vertex *b, const Vertex *c)
{
    if (!reserveVertices(3)) return;

    // Vertex buffer 0 always; texcoords of units 1/2 only when the batch uses them (stale data is never sampled)
    u8 *v = gl.vbo + (size_t)gl.vertexCount*GPU_VERTEX_SIZE;
    memcpy(v, a, GPU_VERTEX_SIZE);
    memcpy(v + GPU_VERTEX_SIZE, b, GPU_VERTEX_SIZE);
    memcpy(v + 2*GPU_VERTEX_SIZE, c, GPU_VERTEX_SIZE);
    if (gl.batch.units[1].texture || gl.batch.units[2].texture)
    {
        memcpy(gl.vboExtra[gl.vertexCount], a->texExtra, GPU_EXTRA_SIZE);
        memcpy(gl.vboExtra[gl.vertexCount + 1], b->texExtra, GPU_EXTRA_SIZE);
        memcpy(gl.vboExtra[gl.vertexCount + 2], c->texExtra, GPU_EXTRA_SIZE);
    }
    gl.vertexCount += 3;
}

static void lerpVertex(Vertex *out, const Vertex *a, const Vertex *b, float t)
{
    for (int i = 0; i < 3; i++) out->pos[i] = a->pos[i] + (b->pos[i] - a->pos[i])*t;
    out->depthBias = a->depthBias + (b->depthBias - a->depthBias)*t;
    for (int i = 0; i < 3; i++) out->tex[i] = a->tex[i] + (b->tex[i] - a->tex[i])*t;
    for (int u = 0; u < C3DGL_TEXTURE_UNITS - 1; u++)
        for (int i = 0; i < 3; i++) out->texExtra[u][i] = a->texExtra[u][i] + (b->texExtra[u][i] - a->texExtra[u][i])*t;
    for (int i = 0; i < 4; i++) out->color[i] = (u8)(a->color[i] + ((float)b->color[i] - a->color[i])*t);
    for (int i = 0; i < 4; i++) out->backColor[i] = (u8)(a->backColor[i] + ((float)b->backColor[i] - a->backColor[i])*t);
    out->pointSize = a->pointSize + (b->pointSize - a->pointSize)*t;
    out->texR = a->texR + (b->texR - a->texR)*t;
}

//----------------------------------------------------------------------------------
// User clip planes (glClipPlane): clipped on the CPU in object space, before lines and points are expanded and
// before polygons are filled, outlined or drawn as vertices. Vertices reach this point untransformed, so the
// eye space planes go to object space with the modelview (p_obj = p_eye * M); attributes are interpolated linearly
// like GL does in clip space (object -> clip space is linear)
//----------------------------------------------------------------------------------
// The enabled planes in object coordinates, cached per modelview
static int objectClipPlanes(void)
{
    if (gl.clipObjectSerial == gl.matrixSerial) return gl.clipObjectCount;
    gl.clipObjectSerial = gl.matrixSerial;

    const float *m = gl.stack[0][gl.stackDepth[0]].m;
    int count = 0;
    for (int i = 0; i < C3DGL_MAX_CLIP_PLANES; i++)
    {
        if (!(gl.clipEnabled & (1u << i))) continue;
        const float *p = gl.clipPlanes[i];
        for (int c = 0; c < 4; c++) gl.clipObject[count][c] = p[0]*m[c*4] + p[1]*m[c*4 + 1] + p[2]*m[c*4 + 2] + p[3]*m[c*4 + 3];
        count++;
    }
    gl.clipObjectCount = count;
    return count;
}

static float clipDistance(const float plane[4], const Vertex *v)
{
    return plane[0]*v->pos[0] + plane[1]*v->pos[1] + plane[2]*v->pos[2] + plane[3];
}

// Points are kept or dropped whole
static bool pointClipped(const Vertex *v)
{
    int count = gl.clipEnabled? objectClipPlanes() : 0;
    for (int i = 0; i < count; i++) if (clipDistance(gl.clipObject[i], v) < 0.0f) return true;
    return false;
}

// Clip the segment *a -> *b; clipped ends are written to va/vb and *a/*b point there. False: nothing is left
static bool clipSegment(const Vertex **a, const Vertex **b, Vertex *va, Vertex *vb)
{
    int count = gl.clipEnabled? objectClipPlanes() : 0;
    float t0 = 0.0f, t1 = 1.0f;
    for (int i = 0; i < count; i++)
    {
        float da = clipDistance(gl.clipObject[i], *a), db = clipDistance(gl.clipObject[i], *b);
        if ((da < 0.0f) && (db < 0.0f)) return false;
        if (da < 0.0f) t0 = fmaxf(t0, da/(da - db));
        else if (db < 0.0f) t1 = fminf(t1, da/(da - db));
    }
    if (t0 > t1) return false;
    if ((t0 == 0.0f) && (t1 == 1.0f)) return true;

    const Vertex *oa = *a, *ob = *b;
    *va = *oa;
    *vb = *ob;
    if (t0 > 0.0f) lerpVertex(va, oa, ob, t0);
    if (t1 < 1.0f) lerpVertex(vb, oa, ob, t1);
    *a = va;
    *b = vb;
    return true;
}

static bool reservePolygonList(PolygonList *l, int count)
{
    if (count <= l->capacity) return true;

    Vertex *verts = realloc(l->verts, count*sizeof(Vertex));
    if (verts != NULL) l->verts = verts;
    bool *edges = realloc(l->edges, count*sizeof(bool));
    if (edges != NULL) l->edges = edges;
    if ((verts == NULL) || (edges == NULL)) { setError(GL_OUT_OF_MEMORY); return false; }
    l->capacity = count;
    return true;
}

// Sutherland-Hodgman against the enabled planes. On return *vs/*edges point to the clipped polygon (unchanged if
// it is completely inside), the result is its vertex count (< 3: nothing left). Parts of the original edges keep
// their edge flags, edges along a clip plane are not drawn by glPolygonMode outlines
static int clipPolygon(const Vertex *const **vs, const bool **edges, int n)
{
    int count = gl.clipEnabled? objectClipPlanes() : 0;
    bool inside = true;
    for (int p = 0; (p < count) && inside; p++)
        for (int i = 0; i < n; i++) if (clipDistance(gl.clipObject[p], (*vs)[i]) < 0.0f) { inside = false; break; }
    if (inside) return n;

    PolygonList *in = &gl.clipLists[0], *out = &gl.clipLists[1];
    if (!reservePolygonList(in, n)) return 0;
    for (int i = 0; i < n; i++) { in->verts[i] = *(*vs)[i]; in->edges[i] = (*edges)[i]; }

    for (int p = 0; (p < count) && (n >= 3); p++)
    {
        // Every edge adds at most two vertices
        if (!reservePolygonList(out, 2*n)) return 0;
        const float *plane = gl.clipObject[p];
        int m = 0;
        float da = clipDistance(plane, &in->verts[0]);
        for (int i = 0; i < n; i++)
        {
            const Vertex *a = &in->verts[i], *b = &in->verts[(i + 1) % n];
            float db = clipDistance(plane, b);
            if (da >= 0.0f) { out->verts[m] = *a; out->edges[m++] = in->edges[i]; }
            if ((da >= 0.0f) != (db >= 0.0f))
            {
                out->verts[m] = *a;
                lerpVertex(&out->verts[m], a, b, da/(da - db));
                out->edges[m++] = (da < 0.0f) && in->edges[i];     // Leaving: the next edge runs along the plane
            }
            da = db;
        }
        n = m;
        PolygonList *t = in; in = out; out = t;
    }
    if (n < 3) return 0;

    if (n > gl.clipPtrCapacity)
    {
        const Vertex **ptrs = realloc(gl.clipPtrs, n*sizeof(Vertex *));
        if (ptrs == NULL) { setError(GL_OUT_OF_MEMORY); return 0; }
        gl.clipPtrs = ptrs;
        gl.clipPtrCapacity = n;
    }
    for (int i = 0; i < n; i++) gl.clipPtrs[i] = &in->verts[i];
    *vs = gl.clipPtrs;
    *edges = in->edges;
    return n;
}

#define CLIP_W_MIN  1e-5f       // Lines and points are clipped against w > CLIP_W_MIN before the divide

//----------------------------------------------------------------------------------
// Feedback and selection (GL 1.1 sections 5.2, 5.3): in GL_FEEDBACK and GL_SELECT render mode nothing is drawn. Points,
// lines and polygons (after user clip planes, culling and polygon mode) are clipped against the view volume in clip
// space here and either written to the feedback buffer as tokens with their vertices in window coordinates, or
// recorded as a hit with their window depth range. glRasterPos hits, glBitmap/glDrawPixels/glCopyPixels feed back the
// raster position
//----------------------------------------------------------------------------------
// A vertex as feedback sees it: clip coordinates, color, texcoords of unit 0 with the texture matrix applied
typedef struct {
    float clip[4], color[4], tex[4];
} FeedbackVertex;

static void feedbackVertexOf(const Vertex *v, FeedbackVertex *out)
{
    mat4Transform(projectionModelview(), v->pos, out->clip);
    for (int i = 0; i < 4; i++) out->color[i] = v->color[i]*(1.0f/255.0f);
    float tc[4] = { v->tex[0], v->tex[1], v->texR, v->tex[2] };
    if (gl.texGen[0].enabled) { memcpy(out->tex, tc, sizeof(tc)); return; }    // Generated: matrix applied already

    const float *tm = gl.stack[2][gl.stackDepth[2]].m;
    for (int r = 0; r < 4; r++) out->tex[r] = tm[r]*tc[0] + tm[4 + r]*tc[1] + tm[8 + r]*tc[2] + tm[12 + r]*tc[3];
}

static void lerpFeedbackVertex(FeedbackVertex *out, const FeedbackVertex *a, const FeedbackVertex *b, float t)
{
    for (int i = 0; i < 4; i++)
    {
        out->clip[i] = a->clip[i] + (b->clip[i] - a->clip[i])*t;
        out->color[i] = a->color[i] + (b->color[i] - a->color[i])*t;
        out->tex[i] = a->tex[i] + (b->tex[i] - a->tex[i])*t;
    }
}

// Distance to view volume plane 0..5 (-w <= x, x <= w, y, z), >= 0 inside
static float viewPlaneDistance(const float c[4], int plane)
{
    return (plane & 1)? (c[3] - c[plane >> 1]) : (c[3] + c[plane >> 1]);
}

static void windowCoords(const float clip[4], float out[3])
{
    const GLint *vp = gl.state.viewport;
    float n = gl.state.depthNear, f = gl.state.depthFar;
    out[0] = vp[0] + 0.5f*(clip[0]/clip[3] + 1.0f)*vp[2];
    out[1] = vp[1] + 0.5f*(clip[1]/clip[3] + 1.0f)*vp[3];
    out[2] = n + 0.5f*(clip[2]/clip[3] + 1.0f)*(f - n);
}

static void feedbackValue(GLfloat value)
{
    if (gl.feedbackCount < gl.feedbackSize) gl.feedbackBuffer[gl.feedbackCount] = value;
    if (gl.feedbackCount <= gl.feedbackSize) gl.feedbackCount++;
}

// One vertex in the layout of the feedback type: window x, y, z, clip w, color, texcoords
static void feedbackWrite(const float win[3], float w, const float color[4], const float tex[4])
{
    GLenum type = gl.feedbackType;
    feedbackValue(win[0]);
    feedbackValue(win[1]);
    if (type != GL_2D) feedbackValue(win[2]);
    if (type == GL_4D_COLOR_TEXTURE) feedbackValue(w);
    if ((type == GL_2D) || (type == GL_3D)) return;
    for (int i = 0; i < 4; i++) feedbackValue(color[i]);
    if (type != GL_3D_COLOR) for (int i = 0; i < 4; i++) feedbackValue(tex[i]);
}

static void selectHit(float z)
{
    gl.hit = true;
    gl.hitMinZ = fminf(gl.hitMinZ, z);
    gl.hitMaxZ = fmaxf(gl.hitMaxZ, z);
}

// A clipped primitive: token (and vertex count for polygons) with its vertices, or a hit
static void feedbackPrimitive(GLenum token, const FeedbackVertex *vs, int n)
{
    if (gl.renderMode == GL_SELECT)
    {
        for (int i = 0; i < n; i++)
        {
            float win[3];
            windowCoords(vs[i].clip, win);
            selectHit(win[2]);
        }
        return;
    }

    feedbackValue((GLfloat)token);
    if (token == GL_POLYGON_TOKEN) feedbackValue((GLfloat)n);
    for (int i = 0; i < n; i++)
    {
        float win[3];
        windowCoords(vs[i].clip, win);
        feedbackWrite(win, vs[i].clip[3], vs[i].color, vs[i].tex);
    }
}

// Points are kept or dropped whole
static void feedbackPoint(const Vertex *v)
{
    FeedbackVertex f;
    feedbackVertexOf(v, &f);
    for (int p = 0; p < 6; p++) if (viewPlaneDistance(f.clip, p) < 0.0f) return;
    if (f.clip[3] <= 0.0f) return;      // Only the origin is left, which has no window position
    feedbackPrimitive(GL_POINT_TOKEN, &f, 1);
}

static void feedbackLine(const Vertex *a, const Vertex *b)
{
    FeedbackVertex f[2], clipped[2];
    feedbackVertexOf(a, &f[0]);
    feedbackVertexOf(b, &f[1]);
    float t0 = 0.0f, t1 = 1.0f;
    for (int p = 0; p < 6; p++)
    {
        float da = viewPlaneDistance(f[0].clip, p), db = viewPlaneDistance(f[1].clip, p);
        if ((da < 0.0f) && (db < 0.0f)) return;
        if (da < 0.0f) t0 = fmaxf(t0, da/(da - db));
        else if (db < 0.0f) t1 = fminf(t1, da/(da - db));
    }
    if (t0 > t1) return;
    lerpFeedbackVertex(&clipped[0], &f[0], &f[1], t0);
    lerpFeedbackVertex(&clipped[1], &f[0], &f[1], t1);
    if ((clipped[0].clip[3] <= 0.0f) || (clipped[1].clip[3] <= 0.0f)) return;

    feedbackPrimitive(gl.lineReset? GL_LINE_RESET_TOKEN : GL_LINE_TOKEN, clipped, 2);
    gl.lineReset = false;
}

// Filled polygon: Sutherland-Hodgman against the view volume
static void feedbackFilledPolygon(const Vertex *const *vs, int n, const Vertex *pv, bool back)
{
    FeedbackVertex *in = malloc((size_t)n*sizeof(FeedbackVertex)), *out = NULL;
    if (in == NULL) { setError(GL_OUT_OF_MEMORY); return; }
    for (int i = 0; i < n; i++)
    {
        Vertex shaded = *vs[i];
        const Vertex *src = (pv != NULL)? pv : vs[i];
        memcpy(shaded.color, back? src->backColor : src->color, sizeof(shaded.color));
        feedbackVertexOf(&shaded, &in[i]);
    }

    for (int p = 0; (p < 6) && (n >= 3); p++)
    {
        // Every edge adds at most two vertices
        FeedbackVertex *grown = realloc(out, 2*(size_t)n*sizeof(FeedbackVertex));
        if (grown == NULL) { setError(GL_OUT_OF_MEMORY); n = 0; break; }
        out = grown;
        int m = 0;
        float da = viewPlaneDistance(in[0].clip, p);
        for (int i = 0; i < n; i++)
        {
            const FeedbackVertex *a = &in[i], *b = &in[(i + 1) % n];
            float db = viewPlaneDistance(b->clip, p);
            if (da >= 0.0f) out[m++] = *a;
            if ((da >= 0.0f) != (db >= 0.0f)) lerpFeedbackVertex(&out[m++], a, b, da/(da - db));
            da = db;
        }
        n = m;
        FeedbackVertex *t = in; in = out; out = t;
    }
    for (int i = 0; i < n; i++) if (in[i].clip[3] <= 0.0f) n = 0;     // Degenerate: collapsed onto the eye
    if (n >= 3) feedbackPrimitive(GL_POLYGON_TOKEN, in, n);
    free(in);
    free(out);
}

// glBitmap, glDrawPixels, glCopyPixels in feedback mode: the token and the raster position (in selection mode the
// raster position counted as a hit already)
static void feedbackRaster(GLenum token)
{
    if ((gl.renderMode != GL_FEEDBACK) || !gl.raster.valid) return;
    float color[4];
    for (int i = 0; i < 4; i++) color[i] = gl.raster.color[i]*(1.0f/255.0f);
    feedbackValue((GLfloat)token);
    feedbackWrite(gl.raster.pos, gl.raster.pos[3], color, gl.raster.tex);
}

// Quad around the NDC positions a and b, widened by (nx, ny) perpendicular and (ex, ey) along a -> b
static void emitExpandedQuad(const Vertex *a, const Vertex *b, const float pa[3], const float pb[3],
                             float nx, float ny, float ex, float ey)
{
    Vertex q[4] = { *a, *a, *b, *b };
    q[0].pos[0] = pa[0] - ex - nx; q[0].pos[1] = pa[1] - ey - ny; q[0].pos[2] = pa[2];
    q[1].pos[0] = pa[0] - ex + nx; q[1].pos[1] = pa[1] - ey + ny; q[1].pos[2] = pa[2];
    q[2].pos[0] = pb[0] + ex + nx; q[2].pos[1] = pb[1] + ey + ny; q[2].pos[2] = pb[2];
    q[3].pos[0] = pb[0] + ex - nx; q[3].pos[1] = pb[1] + ey - ny; q[3].pos[2] = pb[2];

    emitTriangle(&q[0], &q[1], &q[2]);
    emitTriangle(&q[0], &q[2], &q[3]);
}

// Line stipple (GL 1.1 section 3.4.2): fragment s of a strip (counted along the major axis, carried over from segment to
// segment) is drawn when bit (s/factor) % 16 of the pattern is set. Fragment k of the segment is the pixel column (row)
// k whose center lies in [start, end) along the major axis; each run of drawn fragments becomes a quad that ends on the
// pixel edges between them, clipped to the line with its square caps (capU along the major axis)
static void emitStippledLine(const Vertex *a, const Vertex *b, const float pa[3], const float pb[3], float nx, float ny,
                             float capU)
{
    const GLint *vp = gl.state.viewport;
    float halfW = 0.5f*(float)vp[2], halfH = 0.5f*(float)vp[3];
    float wa[2] = { vp[0] + (pa[0] + 1.0f)*halfW, vp[1] + (pa[1] + 1.0f)*halfH };
    float wb[2] = { vp[0] + (pb[0] + 1.0f)*halfW, vp[1] + (pb[1] + 1.0f)*halfH };
    int axis = (fabsf(wb[0] - wa[0]) >= fabsf(wb[1] - wa[1]))? 0 : 1;

    // u: window coordinate along the major axis, mirrored to increase from a to b
    float sign = (wb[axis] >= wa[axis])? 1.0f : -1.0f;
    float ua = sign*wa[axis], ub = sign*wb[axis];
    // First fragment, one past the last; endpoints within 1/256 pixel of a center (rounding of vertices given at
    // pixel centers) count as on it
    float first = ceilf(ua - 0.5f - 1.0f/256.0f), end = ceilf(ub - 0.5f - 1.0f/256.0f);
    if (!(end > first)) return;

    u32 factor = (u32)gl.lineStippleFactor, period = 16*factor, s = gl.stippleCounter;
    gl.stippleCounter = (s + (u32)fmodf(end - first, (float)period)) % period;

    // Quads only for the fragments that can be on the screen (window 0..400 along either axis, plus the line width)
    float margin = ceilf(2.0f*capU) + 1.0f;
    float screen0 = ((sign > 0.0f)? 0.0f : -(float)C3DGL_TOP_SCREEN_WIDTH) - margin;
    float k0 = fmaxf(first, screen0), k1 = fminf(end, screen0 + (float)C3DGL_TOP_SCREEN_WIDTH + 2.0f*margin);
    if (!(k1 > k0)) return;
    s = (s + (u32)fmodf(k0 - first, (float)period)) % period;

    int n = (int)(k1 - k0);
    for (int k = 0; k < n; )
    {
        bool on = (gl.lineStipplePattern >> (((s + (u32)k) % period)/factor)) & 1;
        int run = k + 1;
        while ((run < n) && ((bool)((gl.lineStipplePattern >> (((s + (u32)run) % period)/factor)) & 1) == on)) run++;
        if (on)
        {
            float u0 = fmaxf(k0 + (float)k, ua - capU), u1 = fminf(k0 + (float)run, ub + capU);
            float t0 = (u0 - ua)/(ub - ua), t1 = (u1 - ua)/(ub - ua);
            Vertex va, vb;
            lerpVertex(&va, a, b, fminf(fmaxf(t0, 0.0f), 1.0f));
            lerpVertex(&vb, a, b, fminf(fmaxf(t1, 0.0f), 1.0f));
            float p0[3], p1[3];
            for (int i = 0; i < 3; i++) { p0[i] = pa[i] + (pb[i] - pa[i])*t0; p1[i] = pa[i] + (pb[i] - pa[i])*t1; }
            emitExpandedQuad(&va, &vb, p0, p1, nx, ny, 0.0f, 0.0f);
        }
        k = run;
    }
}

// Expand a line to a screen-aligned quad in NDC (batch must be in clipSpace mode).
// zBias is added to the NDC depth (polygon offset of polygon outlines)
static void emitLine(const Vertex *a, const Vertex *b, float zBias)
{
    Vertex clippedA, clippedB;
    if (!clipSegment(&a, &b, &clippedA, &clippedB)) return;
    if (gl.renderMode != GL_RENDER) { feedbackLine(a, b); return; }
    if (gl.lineReset) { gl.stippleCounter = 0; gl.lineReset = false; }

    const Mat4 *pmv = projectionModelview();

    float ca[4], cb[4];
    mat4Transform(pmv, a->pos, ca);
    mat4Transform(pmv, b->pos, cb);

    // Clip against w > 0 before the perspective divide
    if ((ca[3] < CLIP_W_MIN) && (cb[3] < CLIP_W_MIN)) return;

    Vertex va = *a, vb = *b;
    if (ca[3] < CLIP_W_MIN)
    {
        float t = (CLIP_W_MIN - ca[3])/(cb[3] - ca[3]);
        for (int i = 0; i < 4; i++) ca[i] += (cb[i] - ca[i])*t;
        lerpVertex(&va, a, b, t);
    }
    else if (cb[3] < CLIP_W_MIN)
    {
        float t = (CLIP_W_MIN - cb[3])/(ca[3] - cb[3]);
        for (int i = 0; i < 4; i++) cb[i] += (ca[i] - cb[i])*t;
        lerpVertex(&vb, b, a, t);
    }

    float pa[3] = { ca[0]/ca[3], ca[1]/ca[3], ca[2]/ca[3] + zBias };
    float pb[3] = { cb[0]/cb[3], cb[1]/cb[3], cb[2]/cb[3] + zBias };

    // Direction in pixels, then half line width back to NDC (perpendicular and along the line for square caps)
    float halfW = 0.5f*(float)gl.state.viewport[2], halfH = 0.5f*(float)gl.state.viewport[3];
    float dx = (pb[0] - pa[0])*halfW, dy = (pb[1] - pa[1])*halfH;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 1e-6f) { dx = 1.0f; dy = 0.0f; }
    else { dx /= len; dy /= len; }

    float r = 0.5f*gl.lineWidth;
    if (gl.lineStipple && (len >= 1e-6f))
    {
        // The caps reach r further along the line, r*|major component of the direction| along the major axis
        emitStippledLine(&va, &vb, pa, pb, -dy*r/halfW, dx*r/halfH, r*fmaxf(fabsf(dx), fabsf(dy)));
        return;
    }
    emitExpandedQuad(&va, &vb, pa, pb, -dy*r/halfW, dx*r/halfH, dx*r/halfW, dy*r/halfH);
}

// Point size of v (glPointParameter, GL 1.4 / ES 1.1 3.3): glPointSize or the point size array value, scaled by the
// distance attenuation 1/sqrt(a + b*d + c*d^2) of the eye distance d, clamped to GL_POINT_SIZE_MIN/MAX. The fade
// threshold only applies with multisampling, which PICA does not have
static float pointSize(const Vertex *v)
{
    float size = (v->pointSize >= 0.0f)? v->pointSize : gl.pointSize;
    const float *att = gl.pointAttenuation;
    if ((att[0] != 1.0f) || (att[1] != 0.0f) || (att[2] != 0.0f))
    {
        const float *m = gl.stack[0][gl.stackDepth[0]].m;
        float e[3];
        for (int i = 0; i < 3; i++) e[i] = m[i]*v->pos[0] + m[4 + i]*v->pos[1] + m[8 + i]*v->pos[2] + m[12 + i];
        float d = sqrtf(e[0]*e[0] + e[1]*e[1] + e[2]*e[2]);
        float k = att[0] + att[1]*d + att[2]*d*d;
        size = (k > 0.0f)? size/sqrtf(k) : C3DGL_MAX_POINT_SIZE;
    }
    size = fminf(fmaxf(size, gl.pointSizeMin), gl.pointSizeMax);
    return fminf(fmaxf(size, 1.0f), C3DGL_MAX_POINT_SIZE);
}

// Expand a point to a screen-aligned square in NDC (batch must be in clipSpace mode). Point sprites: the texcoords
// of the batch's sprite units run from (0, 0) at the top left to (1, 1) at the bottom right (OES_point_sprite)
static void emitPoint(const Vertex *v, float zBias)
{
    if (pointClipped(v)) return;
    if (gl.renderMode != GL_RENDER) { feedbackPoint(v); return; }

    float c[4];
    mat4Transform(projectionModelview(), v->pos, c);
    if (c[3] < CLIP_W_MIN) return;

    float p[3] = { c[0]/c[3], c[1]/c[3], c[2]/c[3] + zBias };
    float r = 0.5f*pointSize(v);
    float rx = 2.0f*r/(float)gl.state.viewport[2], ry = 2.0f*r/(float)gl.state.viewport[3];

    // Corners bottom left, top left, top right, bottom right (NDC y points up)
    static const float corner[4][2] = { { -1, -1 }, { -1, 1 }, { 1, 1 }, { 1, -1 } };
    Vertex q[4];
    for (int i = 0; i < 4; i++)
    {
        q[i] = *v;
        q[i].pos[0] = p[0] + corner[i][0]*rx;
        q[i].pos[1] = p[1] + corner[i][1]*ry;
        q[i].pos[2] = p[2];
        for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
        {
            if (!((gl.batch.spriteUnits >> unit) & 1)) continue;
            float *tc = (unit == 0)? q[i].tex : q[i].texExtra[unit - 1];
            tc[0] = 0.5f + 0.5f*corner[i][0];
            tc[1] = 0.5f - 0.5f*corner[i][1];
            tc[2] = 1.0f;
        }
    }
    emitTriangle(&q[0], &q[1], &q[2]);
    emitTriangle(&q[0], &q[2], &q[3]);
}

// Copy of v with the color of the provoking vertex pv (flat shading, NULL: smooth) and a depth bias.
// back: take the back color (back-facing polygon with two-sided lighting)
static Vertex shadeVertex(const Vertex *v, const Vertex *pv, float depthBias, bool back)
{
    Vertex out = *v;
    const Vertex *src = (pv != NULL)? pv : v;
    memcpy(out.color, back? src->backColor : src->color, sizeof(out.color));
    out.depthBias = depthBias;
    return out;
}

// Provoking vertex for flat shading, NULL when shading is smooth
#define FLAT(pv) ((gl.shadeModel == GL_FLAT)? (pv) : NULL)

static void emitShadedLine(const Vertex *a, const Vertex *b, const Vertex *pv)
{
    Vertex va = shadeVertex(a, pv, 0.0f, false), vb = shadeVertex(b, pv, 0.0f, false);
    emitLine(&va, &vb, 0.0f);
}

// Twice the signed area of the polygon in clip space (x, y, w), positive when counter-clockwise on screen.
// Homogeneous, so it is also right for vertices behind the viewer
static float polygonArea(const Vertex *const *vs, int n)
{
    const Mat4 *pmv = projectionModelview();
    float c0[4], c1[4], c2[4];
    mat4Transform(pmv, vs[0]->pos, c0);
    mat4Transform(pmv, vs[1]->pos, c1);

    float area = 0.0f;
    for (int i = 2; i < n; i++)
    {
        mat4Transform(pmv, vs[i]->pos, c2);
        area += c0[0]*(c1[1]*c2[3] - c2[1]*c1[3]) - c1[0]*(c0[1]*c2[3] - c2[1]*c0[3]) + c2[0]*(c0[1]*c1[3] - c1[1]*c0[3]);
        memcpy(c1, c2, sizeof(c1));
    }
    return area;
}

#define DEPTH_RESOLUTION    (1.0f/16777216.0f)  // r of glPolygonOffset: one step of the 24-bit depth buffer

// glPolygonOffset in window depth units: factor*m + r*units, m = max depth slope in window space
static float polygonOffset(const Vertex *const *vs, int n)
{
    if (gl.offsetFactor == 0.0f) return gl.offsetUnits*DEPTH_RESOLUTION;

    const Mat4 *pmv = projectionModelview();
    float halfW = 0.5f*(float)gl.state.viewport[2], halfH = 0.5f*(float)gl.state.viewport[3];
    float halfD = 0.5f*(gl.state.depthFar - gl.state.depthNear);

    // Window coordinates of three vertices (the polygon is planar); behind the viewer: no slope
    float w[3][3];
    for (int i = 0; i < 3; i++)
    {
        float c[4];
        mat4Transform(pmv, vs[(i == 0)? 0 : (n - 3 + i)]->pos, c);
        if (c[3] < CLIP_W_MIN) return gl.offsetUnits*DEPTH_RESOLUTION;
        w[i][0] = c[0]/c[3]*halfW;
        w[i][1] = c[1]/c[3]*halfH;
        w[i][2] = c[2]/c[3]*halfD;
    }

    // Depth gradient from the plane normal
    float e1[3] = { w[1][0] - w[0][0], w[1][1] - w[0][1], w[1][2] - w[0][2] };
    float e2[3] = { w[2][0] - w[0][0], w[2][1] - w[0][1], w[2][2] - w[0][2] };
    float nx = e1[1]*e2[2] - e1[2]*e2[1], ny = e1[2]*e2[0] - e1[0]*e2[2], nz = e1[0]*e2[1] - e1[1]*e2[0];
    float m = (fabsf(nz) > 1e-12f)? fmaxf(fabsf(nx/nz), fabsf(ny/nz)) : 0.0f;

    return gl.offsetFactor*m + gl.offsetUnits*DEPTH_RESOLUTION;
}

// Polygon in feedback/selection mode (see the feedback section): culled, then filled, outlined or as vertices like
// emitPolygon() does
static void feedbackPolygon(const Vertex *const *vs, const bool *edges, int n, const Vertex *pv)
{
    bool front = (polygonArea(vs, n) > 0.0f) == (gl.state.frontFace == GL_CCW);
    if (gl.state.cull && ((gl.state.cullFace == GL_FRONT_AND_BACK) || ((gl.state.cullFace == GL_FRONT) == front))) return;
    bool back = gl.lightingEnabled && gl.lighting.twoSide && !front;

    GLenum mode = gl.polygonMode[front? 0 : 1];
    if (mode == GL_FILL) { feedbackFilledPolygon(vs, n, pv, back); return; }

    gl.lineReset = true;
    for (int i = 0; i < n; i++)
    {
        if (!edges[i]) continue;
        Vertex a = shadeVertex(vs[i], pv, 0.0f, back);
        if (mode == GL_LINE)
        {
            Vertex b = shadeVertex(vs[(i + 1) % n], pv, 0.0f, back);
            feedbackLine(&a, &b);
        }
        else feedbackPoint(&a);
    }
}

// A polygon (triangle, quad, polygon) with edge flags: filled, outlined or as vertices depending on
// glPolygonMode of the side that faces the viewer. pv: provoking vertex for flat shading
static void emitPolygon(const Vertex *const *vs, const bool *edges, int n, const Vertex *pv)
{
    if (gl.clipEnabled && ((n = clipPolygon(&vs, &edges, n)) < 3)) return;
    if (gl.renderMode != GL_RENDER) { feedbackPolygon(vs, edges, n, pv); return; }

    GLenum mode = GL_FILL;
    bool polygonModes = (gl.polygonMode[0] != GL_FILL) || (gl.polygonMode[1] != GL_FILL);
    bool twoSided = gl.lightingEnabled && gl.lighting.twoSide;
    bool front = true;
    if (polygonModes || twoSided) front = (polygonArea(vs, n) > 0.0f) == (gl.state.frontFace == GL_CCW);
    bool back = twoSided && !front;
    if (polygonModes)
    {
        // Culling on the CPU: outlines and vertices are drawn in NDC, the GPU cannot cull them
        if (gl.state.cull && ((gl.state.cullFace == GL_FRONT_AND_BACK) || ((gl.state.cullFace == GL_FRONT) == front))) return;

        mode = gl.polygonMode[front? 0 : 1];
        prepareDraw(mode != GL_FILL, mode == GL_POINT);
    }

    bool offset = (mode == GL_FILL)? gl.offsetFill : (mode == GL_LINE)? gl.offsetLine : gl.offsetPoint;
    float range = gl.state.depthFar - gl.state.depthNear;
    float windowOffset = (offset && (range != 0.0f))? polygonOffset(vs, n)/range : 0.0f;    // In PICA NDC (= 1/2 GL NDC)

    if ((mode == GL_FILL) && (pv == NULL) && !back && (windowOffset == 0.0f))
    {
        // Common case: the vertices go to the buffer as they are (their depth bias is 0)
        for (int i = 1; i + 1 < n; i++) emitTriangle(vs[0], vs[i], vs[i + 1]);
    }
    else if (mode == GL_FILL)
    {
        Vertex a = shadeVertex(vs[0], pv, windowOffset, back);
        for (int i = 1; i + 1 < n; i++)
        {
            Vertex b = shadeVertex(vs[i], pv, windowOffset, back), c = shadeVertex(vs[i + 1], pv, windowOffset, back);
            emitTriangle(&a, &b, &c);
        }
    }
    else
    {
        // Edge i runs from vertex i to i + 1; its flag also decides whether vertex i is drawn as a point. The outline
        // restarts the line stipple
        gl.lineReset = true;
        for (int i = 0; i < n; i++)
        {
            if (!edges[i]) continue;
            Vertex a = shadeVertex(vs[i], pv, 0.0f, back);
            if (mode == GL_LINE)
            {
                Vertex b = shadeVertex(vs[(i + 1) % n], pv, 0.0f, back);
                emitLine(&a, &b, 2.0f*windowOffset);
            }
            else emitPoint(&a, 2.0f*windowOffset);
        }
    }
}

static const bool allEdges[4] = { true, true, true, true };

static bool beginPrimitive(GLenum mode)
{
    if (mode > GL_POLYGON)
    {
        WARN_ONCE("Primitive 0x%x not supported\n", mode);
        setError(GL_INVALID_ENUM);
        return false;
    }

    bool lineOrPoint = (mode == GL_POINTS) || (mode == GL_LINES) || (mode == GL_LINE_STRIP) || (mode == GL_LINE_LOOP);
    bool render = (gl.renderMode == GL_RENDER);
    if (render) prepareDraw(lineOrPoint, mode == GL_POINTS);
    gl.primitive = mode;
    gl.primCount = 0;
    gl.primTotal = 0;
    gl.lineReset = true;
    // Feedback reports a polygon as one, not as the triangles it is filled with
    gl.collectPolygon = (mode == GL_POLYGON) && (!render || (gl.polygonMode[0] != GL_FILL) || (gl.polygonMode[1] != GL_FILL));
    gl.polyCount = 0;
    return true;
}

static void collectPolygonVertex(const Vertex *v, bool edge)
{
    if (gl.polyCount == gl.polyCapacity)
    {
        int capacity = gl.polyCapacity? gl.polyCapacity*2 : 32;
        Vertex *verts = realloc(gl.polyVerts, capacity*sizeof(Vertex));
        if (verts != NULL) gl.polyVerts = verts;
        bool *edges = realloc(gl.polyEdges, capacity*sizeof(bool));
        if (edges != NULL) gl.polyEdges = edges;
        const Vertex **ptrs = realloc(gl.polyPtrs, capacity*sizeof(Vertex *));
        if (ptrs != NULL) gl.polyPtrs = ptrs;
        if ((verts == NULL) || (edges == NULL) || (ptrs == NULL)) { setError(GL_OUT_OF_MEMORY); return; }
        gl.polyCapacity = capacity;
    }
    gl.polyVerts[gl.polyCount] = *v;
    gl.polyEdges[gl.polyCount] = edge;
    gl.polyCount++;
}

// Assemble the primitive from the submitted vertices. prim[] holds what the mode still needs:
//   strips:              the last two vertices (quad strip: up to four)
//   fans, polygons, line loops/strips: the first and the last vertex
// Provoking vertices (flat shading) follow GL: the last vertex of each primitive, the first one for GL_POLYGON.
// Edge flags only apply to separate triangles, quads and polygons
static void submitVertex(const Vertex *v, bool edge)
{
    Vertex *p = gl.prim;
    int n = gl.primTotal++;

    switch (gl.primitive)
    {
        case GL_POINTS:
            emitPoint(v, 0.0f);
            break;
        case GL_LINES:
            p[gl.primCount++] = *v;
            if (gl.primCount == 2) { gl.lineReset = true; emitShadedLine(&p[0], &p[1], FLAT(&p[1])); gl.primCount = 0; }
            break;
        case GL_LINE_STRIP:
        case GL_LINE_LOOP:
            if (n == 0) p[0] = *v;
            else emitShadedLine(&p[1], v, FLAT(v));
            p[1] = *v;
            break;
        case GL_TRIANGLES:
            gl.primEdge[gl.primCount] = edge;
            p[gl.primCount++] = *v;
            if (gl.primCount == 3)
            {
                const Vertex *tri[3] = { &p[0], &p[1], &p[2] };
                emitPolygon(tri, gl.primEdge, 3, FLAT(&p[2]));
                gl.primCount = 0;
            }
            break;
        case GL_TRIANGLE_STRIP:
            // Every other triangle is flipped to keep the winding of the first one
            if (n >= 2)
            {
                const Vertex *odd[3] = { &p[1], &p[0], v }, *even[3] = { &p[0], &p[1], v };
                emitPolygon((n & 1)? odd : even, allEdges, 3, FLAT(v));
            }
            p[0] = p[1];
            p[1] = *v;
            break;
        case GL_TRIANGLE_FAN:
            if (n == 0) p[0] = *v;
            else if (n >= 2)
            {
                const Vertex *tri[3] = { &p[0], &p[1], v };
                emitPolygon(tri, allEdges, 3, FLAT(v));
            }
            p[1] = *v;
            break;
        case GL_POLYGON:
            if (gl.collectPolygon) { collectPolygonVertex(v, edge); break; }
            // Filled: a fan, all triangles shaded with the first vertex
            if (n == 0) p[0] = *v;
            else if (n >= 2)
            {
                const Vertex *tri[3] = { &p[0], &p[1], v };
                emitPolygon(tri, allEdges, 3, FLAT(&p[0]));
            }
            p[1] = *v;
            break;
        case GL_QUADS:
            gl.primEdge[gl.primCount] = edge;
            p[gl.primCount++] = *v;
            if (gl.primCount == 4)
            {
                const Vertex *quad[4] = { &p[0], &p[1], &p[2], &p[3] };
                emitPolygon(quad, gl.primEdge, 4, FLAT(&p[3]));
                gl.primCount = 0;
            }
            break;
        case GL_QUAD_STRIP:
            // Quad i is v[2i], v[2i+1], v[2i+3], v[2i+2]
            p[gl.primCount++] = *v;
            if (gl.primCount == 4)
            {
                const Vertex *quad[4] = { &p[0], &p[1], &p[3], &p[2] };
                emitPolygon(quad, allEdges, 4, FLAT(&p[3]));
                p[0] = p[2];
                p[1] = p[3];
                gl.primCount = 2;
            }
            break;
        default: break;
    }
}

// glEnd or the end of a glDraw* call: close line loops, draw collected polygons, drop incomplete primitives
static void endPrimitive(void)
{
    // The closing segment of a loop is shaded with the first vertex
    if ((gl.primitive == GL_LINE_LOOP) && (gl.primTotal >= 2)) emitShadedLine(&gl.prim[1], &gl.prim[0], FLAT(&gl.prim[0]));

    if (gl.collectPolygon && (gl.polyCount >= 3))
    {
        for (int i = 0; i < gl.polyCount; i++) gl.polyPtrs[i] = &gl.polyVerts[i];
        emitPolygon(gl.polyPtrs, gl.polyEdges, gl.polyCount, FLAT(&gl.polyVerts[0]));
    }

    gl.primCount = 0;
    gl.primTotal = 0;
    gl.polyCount = 0;
    gl.collectPolygon = false;
}
