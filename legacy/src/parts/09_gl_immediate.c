// OpenGL: immediate mode (glBegin/glEnd/glVertex...)
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// OpenGL: immediate mode
//----------------------------------------------------------------------------------
void glBegin(GLenum mode)
{
    LIST_SAVE(BEGIN, "u", mode);
    PROF_IMM_ENTER();
    gl.inBegin = beginPrimitive(mode);
}

void glEnd(void)
{
    LIST_SAVE(END, "");
    if (gl.inBegin) endPrimitive();
    gl.inBegin = false;
    PROF_IMM_LEAVE();
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    LIST_SAVE(VERTEX, "fff", x, y, z);
    if (!gl.inBegin) return;
    PROF_COUNT(PB_IMMEDIATE);

    Vertex v = gl.current;
    v.pos[0] = x;
    v.pos[1] = y;
    v.pos[2] = z;
    submitLitVertex(&v, gl.currentNormal, gl.currentEdge);
}

// Vertices are stored with w = 1: homogeneous positions are divided (exact unless w <= 0)
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    if (w == 0.0f) { WARN_ONCE("glVertex4: w = 0 (point at infinity) not supported\n"); return; }
    glVertex3f(x/w, y/w, z/w);
}

// The first texcoord with q != 1 switches texturing to projection mode (for good, it is exact with q = 1 too)
static void markTexQ(void)
{
    if (gl.texQUsed) return;
    gl.texQUsed = true;
    // The batch of the current primitive needs it already
    if (gl.inBegin && (gl.renderMode == GL_RENDER)) prepareDraw(gl.batch.clipSpace, gl.primitive == GL_POINTS);
}

void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    LIST_SAVE(TEX_COORD, "ffff", s, t, r, q);
    gl.current.tex[0] = s;
    gl.current.tex[1] = t;
    gl.current.tex[2] = q;
    gl.current.texR = r;
    gl.currentTexR[0] = r;
    if (q != 1.0f) markTexQ();
}

void glEdgeFlag(GLboolean flag)
{
    LIST_SAVE(EDGE_FLAG, "i", flag);
    gl.currentEdge = flag;
}

void glEdgeFlagv(const GLboolean *flag) { glEdgeFlag(*flag); }

void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
    LIST_SAVE(NORMAL, "fff", nx, ny, nz);
    gl.currentNormal[0] = nx;
    gl.currentNormal[1] = ny;
    gl.currentNormal[2] = nz;
}

void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    LIST_SAVE(COLOR, "iiii", red, green, blue, alpha);
    gl.current.color[0] = red;
    gl.current.color[1] = green;
    gl.current.color[2] = blue;
    gl.current.color[3] = alpha;
    if (gl.colorMaterial) applyColorMaterial(gl.current.color);
}

void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    glColor4ub(colorByte(red), colorByte(green), colorByte(blue), colorByte(alpha));
}


// glRect: counter-clockwise quad from (x1, y1) to (x2, y2) at z = 0
void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
    glBegin(GL_QUADS);
    glVertex2f(x1, y1);
    glVertex2f(x2, y1);
    glVertex2f(x2, y2);
    glVertex2f(x1, y2);
    glEnd();
}
// All other GL 1.1 variants of glVertex, glTexCoord, glNormal, glColor, glRect, generated: integer colors and
// normals are normalized ((2c + 1)/(2^b - 1) for signed types), vertices and texcoords are not
void glVertex2d(GLdouble x, GLdouble y) { glVertex3f((float)x, (float)y, 0.0f); }
void glVertex2dv(const GLdouble *v) { glVertex3f((float)v[0], (float)v[1], 0.0f); }
void glVertex2f(GLfloat x, GLfloat y) { glVertex3f(x, y, 0.0f); }
void glVertex2fv(const GLfloat *v) { glVertex3f(v[0], v[1], 0.0f); }
void glVertex2i(GLint x, GLint y) { glVertex3f((float)x, (float)y, 0.0f); }
void glVertex2iv(const GLint *v) { glVertex3f((float)v[0], (float)v[1], 0.0f); }
void glVertex2s(GLshort x, GLshort y) { glVertex3f(x, y, 0.0f); }
void glVertex2sv(const GLshort *v) { glVertex3f(v[0], v[1], 0.0f); }
void glVertex3d(GLdouble x, GLdouble y, GLdouble z) { glVertex3f((float)x, (float)y, (float)z); }
void glVertex3dv(const GLdouble *v) { glVertex3f((float)v[0], (float)v[1], (float)v[2]); }
void glVertex3fv(const GLfloat *v) { glVertex3f(v[0], v[1], v[2]); }
void glVertex3i(GLint x, GLint y, GLint z) { glVertex3f((float)x, (float)y, (float)z); }
void glVertex3iv(const GLint *v) { glVertex3f((float)v[0], (float)v[1], (float)v[2]); }
void glVertex3s(GLshort x, GLshort y, GLshort z) { glVertex3f(x, y, z); }
void glVertex3sv(const GLshort *v) { glVertex3f(v[0], v[1], v[2]); }
void glVertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w) { glVertex4f((float)x, (float)y, (float)z, (float)w); }
void glVertex4dv(const GLdouble *v) { glVertex4f((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void glVertex4fv(const GLfloat *v) { glVertex4f(v[0], v[1], v[2], v[3]); }
void glVertex4i(GLint x, GLint y, GLint z, GLint w) { glVertex4f((float)x, (float)y, (float)z, (float)w); }
void glVertex4iv(const GLint *v) { glVertex4f((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void glVertex4s(GLshort x, GLshort y, GLshort z, GLshort w) { glVertex4f(x, y, z, w); }
void glVertex4sv(const GLshort *v) { glVertex4f(v[0], v[1], v[2], v[3]); }
void glTexCoord1d(GLdouble s) { glTexCoord4f((float)s, 0.0f, 0.0f, 1.0f); }
void glTexCoord1dv(const GLdouble *v) { glTexCoord4f((float)v[0], 0.0f, 0.0f, 1.0f); }
void glTexCoord1f(GLfloat s) { glTexCoord4f(s, 0.0f, 0.0f, 1.0f); }
void glTexCoord1fv(const GLfloat *v) { glTexCoord4f(v[0], 0.0f, 0.0f, 1.0f); }
void glTexCoord1i(GLint s) { glTexCoord4f((float)s, 0.0f, 0.0f, 1.0f); }
void glTexCoord1iv(const GLint *v) { glTexCoord4f((float)v[0], 0.0f, 0.0f, 1.0f); }
void glTexCoord1s(GLshort s) { glTexCoord4f(s, 0.0f, 0.0f, 1.0f); }
void glTexCoord1sv(const GLshort *v) { glTexCoord4f(v[0], 0.0f, 0.0f, 1.0f); }
void glTexCoord2d(GLdouble s, GLdouble t) { glTexCoord4f((float)s, (float)t, 0.0f, 1.0f); }
void glTexCoord2dv(const GLdouble *v) { glTexCoord4f((float)v[0], (float)v[1], 0.0f, 1.0f); }
void glTexCoord2f(GLfloat s, GLfloat t) { glTexCoord4f(s, t, 0.0f, 1.0f); }
void glTexCoord2fv(const GLfloat *v) { glTexCoord4f(v[0], v[1], 0.0f, 1.0f); }
void glTexCoord2i(GLint s, GLint t) { glTexCoord4f((float)s, (float)t, 0.0f, 1.0f); }
void glTexCoord2iv(const GLint *v) { glTexCoord4f((float)v[0], (float)v[1], 0.0f, 1.0f); }
void glTexCoord2s(GLshort s, GLshort t) { glTexCoord4f(s, t, 0.0f, 1.0f); }
void glTexCoord2sv(const GLshort *v) { glTexCoord4f(v[0], v[1], 0.0f, 1.0f); }
void glTexCoord3d(GLdouble s, GLdouble t, GLdouble r) { glTexCoord4f((float)s, (float)t, (float)r, 1.0f); }
void glTexCoord3dv(const GLdouble *v) { glTexCoord4f((float)v[0], (float)v[1], (float)v[2], 1.0f); }
void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r) { glTexCoord4f(s, t, r, 1.0f); }
void glTexCoord3fv(const GLfloat *v) { glTexCoord4f(v[0], v[1], v[2], 1.0f); }
void glTexCoord3i(GLint s, GLint t, GLint r) { glTexCoord4f((float)s, (float)t, (float)r, 1.0f); }
void glTexCoord3iv(const GLint *v) { glTexCoord4f((float)v[0], (float)v[1], (float)v[2], 1.0f); }
void glTexCoord3s(GLshort s, GLshort t, GLshort r) { glTexCoord4f(s, t, r, 1.0f); }
void glTexCoord3sv(const GLshort *v) { glTexCoord4f(v[0], v[1], v[2], 1.0f); }
void glTexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q) { glTexCoord4f((float)s, (float)t, (float)r, (float)q); }
void glTexCoord4dv(const GLdouble *v) { glTexCoord4f((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void glTexCoord4fv(const GLfloat *v) { glTexCoord4f(v[0], v[1], v[2], v[3]); }
void glTexCoord4i(GLint s, GLint t, GLint r, GLint q) { glTexCoord4f((float)s, (float)t, (float)r, (float)q); }
void glTexCoord4iv(const GLint *v) { glTexCoord4f((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void glTexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q) { glTexCoord4f(s, t, r, q); }
void glTexCoord4sv(const GLshort *v) { glTexCoord4f(v[0], v[1], v[2], v[3]); }
void glNormal3b(GLbyte nx, GLbyte ny, GLbyte nz) { glNormal3f((2.0f*nx + 1.0f)/255.0f, (2.0f*ny + 1.0f)/255.0f, (2.0f*nz + 1.0f)/255.0f); }
void glNormal3bv(const GLbyte *v) { glNormal3f((2.0f*v[0] + 1.0f)/255.0f, (2.0f*v[1] + 1.0f)/255.0f, (2.0f*v[2] + 1.0f)/255.0f); }
void glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz) { glNormal3f((float)nx, (float)ny, (float)nz); }
void glNormal3dv(const GLdouble *v) { glNormal3f((float)v[0], (float)v[1], (float)v[2]); }
void glNormal3fv(const GLfloat *v) { glNormal3f(v[0], v[1], v[2]); }
void glNormal3i(GLint nx, GLint ny, GLint nz) { glNormal3f((float)((2.0*nx + 1.0)/4294967295.0), (float)((2.0*ny + 1.0)/4294967295.0), (float)((2.0*nz + 1.0)/4294967295.0)); }
void glNormal3iv(const GLint *v) { glNormal3f((float)((2.0*v[0] + 1.0)/4294967295.0), (float)((2.0*v[1] + 1.0)/4294967295.0), (float)((2.0*v[2] + 1.0)/4294967295.0)); }
void glNormal3s(GLshort nx, GLshort ny, GLshort nz) { glNormal3f((2.0f*nx + 1.0f)/65535.0f, (2.0f*ny + 1.0f)/65535.0f, (2.0f*nz + 1.0f)/65535.0f); }
void glNormal3sv(const GLshort *v) { glNormal3f((2.0f*v[0] + 1.0f)/65535.0f, (2.0f*v[1] + 1.0f)/65535.0f, (2.0f*v[2] + 1.0f)/65535.0f); }
void glColor3b(GLbyte red, GLbyte green, GLbyte blue) { glColor4f((2.0f*red + 1.0f)/255.0f, (2.0f*green + 1.0f)/255.0f, (2.0f*blue + 1.0f)/255.0f, 1.0f); }
void glColor3bv(const GLbyte *v) { glColor4f((2.0f*v[0] + 1.0f)/255.0f, (2.0f*v[1] + 1.0f)/255.0f, (2.0f*v[2] + 1.0f)/255.0f, 1.0f); }
void glColor3d(GLdouble red, GLdouble green, GLdouble blue) { glColor4f((float)red, (float)green, (float)blue, 1.0f); }
void glColor3dv(const GLdouble *v) { glColor4f((float)v[0], (float)v[1], (float)v[2], 1.0f); }
void glColor3f(GLfloat red, GLfloat green, GLfloat blue) { glColor4f(red, green, blue, 1.0f); }
void glColor3fv(const GLfloat *v) { glColor4f(v[0], v[1], v[2], 1.0f); }
void glColor3i(GLint red, GLint green, GLint blue) { glColor4f((float)((2.0*red + 1.0)/4294967295.0), (float)((2.0*green + 1.0)/4294967295.0), (float)((2.0*blue + 1.0)/4294967295.0), 1.0f); }
void glColor3iv(const GLint *v) { glColor4f((float)((2.0*v[0] + 1.0)/4294967295.0), (float)((2.0*v[1] + 1.0)/4294967295.0), (float)((2.0*v[2] + 1.0)/4294967295.0), 1.0f); }
void glColor3s(GLshort red, GLshort green, GLshort blue) { glColor4f((2.0f*red + 1.0f)/65535.0f, (2.0f*green + 1.0f)/65535.0f, (2.0f*blue + 1.0f)/65535.0f, 1.0f); }
void glColor3sv(const GLshort *v) { glColor4f((2.0f*v[0] + 1.0f)/65535.0f, (2.0f*v[1] + 1.0f)/65535.0f, (2.0f*v[2] + 1.0f)/65535.0f, 1.0f); }
void glColor3ub(GLubyte red, GLubyte green, GLubyte blue) { glColor4ub(red, green, blue, 255); }
void glColor3ubv(const GLubyte *v) { glColor4ub(v[0], v[1], v[2], 255); }
void glColor3ui(GLuint red, GLuint green, GLuint blue) { glColor4f((float)(red/4294967295.0), (float)(green/4294967295.0), (float)(blue/4294967295.0), 1.0f); }
void glColor3uiv(const GLuint *v) { glColor4f((float)(v[0]/4294967295.0), (float)(v[1]/4294967295.0), (float)(v[2]/4294967295.0), 1.0f); }
void glColor3us(GLushort red, GLushort green, GLushort blue) { glColor4f(red/65535.0f, green/65535.0f, blue/65535.0f, 1.0f); }
void glColor3usv(const GLushort *v) { glColor4f(v[0]/65535.0f, v[1]/65535.0f, v[2]/65535.0f, 1.0f); }
void glColor4b(GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha) { glColor4f((2.0f*red + 1.0f)/255.0f, (2.0f*green + 1.0f)/255.0f, (2.0f*blue + 1.0f)/255.0f, (2.0f*alpha + 1.0f)/255.0f); }
void glColor4bv(const GLbyte *v) { glColor4f((2.0f*v[0] + 1.0f)/255.0f, (2.0f*v[1] + 1.0f)/255.0f, (2.0f*v[2] + 1.0f)/255.0f, (2.0f*v[3] + 1.0f)/255.0f); }
void glColor4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha) { glColor4f((float)red, (float)green, (float)blue, (float)alpha); }
void glColor4dv(const GLdouble *v) { glColor4f((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void glColor4fv(const GLfloat *v) { glColor4f(v[0], v[1], v[2], v[3]); }
void glColor4i(GLint red, GLint green, GLint blue, GLint alpha) { glColor4f((float)((2.0*red + 1.0)/4294967295.0), (float)((2.0*green + 1.0)/4294967295.0), (float)((2.0*blue + 1.0)/4294967295.0), (float)((2.0*alpha + 1.0)/4294967295.0)); }
void glColor4iv(const GLint *v) { glColor4f((float)((2.0*v[0] + 1.0)/4294967295.0), (float)((2.0*v[1] + 1.0)/4294967295.0), (float)((2.0*v[2] + 1.0)/4294967295.0), (float)((2.0*v[3] + 1.0)/4294967295.0)); }
void glColor4s(GLshort red, GLshort green, GLshort blue, GLshort alpha) { glColor4f((2.0f*red + 1.0f)/65535.0f, (2.0f*green + 1.0f)/65535.0f, (2.0f*blue + 1.0f)/65535.0f, (2.0f*alpha + 1.0f)/65535.0f); }
void glColor4sv(const GLshort *v) { glColor4f((2.0f*v[0] + 1.0f)/65535.0f, (2.0f*v[1] + 1.0f)/65535.0f, (2.0f*v[2] + 1.0f)/65535.0f, (2.0f*v[3] + 1.0f)/65535.0f); }
void glColor4ubv(const GLubyte *v) { glColor4ub(v[0], v[1], v[2], v[3]); }
void glColor4ui(GLuint red, GLuint green, GLuint blue, GLuint alpha) { glColor4f((float)(red/4294967295.0), (float)(green/4294967295.0), (float)(blue/4294967295.0), (float)(alpha/4294967295.0)); }
void glColor4uiv(const GLuint *v) { glColor4f((float)(v[0]/4294967295.0), (float)(v[1]/4294967295.0), (float)(v[2]/4294967295.0), (float)(v[3]/4294967295.0)); }
void glColor4us(GLushort red, GLushort green, GLushort blue, GLushort alpha) { glColor4f(red/65535.0f, green/65535.0f, blue/65535.0f, alpha/65535.0f); }
void glColor4usv(const GLushort *v) { glColor4f(v[0]/65535.0f, v[1]/65535.0f, v[2]/65535.0f, v[3]/65535.0f); }
void glRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2) { glRectf((float)x1, (float)y1, (float)x2, (float)y2); }
void glRectdv(const GLdouble *v1, const GLdouble *v2) { glRectf((float)v1[0], (float)v1[1], (float)v2[0], (float)v2[1]); }
void glRectfv(const GLfloat *v1, const GLfloat *v2) { glRectf(v1[0], v1[1], v2[0], v2[1]); }
void glRecti(GLint x1, GLint y1, GLint x2, GLint y2) { glRectf((float)x1, (float)y1, (float)x2, (float)y2); }
void glRectiv(const GLint *v1, const GLint *v2) { glRectf((float)v1[0], (float)v1[1], (float)v2[0], (float)v2[1]); }
void glRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2) { glRectf(x1, y1, x2, y2); }
void glRectsv(const GLshort *v1, const GLshort *v2) { glRectf(v1[0], v1[1], v2[0], v2[1]); }
