/* Shared immutable inputs and GL calls for native/reference acceptance. */
#ifndef C3DGL_DEPTH_SCENE_H
#define C3DGL_DEPTH_SCENE_H
#include <stdint.h>
typedef struct {float position[3];uint8_t color[4];} SceneVertex;
typedef struct {int16_t position[4];int16_t tex[2];uint8_t color[4];} CompactVertex;
static const CompactVertex compact_triangle[]={
    {{-80,-70,0,0},{0,0},{255,0,0,255}},{{80,-70,0,0},{0,0},{0,255,0,255}},{{0,80,0,0},{0,0},{0,0,255,255}}
};
static const SceneVertex cube_vertices[]={
    {{-0.5000000f,-0.5000000f,0.5000000f},{255,0,0,255}},
    {{0.5000000f,-0.5000000f,0.5000000f},{255,0,0,255}},
    {{0.5000000f,0.5000000f,0.5000000f},{255,0,0,255}},
    {{-0.5000000f,0.5000000f,0.5000000f},{255,0,0,255}},
    {{-0.5000000f,-0.5000000f,-0.5000000f},{0,255,0,255}},
    {{-0.5000000f,0.5000000f,-0.5000000f},{0,255,0,255}},
    {{0.5000000f,0.5000000f,-0.5000000f},{0,255,0,255}},
    {{0.5000000f,-0.5000000f,-0.5000000f},{0,255,0,255}},
    {{0.5000000f,-0.5000000f,-0.5000000f},{0,0,255,255}},
    {{0.5000000f,0.5000000f,-0.5000000f},{0,0,255,255}},
    {{0.5000000f,0.5000000f,0.5000000f},{0,0,255,255}},
    {{0.5000000f,-0.5000000f,0.5000000f},{0,0,255,255}},
    {{-0.5000000f,-0.5000000f,0.5000000f},{255,255,0,255}},
    {{-0.5000000f,0.5000000f,0.5000000f},{255,255,0,255}},
    {{-0.5000000f,0.5000000f,-0.5000000f},{255,255,0,255}},
    {{-0.5000000f,-0.5000000f,-0.5000000f},{255,255,0,255}},
    {{-0.5000000f,0.5000000f,0.5000000f},{255,0,255,255}},
    {{0.5000000f,0.5000000f,0.5000000f},{255,0,255,255}},
    {{0.5000000f,0.5000000f,-0.5000000f},{255,0,255,255}},
    {{-0.5000000f,0.5000000f,-0.5000000f},{255,0,255,255}},
    {{-0.5000000f,-0.5000000f,-0.5000000f},{0,255,255,255}},
    {{0.5000000f,-0.5000000f,-0.5000000f},{0,255,255,255}},
    {{0.5000000f,-0.5000000f,0.5000000f},{0,255,255,255}},
    {{-0.5000000f,-0.5000000f,0.5000000f},{0,255,255,255}}
};
static const uint16_t cube_indices[]={0,1,2,0,2,3,4,5,6,4,6,7,8,9,10,8,10,11,12,13,14,12,14,15,16,17,18,16,18,19,20,21,22,20,22,23};
static const float cube_phases[16][2]={{1.0000000f,0.0000000f},{0.9238795f,0.3826834f},{0.7071068f,0.7071068f},{0.3826834f,0.9238795f},{0.0000000f,1.0000000f},{-0.3826834f,0.9238795f},{-0.7071068f,0.7071068f},{-0.9238795f,0.3826834f},{-1.0000000f,0.0000000f},{-0.9238795f,-0.3826834f},{-0.7071068f,-0.7071068f},{-0.3826834f,-0.9238795f},{0.0000000f,-1.0000000f},{0.3826834f,-0.9238795f},{0.7071068f,-0.7071068f},{0.9238795f,-0.3826834f}};
static const SceneVertex depth_vertices[]={
    {{-.8f,-.7f,-.5f},{255,0,0,255}},{{.8f,-.7f,-.5f},{255,0,0,255}},{{0,.8f,-.5f},{255,0,0,255}},
    {{-.8f,-.7f,.5f},{0,255,0,255}},{{.8f,-.7f,.5f},{0,255,0,255}},{{0,.8f,.5f},{0,255,0,255}},
    {{-.8f,-.7f,-.5f},{0,0,255,255}},{{.8f,-.7f,-.5f},{0,0,255,255}},{{0,.8f,-.5f},{0,0,255,255}}
};
static const uint16_t depth_indices[]={0,1,2,3,4,5,6,7,8};
static const uint8_t depth_expected[20][3]={
    {255,0,0},{0,0,255},{0,255,0},{0,0,255},{255,0,0},{0,255,0},{255,0,0},{0,255,0},
    {0,255,0},{255,0,0},{0,0,255},{255,0,0},{255,0,0},{24,48,80},{24,48,80},{24,48,80},
    {255,0,0},{255,0,0},{255,0,0},{255,0,0}
};
static void scene_setup(const SceneVertex *v,size_t vertex_bytes,const uint16_t *indices,size_t index_bytes,GLuint *buffers) {
#ifdef C3DGL_REFERENCE_TEST
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
#endif
    glGenBuffers(2,buffers);glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);
    glBufferData(GL_ARRAY_BUFFER,vertex_bytes,v,GL_STATIC_DRAW);
    glVertexPointer(3,GL_FLOAT,sizeof(SceneVertex),NULL);glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(SceneVertex),(const void *)12);
    glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_COLOR_ARRAY);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);glBufferData(GL_ELEMENT_ARRAY_BUFFER,index_bytes,indices,GL_STATIC_DRAW);
}
static void scene_defaults(void) {
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glViewport(0,0,400,240);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);glDepthRange(0,1);glClearDepth(1);
    glCullFace(GL_BACK);glFrontFace(GL_CCW);glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);
}
static void compact_setup(const CompactVertex *vertices,size_t bytes,const uint16_t *indices,size_t index_bytes,GLuint *buffers) {
    glGenBuffers(2,buffers);glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);glBufferData(GL_ARRAY_BUFFER,bytes,vertices,GL_STATIC_DRAW);
    glVertexPointer(3,GL_SHORT,sizeof(CompactVertex),NULL);glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(CompactVertex),(const void *)12);
    glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_COLOR_ARRAY);
#ifdef C3DGL_REFERENCE_TEST
    glTexCoordPointer(2,GL_SHORT,sizeof(CompactVertex),(const void *)8);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
#endif
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);glBufferData(GL_ELEMENT_ARRAY_BUFFER,index_bytes,indices,GL_STATIC_DRAW);
}
static void cube_setup(GLuint *buffers) {
    scene_defaults();CompactVertex packed[24];
    for(unsigned i=0;i<24;i++) {
        for(unsigned k=0;k<3;k++) packed[i].position[k]=(int16_t)(cube_vertices[i].position[k]*1024.f);
        packed[i].position[3]=0;packed[i].tex[0]=packed[i].tex[1]=0;
        for(unsigned k=0;k<4;k++) packed[i].color[k]=cube_vertices[i].color[k];
    }
    compact_setup(packed,sizeof(packed),cube_indices,sizeof(cube_indices),buffers);
    glMatrixMode(GL_PROJECTION);glFrustum(-1,1,-.6,.6,1,10);glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);glEnable(GL_CULL_FACE);
}
static void cube_transform(unsigned phase) {
    float c=cube_phases[phase%16][0],s=cube_phases[phase%16][1];
    const float m[]={c/1024,0,-s/1024,0, .6f*s/1024,.8f/1024,.6f*c/1024,0, .8f*s/1024,-.6f/1024,.8f*c/1024,0, 0,0,-3,1};glLoadMatrixf(m);
}
static void cube_state(unsigned phase) {
    glDepthFunc((phase&1)?GL_LEQUAL:GL_LESS);glFrontFace((phase&1)?GL_CW:GL_CCW);
    glDepthRange((phase&1)? .2:0,(phase&1)? .8:1);
}
static void depth_draw(unsigned triangle) {
#ifdef C3DGL_REFERENCE_TEST
    GLint face;glGetIntegerv(GL_CULL_FACE_MODE,&face);
    if(glIsEnabled(GL_CULL_FACE) && face==GL_FRONT_AND_BACK) {
        /* Frozen filled C paths miss both-face rejection. Force its generic
         * polygon path with a line mode: it rejects both faces on the CPU.
         * This unmeasured correctness cell does not affect benchmark timing. */
        glPolygonMode(GL_FRONT,GL_LINE);glBegin(GL_TRIANGLES);
        for(unsigned i=0;i<3;i++) {
            const SceneVertex *v=depth_vertices+triangle*3+i;
            glColor4ubv(v->color);glVertex3fv(v->position);
        }
        glEnd();glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);return;
    }
#endif
    glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,(const void *)(uintptr_t)(triangle*6));}
static void depth_cell(unsigned cell) {
    glViewport((cell%5)*80,(cell/5)*60,80,60);
    glEnable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDepthMask(GL_TRUE);glDepthRange(0,1);
    glDepthFunc(GL_ALWAYS);
    if (cell<8) {
        const GLenum functions[]={GL_LESS,GL_LEQUAL,GL_GREATER,GL_GEQUAL,GL_EQUAL,GL_NOTEQUAL,GL_NEVER,GL_ALWAYS};
        depth_draw(0);glDepthFunc(functions[cell]);
        if (cell==5) depth_draw(2);
        depth_draw(cell==1 || cell==3?2:1);
    } else if (cell==8) {glDepthFunc(GL_LESS);glDepthMask(GL_FALSE);depth_draw(0);depth_draw(1);}
    else if (cell==9) {glDepthFunc(GL_LESS);depth_draw(0);glDepthMask(GL_FALSE);glClear(GL_DEPTH_BUFFER_BIT);glDepthMask(GL_TRUE);depth_draw(1);}
    else if (cell==10) {glDisable(GL_DEPTH_TEST);depth_draw(0);depth_draw(1);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);depth_draw(2);}
    else if (cell==11) {glDepthRange(1,0);depth_draw(1);glDepthFunc(GL_GREATER);depth_draw(0);}
    else if (cell==16) {glDepthRange(.5,.5);depth_draw(0);glDepthFunc(GL_LESS);depth_draw(1);}
    else if (cell==17) {glDepthRange(.2,.8);depth_draw(1);glDepthFunc(GL_LESS);depth_draw(0);}
    else {
        glEnable(GL_CULL_FACE);glCullFace(cell==13 || cell==18?GL_FRONT:cell==15 || cell==19?GL_FRONT_AND_BACK:GL_BACK);
        glFrontFace(cell==14 || cell==18?GL_CW:GL_CCW);
        if (cell==19) glDisable(GL_CULL_FACE);
        depth_draw(0);
    }
}
#endif
