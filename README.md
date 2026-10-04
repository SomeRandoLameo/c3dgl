# c3dgl

OpenGL 1.1 subset for the Nintendo 3DS, running on the GPU through [citro3d](https://github.com/devkitPro/citro3d).
Code written against classic fixed-function OpenGL (immediate mode, matrix stacks, client arrays) can be
ported to the 3DS without rewriting its renderer. It depends on nothing but libctru and citro3d.

**Status:** early. 2D drawing (shapes, text, lines, textures) runs at a steady 60 FPS in the
[Azahar](https://azahar-emu.org/) emulator. Not yet tested on real hardware; 3D, scissor and sub-viewports
are covered by the cube example but not verified yet. Issues and pull requests are welcome.

## Usage

```c
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>              // and <GL/glu.h> for GLU

int main(void)
{
    gfxInitDefault();
    c3dglInit();                        // top screen (400x240) is current

    while (aptMainLoop())
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
            glColor3f(1, 0, 0); glVertex2f( 0.0f,  0.5f);
            glColor3f(0, 1, 0); glVertex2f(-0.5f, -0.5f);
            glColor3f(0, 0, 1); glVertex2f( 0.5f, -0.5f);
        glEnd();

        c3dglSetScreen(C3DGL_SCREEN_BOTTOM);    // 320x240, viewport is reset to the full screen
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // ... draw the bottom screen ...
        c3dglSetScreen(C3DGL_SCREEN_TOP);

        c3dglSwapBuffers();             // presents both screens, waits for VBlank on the next frame
    }

    c3dglClose();
    gfxExit();
}
```

`<GL/gl.h>` and `<c3dgl.h>` do not include `<3ds.h>`, so they also work next to headers that clash with
libctru (e.g. other libraries defining `KEY_A`). See [`examples/cube`](examples/cube/main.cpp) for a complete program.

### Screens

Both screens have their own render target and share all other GL state (textures, matrices, blending, ...).
`c3dglSetScreen()` selects where the following draws and clears go, it can be switched any number of times per
frame and resets viewport and scissor box to the full screen. `c3dglSwapBuffers()` presents every screen that was
drawn on in that frame, the other one keeps its last picture. The bottom screen is not touched until the first
switch to it, so `consoleInit(GFX_BOTTOM, NULL)` works as long as nothing is rendered there.

## Integration

Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the `3ds-dev` group (devkitARM, libctru,
citro3d, picasso) and `$DEVKITPRO` set. Add c3dgl to your CMake project, e.g. as a git submodule:

```sh
git submodule add https://github.com/SomeRandoLameo/c3dgl.git third_party/c3dgl
```

```cmake
add_subdirectory(third_party/c3dgl)
target_link_libraries(my_app PRIVATE c3dgl::c3dgl)  # c3dgl::glu as well for <GL/glu.h>
```

### GLU

`c3dgl::glu` is [Mesa GLU](https://gitlab.freedesktop.org/mesa/glu) 9.0.3, vendored unmodified in `external/glu`
(SGI Free Software License B 2.0, MIT style; see `external/glu/README.md`). Everything works except what needs GL
features c3dgl does not have yet: NURBS rendering through evaluators (`GLU_NURBS_TESSELLATOR` mode works) and
`gluBuild3DMipmaps` (GL 1.2).
Turn it off with `-DC3DGL_BUILD_GLU=OFF`.

## Building the examples

```sh
cmake -S . -B build          # picks up $DEVKITPRO/cmake/3DS.cmake
cmake --build build          # -> build/examples/<name>/c3dgl_<name>.3dsx
```

- `cube`: 3D, depth test, culling, textures (NPOT and PNG), scissor, sub-viewports
- `primitives`: every primitive mode with culling on to catch wrong winding; page 2: flat shading, polygon modes,
  edge flags, polygon offset, depth range
- `api` (C): self-check of queries, errors, entry point variants, array types, VBOs, attribute stacks, display lists,
  texgen, texture formats, 1D textures, color buffers, pixel drawing and transfer (rendered and read back), feedback
  and selection, line and polygon stipple, the accumulation buffer (rendered and read back) and the ES API; green
  screen = all passed
- `glu`: Mesa GLU on c3dgl: matrices, image scaling, quadrics, numeric self-checks; page 2: tessellator, NURBS
- `texture`: texture features; page 1: texture matrix, page 2: texture coordinates (per-vertex q, array types),
  page 3: multitexturing and `GL_COMBINE`, page 4: mipmaps, page 5: compressed textures (paletted, ETC1)
- `fragment`: per-fragment operations (alpha test, texture environment, stencil, `glClear`); A switches pages
- `lighting`: directional, point and spot lights, specular, several lights, color material, two-sided lighting,
  flat shading with `GL_NORMALIZE`/`GL_RESCALE_NORMAL`; self-checks of the lighting API on the bottom screen
- `fog`: linear/exp/exp2 fog with perspective and orthographic projections, depth range, lines/points and blending,
  each next to a reference square in the expected color; self-checks of the fog API on the bottom screen
- `clipplane`: user clip planes on smooth-shaded, textured and lit geometry, lines, points and `glPolygonMode`
  outlines, mostly next to the expected shape drawn without clipping; self-checks of the clip plane API on the bottom screen
- `points` (ES API): point size min/max, distance attenuation (also through the fixed-point API), the point size
  array, point sprites with `GL_COORD_REPLACE_OES` on one and two units, a particle field with per-particle sizes;
  self-checks of the point API on the bottom screen
- `lists`: display lists, the classic gears: each gear is a list (geometry, normals, material, shade model) drawn with
  `glCallList`; A switches to immediate mode, which looks identical
- `texgen`: texture coordinate generation on tori without texcoords: `GL_SPHERE_MAP` (chrome from a generated
  environment map), `GL_OBJECT_LINEAR` and `GL_EYE_LINEAR` stripes; A scrolls the stripes with the texture matrix
- `pixels`: drawing pixels: text from `glBitmap` display lists (the console font), a label at a cube corner
  (`glRasterPos` in 3D, depth tested), the cube's reflection by `glCopyPixels` with zoom 1 x -0.5, an animated
  color index `glDrawPixels` image (palette in the `glPixelMap` tables, cycled by `GL_INDEX_OFFSET`) with a pulsing
  `glPixelZoom` and its mirror image (zoom -1, drawn as luminance tinted by `glPixelTransfer` scale/bias)
- `select`: picking with a cursor (circle pad / D-pad): `GL_SELECT` with `gluPickMatrix` finds the nearest cube under
  it in a turning ring, `GL_FEEDBACK` gives back the picked cube's front faces in window coordinates, drawn as a yellow
  outline that must sit exactly on the cube; the hit records are listed on the bottom screen
- `stipple`: line stipple (dashes, factor 2, dash-dot, dots, a wide line, a turning star as one line loop), two
  rectangles with complementary halftone patterns that fill each other in, and in perspective a textured screen door
  cube (polygon stipple fixed to the window) circling an opaque cube with dashed `glPolygonMode(GL_LINE)` edges
- `accum`: the accumulation buffer; left: motion blur (a fading trail from `GL_MULT` and `GL_ACCUM` every frame),
  right: depth of field (6 views from points on a lens, focused on the middle cube, averaged with `GL_LOAD`/`GL_ACCUM`);
  the scissor box keeps the halves apart. B switches the accumulation buffer off for comparison

Every example shows the CPU and GPU time of the last frame, the command buffer usage and the frames per second
(averaged over one second) in rows 2-5 of the bottom screen (`C3D_GetProcessingTime`, `C3D_GetDrawingTime`,
`C3D_GetCmdBufUsage`, `osGetTime`).

The cube example needs libpng from the devkitPro portlibs (`3ds-libpng`) to load a PNG texture from its romfs;
c3dgl itself does not.

Run a `.3dsx` in an emulator or send it to a 3DS with `3dslink`. The bottom screen describes the
expected picture. Examples are built by default only when c3dgl is the top-level project
(`-DC3DGL_BUILD_EXAMPLES=ON/OFF`).

`cmake -P scripts/dev.cmake run [app.3dsx]` builds and launches an example (default: cube) in the first emulator it
finds (Azahar, Lime3DS, Mandarine, Citra; override with `C3DGL_EMULATOR=/path/to/emu`); `build` and
`launch` do one step each. It works on macOS, Linux and Windows. On Windows it builds through
devkitPro's msys2 (`C:/devkitPro/msys2`, or set `C3DGL_MSYS2`), which needs `pacman -S cmake` there,
plus a native CMake on `PATH` to run the script. The Zed tasks in `.zed/tasks.json` wrap these commands.

## Supported

- `glBegin`/`glEnd` and client arrays (`glDrawArrays`, `glDrawElements`, `glArrayElement`, `glInterleavedArrays`)
  with every primitive: points, lines, line strips/loops, triangles, triangle strips/fans, quads, quad strips and
  convex polygons; arrays of every GL 1.1 / ES 1.1 type (`GL_BYTE` ... `GL_DOUBLE`, `GL_FIXED`)
- Buffer objects (VBOs): `glGenBuffers`, `glBindBuffer`, `glBufferData`, `glBufferSubData`, ... for vertex arrays and
  indices
- OpenGL ES 1.1 API: `<GLES/gl.h>`, the fixed-point `x` functions and `glOrthof`/`glFrustumf`/... for everything
  implemented
- Modelview/projection/texture matrix stacks (projective texture matrices included), `glOrtho`, `glFrustum`,
  `glTranslatef`, `glRotatef`, `glScalef`, `glMultMatrixf`
- Textures of any size up to 1024x1024: RGBA8, RGB8, luminance/alpha, luminance, alpha, RGB565, RGBA5551, RGBA4;
  `glTexSubImage2D`, `glGetTexImage`, nearest/linear filtering, repeat/clamp/mirror wrapping
- Compressed textures (`glCompressedTexImage2D`): the 10 paletted formats of `GL_OES_compressed_paletted_texture`
  (expanded to the palette's format on load, one image can carry all mip levels) and ETC1
  (`GL_OES_compressed_ETC1_RGB8_texture`, sampled natively by PICA); `GL_COMPRESSED_TEXTURE_FORMATS`
- Texture environment (`glTexEnv`): `GL_MODULATE`, `GL_REPLACE`, `GL_DECAL`, `GL_BLEND`, `GL_ADD`, `GL_TEXTURE_ENV_COLOR`,
  and `GL_COMBINE` with all functions including `GL_DOT3_RGB(A)`
- Multitexturing: 3 texture units (`glActiveTexture`, `glClientActiveTexture`, `glMultiTexCoord*`), each with its own
  environment, texture matrix and texcoord array
- Flat shading with GL's provoking vertices, `glPolygonMode` (fill/line/point per face, with edge flags),
  `glPolygonOffset` (including the slope factor) and `glDepthRange`
- Blending (`glBlendFunc`), logic ops (`glLogicOp`, `GL_COLOR_LOGIC_OP`), alpha test (`glAlphaFunc`), stencil (`glStencilFunc`/`Op`/`Mask`), depth
  test/function/mask, color mask, face culling, scissor, viewport, line width, point size
- Point parameters (`glPointParameter*`: size min/max, distance attenuation) and point sprites
  (`GL_POINT_SPRITE_OES`, `GL_COORD_REPLACE_OES` per texture unit), listed as `GL_OES_point_sprite`; the point size
  array (`glPointSizePointerOES`, `GL_POINT_SIZE_ARRAY_OES`), listed as `GL_OES_point_size_array`
- `glClear` of color, depth and stencil, honoring scissor and write masks
- `glGetError`, `glGet{Boolean,Integer,Float,Double}v` for the common state, `glIsEnabled`, `glIsTexture`
- The common variants of the immediate mode calls (`glVertex2/3/4{f,d,i,s}[v]`, `glColor3/4{f,d,ub}[v]`, ...),
  `glRect*`, `glLoadMatrix*`, `glTranslated`/`glRotated`/`glScaled`
- Mipmaps: all levels, mipmap filters, `GL_GENERATE_MIPMAP`; `glGetTexParameter`
- Attribute stacks: `glPushAttrib`/`glPopAttrib`, `glPushClientAttrib`/`glPopClientAttrib`
- Display lists: `glNewList`/`glEndList` (`GL_COMPILE`, `GL_COMPILE_AND_EXECUTE`), `glCallList`, `glCallLists` (all
  types) with `glListBase`, `glGenLists`, `glDeleteLists`, `glIsList`, nesting 64 deep; client data (pixels, control
  points, vertex arrays) is copied at compile time
- Pixel store modes (alignment, row length, skip rows/pixels, byte swapping), texture borders, proxy textures,
  `glGetTexLevelParameter`
- GLU: the complete Mesa GLU 9.0.3 (`c3dgl::glu`, `<GL/glu.h>`), see [GLU](#glu)
- Lighting: 8 lights (directional, positional with attenuation, spot), materials per face, `glColorMaterial`,
  two-sided lighting, local viewer, `GL_NORMALIZE`, `GL_RESCALE_NORMAL`; `glGetLight`, `glGetMaterial`
- Fog: `GL_LINEAR`, `GL_EXP`, `GL_EXP2`, per pixel; `glFog*` and the fog queries
- User clip planes: 6 (`glClipPlane`, ES `glClipPlanef/x`, `glGetClipPlane*`)
- Texture coordinate generation (`glTexGen*`, `glGetTexGen*`): `GL_OBJECT_LINEAR`, `GL_EYE_LINEAR`, `GL_SPHERE_MAP`
  for s, t, r, q of every texture unit
- Dithering and smoothing can be enabled and queried but have no effect yet
- Multisampling state (`glSampleCoverage`, `GL_MULTISAMPLE`, `GL_SAMPLE_ALPHA_TO_*`): stored and queried; there are no
  sample buffers (`GL_SAMPLE_BUFFERS` = 0), so as the spec says it has no effect
- `glReadPixels`: color in all GL 1.1 formats and types (plus the packed 16-bit ES types), depth and stencil;
  waits for the GPU to finish the draws so far
- `glCopyTexImage2D` / `glCopyTexSubImage2D` (render to texture): internal formats alpha, luminance(-alpha), RGB, RGBA;
  like `glReadPixels` they wait for the GPU, draws before the copy keep the old texels
- Drawing pixels: `glRasterPos*` (all variants; clipped, lit, queries), `glBitmap`, `glDrawPixels` (color in all GL 1.1
  formats and types, depth, stencil, `GL_BITMAP` stencil), `glCopyPixels` (color, depth, stencil), `glPixelZoom`
  (also negative); color goes through all per-fragment operations, texturing does not apply. Depth and stencil
  images are written on the CPU and wait for the GPU; their fragments' raster color is not written
- Pixel transfer: `glPixelTransfer{i,f}` (scale/bias of R, G, B, A and depth, `GL_INDEX_SHIFT/OFFSET`,
  `GL_MAP_COLOR`, `GL_MAP_STENCIL`), `glPixelMap{fv,uiv,usv}` and `glGetPixelMap*` (all 10 tables, 256 entries) for
  `glDrawPixels`, `glReadPixels`, `glCopyPixels` and texture images and copies; `GL_COLOR_INDEX` images (also
  `GL_BITMAP`) drawn or loaded as textures through the `I_TO_*` tables
- Feedback and selection: `glRenderMode`, `glFeedbackBuffer` (all 5 types), `glPassThrough`, `glSelectBuffer`,
  `glInitNames`, `glLoadName`, `glPushName`, `glPopName` (64 names); points, lines (with `GL_LINE_RESET_TOKEN`),
  polygons (clipped, culled, polygon mode) and the raster position of `glBitmap`/`glDrawPixels`/`glCopyPixels`.
  Feedback colors have 8 bits per component, nothing is drawn or cleared meanwhile
- Line stipple (`glLineStipple`, `GL_LINE_STIPPLE`; the counter runs on along strips and loops) and polygon stipple
  (`glPolygonStipple`, `glGetPolygonStipple`, `GL_POLYGON_STIPPLE`) with their queries, attribute groups and display
  lists. Polygon stipple needs texture unit 2 to be unused (drawn without stipple otherwise); with the alpha test off,
  stippled fragments of alpha exactly 1/255 are dropped too
- Accumulation buffer: `glAccum` (`GL_ACCUM`, `GL_LOAD`, `GL_ADD`, `GL_MULT`, `GL_RETURN`), `glClearAccum`,
  `glClear(GL_ACCUM_BUFFER_BIT)`, 16 bits per component, one per screen (allocated at its first use, 750 KB for the
  top screen). Operations work on the CPU within the scissor box; `GL_ACCUM`/`GL_LOAD` wait for the GPU like
  `glReadPixels`, so each costs a few milliseconds
- Top (400x240) and bottom (320x240) screen, see [Screens](#screens)

The full list of functions is `include/GL/gl.h`.

## Not supported

Round points (`GL_POINT_SMOOTH`) and stereoscopic 3D. Mipmap levels
below 8x8 are accepted but not sampled (PICA stops at 8x8).
`GL_REPEAT` on non-power-of-two textures samples the padding.

## How it works

- Vertices are collected in one linear buffer per frame and drawn in batches. A batch is submitted when the
  draw state (texture, matrices, blend, depth, ...) changes; the state is compared when drawing, not in the
  setters, so code that binds and unbinds a texture around every quad still ends up in one draw call. Only the
  parts of the state that differ from the previous batch are sent to the GPU, and the vertex buffer is flushed
  from the CPU cache once before the command list is submitted, not per batch.
- The PICA200 vertex shader (`shaders/c3dgl.v.pica`) applies `post * projection * modelview`, where `post`
  rotates to the 3DS screen orientation and maps depth to PICA's [-1, 0] range.
- Strips, fans, quads and polygons are split into triangles on the CPU. Lines and points have no PICA
  equivalent: they are transformed on the CPU and expanded to screen-aligned quads (points with the attenuated
  size; point sprites get their texcoords per corner, the texture matrix is not applied to them).
- Lighting is computed per vertex on the CPU when the vertex is submitted (GL 1.1's formula), the lit color
  replaces the vertex color. Flat shading, lines and points therefore need nothing extra; two-sided lighting computes
  a back color too, and each polygon takes the one of the side it shows. It costs CPU time per lit vertex; a cache
  of recently lit vertices (keyed by position, normal and color) avoids lighting shared mesh vertices again.
- Texture coordinate generation also runs per vertex on the CPU. Planes, modelview (for eye planes) and the texture
  matrix are folded into one small matrix per unit when one of them changes, so a vertex costs a 3x4 product (plus
  the reflection vector for `GL_SPHERE_MAP`). The texture matrix is applied there with the generated r, which the GPU
  path drops (vertices carry s, t, q), so projective texturing with eye linear s, t, r, q works; the shader gets an
  identity matrix for these units.
- Assembling primitives on the CPU also gives flat shading (the provoking vertex's color on every vertex),
  `glPolygonMode` (outlines and vertices per polygon, culled on the CPU) and the slope part of `glPolygonOffset`
  (computed per polygon, passed as a per-vertex depth bias that the vertex shader adds).
- Fog runs on PICA's fog unit, which takes the fog factor from a 128-entry table indexed by the depth buffer value.
  GL's factor depends on the eye distance, so each entry maps its window depth back through the projection to
  `z_eye` (exact for `glFrustum`/`glOrtho`-style projections). The table is rebuilt only when the projection,
  depth range or fog parameters change.
- User clip planes are clipped on the CPU (PICA has only one clip plane), in object space before lines and points are
  expanded: each plane goes to object space with the current modelview (cached per matrix change), polygons are
  clipped Sutherland-Hodgman with all attributes interpolated, so the GPU only ever sees the remaining part.
  Edges along a clip plane are not outlined by `glPolygonMode(GL_LINE)`.
- Depth and stencil share one D24S8 buffer. `glClear` uses a memory fill when it can; clearing only depth or
  only stencil (once stencil is in use), or clearing with a scissor box or color mask, draws a full-screen quad.
- Textures are padded to power-of-two sizes and Morton-swizzled on upload; the shader applies the texture matrix
  and scales the texcoords back. Texture unit n is TexEnv stage n; texcoords of units 1/2 are written to a second
  vertex buffer only while those units are in use.
- ETC1 blocks are stored as they come (byte order reversed, 4 blocks per 8x8 tile) and therefore upside down
  compared to the other formats, whose rows are flipped on upload; the texture matrix flips t for them instead.
- Textures deleted during a frame are freed after the GPU finished that frame.
- One render target per screen; switching flushes the batch and changes the target within the same frame.
- `glDrawPixels` and `glBitmap` draw quads in window coordinates with the image as a texture (in per-frame linear
  memory, `GL_NEAREST`, in tiles of up to 256x256; zoom is the quad's size). Bitmaps are packed into one atlas per frame,
  so a line of text is one batch; fragments outside the bitmap are discarded by the alpha test (the GL alpha test on the
  raster color is decided on the CPU). `glCopyPixels(GL_COLOR)` queues a GX texture copy of the color buffer between the
  draws before and after it (the color buffer is tiled like a texture), so it does not wait for the GPU. PICA cannot
  output a per-pixel depth: depth and stencil images are read, tested and written back on the CPU.
- The pixel transfer runs on the CPU while images are converted (unsigned byte images through 256-entry tables).
  `glCopyPixels(GL_COLOR)` with an active color transfer is read back like `glReadPixels` (waits for the GPU) and
  drawn like `glDrawPixels`.
- Feedback and selection reuse the CPU primitive assembly: after user clip planes, a primitive is clipped against the
  view volume in clip space (Sutherland-Hodgman for polygons, with colors and texcoords interpolated), culled and split
  by polygon mode like for drawing, then written as tokens in window coordinates or recorded as a hit instead of being
  drawn. `GL_POLYGON` is kept whole so that it comes back as one polygon.
- Line stipple splits each expanded line into one quad per run of drawn fragments: the fragments are counted per pixel
  along the major axis, the runs end on pixel edges.
- Polygon stipple is a 32x32 alpha texture on PICA texture unit 0 with texcoords window position / 32, which the
  shader computes from the vertex position in projection mode (PICA divides by w per pixel, so the pattern stays fixed
  to the window under perspective). GL texture units 0 and 1 move to PICA units 1 and 2 meanwhile (a projective
  texcoord of unit 0 is then divided per vertex). TexEnv stage 3 gives the fragments outside the pattern an alpha that
  fails the alpha test, which is set up to combine with the GL alpha test.
- The accumulation buffer lives in normal memory, two 16-bit components per word, so that the ARMv6 SIMD and DSP
  instructions (`QADD16`, `SMLAWB`/`SMLAWT`, `SSAT`/`USAT`) handle a pair at a time. `GL_ACCUM` and `GL_LOAD` read
  the color buffer like `glReadPixels` (the frame so far is run, then copied out by a display transfer). `GL_RETURN`
  converts the result to RGBA8 textures that are drawn over the scissor box like `glDrawPixels` images, with only the
  scissor test and the color mask, so it is queued between the draws without waiting.
- Display lists store each command with its arguments; client memory is copied at compile time (texture images
  tightly packed, vertex array elements as the immediate mode calls they stand for). `glCallList` runs the commands
  through the same entry points, so a list renders exactly like the calls it recorded; it costs about as much CPU
  time as those calls (nothing is pre-transformed or cached on the GPU).

## License

zlib, see [LICENSE](LICENSE).
