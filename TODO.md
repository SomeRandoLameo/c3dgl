# c3dgl — OpenGL ES 1.1 Core Status

**Legend**

* `[x]` Implemented and tested
* `[~]` Implemented, but not fully tested / incomplete
* `[ ]` Missing

## Multitexturing

* [ ] `glActiveTexture`
* [ ] `glClientActiveTexture`
* [ ] `glMultiTexCoord*`
* [ ] Multiple texture units
* [~] `glTexEnv*` — single texture unit works
* [ ] Complete multitexture environment handling

## Vertex Buffer Objects

* [ ] `glGenBuffers`
* [ ] `glDeleteBuffers`
* [ ] `glBindBuffer`
* [ ] `glBufferData`
* [ ] `glBufferSubData`
* [ ] `glGetBufferParameteriv`
* [ ] `glIsBuffer`

## Vertex Arrays

* [x] `glVertexPointer`
* [x] `glColorPointer`
* [x] `glNormalPointer`
* [x] `glTexCoordPointer`
* [x] `glEnableClientState`
* [x] `glDisableClientState`
* [x] `glDrawArrays`
* [x] `glDrawElements`

## Transform / Matrix

* [x] `glMatrixMode`
* [x] `glLoadIdentity`
* [x] `glLoadMatrix*`
* [x] `glMultMatrix*`
* [x] `glPushMatrix`
* [x] `glPopMatrix`
* [x] `glTranslate*`
* [x] `glRotate*`
* [x] `glScale*`
* [x] `glFrustum`
* [x] `glOrtho`
* [x] Matrix queries

## Lighting

* [ ] `glLight*`
* [ ] `glLightModel*`
* [ ] `glMaterial*`
* [~] `glNormal*` — implemented
* [~] `glNormalPointer` — implemented
* [ ] Actual lighting calculation

## Fog

* [ ] `glFog*`
* [ ] Actual fog rendering

## Textures

* [x] `glGenTextures`
* [x] `glDeleteTextures`
* [x] `glBindTexture`
* [x] `glTexImage2D`
* [x] `glTexSubImage2D`
* [x] `glTexParameter*`
* [x] `glTexEnv*`
* [ ] `glGetTexParameter*`
* [ ] `glGetTexEnv*`
* [ ] `glCompressedTexImage2D`
* [ ] `glCompressedTexSubImage2D`
* [ ] `glCopyTexImage2D`
* [ ] `glCopyTexSubImage2D`

## Texture Environment

* [x] `GL_MODULATE`
* [x] `GL_REPLACE`
* [x] `GL_DECAL`
* [x] `GL_BLEND`
* [x] `GL_ADD`
* [x] `GL_TEXTURE_ENV_COLOR`
* [ ] Multiple texture environments / texture units

## Mipmapping

* [~] `gluBuild2DMipmaps`
* [ ] Multiple mipmap levels
* [ ] Mipmap sampling
* [ ] Automatic LOD selection
* [ ] Mipmap filtering

## Blending

* [x] `glBlendFunc`
* [x] `GL_BLEND`
* [x] Blend state handling

## Alpha Test

* [x] `glAlphaFunc`
* [x] `GL_ALPHA_TEST`

## Depth

* [x] `glDepthFunc`
* [x] `glDepthMask`
* [x] `glClearDepth`
* [x] `glDepthRange`
* [x] `GL_DEPTH_TEST`

## Stencil

* [x] `glStencilFunc`
* [x] `glStencilMask`
* [x] `glStencilOp`
* [x] `glClearStencil`
* [x] `GL_STENCIL_TEST`

## Face Culling

* [x] `glCullFace`
* [x] `glFrontFace`
* [x] `GL_CULL_FACE`

## Scissor

* [x] `glScissor`
* [x] `GL_SCISSOR_TEST`
* [~] Hardware verification pending

## Viewport

* [x] `glViewport`
* [x] `GL_VIEWPORT`

## Polygon Offset

* [x] `glPolygonOffset`
* [x] `GL_POLYGON_OFFSET_FILL`

## Shading

* [x] `glShadeModel`
* [x] `GL_FLAT`
* [x] `GL_SMOOTH`

## Points

* [x] `glPointSize`
* [ ] `glPointParameter*`
* [ ] Point attenuation
* [ ] Point sprites

## Lines

* [x] `glLineWidth`
* [x] Line rendering
* [x] CPU line expansion

## Clear

* [x] `glClear`
* [x] `glClearColor`
* [x] `glClearDepth`
* [x] `glClearStencil`
* [x] Color mask
* [x] Depth mask
* [x] Stencil mask
* [x] Scissored clear

## Color Mask

* [x] `glColorMask`
* [x] `GL_COLOR_WRITEMASK`

## Pixel Storage

* [x] `glPixelStorei`
* [x] `GL_UNPACK_ALIGNMENT`

## Readback

* [ ] `glReadPixels`
* [ ] `glCopyTexImage2D`
* [ ] `glCopyTexSubImage2D`

## Logic Operations

* [ ] `glLogicOp`

## Multisampling

* [ ] `glSampleCoverage`

## Clip Planes

* [ ] `glClipPlane`
* [ ] `glGetClipPlane`

## State Queries

* [x] `glGetBooleanv`
* [x] `glGetIntegerv`
* [x] `glGetFloatv`
* [x] `glGetError`
* [x] `glGetString`
* [ ] `glGetPointerv`
* [ ] `glGetLight*`
* [ ] `glGetMaterial*`
* [ ] `glGetTexEnv*`
* [ ] `glGetTexParameter*`
* [ ] `glGetBufferParameteriv`
* [ ] `glIsBuffer`
* [x] `glIsTexture`
* [x] `glIsEnabled`

## Error Handling

* [x] `GL_INVALID_ENUM`
* [x] `GL_INVALID_VALUE`
* [x] `GL_STACK_UNDERFLOW`
* [x] Error reset behavior
* [x] First-error behavior

## Hints

* [~] `glHint`
* [ ] Complete hint semantics

---

# Main Remaining GLES 1.1 Core Work

```text
[ ] Multitexturing
[ ] VBOs
[ ] Lighting
[ ] Fog
[ ] Clip planes
[ ] Point parameters
[ ] Logic operations
[ ] Sample coverage
[ ] Texture copy operations
[ ] Compressed texture API
[ ] Complete mipmapping
[ ] glReadPixels
[ ] Complete state-query coverage
```

# Already Solid

```text
[x] Matrix system
[x] Matrix stacks
[x] Vertex arrays
[x] DrawArrays
[x] DrawElements
[x] Texture upload
[x] Texture sampling
[x] Texture environment
[x] Blending
[x] Alpha test
[x] Depth
[x] Stencil
[x] Culling
[x] Scissor
[x] Viewport
[x] Polygon offset
[x] Primitive conversion
[x] Error handling
```
