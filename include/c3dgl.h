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
//   - glBegin/glEnd and glDrawArrays/glDrawElements (client arrays of all GL 1.1 / ES 1.1 types) with all primitives
//     (points, lines, line strips/loops, triangles, triangle strips/fans, quads, quad strips, convex polygons)
//   - Matrix stacks (modelview, projection, texture), glOrtho/glFrustum/glTranslate/glRotate/glScale/glMultMatrix
//   - Textures: RGBA8, RGB8, LA8, L8, A8, RGB565, RGBA5551, RGBA4; any size up to 1024x1024
//   - Texture environment: GL_MODULATE, GL_REPLACE, GL_DECAL, GL_BLEND, GL_ADD, GL_COMBINE (incl. DOT3)
//   - Mipmaps (all levels, mipmap filters, GL_GENERATE_MIPMAP)
//   - Compressed textures: paletted (GL_OES_compressed_paletted_texture, expanded on load) and ETC1 (native)
//   - Attribute stacks (glPushAttrib, glPushClientAttrib)
//   - Display lists (glNewList, glCallList(s), ...)
//   - Multitexturing: 3 units (glActiveTexture, glClientActiveTexture, glMultiTexCoord)
//   - Blending, logic ops, alpha test, stencil, depth test/mask, color mask, face culling, scissor, viewport, line width, point size
//   - Point parameters (size min/max, distance attenuation) and point sprites (GL_OES_point_sprite)
//   - glClear of color/depth/stencil honors scissor and write masks
//   - glGetError, glGet*v, glIsEnabled; common glVertex/glColor/glTexCoord/glRect/matrix variants
//   - Lighting: 8 lights, materials, glColorMaterial, two-sided, GL_NORMALIZE/GL_RESCALE_NORMAL (per vertex, on the CPU)
//   - Fog: GL_LINEAR, GL_EXP, GL_EXP2 per pixel (PICA fog table, eye distance |z_eye|)
//   - 6 user clip planes (glClipPlane), clipped on the CPU
//   - Texture coordinate generation (glTexGen: object linear, eye linear, sphere map), per vertex on the CPU
//   - GL_FLAT, glPolygonMode (with edge flags), glPolygonOffset, glDepthRange
//   - Pixel store modes, texture borders, proxy textures, glGetTexLevelParameter
//   - glRasterPos, glBitmap, glDrawPixels, glCopyPixels, glPixelZoom (color on the GPU, depth/stencil on the CPU)
//   - GLU: Mesa GLU as c3dgl::glu (<GL/glu.h>), see README
//   - Buffer objects (VBOs) for vertex arrays and indices
//   - OpenGL ES 1.1: <GLES/gl.h>, fixed-point x functions, glOrthof/glFrustumf/... for the implemented features
//
// Mipmaps work down to 8x8; smaller levels are accepted but PICA cannot sample them. Points are always square (no GL_POINT_SMOOTH).
//
// Screens: rendering goes to the top screen (400x240) by default. c3dglSetScreen() switches to the
// bottom screen (320x240) and back, also within a frame; both are presented by c3dglSwapBuffers().
// The bottom screen is only touched once something is drawn on it, until then it can be used for
// consoleInit(). Do not use the console on a screen that c3dgl renders to.
#ifndef C3DGL_H
#define C3DGL_H

#include <stdbool.h>

#define C3DGL_TOP_SCREEN_WIDTH      400     // Landscape
#define C3DGL_BOTTOM_SCREEN_WIDTH   320
#define C3DGL_SCREEN_HEIGHT         240     // Both screens

typedef enum {
    C3DGL_SCREEN_TOP = 0,
    C3DGL_SCREEN_BOTTOM,
} C3DGLscreen;

#ifdef __cplusplus
extern "C" {
#endif

// Initialize citro3d and the render targets of both screens; the top screen is current.
// Requires gfxInit*() to be done. Call before any gl* function.
bool c3dglInit(void);
void c3dglClose(void);

// Select the screen that following gl* calls draw on and glClear clears (like binding a framebuffer).
// All other GL state is shared, except that viewport and scissor box are reset to the full screen.
void c3dglSetScreen(C3DGLscreen screen);
C3DGLscreen c3dglGetScreen(void);
int c3dglGetScreenWidth(C3DGLscreen screen);   // 400 or 320; the height is always C3DGL_SCREEN_HEIGHT

// Submit everything drawn since the last call and present it on the screens drawn on in this frame.
// The next frame starts lazily with the next gl* call that draws or clears;
// that is also where citro3d waits for VBlank (C3D_FRAME_SYNCDRAW).
void c3dglSwapBuffers(void);

#ifdef __cplusplus
}
#endif

#endif // C3DGL_H
