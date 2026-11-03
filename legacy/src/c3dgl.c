// c3dgl: OpenGL 1.1 subset for the Nintendo 3DS, implemented on citro3d.
//
// Design:
//   - Immediate mode and client arrays are assembled into triangles in one linear vertex buffer.
//     Vertices are batched across glBegin/glEnd; a batch is drawn with one C3D_DrawArrays() when
//     the draw state (texture, matrices, blend, depth, ...) changes, see prepareDraw().
//     Engines like raylib set/unset the texture around every glyph/shape, so state is compared
//     lazily at draw time instead of flushing inside the gl* setters.
//   - Vertices stay in object space, the vertex shader applies post * projection * modelview.
//     `post` maps OpenGL clip space to PICA clip space: 90 degree screen rotation + depth range [-1, 0].
//   - Strips, fans, quads and polygons are split into triangles on the CPU, which also implements flat shading
//     (provoking vertex color), glPolygonMode (outlines/vertices of each polygon, culled on the CPU) and the
//     slope part of glPolygonOffset (per-vertex depth bias that the vertex shader adds).
//   - Lines and points have no PICA equivalent: they are transformed on the CPU and expanded to quads in NDC.
//   - Lighting is computed per vertex on the CPU when the vertex is submitted (exact GL 1.1 formula); the lit color
//     replaces the vertex color, so flat shading, lines and points need nothing special. Two-sided lighting also
//     computes the back color, emitPolygon() picks one per polygon from its facing.
//   - Textures are padded to power-of-two sizes and Morton-swizzled; the padding repeats the image's edges as the wrap
//     modes sample them (fillPadding()). The shader applies the texture matrix combined with the scale back from the
//     padded size; a projective texture matrix uses PICA's projection mode.
//     Paletted textures are expanded on load; ETC1 blocks are stored as they are, upside down (see uploadEtc1()).
//     Images of any format/type are converted to the PICA format closest to their internal format (see loadTexels());
//     1D textures are 2D textures whose rows all hold the image.
//   - One render target per screen, sharing the vertex buffer and GL state; c3dglSetScreen() flushes the
//     batch and switches the target (C3D_FrameDrawOn), citro3d presents every target drawn on in the frame.
//
//   - Fog uses PICA's fog unit, which looks the fog factor up in a 128 entry table indexed by window depth. GL's factor
//     depends on the eye distance, so the table inverts the projection per entry (see updateFogLut()).
//   - Depth and stencil share one D24S8 buffer that a memory fill can only clear as a whole. glClear uses the
//     fill when that is equivalent, otherwise it draws a full-screen quad (scissor, masks, depth or stencil only).
//   - glDrawPixels/glBitmap draw textured quads in window coordinates; their textures live in per-frame linear memory,
//     bitmaps share an atlas (see drawBitmap()). glCopyPixels copies the color buffer into such a texture with a GX texture
//     copy queued between the draws (copyColorRect()). Depth/stencil images are written on the CPU (drawDepthStencil()).
//   - Feedback and selection (glRenderMode) take the primitives after user clipping, culling and polygon mode, clip them
//     against the view volume on the CPU and write tokens or hit records instead of drawing (see the feedback section).
//   - The accumulation buffer is kept on the CPU (see glAccum()): GL_ACCUM/GL_LOAD read the color buffer back, GL_RETURN
//     draws the result like a glDrawPixels image.
//   - Display lists record the commands with their arguments (client data like pixels, control points and vertex
//     arrays copied at compile time) and replay them through the same gl* entry points, see listSave().
//
// Known limitations: REPEAT wrap on non-power-of-two textures is exact only for texture coordinates in [0, 1] (the
// padding holds the image wrapped around its edges, see fillPadding(); the GPU wraps at the padded size).
#include "GL/gl.h"
#include "c3dgl.h"

#include <3ds.h>
#include <citro3d.h>

#include <arm_acle.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "c3dgl_vsh_shbin.h"

// The implementation is one translation unit (everything is static, the regression tests include this file), split
// into parts for readability. Order matters: C needs a definition before its use.
#include "parts/01_types.c"   // Defines and types: tuning constants, Vertex, textures, buffers, display lists
#include "parts/01a_profile.c" // Optional CPU profiler (C3DGL_PROFILE)
#include "parts/02_state.c"   // Global state (the gl struct), matrix helpers, GL -> PICA enum mapping
#include "parts/03_frame.c"   // Frame and batch management: render state application, draw submission, GPU mesh cache
#include "parts/04_primitives.c"   // Primitive assembly: triangles, lines, points, user clip planes, feedback/selection output
#include "parts/05_lighting.c"   // CPU lighting and texture coordinate generation, per vertex
#include "parts/06_platform.c"   // Platform API (c3dgl.h): init, screens, stereo, swap buffers
#include "parts/07_gl_state.c"   // OpenGL: capabilities and state
#include "parts/08_gl_matrices.c"   // OpenGL: matrices
#include "parts/09_gl_immediate.c"   // OpenGL: immediate mode (glBegin/glEnd/glVertex...)
#include "parts/10_gl_lighting.c"   // OpenGL: lighting parameters
#include "parts/11_gl_fog_clip_texgen.c"   // OpenGL: fog, user clip planes, glTexGen parameters
#include "parts/12_gl_arrays.c"   // OpenGL: client-side vertex arrays, glDrawArrays
#include "parts/13_gl_draw_fast.c"   // Direct-decode fast paths (indexed and array triangles), glDrawElements
#include "parts/14_gl_buffers.c"   // OpenGL: buffer objects
#include "parts/15_pixels.c"   // Client pixel images and pixel transfer
#include "parts/16_gl_textures.c"   // OpenGL: textures
#include "parts/17_gl_evaluators.c"   // OpenGL: evaluators
#include "parts/18_gl_attrib.c"   // OpenGL: attribute stacks
#include "parts/19_framebuffer.c"   // OpenGL: reading and copying the framebuffer
#include "parts/20_gl_drawpixels.c"   // OpenGL: raster position, glDrawPixels, glBitmap, glCopyPixels
#include "parts/21_gl_colorbuffers_accum.c"   // OpenGL: color buffers and accumulation buffer
#include "parts/22_gl_feedback.c"   // OpenGL: glRenderMode, feedback and selection
#include "parts/23_gl_lists.c"   // OpenGL: display lists
#include "parts/24_gl_compat.c"   // Compatibility: GLU link stubs, GL 1.2 stubs, OpenGL ES 1.1 float and fixed-point API
