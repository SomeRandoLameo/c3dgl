// c3dgl example: GLU (Mesa GLU, c3dgl::glu), one cell each on the top screen (4x2 grid, 100x120 px per cell),
// numeric self-checks on the bottom screen. The expected result is printed there as well.
// Page 1: matrices, images, quadrics. Page 2: tessellator and NURBS (tessellator mode).
// Page 3: NURBS rendered through GL evaluators (GLU_NURBS_RENDERER). A switches pages.
#include <3ds.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <c3dgl.h>
#include <citro3d.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// GLU's NURBS code keeps large arrays on the stack and recurses: libctru's default 32 KB main thread stack is
// too small for NURBS rendering (stack overflow in Patch::Patch). Any app using GLU NURBS needs this
extern "C" {
u32 __stacksize__ = 256 * 1024;
}

namespace {

constexpr int COLUMNS = 4, ROWS = 2;
constexpr int CELL_W = C3DGL_TOP_SCREEN_WIDTH / COLUMNS, CELL_H = C3DGL_SCREEN_HEIGHT / ROWS;

int checks = 0, failures = 0;

void check(bool ok, const char* what) {
    checks++;
    if (ok) return;
    failures++;
    std::printf("FAIL: %s\n", what);
}

bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

void selfChecks() {
    check(std::strcmp(reinterpret_cast<const char*>(gluErrorString(GL_INVALID_ENUM)), "invalid enumerant") == 0, "gluErrorString");
    check(gluErrorString(12345) == nullptr, "gluErrorString unknown");
    check(std::strncmp(reinterpret_cast<const char*>(gluGetString(GLU_VERSION)), "1.3", 3) == 0, "gluGetString");
    const GLubyte* exts = reinterpret_cast<const GLubyte*>("GL_a GL_bb GL_c");
    check(gluCheckExtension(reinterpret_cast<const GLubyte*>("GL_bb"), exts), "gluCheckExtension found");
    check(!gluCheckExtension(reinterpret_cast<const GLubyte*>("GL_b"), exts), "gluCheckExtension prefix");

    // gluLookAt from the origin down -z with y up is the identity
    GLdouble m[16];
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0, 0, 0, 0, 0, -1, 0, 1, 0);
    glGetDoublev(GL_MODELVIEW_MATRIX, m);
    bool identity = true;
    for (int i = 0; i < 16; i++) identity = identity && near(m[i], (i % 5 == 0) ? 1.0 : 0.0, 1e-5);
    check(identity, "gluLookAt identity");

    // gluPerspective(90, 1, 1, 3): cot(45) = 1, z terms -2 and -3
    glLoadIdentity();
    gluPerspective(90.0, 1.0, 1.0, 3.0);
    glGetDoublev(GL_MODELVIEW_MATRIX, m);
    check(near(m[0], 1, 1e-5) && near(m[5], 1, 1e-5) && near(m[10], -2, 1e-5) && near(m[11], -1) && near(m[14], -3, 1e-5),
          "gluPerspective");

    // gluProject / gluUnProject round trip
    GLdouble model[16], proj[16];
    glLoadIdentity();
    gluLookAt(1, 2, 5, 0, 0, 0, 0, 1, 0);
    glGetDoublev(GL_MODELVIEW_MATRIX, model);
    glLoadIdentity();
    gluPerspective(60.0, 4.0 / 3.0, 0.5, 50.0);
    glGetDoublev(GL_MODELVIEW_MATRIX, proj);
    glLoadIdentity();
    const GLint view[4] = {10, 20, 400, 240};
    GLdouble wx, wy, wz, ox, oy, oz;
    check(gluProject(0.3, -0.7, 1.1, model, proj, view, &wx, &wy, &wz), "gluProject");
    check(gluUnProject(wx, wy, wz, model, proj, view, &ox, &oy, &oz), "gluUnProject");
    check(near(ox, 0.3, 1e-4) && near(oy, -0.7, 1e-4) && near(oz, 1.1, 1e-4), "project round trip");
    gluProject(0, 0, 0, model, proj, view, &wx, &wy, &wz);
    check(near(wx, 210, 1e-3) && near(wy, 140, 1e-3), "look-at center projects to viewport center");

    // gluScaleImage: 2x2 -> 1x1 averages, 1x1 -> 2x2 replicates, types convert
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    const GLubyte gray[4] = {0, 100, 200, 100};
    GLubyte one = 0;
    check(gluScaleImage(GL_LUMINANCE, 2, 2, GL_UNSIGNED_BYTE, gray, 1, 1, GL_UNSIGNED_BYTE, &one) == 0 && one == 100, "scale down");
    const GLubyte red[3] = {255, 0, 0};
    GLubyte big[2 * 2 * 3];
    gluScaleImage(GL_RGB, 1, 1, GL_UNSIGNED_BYTE, red, 2, 2, GL_UNSIGNED_BYTE, big);
    check(big[0] == 255 && big[1] == 0 && big[9] == 255 && big[11] == 0, "scale up");
    GLfloat f = 0;
    gluScaleImage(GL_ALPHA, 1, 1, GL_UNSIGNED_BYTE, &gray[3], 1, 1, GL_FLOAT, &f);
    check(near(f, 100.0 / 255.0, 1e-6), "scale type conversion");
    GLushort packed = 0;
    gluScaleImage(GL_RGB, 1, 1, GL_UNSIGNED_BYTE, red, 1, 1, GL_UNSIGNED_SHORT_5_6_5, &packed);
    check(packed == 0xF800, "scale to RGB565");
    check(gluScaleImage(GL_RGB, 1, 1, GL_UNSIGNED_SHORT_4_4_4_4, &packed, 1, 1, GL_UNSIGNED_BYTE, big) == GLU_INVALID_OPERATION,
          "scale format/type mismatch");
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    // gluBuild1DMipmaps: 12 texels scaled to a power of two, levels down to 1 texel
    GLuint tex1D = 0;
    glGenTextures(1, &tex1D);
    glBindTexture(GL_TEXTURE_1D, tex1D);
    GLubyte row[12 * 4];
    for (int i = 0; i < 12 * 4; i++) row[i] = static_cast<GLubyte>(i * 5);
    check(gluBuild1DMipmaps(GL_TEXTURE_1D, GL_RGBA, 12, GL_RGBA, GL_UNSIGNED_BYTE, row) == 0, "gluBuild1DMipmaps");
    GLint width0 = 0, widthLast = 0, levels = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_1D, 0, GL_TEXTURE_WIDTH, &width0);
    while ((width0 >> levels) > 1) levels++;
    glGetTexLevelParameteriv(GL_TEXTURE_1D, levels, GL_TEXTURE_WIDTH, &widthLast);
    check((width0 == 8 || width0 == 16) && widthLast == 1, "gluBuild1DMipmaps levels");
    glDeleteTextures(1, &tex1D);
    check(glGetError() == GL_NO_ERROR, "no GL error");
}

// 12x12 RGB checker (NPOT on purpose), turned into mipmaps by GLU (level 0 scaled to 16x16)
GLuint createChecker() {
    GLubyte pixels[12][12][3];
    for (int y = 0; y < 12; y++) {
        for (int x = 0; x < 12; x++) {
            const bool light = ((x / 3) + (y / 3)) % 2 == 0;
            pixels[y][x][0] = light ? 255 : 40;
            pixels[y][x][1] = light ? 220 : 60;
            pixels[y][x][2] = light ? 120 : 160;
        }
    }
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    check(gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, 12, 12, GL_RGB, GL_UNSIGNED_BYTE, pixels) == 0, "gluBuild2DMipmaps");
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return id;
}

// Cell (column, row from the top) with a perspective camera at distance 3 looking at the origin
void beginCell(int column, int row, double elevation = 1.0) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0, static_cast<double>(CELL_W) / CELL_H, 0.5, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0.0, elevation, 3.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0);
}

// Front faces filled, back faces as lines: whatever faces the viewer the wrong way shows up as wireframe
void frontFillBackLine() {
    glPolygonMode(GL_FRONT, GL_FILL);
    glPolygonMode(GL_BACK, GL_LINE);
}

// Page 2 ------------------------------------------------------------------------------------------------

// Cell with a 2D view: x -1..1, y -1.2..1.2
void beginFlatCell(int column, int row) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(-1.0, 1.0, -1.2, 1.2);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

// Vertices created by the combine callback at contour intersections, reset every frame
GLdouble combined[64][3];
int combinedCount = 0;

void GLAPIENTRY tessCombine(GLdouble coords[3], void* /*data*/[4], GLfloat /*weight*/[4], void** out) {
    GLdouble* v = combined[combinedCount++ % 64];
    v[0] = coords[0];
    v[1] = coords[1];
    v[2] = coords[2];
    *out = v;
}

void GLAPIENTRY tessError(GLenum error) {
    std::printf("tess error: %s\n", reinterpret_cast<const char*>(gluErrorString(error)));
}

GLUtesselator* newTess() {
    GLUtesselator* tess = gluNewTess();
    gluTessCallback(tess, GLU_TESS_BEGIN, reinterpret_cast<_GLUfuncptr>(glBegin));
    gluTessCallback(tess, GLU_TESS_VERTEX, reinterpret_cast<_GLUfuncptr>(glVertex3dv));
    gluTessCallback(tess, GLU_TESS_END, reinterpret_cast<_GLUfuncptr>(glEnd));
    gluTessCallback(tess, GLU_TESS_COMBINE, reinterpret_cast<_GLUfuncptr>(tessCombine));
    gluTessCallback(tess, GLU_TESS_ERROR, reinterpret_cast<_GLUfuncptr>(tessError));
    gluTessNormal(tess, 0, 0, 1);
    return tess;
}

// Contours as {x, y} lists; GLU keeps pointers to the vertices until gluTessEndPolygon
using Contour = std::vector<GLdouble>;

void tessellate(GLUtesselator* tess, GLenum winding, const std::vector<Contour>& contours, GLdouble storage[][3]) {
    gluTessProperty(tess, GLU_TESS_WINDING_RULE, winding);
    gluTessBeginPolygon(tess, nullptr);
    int n = 0;
    for (const auto& contour : contours) {
        gluTessBeginContour(tess);
        for (auto it = contour.begin(); it != contour.end(); it += 2) {
            GLdouble* v = storage[n++];
            v[0] = it[0];
            v[1] = it[1];
            v[2] = 0.0;
            gluTessVertex(tess, v, v);
        }
        gluTessEndContour(tess);
    }
    gluTessEndPolygon(tess);
}

// Five-pointed star drawn in one stroke (self-intersecting)
Contour pentagram() {
    Contour points;
    for (int i = 0; i < 5; i++) {
        const double a = M_PI / 2 + i * 4 * M_PI / 5;
        points.push_back(0.85 * std::cos(a));
        points.push_back(0.85 * std::sin(a));
    }
    return points;
}

void drawTessellation(GLUtesselator* tess) {
    static GLdouble storage[32][3];
    combinedCount = 0;

    // Concave arrow
    beginFlatCell(0, 0);
    glColor3f(0.3f, 0.8f, 1.0f);
    tessellate(tess, GLU_TESS_WINDING_ODD, {{-0.8, 0.2, 0.1, 0.2, 0.1, 0.7, 0.9, 0.0, 0.1, -0.7, 0.1, -0.2, -0.8, -0.2}}, storage);

    // Square with a square hole (two contours)
    beginFlatCell(1, 0);
    glColor3f(1.0f, 0.6f, 0.2f);
    tessellate(tess, GLU_TESS_WINDING_ODD, {{-0.8, -0.8, 0.8, -0.8, 0.8, 0.8, -0.8, 0.8},
                                            {-0.4, -0.4, 0.4, -0.4, 0.4, 0.4, -0.4, 0.4}}, storage);

    // Pentagram: ODD leaves the center open, NONZERO fills it
    beginFlatCell(2, 0);
    glColor3f(1.0f, 0.9f, 0.2f);
    tessellate(tess, GLU_TESS_WINDING_ODD, {pentagram()}, storage);
    beginFlatCell(3, 0);
    tessellate(tess, GLU_TESS_WINDING_NONZERO, {pentagram()}, storage);

    // Boundary only: the outline of the square with a hole (line loops)
    beginFlatCell(0, 1);
    glColor3f(1, 1, 1);
    glLineWidth(2.0f);
    gluTessProperty(tess, GLU_TESS_BOUNDARY_ONLY, GL_TRUE);
    tessellate(tess, GLU_TESS_WINDING_ODD, {{-0.8, -0.8, 0.8, -0.8, 0.8, 0.8, -0.8, 0.8},
                                            {-0.4, -0.4, 0.4, -0.4, 0.4, 0.4, -0.4, 0.4}}, storage);
    gluTessProperty(tess, GLU_TESS_BOUNDARY_ONLY, GL_FALSE);
    glLineWidth(1.0f);

    // Two overlapping squares, POSITIVE winding: their union (needs the combine callback), as wireframe
    beginFlatCell(1, 1);
    glColor3f(0.4f, 1.0f, 0.4f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    tessellate(tess, GLU_TESS_WINDING_POSITIVE, {{-0.8, -0.6, 0.3, -0.6, 0.3, 0.5, -0.8, 0.5},
                                                 {-0.3, -0.1, 0.8, -0.1, 0.8, 0.9, -0.3, 0.9}}, storage);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void GLAPIENTRY nurbsBegin(GLenum type) { glBegin(type); }
void GLAPIENTRY nurbsVertex(GLfloat* v) { glVertex3fv(v); }
void GLAPIENTRY nurbsEnd() { glEnd(); }

void GLAPIENTRY nurbsError(GLenum error) {
    std::printf("nurbs error: %s\n", reinterpret_cast<const char*>(gluErrorString(error)));
}

GLUnurbs* newNurbs() {
    GLUnurbs* nurbs = gluNewNurbsRenderer();
    gluNurbsProperty(nurbs, GLU_NURBS_MODE, GLU_NURBS_TESSELLATOR);
    gluNurbsProperty(nurbs, GLU_SAMPLING_METHOD, GLU_DOMAIN_DISTANCE);
    gluNurbsProperty(nurbs, GLU_U_STEP, 16);
    gluNurbsProperty(nurbs, GLU_V_STEP, 16);
    gluNurbsCallback(nurbs, GLU_NURBS_BEGIN, reinterpret_cast<_GLUfuncptr>(nurbsBegin));
    gluNurbsCallback(nurbs, GLU_NURBS_VERTEX, reinterpret_cast<_GLUfuncptr>(nurbsVertex));
    gluNurbsCallback(nurbs, GLU_NURBS_END, reinterpret_cast<_GLUfuncptr>(nurbsEnd));
    gluNurbsCallback(nurbs, GLU_NURBS_ERROR, reinterpret_cast<_GLUfuncptr>(nurbsError));
    return nurbs;
}

void drawNurbs(GLUnurbs* nurbs, float angle) {
    // Cubic B-spline curve through 6 control points (gray: control polygon)
    static GLfloat curve[6][3] = {{-0.9f, -0.8f, 0}, {-0.6f, 0.8f, 0}, {-0.1f, -0.9f, 0},
                                  {0.2f, 0.9f, 0}, {0.6f, -0.6f, 0}, {0.9f, 0.7f, 0}};
    static GLfloat curveKnots[10] = {0, 0, 0, 0, 1, 2, 3, 3, 3, 3};
    beginFlatCell(2, 1);
    glColor3f(0.5f, 0.5f, 0.5f);
    glBegin(GL_LINE_STRIP);
    for (const auto& p : curve) glVertex3fv(p);
    glEnd();
    glColor3f(1.0f, 0.4f, 0.8f);
    glLineWidth(2.0f);
    gluBeginCurve(nurbs);
    gluNurbsCurve(nurbs, 10, curveKnots, 3, &curve[0][0], 4, GL_MAP1_VERTEX_3);
    gluEndCurve(nurbs);
    glLineWidth(1.0f);

    // Bicubic patch with a bump in the middle, as wireframe of the generated triangles
    static GLfloat patch[4][4][3];
    for (int u = 0; u < 4; u++) {
        for (int v = 0; v < 4; v++) {
            patch[u][v][0] = -0.9f + 0.6f * u;
            patch[u][v][1] = -0.9f + 0.6f * v;
            patch[u][v][2] = ((u == 1 || u == 2) && (v == 1 || v == 2)) ? 1.2f : 0.0f;
        }
    }
    static GLfloat patchKnots[8] = {0, 0, 0, 0, 1, 1, 1, 1};
    beginCell(3, 1, 1.5);
    glRotatef(angle, 0, 1, 0);
    glRotatef(-90, 1, 0, 0);
    glEnable(GL_DEPTH_TEST);
    glColor3f(0.5f, 0.8f, 1.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    gluBeginSurface(nurbs);
    gluNurbsSurface(nurbs, 8, patchKnots, 8, patchKnots, 4 * 3, 3, &patch[0][0][0], 4, 4, GL_MAP2_VERTEX_3);
    gluEndSurface(nurbs);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_DEPTH_TEST);
}

// Page 3 ------------------------------------------------------------------------------------------------

GLUnurbs* newRenderer(GLfloat displayMode) {
    GLUnurbs* nurbs = gluNewNurbsRenderer();
    gluNurbsProperty(nurbs, GLU_SAMPLING_METHOD, GLU_DOMAIN_DISTANCE);
    gluNurbsProperty(nurbs, GLU_U_STEP, 10);
    gluNurbsProperty(nurbs, GLU_V_STEP, 10);
    gluNurbsProperty(nurbs, GLU_DISPLAY_MODE, displayMode);
    gluNurbsCallback(nurbs, GLU_NURBS_ERROR, reinterpret_cast<_GLUfuncptr>(nurbsError));
    return nurbs;
}

struct Renderers {
    GLUnurbs *fill, *outlinePolygon, *outlinePatch;
};

// Bicubic Bezier patch with a bump, 4x4 control points
GLfloat bumpPatch[4][4][3];
GLfloat patchKnots[8] = {0, 0, 0, 0, 1, 1, 1, 1};

void initBumpPatch() {
    for (int u = 0; u < 4; u++)
        for (int v = 0; v < 4; v++) {
            bumpPatch[u][v][0] = -0.9f + 0.6f * u;
            bumpPatch[u][v][1] = -0.9f + 0.6f * v;
            bumpPatch[u][v][2] = ((u == 1 || u == 2) && (v == 1 || v == 2)) ? 1.2f : 0.0f;
        }
}

void beginNurbsCell(int column, int row, float angle) {
    beginCell(column, row, 1.5);
    glRotatef(angle, 0, 1, 0);
    glRotatef(-90, 1, 0, 0);
    glEnable(GL_DEPTH_TEST);
}

void patchSurface(GLUnurbs* nurbs) {
    gluNurbsSurface(nurbs, 8, patchKnots, 8, patchKnots, 4 * 3, 3, &bumpPatch[0][0][0], 4, 4, GL_MAP2_VERTEX_3);
}

void drawNurbsRendererPage(const Renderers& r, float angle, GLuint checker) {
    // 1: cubic B-spline curve (same as page 2, now through glMap1/glEvalMesh1)
    static GLfloat curve[6][3] = {{-0.9f, -0.8f, 0}, {-0.6f, 0.8f, 0}, {-0.1f, -0.9f, 0},
                                  {0.2f, 0.9f, 0}, {0.6f, -0.6f, 0}, {0.9f, 0.7f, 0}};
    static GLfloat curveKnots[10] = {0, 0, 0, 0, 1, 2, 3, 3, 3, 3};
    beginFlatCell(0, 0);
    glColor3f(0.5f, 0.5f, 0.5f);
    glBegin(GL_LINE_STRIP);
    for (const auto& p : curve) glVertex3fv(p);
    glEnd();
    glColor3f(1.0f, 0.4f, 0.8f);
    glLineWidth(2.0f);
    gluBeginCurve(r.fill);
    gluNurbsCurve(r.fill, 10, curveKnots, 3, &curve[0][0], 4, GL_MAP1_VERTEX_3);
    gluEndCurve(r.fill);
    glLineWidth(1.0f);

    // 2-4: the bump patch filled, as tessellation outline, as patch outline
    GLUnurbs* modes[3] = {r.fill, r.outlinePolygon, r.outlinePatch};
    for (int i = 0; i < 3; i++) {
        beginNurbsCell(1 + i, 0, angle);
        glColor3f(0.5f, 0.8f, 1.0f);
        gluBeginSurface(modes[i]);
        patchSurface(modes[i]);
        gluEndSurface(modes[i]);
        glDisable(GL_DEPTH_TEST);
    }

    // 5: trimmed by a square hole (piecewise linear trim: outer boundary counter-clockwise, hole clockwise)
    static GLfloat outer[5][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}};
    static GLfloat square[5][2] = {{0.3f, 0.3f}, {0.3f, 0.7f}, {0.7f, 0.7f}, {0.7f, 0.3f}, {0.3f, 0.3f}};
    beginNurbsCell(0, 1, angle);
    glColor3f(1.0f, 0.6f, 0.2f);
    gluBeginSurface(r.fill);
    patchSurface(r.fill);
    gluBeginTrim(r.fill); gluPwlCurve(r.fill, 5, &outer[0][0], 2, GLU_MAP1_TRIM_2); gluEndTrim(r.fill);
    gluBeginTrim(r.fill); gluPwlCurve(r.fill, 5, &square[0][0], 2, GLU_MAP1_TRIM_2); gluEndTrim(r.fill);
    gluEndSurface(r.fill);
    glDisable(GL_DEPTH_TEST);

    // 6: trimmed by a round hole: a rational quadratic NURBS circle of radius 0.25 (clockwise)
    static GLfloat circle[9][3];
    static GLfloat circleKnots[12] = {0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4};
    {
        const float w = 0.70710678f;
        static const float corner[9][2] = {{1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}};
        for (int i = 0; i < 9; i++) {
            const float wi = (i % 2) ? w : 1.0f;
            circle[i][0] = (0.5f + 0.25f * corner[i][0]) * wi;
            circle[i][1] = (0.5f + 0.25f * corner[i][1]) * wi;
            circle[i][2] = wi;
        }
    }
    beginNurbsCell(1, 1, angle);
    glColor3f(0.3f, 0.9f, 0.4f);
    gluBeginSurface(r.fill);
    patchSurface(r.fill);
    gluBeginTrim(r.fill); gluPwlCurve(r.fill, 5, &outer[0][0], 2, GLU_MAP1_TRIM_2); gluEndTrim(r.fill);
    gluBeginTrim(r.fill); gluNurbsCurve(r.fill, 12, circleKnots, 3, &circle[0][0], 3, GLU_MAP1_TRIM_3); gluEndTrim(r.fill);
    gluEndSurface(r.fill);
    glDisable(GL_DEPTH_TEST);

    // 7: texture coordinates from a second NURBS surface (GL_MAP2_TEXTURE_COORD_2): checkered patch
    static GLfloat texPoints[2][2][2] = {{{0, 0}, {0, 2}}, {{2, 0}, {2, 2}}};
    static GLfloat linearKnots[4] = {0, 0, 1, 1};
    beginNurbsCell(2, 1, angle);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, checker);
    glColor3f(1, 1, 1);
    gluBeginSurface(r.fill);
    patchSurface(r.fill);
    gluNurbsSurface(r.fill, 4, linearKnots, 4, linearKnots, 2 * 2, 2, &texPoints[0][0][0], 2, 2, GL_MAP2_TEXTURE_COORD_2);
    gluEndSurface(r.fill);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);

    // 8: colors from a GL_MAP2_COLOR_4 surface: red, green, blue, yellow corners
    static GLfloat colorPoints[2][2][4] = {{{1, 0.2f, 0.2f, 1}, {0.2f, 1, 0.2f, 1}}, {{0.2f, 0.4f, 1, 1}, {1, 1, 0.2f, 1}}};
    beginNurbsCell(3, 1, angle);
    gluBeginSurface(r.fill);
    patchSurface(r.fill);
    gluNurbsSurface(r.fill, 4, linearKnots, 4, linearKnots, 2 * 4, 4, &colorPoints[0][0][0], 2, 2, GL_MAP2_COLOR_4);
    gluEndSurface(r.fill);
    glDisable(GL_DEPTH_TEST);
}

void printPage(int page) {
    consoleClear();
    std::printf("c3dgl glu test, page %i/3\n\n\n\n\n%i/%i checks passed\n\n", page + 1, checks - failures, checks);
    if (page == 0) {
        std::printf("Expected on the top screen,\n"
                    "left to right, top row:\n"
                    "- wireframe globe, turning\n"
                    "- solid checkered ball, turning\n"
                    "- OUTSIDE cone: solid shell, inside\n"
                    "  (through the top) wireframe\n"
                    "- INSIDE cone: solid inside, its\n"
                    "  outer wall wireframe (visible\n"
                    "  at the bottom)\n"
                    "bottom row:\n"
                    "- checkered ring (disk with hole)\n"
                    "- 3/4 pie, top-left quarter open\n"
                    "- outline of a 3/4 ring\n"
                    "- crosshair stays on the moving\n"
                    "  ball (gluProject)\n");
    } else if (page == 2) {
        std::printf("NURBS through GL evaluators.\n"
                    "Expected, top row:\n"
                    "- pink curve along its gray\n"
                    "  control polygon\n"
                    "- bump patch: filled, its\n"
                    "  triangles, its outline\n"
                    "  (all turning)\n"
                    "bottom row (bump patch):\n"
                    "- orange, square hole\n"
                    "- green, round hole\n"
                    "- checkered (texcoord surface)\n"
                    "- red/green/blue/yellow corners\n"
                    "  (color surface)\n");
    } else {
        std::printf("Expected on the top screen,\n"
                    "left to right, top row (tess):\n"
                    "- blue arrow pointing right\n"
                    "- orange square with square hole\n"
                    "- star, ODD: center pentagon open\n"
                    "- star, NONZERO: center filled\n"
                    "bottom row:\n"
                    "- outlines of square and hole\n"
                    "- union of 2 squares (wireframe)\n"
                    "- NURBS curve (pink) along its\n"
                    "  gray control polygon\n"
                    "- NURBS patch with a bump\n"
                    "  (wireframe), turning\n");
    }
    std::printf("\nA: next page   START: exit\n");
}

} // namespace

static void printStats() {
    // Rows 2-5 of the console: timings of the last frame, frames per second over the last second; the cursor stays
    // where the text ended
    std::printf("\x1b[s");
    std::printf("\x1b[2;1HCPU:     %6.2fms\x1b[K", C3D_GetProcessingTime());
    std::printf("\x1b[3;1HGPU:     %6.2fms\x1b[K", C3D_GetDrawingTime());
    std::printf("\x1b[4;1HCmdBuf:  %6.2f%%\x1b[K", C3D_GetCmdBufUsage()*100.0f);
    static u64 fpsStart;
    static int fpsFrames;
    static float fps;
    const u64 now = osGetTime();
    if (fpsStart == 0) fpsStart = now;
    fpsFrames++;
    if (now - fpsStart >= 1000) {
        fps = fpsFrames*1000.0f/(now - fpsStart);
        fpsFrames = 0;
        fpsStart = now;
    }
    std::printf("\x1b[5;1HFPS:     %6.2f\x1b[K", fps);
    std::printf("\x1b[u");
}

int main() {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, nullptr);

    if (!c3dglInit()) {
        std::printf("c3dglInit failed\n");
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            gspWaitForVBlank();
        }
        gfxExit();
        return 1;
    }

    selfChecks();
    const GLuint checker = createChecker();
    GLUquadric* quad = gluNewQuadric();
    GLUtesselator* tess = newTess();
    GLUnurbs* nurbs = newNurbs();
    initBumpPatch();
    const Renderers renderers = {newRenderer(GLU_FILL), newRenderer(GLU_OUTLINE_POLYGON), newRenderer(GLU_OUTLINE_PATCH)};
    int page = 0;
    printPage(page);

    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
    float angle = 0.0f;

    while (aptMainLoop()) {
        hidScanInput();
        printStats();
        const u32 keys = hidKeysDown();
        if (keys & KEY_START) break;
        if (keys & KEY_A) {
            page = (page + 1) % 3;
            printPage(page);
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (page == 2) {
            drawNurbsRendererPage(renderers, angle, checker);
            c3dglSwapBuffers();
            angle += 1.0f;
            continue;
        }
        if (page == 1) {
            drawTessellation(tess);
            drawNurbs(nurbs, angle);
            c3dglSwapBuffers();
            angle += 1.0f;
            continue;
        }
        glEnable(GL_DEPTH_TEST);

        // Wireframe globe: z axis up
        beginCell(0, 0);
        glRotatef(angle, 0, 1, 0);
        glRotatef(-90, 1, 0, 0);
        glColor3f(0.4f, 0.8f, 1.0f);
        gluQuadricDrawStyle(quad, GLU_LINE);
        gluSphere(quad, 0.9, 16, 10);

        // Textured ball, back faces culled
        beginCell(1, 0);
        glRotatef(angle, 0, 1, 0);
        glRotatef(-90, 1, 0, 0);
        glEnable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, checker);
        glColor3f(1, 1, 1);
        gluQuadricDrawStyle(quad, GLU_FILL);
        gluQuadricTexture(quad, GL_TRUE);
        gluSphere(quad, 0.9, 24, 16);
        gluQuadricTexture(quad, GL_FALSE);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_CULL_FACE);

        // Cones (open both ends), seen from above, front filled / back wireframe
        for (int inside = 0; inside < 2; inside++) {
            beginCell(2 + inside, 0, 2.0);
            glRotatef(angle, 0, 1, 0);
            glRotatef(-90, 1, 0, 0);
            glTranslatef(0, 0, -0.6f);
            frontFillBackLine();
            glColor3f(1.0f, 0.6f, 0.2f);
            gluQuadricOrientation(quad, inside ? GLU_INSIDE : GLU_OUTSIDE);
            gluCylinder(quad, 0.4, 0.8, 1.2, 20, 4);
            gluQuadricOrientation(quad, GLU_OUTSIDE);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        // Disk with a hole, facing the camera (+z), back faces culled
        beginCell(0, 1, 0.0);
        glRotatef(angle * 0.5f, 0, 0, 1);
        glEnable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, checker);
        glColor3f(1, 1, 1);
        gluQuadricTexture(quad, GL_TRUE);
        gluDisk(quad, 0.35, 0.9, 24, 3);
        gluQuadricTexture(quad, GL_FALSE);
        glDisable(GL_TEXTURE_2D);

        // 3/4 pie: from +y clockwise over +x and -y to -x, the top-left quarter stays open
        beginCell(1, 1, 0.0);
        glColor3f(0.3f, 0.9f, 0.4f);
        gluPartialDisk(quad, 0.0, 0.9, 24, 2, 0.0, 270.0);
        glDisable(GL_CULL_FACE);

        // Silhouette: two arcs and the two radial edges of a 3/4 ring
        beginCell(2, 1, 0.0);
        glColor3f(1, 1, 1);
        glLineWidth(2.0f);
        gluQuadricDrawStyle(quad, GLU_SILHOUETTE);
        gluPartialDisk(quad, 0.4, 0.9, 24, 3, 0.0, 270.0);
        gluQuadricDrawStyle(quad, GLU_FILL);
        glLineWidth(1.0f);

        // gluProject: ball on a circle, crosshair drawn in window coordinates at its projected center
        beginCell(3, 1);
        const float bx = 0.7f * std::cos(angle * 0.05f), bz = 0.7f * std::sin(angle * 0.05f);
        glTranslatef(bx, 0, bz);
        glColor3f(0.9f, 0.3f, 0.3f);
        gluSphere(quad, 0.2, 12, 8);
        GLdouble model[16], proj[16], wx, wy, wz;
        GLint view[4];
        glGetDoublev(GL_MODELVIEW_MATRIX, model);
        glGetDoublev(GL_PROJECTION_MATRIX, proj);
        glGetIntegerv(GL_VIEWPORT, view);
        gluProject(0, 0, 0, model, proj, view, &wx, &wy, &wz);

        glDisable(GL_DEPTH_TEST);
        glViewport(0, 0, C3DGL_TOP_SCREEN_WIDTH, C3DGL_SCREEN_HEIGHT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluOrtho2D(0, C3DGL_TOP_SCREEN_WIDTH, 0, C3DGL_SCREEN_HEIGHT);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glColor3f(1, 1, 1);
        glBegin(GL_LINES);
        glVertex2d(wx - 8, wy); glVertex2d(wx + 8, wy);
        glVertex2d(wx, wy - 8); glVertex2d(wx, wy + 8);
        glEnd();

        c3dglSwapBuffers();
        angle += 1.0f;
    }

    gluDeleteNurbsRenderer(nurbs);
    gluDeleteTess(tess);
    gluDeleteQuadric(quad);
    glDeleteTextures(1, &checker);
    c3dglClose();
    gfxExit();
    return 0;
}
