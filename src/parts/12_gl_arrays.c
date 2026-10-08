// OpenGL: client-side vertex arrays, glDrawArrays
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// OpenGL: client-side vertex arrays
//----------------------------------------------------------------------------------
static int typeSize(GLenum type)
{
    switch (type)
    {
        case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
        case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
        case GL_INT: case GL_UNSIGNED_INT: case GL_FLOAT: case GL_FIXED: return 4;
        case GL_DOUBLE: return 8;
        default: return 0;
    }
}

// Validate and set a client array. sizes: bit n set = size n allowed; types: zero-terminated list
static void setArray(int index, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer, unsigned sizes, const GLenum *types)
{
    if (((sizes >> size) & 1) == 0) { setError(GL_INVALID_VALUE); return; }
    if (stride < 0) { setError(GL_INVALID_VALUE); return; }

    bool valid = false;
    for (const GLenum *t = types; *t != 0; t++) valid = valid || (*t == type);
    if (!valid) { setError(GL_INVALID_ENUM); return; }

    gl.arrays[index].pointer = pointer;
    gl.arrays[index].buffer = gl.arrayBuffer;
    gl.arrays[index].size = size;
    gl.arrays[index].type = type;
    gl.arrays[index].stride = stride;
}

// Types of GL 1.1 and ES 1.1 together (GL_BYTE and GL_FIXED from ES, GL_INT and GL_DOUBLE from GL)
static const GLenum positionTypes[] = { GL_BYTE, GL_SHORT, GL_INT, GL_FLOAT, GL_DOUBLE, GL_FIXED, 0 };
static const GLenum colorTypes[] = { GL_BYTE, GL_UNSIGNED_BYTE, GL_SHORT, GL_UNSIGNED_SHORT, GL_INT, GL_UNSIGNED_INT,
                                     GL_FLOAT, GL_DOUBLE, GL_FIXED, 0 };
static const GLenum edgeFlagTypes[] = { GL_UNSIGNED_BYTE, 0 };
static const GLenum pointSizeTypes[] = { GL_FLOAT, GL_FIXED, 0 };

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    setArray(ARRAY_VERTEX, size, type, stride, pointer, (1 << 2) | (1 << 3) | (1 << 4), positionTypes);
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    setArray(ARRAY_TEXCOORD, size, type, stride, pointer, (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4), positionTypes);
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    setArray(ARRAY_COLOR, size, type, stride, pointer, (1 << 3) | (1 << 4), colorTypes);
}

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    if (type == GL_UNSIGNED_BYTE) { setError(GL_INVALID_ENUM); return; }
    setArray(ARRAY_NORMAL, 3, type, stride, pointer, 1 << 3, positionTypes);
}

void glEdgeFlagPointer(GLsizei stride, const GLvoid *pointer)
{
    setArray(ARRAY_EDGEFLAG, 1, GL_UNSIGNED_BYTE, stride, pointer, 1 << 1, edgeFlagTypes);
}

void glPointSizePointerOES(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    setArray(ARRAY_POINTSIZE, 1, type, stride, pointer, 1 << 1, pointSizeTypes);
}

void glGetPointerv(GLenum pname, GLvoid **params)
{
    switch (pname)
    {
        case GL_VERTEX_ARRAY_POINTER: *params = (GLvoid *)gl.arrays[ARRAY_VERTEX].pointer; break;
        case GL_NORMAL_ARRAY_POINTER: *params = (GLvoid *)gl.arrays[ARRAY_NORMAL].pointer; break;
        case GL_COLOR_ARRAY_POINTER: *params = (GLvoid *)gl.arrays[ARRAY_COLOR].pointer; break;
        case GL_TEXTURE_COORD_ARRAY_POINTER: *params = (GLvoid *)gl.arrays[ARRAY_TEXCOORD].pointer; break;
        case GL_EDGE_FLAG_ARRAY_POINTER: *params = (GLvoid *)gl.arrays[ARRAY_EDGEFLAG].pointer; break;
        case GL_POINT_SIZE_ARRAY_POINTER_OES: *params = (GLvoid *)gl.arrays[ARRAY_POINTSIZE].pointer; break;
        case GL_FEEDBACK_BUFFER_POINTER: *params = gl.feedbackBuffer; break;
        case GL_SELECTION_BUFFER_POINTER: *params = gl.selectBuffer; break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

// Data of `bytes` bytes at `offset` of a buffer object, or of client memory if buffer is 0.
// NULL if it is outside the buffer (GL leaves that undefined; we skip the data instead of crashing)
static const u8 *bufferRange(GLuint buffer, const void *pointer, size_t offset, size_t bytes)
{
    if (buffer == 0) return (const u8 *)pointer + offset;

    const Buffer *b = &gl.buffers[buffer];
    size_t start = (size_t)(uintptr_t)pointer + offset;
    if ((b->data == NULL) || (start + bytes > (size_t)b->size))
    {
        WARN_ONCE("Buffer object %u read out of range, skipped\n", buffer);
        return NULL;
    }
    return b->data + start;
}

// Address of element `index` of a client array (NULL if outside its buffer object)
static const u8 *arrayElement(const ClientArray *a, int index)
{
    int size = typeSize(a->type);
    size_t stride = a->stride? (size_t)a->stride : (size_t)(a->size*size);
    return bufferRange(a->buffer, a->pointer, (size_t)index*stride, (size_t)(a->size*size));
}

// Components of element `index` as floats. normalized: integer types map to [0, 1] / [-1, 1] like glColor.
// False if the element is outside its buffer object
static bool readArray(const ClientArray *a, int index, float out[4], bool normalized)
{
    int size = typeSize(a->type);
    const u8 *p = arrayElement(a, index);
    if (p == NULL) return false;
    if (a->type == GL_FLOAT) { memcpy(out, p, (size_t)a->size*sizeof(float)); return true; }

    for (int i = 0; i < a->size; i++, p += size)
    {
        switch (a->type)
        {
            case GL_BYTE: { s8 v = *(const s8 *)p; out[i] = normalized? (2.0f*v + 1.0f)/255.0f : v; break; }
            case GL_UNSIGNED_BYTE: out[i] = normalized? *p/255.0f : *p; break;
            case GL_SHORT: { s16 v; memcpy(&v, p, 2); out[i] = normalized? (2.0f*v + 1.0f)/65535.0f : v; break; }
            case GL_UNSIGNED_SHORT: { u16 v; memcpy(&v, p, 2); out[i] = normalized? v/65535.0f : v; break; }
            case GL_INT: { s32 v; memcpy(&v, p, 4); out[i] = normalized? (float)((2.0*v + 1.0)/4294967295.0) : (float)v; break; }
            case GL_UNSIGNED_INT: { u32 v; memcpy(&v, p, 4); out[i] = normalized? (float)(v/4294967295.0) : (float)v; break; }
            case GL_FIXED: { s32 v; memcpy(&v, p, 4); out[i] = v/65536.0f; break; }
            case GL_DOUBLE: { double v; memcpy(&v, p, 8); out[i] = (float)v; break; }
            default: memcpy(&out[i], p, 4); break;     // GL_FLOAT
        }
    }
    return true;
}

// Enabled and set; with a buffer object the pointer is an offset and may be 0
static bool arrayActive(int index)
{
    return gl.arrays[index].enabled && ((gl.arrays[index].pointer != NULL) || (gl.arrays[index].buffer != 0));
}

// Vertex `index` from the enabled arrays; attributes without an array come from the current values.
// Without a vertex array only the current values are updated (glArrayElement)
static void submitArrayVertex(int index)
{
    Vertex v = gl.current;
    bool edge = gl.currentEdge;

    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        if (!arrayActive(ARRAY_TEXCOORD0 + unit)) continue;
        float t[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        if (!readArray(&gl.arrays[ARRAY_TEXCOORD0 + unit], index, t, false)) return;
        float *dst = (unit == 0)? v.tex : v.texExtra[unit - 1];
        dst[0] = t[0];
        dst[1] = t[1];
        dst[2] = t[3];
        if (unit == 0) v.texR = t[2];
    }
    if (arrayActive(ARRAY_COLOR))
    {
        float c[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        if (!readArray(&gl.arrays[ARRAY_COLOR], index, c, true)) return;
        for (int i = 0; i < 4; i++) v.color[i] = colorByte(c[i]);
    }
    if (arrayActive(ARRAY_NORMAL))
    {
        float n[4];
        if (!readArray(&gl.arrays[ARRAY_NORMAL], index, n, true)) return;
        memcpy(gl.currentNormal, n, sizeof(gl.currentNormal));
    }
    if (arrayActive(ARRAY_EDGEFLAG))
    {
        const u8 *e = arrayElement(&gl.arrays[ARRAY_EDGEFLAG], index);
        if (e == NULL) return;
        edge = (*e != 0);
    }
    if (arrayActive(ARRAY_POINTSIZE))
    {
        float size[4];
        if (!readArray(&gl.arrays[ARRAY_POINTSIZE], index, size, false)) return;
        v.pointSize = fmaxf(size[0], 0.0f);
    }

    if (!arrayActive(ARRAY_VERTEX))
    {
        memcpy(gl.current.tex, v.tex, sizeof(v.tex));
        gl.current.texR = gl.currentTexR[0] = v.texR;
        memcpy(gl.current.texExtra, v.texExtra, sizeof(v.texExtra));
        memcpy(gl.current.color, v.color, 4);
        gl.currentEdge = edge;
        return;
    }

    float p[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    if (!readArray(&gl.arrays[ARRAY_VERTEX], index, p, false)) return;
    if (p[3] != 1.0f)
    {
        if (p[3] == 0.0f) { WARN_ONCE("Vertex array: w = 0 (point at infinity) not supported\n"); return; }
        for (int i = 0; i < 3; i++) p[i] /= p[3];
    }
    memcpy(v.pos, p, sizeof(v.pos));
    submitLitVertex(&v, gl.currentNormal, edge);
}

// Before drawing from arrays: a vertex array is needed, size 4 texcoords may need projection mode
static bool arraysReady(void)
{
    if (!arrayActive(ARRAY_VERTEX)) return false;
    if (arrayActive(ARRAY_TEXCOORD0) && (gl.arrays[ARRAY_TEXCOORD0].size == 4)) markTexQ();
    return true;
}

void glArrayElement(GLint i)
{
    if (gl.listCompiling) { listArrayElement(i); return; }
    if (arrayActive(ARRAY_TEXCOORD0) && (gl.arrays[ARRAY_TEXCOORD0].size == 4)) markTexQ();
    if (!gl.inBegin && arrayActive(ARRAY_VERTEX)) return;    // A vertex outside glBegin/glEnd is ignored
    submitArrayVertex(i);
}

// glInterleavedArrays formats: which arrays, their sizes/types and byte offsets, the default stride
typedef struct {
    GLenum format;
    int texSize, colorSize, normal, vertexSize;
    GLenum colorType;
    int colorOffset, normalOffset, vertexOffset, stride;
} InterleavedFormat;

#define F_ sizeof(GLfloat)
#define C_ 4            // 4 GLubytes rounded up to a multiple of sizeof(GLfloat)
static const InterleavedFormat interleavedFormats[] = {
    { GL_V2F,             0, 0, 0, 2, 0,                0,     0,     0,      2*F_ },
    { GL_V3F,             0, 0, 0, 3, 0,                0,     0,     0,      3*F_ },
    { GL_C4UB_V2F,        0, 4, 0, 2, GL_UNSIGNED_BYTE, 0,     0,     C_,     C_ + 2*F_ },
    { GL_C4UB_V3F,        0, 4, 0, 3, GL_UNSIGNED_BYTE, 0,     0,     C_,     C_ + 3*F_ },
    { GL_C3F_V3F,         0, 3, 0, 3, GL_FLOAT,         0,     0,     3*F_,   6*F_ },
    { GL_N3F_V3F,         0, 0, 1, 3, 0,                0,     0,     3*F_,   6*F_ },
    { GL_C4F_N3F_V3F,     0, 4, 1, 3, GL_FLOAT,         0,     4*F_,  7*F_,   10*F_ },
    { GL_T2F_V3F,         2, 0, 0, 3, 0,                0,     0,     2*F_,   5*F_ },
    { GL_T4F_V4F,         4, 0, 0, 4, 0,                0,     0,     4*F_,   8*F_ },
    { GL_T2F_C4UB_V3F,    2, 4, 0, 3, GL_UNSIGNED_BYTE, 2*F_,  0,     C_ + 2*F_, C_ + 5*F_ },
    { GL_T2F_C3F_V3F,     2, 3, 0, 3, GL_FLOAT,         2*F_,  0,     5*F_,   8*F_ },
    { GL_T2F_N3F_V3F,     2, 0, 1, 3, 0,                0,     2*F_,  5*F_,   8*F_ },
    { GL_T2F_C4F_N3F_V3F, 2, 4, 1, 3, GL_FLOAT,         2*F_,  6*F_,  9*F_,   12*F_ },
    { GL_T4F_C4F_N3F_V4F, 4, 4, 1, 4, GL_FLOAT,         4*F_,  8*F_,  11*F_,  15*F_ },
};
#undef F_
#undef C_

void glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer)
{
    const InterleavedFormat *f = NULL;
    for (size_t i = 0; i < sizeof(interleavedFormats)/sizeof(interleavedFormats[0]); i++)
        if (interleavedFormats[i].format == format) f = &interleavedFormats[i];
    if (f == NULL) { setError(GL_INVALID_ENUM); return; }
    if (stride < 0) { setError(GL_INVALID_VALUE); return; }

    if (stride == 0) stride = f->stride;
    const u8 *base = pointer;

    gl.arrays[ARRAY_EDGEFLAG].enabled = false;
    gl.arrays[ARRAY_TEXCOORD].enabled = (f->texSize > 0);
    if (f->texSize > 0) glTexCoordPointer(f->texSize, GL_FLOAT, stride, base);
    gl.arrays[ARRAY_COLOR].enabled = (f->colorSize > 0);
    if (f->colorSize > 0) glColorPointer(f->colorSize, f->colorType, stride, base + f->colorOffset);
    gl.arrays[ARRAY_NORMAL].enabled = f->normal;
    if (f->normal) glNormalPointer(GL_FLOAT, stride, base + f->normalOffset);
    gl.arrays[ARRAY_VERTEX].enabled = true;
    glVertexPointer(f->vertexSize, GL_FLOAT, stride, base + f->vertexOffset);
}

static bool arrayTriangleFastPath(GLenum mode, GLint first, GLsizei count);

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (count < 0) { setError(GL_INVALID_VALUE); return; }
    if (gl.listCompiling)
    {
        // Compiled as glBegin, glArrayElement per vertex (dereferenced now), glEnd
        if (!arrayActive(ARRAY_VERTEX)) return;
        glBegin(mode);
        for (int i = 0; i < count; i++) listArrayElement(first + i);
        glEnd();
        return;
    }
    if (!arraysReady() || !beginPrimitive(mode)) return;

    if (arrayTriangleFastPath(mode, first, count))
    {
        endPrimitive();
        return;
    }
    for (int i = 0; i < count; i++) submitArrayVertex(first + i);
    endPrimitive();
}
