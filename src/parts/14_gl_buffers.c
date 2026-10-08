// OpenGL: buffer objects
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// OpenGL: buffer objects (ES 1.1, GL 1.5)
//----------------------------------------------------------------------------------
static bool bufferValid(GLuint id)
{
    return (id > 0) && (id < gl.bufferCount) && gl.buffers[id].used;
}

// Make room for ids up to `id` (table grows, there is no fixed limit)
static bool reserveBufferId(GLuint id)
{
    if (id < gl.bufferCount) return true;

    GLuint count = gl.bufferCount? gl.bufferCount : 16;
    while (count <= id) count *= 2;
    Buffer *table = realloc(gl.buffers, count*sizeof(Buffer));
    if (table == NULL) { setError(GL_OUT_OF_MEMORY); return false; }
    memset(table + gl.bufferCount, 0, (count - gl.bufferCount)*sizeof(Buffer));
    gl.buffers = table;
    gl.bufferCount = count;
    return true;
}

void glGenBuffers(GLsizei n, GLuint *buffers)
{
    if (n < 0) { setError(GL_INVALID_VALUE); return; }

    GLuint id = 1;
    for (int i = 0; i < n; i++)
    {
        while ((id < gl.bufferCount) && gl.buffers[id].used) id++;
        if (!reserveBufferId(id)) return;
        memset(&gl.buffers[id], 0, sizeof(Buffer));
        gl.buffers[id].used = true;
        gl.buffers[id].usage = GL_STATIC_DRAW;
        buffers[i] = id++;
    }
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers)
{
    if (n < 0) { setError(GL_INVALID_VALUE); return; }

    for (int i = 0; i < n; i++)
    {
        GLuint id = buffers[i];
        if (!bufferValid(id)) continue;

        // Bindings to the deleted buffer revert to 0; arrays sourcing from it are cleared (their pointer is an offset)
        if (gl.arrayBuffer == id) gl.arrayBuffer = 0;
        if (gl.elementArrayBuffer == id) gl.elementArrayBuffer = 0;
        for (int a = 0; a < ARRAY_COUNT; a++)
        {
            if (gl.arrays[a].buffer != id) continue;
            gl.arrays[a].buffer = 0;
            gl.arrays[a].pointer = NULL;
        }

        invalidateGpuCache(&gl.buffers[id]);
        free(gl.buffers[id].data);
        memset(&gl.buffers[id], 0, sizeof(Buffer));
    }
}

GLboolean glIsBuffer(GLuint buffer) { return bufferValid(buffer); }

static GLuint *bufferBinding(GLenum target)
{
    switch (target)
    {
        case GL_ARRAY_BUFFER: return &gl.arrayBuffer;
        case GL_ELEMENT_ARRAY_BUFFER: return &gl.elementArrayBuffer;
        default: setError(GL_INVALID_ENUM); return NULL;
    }
}

void glBindBuffer(GLenum target, GLuint buffer)
{
    GLuint *binding = bufferBinding(target);
    if (binding == NULL) return;

    // Binding an unused name creates the buffer (like glBindTexture)
    if ((buffer != 0) && !bufferValid(buffer))
    {
        if (!reserveBufferId(buffer)) return;
        memset(&gl.buffers[buffer], 0, sizeof(Buffer));
        gl.buffers[buffer].used = true;
        gl.buffers[buffer].usage = GL_STATIC_DRAW;
    }
    *binding = buffer;
}

static bool bufferUsageValid(GLenum usage)
{
    // ES 1.1: STATIC_DRAW, DYNAMIC_DRAW; GL 1.5 adds the STREAM and READ/COPY variants
    return (usage >= GL_STREAM_DRAW) && (usage <= GL_DYNAMIC_COPY) && (usage != 0x88E3) && (usage != 0x88E7);
}

void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage)
{
    GLuint *binding = bufferBinding(target);
    if (binding == NULL) return;
    if (!bufferUsageValid(usage)) { setError(GL_INVALID_ENUM); return; }
    if (size < 0) { setError(GL_INVALID_VALUE); return; }
    if (*binding == 0) { setError(GL_INVALID_OPERATION); return; }

    Buffer *b = &gl.buffers[*binding];
    u8 *storage = NULL;
    if (size > 0)
    {
        storage = malloc((size_t)size);
        if (storage == NULL) { setError(GL_OUT_OF_MEMORY); return; }
        if (data != NULL) memcpy(storage, data, (size_t)size);
    }
    invalidateGpuCache(b);
    b->revision = ++gl.bufferRevision;
    free(b->data);
    b->data = storage;
    b->size = size;
    b->usage = usage;
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data)
{
    GLuint *binding = bufferBinding(target);
    if (binding == NULL) return;
    if (*binding == 0) { setError(GL_INVALID_OPERATION); return; }

    Buffer *b = &gl.buffers[*binding];
    if ((offset < 0) || (size < 0) || (offset + size > b->size)) { setError(GL_INVALID_VALUE); return; }
    if (size > 0) {
        invalidateGpuCache(b);
        b->revision = ++gl.bufferRevision;
        memcpy(b->data + offset, data, (size_t)size);
    }
}

void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
    GLuint *binding = bufferBinding(target);
    if (binding == NULL) return;
    if (*binding == 0) { setError(GL_INVALID_OPERATION); return; }

    const Buffer *b = &gl.buffers[*binding];
    switch (pname)
    {
        case GL_BUFFER_SIZE: *params = (GLint)b->size; break;
        case GL_BUFFER_USAGE: *params = (GLint)b->usage; break;
        default: setError(GL_INVALID_ENUM); break;
    }
}
