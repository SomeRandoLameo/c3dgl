// OpenGL: display lists
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// OpenGL: display lists (GL)
//
// Between glNewList and glEndList, the gl* function of every command in LIST_COMMANDS records it with listSave() (see
// LIST_SAVE) and returns. Client memory is copied while compiling (pixels, control points, vertex array elements,
// glCallLists names), as GL requires. glCallList executes the commands through the same gl* functions, so a list does
// exactly what the calls would do; in GL_COMPILE_AND_EXECUTE mode each command is executed that way right after it is
// recorded. Errors of recorded commands are raised when the list is executed (some argument checks of the variant
// functions, like glLightf's, happen while compiling)
//----------------------------------------------------------------------------------
static double listDouble(const ListWord *w)
{
    double d;
    memcpy(&d, w, sizeof(d));      // Doubles are only 4-byte aligned in a list
    return d;
}

// glTexImage1D/2D, glTexSubImage1D/2D from a list: the pixels were stored tightly packed, see listSaveImage()
static void listTexImage(const ListWord *w, bool sub)
{
    PixelStore saved = gl.unpack;
    gl.unpack = (PixelStore){ .alignment = 1, .swapBytes = (w[8].i & 2) != 0 };
    const GLvoid *pixels = (w[8].i & 1)? (const GLvoid *)&w[9] : NULL;
    bool oneD = (w[8].i & 4) != 0;
    if (sub && oneD) glTexSubImage1D(w[0].u, w[1].i, w[2].i, w[4].i, w[6].u, w[7].u, pixels);
    else if (sub) glTexSubImage2D(w[0].u, w[1].i, w[2].i, w[3].i, w[4].i, w[5].i, w[6].u, w[7].u, pixels);
    else if (oneD) glTexImage1D(w[0].u, w[1].i, w[2].i, w[3].i, w[5].i, w[6].u, w[7].u, pixels);
    else glTexImage2D(w[0].u, w[1].i, w[2].i, w[3].i, w[4].i, w[5].i, w[6].u, w[7].u, pixels);
    gl.unpack = saved;
}

static void executeCommand(const ListWord *command)
{
    const ListWord *w = command + 1;
    switch ((ListCommand)(command->u & 0xFF))
    {
        #define X(name, call) case LIST_##name: call; break;
        LIST_COMMANDS(X)
        #undef X
    }
}

// Binary search; *index (if given): position of the list, or where it would be inserted
static DisplayList *findList(GLuint name, int *index)
{
    int lo = 0, hi = gl.listCount;
    while (lo < hi)
    {
        int mid = (lo + hi)/2;
        if (gl.lists[mid].name < name) lo = mid + 1;
        else hi = mid;
    }
    if (index != NULL) *index = lo;
    return ((lo < gl.listCount) && (gl.lists[lo].name == name))? &gl.lists[lo] : NULL;
}

// Lists cannot be created or deleted while one is executed (those calls are not compiled), so l stays valid
static void executeList(GLuint name)
{
    if (gl.listDepth >= C3DGL_MAX_LIST_NESTING) return;     // Deeper calls are ignored
    const DisplayList *l = findList(name, NULL);
    if (l == NULL) return;

    gl.listDepth++;
    for (const ListWord *w = l->words, *end = w + l->count; w < end; w += w->u >> 8) executeCommand(w);
    gl.listDepth--;
}

// Append a command with `words` argument words to the list being compiled and return its arguments; NULL if out of
// memory (the command is dropped)
static ListWord *listBegin(ListCommand command, int words)
{
    gl.listLast = -1;
    int needed = gl.listWordCount + 1 + words;
    if ((words >= (1 << 24) - 1) || (needed < 0)) { setError(GL_OUT_OF_MEMORY); return NULL; }
    if (needed > gl.listWordCapacity)
    {
        int capacity = gl.listWordCapacity? gl.listWordCapacity : 256;
        while (capacity < needed) capacity *= 2;
        ListWord *grown = realloc(gl.listWords, (size_t)capacity*sizeof(ListWord));
        if (grown == NULL) { setError(GL_OUT_OF_MEMORY); return NULL; }
        gl.listWords = grown;
        gl.listWordCapacity = capacity;
    }

    ListWord *header = &gl.listWords[gl.listWordCount];
    header->u = (GLuint)command | ((GLuint)(1 + words) << 8);
    gl.listLast = gl.listWordCount;
    gl.listWordCount = needed;
    return header + 1;
}

// After the arguments are written: GL_COMPILE_AND_EXECUTE executes the command, from the list
static void listEnd(void)
{
    if ((gl.listMode != GL_COMPILE_AND_EXECUTE) || (gl.listLast < 0)) return;
    gl.listCompiling = false;
    executeCommand(&gl.listWords[gl.listLast]);
    gl.listCompiling = true;
}

// Record a command. Each character of format is an argument: 'i' int, 'u' unsigned, 'f' float (passed as double),
// 'd' double (2 words), 'F' an int count and a pointer to that many floats, 'D' the same for doubles
static void listSave(ListCommand command, const char *format, ...)
{
    va_list args, sizes;
    va_start(args, format);
    va_copy(sizes, args);
    int words = 0;
    for (const char *c = format; *c != '\0'; c++)
    {
        switch (*c)
        {
            case 'i': (void)va_arg(sizes, int); words++; break;
            case 'u': (void)va_arg(sizes, unsigned); words++; break;
            case 'f': (void)va_arg(sizes, double); words++; break;
            case 'd': (void)va_arg(sizes, double); words += 2; break;
            case 'F': words += va_arg(sizes, int); (void)va_arg(sizes, const float *); break;
            default: words += 2*va_arg(sizes, int); (void)va_arg(sizes, const double *); break;    // 'D'
        }
    }
    va_end(sizes);

    ListWord *w = listBegin(command, words);
    bool saved = (w != NULL);
    for (const char *c = format; saved && (*c != '\0'); c++)
    {
        switch (*c)
        {
            case 'i': (w++)->i = va_arg(args, int); break;
            case 'u': (w++)->u = va_arg(args, unsigned); break;
            case 'f': (w++)->f = (float)va_arg(args, double); break;
            case 'd': { double d = va_arg(args, double); memcpy(w, &d, sizeof(d)); w += 2; break; }
            case 'F':
            {
                int n = va_arg(args, int);
                const float *p = va_arg(args, const float *);
                if (n > 0) memcpy(w, p, (size_t)n*sizeof(float));
                w += n;
                break;
            }
            default:    // 'D'
            {
                int n = va_arg(args, int);
                const double *p = va_arg(args, const double *);
                if (n > 0) memcpy(w, p, (size_t)n*sizeof(double));
                w += 2*n;
                break;
            }
        }
    }
    va_end(args);
    if (saved) listEnd();
}

// glTexImage1D/2D, glTexSubImage1D/2D: args are the 8 integer arguments of the 2D call before the pixels (1D: height 1
// and yoffset 0 filled in). The pixels are read as the unpack state lays them out and stored tightly packed; not if the
// call fails anyway before reading them (sizeValid false or an invalid format/type), it is recorded without them then
static void listSaveImage(ListCommand command, const GLint args[8], GLsizei width, GLsizei height, bool sizeValid,
                          bool oneD, const void *pixels)
{
    int n, elemSize, groupSize;
    bool captured = (pixels != NULL) && sizeValid &&
                    (colorImageLayout((GLenum)args[6], (GLenum)args[7], &n, &elemSize, &groupSize) == GL_NO_ERROR);
    bool bits = ((GLenum)args[7] == GL_BITMAP);
    size_t rowBytes = !captured? 0 : bits? ((size_t)width + 7)/8 : (size_t)width*groupSize;
    ListWord *w = listBegin(command, 9 + (captured? (int)((rowBytes*height + 3)/4) : 0));
    if (w == NULL) return;

    for (int i = 0; i < 8; i++) w[i].i = args[i];
    w[8].i = (captured? 1 : 0) | (gl.unpack.swapBytes? 2 : 0) | (oneD? 4 : 0);
    if (captured && bits) packBitmap(pixels, &gl.unpack, width, height, (u8 *)&w[9]);
    else if (captured)
    {
        const PixelStore *ps = &gl.unpack;
        size_t srcRow = imageRowBytes(ps, width, elemSize, groupSize);
        const u8 *src = (const u8 *)pixels + (size_t)ps->skipRows*srcRow + (size_t)ps->skipPixels*groupSize;
        for (int y = 0; y < height; y++) memcpy((u8 *)&w[9] + (size_t)y*rowBytes, src + (size_t)y*srcRow, rowBytes);
    }
    listEnd();
}

// glArrayElement while compiling: the element of the enabled arrays is recorded as the immediate mode calls it stands
// for (nothing if it is outside its buffer object, like submitArrayVertex())
static void listArrayElement(int index)
{
    float tex[C3DGL_TEXTURE_UNITS][4], color[4] = { 0.0f, 0.0f, 0.0f, 1.0f }, normal[4];
    float pos[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        float *t = tex[unit];
        t[0] = t[1] = t[2] = 0.0f;
        t[3] = 1.0f;
        if (arrayActive(ARRAY_TEXCOORD0 + unit) && !readArray(&gl.arrays[ARRAY_TEXCOORD0 + unit], index, t, false)) return;
    }
    if (arrayActive(ARRAY_COLOR) && !readArray(&gl.arrays[ARRAY_COLOR], index, color, true)) return;
    if (arrayActive(ARRAY_NORMAL) && !readArray(&gl.arrays[ARRAY_NORMAL], index, normal, true)) return;
    const u8 *edge = arrayActive(ARRAY_EDGEFLAG)? arrayElement(&gl.arrays[ARRAY_EDGEFLAG], index) : NULL;
    if (arrayActive(ARRAY_EDGEFLAG) && (edge == NULL)) return;
    if (arrayActive(ARRAY_VERTEX) && !readArray(&gl.arrays[ARRAY_VERTEX], index, pos, false)) return;
    if (arrayActive(ARRAY_POINTSIZE)) WARN_ONCE("Display lists: the point size array is not recorded\n");

    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        if (arrayActive(ARRAY_TEXCOORD0 + unit))
            glMultiTexCoord4f(GL_TEXTURE0 + unit, tex[unit][0], tex[unit][1], tex[unit][2], tex[unit][3]);
    }
    if (arrayActive(ARRAY_COLOR)) glColor4ub(colorByte(color[0]), colorByte(color[1]), colorByte(color[2]), colorByte(color[3]));
    if (arrayActive(ARRAY_NORMAL)) glNormal3f(normal[0], normal[1], normal[2]);
    if (edge != NULL) glEdgeFlag(*edge != 0);
    if (arrayActive(ARRAY_VERTEX)) glVertex4f(pos[0], pos[1], pos[2], pos[3]);
}

// Insert empty lists named name .. name + count - 1 at `index` (none of them exists); false if out of memory
static bool insertLists(int index, GLuint name, int count)
{
    if (gl.listCount + count > gl.listCapacity)
    {
        int capacity = gl.listCapacity? gl.listCapacity : 64;
        while (capacity < gl.listCount + count) capacity *= 2;
        DisplayList *grown = realloc(gl.lists, (size_t)capacity*sizeof(DisplayList));
        if (grown == NULL) { setError(GL_OUT_OF_MEMORY); return false; }
        gl.lists = grown;
        gl.listCapacity = capacity;
    }
    memmove(&gl.lists[index + count], &gl.lists[index], (size_t)(gl.listCount - index)*sizeof(DisplayList));
    for (int i = 0; i < count; i++) gl.lists[index + i] = (DisplayList){ name + i, NULL, 0 };
    gl.listCount += count;
    return true;
}

void glNewList(GLuint list, GLenum mode)
{
    if (gl.inBegin || (gl.listName != 0)) { setError(GL_INVALID_OPERATION); return; }
    if (list == 0) { setError(GL_INVALID_VALUE); return; }
    if ((mode != GL_COMPILE) && (mode != GL_COMPILE_AND_EXECUTE)) { setError(GL_INVALID_ENUM); return; }

    gl.listName = list;
    gl.listMode = mode;
    gl.listWordCount = 0;
    gl.listCompiling = true;
}

// The list gets its new commands only now: until then, calling it (from itself too) executes the old ones
void glEndList(void)
{
    if (gl.listName == 0) { setError(GL_INVALID_OPERATION); return; }
    GLuint name = gl.listName;
    gl.listName = 0;
    gl.listCompiling = false;

    int index;
    DisplayList *l = findList(name, &index);
    if ((l == NULL) && insertLists(index, name, 1)) l = &gl.lists[index];
    if (l == NULL) return;

    // The recorded words become the list, trimmed to size; the next glNewList starts a new buffer
    free(l->words);
    l->words = NULL;
    l->count = gl.listWordCount;
    if (l->count > 0)
    {
        l->words = realloc(gl.listWords, (size_t)l->count*sizeof(ListWord));
        if (l->words == NULL) l->words = gl.listWords;
    }
    else free(gl.listWords);
    gl.listWords = NULL;
    gl.listWordCount = gl.listWordCapacity = 0;
}

#ifdef C3DGL_PROFILE
static void glCallListBody(GLuint list)
#else
void glCallList(GLuint list)
#endif
{
    LIST_SAVE(CALL_LIST, "u", list);
    executeList(list);
}

#ifdef C3DGL_PROFILE
void glCallList(GLuint list)
{
    PROF_ENTER();
    glCallListBody(list);
    PROF_LEAVE(PB_LISTS, 0);
}
#endif

// Bytes per name of a glCallLists type, 0 if the type is invalid
static int callListsSize(GLenum type)
{
    switch (type)
    {
        case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
        case GL_SHORT: case GL_UNSIGNED_SHORT: case GL_2_BYTES: return 2;
        case GL_3_BYTES: return 3;
        case GL_INT: case GL_UNSIGNED_INT: case GL_FLOAT: case GL_4_BYTES: return 4;
        default: return 0;
    }
}

// Name i of glCallLists, before the list base is added (GL_n_BYTES: big-endian)
static GLuint callListsName(GLenum type, const GLvoid *lists, int i)
{
    const u8 *p = (const u8 *)lists + (size_t)i*callListsSize(type);
    switch (type)
    {
        case GL_BYTE: return (GLuint)(GLint)*(const s8 *)p;
        case GL_UNSIGNED_BYTE: return *p;
        case GL_SHORT: { s16 v; memcpy(&v, p, 2); return (GLuint)(GLint)v; }
        case GL_UNSIGNED_SHORT: { u16 v; memcpy(&v, p, 2); return v; }
        case GL_INT: case GL_UNSIGNED_INT: { u32 v; memcpy(&v, p, 4); return v; }
        case GL_FLOAT: { float v; memcpy(&v, p, 4); return (GLuint)(GLint)v; }
        case GL_2_BYTES: return ((GLuint)p[0] << 8) | p[1];
        case GL_3_BYTES: return ((GLuint)p[0] << 16) | ((GLuint)p[1] << 8) | p[2];
        default: return ((GLuint)p[0] << 24) | ((GLuint)p[1] << 16) | ((GLuint)p[2] << 8) | p[3];    // GL_4_BYTES
    }
}

#ifdef C3DGL_PROFILE
static void glCallListsBody(GLsizei n, GLenum type, const GLvoid *lists)
#else
void glCallLists(GLsizei n, GLenum type, const GLvoid *lists)
#endif
{
    int size = callListsSize(type);
    if (gl.listCompiling)
    {
        // The names are read now (as GL_UNSIGNED_INT); an invalid call is recorded without them and fails when executed
        bool valid = (n >= 0) && (size > 0);
        ListWord *w = listBegin(LIST_CALL_LISTS, 2 + (valid? n : 0));
        if (w == NULL) return;
        w[0].i = n;
        w[1].u = valid? GL_UNSIGNED_INT : type;
        for (int i = 0; valid && (i < n); i++) w[2 + i].u = callListsName(type, lists, i);
        listEnd();
        return;
    }
    if (n < 0) { setError(GL_INVALID_VALUE); return; }
    if (size == 0) { setError(GL_INVALID_ENUM); return; }

    // The base is read per name: a called list may change it
    for (int i = 0; i < n; i++) executeList(gl.listBase + callListsName(type, lists, i));
}

#ifdef C3DGL_PROFILE
void glCallLists(GLsizei n, GLenum type, const GLvoid *lists)
{
    PROF_ENTER();
    glCallListsBody(n, type, lists);
    PROF_LEAVE(PB_LISTS, 0);
}
#endif

void glListBase(GLuint base)
{
    LIST_SAVE(LIST_BASE, "u", base);
    gl.listBase = base;
}

// The first `range` consecutive unused names; they become empty lists. 0 if there is no such range
GLuint glGenLists(GLsizei range)
{
    if (range < 0) { setError(GL_INVALID_VALUE); return 0; }
    if (range == 0) return 0;

    GLuint base = 1;
    int index = 0;
    for (; index < gl.listCount; index++)
    {
        if (gl.lists[index].name - base >= (GLuint)range) break;      // Names before index are all < base
        base = gl.lists[index].name + 1;
    }
    if ((base == 0) || ((GLuint)range - 1 > 0xFFFFFFFFu - base) || !insertLists(index, base, range)) return 0;
    return base;
}

void glDeleteLists(GLuint list, GLsizei range)
{
    if (range < 0) { setError(GL_INVALID_VALUE); return; }

    int first, last;
    findList(list, &first);
    u64 end = (u64)list + (u64)range;
    for (last = first; (last < gl.listCount) && (gl.lists[last].name < end); last++) free(gl.lists[last].words);
    memmove(&gl.lists[first], &gl.lists[last], (size_t)(gl.listCount - last)*sizeof(DisplayList));
    gl.listCount -= last - first;
}

GLboolean glIsList(GLuint list) { return findList(list, NULL) != NULL; }
