// c3dgl example: lighting. The top screen shows one lit scene per cell (4x2 grid, 100x120 px per cell), the bottom
// screen the result of the non-visual checks (glGetLight, glGetMaterial, errors, color material, attribute stack)
// and what each cell must look like.
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr int COLUMNS = 4, ROWS = 2;
constexpr int CELL_W = C3DGL_TOP_SCREEN_WIDTH / COLUMNS, CELL_H = C3DGL_SCREEN_HEIGHT / ROWS;

int checks, failures;

#define CHECK(cond) check((cond), #cond, __LINE__)

void check(bool ok, const char* what, int line) {
    checks++;
    if (ok) return;
    failures++;
    std::printf("FAIL %i: %s\n", line, what);
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

bool near4(const GLfloat* v, float a, float b, float c, float d) {
    return near(v[0], a) && near(v[1], b) && near(v[2], c) && near(v[3], d);
}

//----------------------------------------------------------------------------------
// Geometry
//----------------------------------------------------------------------------------
// Unit sphere for glDrawElements: positions = normals, colors from red (top) to blue (bottom)
struct Sphere {
    std::vector<GLfloat> positions;
    std::vector<GLubyte> colors;
    std::vector<GLushort> indices;
};

Sphere createSphere(int slices, int stacks) {
    Sphere s;
    for (int i = 0; i <= stacks; i++) {
        const float phi = static_cast<float>(M_PI) * i / stacks;
        for (int j = 0; j <= slices; j++) {
            const float theta = 2.0f * static_cast<float>(M_PI) * j / slices;
            s.positions.push_back(std::sin(phi) * std::sin(theta));
            s.positions.push_back(std::cos(phi));
            s.positions.push_back(std::sin(phi) * std::cos(theta));
            const GLubyte t = static_cast<GLubyte>(255 * i / stacks);
            s.colors.insert(s.colors.end(), {static_cast<GLubyte>(255 - t), 40, t, 255});
        }
    }
    for (int i = 0; i < stacks; i++) {
        for (int j = 0; j < slices; j++) {
            const GLushort a = i * (slices + 1) + j, b = a + slices + 1;
            s.indices.insert(s.indices.end(), {a, b, static_cast<GLushort>(a + 1), static_cast<GLushort>(a + 1), b,
                                               static_cast<GLushort>(b + 1)});
        }
    }
    return s;
}

// Through vertex arrays: GL_NORMAL_ARRAY (+ GL_COLOR_ARRAY with colors)
void drawSphereArrays(const Sphere& s, bool colors) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, s.positions.data());
    glNormalPointer(GL_FLOAT, 0, s.positions.data());
    if (colors) {
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, s.colors.data());
    }
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(s.indices.size()), GL_UNSIGNED_SHORT, s.indices.data());
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

// Through immediate mode (glNormal3fv + glVertex3fv per vertex)
void drawSphereImmediate(const Sphere& s) {
    glBegin(GL_TRIANGLES);
    for (GLushort index : s.indices) {
        const GLfloat* p = &s.positions[index * 3];
        glNormal3fv(p);
        glVertex3fv(p);
    }
    glEnd();
}

// n x n quads in the z = 0 plane from (-size, -size) to (size, size), normal +z. Lighting is per vertex, so
// local light effects need the tessellation
void drawGrid(float size, int n) {
    glNormal3f(0.0f, 0.0f, 1.0f);
    for (int y = 0; y < n; y++) {
        glBegin(GL_QUAD_STRIP);
        for (int x = 0; x <= n; x++) {
            const float px = -size + 2.0f * size * x / n;
            glVertex2f(px, -size + 2.0f * size * (y + 1) / n);
            glVertex2f(px, -size + 2.0f * size * y / n);
        }
        glEnd();
    }
}

//----------------------------------------------------------------------------------
// Cells
//----------------------------------------------------------------------------------
// Cell (column, row from the top) as viewport; x -1..1, y -1.2..1.2 (square pixels), z = 2 is nearest.
// The lighting state of every cell is undone by the attribute stack (endCell)
void beginCell(int column, int row) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.2, 1.2, -2.0, 2.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glPushAttrib(GL_LIGHTING_BIT | GL_ENABLE_BIT | GL_TRANSFORM_BIT | GL_CURRENT_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

void endCell() { glPopAttrib(); }

void setLight(GLenum light, GLenum pname, float a, float b, float c, float d) {
    const GLfloat v[4] = {a, b, c, d};
    glLightfv(light, pname, v);
}

void setMaterial(GLenum pname, float a, float b, float c, float d) {
    const GLfloat v[4] = {a, b, c, d};
    glMaterialfv(GL_FRONT_AND_BACK, pname, v);
}

// 1: red diffuse sphere, directional light from the upper left
void drawDiffuse(const Sphere& s) {
    glEnable(GL_LIGHT0);
    setLight(GL_LIGHT0, GL_POSITION, -1.0f, 1.0f, 1.0f, 0.0f);
    setMaterial(GL_DIFFUSE, 0.9f, 0.15f, 0.1f, 1.0f);
    glScalef(0.8f, 0.8f, 0.8f);
    glEnable(GL_NORMALIZE);
    drawSphereArrays(s, false);
}

// 2: blue sphere with a white specular highlight in the upper left
void drawSpecular(const Sphere& s) {
    glEnable(GL_LIGHT0);
    setLight(GL_LIGHT0, GL_POSITION, -1.0f, 1.0f, 1.0f, 0.0f);
    setMaterial(GL_DIFFUSE, 0.1f, 0.25f, 0.8f, 1.0f);
    setMaterial(GL_SPECULAR, 1.0f, 1.0f, 1.0f, 1.0f);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 40.0f);
    glScalef(0.8f, 0.8f, 0.8f);
    glEnable(GL_NORMALIZE);
    drawSphereImmediate(s);
}

// 3: positional light just above the center of a flat grid, quadratic attenuation: bright center, dark corners
void drawPointLight() {
    glEnable(GL_LIGHT0);
    setLight(GL_LIGHT0, GL_POSITION, 0.0f, 0.0f, 0.4f, 1.0f);
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 0.0f);
    glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 6.0f);
    setMaterial(GL_DIFFUSE, 1.0f, 0.85f, 0.4f, 1.0f);
    drawGrid(0.9f, 24);
}

// 4: spot light from above pointing down at a grid: a lit disc (radius ~0.45), dark outside
void drawSpotLight() {
    glEnable(GL_LIGHT0);
    setLight(GL_LIGHT0, GL_POSITION, 0.0f, 0.0f, 1.0f, 1.0f);
    const GLfloat down[3] = {0.0f, 0.0f, -1.0f};
    glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, down);
    glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 25.0f);
    glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 4.0f);
    setMaterial(GL_DIFFUSE, 0.3f, 1.0f, 0.4f, 1.0f);
    drawGrid(0.9f, 32);
}

// 5: white sphere, red light from the left, green from the right, blue from the top
void drawThreeLights(const Sphere& s) {
    const GLenum lights[3] = {GL_LIGHT0, GL_LIGHT1, GL_LIGHT2};
    const float dirs[3][2] = {{-1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    for (int i = 0; i < 3; i++) {
        glEnable(lights[i]);
        setLight(lights[i], GL_POSITION, dirs[i][0], dirs[i][1], 0.4f, 0.0f);
        setLight(lights[i], GL_DIFFUSE, i == 0, i == 1, i == 2, 1.0f);
        setLight(lights[i], GL_SPECULAR, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    setMaterial(GL_DIFFUSE, 1.0f, 1.0f, 1.0f, 1.0f);
    glScalef(0.8f, 0.8f, 0.8f);
    glEnable(GL_NORMALIZE);
    drawSphereArrays(s, false);
}

// 6: GL_COLOR_MATERIAL with a color array: the sphere is red at the top and blue at the bottom, lit from the
// upper left, with a white highlight
void drawColorMaterial(const Sphere& s) {
    glEnable(GL_LIGHT0);
    setLight(GL_LIGHT0, GL_POSITION, -1.0f, 1.0f, 1.0f, 0.0f);
    setMaterial(GL_SPECULAR, 0.6f, 0.6f, 0.6f, 1.0f);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 20.0f);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);
    glScalef(0.8f, 0.8f, 0.8f);
    glEnable(GL_NORMALIZE);
    drawSphereArrays(s, true);
}

// Quad from (x0, y0) to (x1, y1) with normal (0, 0, nz), counter-clockwise (front facing) or clockwise
void drawQuad(float x0, float y0, float x1, float y1, float nz, bool ccw) {
    glNormal3f(0.0f, 0.0f, nz);
    glBegin(GL_QUADS);
    if (ccw) {
        glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
    } else {
        glVertex2f(x0, y0); glVertex2f(x0, y1); glVertex2f(x1, y1); glVertex2f(x1, y0);
    }
    glEnd();
}

// 7: two-sided lighting, light from the viewer. Left quads face the viewer (front material: green), right quads
// show their back (normal away from the viewer, back material: magenta). Top: two-sided, so the back is lit
// with the reversed normal (magenta); bottom: one-sided, the back stays dark
void drawTwoSided() {
    glEnable(GL_LIGHT0);
    const GLfloat green[4] = {0.2f, 0.9f, 0.2f, 1.0f}, magenta[4] = {0.9f, 0.2f, 0.9f, 1.0f};
    glMaterialfv(GL_FRONT, GL_DIFFUSE, green);
    glMaterialfv(GL_BACK, GL_DIFFUSE, magenta);

    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    drawQuad(-0.85f, 0.15f, -0.05f, 1.0f, 1.0f, true);
    drawQuad(0.05f, 0.15f, 0.85f, 1.0f, -1.0f, false);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);
    drawQuad(-0.85f, -1.0f, -0.05f, -0.15f, 1.0f, true);
    drawQuad(0.05f, -1.0f, 0.85f, -0.15f, -1.0f, false);
}

// 8: flat shading of lit colors on small (glScale 0.4) spheres: top left GL_NORMALIZE and top right
// GL_RESCALE_NORMAL must look the same (faceted, orange); bottom: neither, the scaled normals are 2.5 times too
// long and the sphere is washed out
void drawFlatScaled(const Sphere& s) {
    glEnable(GL_LIGHT0);
    setLight(GL_LIGHT0, GL_POSITION, -1.0f, 1.0f, 1.0f, 0.0f);
    setMaterial(GL_DIFFUSE, 0.9f, 0.5f, 0.1f, 1.0f);
    glShadeModel(GL_FLAT);

    const float centers[3][2] = {{-0.48f, 0.55f}, {0.48f, 0.55f}, {0.0f, -0.6f}};
    for (int i = 0; i < 3; i++) {
        glDisable(GL_NORMALIZE);
        glDisable(GL_RESCALE_NORMAL);
        if (i == 0) glEnable(GL_NORMALIZE);
        if (i == 1) glEnable(GL_RESCALE_NORMAL);
        glPushMatrix();
        glTranslatef(centers[i][0], centers[i][1], 0.0f);
        glScalef(0.4f, 0.4f, 0.4f);
        drawSphereImmediate(s);
        glPopMatrix();
    }
    glShadeModel(GL_SMOOTH);
}

//----------------------------------------------------------------------------------
// Non-visual checks
//----------------------------------------------------------------------------------
void testDefaults() {
    GLfloat f[4];
    GLint v = 0;

    CHECK(!glIsEnabled(GL_LIGHTING) && !glIsEnabled(GL_LIGHT0) && !glIsEnabled(GL_COLOR_MATERIAL));
    CHECK(!glIsEnabled(GL_NORMALIZE) && !glIsEnabled(GL_RESCALE_NORMAL));
    glGetIntegerv(GL_MAX_LIGHTS, &v);
    CHECK(v == 8);

    glGetLightfv(GL_LIGHT0, GL_DIFFUSE, f);
    CHECK(near4(f, 1, 1, 1, 1));
    glGetLightfv(GL_LIGHT1, GL_DIFFUSE, f);
    CHECK(near4(f, 0, 0, 0, 1));
    glGetLightfv(GL_LIGHT1, GL_SPECULAR, f);
    CHECK(near4(f, 0, 0, 0, 1));
    glGetLightfv(GL_LIGHT7, GL_AMBIENT, f);
    CHECK(near4(f, 0, 0, 0, 1));
    glGetLightfv(GL_LIGHT0, GL_POSITION, f);
    CHECK(near4(f, 0, 0, 1, 0));
    glGetLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, f);
    CHECK(near(f[0], 0) && near(f[1], 0) && near(f[2], -1));
    glGetLightfv(GL_LIGHT0, GL_SPOT_CUTOFF, f);
    CHECK(near(f[0], 180));
    glGetLightfv(GL_LIGHT0, GL_CONSTANT_ATTENUATION, f);
    CHECK(near(f[0], 1));

    glGetMaterialfv(GL_FRONT, GL_AMBIENT, f);
    CHECK(near4(f, 0.2f, 0.2f, 0.2f, 1));
    glGetMaterialfv(GL_BACK, GL_DIFFUSE, f);
    CHECK(near4(f, 0.8f, 0.8f, 0.8f, 1));
    glGetMaterialfv(GL_FRONT, GL_EMISSION, f);
    CHECK(near4(f, 0, 0, 0, 1));
    glGetMaterialfv(GL_FRONT, GL_SHININESS, f);
    CHECK(near(f[0], 0));

    glGetFloatv(GL_LIGHT_MODEL_AMBIENT, f);
    CHECK(near4(f, 0.2f, 0.2f, 0.2f, 1));
    GLboolean b = GL_TRUE;
    glGetBooleanv(GL_LIGHT_MODEL_TWO_SIDE, &b);
    CHECK(b == GL_FALSE);
    glGetIntegerv(GL_COLOR_MATERIAL_FACE, &v);
    CHECK(v == GL_FRONT_AND_BACK);
    glGetIntegerv(GL_COLOR_MATERIAL_PARAMETER, &v);
    CHECK(v == GL_AMBIENT_AND_DIFFUSE);
    CHECK(glGetError() == GL_NO_ERROR);
}

void testParameters() {
    GLfloat f[4];
    GLint iv[4];

    // Position and spot direction are stored in eye coordinates
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glTranslatef(1.0f, 2.0f, 3.0f);
    glRotatef(90.0f, 0.0f, 0.0f, 1.0f);         // x -> y
    setLight(GL_LIGHT3, GL_POSITION, 1.0f, 0.0f, 0.0f, 1.0f);
    glGetLightfv(GL_LIGHT3, GL_POSITION, f);
    CHECK(near4(f, 1, 3, 3, 1));
    setLight(GL_LIGHT3, GL_POSITION, 1.0f, 0.0f, 0.0f, 0.0f);    // Directional: no translation
    glGetLightfv(GL_LIGHT3, GL_POSITION, f);
    CHECK(near4(f, 0, 1, 0, 0));
    const GLfloat dir[3] = {1.0f, 0.0f, 0.0f};
    glLightfv(GL_LIGHT3, GL_SPOT_DIRECTION, dir);
    glGetLightfv(GL_LIGHT3, GL_SPOT_DIRECTION, f);
    CHECK(near(f[0], 0) && near(f[1], 1) && near(f[2], 0));
    glPopMatrix();

    // Integer variants: colors map like glColor*i, positions do not
    const GLint white[4] = {2147483647, 0, 2147483647, 2147483647};
    glLightiv(GL_LIGHT3, GL_DIFFUSE, white);
    glGetLightfv(GL_LIGHT3, GL_DIFFUSE, f);
    CHECK(near(f[0], 1.0f) && near(f[1], 0.0f) && near(f[3], 1.0f));
    glGetLightiv(GL_LIGHT3, GL_DIFFUSE, iv);
    CHECK(iv[0] == 2147483647 && iv[1] == 0);
    const GLint pos[4] = {2, 3, 4, 1};
    glLoadIdentity();
    glLightiv(GL_LIGHT3, GL_POSITION, pos);
    glGetLightiv(GL_LIGHT3, GL_POSITION, iv);
    CHECK(iv[0] == 2 && iv[1] == 3 && iv[2] == 4 && iv[3] == 1);
    glLighti(GL_LIGHT3, GL_SPOT_CUTOFF, 45);
    glGetLightiv(GL_LIGHT3, GL_SPOT_CUTOFF, iv);
    CHECK(iv[0] == 45);

    // Materials per face
    const GLfloat red[4] = {1, 0, 0, 1};
    glMaterialfv(GL_BACK, GL_EMISSION, red);
    glGetMaterialfv(GL_BACK, GL_EMISSION, f);
    CHECK(near4(f, 1, 0, 0, 1));
    glGetMaterialfv(GL_FRONT, GL_EMISSION, f);
    CHECK(near4(f, 0, 0, 0, 1));
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, red);
    glGetMaterialfv(GL_FRONT, GL_AMBIENT, f);
    CHECK(near4(f, 1, 0, 0, 1));
    glGetMaterialfv(GL_BACK, GL_DIFFUSE, f);
    CHECK(near4(f, 1, 0, 0, 1));
    glMateriali(GL_FRONT, GL_SHININESS, 64);
    glGetMaterialiv(GL_FRONT, GL_SHININESS, iv);
    CHECK(iv[0] == 64);

    // ES fixed point
    glLightx(GL_LIGHT4, GL_LINEAR_ATTENUATION, 0x8000);
    glGetLightfv(GL_LIGHT4, GL_LINEAR_ATTENUATION, f);
    CHECK(near(f[0], 0.5f));
    const GLfixed half[4] = {0x8000, 0x8000, 0x8000, 0x10000};
    glLightModelxv(GL_LIGHT_MODEL_AMBIENT, half);
    GLfixed x[4];
    glGetFixedv(GL_LIGHT_MODEL_AMBIENT, x);
    CHECK(x[0] == 0x8000 && x[3] == 0x10000);
    glMaterialxv(GL_FRONT_AND_BACK, GL_SPECULAR, half);
    glGetMaterialxv(GL_BACK, GL_SPECULAR, x);
    CHECK(x[1] == 0x8000);
    glLightModelx(GL_LIGHT_MODEL_TWO_SIDE, 1);
    GLboolean b = GL_FALSE;
    glGetBooleanv(GL_LIGHT_MODEL_TWO_SIDE, &b);
    CHECK(b == GL_TRUE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 0);
    CHECK(glGetError() == GL_NO_ERROR);
}

void testErrors() {
    GLfloat f[4];
    glLightf(GL_LIGHT0 + 8, GL_SPOT_EXPONENT, 1.0f);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glLightf(GL_LIGHT0, GL_POSITION, 1.0f);              // Vector parameter through the scalar call
    CHECK(glGetError() == GL_INVALID_ENUM);
    glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 100.0f);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 129.0f);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, -1.0f);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glMaterialf(GL_FRONT, GL_SHININESS, 200.0f);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glMaterialf(GL_FRONT, GL_DIFFUSE, 1.0f);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glGetMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, f);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glColorMaterial(GL_FRONT, GL_SHININESS);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glLightModelf(GL_LIGHT_MODEL_AMBIENT, 1.0f);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glGetLightfv(GL_LIGHT0, GL_SHININESS, f);
    CHECK(glGetError() == GL_INVALID_ENUM);
}

void testColorMaterial() {
    GLfloat f[4];

    // The tracked properties follow the current color while enabled, and keep the last one afterwards
    glColorMaterial(GL_FRONT, GL_EMISSION);
    glColor4f(0.0f, 1.0f, 0.0f, 1.0f);
    glEnable(GL_COLOR_MATERIAL);
    glGetMaterialfv(GL_FRONT, GL_EMISSION, f);
    CHECK(near4(f, 0, 1, 0, 1));
    glColor4f(0.0f, 0.0f, 1.0f, 1.0f);
    glGetMaterialfv(GL_FRONT, GL_EMISSION, f);
    CHECK(near4(f, 0, 0, 1, 1));
    glGetMaterialfv(GL_BACK, GL_EMISSION, f);
    CHECK(!near4(f, 0, 0, 1, 1));               // Back not tracked
    glDisable(GL_COLOR_MATERIAL);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glGetMaterialfv(GL_FRONT, GL_EMISSION, f);
    CHECK(near4(f, 0, 0, 1, 1));
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    CHECK(glGetError() == GL_NO_ERROR);
}

void testAttribStack() {
    GLfloat f[4];

    // LIGHTING_BIT: enables, light and material parameters; TRANSFORM_BIT: normalization
    glPushAttrib(GL_LIGHTING_BIT | GL_TRANSFORM_BIT);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT5);
    glEnable(GL_NORMALIZE);
    glEnable(GL_RESCALE_NORMAL);
    setLight(GL_LIGHT5, GL_SPECULAR, 0.5f, 0.5f, 0.5f, 1.0f);
    glMaterialf(GL_BACK, GL_SHININESS, 7.0f);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);
    glPopAttrib();
    CHECK(!glIsEnabled(GL_LIGHTING) && !glIsEnabled(GL_LIGHT5));
    CHECK(!glIsEnabled(GL_NORMALIZE) && !glIsEnabled(GL_RESCALE_NORMAL));
    glGetLightfv(GL_LIGHT5, GL_SPECULAR, f);
    CHECK(near4(f, 0, 0, 0, 1));
    glGetMaterialfv(GL_BACK, GL_SHININESS, f);
    CHECK(near(f[0], 0));
    GLboolean b = GL_TRUE;
    glGetBooleanv(GL_LIGHT_MODEL_LOCAL_VIEWER, &b);
    CHECK(b == GL_FALSE);

    // ENABLE_BIT restores the enables only
    glPushAttrib(GL_ENABLE_BIT);
    glEnable(GL_LIGHT6);
    glEnable(GL_COLOR_MATERIAL);
    setLight(GL_LIGHT6, GL_AMBIENT, 0.25f, 0.0f, 0.0f, 1.0f);
    glPopAttrib();
    CHECK(!glIsEnabled(GL_LIGHT6) && !glIsEnabled(GL_COLOR_MATERIAL));
    glGetLightfv(GL_LIGHT6, GL_AMBIENT, f);
    CHECK(near4(f, 0.25f, 0, 0, 1));
    setLight(GL_LIGHT6, GL_AMBIENT, 0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(glGetError() == GL_NO_ERROR);
}

void printExpected() {
    std::printf("\n%i/%i checks passed\n\n"
                "Top screen, top row:\n"
                "- red sphere lit from upper left\n"
                "- blue sphere, white highlight\n"
                "  upper left\n"
                "- yellow glow fading from the\n"
                "  center (point light)\n"
                "- green spot light disc\n"
                "Bottom row:\n"
                "- white sphere: red left, green\n"
                "  right, blue top (mixes)\n"
                "- sphere red top -> blue bottom,\n"
                "  lit, white highlight\n"
                "- green | magenta on top,\n"
                "  green | black below\n"
                "- 2 identical faceted orange\n"
                "  spheres, a washed-out one below\n\n"
                "START: exit\n",
                checks - failures, checks);
}

} // namespace

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

    std::printf("c3dgl lighting\n");
    testDefaults();
    testParameters();
    testErrors();
    testColorMaterial();
    testAttribStack();

    // Back to the defaults the checks changed
    const GLfloat ambient[4] = {0.2f, 0.2f, 0.2f, 1.0f}, black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    const GLfloat diffuse[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuse);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, black);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, black);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 0.0f);
    CHECK(glGetError() == GL_NO_ERROR);
    printExpected();

    const Sphere sphere = createSphere(24, 16);
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        beginCell(0, 0); drawDiffuse(sphere); endCell();
        beginCell(1, 0); drawSpecular(sphere); endCell();
        beginCell(2, 0); drawPointLight(); endCell();
        beginCell(3, 0); drawSpotLight(); endCell();
        beginCell(0, 1); drawThreeLights(sphere); endCell();
        beginCell(1, 1); drawColorMaterial(sphere); endCell();
        beginCell(2, 1); drawTwoSided(); endCell();
        beginCell(3, 1); drawFlatScaled(sphere); endCell();
        c3dglSwapBuffers();
    }

    c3dglClose();
    gfxExit();
    return 0;
}
