// OpenGL: capabilities and state
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// OpenGL: state
//----------------------------------------------------------------------------------
static void setCapability(GLenum cap, bool enable)
{
    LIST_SAVE(ENABLE, "ui", cap, (int)enable);
    litStateChanged();          // Lights, GL_NORMALIZE, ...
    int bit = ignoredCapBit(cap);
    if (bit >= 0)
    {
        if (enable) gl.ignoredCaps |= 1u << bit;
        else gl.ignoredCaps &= ~(1u << bit);
        return;
    }

    switch (cap)
    {
        case GL_TEXTURE_1D: gl.texture1D[gl.activeTexture] = enable; break;
        case GL_TEXTURE_2D: gl.texture2D[gl.activeTexture] = enable; break;
        case GL_TEXTURE_GEN_S: case GL_TEXTURE_GEN_T: case GL_TEXTURE_GEN_R: case GL_TEXTURE_GEN_Q:
            if (enable) gl.texGen[gl.activeTexture].enabled |= 1u << (cap - GL_TEXTURE_GEN_S);
            else gl.texGen[gl.activeTexture].enabled &= ~(1u << (cap - GL_TEXTURE_GEN_S));
            gl.texGenSerial++;
            break;
        case GL_BLEND: gl.state.blend = enable; break;
        case GL_COLOR_LOGIC_OP: gl.state.logicOp = enable; break;
        case GL_DEPTH_TEST: gl.state.depthTest = enable; break;
        case GL_ALPHA_TEST: gl.state.alphaTest = enable; break;
        case GL_STENCIL_TEST:
            gl.state.stencilTest = enable;
            if (enable) gl.stencilUsed = true;
            break;
        case GL_CULL_FACE: gl.state.cull = enable; break;
        case GL_SCISSOR_TEST: gl.state.scissor = enable; break;
        case GL_FOG: gl.fog = enable; break;
        case GL_CLIP_PLANE0: case GL_CLIP_PLANE1: case GL_CLIP_PLANE2:
        case GL_CLIP_PLANE3: case GL_CLIP_PLANE4: case GL_CLIP_PLANE5:
            if (enable) gl.clipEnabled |= 1u << (cap - GL_CLIP_PLANE0);
            else gl.clipEnabled &= ~(1u << (cap - GL_CLIP_PLANE0));
            gl.clipObjectSerial = 0;
            break;
        case GL_LIGHTING: gl.lightingEnabled = enable; break;
        case GL_LIGHT0: case GL_LIGHT1: case GL_LIGHT2: case GL_LIGHT3:
        case GL_LIGHT4: case GL_LIGHT5: case GL_LIGHT6: case GL_LIGHT7:
            if (enable) gl.lightEnabled |= 1u << (cap - GL_LIGHT0);
            else gl.lightEnabled &= ~(1u << (cap - GL_LIGHT0));
            break;
        case GL_COLOR_MATERIAL:
            gl.colorMaterial = enable;
            if (enable) applyColorMaterial(gl.current.color);     // The material follows the current color from now on
            break;
        case GL_NORMALIZE: gl.normalize = enable; break;
        case GL_RESCALE_NORMAL: gl.rescaleNormal = enable; break;
        case GL_AUTO_NORMAL: gl.autoNormal = enable; break;
        case GL_MAP1_COLOR_4: case GL_MAP1_INDEX: case GL_MAP1_NORMAL: case GL_MAP1_TEXTURE_COORD_1:
        case GL_MAP1_TEXTURE_COORD_2: case GL_MAP1_TEXTURE_COORD_3: case GL_MAP1_TEXTURE_COORD_4:
        case GL_MAP1_VERTEX_3: case GL_MAP1_VERTEX_4:
            gl.map1[cap - GL_MAP1_COLOR_4].enabled = enable;
            break;
        case GL_MAP2_COLOR_4: case GL_MAP2_INDEX: case GL_MAP2_NORMAL: case GL_MAP2_TEXTURE_COORD_1:
        case GL_MAP2_TEXTURE_COORD_2: case GL_MAP2_TEXTURE_COORD_3: case GL_MAP2_TEXTURE_COORD_4:
        case GL_MAP2_VERTEX_3: case GL_MAP2_VERTEX_4:
            gl.map2[cap - GL_MAP2_COLOR_4].enabled = enable;
            break;
        case GL_POLYGON_OFFSET_FILL: gl.offsetFill = enable; break;
        case GL_POLYGON_OFFSET_LINE: gl.offsetLine = enable; break;
        case GL_POLYGON_OFFSET_POINT: gl.offsetPoint = enable; break;
        case GL_POINT_SPRITE_OES: gl.pointSprite = enable; break;
        case GL_LINE_STIPPLE: gl.lineStipple = enable; break;
        case GL_POLYGON_STIPPLE: gl.polygonStipple = enable; break;
        default:
            WARN_ONCE("glEnable/glDisable: capability 0x%x not supported\n", cap);
            setError(GL_INVALID_ENUM);
            break;
    }
}

void glEnable(GLenum cap) { setCapability(cap, true); }
void glDisable(GLenum cap) { setCapability(cap, false); }

static void setClientState(GLenum array, bool enable)
{
    switch (array)
    {
        case GL_VERTEX_ARRAY: gl.arrays[ARRAY_VERTEX].enabled = enable; break;
        case GL_TEXTURE_COORD_ARRAY: gl.arrays[ARRAY_TEXCOORD].enabled = enable; break;
        case GL_COLOR_ARRAY: gl.arrays[ARRAY_COLOR].enabled = enable; break;
        case GL_EDGE_FLAG_ARRAY: gl.arrays[ARRAY_EDGEFLAG].enabled = enable; break;
        case GL_NORMAL_ARRAY: gl.arrays[ARRAY_NORMAL].enabled = enable; break;
        case GL_POINT_SIZE_ARRAY_OES: gl.arrays[ARRAY_POINTSIZE].enabled = enable; break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

void glEnableClientState(GLenum array) { setClientState(array, true); }
void glDisableClientState(GLenum array) { setClientState(array, false); }

GLboolean glIsEnabled(GLenum cap)
{
    int bit = ignoredCapBit(cap);
    if (bit >= 0) return (gl.ignoredCaps >> bit) & 1;

    switch (cap)
    {
        case GL_TEXTURE_1D: return gl.texture1D[gl.activeTexture];
        case GL_TEXTURE_2D: return gl.texture2D[gl.activeTexture];
        case GL_TEXTURE_GEN_S: case GL_TEXTURE_GEN_T: case GL_TEXTURE_GEN_R: case GL_TEXTURE_GEN_Q:
            return (gl.texGen[gl.activeTexture].enabled >> (cap - GL_TEXTURE_GEN_S)) & 1;
        case GL_BLEND: return gl.state.blend;
        case GL_COLOR_LOGIC_OP: return gl.state.logicOp;
        case GL_DEPTH_TEST: return gl.state.depthTest;
        case GL_ALPHA_TEST: return gl.state.alphaTest;
        case GL_STENCIL_TEST: return gl.state.stencilTest;
        case GL_CULL_FACE: return gl.state.cull;
        case GL_SCISSOR_TEST: return gl.state.scissor;
        case GL_FOG: return gl.fog;
        case GL_CLIP_PLANE0: case GL_CLIP_PLANE1: case GL_CLIP_PLANE2:
        case GL_CLIP_PLANE3: case GL_CLIP_PLANE4: case GL_CLIP_PLANE5:
            return (gl.clipEnabled >> (cap - GL_CLIP_PLANE0)) & 1;
        case GL_LIGHTING: return gl.lightingEnabled;
        case GL_LIGHT0: case GL_LIGHT1: case GL_LIGHT2: case GL_LIGHT3:
        case GL_LIGHT4: case GL_LIGHT5: case GL_LIGHT6: case GL_LIGHT7:
            return (gl.lightEnabled >> (cap - GL_LIGHT0)) & 1;
        case GL_COLOR_MATERIAL: return gl.colorMaterial;
        case GL_NORMALIZE: return gl.normalize;
        case GL_RESCALE_NORMAL: return gl.rescaleNormal;
        case GL_AUTO_NORMAL: return gl.autoNormal;
        case GL_MAP1_COLOR_4: case GL_MAP1_INDEX: case GL_MAP1_NORMAL: case GL_MAP1_TEXTURE_COORD_1:
        case GL_MAP1_TEXTURE_COORD_2: case GL_MAP1_TEXTURE_COORD_3: case GL_MAP1_TEXTURE_COORD_4:
        case GL_MAP1_VERTEX_3: case GL_MAP1_VERTEX_4:
            return gl.map1[cap - GL_MAP1_COLOR_4].enabled;
        case GL_MAP2_COLOR_4: case GL_MAP2_INDEX: case GL_MAP2_NORMAL: case GL_MAP2_TEXTURE_COORD_1:
        case GL_MAP2_TEXTURE_COORD_2: case GL_MAP2_TEXTURE_COORD_3: case GL_MAP2_TEXTURE_COORD_4:
        case GL_MAP2_VERTEX_3: case GL_MAP2_VERTEX_4:
            return gl.map2[cap - GL_MAP2_COLOR_4].enabled;
        case GL_VERTEX_ARRAY: return gl.arrays[ARRAY_VERTEX].enabled;
        case GL_TEXTURE_COORD_ARRAY: return gl.arrays[ARRAY_TEXCOORD].enabled;
        case GL_COLOR_ARRAY: return gl.arrays[ARRAY_COLOR].enabled;
        case GL_EDGE_FLAG_ARRAY: return gl.arrays[ARRAY_EDGEFLAG].enabled;
        case GL_NORMAL_ARRAY: return gl.arrays[ARRAY_NORMAL].enabled;
        case GL_POINT_SIZE_ARRAY_OES: return gl.arrays[ARRAY_POINTSIZE].enabled;
        case GL_POLYGON_OFFSET_FILL: return gl.offsetFill;
        case GL_POLYGON_OFFSET_LINE: return gl.offsetLine;
        case GL_POLYGON_OFFSET_POINT: return gl.offsetPoint;
        case GL_POINT_SPRITE_OES: return gl.pointSprite;
        case GL_LINE_STIPPLE: return gl.lineStipple;
        case GL_POLYGON_STIPPLE: return gl.polygonStipple;
        default: setError(GL_INVALID_ENUM); return GL_FALSE;
    }
}

GLenum glGetError(void)
{
    GLenum error = gl.error;
    gl.error = GL_NO_ERROR;
    return error;
}

// Draws are submitted at c3dglSwapBuffers(); flushing the batch is all that can be done earlier
void glFlush(void) { if (gl.frameActive) flush(); }
void glFinish(void) { glFlush(); }

void glHint(GLenum target, GLenum mode) { (void)target; (void)mode; }

void glShadeModel(GLenum mode)
{
    LIST_SAVE(SHADE_MODEL, "u", mode);
    if ((mode != GL_SMOOTH) && (mode != GL_FLAT)) { setError(GL_INVALID_ENUM); return; }
    gl.shadeModel = mode;
}

void glPixelStorei(GLenum pname, GLint param)
{
    bool unpack = (pname >= GL_UNPACK_SWAP_BYTES) && (pname <= GL_UNPACK_ALIGNMENT);
    if ((pname == GL_UNPACK_IMAGE_HEIGHT) || (pname == GL_UNPACK_SKIP_IMAGES)) unpack = true;
    PixelStore *ps = unpack? &gl.unpack : &gl.pack;

    switch (pname)
    {
        case GL_UNPACK_ALIGNMENT: case GL_PACK_ALIGNMENT:
            if ((param != 1) && (param != 2) && (param != 4) && (param != 8)) { setError(GL_INVALID_VALUE); return; }
            ps->alignment = param;
            return;
        case GL_UNPACK_SWAP_BYTES: case GL_PACK_SWAP_BYTES: ps->swapBytes = (param != 0); return;
        case GL_UNPACK_LSB_FIRST: case GL_PACK_LSB_FIRST: ps->lsbFirst = (param != 0); return;     // Only for GL_BITMAP
        default: break;
    }

    if (param < 0) { setError(GL_INVALID_VALUE); return; }
    switch (pname)
    {
        case GL_UNPACK_ROW_LENGTH: case GL_PACK_ROW_LENGTH: ps->rowLength = param; break;
        case GL_UNPACK_SKIP_ROWS: case GL_PACK_SKIP_ROWS: ps->skipRows = param; break;
        case GL_UNPACK_SKIP_PIXELS: case GL_PACK_SKIP_PIXELS: ps->skipPixels = param; break;
        case GL_UNPACK_IMAGE_HEIGHT: case GL_PACK_IMAGE_HEIGHT: ps->imageHeight = param; break;     // GL 1.2, no 3D textures
        case GL_UNPACK_SKIP_IMAGES: case GL_PACK_SKIP_IMAGES: ps->skipImages = param; break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

void glPixelStoref(GLenum pname, GLfloat param) { glPixelStorei(pname, (GLint)lroundf(param)); }

// Component (R, G, B, A, depth) of a glPixelTransfer scale/bias, -1 for the other pnames
static int transferComponent(GLenum pname)
{
    switch (pname)
    {
        case GL_RED_SCALE: case GL_RED_BIAS: return 0;
        case GL_GREEN_SCALE: case GL_GREEN_BIAS: return 1;
        case GL_BLUE_SCALE: case GL_BLUE_BIAS: return 2;
        case GL_ALPHA_SCALE: case GL_ALPHA_BIAS: return 3;
        case GL_DEPTH_SCALE: case GL_DEPTH_BIAS: return 4;
        default: return -1;
    }
}

static bool transferIsBias(GLenum pname)
{
    return (pname == GL_RED_BIAS) || (pname == GL_GREEN_BIAS) || (pname == GL_BLUE_BIAS) || (pname == GL_ALPHA_BIAS) ||
           (pname == GL_DEPTH_BIAS);
}

void glPixelTransferf(GLenum pname, GLfloat param)
{
    LIST_SAVE(PIXEL_TRANSFER, "uf", pname, param);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    PixelTransfer *t = &gl.transfer;
    int i = transferComponent(pname);
    if (i >= 0)
    {
        (transferIsBias(pname)? t->bias : t->scale)[i] = param;
        return;
    }
    switch (pname)
    {
        case GL_MAP_COLOR: t->mapColor = (param != 0.0f); break;
        case GL_MAP_STENCIL: t->mapStencil = (param != 0.0f); break;
        case GL_INDEX_SHIFT: t->indexShift = (GLint)lroundf(param); break;
        case GL_INDEX_OFFSET: t->indexOffset = (GLint)lroundf(param); break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

void glPixelTransferi(GLenum pname, GLint param) { glPixelTransferf(pname, (GLfloat)param); }

// Table of a GL_PIXEL_MAP_* enum, -1 if map is not one
static int pixelMapIndex(GLenum map)
{
    return ((map >= GL_PIXEL_MAP_I_TO_I) && (map <= GL_PIXEL_MAP_A_TO_A))? (int)(map - GL_PIXEL_MAP_I_TO_I) : -1;
}

// glPixelMap with the values as floats (see pixelMap()). Tables that are looked up by index (I_TO_*, S_TO_S) need 2^n
// entries; color components are clamped to [0, 1]
static void setPixelMap(GLenum map, GLsizei mapsize, const GLfloat *values)
{
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    int m = pixelMapIndex(map);
    if (m < 0) { setError(GL_INVALID_ENUM); return; }
    bool pow2 = (m < PIXEL_MAP_R_TO_R);
    if ((mapsize < 1) || (mapsize > C3DGL_MAX_PIXEL_MAP_TABLE) || (pow2 && (mapsize & (mapsize - 1))))
    {
        setError(GL_INVALID_VALUE);
        return;
    }
    PixelMap *p = &gl.pixelMaps[m];
    p->size = mapsize;
    for (int i = 0; i < mapsize; i++)
    {
        float v = values[i];
        p->values[i] = (m < PIXEL_MAP_I_TO_R)? v : !(v > 0.0f)? 0.0f : (v > 1.0f)? 1.0f : v;
    }
}

// glPixelMap{fv,uiv,usv}: unsigned integers of the color tables are normalized (GL 1.1 table 2.9), index tables take
// them as they are. Recorded in display lists with the converted values (an invalid size without them)
static void pixelMap(GLenum map, GLsizei mapsize, const void *values, GLenum type)
{
    int m = pixelMapIndex(map), count = ((mapsize > 0) && (mapsize <= C3DGL_MAX_PIXEL_MAP_TABLE))? mapsize : 0;
    float v[C3DGL_MAX_PIXEL_MAP_TABLE];
    for (int i = 0; i < count; i++)
    {
        if (type == GL_FLOAT) v[i] = ((const GLfloat *)values)[i];
        else if (type == GL_UNSIGNED_INT) v[i] = (float)((m >= PIXEL_MAP_I_TO_R)? ((const GLuint *)values)[i]/4294967295.0 : ((const GLuint *)values)[i]);
        else v[i] = (m >= PIXEL_MAP_I_TO_R)? ((const GLushort *)values)[i]/65535.0f : ((const GLushort *)values)[i];
    }
    if (gl.listCompiling) listSave(LIST_PIXEL_MAP, "uiF", map, mapsize, count, v);
    else setPixelMap(map, mapsize, v);
}

void glPixelMapfv(GLenum map, GLsizei mapsize, const GLfloat *values) { pixelMap(map, mapsize, values, GL_FLOAT); }
void glPixelMapuiv(GLenum map, GLsizei mapsize, const GLuint *values) { pixelMap(map, mapsize, values, GL_UNSIGNED_INT); }
void glPixelMapusv(GLenum map, GLsizei mapsize, const GLushort *values) { pixelMap(map, mapsize, values, GL_UNSIGNED_SHORT); }

// glGetPixelMap{fv,uiv,usv}: color components scaled to the integer range, indices rounded
static void getPixelMap(GLenum map, void *values, GLenum type)
{
    int m = pixelMapIndex(map);
    if (m < 0) { setError(GL_INVALID_ENUM); return; }
    const PixelMap *p = &gl.pixelMaps[m];
    bool color = (m >= PIXEL_MAP_I_TO_R);
    for (int i = 0; i < p->size; i++)
    {
        double v = p->values[i];
        if (type == GL_FLOAT) ((GLfloat *)values)[i] = (GLfloat)v;
        else if (type == GL_UNSIGNED_INT) ((GLuint *)values)[i] = (GLuint)(s64)llround(color? v*4294967295.0 : v);
        else ((GLushort *)values)[i] = (GLushort)(s64)llround(color? v*65535.0 : v);
    }
}

void glGetPixelMapfv(GLenum map, GLfloat *values) { getPixelMap(map, values, GL_FLOAT); }
void glGetPixelMapuiv(GLenum map, GLuint *values) { getPixelMap(map, values, GL_UNSIGNED_INT); }
void glGetPixelMapusv(GLenum map, GLushort *values) { getPixelMap(map, values, GL_UNSIGNED_SHORT); }

// State for glGet*: fills v and returns the number of values (0: unknown pname).
// *normalized: the values are colors/depths in [0, 1], which integer queries scale to [0, INT_MAX]
static int getState(GLenum pname, double v[16], bool *normalized)
{
    *normalized = false;

    switch (pname)
    {
        case GL_MODELVIEW_MATRIX:
        case GL_PROJECTION_MATRIX:
        case GL_TEXTURE_MATRIX:
        {
            int mode = pname - GL_MODELVIEW_MATRIX;
            if (mode == 2) mode += gl.activeTexture;
            for (int i = 0; i < 16; i++) v[i] = gl.stack[mode][gl.stackDepth[mode]].m[i];
            return 16;
        }
        case GL_MODELVIEW_STACK_DEPTH: v[0] = gl.stackDepth[0] + 1; return 1;
        case GL_PROJECTION_STACK_DEPTH: v[0] = gl.stackDepth[1] + 1; return 1;
        case GL_TEXTURE_STACK_DEPTH: v[0] = gl.stackDepth[2 + gl.activeTexture] + 1; return 1;
        case GL_MAX_MODELVIEW_STACK_DEPTH:
        case GL_MAX_PROJECTION_STACK_DEPTH:
        case GL_MAX_TEXTURE_STACK_DEPTH: v[0] = C3DGL_MATRIX_STACK; return 1;
        case GL_MATRIX_MODE: v[0] = GL_MODELVIEW + gl.matrixMode; return 1;

        case GL_VIEWPORT: for (int i = 0; i < 4; i++) v[i] = gl.state.viewport[i]; return 4;
        case GL_SCISSOR_BOX: for (int i = 0; i < 4; i++) v[i] = gl.state.scissorBox[i]; return 4;
        case GL_MAX_VIEWPORT_DIMS: v[0] = C3DGL_TOP_SCREEN_WIDTH; v[1] = C3DGL_SCREEN_HEIGHT; return 2;
        case GL_DEPTH_RANGE: v[0] = gl.state.depthNear; v[1] = gl.state.depthFar; *normalized = true; return 2;
        case GL_POLYGON_MODE: v[0] = gl.polygonMode[0]; v[1] = gl.polygonMode[1]; return 2;
        case GL_POLYGON_OFFSET_FACTOR: v[0] = gl.offsetFactor; return 1;
        case GL_POLYGON_OFFSET_UNITS: v[0] = gl.offsetUnits; return 1;
        case GL_EDGE_FLAG: v[0] = gl.currentEdge; return 1;

        case GL_CURRENT_COLOR: for (int i = 0; i < 4; i++) v[i] = gl.current.color[i]/255.0; *normalized = true; return 4;
        case GL_CURRENT_TEXTURE_COORDS:
        {
            const float *tc = (gl.activeTexture == 0)? gl.current.tex : gl.current.texExtra[gl.activeTexture - 1];
            v[0] = tc[0]; v[1] = tc[1]; v[2] = gl.currentTexR[gl.activeTexture]; v[3] = tc[2];
            return 4;
        }
        case GL_CURRENT_RASTER_POSITION: for (int i = 0; i < 4; i++) v[i] = gl.raster.pos[i]; return 4;
        case GL_CURRENT_RASTER_POSITION_VALID: v[0] = gl.raster.valid; return 1;
        case GL_CURRENT_RASTER_COLOR: for (int i = 0; i < 4; i++) v[i] = gl.raster.color[i]/255.0; *normalized = true; return 4;
        case GL_CURRENT_RASTER_TEXTURE_COORDS: for (int i = 0; i < 4; i++) v[i] = gl.raster.tex[i]; return 4;
        case GL_CURRENT_RASTER_DISTANCE: v[0] = gl.raster.distance; return 1;
        case GL_CURRENT_RASTER_INDEX: v[0] = 1; return 1;       // Color index mode only
        case GL_ZOOM_X: v[0] = gl.zoomX; return 1;
        case GL_ZOOM_Y: v[0] = gl.zoomY; return 1;
        case GL_MAP_COLOR: v[0] = gl.transfer.mapColor; return 1;
        case GL_MAP_STENCIL: v[0] = gl.transfer.mapStencil; return 1;
        case GL_INDEX_SHIFT: v[0] = gl.transfer.indexShift; return 1;
        case GL_INDEX_OFFSET: v[0] = gl.transfer.indexOffset; return 1;
        case GL_RED_SCALE: case GL_GREEN_SCALE: case GL_BLUE_SCALE: case GL_ALPHA_SCALE: case GL_DEPTH_SCALE:
        case GL_RED_BIAS: case GL_GREEN_BIAS: case GL_BLUE_BIAS: case GL_ALPHA_BIAS: case GL_DEPTH_BIAS:
        {
            int i = transferComponent(pname);
            v[0] = (transferIsBias(pname)? gl.transfer.bias : gl.transfer.scale)[i];
            return 1;
        }
        case GL_MAX_PIXEL_MAP_TABLE: v[0] = C3DGL_MAX_PIXEL_MAP_TABLE; return 1;
        case GL_PIXEL_MAP_I_TO_I_SIZE: case GL_PIXEL_MAP_S_TO_S_SIZE: case GL_PIXEL_MAP_I_TO_R_SIZE:
        case GL_PIXEL_MAP_I_TO_G_SIZE: case GL_PIXEL_MAP_I_TO_B_SIZE: case GL_PIXEL_MAP_I_TO_A_SIZE:
        case GL_PIXEL_MAP_R_TO_R_SIZE: case GL_PIXEL_MAP_G_TO_G_SIZE: case GL_PIXEL_MAP_B_TO_B_SIZE:
        case GL_PIXEL_MAP_A_TO_A_SIZE: v[0] = gl.pixelMaps[pname - GL_PIXEL_MAP_I_TO_I_SIZE].size; return 1;
        case GL_ACTIVE_TEXTURE: v[0] = GL_TEXTURE0 + gl.activeTexture; return 1;
        case GL_CLIENT_ACTIVE_TEXTURE: v[0] = GL_TEXTURE0 + gl.clientActiveTexture; return 1;
        case GL_MAX_TEXTURE_UNITS: v[0] = C3DGL_TEXTURE_UNITS; return 1;
        case GL_ATTRIB_STACK_DEPTH: v[0] = gl.attribDepth; return 1;
        case GL_MAX_EVAL_ORDER: v[0] = C3DGL_MAX_EVAL_ORDER; return 1;
        case GL_MAP1_GRID_DOMAIN: v[0] = gl.grid1u1; v[1] = gl.grid1u2; return 2;
        case GL_MAP1_GRID_SEGMENTS: v[0] = gl.grid1n; return 1;
        case GL_MAP2_GRID_DOMAIN: v[0] = gl.grid2u1; v[1] = gl.grid2u2; v[2] = gl.grid2v1; v[3] = gl.grid2v2; return 4;
        case GL_MAP2_GRID_SEGMENTS: v[0] = gl.grid2un; v[1] = gl.grid2vn; return 2;
        case GL_CLIENT_ATTRIB_STACK_DEPTH: v[0] = gl.clientAttribDepth; return 1;
        case GL_LIST_BASE: v[0] = gl.listBase; return 1;
        case GL_LIST_INDEX: v[0] = gl.listName; return 1;
        case GL_LIST_MODE: v[0] = gl.listName? gl.listMode : 0; return 1;
        case GL_MAX_LIST_NESTING: v[0] = C3DGL_MAX_LIST_NESTING; return 1;
        case GL_RENDER_MODE: v[0] = gl.renderMode; return 1;
        case GL_FEEDBACK_BUFFER_SIZE: v[0] = gl.feedbackSize; return 1;
        case GL_FEEDBACK_BUFFER_TYPE: v[0] = gl.feedbackType; return 1;
        case GL_SELECTION_BUFFER_SIZE: v[0] = gl.selectSize; return 1;
        case GL_NAME_STACK_DEPTH: v[0] = gl.nameDepth; return 1;
        case GL_MAX_NAME_STACK_DEPTH: v[0] = C3DGL_MAX_NAME_STACK; return 1;
        case GL_MAX_ATTRIB_STACK_DEPTH: case GL_MAX_CLIENT_ATTRIB_STACK_DEPTH: v[0] = C3DGL_ATTRIB_STACK; return 1;

        // Client arrays
        case GL_VERTEX_ARRAY_SIZE: v[0] = gl.arrays[ARRAY_VERTEX].size; return 1;
        case GL_VERTEX_ARRAY_TYPE: v[0] = gl.arrays[ARRAY_VERTEX].type; return 1;
        case GL_VERTEX_ARRAY_STRIDE: v[0] = gl.arrays[ARRAY_VERTEX].stride; return 1;
        case GL_NORMAL_ARRAY_TYPE: v[0] = gl.arrays[ARRAY_NORMAL].type; return 1;
        case GL_NORMAL_ARRAY_STRIDE: v[0] = gl.arrays[ARRAY_NORMAL].stride; return 1;
        case GL_COLOR_ARRAY_SIZE: v[0] = gl.arrays[ARRAY_COLOR].size; return 1;
        case GL_COLOR_ARRAY_TYPE: v[0] = gl.arrays[ARRAY_COLOR].type; return 1;
        case GL_COLOR_ARRAY_STRIDE: v[0] = gl.arrays[ARRAY_COLOR].stride; return 1;
        case GL_TEXTURE_COORD_ARRAY_SIZE: v[0] = gl.arrays[ARRAY_TEXCOORD].size; return 1;
        case GL_TEXTURE_COORD_ARRAY_TYPE: v[0] = gl.arrays[ARRAY_TEXCOORD].type; return 1;
        case GL_TEXTURE_COORD_ARRAY_STRIDE: v[0] = gl.arrays[ARRAY_TEXCOORD].stride; return 1;
        case GL_EDGE_FLAG_ARRAY_STRIDE: v[0] = gl.arrays[ARRAY_EDGEFLAG].stride; return 1;
        case GL_POINT_SIZE_ARRAY_TYPE_OES: v[0] = gl.arrays[ARRAY_POINTSIZE].type; return 1;
        case GL_POINT_SIZE_ARRAY_STRIDE_OES: v[0] = gl.arrays[ARRAY_POINTSIZE].stride; return 1;
        case GL_ARRAY_BUFFER_BINDING: v[0] = gl.arrayBuffer; return 1;
        case GL_ELEMENT_ARRAY_BUFFER_BINDING: v[0] = gl.elementArrayBuffer; return 1;
        case GL_VERTEX_ARRAY_BUFFER_BINDING: v[0] = gl.arrays[ARRAY_VERTEX].buffer; return 1;
        case GL_NORMAL_ARRAY_BUFFER_BINDING: v[0] = gl.arrays[ARRAY_NORMAL].buffer; return 1;
        case GL_COLOR_ARRAY_BUFFER_BINDING: v[0] = gl.arrays[ARRAY_COLOR].buffer; return 1;
        case GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING: v[0] = gl.arrays[ARRAY_TEXCOORD].buffer; return 1;
        case GL_EDGE_FLAG_ARRAY_BUFFER_BINDING: v[0] = gl.arrays[ARRAY_EDGEFLAG].buffer; return 1;
        case GL_POINT_SIZE_ARRAY_BUFFER_BINDING_OES: v[0] = gl.arrays[ARRAY_POINTSIZE].buffer; return 1;
        case GL_CURRENT_NORMAL: for (int i = 0; i < 3; i++) v[i] = gl.currentNormal[i]; return 3;

        case GL_COLOR_CLEAR_VALUE:
            for (int i = 0; i < 4; i++) v[i] = ((gl.clearColor >> (24 - 8*i)) & 0xFF)/255.0;
            *normalized = true;
            return 4;
        case GL_DEPTH_CLEAR_VALUE: v[0] = gl.clearDepth; *normalized = true; return 1;
        case GL_STENCIL_CLEAR_VALUE: v[0] = gl.clearStencil; return 1;
        case GL_ACCUM_CLEAR_VALUE: for (int i = 0; i < 4; i++) v[i] = gl.clearAccum[i]; *normalized = true; return 4;

        case GL_COLOR_WRITEMASK:
            v[0] = (gl.state.colorMask & GPU_WRITE_RED) != 0;
            v[1] = (gl.state.colorMask & GPU_WRITE_GREEN) != 0;
            v[2] = (gl.state.colorMask & GPU_WRITE_BLUE) != 0;
            v[3] = (gl.state.colorMask & GPU_WRITE_ALPHA) != 0;
            return 4;
        case GL_DEPTH_WRITEMASK: v[0] = gl.state.depthMask; return 1;
        case GL_DEPTH_FUNC: v[0] = gl.state.depthFunc; return 1;
        case GL_BLEND_SRC: v[0] = gl.state.blendSrc; return 1;
        case GL_BLEND_DST: v[0] = gl.state.blendDst; return 1;
        case GL_LOGIC_OP_MODE: v[0] = gl.state.logicOpMode; return 1;
        case GL_SAMPLE_COVERAGE_VALUE: v[0] = gl.sampleCoverage; return 1;
        case GL_SAMPLE_COVERAGE_INVERT: v[0] = gl.sampleCoverageInvert; return 1;
        case GL_SAMPLE_BUFFERS: case GL_SAMPLES: v[0] = 0; return 1;
        case GL_ALPHA_TEST_FUNC: v[0] = gl.state.alphaFunc; return 1;
        case GL_ALPHA_TEST_REF: v[0] = gl.state.alphaRef/255.0; *normalized = true; return 1;
        case GL_STENCIL_FUNC: v[0] = gl.state.stencilFunc; return 1;
        case GL_STENCIL_REF: v[0] = gl.state.stencilRef; return 1;
        case GL_STENCIL_VALUE_MASK: v[0] = gl.state.stencilFuncMask; return 1;
        case GL_STENCIL_WRITEMASK: v[0] = gl.state.stencilWriteMask; return 1;
        case GL_STENCIL_FAIL: v[0] = gl.state.stencilFail; return 1;
        case GL_STENCIL_PASS_DEPTH_FAIL: v[0] = gl.state.stencilDepthFail; return 1;
        case GL_STENCIL_PASS_DEPTH_PASS: v[0] = gl.state.stencilPass; return 1;
        case GL_CULL_FACE_MODE: v[0] = gl.state.cullFace; return 1;
        case GL_FRONT_FACE: v[0] = gl.state.frontFace; return 1;
        case GL_SHADE_MODEL: v[0] = gl.shadeModel; return 1;
        case GL_MAX_LIGHTS: v[0] = C3DGL_MAX_LIGHTS; return 1;
        case GL_MAX_CLIP_PLANES: v[0] = C3DGL_MAX_CLIP_PLANES; return 1;
        case GL_FOG_MODE: v[0] = gl.fogMode; return 1;
        case GL_FOG_DENSITY: v[0] = gl.fogDensity; return 1;
        case GL_FOG_START: v[0] = gl.fogStart; return 1;
        case GL_FOG_END: v[0] = gl.fogEnd; return 1;
        case GL_FOG_INDEX: v[0] = gl.fogIndex; return 1;
        case GL_FOG_COLOR: for (int i = 0; i < 4; i++) v[i] = gl.fogColor[i]; *normalized = true; return 4;
        case GL_LIGHT_MODEL_AMBIENT: for (int i = 0; i < 4; i++) v[i] = gl.lighting.modelAmbient[i]; *normalized = true; return 4;
        case GL_LIGHT_MODEL_LOCAL_VIEWER: v[0] = gl.lighting.localViewer; return 1;
        case GL_LIGHT_MODEL_TWO_SIDE: v[0] = gl.lighting.twoSide; return 1;
        case GL_COLOR_MATERIAL_FACE: v[0] = gl.lighting.colorMaterialFace; return 1;
        case GL_COLOR_MATERIAL_PARAMETER: v[0] = gl.lighting.colorMaterialMode; return 1;

        case GL_LINE_WIDTH: v[0] = gl.lineWidth; return 1;
        case GL_LINE_STIPPLE_PATTERN: v[0] = gl.lineStipplePattern; return 1;
        case GL_LINE_STIPPLE_REPEAT: v[0] = gl.lineStippleFactor; return 1;
        case GL_POINT_SIZE: v[0] = gl.pointSize; return 1;
        case GL_POINT_SIZE_RANGE: case GL_ALIASED_POINT_SIZE_RANGE: v[0] = 1.0; v[1] = C3DGL_MAX_POINT_SIZE; return 2;
        case GL_POINT_SIZE_GRANULARITY: v[0] = 0.0; return 1;     // Any size (points are quads)
        case GL_POINT_SIZE_MIN: v[0] = gl.pointSizeMin; return 1;
        case GL_POINT_SIZE_MAX: v[0] = gl.pointSizeMax; return 1;
        case GL_POINT_FADE_THRESHOLD_SIZE: v[0] = gl.pointFadeThreshold; return 1;
        case GL_POINT_DISTANCE_ATTENUATION: for (int i = 0; i < 3; i++) v[i] = gl.pointAttenuation[i]; return 3;
        case GL_UNPACK_ALIGNMENT: v[0] = gl.unpack.alignment; return 1;
        case GL_UNPACK_ROW_LENGTH: v[0] = gl.unpack.rowLength; return 1;
        case GL_UNPACK_SKIP_ROWS: v[0] = gl.unpack.skipRows; return 1;
        case GL_UNPACK_SKIP_PIXELS: v[0] = gl.unpack.skipPixels; return 1;
        case GL_UNPACK_SWAP_BYTES: v[0] = gl.unpack.swapBytes; return 1;
        case GL_UNPACK_LSB_FIRST: v[0] = gl.unpack.lsbFirst; return 1;
        case GL_UNPACK_IMAGE_HEIGHT: v[0] = gl.unpack.imageHeight; return 1;
        case GL_UNPACK_SKIP_IMAGES: v[0] = gl.unpack.skipImages; return 1;
        case GL_PACK_ALIGNMENT: v[0] = gl.pack.alignment; return 1;
        case GL_PACK_ROW_LENGTH: v[0] = gl.pack.rowLength; return 1;
        case GL_PACK_SKIP_ROWS: v[0] = gl.pack.skipRows; return 1;
        case GL_PACK_SKIP_PIXELS: v[0] = gl.pack.skipPixels; return 1;
        case GL_PACK_SWAP_BYTES: v[0] = gl.pack.swapBytes; return 1;
        case GL_PACK_LSB_FIRST: v[0] = gl.pack.lsbFirst; return 1;
        case GL_PACK_IMAGE_HEIGHT: v[0] = gl.pack.imageHeight; return 1;
        case GL_PACK_SKIP_IMAGES: v[0] = gl.pack.skipImages; return 1;
        case GL_TEXTURE_BINDING_1D: v[0] = gl.boundTexture1D[gl.activeTexture]; return 1;
        case GL_TEXTURE_BINDING_2D: v[0] = gl.boundTexture[gl.activeTexture]; return 1;
        case GL_DRAW_BUFFER: v[0] = gl.drawBuffer; return 1;
        case GL_READ_BUFFER: v[0] = gl.readBuffer; return 1;
        case GL_AUX_BUFFERS: v[0] = 0; return 1;
        case GL_DOUBLEBUFFER: v[0] = GL_TRUE; return 1;
        case GL_STEREO: v[0] = GL_FALSE; return 1;
        case GL_MAX_TEXTURE_SIZE: v[0] = C3DGL_MAX_TEXTURE_SIZE; return 1;
        case GL_NUM_COMPRESSED_TEXTURE_FORMATS: v[0] = COMPRESSED_FORMAT_COUNT; return 1;
        case GL_IMPLEMENTATION_COLOR_READ_TYPE_OES: v[0] = GL_UNSIGNED_BYTE; return 1;
        case GL_IMPLEMENTATION_COLOR_READ_FORMAT_OES: v[0] = GL_RGBA; return 1;
        case GL_COMPRESSED_TEXTURE_FORMATS: for (int i = 0; i < COMPRESSED_FORMAT_COUNT; i++) v[i] = compressedFormats[i]; return COMPRESSED_FORMAT_COUNT;

        // Render target: RGBA8 color, D24S8 depth/stencil; accumulation buffer: 16 bits per component, see glAccum()
        case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: v[0] = 8; return 1;
        case GL_DEPTH_BITS: v[0] = 24; return 1;
        case GL_STENCIL_BITS: v[0] = 8; return 1;
        case GL_ACCUM_RED_BITS: case GL_ACCUM_GREEN_BITS: case GL_ACCUM_BLUE_BITS: case GL_ACCUM_ALPHA_BITS: v[0] = 16; return 1;

        default:
            // Capabilities can be queried with glGet too
            if ((ignoredCapBit(pname) >= 0) || (pname == GL_TEXTURE_2D) || (pname == GL_BLEND) || (pname == GL_COLOR_LOGIC_OP) || (pname == GL_DEPTH_TEST) ||
                (pname == GL_ALPHA_TEST) || (pname == GL_STENCIL_TEST) || (pname == GL_CULL_FACE) || (pname == GL_SCISSOR_TEST) ||
                (pname == GL_VERTEX_ARRAY) || (pname == GL_TEXTURE_COORD_ARRAY) || (pname == GL_COLOR_ARRAY) || (pname == GL_NORMAL_ARRAY) ||
                (pname == GL_EDGE_FLAG_ARRAY) || (pname == GL_POINT_SIZE_ARRAY_OES) || (pname == GL_POLYGON_OFFSET_FILL) || (pname == GL_POLYGON_OFFSET_LINE) ||
                (pname == GL_POLYGON_OFFSET_POINT) || (pname == GL_AUTO_NORMAL) || (pname == GL_LIGHTING) || (pname == GL_FOG) ||
                ((pname >= GL_LIGHT0) && (pname <= GL_LIGHT7)) || (pname == GL_COLOR_MATERIAL) ||
                ((pname >= GL_CLIP_PLANE0) && (pname < GL_CLIP_PLANE0 + C3DGL_MAX_CLIP_PLANES)) ||
                (pname == GL_NORMALIZE) || (pname == GL_RESCALE_NORMAL) || (pname == GL_POINT_SPRITE_OES) ||
                (pname == GL_LINE_STIPPLE) || (pname == GL_POLYGON_STIPPLE) ||
                ((pname >= GL_TEXTURE_GEN_S) && (pname <= GL_TEXTURE_GEN_Q)) ||
                ((pname >= GL_MAP1_COLOR_4) && (pname <= GL_MAP1_VERTEX_4)) || ((pname >= GL_MAP2_COLOR_4) && (pname <= GL_MAP2_VERTEX_4)))
            {
                v[0] = glIsEnabled(pname);
                return 1;
            }
            WARN_ONCE("glGet: 0x%x not supported\n", pname);
            setError(GL_INVALID_ENUM);
            return 0;
    }
}

void glGetDoublev(GLenum pname, GLdouble *params)
{
    double v[16];
    bool normalized;
    int n = getState(pname, v, &normalized);
    for (int i = 0; i < n; i++) params[i] = v[i];
}

void glGetFloatv(GLenum pname, GLfloat *params)
{
    double v[16];
    bool normalized;
    int n = getState(pname, v, &normalized);
    for (int i = 0; i < n; i++) params[i] = (GLfloat)v[i];
}

// Integer query of a color/depth: [-1, 1] -> [-INT_MAX, INT_MAX], clamped (light colors may exceed 1)
static GLint normalizedToInt(double v)
{
    if (v >= 1.0) return 2147483647;
    if (v <= -1.0) return -2147483647;
    return (GLint)(v*2147483647.0);
}

void glGetIntegerv(GLenum pname, GLint *params)
{
    double v[16];
    bool normalized;
    int n = getState(pname, v, &normalized);
    for (int i = 0; i < n; i++) params[i] = normalized? normalizedToInt(v[i]) : (GLint)lround(v[i]);
}

void glGetBooleanv(GLenum pname, GLboolean *params)
{
    double v[16];
    bool normalized;
    int n = getState(pname, v, &normalized);
    for (int i = 0; i < n; i++) params[i] = (v[i] != 0.0)? GL_TRUE : GL_FALSE;
}

const GLubyte *glGetString(GLenum name)
{
    switch (name)
    {
        case GL_VENDOR: return (const GLubyte *)"c3dgl";
        case GL_RENDERER: return (const GLubyte *)"citro3d (PICA200)";
        case GL_VERSION: return (const GLubyte *)"1.1 c3dgl";
        case GL_EXTENSIONS:
            return (const GLubyte *)"GL_OES_point_sprite GL_OES_point_size_array GL_OES_compressed_paletted_texture "
                                    "GL_OES_compressed_ETC1_RGB8_texture";
        default: return (const GLubyte *)"";
    }
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    LIST_SAVE(VIEWPORT, "iiii", x, y, width, height);
    if ((width < 0) || (height < 0)) { setError(GL_INVALID_VALUE); return; }
    gl.state.viewport[0] = x;
    gl.state.viewport[1] = y;
    gl.state.viewport[2] = width;
    gl.state.viewport[3] = height;
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    LIST_SAVE(SCISSOR, "iiii", x, y, width, height);
    if ((width < 0) || (height < 0)) { setError(GL_INVALID_VALUE); return; }
    gl.state.scissorBox[0] = x;
    gl.state.scissorBox[1] = y;
    gl.state.scissorBox[2] = width;
    gl.state.scissorBox[3] = height;
}

void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    LIST_SAVE(CLEAR_COLOR, "ffff", red, green, blue, alpha);
    gl.clearColor = ((u32)colorByte(red) << 24) | ((u32)colorByte(green) << 16) | ((u32)colorByte(blue) << 8) | colorByte(alpha);
}

void glClearDepth(GLclampd depth)
{
    LIST_SAVE(CLEAR_DEPTH, "d", depth);
    gl.clearDepth = (depth < 0.0)? 0.0f : (depth > 1.0)? 1.0f : (float)depth;
}

void glClearStencil(GLint s)
{
    LIST_SAVE(CLEAR_STENCIL, "i", s);
    gl.clearStencil = (u8)s;
}

// Clear by drawing a full-screen quad at the clear depth: honors scissor and all write masks
static void clearWithQuad(bool color, bool depth, bool stencil)
{
    DrawState key;
    memset(&key, 0, sizeof(key));
    key.clipSpace = true;
    key.viewport[2] = screenWidth(gl.screen);       // glClear ignores the viewport
    key.viewport[3] = C3DGL_SCREEN_HEIGHT;
    key.scissor = gl.state.scissor;
    memcpy(key.scissorBox, gl.state.scissorBox, sizeof(key.scissorBox));
    key.colorMask = color? gl.state.colorMask : 0;
    key.depthTest = true;                           // Depth writes need the test enabled
    key.depthFunc = GL_ALWAYS;
    key.depthMask = depth;
    key.depthFar = 1.0f;                            // The clear depth is not affected by glDepthRange
    if (stencil)
    {
        key.stencilTest = true;
        key.stencilFunc = GL_ALWAYS;
        key.stencilRef = gl.clearStencil;
        key.stencilFuncMask = 0xFF;
        key.stencilWriteMask = gl.state.stencilWriteMask;
        key.stencilFail = key.stencilDepthFail = key.stencilPass = GL_REPLACE;
    }
    useState(&key);

    Vertex v[4];
    memset(v, 0, sizeof(v));
    for (int i = 0; i < 4; i++)
    {
        v[i].pos[0] = (i == 1 || i == 2)? 1.0f : -1.0f;
        v[i].pos[1] = (i >= 2)? 1.0f : -1.0f;
        v[i].pos[2] = 2.0f*gl.clearDepth - 1.0f;    // Window depth -> NDC
        for (int c = 0; c < 4; c++) v[i].color[c] = (u8)(gl.clearColor >> (24 - 8*c));
    }
    emitTriangle(&v[0], &v[1], &v[2]);
    emitTriangle(&v[0], &v[2], &v[3]);
}

void glClear(GLbitfield mask)
{
    LIST_SAVE(CLEAR, "u", mask);
    if (mask & ~(GLbitfield)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_ACCUM_BUFFER_BIT))
    {
        setError(GL_INVALID_VALUE);
        return;
    }
    if (gl.renderMode != GL_RENDER) return;     // Like Mesa: feedback and selection draw nothing
    if (mask & GL_ACCUM_BUFFER_BIT) clearAccum();

    // Write masks apply to clears, glDrawBuffer(GL_NONE) clears no color
    bool color = (mask & GL_COLOR_BUFFER_BIT) && (gl.state.colorMask != 0) && (gl.drawBuffer != GL_NONE);
    bool depth = (mask & GL_DEPTH_BUFFER_BIT) && gl.state.depthMask;
    bool stencil = (mask & GL_STENCIL_BUFFER_BIT) && (gl.state.stencilWriteMask != 0);
    if (!color && !depth && !stencil) return;

    // The memory fill clears whole buffers, and depth and stencil only together. Stencil may be
    // overwritten as long as it was never used
    bool fill = !gl.state.scissor &&
                (!color || (gl.state.colorMask == GPU_WRITE_COLOR)) &&
                (!stencil || (gl.state.stencilWriteMask == 0xFF)) &&
                ((depth == stencil) || (depth && !gl.stencilUsed));
    if (!fill)
    {
        clearWithQuad(color, depth, stencil);
        return;
    }

    ensureFrame();
    flushVertexCache();

    // Clears run as memory fills outside the command list; split it so earlier draws stay before the clear. The split
    // part is flushed from the CPU cache: C3D_FrameEnd(GX_CMDLIST_FLUSH) (see suspendFrame()) flushes only the last part,
    // and the GPU locks up on a stale command list (real hardware only)
    if (gl.drawnThisFrame) C3D_FrameSplit(GX_CMDLIST_FLUSH);

    // D24S8: stencil in the top byte, depth reversed (see depthFunc())
    u32 depthStencil = ((u32)gl.clearStencil << 24) | (u32)((1.0f - gl.clearDepth)*0xFFFFFF);
    int bits = (color? C3D_CLEAR_COLOR : 0) | ((depth || stencil)? C3D_CLEAR_DEPTH : 0);
    C3D_RenderTargetClear(curTarget(), (C3D_ClearBits)bits, gl.clearColor, depthStencil);
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    LIST_SAVE(COLOR_MASK, "iiii", red, green, blue, alpha);
    gl.state.colorMask = (red? GPU_WRITE_RED : 0) | (green? GPU_WRITE_GREEN : 0) | (blue? GPU_WRITE_BLUE : 0) | (alpha? GPU_WRITE_ALPHA : 0);
}

void glDepthMask(GLboolean flag)
{
    LIST_SAVE(DEPTH_MASK, "i", flag);
    gl.state.depthMask = flag;
}

void glDepthFunc(GLenum func)
{
    LIST_SAVE(DEPTH_FUNC, "u", func);
    gl.state.depthFunc = func;
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
    LIST_SAVE(STENCIL_FUNC, "uiu", func, ref, mask);
    gl.state.stencilFunc = func;
    gl.state.stencilRef = (u8)((ref < 0)? 0 : (ref > 255)? 255 : ref);
    gl.state.stencilFuncMask = (u8)mask;
}

void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
    LIST_SAVE(STENCIL_OP, "uuu", fail, zfail, zpass);
    gl.state.stencilFail = fail;
    gl.state.stencilDepthFail = zfail;
    gl.state.stencilPass = zpass;
}

void glStencilMask(GLuint mask)
{
    LIST_SAVE(STENCIL_MASK, "u", mask);
    gl.state.stencilWriteMask = (u8)mask;
}

void glAlphaFunc(GLenum func, GLclampf ref)
{
    LIST_SAVE(ALPHA_FUNC, "uf", func, ref);
    gl.state.alphaFunc = func;
    gl.state.alphaRef = colorByte(ref);
}

void glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    LIST_SAVE(BLEND_FUNC, "uu", sfactor, dfactor);
    gl.state.blendSrc = sfactor;
    gl.state.blendDst = dfactor;
}

void glLogicOp(GLenum opcode)
{
    LIST_SAVE(LOGIC_OP, "u", opcode);
    if ((opcode < GL_CLEAR) || (opcode > GL_SET)) { setError(GL_INVALID_ENUM); return; }
    gl.state.logicOpMode = opcode;
}

void glSampleCoverage(GLclampf value, GLboolean invert)
{
    LIST_SAVE(SAMPLE_COVERAGE, "fi", value, invert);
    gl.sampleCoverage = (value < 0.0f)? 0.0f : (value > 1.0f)? 1.0f : value;
    gl.sampleCoverageInvert = invert;
}

void glCullFace(GLenum mode)
{
    LIST_SAVE(CULL_FACE, "u", mode);
    gl.state.cullFace = mode;
}

void glFrontFace(GLenum mode)
{
    LIST_SAVE(FRONT_FACE, "u", mode);
    gl.state.frontFace = mode;
}

void glPolygonMode(GLenum face, GLenum mode)
{
    LIST_SAVE(POLYGON_MODE, "uu", face, mode);
    if ((mode != GL_POINT) && (mode != GL_LINE) && (mode != GL_FILL)) { setError(GL_INVALID_ENUM); return; }

    switch (face)
    {
        case GL_FRONT: gl.polygonMode[0] = mode; break;
        case GL_BACK: gl.polygonMode[1] = mode; break;
        case GL_FRONT_AND_BACK: gl.polygonMode[0] = gl.polygonMode[1] = mode; break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

void glPolygonOffset(GLfloat factor, GLfloat units)
{
    LIST_SAVE(POLYGON_OFFSET, "ff", factor, units);
    gl.offsetFactor = factor;
    gl.offsetUnits = units;
}

void glDepthRange(GLclampd zNear, GLclampd zFar)
{
    LIST_SAVE(DEPTH_RANGE, "dd", zNear, zFar);
    gl.state.depthNear = (zNear < 0.0)? 0.0f : (zNear > 1.0)? 1.0f : (float)zNear;
    gl.state.depthFar = (zFar < 0.0)? 0.0f : (zFar > 1.0)? 1.0f : (float)zFar;
}

void glLineWidth(GLfloat width)
{
    LIST_SAVE(LINE_WIDTH, "f", width);
    gl.lineWidth = width;
}

void glLineStipple(GLint factor, GLushort pattern)
{
    LIST_SAVE(LINE_STIPPLE, "iu", factor, (unsigned)pattern);
    gl.lineStippleFactor = (factor < 1)? 1 : (factor > 256)? 256 : factor;
    gl.lineStipplePattern = pattern;
}

// The pattern from 32 words of 4 bytes (display lists and glPolygonStipple); draws issued before keep the old one
static void setPolygonStipple(const ListWord *w)
{
    memcpy(gl.polygonStipplePattern, w, sizeof(gl.polygonStipplePattern));
    gl.stippleTex = NULL;
}

// Unpacked like a 32x32 glBitmap (GL 1.1 section 3.5.6), without pixel transfer
void glPolygonStipple(const GLubyte *mask)
{
    ListWord pattern[32];
    u8 *bytes = (u8 *)pattern;
    if (mask == NULL) return;
    packBitmap(mask, &gl.unpack, 32, 32, bytes);
    if (gl.listCompiling)
    {
        ListWord *w = listBegin(LIST_POLYGON_STIPPLE, 32);
        if (w == NULL) return;
        memcpy(w, pattern, sizeof(pattern));
        listEnd();
        return;
    }
    setPolygonStipple(pattern);
}

// Packed like glReadPixels of a 32x32 GL_BITMAP image
void glGetPolygonStipple(GLubyte *mask)
{
    if (mask == NULL) return;
    const PixelStore *ps = &gl.pack;
    size_t rowBytes = ((size_t)((ps->rowLength > 0)? ps->rowLength : 32) + 7)/8;
    rowBytes = (rowBytes + ps->alignment - 1)/ps->alignment*ps->alignment;
    for (int y = 0; y < 32; y++)
    {
        u8 *row = mask + (size_t)(ps->skipRows + y)*rowBytes;
        for (int x = 0; x < 32; x++)
        {
            int col = ps->skipPixels + x;
            u8 bit = ps->lsbFirst? (u8)(1 << (col & 7)) : (u8)(0x80 >> (col & 7));
            if ((gl.polygonStipplePattern[4*y + x/8] << (x & 7)) & 0x80) row[col/8] |= bit;
            else row[col/8] &= (u8)~bit;
        }
    }
}

void glPointSize(GLfloat size)
{
    LIST_SAVE(POINT_SIZE, "f", size);
    if (size <= 0.0f) { setError(GL_INVALID_VALUE); return; }
    gl.pointSize = size;
}

// glPointParameter (GL 1.4, ES 1.1)
void glPointParameterfv(GLenum pname, const GLfloat *params)
{
    LIST_SAVE(POINT_PARAMETER, "uF", pname, (pname == GL_POINT_DISTANCE_ATTENUATION)? 3 : 1, params);
    switch (pname)
    {
        case GL_POINT_SIZE_MIN: case GL_POINT_SIZE_MAX: case GL_POINT_FADE_THRESHOLD_SIZE:
            if (params[0] < 0.0f) { setError(GL_INVALID_VALUE); return; }
            if (pname == GL_POINT_SIZE_MIN) gl.pointSizeMin = params[0];
            else if (pname == GL_POINT_SIZE_MAX) gl.pointSizeMax = params[0];
            else gl.pointFadeThreshold = params[0];
            return;
        case GL_POINT_DISTANCE_ATTENUATION:
            memcpy(gl.pointAttenuation, params, sizeof(gl.pointAttenuation));
            return;
        default: setError(GL_INVALID_ENUM); return;
    }
}

void glPointParameterf(GLenum pname, GLfloat param)
{
    if (pname == GL_POINT_DISTANCE_ATTENUATION) { setError(GL_INVALID_ENUM); return; }     // Needs 3 values
    glPointParameterfv(pname, &param);
}

void glPointParameteri(GLenum pname, GLint param) { glPointParameterf(pname, (GLfloat)param); }

void glPointParameteriv(GLenum pname, const GLint *params)
{
    GLfloat f[3] = { (GLfloat)params[0], 0.0f, 0.0f };
    if (pname == GL_POINT_DISTANCE_ATTENUATION) { f[1] = (GLfloat)params[1]; f[2] = (GLfloat)params[2]; }
    glPointParameterfv(pname, f);
}
