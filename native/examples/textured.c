/* Resident textured VBO plus alpha-blended immediate geometry and same-frame updates. */
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl_native.h>
#include <stdint.h>
#include <stdio.h>
typedef struct { float position[3]; float uv[2]; uint8_t color[4]; } TexVertex;
static const TexVertex mesh[]={
    {{-.9f,-.75f,0},{0,0},{255,255,255,255}},
    {{.25f,-.75f,0},{2,0},{255,255,255,255}},
    {{.25f,.75f,0},{2,2},{255,255,255,255}},
    {{-.9f,.75f,0},{0,2},{255,255,255,255}}
};
static const uint16_t indices[]={0,1,2,0,2,3};
static uint8_t pixels[16*16*4];
int main(void) {
    gfxInitDefault();consoleInit(GFX_BOTTOM,NULL);
    if(!c3dglInit()) {gfxExit();return 1;}
    for(unsigned y=0;y<16;y++) for(unsigned x=0;x<16;x++) {
        unsigned i=(y*16+x)*4, cell=((x/4)^(y/4))&1;
        pixels[i]=cell?240:30;pixels[i+1]=y<8?80:200;pixels[i+2]=cell?40:230;pixels[i+3]=255;
    }
    GLuint texture,buffers[2];glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glEnable(GL_TEXTURE_2D);
    glGenBuffers(2,buffers);glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);
    glBufferData(GL_ARRAY_BUFFER,sizeof(mesh),mesh,GL_STATIC_DRAW);
    glVertexPointer(3,GL_FLOAT,sizeof(TexVertex),NULL);
    glTexCoordPointer(2,GL_FLOAT,sizeof(TexVertex),(const void *)12);
    glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(TexVertex),(const void *)20);
    glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_COLOR_ARRAY);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    glClearColor(.08f,.12f,.2f,1);
    unsigned frame=0;int result=0;
    printf("Rust textured VBO + alpha blend\nA: nearest/linear, B: repeat/clamp\nSTART exits\n");
    int linear=0,clamp=0;
    while(aptMainLoop()) {
        hidScanInput();u32 keys=hidKeysDown();if(keys&KEY_START) break;
        if(keys&KEY_A) {linear=!linear;glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,linear?GL_LINEAR:GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,linear?GL_LINEAR:GL_NEAREST);}
        if(keys&KEY_B) {clamp=!clamp;glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,clamp?GL_CLAMP_TO_EDGE:GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,clamp?GL_CLAMP_TO_EDGE:GL_REPEAT);}
        glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_BLEND);
        glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,NULL);
        // The left-hand draw keeps the old image; the overlay uses the updated image.
        if((frame++%120)==0) {
            uint8_t patch[4*4*4];for(unsigned i=0;i<sizeof(patch);i+=4) {patch[i]=frame&128?255:20;patch[i+1]=220;patch[i+2]=50;patch[i+3]=255;}
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,patch);
        }
        glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1,1,1,.5f);
        glBegin(GL_TRIANGLES);
        glTexCoord2f(0,0);glVertex2f(-.25f,-.5f);
        glTexCoord2f(1,0);glVertex2f(.9f,-.5f);
        glTexCoord2f(1,1);glVertex2f(.9f,.5f);
        glTexCoord2f(0,0);glVertex2f(-.25f,-.5f);
        glTexCoord2f(1,1);glVertex2f(.9f,.5f);
        glTexCoord2f(0,1);glVertex2f(-.25f,.5f);
        glEnd();
        if(c3dglNativePresent() || glGetError()!=GL_NO_ERROR) {printf("FAIL textured draw\n");result=1;break;}
    }
    glDeleteBuffers(2,buffers);glDeleteTextures(1,&texture);c3dglClose();gfxExit();return result;
}
