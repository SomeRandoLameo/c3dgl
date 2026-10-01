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
features c3dgl does not have yet: NURBS rendering through evaluators (`GLU_NURBS_TESSELLATOR` mode works),
`gluBuild1DMipmaps` (1D textures) and `gluBuild3DMipmaps` (GL 1.2).
Turn it off with `-DC3DGL_BUILD_GLU=OFF`.

## Building the examples

```sh
cmake -S . -B build          # picks up $DEVKITPRO/cmake/3DS.cmake
cmake --build build          # -> build/examples/<name>/c3dgl_<name>.3dsx
```

- `cube`: 3D, depth test, culling, textures (NPOT and PNG), scissor, sub-viewports
- `primitives`: every primitive mode with culling on to catch wrong winding; page 2: flat shading, polygon modes,
  edge flags, polygon offset, depth range
- `api` (C): self-check of queries, errors, entry point variants, array types, VBOs, attribute stacks and the ES API; green screen = all passed
- `glu`: Mesa GLU on c3dgl: matrices, image scaling, quadrics, numeric self-checks; page 2: tessellator, NURBS
- `texture`: texture features; page 1: texture matrix, page 2: texture coordinates (per-vertex q, array types),
  page 3: multitexturing and `GL_COMBINE`, page 4: mipmaps
- `fragment`: per-fragment operations (alpha test, texture environment, stencil, `glClear`); A switches pages

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
- Texture environment (`glTexEnv`): `GL_MODULATE`, `GL_REPLACE`, `GL_DECAL`, `GL_BLEND`, `GL_ADD`, `GL_TEXTURE_ENV_COLOR`,
  and `GL_COMBINE` with all functions including `GL_DOT3_RGB(A)`
- Multitexturing: 3 texture units (`glActiveTexture`, `glClientActiveTexture`, `glMultiTexCoord*`), each with its own
  environment, texture matrix and texcoord array
- Flat shading with GL's provoking vertices, `glPolygonMode` (fill/line/point per face, with edge flags),
  `glPolygonOffset` (including the slope factor) and `glDepthRange`
- Blending (`glBlendFunc`), alpha test (`glAlphaFunc`), stencil (`glStencilFunc`/`Op`/`Mask`), depth
  test/function/mask, color mask, face culling, scissor, viewport, line width, point size
- `glClear` of color, depth and stencil, honoring scissor and write masks
- `glGetError`, `glGet{Boolean,Integer,Float,Double}v` for the common state, `glIsEnabled`, `glIsTexture`
- The common variants of the immediate mode calls (`glVertex2/3/4{f,d,i,s}[v]`, `glColor3/4{f,d,ub}[v]`, ...),
  `glRect*`, `glLoadMatrix*`, `glTranslated`/`glRotated`/`glScaled`
- Mipmaps: all levels, mipmap filters, `GL_GENERATE_MIPMAP`; `glGetTexParameter`
- Attribute stacks: `glPushAttrib`/`glPopAttrib`, `glPushClientAttrib`/`glPopClientAttrib`
- Pixel store modes (alignment, row length, skip rows/pixels, byte swapping), texture borders, proxy textures,
  `glGetTexLevelParameter`
- GLU: the complete Mesa GLU 9.0.3 (`c3dgl::glu`, `<GL/glu.h>`), see [GLU](#glu)
- Lighting, fog, dithering and smoothing can be enabled and queried but have no effect yet
- Top (400x240) and bottom (320x240) screen, see [Screens](#screens)

The full list of functions is `include/GL/gl.h`.

## Not supported

Lighting, fog, display lists, `glReadPixels`, round points (`GL_POINT_SMOOTH`) and stereoscopic 3D. Mipmap levels
below 8x8 are accepted but not sampled (PICA stops at 8x8).
`GL_REPEAT` on non-power-of-two textures samples the padding.

## How it works

- Vertices are collected in one linear buffer per frame and drawn in batches. A batch is submitted when the
  draw state (texture, matrices, blend, depth, ...) changes; the state is compared when drawing, not in the
  setters, so code that binds and unbinds a texture around every quad still ends up in one draw call.
- The PICA200 vertex shader (`shaders/c3dgl.v.pica`) applies `post * projection * modelview`, where `post`
  rotates to the 3DS screen orientation and maps depth to PICA's [-1, 0] range.
- Strips, fans, quads and polygons are split into triangles on the CPU. Lines and points have no PICA
  equivalent: they are transformed on the CPU and expanded to screen-aligned quads.
- Assembling primitives on the CPU also gives flat shading (the provoking vertex's color on every vertex),
  `glPolygonMode` (outlines and vertices per polygon, culled on the CPU) and the slope part of `glPolygonOffset`
  (computed per polygon, passed as a per-vertex depth bias that the vertex shader adds).
- Depth and stencil share one D24S8 buffer. `glClear` uses a memory fill when it can; clearing only depth or
  only stencil (once stencil is in use), or clearing with a scissor box or color mask, draws a full-screen quad.
- Textures are padded to power-of-two sizes and Morton-swizzled on upload; the shader applies the texture matrix
  and scales the texcoords back. Texture unit n is TexEnv stage n; texcoords of units 1/2 are written to a second
  vertex buffer only while those units are in use.
- Textures deleted during a frame are freed after the GPU finished that frame.
- One render target per screen; switching flushes the batch and changes the target within the same frame.

## License

zlib, see [LICENSE](LICENSE).
