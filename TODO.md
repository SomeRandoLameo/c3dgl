# c3dgl — OpenGL 1.1 + OpenGL ES 1.1 Status

Target: everything in desktop OpenGL 1.1 and in OpenGL ES 1.1 (common profile). Updated after every step.

**Legend**

* `[x]` Implemented and tested (in Azahar; real hardware is still pending for everything)
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
* [ ] `x` entry points that come with their features: `glSampleCoveragex`

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
* [ ] Internal formats with different sampling (`GL_INTENSITY`, ...) (GL)
* [x] `glGetTexParameter*` (`iv`, `fv`, `xv`)
* [ ] `glCopyTexImage2D`, `glCopyTexSubImage2D`
* [ ] `glCompressedTexImage2D`, `glCompressedTexSubImage2D`
* [ ] Paletted textures `GL_PALETTE4/8_*_OES` (ES, required); ETC1 (PICA native, extension)
* [ ] 1D textures: `glTexImage1D` (stub), `glTexSubImage1D`, `glCopyTexImage1D`... (GL)
* [ ] Texture coordinate generation `glTexGen*` (GL)
* [ ] `glPrioritizeTextures`, `glAreTexturesResident` (GL)
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
* [ ] Logic operations: `glLogicOp`, `GL_COLOR_LOGIC_OP`
* [ ] Multisampling: `glSampleCoverage`, `GL_MULTISAMPLE`, `GL_SAMPLE_ALPHA_TO_COVERAGE`, `GL_SAMPLE_ALPHA_TO_ONE` (ES)
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
* [ ] `glReadPixels` — stub returning black, and it can overflow the caller's buffer
* [ ] `glDrawPixels`, `glCopyPixels`, `glBitmap`, `glRasterPos*`, `glPixelZoom` (GL)
* [ ] `glPixelTransfer*`, `glPixelMap*`, `glGetPixelMap*` (GL)

## Desktop GL 1.1 Only

* [ ] Display lists: `glNewList`, `glEndList`, `glCallList(s)`, `glGenLists`, `glDeleteLists`, `glIsList`, `glListBase`
* [x] Attribute stacks: `glPushAttrib`/`glPopAttrib`, `glPushClientAttrib`/`glPopClientAttrib` (16 deep, all groups of the
  implemented state; groups of missing features fill in with them)
* [x] Evaluators: `glMap1/2`, `glMapGrid*`, `glEvalCoord*`, `glEvalMesh*`, `glEvalPoint*`, `glGetMap*`, `GL_AUTO_NORMAL`
* [ ] Feedback and selection: `glRenderMode`, `glFeedbackBuffer`, `glSelectBuffer`, `glInitNames`, `glPushName`, `glPopName`, `glLoadName`, `glPassThrough`
* [ ] Accumulation buffer: `glAccum`, `glClearAccum`
* [ ] `glDrawBuffer`, `glReadBuffer`
* [ ] Color index mode (`glIndex*`, `glIndexMask`, `glClearIndex`) — likely out of scope (RGBA framebuffer only)

## State Queries

* [x] `glGetBooleanv`, `glGetIntegerv`, `glGetFloatv`, `glGetDoublev` (GL) for the implemented state
* [x] `glGetError`, `glGetString`, `glIsEnabled`, `glIsTexture`
* [x] `glGetFixedv` (ES), `glGetPointerv`, `glGetBufferParameteriv` (ES)
* [x] `glGetLight*`, `glGetMaterial*`, lighting state in `glGet*`
* [x] `glGetClipPlane*`
* [~] `GL_EXTENSIONS` lists `GL_OES_point_sprite`, `GL_OES_point_size_array`; ES 1.1 also requires the paletted
  texture name (`GL_OES_compressed_paletted_texture`)
* [~] `glHint` — accepted, hints have no effect (allowed by the spec)

## Error Handling

* [x] `GL_INVALID_ENUM`, `GL_INVALID_VALUE`, `GL_INVALID_OPERATION`, `GL_STACK_OVERFLOW`, `GL_STACK_UNDERFLOW`, `GL_OUT_OF_MEMORY`
* [x] First-error behavior, reset on read
* [~] Not every invalid argument of every call is detected yet

## GLU (`c3dgl::glu`, Mesa GLU 9.0.3)

* [x] Matrices, `gluProject`/`gluUnProject`, `gluScaleImage`, quadrics, tessellator, NURBS in `GLU_NURBS_TESSELLATOR` mode
* [x] NURBS rendering through GL (`GLU_NURBS_RENDERER`, evaluators)
* [ ] `gluBuild1DMipmaps` (needs 1D textures); `gluBuild3DMipmaps` fails by design (GL 1.2)

## c3dgl Platform

* [x] Top and bottom screen, `c3dglSetScreen`, `c3dglSwapBuffers`
* [ ] Stereoscopic 3D (right eye, 3D slider)
* [x] Examples show CPU/GPU time and command buffer usage (bottom screen rows 2-4)
* [x] Resource use: only changed GPU state is sent per batch (command buffer about halved), one vertex cache flush per
  command list submission instead of one per batch, `glEvalMesh2` evaluates each grid point once
* [ ] Real hardware verification (everything so far is verified in Azahar only)
  * [x] Fixed: GPU lockup on the first draw (since bf91bd4): the vertex shader left `outtc0.w` unwritten

## Known Bugs / Limits

* [ ] `glTexSubImage2D` during a frame also changes draws issued earlier in that frame
* [ ] `glReadPixels` writes `w*h*4` bytes regardless of format
* [ ] 64K vertices per frame and 511 texture ids, the rest is dropped

---

# Main Remaining Work

```text
[ ] Lighting in the vertex shader     [ ] Display lists (GL)
[ ] Compressed / paletted textures    [ ] Texture copies, glReadPixels
[ ] Smooth points/lines (GL)          [ ] Feedback / selection (GL)
                                      [ ] Pixel ops: DrawPixels, Bitmap, RasterPos (GL)
                                      [ ] Accumulation buffer (GL)
[ ] Logic op, sample coverage         [ ] 1D textures, texgen (GL)
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
```
