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
* [ ] `glMultiTexCoord*` (ES; with multitexturing)
* [ ] `glVertex4*` with w = 0 (points at infinity), `glIndex*` (GL, color index mode)

## Vertex Arrays

* [x] `glVertexPointer`, `glColorPointer`, `glTexCoordPointer`
* [x] `glEnableClientState`, `glDisableClientState`
* [x] `glDrawArrays`, `glDrawElements` (ubyte/ushort indices; uint (GL))
* [x] All array types and sizes: `GL_BYTE`, `GL_SHORT`, `GL_FIXED` (ES), `GL_INT`, `GL_DOUBLE` (GL), `GL_FLOAT`; unsigned color types
* [x] Size 4 texcoord arrays with per-vertex q, size 4 vertex arrays (divided by w)
* [x] `glNormalPointer`, `GL_NORMAL_ARRAY` (normals read; not used until lighting)
* [x] `glArrayElement`, `glInterleavedArrays` (GL)
* [x] `glGetPointerv`, array state queries, validation errors
* [ ] `glPointSizePointerOES`, `GL_POINT_SIZE_ARRAY_OES` (ES, required extension)

## Vertex Buffer Objects (ES)

* [ ] `glGenBuffers`, `glDeleteBuffers`, `glBindBuffer`, `glIsBuffer`
* [ ] `glBufferData`, `glBufferSubData`
* [ ] `glGetBufferParameteriv`
* [ ] Buffer offsets in `gl*Pointer` and `glDrawElements`

## Transform / Matrix

* [x] `glMatrixMode`, `glLoadIdentity`, `glPushMatrix`, `glPopMatrix`
* [x] `glLoadMatrixf/d`, `glMultMatrixf/d`, `glTranslate*`, `glRotate*`, `glScale*`, `glFrustum`, `glOrtho`
* [x] Matrix queries, stack depth queries
* [x] `glDepthRange`
* [x] `glViewport`
* [x] Texture matrix (`GL_TEXTURE`), including projective matrices (q divide by PICA projection mode)
* [x] `glOrthof`, `glFrustumf`, `glDepthRangef`, `glClearDepthf` (ES float variants)
* [ ] Clip planes: `glClipPlane`, `glGetClipPlane` (`f`/`x` variants in ES), `GL_CLIP_PLANE0..5`

## Fixed-Point API (ES)

* [x] `GLfixed`, `GL_FIXED` arrays, `<GLES/gl.h>`
* [x] `x` entry points of the implemented features: `glAlphaFuncx`, `glClearColorx`, `glClearDepthx`, `glColor4x`,
  `glDepthRangex`, `glFrustumx`, `glGetFixedv`, `glLineWidthx`, `glLoadMatrixx`, `glMultMatrixx`, `glNormal3x`,
  `glOrthox`, `glPointSizex`, `glPolygonOffsetx`, `glRotatex`, `glScalex`, `glTexEnvx(v)`, `glTexParameterx(v)`, `glTranslatex`
* [ ] `x` entry points that come with their features: `glClipPlanex`, `glFogx(v)`, `glLightx(v)`, `glLightModelx(v)`,
  `glMaterialx(v)`, `glMultiTexCoord4x`, `glPointParameterx(v)`, `glSampleCoveragex`, `glGet*xv`

## Lighting

* [ ] `glLight*`, `glLightModel*`, `glMaterial*`, `glGetLight*`, `glGetMaterial*`
* [ ] Actual lighting calculation (8 lights, two-sided, local viewer)
* [ ] `glColorMaterial` (GL), `GL_COLOR_MATERIAL` (both)
* [ ] `GL_NORMALIZE`, `GL_RESCALE_NORMAL` (ES)
* [~] `glNormal*` — stored for queries only

## Fog

* [ ] `glFog*` (linear, exp, exp2), `GL_FOG`
* [ ] Actual fog rendering (PICA fog LUT)

## Textures

* [x] `glGenTextures`, `glDeleteTextures`, `glBindTexture`, `glIsTexture`
* [x] `glTexImage2D`, `glTexSubImage2D`, `glGetTexImage` (GL)
* [x] Formats RGBA, RGB, LUMINANCE_ALPHA, LUMINANCE, ALPHA (ubyte), RGB565, RGBA5551, RGBA4; NPOT sizes up to 1024
* [x] `glTexParameter*` (filters, wrap)
* [x] Texture borders (GL; border texels are dropped), proxy textures, `glGetTexLevelParameter*` (GL)
* [ ] Internal formats with different sampling (`GL_INTENSITY`, ...) (GL)
* [ ] `glGetTexParameter*`
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
* [ ] `glGetTexEnv*`
* [ ] `GL_COMBINE` with sources, operands, `GL_RGB_SCALE`/`GL_ALPHA_SCALE`, `GL_DOT3_RGB(A)` (ES)
* [ ] `GL_COORD_REPLACE_OES` (ES, with point sprites)

## Multitexturing (ES)

* [ ] `glActiveTexture`, `glClientActiveTexture`, `glMultiTexCoord*`
* [ ] Multiple texture units (PICA has 3 usable for 2D) and per-unit environments

## Mipmapping

* [~] `gluBuild2DMipmaps` — uploads all levels, only level 0 is used
* [ ] Multiple mipmap levels, mipmap filtering, automatic LOD
* [ ] `GL_GENERATE_MIPMAP` (ES), `GL_GENERATE_MIPMAP_HINT` (ES)

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
* [x] Points: `glPointSize`
* [ ] Point parameters: `glPointParameter*`, attenuation (ES)
* [ ] Point sprites `GL_POINT_SPRITE_OES` (ES, required extension)
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
* [ ] Attribute stacks: `glPushAttrib`/`glPopAttrib` (stub, used by GLU NURBS), `glPushClientAttrib`/`glPopClientAttrib`
* [ ] Evaluators: `glMap1/2`, `glMapGrid*`, `glEvalCoord*`, `glEvalMesh*`, `glEvalPoint*` (stubs), `glGetMap*`, `GL_AUTO_NORMAL`
* [ ] Feedback and selection: `glRenderMode`, `glFeedbackBuffer`, `glSelectBuffer`, `glInitNames`, `glPushName`, `glPopName`, `glLoadName`, `glPassThrough`
* [ ] Accumulation buffer: `glAccum`, `glClearAccum`
* [ ] `glDrawBuffer`, `glReadBuffer`
* [ ] Color index mode (`glIndex*`, `glIndexMask`, `glClearIndex`) — likely out of scope (RGBA framebuffer only)

## State Queries

* [x] `glGetBooleanv`, `glGetIntegerv`, `glGetFloatv`, `glGetDoublev` (GL) for the implemented state
* [x] `glGetError`, `glGetString`, `glIsEnabled`, `glIsTexture`
* [x] `glGetFixedv` (ES), `glGetPointerv`
* [ ] `glGetTexEnv*`, `glGetTexParameter*`, `glGetLight*`, `glGetMaterial*`, `glGetClipPlane*`, `glGetBufferParameteriv` (ES)
* [ ] `GL_EXTENSIONS` lists nothing yet (ES 1.1 requires the point sprite / point size array / paletted texture names)
* [~] `glHint` — accepted, hints have no effect (allowed by the spec)

## Error Handling

* [x] `GL_INVALID_ENUM`, `GL_INVALID_VALUE`, `GL_INVALID_OPERATION`, `GL_STACK_OVERFLOW`, `GL_STACK_UNDERFLOW`, `GL_OUT_OF_MEMORY`
* [x] First-error behavior, reset on read
* [~] Not every invalid argument of every call is detected yet

## GLU (`c3dgl::glu`, Mesa GLU 9.0.3)

* [x] Matrices, `gluProject`/`gluUnProject`, `gluScaleImage`, quadrics, tessellator, NURBS in `GLU_NURBS_TESSELLATOR` mode
* [ ] NURBS rendering through GL (needs evaluators and `glPushAttrib`)
* [ ] `gluBuild1DMipmaps` (needs 1D textures); `gluBuild3DMipmaps` fails by design (GL 1.2)

## c3dgl Platform

* [x] Top and bottom screen, `c3dglSetScreen`, `c3dglSwapBuffers`
* [ ] Stereoscopic 3D (right eye, 3D slider)
* [ ] Real hardware verification (everything so far is verified in Azahar only)

## Known Bugs / Limits

* [ ] `glTexSubImage2D` during a frame also changes draws issued earlier in that frame
* [ ] `glReadPixels` writes `w*h*4` bytes regardless of format
* [ ] 64K vertices per frame and 511 texture ids, the rest is dropped

---

# Main Remaining Work

```text
[ ] Lighting                          [ ] Display lists (GL)
[ ] Fog                               [ ] Attribute stacks (GL)
[ ] Mipmapping (+ GL_GENERATE_MIPMAP) [ ] Evaluators (GL)
[ ] Clip planes                       [ ] Feedback / selection (GL)
[ ] Multitexturing + GL_COMBINE (ES)  [ ] Pixel ops: DrawPixels, Bitmap, RasterPos (GL)
[ ] VBOs (ES)                         [ ] Accumulation buffer (GL)
[ ] Point parameters + sprites (ES)   [ ] 1D textures, texgen (GL)
[ ] Logic op, sample coverage         [ ] Stipple (GL)
[ ] glReadPixels, texture copies      [ ] Smooth points/lines
[ ] Compressed / paletted textures    [ ] Complete state queries
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
```
