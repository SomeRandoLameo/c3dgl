// Platform API (c3dgl.h): init, screens, stereo, swap buffers
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// Platform API (c3dgl.h)
//----------------------------------------------------------------------------------
bool c3dglInit(void)
{
    if (gl.ready) return true;
    memset(&gl, 0, sizeof(gl));
    memset(litCache, 0, sizeof(litCache));     // Generations restart at 0

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) { LOG("C3D_Init failed\n"); return false; }

    // Render targets are portrait (240x400, 240x320) because the screens are rotated.
    // The bottom one is linked to its display on the first c3dglSetScreen(), so a console there stays untouched
    for (int i = 0; i < C3DGL_SCREEN_COUNT; i++)
    {
        gl.targets[i] = C3D_RenderTargetCreate(C3DGL_SCREEN_HEIGHT, screenWidth((C3DGLscreen)i), GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
        if (gl.targets[i] == NULL) { LOG("Failed to create render target\n"); c3dglClose(); return false; }
    }
    gl.screen = C3DGL_SCREEN_TOP;
    linkTarget();

    if (!C3D_TexInit(&gl.dummyTexture, 8, 8, GPU_L8)) { LOG("Failed to allocate texture\n"); c3dglClose(); return false; }
    memset(gl.dummyTexture.data, 0, gl.dummyTexture.size);
    C3D_TexFlush(&gl.dummyTexture);

    gl.vbo = linearAlloc(C3DGL_MAX_VERTICES*GPU_VERTEX_SIZE);
    gl.vboExtra = linearAlloc(C3DGL_MAX_VERTICES*GPU_EXTRA_SIZE);
    if ((gl.vbo == NULL) || (gl.vboExtra == NULL)) { LOG("Failed to allocate vertex buffer\n"); c3dglClose(); return false; }

    gl.dvlb = DVLB_ParseFile((u32 *)c3dgl_vsh_shbin, c3dgl_vsh_shbin_size);
    shaderProgramInit(&gl.program);
    shaderProgramSetVsh(&gl.program, &gl.dvlb->DVLE[0]);
    C3D_BindProgram(&gl.program);
    gl.uLocMvp = shaderInstanceGetUniformLocation(gl.program.vertexShader, "mvp");
    gl.uLocTexMat[0] = shaderInstanceGetUniformLocation(gl.program.vertexShader, "texmat0");
    gl.uLocTexMat[1] = shaderInstanceGetUniformLocation(gl.program.vertexShader, "texmat1");
    gl.uLocTexMat[2] = shaderInstanceGetUniformLocation(gl.program.vertexShader, "texmat2");
    gl.uLocStipple = shaderInstanceGetUniformLocation(gl.program.vertexShader, "stipple");

    // Vertex layout: v0 = position (3 floats), v1 = texcoord s, t, q (3 floats), v2 = color (4 ubytes), v3 = depth bias (float)
    C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
    AttrInfo_Init(attrInfo);
    AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attrInfo, 2, GPU_UNSIGNED_BYTE, 4);
    AttrInfo_AddLoader(attrInfo, 3, GPU_FLOAT, 1);
    AttrInfo_AddLoader(attrInfo, 4, GPU_FLOAT, 3);     // Buffer 1: texcoords of units 1 and 2
    AttrInfo_AddLoader(attrInfo, 5, GPU_FLOAT, 3);

    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);
    BufInfo_Add(bufInfo, gl.vbo, GPU_VERTEX_SIZE, 4, 0x3210);
    BufInfo_Add(bufInfo, gl.vboExtra, GPU_EXTRA_SIZE, 2, 0x54);

    // Stored depth = -z_clip: near = 1, far = 0 (see depthFunc())
    C3D_DepthMap(true, -1.0f, 0.0f);

    // OpenGL clip space -> PICA clip space, taken from citro3d itself:
    // Mtx_OrthoTilt(-1,1,-1,1,-1,1) = post * glOrtho(-1,1,-1,1,-1,1) and glOrtho(...) = diag(1,1,-1,1)
    C3D_Mtx tilt;
    Mtx_OrthoTilt(&tilt, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, false);
    mat4FromC3D(&tilt, &gl.post);
    for (int row = 0; row < 4; row++) gl.post.m[2*4 + row] = -gl.post.m[2*4 + row];

    // OpenGL default state
    gl.state.viewport[2] = gl.state.scissorBox[2] = C3DGL_TOP_SCREEN_WIDTH;
    gl.state.viewport[3] = gl.state.scissorBox[3] = C3DGL_SCREEN_HEIGHT;
    gl.state.blendSrc = GL_ONE;
    gl.state.blendDst = GL_ZERO;
    gl.state.logicOpMode = GL_COPY;
    gl.state.depthFunc = GL_LESS;
    gl.state.depthMask = true;
    gl.state.alphaFunc = GL_ALWAYS;
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        // OpenGL defaults (GL 1.3 / ES 1.1 table 6.18)
        TexEnvState *e = &gl.state.units[unit].env;
        e->mode = GL_MODULATE;
        e->combineRgb = e->combineAlpha = GL_MODULATE;
        e->srcRgb[0] = e->srcAlpha[0] = GL_TEXTURE;
        e->srcRgb[1] = e->srcAlpha[1] = GL_PREVIOUS;
        e->srcRgb[2] = e->srcAlpha[2] = GL_CONSTANT;
        e->operandRgb[0] = e->operandRgb[1] = GL_SRC_COLOR;
        e->operandRgb[2] = GL_SRC_ALPHA;
        e->operandAlpha[0] = e->operandAlpha[1] = e->operandAlpha[2] = GL_SRC_ALPHA;
        e->rgbScale = e->alphaScale = 1;

        // glTexGen defaults: eye linear, s and t planes along x and y
        TexGenState *g = &gl.texGen[unit];
        for (int c = 0; c < 4; c++) g->mode[c] = GL_EYE_LINEAR;
        g->objectPlane[0][0] = g->eyePlane[0][0] = 1.0f;
        g->objectPlane[1][1] = g->eyePlane[1][1] = 1.0f;
    }
    for (int unit = 1; unit < C3DGL_TEXTURE_UNITS; unit++) gl.current.texExtra[unit - 1][2] = 1.0f;    // q
    gl.state.stencilFunc = GL_ALWAYS;
    gl.state.stencilFuncMask = gl.state.stencilWriteMask = 0xFF;
    gl.state.stencilFail = gl.state.stencilDepthFail = gl.state.stencilPass = GL_KEEP;
    gl.state.colorMask = GPU_WRITE_COLOR;
    gl.state.cullFace = GL_BACK;
    gl.state.frontFace = GL_CCW;
    gl.unpack.alignment = gl.pack.alignment = 4;
    gl.lineWidth = gl.pointSize = 1.0f;
    gl.pointSizeMax = C3DGL_MAX_POINT_SIZE;
    gl.pointFadeThreshold = 1.0f;
    gl.pointAttenuation[0] = 1.0f;
    gl.clearColor = 0x000000FF;
    gl.clearDepth = 1.0f;
    gl.state.depthFar = 1.0f;
    gl.polygonMode[0] = gl.polygonMode[1] = GL_FILL;
    gl.lineStippleFactor = 1;
    gl.lineStipplePattern = 0xFFFF;
    memset(gl.polygonStipplePattern, 0xFF, sizeof(gl.polygonStipplePattern));
    gl.currentEdge = true;
    gl.shadeModel = GL_SMOOTH;
    gl.currentNormal[2] = 1.0f;
    initLighting();
    gl.fogMode = GL_EXP;
    gl.fogDensity = gl.fogEnd = 1.0f;
    gl.ignoredCaps = (1u << ignoredCapBit(GL_DITHER)) | (1u << ignoredCapBit(GL_MULTISAMPLE));    // Enabled by default
    gl.sampleCoverage = 1.0f;
    gl.drawBuffer = gl.readBuffer = GL_BACK;
    gl.raster = (RasterState){ .pos = { 0, 0, 0, 1 }, .valid = true, .color = { 255, 255, 255, 255 }, .tex = { 0, 0, 0, 1 } };
    gl.zoomX = gl.zoomY = 1.0f;
    gl.transfer = noTransfer;
    gl.renderMode = GL_RENDER;
    gl.feedbackType = GL_2D;
    gl.hitMinZ = 1.0f;
    for (int i = 0; i < PIXEL_MAP_COUNT; i++) gl.pixelMaps[i].size = 1;    // One entry, 0
    memset(gl.current.color, 255, 4);
    gl.current.tex[2] = 1.0f;
    gl.current.pointSize = -1.0f;   // No point size array: glPointSize

    // Client array defaults: size 4 (3 for normals), GL_FLOAT
    for (int i = 0; i < ARRAY_COUNT; i++)
    {
        gl.arrays[i].size = (i == ARRAY_NORMAL)? 3 : ((i == ARRAY_EDGEFLAG) || (i == ARRAY_POINTSIZE))? 1 : 4;
        gl.arrays[i].type = (i == ARRAY_EDGEFLAG)? GL_UNSIGNED_BYTE : GL_FLOAT;
    }

    for (int i = 0; i < 2 + C3DGL_TEXTURE_UNITS; i++) mat4Identity(&gl.stack[i][0]);

    // Texture 0 of each target (GL 1.0 style code without glBindTexture)
    initTexture(&gl.textures[DEFAULT_TEXTURE_1D]);
    gl.textures[DEFAULT_TEXTURE_1D].target = GL_TEXTURE_1D;
    initTexture(&gl.textures[DEFAULT_TEXTURE_2D]);
    gl.textures[DEFAULT_TEXTURE_2D].target = GL_TEXTURE_2D;

    // Evaluator defaults: grids of 1 segment over [0, 1]
    gl.grid1n = gl.grid2un = gl.grid2vn = 1;
    gl.grid1u2 = gl.grid2u2 = gl.grid2v2 = 1.0f;
    gl.matrixSerial = gl.texMatrixSerial = 1;

    gl.ready = true;
    return true;
}

void c3dglClose(void)
{
    if (gl.frameActive) { flushVertexCache(); C3D_FrameEnd(0); gl.frameActive = false; }

    // Finish outstanding commands before releasing immutable GPU buffers.
    if (gl.ready) { C3D_FrameBegin(C3D_FRAME_SYNCDRAW); C3D_FrameEnd(0); gl.frameSerial++; }
    collectGpuCaches();
    for (GLuint i = 0; i < gl.bufferCount; i++) invalidateGpuCache(&gl.buffers[i]);

    for (int i = 1; i < TEXTURE_SLOTS; i++) if (gl.textures[i].loaded) C3D_TexDelete(&gl.textures[i].tex);
    processDeferredDeletes();
    free(gl.deferredDeletes);
    for (int i = 0; i < gl.pixelChunkCount; i++) linearFree(gl.pixelChunks[i].data);
    free(gl.pixelChunks);
    free(gl.polyVerts);
    free(gl.polyEdges);
    free(gl.polyPtrs);
    for (int i = 0; i < 2; i++) { free(gl.clipLists[i].verts); free(gl.clipLists[i].edges); }
    free(gl.clipPtrs);
    for (GLuint i = 0; i < gl.bufferCount; i++) free(gl.buffers[i].data);
    free(gl.buffers);
    for (int i = 0; i < 9; i++) { free(gl.map1[i].points); free(gl.map2[i].points); }
    free(gl.evalGrid);
    for (int i = 0; i < gl.listCount; i++) free(gl.lists[i].words);
    free(gl.lists);
    free(gl.listWords);
    for (int i = 0; i < C3DGL_TARGET_COUNT; i++) free(gl.accum[i]);

    if (gl.dvlb != NULL) { shaderProgramFree(&gl.program); DVLB_Free(gl.dvlb); }
    if (gl.dummyTexture.data != NULL) C3D_TexDelete(&gl.dummyTexture);
    if (gl.vbo != NULL) linearFree(gl.vbo);
    if (gl.vboExtra != NULL) linearFree(gl.vboExtra);
    for (int i = 0; i < C3DGL_TARGET_COUNT; i++) if (gl.targets[i] != NULL) C3D_RenderTargetDelete(gl.targets[i]);
    if (gl.stereo) gfxSet3D(false);
    C3D_Fini();

    memset(&gl, 0, sizeof(gl));
}

// Make the current screen/eye the render target: viewport and scissor box of its size, like a freshly bound framebuffer
static void switchTarget(void)
{
    {
        PROF_ENTER();
        linkTarget();
        PROF_LEAVE(PB_LINK_TARGET, 0);
    }

    gl.state.viewport[0] = gl.state.viewport[1] = 0;
    gl.state.viewport[2] = screenWidth(gl.screen);
    gl.state.viewport[3] = C3DGL_SCREEN_HEIGHT;
    memcpy(gl.state.scissorBox, gl.state.viewport, sizeof(gl.state.viewport));

    if (gl.frameActive)
    {
        PROF_ENTER();
        C3D_FrameDrawOn(curTarget());
        PROF_LEAVE(PB_DRAW_ON, 0);
        gl.batchValid = false;
    }
}

void c3dglSetScreen(C3DGLscreen screen)
{
    if ((screen != C3DGL_SCREEN_TOP) && (screen != C3DGL_SCREEN_BOTTOM)) return;

    if (gl.frameActive) flush();    // Pending vertices belong to the previous screen

    PROF_ENTER();
    gl.screen = screen;
    switchTarget();
    PROF_LEAVE(PB_TARGET_SWITCH, 0);
}

bool c3dglSetStereo(bool enable)
{
    if (!gl.ready) return false;
    if (enable == gl.stereo) return true;
    if (gl.frameActive) { LOG("c3dglSetStereo: call between frames\n"); return gl.stereo; }

    if (enable)
    {
        if (gl.targets[C3DGL_TARGET_RIGHT] == NULL)
        {
            gl.targets[C3DGL_TARGET_RIGHT] = C3D_RenderTargetCreate(C3DGL_SCREEN_HEIGHT, C3DGL_TOP_SCREEN_WIDTH, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
            if (gl.targets[C3DGL_TARGET_RIGHT] == NULL) { LOG("Failed to create the right eye render target\n"); return false; }
        }
        gfxSet3D(true);
        gl.stereo = true;
    }
    else
    {
        gfxSet3D(false);
        gl.stereo = false;
        gl.eye = C3DGL_EYE_LEFT;
        switchTarget();
    }
    return true;
}

bool c3dglGetStereo(void)
{
    return gl.stereo;
}

void c3dglSetEye(C3DGLeye eye)
{
    if ((eye != C3DGL_EYE_LEFT) && (eye != C3DGL_EYE_RIGHT)) return;
    if (!gl.stereo) eye = C3DGL_EYE_LEFT;

    if (gl.frameActive) flush();
    gl.eye = eye;
    if (gl.screen == C3DGL_SCREEN_TOP) switchTarget();
}

C3DGLeye c3dglGetEye(void)
{
    return gl.eye;
}

float c3dglGet3DSlider(void)
{
    return osGet3DSliderState();
}

C3DGLscreen c3dglGetScreen(void)
{
    return gl.screen;
}

int c3dglGetScreenWidth(C3DGLscreen screen)
{
    return screenWidth(screen);
}

void c3dglGetFrameStats(float *gpuMs, float *cpuMs, float *cmdBufUsage)
{
    if (gpuMs) *gpuMs = C3D_GetDrawingTime();
    if (cpuMs) *cpuMs = C3D_GetProcessingTime();
    if (cmdBufUsage) *cmdBufUsage = C3D_GetCmdBufUsage();
}

double c3dglGetGpuWaitMs(void)
{
    double ms = (double)gpuWaitTicks * 1000.0 / SYSCLOCK_ARM11;
    gpuWaitTicks = 0;
    return ms;
}

void c3dglSwapBuffers(void)
{
    ensureFrame();      // Present even if nothing was drawn
    flushVertexCache();
#if C3DGL_PRESENT_GAP_MS > 0
    if (osGetTime() - lastPresentMs < C3DGL_PRESENT_GAP_MS)
    {
        PROF_ENTER();
        C3D_FrameSync();
        PROF_LEAVE(PB_PRESENT_SYNC, 0);
    }
#endif
    {
        PROF_ENTER();
        C3D_FrameEnd(0);
        PROF_LEAVE(PB_SWAP, 0);
    }
    lastPresentMs = osGetTime();
    gl.frameActive = false;
}
