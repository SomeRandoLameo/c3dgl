// Shared capture entry point. Geometry, textures and GL draw calls come directly
// from the existing example TU; the interactive main is not selected here.
#define main c3dglOriginalInteractiveMain
#if defined(C3DGL_OLD_PRIMITIVES) || defined(C3DGL_OLD_PRIMITIVES2) || defined(C3DGL_OLD_PRIMITIVES3)
#include "../../examples/primitives/main.cpp"
#ifdef C3DGL_OLD_PRIMITIVES3
#define CASE "primitives3"
#elif defined(C3DGL_OLD_PRIMITIVES2)
#define CASE "primitives2"
#else
#define CASE "primitives"
#endif
#elif defined(C3DGL_OLD_TEXTURE) || defined(C3DGL_OLD_TEXCOORDS) || defined(C3DGL_OLD_TEXMATRIX) || defined(C3DGL_OLD_MULTITEXTURE) || defined(C3DGL_OLD_MIPMAP) || defined(C3DGL_OLD_COMPRESSED)
#include "../../examples/texture/main.cpp"
#ifdef C3DGL_OLD_COMPRESSED
#define CASE "compressed"
#elif defined(C3DGL_OLD_MIPMAP)
#define CASE "mipmap"
#elif defined(C3DGL_OLD_MULTITEXTURE)
#define CASE "multitexture"
#elif defined(C3DGL_OLD_TEXMATRIX)
#define CASE "texmatrix"
#elif defined(C3DGL_OLD_TEXCOORDS)
#define CASE "texcoords"
#else
#define CASE "texture"
#endif
#elif defined(C3DGL_OLD_LIGHTING)
#include "../../examples/lighting/main.cpp"
#define CASE "lighting"
#elif defined(C3DGL_OLD_FRAGMENT) || defined(C3DGL_OLD_FRAGMENT2)
#include "../../examples/fragment/main.cpp"
#ifdef C3DGL_OLD_FRAGMENT2
#define CASE "fragment2"
#else
#define CASE "fragment"
#endif
#elif defined(C3DGL_OLD_GLU0) || defined(C3DGL_OLD_GLU1) || defined(C3DGL_OLD_GLU2)
#include "../../examples/glu/main.cpp"
#if defined(C3DGL_OLD_GLU2)
#define CASE "glu2"
#elif defined(C3DGL_OLD_GLU1)
#define CASE "glu1"
#else
#define CASE "glu0"
#endif
#elif defined(C3DGL_OLD_ACCUM)
#include "../../examples/accum/main.cpp"
#define CASE "accum"
#elif defined(C3DGL_OLD_STIPPLE)
#include "../../examples/stipple/main.cpp"
#define CASE "stipple"
#elif defined(C3DGL_OLD_SELECT)
#include "../../examples/select/main.cpp"
#define CASE "select"
#elif defined(C3DGL_OLD_LISTS)
#include "../../examples/lists/main.cpp"
#define CASE "lists"
#elif defined(C3DGL_OLD_CLIPPLANE)
#include "../../examples/clipplane/main.cpp"
#define CASE "clipplane"
#elif defined(C3DGL_OLD_FOG)
#include "../../examples/fog/main.cpp"
#define CASE "fog"
#elif defined(C3DGL_OLD_TEXGEN)
#include "../../examples/texgen/main.cpp"
#define CASE "texgen"
#else
#include "../../examples/cube/main.cpp"
#ifdef C3DGL_OLD_OVERLAY
#define CASE "overlay"
#else
#define CASE "cube"
#endif
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
#if defined(C3DGL_OLD_PRIMITIVES3)
    // Page 3 of the original main loop, call for call
    const GLuint checker=createChecker();
    beginCell(0,0);drawEvalCurve();
    beginCell(1,0);drawEvalColorCurve();
    beginCell(2,0);drawEvalPoints();
    beginCell(3,0);drawEvalRational();
    drawEvalSurfaces(checker);
    glDeleteTextures(1,&checker);
#elif defined(C3DGL_OLD_PRIMITIVES2)
    // Page 2 of the original main loop, call for call
    beginCell(0,0);drawFlatFan();
    beginCell(1,0);drawFlatPolygon();
    beginCell(2,0);drawLineMode();
    beginCell(3,0);drawPointMode();
    beginCell(0,1);drawFrontBack();
    beginCell(1,1);drawEdgeFlags();
    beginCell(2,1);drawPolygonOffset();
    beginCell(3,1);drawDepthRange();
#elif defined(C3DGL_OLD_PRIMITIVES)
    // Page 1 of the original main loop, call for call
    glEnable(GL_CULL_FACE);glCullFace(GL_BACK);glFrontFace(GL_CCW);
    beginCell(0,0);drawPoints();
    beginCell(1,0);drawLineStrip();
    beginCell(2,0);drawLineLoop();
    beginCell(3,0);drawTriangleStrip();
    beginCell(0,1);drawTriangleFan();
    beginCell(1,1);drawQuadStrip();
    beginCell(2,1);drawPolygon();
    beginCell(3,1);drawElements();
    glDisable(GL_CULL_FACE);
#elif defined(C3DGL_OLD_COMPRESSED)
    const CompressedTextures comp=createCompressedTextures();const GLuint tex=comp.rgb8;drawCompressedPage(comp);
#elif defined(C3DGL_OLD_MIPMAP)
    const MipTextures mip=createMipTextures();const GLuint tex=mip.levels;drawMipmapPage(mip);
#elif defined(C3DGL_OLD_MULTITEXTURE)
    const GLuint tex=createF(16);const MultiTextures multi=createMultiTextures(tex);drawMultitexturePage(multi);
#elif defined(C3DGL_OLD_TEXMATRIX)
    const GLuint tex=createF(16), npot=createF(12);drawTextureMatrixPage(tex,npot,0.0f);
    glDeleteTextures(1,&npot);
#elif defined(C3DGL_OLD_TEXCOORDS)
    const GLuint tex=createF(16);drawTexcoordPage(tex);
#elif defined(C3DGL_OLD_TEXTURE)
    const GLuint tex=createF(16);drawCell(0,0,tex);
#elif defined(C3DGL_OLD_GLU0)
    // Page 1 of the original main loop: quadrics
    const GLuint tex=createChecker();
    GLUquadric* quad=gluNewQuadric();
    drawQuadricsPage(quad,tex,0.0f);
    gluDeleteQuadric(quad);
#elif defined(C3DGL_OLD_GLU1)
    // Page 2 of the original main loop: tessellation and NURBS curve/surface
    const GLuint tex=0;
    GLUtesselator* tess=newTess();
    GLUnurbs* nurbs=newNurbs();
    initBumpPatch();
    drawTessellation(tess);
    drawNurbs(nurbs,0.0f);
    gluDeleteNurbsRenderer(nurbs);
    gluDeleteTess(tess);
#elif defined(C3DGL_OLD_GLU2)
    // Page 3 of the original main loop: NURBS renderer display modes
    const GLuint tex=createChecker();
    initBumpPatch();
    const Renderers renderers={newRenderer(GLU_FILL),newRenderer(GLU_OUTLINE_POLYGON),newRenderer(GLU_OUTLINE_PATCH)};
    drawNurbsRendererPage(renderers,0.0f,tex);
#elif defined(C3DGL_OLD_ACCUM)
    // Frames 1..5 of the original main loop: the motion blur and the depth of field build on the earlier frames
    setupScene();
    const GLuint tex=0;
    float lens[kViews][2];
    makeLens(lens);
    for(int frame=1;frame<=5;frame++) drawFrame((float)frame,true,lens);
#elif defined(C3DGL_OLD_STIPPLE)
    // The first frame of the original main loop (angle 0.5)
    const GLuint tex=createTexture();
    setupScene();
    drawFrame(0.5f,tex);
    glDeleteTextures(1,&tex);
#elif defined(C3DGL_OLD_SELECT)
    // The first frame of the original main loop: initial angle and cursor, GL_SELECT then GL_FEEDBACK
    setupScene();
    const GLuint tex=0;
    int picked=0;
    // Walk the cursor over the screen until a cube is picked: the frame then shows the highlighted cube and
    // its feedback outline (both backends must pick the same cube at the same position)
    static const int spots[][2]={{150,100},{250,100},{120,120},{280,120},{160,150},{240,150},{140,90},{260,90},{110,150},{290,150}};
    for(const auto &spot:spots){ if(picked) break; drawFrame(0.0f,(float)spot[0],(float)spot[1],picked); }
    if(!picked) drawFrame(0.0f,200.0f,120.0f,picked);
#elif defined(C3DGL_OLD_LISTS)
    // The first frame of the original main loop through display lists (angle 0, view 20/30)
    const GLuint lists=createLists();
    setupScene();
    const GLuint tex=0;
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    drawScene(lists,true,0.0f,20.0f,30.0f);
    glDeleteLists(lists,3);
#elif defined(C3DGL_OLD_CLIPPLANE)
    // The cells of the original main loop, call for call
    createChecker();const GLuint tex=checkerTexture;
    beginCell(0,0);drawGradient();endCell();
    beginCell(1,0);drawTransformed();endCell();
    beginCell(2,0);drawSixPlanes();endCell();
    beginCell(3,0);drawLinesPoints();endCell();
    beginCell(0,1);drawOutlines();endCell();
    beginCell(1,1);drawSphere();endCell();
    beginCell(2,1);drawHexagon();endCell();
    beginCell(3,1);drawFloor();endCell();
#elif defined(C3DGL_OLD_FRAGMENT2)
    // Page 2 of the original main loop, call for call
    const GLuint tex=0;
    glClearStencil(3);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    beginCell(0,0);drawStencilInside();
    beginCell(1,0);drawStencilClearValue();
    beginCell(2,0);drawStencilCount(2,0);
    beginCell(3,0);drawStencilInvert(3,0);
    beginCell(0,1);drawScissorClear(0,1);
    beginCell(1,1);drawColorMaskClear(1,1);
    beginCell(2,1);drawDepthClearKeepsStencil(2,1);
    beginCell(3,1);drawStencilClearKeepsDepth(3,1);
#elif defined(C3DGL_OLD_FRAGMENT)
    // Page 1 of the original main loop, call for call
    const Textures textures={createSprite(),createAlphaDisc(),createChecker(GL_RGB),createChecker(GL_LUMINANCE)};
    const GLuint tex=textures.sprite;
    beginCell(0,0);drawAlphaGradient(GL_GREATER);
    beginCell(1,0);drawAlphaGradient(GL_LESS);
    beginCell(2,0);drawCutout(tex);
    beginCell(3,0);drawCutoutDepth(tex);
    beginCell(0,1);drawModulate(textures);
    beginCell(1,1);drawReplace(textures);
    beginCell(2,1);drawDecal(textures);
    beginCell(3,1);drawBlendAdd(textures);
#elif defined(C3DGL_OLD_FOG)
    // The cells of the original main loop, call for call
    const GLuint tex=0;glEnable(GL_DEPTH_TEST);
    beginCell(0,0);drawLinear();endCell();
    beginCell(1,0);drawExp();endCell();
    beginCell(2,0);drawExp2();endCell();
    beginCell(3,0);drawOrtho();endCell();
    beginCell(0,1);drawFloor();endCell();
    beginCell(1,1);drawDepthRange();endCell();
    beginCell(2,1);drawLinesPoints();endCell();
    beginCell(3,1);drawAlpha();endCell();
#elif defined(C3DGL_OLD_TEXGEN)
    // The first frame of the original main loop: initial angle, scroll offset and tilt
    setupScene();
    const Torus torus(1.0f,0.45f,32,16);
    const GLuint environment=makeEnvironment(),stripes=makeStripes(),tex=environment;
    drawFrame(torus,environment,stripes,0.0f,0.0f,30.0f,0.0f);
    glDeleteTextures(1,&stripes);
#elif defined(C3DGL_OLD_LIGHTING)
    // The cells of the original main loop, call for call
    const GLuint tex=0;const Sphere sphere=createSphere(24,16);
    beginCell(0,0);drawDiffuse(sphere);endCell();
    beginCell(1,0);drawSpecular(sphere);endCell();
    beginCell(2,0);drawPointLight();endCell();
    beginCell(3,0);drawSpotLight();endCell();
    beginCell(0,1);drawThreeLights(sphere);endCell();
    beginCell(1,1);drawColorMaterial(sphere);endCell();
    beginCell(2,1);drawTwoSided();endCell();
    beginCell(3,1);drawFlatScaled(sphere);endCell();
#elif defined(C3DGL_OLD_OVERLAY)
    const GLuint tex=0;drawOverlay();
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
#if !defined(C3DGL_OLD_PRIMITIVES) && !defined(C3DGL_OLD_PRIMITIVES2) && !defined(C3DGL_OLD_PRIMITIVES3)
    glDeleteTextures(1,&tex);
#endif
    c3dglClose();
    const bool passed=error==GL_NO_ERROR&&readback==0&&image!=nullptr;
    fprintf(report,"%s: old example=%s backend=%s GL=%u readback=%d image=%d\n",passed?"ALL CHECKS PASSED":"FAILED",CASE,BACKEND,error,readback,image!=nullptr);
    fclose(report);gfxExit();return passed?0:1;
}
