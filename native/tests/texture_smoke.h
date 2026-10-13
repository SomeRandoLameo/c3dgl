/* Single emulator launch: actual GPU readback, deterministic texture/blend samples. */
#include <string.h>
#include <stdlib.h>
static uint8_t smoke_pixels[400*240*3], smoke_nearest[400*240*3];
static FILE *smoke_report;
static unsigned smoke_checks, smoke_failures;
static void smoke_pixel(const char *label,int x,int y,int r,int g,int b) {
    const uint8_t *p=smoke_pixels+(x*240+y)*3;
    int pass=abs(p[2]-r)<=2 && abs(p[1]-g)<=2 && abs(p[0]-b)<=2;
    fprintf(smoke_report,"%s %s (%d,%d): actual %u,%u,%u expected %d,%d,%d\n",pass?"PASS":"FAIL",label,x,y,p[2],p[1],p[0],r,g,b);
    smoke_checks++;smoke_failures+=!pass;
}
static void smoke_save(unsigned frame) {
    char name[96];snprintf(name,sizeof(name),"sdmc:/c3dgl-texture-smoke-%u.ppm",frame);
    FILE *image=fopen(name,"wb");if(!image) {smoke_failures++;fprintf(smoke_report,"FAIL image write\n");return;}
    fprintf(image,"P6\n400 240\n255\n");
    for(int y=239;y>=0;y--) for(int x=0;x<400;x++) {
        const uint8_t *p=smoke_pixels+(x*240+y)*3;uint8_t rgb[]={p[2],p[1],p[0]};fwrite(rgb,1,3,image);
    }
    fclose(image);
}
static void smoke_validate(unsigned frame) {
    if(c3dglNativeReadback(smoke_pixels,sizeof(smoke_pixels))) {smoke_failures++;fprintf(smoke_report,"FAIL readback frame %u\n",frame);return;}
    smoke_pixel("clear",5,5,20,31,51);
    if(frame==1) {
        smoke_pixel("UV and repeat",50,60,30,80,230);
        smoke_pixel("row orientation",50,170,240,200,40);
        smoke_pixel("queued draw retains pre-update image",40,40,30,80,230);
        smoke_pixel("updated image alpha over VBO",170,80,130,210,45);
        smoke_pixel("alpha over background",340,100,25,56,141);
    } else if(frame==2) {
        smoke_pixel("update visible next frame",40,40,20,220,50);
        memcpy(smoke_nearest,smoke_pixels,sizeof(smoke_pixels));
    } else if(frame==3) {
        int changed=memcmp(smoke_nearest,smoke_pixels,sizeof(smoke_pixels))!=0;
        fprintf(smoke_report,"%s linear filtering changes GPU image\n",changed?"PASS":"FAIL");smoke_checks++;smoke_failures+=!changed;
    } else if(frame==4) smoke_pixel("clamp to edge",50,170,30,200,230);
    else if(frame==5) smoke_pixel("mirrored repeat",50,170,30,80,230);
    else if(frame==6) smoke_pixel("reverse subtract",170,80,220,0,0);
    else if(frame==7) smoke_pixel("minimum equation",170,80,20,200,40);
    smoke_save(frame);fflush(smoke_report);
}
