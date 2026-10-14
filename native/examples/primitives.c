/* Six filled GL topologies through client arrays, u32-indexed VBOs and vectors. */
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl_native.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static const GLfloat positions[4][3]={{-.8f,-.8f,0},{.8f,-.8f,0},{.8f,.8f,0},{-.8f,.8f,0}};
static const GLuint order[6][6]={{0,1,2,0,2,3},{0,1,3,2},{0,1,2,3},{0,1,2,3},{0,1,3,2},{0,1,2,3}};
static const GLubyte colors[6][4]={{240,80,40,255},{30,200,230,255},{240,210,30,255},{150,70,220,255},{40,210,70,255},{230,100,170,255}};
static uint8_t pixels[400*240*3];
int main(void) {
    gfxInitDefault();consoleInit(GFX_BOTTOM,NULL);
#ifdef C3DGL_PRIMITIVE_SMOKE
    FILE *report=fopen("sdmc:/c3dgl-primitive-smoke-results.txt","w");if(!report) {gfxExit();return 1;}
    void *prime=vramAlloc(128);if(!prime) {fprintf(report,"FAIL VRAM pool init\n");fclose(report);gfxExit();return 1;}vramFree(prime);
    u32 free_linear=linearSpaceFree(),free_vram=vramSpaceFree();
#endif
    if(!c3dglInit()) {
#ifdef C3DGL_PRIMITIVE_SMOKE
        fprintf(report,"FAIL renderer init\n");fclose(report);
#endif
        gfxExit();return 1;
    }
    GLuint buffers[2];glGenBuffers(2,buffers);
    glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);glBufferData(GL_ARRAY_BUFFER,sizeof(positions),positions,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(order[0]),NULL,GL_DYNAMIC_DRAW);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glEnable(GL_CULL_FACE);glCullFace(GL_BACK);glFrontFace(GL_CCW);
    glClearColor(24.f/255,48.f/255,80.f/255,1);glClear(GL_COLOR_BUFFER_BIT);
    for(unsigned row=0;row<3;row++) for(unsigned col=0;col<6;col++) {
        GLenum mode=GL_TRIANGLES+col;unsigned count=col==0?6:4;
        glViewport(col*66,row*80,66,80);glColor4ubv(colors[col]);
        if(row==0) {
            GLfloat input[6][3];for(unsigned i=0;i<count;i++) memcpy(input[i],positions[order[col][i]],sizeof(input[i]));
            glBindBuffer(GL_ARRAY_BUFFER,0);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
            glVertexPointer(3,GL_FLOAT,sizeof(input[0]),input);glEnableClientState(GL_VERTEX_ARRAY);
            glDrawArrays(mode,0,count);
        } else if(row==1) {
            glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);
            glVertexPointer(3,GL_FLOAT,sizeof(positions[0]),NULL);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,0,count*sizeof(GLuint),order[col]);
            glDrawElements(mode,count,GL_UNSIGNED_INT,NULL);
        } else {
            glBegin(mode);for(unsigned i=0;i<count;i++) {const GLfloat uv[]={0,0};glTexCoord2fv(uv);glVertex3fv(positions[order[col][i]]);}glEnd();
        }
    }
    int failures=0,present=c3dglNativePresent();GLenum error=glGetError();
    if(present || error || c3dglNativeReadback(pixels,sizeof(pixels))) failures++;
#ifdef C3DGL_PRIMITIVE_SMOKE
    fprintf(report,"%s present=%d GL=%u\n",failures?"FAIL":"PASS",present,error);
    unsigned checks=1;
    for(unsigned row=0;row<3;row++) for(unsigned col=0;col<6;col++) {
        unsigned x=col*66+33,y=row*80+40;const uint8_t *p=pixels+(x*240+y)*3;
        int pass=abs(p[2]-colors[col][0])<=1&&abs(p[1]-colors[col][1])<=1&&abs(p[0]-colors[col][2])<=1;
        fprintf(report,"%s mode=%u path=%u RGB=%u,%u,%u\n",pass?"PASS":"FAIL",GL_TRIANGLES+col,row,p[2],p[1],p[0]);checks++;failures+=!pass;
    }
    for(unsigned row=1;row<3;row++) for(unsigned col=0;col<6;col++) {
        int pass=1;
        for(unsigned x=col*66;x<(col+1)*66;x++) for(unsigned y=0;y<80;y++) {
            if(memcmp(pixels+(x*240+y)*3,pixels+(x*240+y+row*80)*3,3)) pass=0;
        }
        fprintf(report,"%s complete cell match mode=%u path=%u\n",pass?"PASS":"FAIL",GL_TRIANGLES+col,row);checks++;failures+=!pass;
    }
    FILE *image=fopen("sdmc:/c3dgl-primitive-smoke.ppm","wb");
    if(image) {
        fprintf(image,"P6\n400 240\n255\n");for(int y=239;y>=0;y--) for(unsigned x=0;x<400;x++) {const uint8_t *p=pixels+(x*240+y)*3;uint8_t rgb[]={p[2],p[1],p[0]};fwrite(rgb,1,3,image);}fclose(image);
    } else {fprintf(report,"FAIL image write\n");failures++;}
#else
    printf("Triangles / strip / fan / quads / quad-strip / polygon\nRows: arrays / indexed VBO / immediate\nCulling enabled; START exits\nGL error=%u, present=%d\n",error,present);
    while(aptMainLoop()&&!failures) {hidScanInput();if(hidKeysDown()&KEY_START) break;gspWaitForVBlank();}
#endif
    glDeleteBuffers(2,buffers);c3dglClose();
#ifdef C3DGL_PRIMITIVE_SMOKE
    int recovered=free_linear==linearSpaceFree()&&free_vram==vramSpaceFree();
    fprintf(report,"%s GPU memory recovery linear=%lu/%lu VRAM=%lu/%lu\n",recovered?"PASS":"FAIL",(unsigned long)linearSpaceFree(),(unsigned long)free_linear,(unsigned long)vramSpaceFree(),(unsigned long)free_vram);checks++;failures+=!recovered;
    fprintf(report,"%s: %u checks, %d failures\n",failures?"FAILED":"ALL CHECKS PASSED",checks,failures);fclose(report);
#endif
    gfxExit();return failures?1:0;
}
