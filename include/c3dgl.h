// c3dgl: OpenGL 1.1 subset for the Nintendo 3DS on citro3d (GPU), see GL/gl.h.
//
// This header is the platform side: setup and buffer swap, like EGL/GLX on a PC.
// It does not include <3ds.h>, so it can be used next to headers that clash with libctru.
//
// Usage:
//     gfxInitDefault();
//     c3dglInit();
//     while (aptMainLoop()) {
//         glClear(GL_COLOR_BUFFER_BIT);
//         ... immediate mode or client arrays ...
//         c3dglSwapBuffers();
//     }
//     c3dglClose();
//     gfxExit();
//
// Supported:
//   - glBegin/glEnd and glDrawArrays/glDrawElements (client arrays) with GL_TRIANGLES, GL_QUADS, GL_LINES
//   - Matrix stacks (modelview, projection), glOrtho/glFrustum/glTranslate/glRotate/glScale/glMultMatrix
//   - Textures: RGBA8, RGB8, LA8, L8, A8, RGB565, RGBA5551, RGBA4; any size up to 1024x1024
//   - Blending, depth test/mask, color mask, face culling, scissor, viewport, line width
//
// Not supported: lighting, fog, texture environment modes (always vertex color * texture),
// mipmaps, glReadPixels, GL_POINTS/strips/fans/polygons, glPolygonMode other than GL_FILL.
// Rendering goes to the top screen at 400x240, the bottom screen is left alone (e.g. for consoleInit()).
#ifndef C3DGL_H
#define C3DGL_H

#include <stdbool.h>

#define C3DGL_SCREEN_WIDTH      400     // Top screen, landscape
#define C3DGL_SCREEN_HEIGHT     240

#ifdef __cplusplus
extern "C" {
#endif

// Initialize citro3d and the top screen render target.
// Requires gfxInit*() to be done. Call before any gl* function.
bool c3dglInit(void);
void c3dglClose(void);

// Submit everything drawn since the last call and present it on the top screen.
// The next frame starts lazily with the next gl* call that draws or clears;
// that is also where citro3d waits for VBlank (C3D_FRAME_SYNCDRAW).
void c3dglSwapBuffers(void);

#ifdef __cplusplus
}
#endif

#endif // C3DGL_H
