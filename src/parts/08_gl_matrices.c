// OpenGL: matrices
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// OpenGL: matrices
//----------------------------------------------------------------------------------
void glMatrixMode(GLenum mode)
{
    LIST_SAVE(MATRIX_MODE, "u", mode);
    switch (mode)
    {
        case GL_MODELVIEW: gl.matrixMode = 0; break;
        case GL_PROJECTION: gl.matrixMode = 1; break;
        case GL_TEXTURE: gl.matrixMode = 2; break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

void glPushMatrix(void)
{
    LIST_SAVE(PUSH_MATRIX, "");
    int *depth = &gl.stackDepth[matrixStack()];
    if (*depth + 1 >= C3DGL_MATRIX_STACK) { WARN_ONCE("Matrix stack overflow\n"); setError(GL_STACK_OVERFLOW); return; }

    gl.stack[matrixStack()][*depth + 1] = gl.stack[matrixStack()][*depth];
    (*depth)++;
}

void glPopMatrix(void)
{
    LIST_SAVE(POP_MATRIX, "");
    int *depth = &gl.stackDepth[matrixStack()];
    if (*depth == 0) { WARN_ONCE("Matrix stack underflow\n"); setError(GL_STACK_UNDERFLOW); return; }

    (*depth)--;
    matrixChanged();
}

void glLoadIdentity(void)
{
    LIST_SAVE(LOAD_IDENTITY, "");
    mat4Identity(currentMatrix());
    matrixChanged();
}

void glMultMatrixf(const GLfloat *m)
{
    LIST_SAVE(MULT_MATRIX, "F", 16, m);
    Mat4 mat;
    memcpy(mat.m, m, sizeof(mat.m));
    multCurrent(&mat);
}

void glMultMatrixd(const GLdouble *m)
{
    GLfloat f[16];
    for (int i = 0; i < 16; i++) f[i] = (float)m[i];
    glMultMatrixf(f);
}

void glLoadMatrixf(const GLfloat *m)
{
    LIST_SAVE(LOAD_MATRIX, "F", 16, m);
    memcpy(currentMatrix()->m, m, 16*sizeof(float));
    matrixChanged();
}

void glLoadMatrixd(const GLdouble *m)
{
    GLfloat f[16];
    for (int i = 0; i < 16; i++) f[i] = (float)m[i];
    glLoadMatrixf(f);
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    LIST_SAVE(TRANSLATE, "fff", x, y, z);
    Mat4 m;
    mat4Identity(&m);
    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
    multCurrent(&m);
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    LIST_SAVE(ROTATE, "ffff", angle, x, y, z);
    float len = sqrtf(x*x + y*y + z*z);
    if (len == 0.0f) return;
    x /= len; y /= len; z /= len;

    float rad = angle*(float)M_PI/180.0f;
    float c = cosf(rad), s = sinf(rad), t = 1.0f - c;

    Mat4 m;
    mat4Identity(&m);
    m.m[0] = x*x*t + c;     m.m[4] = x*y*t - z*s;   m.m[8] = x*z*t + y*s;
    m.m[1] = y*x*t + z*s;   m.m[5] = y*y*t + c;     m.m[9] = y*z*t - x*s;
    m.m[2] = z*x*t - y*s;   m.m[6] = z*y*t + x*s;   m.m[10] = z*z*t + c;
    multCurrent(&m);
}

void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    LIST_SAVE(SCALE, "fff", x, y, z);
    Mat4 m;
    mat4Identity(&m);
    m.m[0] = x;
    m.m[5] = y;
    m.m[10] = z;
    multCurrent(&m);
}

void glTranslated(GLdouble x, GLdouble y, GLdouble z) { glTranslatef((float)x, (float)y, (float)z); }
void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z) { glRotatef((float)angle, (float)x, (float)y, (float)z); }
void glScaled(GLdouble x, GLdouble y, GLdouble z) { glScalef((float)x, (float)y, (float)z); }

void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    LIST_SAVE(ORTHO, "dddddd", left, right, bottom, top, zNear, zFar);
    Mat4 m;
    mat4Identity(&m);
    m.m[0] = (float)(2.0/(right - left));
    m.m[5] = (float)(2.0/(top - bottom));
    m.m[10] = (float)(-2.0/(zFar - zNear));
    m.m[12] = (float)(-(right + left)/(right - left));
    m.m[13] = (float)(-(top + bottom)/(top - bottom));
    m.m[14] = (float)(-(zFar + zNear)/(zFar - zNear));
    multCurrent(&m);
}

void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    LIST_SAVE(FRUSTUM, "dddddd", left, right, bottom, top, zNear, zFar);
    Mat4 m;
    memset(&m, 0, sizeof(m));
    m.m[0] = (float)(2.0*zNear/(right - left));
    m.m[5] = (float)(2.0*zNear/(top - bottom));
    m.m[8] = (float)((right + left)/(right - left));
    m.m[9] = (float)((top + bottom)/(top - bottom));
    m.m[10] = (float)(-(zFar + zNear)/(zFar - zNear));
    m.m[11] = -1.0f;
    m.m[14] = (float)(-2.0*zFar*zNear/(zFar - zNear));
    multCurrent(&m);
}
