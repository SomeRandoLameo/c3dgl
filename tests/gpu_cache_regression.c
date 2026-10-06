// Exercise the real cache and GPU, including immutable storage lifetime.
#ifdef C3DGL_MAX_VERTICES
#undef C3DGL_MAX_VERTICES
#endif
#define C3DGL_MAX_VERTICES 1024
#define C3DGL_GPU_CACHE_BYTES 4096
#include "../src/c3dgl.c"

typedef struct { s16 pos[3], pad, uv[2]; u8 color[4]; } Packed;
static Packed vertices[4] = {
    {{2,2,0},0,{0,0},{255,0,0,255}}, {{30,2,0},0,{1,0},{0,255,0,255}},
    {{30,30,0},0,{1,1},{0,0,255,255}}, {{2,30,0},0,{0,1},{255,255,0,255}}
};
static u16 indices[] = {0,1,2,0,2,3};
static FILE *logFile;
static int checks, failures;
#define CHECK(expr) do { checks++; if (!(expr)) { failures++; fprintf(logFile,"FAIL line %d: %s\n",__LINE__,#expr); fflush(logFile); } } while (0)

static void bindArrays(GLuint vbo, GLuint ibo)
{
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_SHORT, sizeof(Packed), 0);
    glTexCoordPointer(2, GL_SHORT, sizeof(Packed), (void*)8);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Packed), (void*)12);
}

static void draw(void) { glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0); }
static void readImage(u8 *pixels) { glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,pixels); }

int main(void)
{
    gfxInitDefault(); consoleInit(GFX_BOTTOM,NULL);
    logFile = fopen("sdmc:/c3dgl-gpu-cache-regression.log","w");
    if (!logFile || !c3dglInit()) return 1;
    glViewport(0,0,400,240);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,400,0,240,-1,1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    GLuint vbo, ibo;
    glGenBuffers(1,&vbo); glGenBuffers(1,&ibo);
    glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    bindArrays(vbo,ibo);
    GLuint texture; glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    u8 texels[8*8*4];
    for (int i=0;i<64;i++) { texels[4*i]=i*3; texels[4*i+1]=255-i*3; texels[4*i+2]=100; texels[4*i+3]=255; }
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,texels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    static u8 reference[32*32*4], actual[sizeof(reference)];
    // Compare cached and ordinary output with both shading modes and state
    // applied by the GPU. Mixed immediate geometry verifies buffer restoration.
    for (int shade=0; shade<2; shade++) for (int state=0; state<9; state++) {
        glShadeModel(shade ? GL_FLAT : GL_SMOOTH);
        glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glDisable(GL_FOG);
        glDisable(GL_ALPHA_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_TEXTURE_2D);
        glDisable(GL_STENCIL_TEST); glDepthMask(GL_TRUE);
        glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glLoadIdentity();
        if (state==1) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); }
        if (state==2) { glEnable(GL_SCISSOR_TEST); glScissor(5,5,20,20); }
        if (state==3) { glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER,0.5f); }
        if (state==4) { glTranslatef(3,1,0); glColorMask(GL_TRUE,GL_FALSE,GL_TRUE,GL_TRUE); }
        if (state==5) { glEnable(GL_FOG); glFogi(GL_FOG_MODE,GL_LINEAR); glFogf(GL_FOG_START,0); glFogf(GL_FOG_END,1); glTranslatef(0,0,0.5f); }
        if (state==6) { glEnable(GL_TEXTURE_2D); glMatrixMode(GL_TEXTURE); glScalef(0.5f,0.5f,1); glMatrixMode(GL_MODELVIEW); }
        if (state==7) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); }
        if (state==8) { glEnable(GL_STENCIL_TEST); glStencilFunc(GL_ALWAYS,1,255); glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE); }
        for (int cached=0; cached<2; cached++) {
            glDisable(GL_SCISSOR_TEST); glColorMask(1,1,1,1);
            glClearColor(0.1f,0.2f,0.3f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
            if (state==2) glEnable(GL_SCISSOR_TEST);
            if (state==4) glColorMask(1,0,1,1);
            gl.buffers[vbo].usage = cached ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW;
            draw(); draw();
            glBegin(GL_TRIANGLES); glColor3ub(255,0,255);
            glVertex3f(10,10,0); glVertex3f(20,10,0); glVertex3f(15,20,0); glEnd();
            readImage(cached ? actual : reference);
        }
        if (memcmp(reference,actual,sizeof(reference))) fprintf(logFile,"pixel mismatch shade=%d state=%d\n",shade,state);
        CHECK(memcmp(reference,actual,sizeof(reference))==0);
        CHECK(gl.buffers[vbo].gpuCache != NULL);
    }
    glDisable(GL_FOG); glDisable(GL_STENCIL_TEST); glDisable(GL_TEXTURE_2D); glColorMask(1,1,1,1); glLoadIdentity();
    glShadeModel(GL_FLAT); draw();
    GpuBufferCache *old = gl.buffers[vbo].gpuCache;
    int countBefore=gl.vertexCount; draw();
    CHECK(gl.buffers[vbo].gpuCache==old && gl.vertexCount==countBefore);
    CHECK(old && old->drawnFrame==gl.frameSerial);
    u8 red[4]={255,0,0,255};
    glBufferSubData(GL_ARRAY_BUFFER,12,sizeof(red),red);
    CHECK(gl.buffers[vbo].gpuCache==NULL);
    CHECK(gl.retiredGpuCaches==old); // Not freed while pending GPU commands use it.
    draw(); CHECK(gl.buffers[vbo].gpuCache && gl.buffers[vbo].gpuCache!=old);
    readImage(actual); collectGpuCaches(); CHECK(gl.retiredGpuCaches==NULL);

    draw(); readImage(reference); draw();
    CHECK(reclaimGpuCaches()); CHECK(gl.gpuCacheBytes==0);
    readImage(actual); CHECK(memcmp(reference,actual,sizeof(reference))==0);
    draw(); CHECK(gl.buffers[vbo].gpuCache!=NULL);

    // Index changes and replacement storage must invalidate the expansion key.
    u64 revision=gl.buffers[ibo].revision;
    u16 reverse[]={0,2,1,0,3,2};
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,0,sizeof(reverse),reverse);
    CHECK(gl.buffers[ibo].revision!=revision);
    draw(); CHECK(gl.buffers[vbo].gpuCache->indexRevision==gl.buffers[ibo].revision);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    CHECK(gl.buffers[vbo].gpuCache==NULL); draw();

    // Ranges, layouts and baked state are part of the cache key.
    glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,(void*)6);
    CHECK(gl.buffers[vbo].gpuCache->count==3 && gl.buffers[vbo].gpuCache->indexOffset==6);
    glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(Packed),(void*)4); draw();
    CHECK(gl.buffers[vbo].gpuCache->arrays[2].pointer==(void*)4);
    glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(Packed),(void*)12); draw();
    gl.current.depthBias=0.125f; draw(); CHECK(gl.buffers[vbo].gpuCache->depthBias==0.125f);
    gl.current.depthBias=0; draw();

    // Deletion/name reuse in the same frame keeps the already submitted data alive.
    old=gl.buffers[vbo].gpuCache;
    glDeleteBuffers(1,&vbo); CHECK(gl.retiredGpuCaches==old);
    glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    bindArrays(vbo,ibo); draw(); CHECK(gl.buffers[vbo].gpuCache!=old);
    c3dglSwapBuffers(); glClear(GL_COLOR_BUFFER_BIT); CHECK(gl.retiredGpuCaches==NULL);

    // Force bounded-cache eviction; pending buffers remain alive, safe ones evict.
    GLuint many[32]; glGenBuffers(32,many);
    for(int i=0;i<32;i++) {
        glBindBuffer(GL_ARRAY_BUFFER,many[i]); glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
        bindArrays(many[i],ibo); draw();
        CHECK(gl.gpuCacheBytes<=C3DGL_GPU_CACHE_BYTES);
        c3dglSwapBuffers(); glClear(GL_COLOR_BUFFER_BIT);
    }
    CHECK(gl.buffers[many[0]].gpuCache==NULL);
    c3dglSwapBuffers(); glClear(GL_COLOR_BUFFER_BIT);
    bool fellBack=false;
    for(int i=0;i<32;i++) {
        bindArrays(many[i],ibo); draw();
        if (!gl.buffers[many[i]].gpuCache) fellBack=true;
        CHECK(gl.gpuCacheBytes<=C3DGL_GPU_CACHE_BYTES);
    }
    CHECK(fellBack); // Busy caches cannot be evicted; remaining draws still work.
    glDeleteBuffers(32,many); glDeleteBuffers(1,&vbo); glDeleteBuffers(1,&ibo);
    c3dglSwapBuffers(); glClear(GL_COLOR_BUFFER_BIT); collectGpuCaches();
    CHECK(gl.gpuCacheBytes==0); CHECK(glGetError()==GL_NO_ERROR);
    glDeleteTextures(1,&texture); c3dglClose();
    fprintf(logFile,"checks=%d failures=%d\n",checks,failures); fclose(logFile);
    printf("GPU cache: %d checks, %d failures\n",checks,failures);
    gfxExit(); return failures ? 1 : 0;
}
