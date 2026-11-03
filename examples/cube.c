/* Interactive native Rust GL cube: persistent indexed SHORT VBO, depth and culling. */
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl_native.h>
#include <stdint.h>
#include <stdio.h>
typedef struct {int16_t position[4];uint8_t color[4];} CubeVertex;
static const CubeVertex vertices[]={
    {{-512,-512,512,0},{255,0,0,255}},
    {{512,-512,512,0},{255,0,0,255}},
    {{512,512,512,0},{255,0,0,255}},
    {{-512,512,512,0},{255,0,0,255}},
    {{-512,-512,-512,0},{0,255,0,255}},
    {{-512,512,-512,0},{0,255,0,255}},
    {{512,512,-512,0},{0,255,0,255}},
    {{512,-512,-512,0},{0,255,0,255}},
    {{512,-512,-512,0},{0,0,255,255}},
    {{512,512,-512,0},{0,0,255,255}},
    {{512,512,512,0},{0,0,255,255}},
    {{512,-512,512,0},{0,0,255,255}},
    {{-512,-512,512,0},{255,255,0,255}},
    {{-512,512,512,0},{255,255,0,255}},
    {{-512,512,-512,0},{255,255,0,255}},
    {{-512,-512,-512,0},{255,255,0,255}},
    {{-512,512,512,0},{255,0,255,255}},
    {{512,512,512,0},{255,0,255,255}},
    {{512,512,-512,0},{255,0,255,255}},
    {{-512,512,-512,0},{255,0,255,255}},
    {{-512,-512,-512,0},{0,255,255,255}},
    {{512,-512,-512,0},{0,255,255,255}},
    {{512,-512,512,0},{0,255,255,255}},
    {{-512,-512,512,0},{0,255,255,255}},
};
static const uint16_t indices[]={0,1,2,0,2,3,4,5,6,4,6,7,8,9,10,8,10,11,12,13,14,12,14,15,16,17,18,16,18,19,20,21,22,20,22,23};
int main(void) {
    gfxInitDefault();consoleInit(GFX_BOTTOM,NULL);
    if(!c3dglInit()) {printf("Renderer init failed\n");gfxExit();return 1;}
    GLuint buffers[2];glGenBuffers(2,buffers);
    glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glVertexPointer(3,GL_SHORT,sizeof(CubeVertex),NULL);glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(CubeVertex),(const void *)8);
    glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_COLOR_ARRAY);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glFrustum(-1,1,-.6,.6,1,10);glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);glEnable(GL_CULL_FACE);glCullFace(GL_BACK);glFrontFace(GL_CCW);
    glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);glDepthRange(0,1);glClearDepth(1);
    glClearColor(24.f/255,48.f/255,80.f/255,1);
    float angle=0;unsigned frames=0;int error=0;
    while(aptMainLoop()) {
        hidScanInput();if(hidKeysDown()&KEY_START) break;
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();glTranslatef(0,0,-3);glRotatef(angle,0,1,0);glRotatef(36.8699f,1,0,0);glScalef(1.f/1024,1.f/1024,1.f/1024);
        glDrawElements(GL_TRIANGLES,36,GL_UNSIGNED_SHORT,NULL);
        error=c3dglNativePresent();if(error || glGetError()!=GL_NO_ERROR) {printf("FAIL cube draw\n");error=1;break;}
        C3DGLnativeStats stats;if(c3dglNativeGetStats(&stats)) {error=1;break;}
        if((frames++%30)==0) printf("\x1b[1;1HNative Rust: rotating indexed cube\nCPU %.3f ms, draws %lu\nDepth + back-face culling\nSTART exits\n",(double)stats.cpu_ticks*1000/SYSCLOCK_ARM11,(unsigned long)stats.draws);
        angle+=.7f;if(angle>=360) angle-=360;
    }
    glDeleteBuffers(2,buffers);c3dglClose();gfxExit();return error;
}
