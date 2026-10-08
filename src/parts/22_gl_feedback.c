// OpenGL: glRenderMode, feedback and selection
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// OpenGL: feedback and selection (GL). glRenderMode, glFeedbackBuffer and glSelectBuffer are executed immediately (not
// compiled into display lists); the primitives are handled in the feedback section
//----------------------------------------------------------------------------------
// Selection: a hit record (name stack depth, min and max window depth scaled to 2^32 - 1, the names), written when the
// name stack changes or selection mode ends after a hit
static void selectValue(GLuint value)
{
    if (gl.selectCount < gl.selectSize) gl.selectBuffer[gl.selectCount] = value;
    if (gl.selectCount <= gl.selectSize) gl.selectCount++;
}

static void writeHitRecord(void)
{
    selectValue((GLuint)gl.nameDepth);
    selectValue((GLuint)(gl.hitMinZ*4294967295.0));
    selectValue((GLuint)(gl.hitMaxZ*4294967295.0));
    for (int i = 0; i < gl.nameDepth; i++) selectValue(gl.names[i]);
    gl.hits++;
    gl.hit = false;
    gl.hitMinZ = 1.0f;
    gl.hitMaxZ = 0.0f;
}

GLint glRenderMode(GLenum mode)
{
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return 0; }
    if ((mode != GL_RENDER) && (mode != GL_FEEDBACK) && (mode != GL_SELECT)) { setError(GL_INVALID_ENUM); return 0; }
    if (((mode == GL_FEEDBACK) && !gl.feedbackBufferSet) || ((mode == GL_SELECT) && !gl.selectBufferSet))
    {
        setError(GL_INVALID_OPERATION);
        return 0;
    }

    // Leaving a mode returns its result: the number of hit records or feedback values, -1 on overflow
    GLint result = 0;
    if (gl.renderMode == GL_SELECT)
    {
        if (gl.hit) writeHitRecord();
        result = (gl.selectCount > gl.selectSize)? -1 : gl.hits;
    }
    else if (gl.renderMode == GL_FEEDBACK) result = (gl.feedbackCount > gl.feedbackSize)? -1 : gl.feedbackCount;

    gl.renderMode = mode;
    gl.feedbackCount = gl.selectCount = gl.hits = 0;
    gl.hit = false;
    gl.hitMinZ = 1.0f;
    gl.hitMaxZ = 0.0f;
    gl.nameDepth = 0;
    return result;
}

void glFeedbackBuffer(GLsizei size, GLenum type, GLfloat *buffer)
{
    if (gl.inBegin || (gl.renderMode == GL_FEEDBACK)) { setError(GL_INVALID_OPERATION); return; }
    if ((type != GL_2D) && (type != GL_3D) && (type != GL_3D_COLOR) && (type != GL_3D_COLOR_TEXTURE) &&
        (type != GL_4D_COLOR_TEXTURE)) { setError(GL_INVALID_ENUM); return; }
    if (size < 0) { setError(GL_INVALID_VALUE); return; }
    gl.feedbackBuffer = buffer;
    gl.feedbackSize = (buffer != NULL)? size : 0;
    gl.feedbackType = type;
    gl.feedbackBufferSet = true;
}

void glSelectBuffer(GLsizei size, GLuint *buffer)
{
    if (gl.inBegin || (gl.renderMode == GL_SELECT)) { setError(GL_INVALID_OPERATION); return; }
    if (size < 0) { setError(GL_INVALID_VALUE); return; }
    gl.selectBuffer = buffer;
    gl.selectSize = (buffer != NULL)? size : 0;
    gl.selectBufferSet = true;
}

void glPassThrough(GLfloat token)
{
    LIST_SAVE(PASS_THROUGH, "f", token);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if (gl.renderMode != GL_FEEDBACK) return;
    feedbackValue((GLfloat)GL_PASS_THROUGH_TOKEN);
    feedbackValue(token);
}

// The name stack commands are ignored outside selection mode
void glInitNames(void)
{
    LIST_SAVE(INIT_NAMES, "");
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if (gl.renderMode != GL_SELECT) return;
    if (gl.hit) writeHitRecord();
    gl.nameDepth = 0;
}

void glLoadName(GLuint name)
{
    LIST_SAVE(LOAD_NAME, "u", name);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if (gl.renderMode != GL_SELECT) return;
    if (gl.nameDepth == 0) { setError(GL_INVALID_OPERATION); return; }
    if (gl.hit) writeHitRecord();
    gl.names[gl.nameDepth - 1] = name;
}

void glPushName(GLuint name)
{
    LIST_SAVE(PUSH_NAME, "u", name);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if (gl.renderMode != GL_SELECT) return;
    if (gl.hit) writeHitRecord();
    if (gl.nameDepth == C3DGL_MAX_NAME_STACK) { setError(GL_STACK_OVERFLOW); return; }
    gl.names[gl.nameDepth++] = name;
}

void glPopName(void)
{
    LIST_SAVE(POP_NAME, "");
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if (gl.renderMode != GL_SELECT) return;
    if (gl.hit) writeHitRecord();
    if (gl.nameDepth == 0) { setError(GL_STACK_UNDERFLOW); return; }
    gl.nameDepth--;
}
