# c3dgl — OpenGL 1.1 + OpenGL ES 1.1 Status

Target: everything in desktop OpenGL 1.1 and in OpenGL ES 1.1 (common profile). Updated after every step.

**Legend**

* `[x]` Implemented and tested (in Azahar and on real hardware)
* `[~]` Implemented, but not fully tested / incomplete
* `[ ]` Missing
* `(GL)` only in desktop OpenGL 1.1, `(ES)` only in OpenGL ES 1.1, untagged: both

## Primitives / Immediate Mode

* [x] All primitive modes: points, lines, line strips/loops, triangles, strips, fans, quads (GL), quad strips (GL), polygons (GL)
* [x] `glBegin` / `glEnd` (GL)
* [x] All GL 1.1 variants of `glVertex*`, `glColor*`, `glTexCoord*` (incl. projective q), `glNormal*`; integer colors/normals normalized
* [x] `glRect*` (GL)
* [x] `glEdgeFlag*`, `glEdgeFlagPointer`, `GL_EDGE_FLAG_ARRAY` (GL)
* [x] `glMultiTexCoord*` (all GL 1.3 variants, ES `glMultiTexCoord4f/4x`)
* [ ] `glVertex4*` with w = 0 (points at infinity), `glIndex*` (GL, color index mode)

## Vertex Arrays

* [x] `glVertexPointer`, `glColorPointer`, `glTexCoordPointer`
* [x] `glEnableClientState`, `glDisableClientState`
* [x] `glDrawArrays`, `glDrawElements` (ubyte/ushort indices; uint (GL))
* [x] All array types and sizes: `GL_BYTE`, `GL_SHORT`, `GL_FIXED` (ES), `GL_INT`, `GL_DOUBLE` (GL), `GL_FLOAT`; unsigned color types
* [x] Size 4 texcoord arrays with per-vertex q, size 4 vertex arrays (divided by w)
* [x] `glNormalPointer`, `GL_NORMAL_ARRAY`
* [x] `glArrayElement`, `glInterleavedArrays` (GL)
* [x] `glGetPointerv`, array state queries, validation errors
* [x] `glPointSizePointerOES`, `GL_POINT_SIZE_ARRAY_OES` (ES, required extension): `GL_FLOAT`/`GL_FIXED`, buffer
  offsets, queries, `GL_CLIENT_VERTEX_ARRAY_BIT`; the array value replaces `glPointSize`, then attenuation and
  min/max apply

## Vertex Buffer Objects (ES)

* [x] `glGenBuffers`, `glDeleteBuffers`, `glBindBuffer`, `glIsBuffer`
* [x] `glBufferData`, `glBufferSubData` (buffers live in normal memory; vertices are converted per frame anyway)
* [x] `glGetBufferParameteriv`, buffer binding queries
* [x] Buffer offsets in `gl*Pointer`, `glInterleavedArrays` and `glDrawElements`; out-of-range reads are skipped

## Transform / Matrix

* [x] `glMatrixMode`, `glLoadIdentity`, `glPushMatrix`, `glPopMatrix`
* [x] `glLoadMatrixf/d`, `glMultMatrixf/d`, `glTranslate*`, `glRotate*`, `glScale*`, `glFrustum`, `glOrtho`
* [x] Matrix queries, stack depth queries
* [x] `glDepthRange`
* [x] `glViewport`
* [x] Texture matrix (`GL_TEXTURE`), including projective matrices (q divide by PICA projection mode)
* [x] `glOrthof`, `glFrustumf`, `glDepthRangef`, `glClearDepthf` (ES float variants)
* [x] Clip planes: `glClipPlane`, `glGetClipPlane` (`f`/`x` variants in ES), `GL_CLIP_PLANE0..5`, `GL_MAX_CLIP_PLANES` = 6;
  clipped on the CPU in object space (polygons Sutherland-Hodgman with all attributes, lines before expansion,
  points whole), in `GL_TRANSFORM_BIT`/`GL_ENABLE_BIT`. Edges along a clip plane are not outlined in `GL_LINE` mode

## Fixed-Point API (ES)

* [x] `GLfixed`, `GL_FIXED` arrays, `<GLES/gl.h>`
* [x] `x` entry points of the implemented features: `glAlphaFuncx`, `glClearColorx`, `glClearDepthx`, `glColor4x`,
  `glDepthRangex`, `glFrustumx`, `glGetFixedv`, `glLineWidthx`, `glLoadMatrixx`, `glMultMatrixx`, `glNormal3x`,
  `glOrthox`, `glPointSizex`, `glPolygonOffsetx`, `glRotatex`, `glScalex`, `glTexEnvx(v)`, `glTexParameterx(v)`, `glTranslatex`
* [x] `glLightx(v)`, `glLightModelx(v)`, `glMaterialx(v)`, `glGetLightxv`, `glGetMaterialxv`, `glMultiTexCoord4x`
* [x] `glFogx(v)`, `glClipPlanex`, `glGetClipPlanex`
* [x] `glPointParameterx(v)`
* [x] `glSampleCoveragex`

## Lighting

* [x] `glLight*`, `glLightModel*`, `glMaterial*` (`f`/`fv`/`i`/`iv`/`x`/`xv`), `glGetLight*`, `glGetMaterial*`
* [x] Lighting calculation per vertex on the CPU (GL 1.1 formula): 8 lights, directional/positional, attenuation,
  spot lights, specular, two-sided (back color picked per polygon facing), local viewer
* [x] `glColorMaterial` (GL), `GL_COLOR_MATERIAL` (both; also with color arrays and evaluated colors)
* [x] `GL_NORMALIZE`, `GL_RESCALE_NORMAL`
* [x] Normals from `glNormal*`, normal arrays and evaluators (`GL_MAP*_NORMAL`, `GL_AUTO_NORMAL`)
* [x] Lighting state in `glPushAttrib` (`GL_LIGHTING_BIT`, `GL_ENABLE_BIT`, `GL_TRANSFORM_BIT`)
* [x] Lit vertices are cached (position, normal, color) while the lighting state is unchanged: shared mesh vertices
  are lit once
* [~] Performance: CPU lighting is still the most expensive part of the lighting example (~23k vertices per frame,
  75 ms CPU in Azahar); a vertex shader path for the common case (filled, smooth) would fix it

## Fog

* [x] `glFog*` (`f`/`fv`/`i`/`iv`/`x`/`xv`; linear, exp, exp2), `GL_FOG`, fog queries, `GL_FOG_BIT`
* [x] Fog rendering per pixel on PICA's fog unit: the 128-entry table over window depth maps each entry back to the eye
  distance |z_eye| through the projection's z/w rows and glDepthRange (exact for glFrustum/glOrtho style projections;
  rebuilt only when projection, depth range or fog parameters change). Also applies to lines and points
* [~] Precision: with perspective most of the depth range is near 1, so far fog is interpolated over few table entries
  (the floor in the fog example shows no banding)

## Textures

* [x] `glGenTextures`, `glDeleteTextures`, `glBindTexture`, `glIsTexture`
* [x] `glTexImage2D`, `glTexSubImage2D`, `glGetTexImage` (GL)
* [x] Formats RGBA, RGB, LUMINANCE_ALPHA, LUMINANCE, ALPHA (ubyte), RGB565, RGBA5551, RGBA4; NPOT sizes up to 1024
* [x] `glTexParameter*` (filters, wrap)
* [x] Texture borders (GL; border texels are dropped), proxy textures, `glGetTexLevelParameter*` (GL)
* [x] All GL 1.1 internal formats (GL): base (`GL_ALPHA`, `GL_LUMINANCE(_ALPHA)`, `GL_INTENSITY`, `GL_RGB(A)`, 1..4) and
  sized (`GL_RGB8`, `GL_RGBA4`, `GL_INTENSITY8`, ...), stored in the closest PICA format: intensity as LA8 (I, I) with
  its own `GL_BLEND`/`GL_ADD` alpha, R3_G3_B2/RGB4/RGB5 as RGB565, RGBA2/RGBA4 as RGBA4, RGB5_A1 as RGBA5551, the rest
  in 8 bits per component. Images in any GL 1.1 format/type (`GL_RED`..`GL_BLUE`, `GL_BYTE` .. `GL_FLOAT`, swapped
  bytes) are converted on load (fast path when they are already in the stored layout); sub images convert to the
  texture's format; `glGetTexImage` returns any format/type (table 6.1 components, luminance = R + G + B).
  `GL_COLOR_INDEX` images come with pixel maps (below)
* [x] `glGetTexParameter*` (`iv`, `fv`, `xv`)
* [x] `glCopyTexImage2D`, `glCopyTexSubImage2D`: internal formats `GL_ALPHA`, `GL_LUMINANCE(_ALPHA)`, `GL_RGB`,
  `GL_RGBA` (sized ones come with the internal formats above), borders; sub-copies keep the texture's format (also the
  16-bit ones). The rectangle is read like `glReadPixels` (frame ended without presenting), so draws issued before the
  copy keep the old texels; luminance is R as for texture images
* [x] `glCompressedTexImage2D`, `glCompressedTexSubImage2D` (`GL_INVALID_OPERATION` for both formats, as their
  extensions require), `GL_NUM_COMPRESSED_TEXTURE_FORMATS`, `GL_COMPRESSED_TEXTURE_FORMATS`, compressed proxies
* [x] Paletted textures, all 10 `GL_PALETTE4/8_*_OES` formats (ES, required): expanded to the palette's format
  (RGB8, RGBA8, RGB565, RGBA4, RGBA5551) on load; level <= 0 loads levels 0..-level from one image
* [x] ETC1 `GL_ETC1_RGB8_OES` (`GL_OES_compressed_ETC1_RGB8_texture`): sampled natively, blocks stored upside down and
  flipped back by the texture matrix; any size (NPOT padded), mipmaps per level. `GL_GENERATE_MIPMAP` is not
  supported for ETC1 (would need an encoder)
* [x] 1D textures (GL): `glTexImage1D`, `glTexSubImage1D`, `glCopyTexImage1D`, `glCopyTexSubImage1D`, proxies,
  `GL_TEXTURE_1D` enable and binding per unit (2D takes precedence), mipmaps (also `GL_GENERATE_MIPMAP`), queries,
  display lists, attribute stacks, `gluBuild1DMipmaps`. Stored as 2D with every row holding the image (8 rows, square
  once mipmapped) and sampled with t = s, so t has no effect and the mip level follows s. Verified in Azahar and on
  real hardware (api checks, mip levels too)
* [x] Texture coordinate generation (GL): `glTexGen{i,f,d}[v]`, `glGetTexGen{i,f,d}v`, `GL_OBJECT_LINEAR`,
  `GL_EYE_LINEAR` (plane in eye coordinates, via the inverse modelview of the call), `GL_SPHERE_MAP` (s, t), for s, t,
  r, q of each texture unit; `GL_TEXTURE_GEN_S..Q` per unit, in `GL_TEXTURE_BIT`/`GL_ENABLE_BIT`, recorded in display
  lists. Per vertex on the CPU, the texture matrix applied with the generated r (projective texturing); a coordinate
  that is not generated keeps the vertex's value (r is 0 then, vertices keep no r). Verified in Azahar (api checks
  render and read back every mode, texgen example) and on real hardware (api checks)
  * [~] Performance: ~1.3 us per vertex on top of the normal path (measured 51 -> 69 ms CPU for ~14k vertices; the texgen example now draws ~9k: 45 ms, 20 FPS in Azahar);
    a cache of generated texcoords for shared mesh vertices (like the lit cache) would cut that for indexed meshes
* [x] `glPrioritizeTextures`, `glAreTexturesResident`, `GL_TEXTURE_PRIORITY`, `GL_TEXTURE_RESIDENT` (GL): priorities
  stored, every texture is resident. `GL_TEXTURE_BORDER_COLOR` stored only (no border texels)
* [x] Default texture objects: texture 0 of `GL_TEXTURE_1D` and `GL_TEXTURE_2D` is a texture of its own (GL 1.0 style
  code without `glBindTexture`), shared by all units; deleting a bound texture falls back to it. Verified in Azahar (api checks)
* [ ] `GL_REPEAT` on NPOT textures samples the padding

## Texture Environment

* [x] `GL_MODULATE`, `GL_REPLACE`, `GL_DECAL`, `GL_BLEND`, `GL_ADD`
* [x] `GL_TEXTURE_ENV_COLOR`
* [x] Alpha-only / luminance textures follow the GL format table
* [x] `glGetTexEnv*` (`iv`, `fv`, `xv`)
* [x] `GL_COMBINE` with all functions (incl. `GL_DOT3_RGB(A)`, `GL_SUBTRACT`), sources, operands,
  `GL_RGB_SCALE`/`GL_ALPHA_SCALE`; also `GL_TEXTUREn` sources (crossbar, GL 1.4)
* [x] `GL_COORD_REPLACE_OES` per texture unit (ES, with point sprites; also as GL 2.0 `GL_COORD_REPLACE`)

## Multitexturing (ES)

* [x] `glActiveTexture`, `glClientActiveTexture`, `glMultiTexCoord*`, `GL_MAX_TEXTURE_UNITS` = 3
* [x] 3 texture units (PICA units 0-2) with per-unit binding, enable, environment, texture matrix, texcoords and arrays
* [~] Projective texcoords on units 1/2 are divided per vertex (exact unless q varies across a primitive); unit 0 per pixel

## Mipmapping

* [x] Mipmap levels via `glTexImage2D`/`glTexSubImage2D`/`glGetTexImage` level > 0, `glGetTexLevelParameter` per level
* [x] Mipmap filters (`GL_*_MIPMAP_*`), hardware LOD selection; non-mipmap filters sample level 0 only
* [x] GL completeness: a mipmap filter without all levels disables the unit (with a one-time warning)
* [x] `GL_GENERATE_MIPMAP` (ES; CPU box filter, also for RGB565/RGBA5551/RGBA4), `GL_GENERATE_MIPMAP_HINT`
* [x] `gluBuild2DMipmaps`
* [~] Levels smaller than 8x8 are accepted and count for completeness, but PICA cannot store them: sampling stops at 8x8

## Per-Fragment Operations

* [x] Blending: `glBlendFunc`, `GL_BLEND`
* [x] Alpha test: `glAlphaFunc`, `GL_ALPHA_TEST`
* [x] Depth: `glDepthFunc`, `glDepthMask`, `glClearDepth`, `GL_DEPTH_TEST`
* [x] Stencil: `glStencilFunc`, `glStencilMask`, `glStencilOp`, `glClearStencil`, `GL_STENCIL_TEST`
* [x] Scissor: `glScissor`, `GL_SCISSOR_TEST`
* [x] Color mask: `glColorMask`, `GL_COLOR_WRITEMASK`
* [x] Logic operations: `glLogicOp` (all 16), `GL_COLOR_LOGIC_OP` on PICA's logic op unit (replaces blending while on,
  clears are unaffected), `GL_LOGIC_OP_MODE`, in `GL_COLOR_BUFFER_BIT`/`GL_ENABLE_BIT`; `GL_INDEX_LOGIC_OP` (GL, color
  index) stored only. Verified with Azahar's software renderer: its Vulkan and OpenGL renderers on macOS ignore logic ops
* [x] Multisampling (ES; GL 1.3): `glSampleCoverage(x)`, `GL_MULTISAMPLE` (on by default), `GL_SAMPLE_ALPHA_TO_COVERAGE`,
  `GL_SAMPLE_ALPHA_TO_ONE`, `GL_SAMPLE_COVERAGE`, their queries and `GL_MULTISAMPLE_BIT`. The framebuffer has no
  sample buffers (`GL_SAMPLE_BUFFERS` = `GL_SAMPLES` = 0), so as the spec requires they have no effect
* [~] `GL_DITHER` — accepted, no effect

## Rasterization

* [x] Face culling: `glCullFace`, `glFrontFace`, `GL_CULL_FACE`
* [x] Shading: `glShadeModel`, `GL_FLAT` (GL provoking vertices), `GL_SMOOTH`
* [x] Polygon offset: `glPolygonOffset` (with slope factor), `GL_POLYGON_OFFSET_FILL`, `_LINE`/`_POINT` (GL)
* [x] Polygon mode: `glPolygonMode` fill/line/point per face (GL)
* [x] Lines: `glLineWidth`, CPU line expansion
* [x] Points: `glPointSize` (`GL_INVALID_VALUE` for size <= 0), size range queries (1..256, any size)
* [x] Point parameters: `glPointParameterf/fv/i/iv/x/xv`, `GL_POINT_SIZE_MIN/MAX`, distance attenuation per point
  (eye distance), in `GL_POINT_BIT`. `GL_POINT_FADE_THRESHOLD_SIZE` is stored only: the fade applies with
  multisampling, which PICA does not have
* [x] Point sprites `GL_POINT_SPRITE_OES` (ES, required extension): sprite texcoords (0, 0) top left to (1, 1) bottom
  right on the units with `GL_COORD_REPLACE_OES`, without the texture matrix; also for `glPolygonMode(GL_POINT)`
* [ ] Smooth points/lines/polygons (`GL_POINT_SMOOTH`, `GL_LINE_SMOOTH`, `GL_POLYGON_SMOOTH` (GL)) — accepted, no effect
* [ ] Line stipple, polygon stipple (GL)

## Clear

* [x] `glClear`, `glClearColor`, `glClearDepth`, `glClearStencil`
* [x] Honors color/depth/stencil masks and scissor
* [ ] `glClearAccum`, `glClearIndex` (GL)

## Pixel Operations

* [x] `glPixelStorei/f`: alignment, row length, skip rows/pixels, swap bytes (row length etc. GL only)
* [x] `glReadPixels`: color (`GL_RGBA`, `GL_RGB`, `GL_RED/GREEN/BLUE/ALPHA`, `GL_LUMINANCE(_ALPHA)`) in all GL 1.1
  types plus `GL_UNSIGNED_SHORT_5_6_5/4_4_4_4/5_5_5_1`, `GL_DEPTH_COMPONENT`, `GL_STENCIL_INDEX` (also `GL_BITMAP`);
  pack store modes, clipped to the window, ES `GL_IMPLEMENTATION_COLOR_READ_FORMAT/TYPE_OES` (`GL_RGBA`/ubyte).
  Within a frame the frame is ended without presenting (runs the draws so far), the rows are copied out by a display
  transfer and the frame is begun again; GPU time stats then cover only the part after the last read
* [~] Drawing pixels (GL): `glRasterPos{2,3,4}{s,i,f,d}[v]` (transformed and clipped like a point, also against the user
  clip planes; lit color, texcoords through the texture matrix, eye distance; `GL_CURRENT_RASTER_*` queries),
  `glBitmap`, `glDrawPixels` (color in all GL 1.1 formats/types and the packed 16-bit ones, `GL_DEPTH_COMPONENT`,
  `GL_STENCIL_INDEX` incl. `GL_BITMAP`; unpack store modes), `glCopyPixels` (`GL_COLOR`, `GL_DEPTH`, `GL_STENCIL`),
  `glPixelZoom` (`GL_ZOOM_X/Y`, negative zoom mirrors); raster state in `GL_CURRENT_BIT`, zoom in `GL_PIXEL_MODE_BIT`;
  recorded in display lists (images copied with the unpack state, bitmaps repacked). Color images and bitmaps are
  textured quads in window coordinates through all per-fragment operations (raster depth, fog); bitmaps share a per-frame
  atlas (one batch per run of glyphs); `glCopyPixels(GL_COLOR)` is a GX texture copy queued between the draws (no wait).
  Depth/stencil images are read, tested (scissor, stencil, depth, masks) and written back on the CPU (waits for the GPU).
  Verified in Azahar (api checks in Vulkan at 1x and the software renderer, pixels example)
  * [ ] Real hardware not tested yet
  * [~] Texturing does not apply to `glDrawPixels`/`glBitmap` fragments; depth images do not write the raster color
  * [~] `GL_COLOR_INDEX` images come with the pixel maps (below); feedback/selection tokens with feedback mode
  * [~] Azahar's Vulkan renderer at `resolution_factor=2` reads back depth with ~1/256 error after a CPU depth write
    (exact at 1x and in the software renderer), so 2 api checks fail there
* [ ] `glPixelTransfer*`, `glPixelMap*`, `glGetPixelMap*` (GL)

## Desktop GL 1.1 Only

* [x] Display lists: `glNewList`, `glEndList` (`GL_COMPILE`, `GL_COMPILE_AND_EXECUTE`), `glCallList`, `glCallLists` (all
  types), `glListBase`, `glGenLists`, `glDeleteLists`, `glIsList`; `GL_LIST_BASE/INDEX/MODE`, `GL_MAX_LIST_NESTING` (64),
  `GL_LIST_BIT`. Client data copied at compile time (pixels with the unpack state, control points, vertex array elements,
  `glCallLists` names); immediate commands (`glGet*`, client state, `glPixelStore`, proxies, ...) are not recorded.
  Verified in Azahar (api checks, gears example pixel-identical to immediate mode) and on real hardware (api checks)
  * [~] Replayed through the gl* entry points: no faster than the immediate mode calls it recorded
  * [~] The point size array (ES) is not recorded by `glArrayElement`/`glDrawArrays` in a list
* [x] Attribute stacks: `glPushAttrib`/`glPopAttrib`, `glPushClientAttrib`/`glPopClientAttrib` (16 deep, all groups of the
  implemented state; groups of missing features fill in with them)
* [x] Evaluators: `glMap1/2`, `glMapGrid*`, `glEvalCoord*`, `glEvalMesh*`, `glEvalPoint*`, `glGetMap*`, `GL_AUTO_NORMAL`
* [ ] Feedback and selection: `glRenderMode`, `glFeedbackBuffer`, `glSelectBuffer`, `glInitNames`, `glPushName`, `glPopName`, `glLoadName`, `glPassThrough`
* [ ] Accumulation buffer: `glAccum`, `glClearAccum`
* [x] `glDrawBuffer`, `glReadBuffer`: double-buffered, no stereo or aux buffers (`GL_INVALID_OPERATION` for them).
  Front buffers are drawn/read like the back buffer (the frame is presented by `c3dglSwapBuffers()`), `GL_NONE` draws
  and clears no color. `GL_DRAW_BUFFER`, `GL_READ_BUFFER`, `GL_DOUBLEBUFFER`, `GL_STEREO`, `GL_AUX_BUFFERS`; in
  `GL_COLOR_BUFFER_BIT`/`GL_PIXEL_MODE_BIT` and display lists
* [ ] Color index mode (`glIndex*`, `glIndexMask`, `glClearIndex`) — likely out of scope (RGBA framebuffer only)

## State Queries

* [x] `glGetBooleanv`, `glGetIntegerv`, `glGetFloatv`, `glGetDoublev` (GL) for the implemented state
* [x] `glGetError`, `glGetString`, `glIsEnabled`, `glIsTexture`
* [x] `glGetFixedv` (ES), `glGetPointerv`, `glGetBufferParameteriv` (ES)
* [x] `glGetLight*`, `glGetMaterial*`, lighting state in `glGet*`
* [x] `glGetClipPlane*`
* [x] `GL_EXTENSIONS` lists `GL_OES_point_sprite`, `GL_OES_point_size_array`, `GL_OES_compressed_paletted_texture`
  (all required by ES 1.1) and `GL_OES_compressed_ETC1_RGB8_texture`
* [~] `glHint` — accepted, hints have no effect (allowed by the spec)

## Error Handling

* [x] `GL_INVALID_ENUM`, `GL_INVALID_VALUE`, `GL_INVALID_OPERATION`, `GL_STACK_OVERFLOW`, `GL_STACK_UNDERFLOW`, `GL_OUT_OF_MEMORY`
* [x] First-error behavior, reset on read
* [~] Not every invalid argument of every call is detected yet

## GLU (`c3dgl::glu`, Mesa GLU 9.0.3)

* [x] Matrices, `gluProject`/`gluUnProject`, `gluScaleImage`, quadrics, tessellator, NURBS in `GLU_NURBS_TESSELLATOR` mode
* [x] NURBS rendering through GL (`GLU_NURBS_RENDERER`, evaluators)
* [x] `gluBuild1DMipmaps`; `gluBuild3DMipmaps` fails by design (GL 1.2)

## c3dgl Platform

* [x] Top and bottom screen, `c3dglSetScreen`, `c3dglSwapBuffers`
* [ ] Stereoscopic 3D (right eye, 3D slider)
* [x] Examples show CPU/GPU time, command buffer usage and FPS (bottom screen rows 2-5)
* [x] Resource use: only changed GPU state is sent per batch (command buffer about halved), one vertex cache flush per
  command list submission instead of one per batch, `glEvalMesh2` evaluates each grid point once
* [~] Real hardware verification (all features up to color buffers verified on hardware; display lists, texgen, internal
  formats, 1D textures and color buffers through the api checks, 2026-10-04). Drawing pixels not yet
  * [x] Fixed: GPU lockup on the first draw (since bf91bd4): the vertex shader left `outtc0.w` unwritten

## Known Bugs / Limits

* [ ] `glTexSubImage2D` during a frame also changes draws issued earlier in that frame
* [ ] 64K vertices per frame and 511 texture ids, the rest is dropped
* [~] Testing mipmaps in Azahar: the software renderer samples only level 0; the Vulkan renderer picks the level from
  the t derivative alone (a quad with constant t samples level 0) and its resolution scale lowers the LOD

---

# Main Remaining Work

```text
[ ] Lighting in the vertex shader     [ ] Feedback / selection (GL)
[ ] Smooth points/lines (GL)
                                      [~] Pixel ops: pixel maps / transfer (GL)
                                      [ ] Accumulation buffer (GL)
[ ] Complete state queries            [ ] Stipple (GL)
```

# Already Solid

```text
[x] Primitives and immediate mode     [x] Alpha test, depth, stencil
[x] Matrix system, stacks, texture matrix [x] Culling, flat shading, polygon mode/offset
[x] Vertex arrays, all types (GL+ES) [x] Scissor, viewport, depth range
[x] Texture upload and sampling       [x] Clears with masks and scissor
[x] Texture environment               [x] Pixel store, proxy textures
[x] Blending                          [x] Error handling, state queries
[x] GLU (Mesa)                       [x] ES fixed-point API (implemented features)
[x] VBOs (ES)                         [x] Multitexturing + GL_COMBINE (ES)
[x] Mipmapping (+ GL_GENERATE_MIPMAP) [x] Attribute stacks (GL)
[x] Lighting (CPU, per vertex)        [x] Evaluators (GL)
[x] Fog (PICA fog table)              [x] User clip planes (CPU)
[x] Point parameters + sprites (ES) [x] Point size array (ES)
[x] Compressed textures: paletted + ETC1 (ES) [x] Texture copies (glCopyTexImage2D)
[x] Logic ops, sample coverage state  [x] Display lists (GL)
[x] Texture coordinate generation (GL) [x] 1D textures, all internal formats (GL)
[x] glDrawBuffer / glReadBuffer (GL)   [x] Default texture objects (GL)
```
