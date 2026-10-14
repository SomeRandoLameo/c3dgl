// Shared capture entry point. Geometry, textures and GL draw calls come directly
// from the existing example TU; the interactive main is not selected here.
#define main c3dglOriginalInteractiveMain
#if defined(C3DGL_OLD_PRIMITIVES)
#include "../../examples/primitives/main.cpp"
#define CASE "primitives"
#elif defined(C3DGL_OLD_TEXTURE) || defined(C3DGL_OLD_TEXCOORDS) || defined(C3DGL_OLD_TEXMATRIX) || defined(C3DGL_OLD_MULTITEXTURE)
#include "../../examples/texture/main.cpp"
#ifdef C3DGL_OLD_MULTITEXTURE
#define CASE "multitexture"
#elif defined(C3DGL_OLD_TEXMATRIX)
#define CASE "texmatrix"
#elif defined(C3DGL_OLD_TEXCOORDS)
#define CASE "texcoords"
#else
#define CASE "texture"
#endif
#else
#include "../../examples/cube/main.cpp"
#define CASE "cube"
#endif
#undef main
#ifdef C3DGL_COMPARE_NATIVE
#include <c3dgl_native.h>
#define BACKEND "native"
#else
#define BACKEND "reference"
#endif
static uint8_t capture[400*240*3];
int main() {
    gfxInitDefault();consoleInit(GFX_BOTTOM,nullptr);
    FILE *report=fopen("sdmc:/c3dgl-old-" CASE "-" BACKEND "-results.txt","w");
    if(!report) {gfxExit();return 1;}
    if(!c3dglInit()) {fprintf(report,"FAIL renderer init\n");fclose(report);gfxExit();return 1;}
    glClearColor(.12f,.12f,.15f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
#if defined(C3DGL_OLD_PRIMITIVES)
    glEnable(GL_CULL_FACE);glCullFace(GL_BACK);glFrontFace(GL_CCW);
    beginCell(3,0);drawTriangleStrip();
    beginCell(0,1);drawTriangleFan();
    beginCell(1,1);drawQuadStrip();
    beginCell(2,1);drawPolygon();
    glDisable(GL_CULL_FACE);
#elif defined(C3DGL_OLD_MULTITEXTURE)
    const GLuint tex=createF(16);const MultiTextures multi=createMultiTextures(tex);drawMultitexturePage(multi);
#elif defined(C3DGL_OLD_TEXMATRIX)
    const GLuint tex=createF(16), npot=createF(12);drawTextureMatrixPage(tex,npot,0.0f);
    glDeleteTextures(1,&npot);
#elif defined(C3DGL_OLD_TEXCOORDS)
    const GLuint tex=createF(16);drawTexcoordPage(tex);
#elif defined(C3DGL_OLD_TEXTURE)
    const GLuint tex=createF(16);drawCell(0,0,tex);
#else
    const GLuint tex=createCheckerTexture();drawCube(tex,0,0,0,200,240,true);
#endif
#ifndef C3DGL_COMPARE_NATIVE
    const uint8_t *display=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,nullptr,nullptr);
#endif
    c3dglSwapBuffers();
    int readback=0;
#ifdef C3DGL_COMPARE_NATIVE
    readback=c3dglNativeReadback(capture,sizeof(capture));
#else
    C3D_FrameSync();
    readback=svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE,(u32)display,sizeof(capture));
    memcpy(capture,display,sizeof(capture));
#endif
    GLenum error=glGetError();
    FILE *image=fopen("sdmc:/c3dgl-old-" CASE "-" BACKEND ".ppm","wb");
    if(image) {
        fprintf(image,"P6\n400 240\n255\n");
        for(int y=239;y>=0;y--) for(int x=0;x<400;x++) {
            const uint8_t *p=capture+(x*240+y)*3;const uint8_t rgb[]={p[2],p[1],p[0]};fwrite(rgb,1,3,image);
        }
        fclose(image);
    }
#if !defined(C3DGL_OLD_PRIMITIVES)
    glDeleteTextures(1,&tex);
#endif
    c3dglClose();
    const bool passed=error==GL_NO_ERROR&&readback==0&&image!=nullptr;
    fprintf(report,"%s: old example=%s backend=%s GL=%u readback=%d image=%d\n",passed?"ALL CHECKS PASSED":"FAILED",CASE,BACKEND,error,readback,image!=nullptr);
    fclose(report);gfxExit();return passed?0:1;
}
