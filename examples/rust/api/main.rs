//! Self-check of the non-visual API (glGet*, glGetError, glIsEnabled, matrix loads, entry point
//! variants). Failed checks are listed on the bottom screen; the top screen is GREEN when
//! everything passed, RED otherwise (display lists included). On top of that, four white squares
//! are drawn with four different vertex calls (glRectf, glRecti, glVertex2sv, glVertex4f with
//! w = 2) and must look identical.
#[macro_use]
#[path = "../common/mod.rs"]
mod common;

use c3dgl::*;
use common::gl::*;
use common::{App, Checks, Renderer, StatsOverlay, SCREEN_HEIGHT, SCREEN_WIDTH};
use ctru::prelude::*;
use std::ffi::c_void;
use std::ptr::{null, null_mut};

fn near(a: f64, b: f64) -> bool {
    (a - b).abs() < 1e-4
}

unsafe fn test_errors(c: &mut Checks) {
    check!(c, glGetError() == GL_NO_ERROR);

    gl!(glEnable(0x1234));
    check!(c, glGetError() == GL_INVALID_ENUM);
    check!(c, glGetError() == GL_NO_ERROR); // Reading resets it

    // Only the first error is kept
    gl!(glMatrixMode(0x1234));
    glViewport(0, 0, -1, -1);
    check!(c, glGetError() == GL_INVALID_ENUM);
    check!(c, glGetError() == GL_NO_ERROR);

    glViewport(0, 0, -1, -1);
    check!(c, glGetError() == GL_INVALID_VALUE);

    gl!(glPixelStorei(GL_UNPACK_ALIGNMENT, 3));
    check!(c, glGetError() == GL_INVALID_VALUE);

    glPopMatrix();
    check!(c, glGetError() == GL_STACK_UNDERFLOW);

    let mut tex = 0u32;
    glGenTextures(1, &mut tex);
    gl!(glBindTexture(GL_TEXTURE_2D, tex));
    let pixel = [0u8; 4];
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_DOUBLE, pixel.as_ptr()));
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel.as_ptr()));
    check!(c, glGetError() == GL_NO_ERROR);
    check!(c, glIsTexture(tex) != 0);
    glDeleteTextures(1, &tex);
    check!(c, glIsTexture(tex) == 0);
    check!(c, glIsTexture(0) == 0);
}

unsafe fn test_capabilities(c: &mut Checks) {
    check!(c, glIsEnabled(GL_DITHER) != 0); // OpenGL default
    gl!(glDisable(GL_DITHER));
    check!(c, glIsEnabled(GL_DITHER) == 0);
    gl!(glDisable(GL_LIGHTING)); // Accepted silently
    check!(c, glGetError() == GL_NO_ERROR);
    check!(c, glIsEnabled(GL_LIGHTING) == 0);

    check!(c, glIsEnabled(GL_BLEND) == 0);
    glEnable(GL_BLEND);
    check!(c, glIsEnabled(GL_BLEND) != 0);
    let mut b = 0u8;
    glGetBooleanv(GL_BLEND, &mut b);
    check!(c, b == 1);
    glDisable(GL_BLEND);

    glEnableClientState(GL_VERTEX_ARRAY);
    check!(c, glIsEnabled(GL_VERTEX_ARRAY) != 0);
    glDisableClientState(GL_VERTEX_ARRAY);
    check!(c, glIsEnabled(GL_VERTEX_ARRAY) == 0);
}

unsafe fn test_queries(c: &mut Checks) {
    let mut v = [0i32; 4];
    glGetIntegerv(GL_VIEWPORT, v.as_mut_ptr());
    check!(c, v[0] == 0 && v[1] == 0 && v[2] == 400 && v[3] == 240);
    glViewport(10, 20, 30, 40);
    glGetIntegerv(GL_VIEWPORT, v.as_mut_ptr());
    check!(c, v[0] == 10 && v[1] == 20 && v[2] == 30 && v[3] == 40);
    glViewport(0, 0, 400, 240);

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, v.as_mut_ptr());
    check!(c, v[0] == 1024);
    glGetIntegerv(GL_DEPTH_BITS, v.as_mut_ptr());
    check!(c, v[0] == 24);
    glGetIntegerv(GL_STENCIL_BITS, v.as_mut_ptr());
    check!(c, v[0] == 8);

    glDepthFunc(GL_LEQUAL);
    glGetIntegerv(GL_DEPTH_FUNC, v.as_mut_ptr());
    check!(c, v[0] == GL_LEQUAL as i32);
    glDepthFunc(GL_LESS);

    glStencilFunc(GL_EQUAL, 5, 0x0F);
    glGetIntegerv(GL_STENCIL_REF, &mut v[0]);
    glGetIntegerv(GL_STENCIL_VALUE_MASK, &mut v[1]);
    glGetIntegerv(GL_STENCIL_FUNC, &mut v[2]);
    check!(c, v[0] == 5 && v[1] == 0x0F && v[2] == GL_EQUAL as i32);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);

    glMatrixMode(GL_PROJECTION);
    glGetIntegerv(GL_MATRIX_MODE, v.as_mut_ptr());
    check!(c, v[0] == GL_PROJECTION as i32);
    glMatrixMode(GL_MODELVIEW);

    let mut f = [0f32; 4];
    glClearColor(0.25, 0.5, 0.75, 1.0);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, f.as_mut_ptr());
    check!(c, (f[0] - 0.25).abs() < 0.01 && (f[1] - 0.5).abs() < 0.01 && (f[2] - 0.75).abs() < 0.01 && f[3] == 1.0);

    // Colors: floats as set (8 bits), integers scaled to [0, INT_MAX]
    glColor3ub(255, 0, 0);
    glGetFloatv(GL_CURRENT_COLOR, f.as_mut_ptr());
    check!(c, f[0] == 1.0 && f[1] == 0.0 && f[2] == 0.0 && f[3] == 1.0);
    glGetIntegerv(GL_CURRENT_COLOR, v.as_mut_ptr());
    check!(c, v[0] == 2147483647 && v[1] == 0);
    let green = [0.0f32, 1.0, 0.0, 0.0];
    glColor4fv(green.as_ptr());
    glGetFloatv(GL_CURRENT_COLOR, f.as_mut_ptr());
    check!(c, f[0] == 0.0 && f[1] == 1.0 && f[3] == 0.0);
    glColor3f(1.0, 1.0, 1.0);

    glTexCoord2i(3, 4);
    glGetFloatv(GL_CURRENT_TEXTURE_COORDS, f.as_mut_ptr());
    check!(c, f[0] == 3.0 && f[1] == 4.0);

    let mut mask = [0u8; 4];
    glColorMask(1, 0, 1, 0);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask.as_mut_ptr());
    check!(c, mask[0] != 0 && mask[1] == 0 && mask[2] != 0 && mask[3] == 0);
    glColorMask(1, 1, 1, 1);

    gl!(glGetIntegerv(0x1234, v.as_mut_ptr()));
    check!(c, glGetError() == GL_INVALID_ENUM);
}

unsafe fn test_matrices(c: &mut Checks) {
    let mut m = [0f32; 16];
    let mut out = [0f32; 16];
    for (i, v) in m.iter_mut().enumerate() {
        *v = (i + 1) as f32;
    }
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m.as_ptr());
    glGetFloatv(GL_MODELVIEW_MATRIX, out.as_mut_ptr());
    check!(c, out == m);

    let mut d = [0f64; 16];
    glLoadIdentity();
    glTranslated(1.0, 2.0, 3.0);
    glGetDoublev(GL_MODELVIEW_MATRIX, d.as_mut_ptr());
    check!(c, near(d[12], 1.0) && near(d[13], 2.0) && near(d[14], 3.0) && near(d[15], 1.0));
    glLoadIdentity();
    glRotated(90.0, 0.0, 0.0, 1.0); // x axis -> y axis
    glGetDoublev(GL_MODELVIEW_MATRIX, d.as_mut_ptr());
    check!(c, near(d[0], 0.0) && near(d[1], 1.0));

    for (i, v) in d.iter_mut().enumerate() {
        *v = if i % 5 == 0 { 2.0 } else { 0.0 };
    }
    glLoadMatrixd(d.as_ptr());
    glScaled(0.5, 0.5, 0.5);
    glGetFloatv(GL_MODELVIEW_MATRIX, out.as_mut_ptr());
    check!(c, out[0] == 1.0 && out[5] == 1.0 && out[10] == 1.0 && out[15] == 2.0);

    let mut depth = 0i32;
    glLoadIdentity();
    glPushMatrix();
    glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &mut depth);
    check!(c, depth == 2);
    glPopMatrix();
    glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &mut depth);
    check!(c, depth == 1);
}

unsafe fn test_arrays_and_es(c: &mut Checks) {
    // Array state and validation
    static SHORTS: [i16; 8] = [0; 8];
    let shorts = SHORTS.as_ptr() as *const c_void;
    let mut v = 0i32;
    gl!(glVertexPointer(2, GL_SHORT, 4, shorts));
    glGetIntegerv(GL_VERTEX_ARRAY_SIZE, &mut v);
    check!(c, v == 2);
    glGetIntegerv(GL_VERTEX_ARRAY_TYPE, &mut v);
    check!(c, v == GL_SHORT as i32);
    glGetIntegerv(GL_VERTEX_ARRAY_STRIDE, &mut v);
    check!(c, v == 4);
    let mut ptr: *mut c_void = null_mut();
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &mut ptr);
    check!(c, ptr as *const c_void == shorts);
    glGetIntegerv(GL_COLOR_ARRAY_SIZE, &mut v);
    check!(c, v == 4); // Default
    gl!(glVertexPointer(1, GL_FLOAT, 0, shorts));
    check!(c, glGetError() == GL_INVALID_VALUE);
    gl!(glVertexPointer(2, GL_UNSIGNED_BYTE, 0, shorts));
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glColorPointer(2, GL_FLOAT, 0, shorts));
    check!(c, glGetError() == GL_INVALID_VALUE);
    gl!(glNormalPointer(GL_UNSIGNED_BYTE, 0, shorts));
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glDrawElements(GL_TRIANGLES, 3, GL_FLOAT, shorts));
    check!(c, glGetError() == GL_INVALID_ENUM);

    // glInterleavedArrays sets up and enables the arrays of the format
    static INTERLEAVED: [f32; 12] = [0.0; 12];
    gl!(glInterleavedArrays(GL_T2F_N3F_V3F, 0, INTERLEAVED.as_ptr()));
    glGetIntegerv(GL_TEXTURE_COORD_ARRAY_STRIDE, &mut v);
    check!(c, v == 32);
    glGetPointerv(GL_NORMAL_ARRAY_POINTER, &mut ptr);
    check!(c, ptr as *const c_void == INTERLEAVED.as_ptr().add(2).cast());
    check!(c, glIsEnabled(GL_NORMAL_ARRAY) != 0 && glIsEnabled(GL_TEXTURE_COORD_ARRAY) != 0 && glIsEnabled(GL_COLOR_ARRAY) == 0);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    // Integer colors and normals are normalized, texcoords keep r and q
    let mut f = [0f32; 4];
    glColor3b(127, 63, -128); // (2c + 1)/255: 1.0, 0.5, -1.0 (clamped to 0)
    glGetFloatv(GL_CURRENT_COLOR, f.as_mut_ptr());
    check!(c, f[0] == 1.0 && (f[1] - 0.5).abs() < 0.01 && f[2] == 0.0 && f[3] == 1.0);
    glColor4us(65535, 0, 65535, 32768);
    glGetFloatv(GL_CURRENT_COLOR, f.as_mut_ptr());
    check!(c, f[0] == 1.0 && f[1] == 0.0 && (f[3] - 0.5).abs() < 0.01);
    glNormal3s(32767, -32768, 0);
    glGetFloatv(GL_CURRENT_NORMAL, f.as_mut_ptr());
    check!(c, near(f[0] as f64, 1.0) && near(f[1] as f64, -1.0) && f[2].abs() < 1e-4);
    glTexCoord4f(1.0, 2.0, 3.0, 4.0);
    glGetFloatv(GL_CURRENT_TEXTURE_COORDS, f.as_mut_ptr());
    check!(c, f[0] == 1.0 && f[1] == 2.0 && f[2] == 3.0 && f[3] == 4.0);
    glTexCoord2f(0.0, 0.0);
    glColor3ub(255, 255, 255);

    // ES fixed point: 1.5 = 0x18000
    glLineWidthx(0x18000);
    let mut x = 0i32;
    glGetFixedv(GL_LINE_WIDTH, &mut x);
    check!(c, x == 0x18000);
    glLineWidth(1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatex(0x10000, 0x20000, -0x8000);
    let mut m = [0f32; 16];
    glGetFloatv(GL_MODELVIEW_MATRIX, m.as_mut_ptr());
    check!(c, m[12] == 1.0 && m[13] == 2.0 && m[14] == -0.5);
    glLoadIdentity();
    gl!(glTexEnvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE)); // Enums are not scaled
    check!(c, glGetError() == GL_NO_ERROR);
    gl!(glTexEnvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE));
}

unsafe fn test_buffers(c: &mut Checks) {
    let mut ids = [0u32; 2];
    glGenBuffers(2, ids.as_mut_ptr());
    check!(c, ids[0] != 0 && ids[1] != 0 && ids[0] != ids[1]);
    check!(c, glIsBuffer(ids[0]) != 0 && glIsBuffer(0) == 0);

    // Data, sub data, parameters
    let bytes = [1u8, 2, 3, 4, 5, 6, 7, 8];
    glBindBuffer(GL_ARRAY_BUFFER, ids[0]);
    gl!(glBufferData(GL_ARRAY_BUFFER, 8, bytes.as_ptr(), GL_DYNAMIC_DRAW));
    let mut v = 0i32;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &mut v);
    check!(c, v == 8);
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_USAGE, &mut v);
    check!(c, v == GL_DYNAMIC_DRAW as i32);
    gl!(glBufferSubData(GL_ARRAY_BUFFER, 6, 4, bytes.as_ptr()));
    check!(c, glGetError() == GL_INVALID_VALUE); // Beyond the end
    gl!(glBufferData(GL_ARRAY_BUFFER, 8, bytes.as_ptr(), 0x1234));
    check!(c, glGetError() == GL_INVALID_ENUM);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &mut v);
    check!(c, v == ids[0] as i32);

    // Arrays remember the buffer bound when their pointer was set; the pointer is an offset
    gl!(glVertexPointer(2, GL_SHORT, 0, 4usize as *const c_void));
    glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &mut v);
    check!(c, v == ids[0] as i32);
    let mut ptr: *mut c_void = null_mut();
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &mut ptr);
    check!(c, ptr as usize == 4);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &mut v);
    check!(c, v == ids[0] as i32); // Unchanged by rebinding

    // Deleting unbinds everywhere
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ids[0]);
    glDeleteBuffers(2, ids.as_ptr());
    check!(c, glIsBuffer(ids[0]) == 0);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &mut v);
    check!(c, v == 0);
    glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &mut v);
    check!(c, v == 0);

    // Errors without a bound buffer, wrong target
    gl!(glBufferData(GL_ARRAY_BUFFER, 4, bytes.as_ptr(), GL_STATIC_DRAW));
    check!(c, glGetError() == GL_INVALID_OPERATION);
    gl!(glBindBuffer(0x1234, 1));
    check!(c, glGetError() == GL_INVALID_ENUM);
}

unsafe fn test_attrib_stacks(c: &mut Checks) {
    let mut v = 0i32;
    let mut f = [0f32; 4];

    // ENABLE | COLOR_BUFFER: enables, blend func, clear color come back; depth func (DEPTH_BUFFER) does not
    glDisable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ZERO);
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glDepthFunc(GL_LESS);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT);
    glGetIntegerv(GL_ATTRIB_STACK_DEPTH, &mut v);
    check!(c, v == 1);
    glEnable(GL_BLEND);
    glEnable(GL_LINE_SMOOTH); // Stored-only capability, also restored
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(1.0, 0.0, 0.0, 1.0);
    glDepthFunc(GL_GREATER);
    glPopAttrib();
    check!(c, glIsEnabled(GL_BLEND) == 0 && glIsEnabled(GL_LINE_SMOOTH) == 0);
    glGetIntegerv(GL_BLEND_SRC, &mut v);
    check!(c, v == GL_ONE as i32);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, f.as_mut_ptr());
    check!(c, f[0] == 0.0);
    glGetIntegerv(GL_DEPTH_FUNC, &mut v);
    check!(c, v == GL_GREATER as i32); // Not in the pushed groups
    glDepthFunc(GL_LESS);

    // Nested: CURRENT inside VIEWPORT
    glColor3f(1.0, 1.0, 1.0);
    glViewport(0, 0, 400, 240);
    glPushAttrib(GL_VIEWPORT_BIT);
    glViewport(1, 2, 3, 4);
    glPushAttrib(GL_CURRENT_BIT);
    glColor3f(0.0, 1.0, 0.0);
    glGetIntegerv(GL_ATTRIB_STACK_DEPTH, &mut v);
    check!(c, v == 2);
    glPopAttrib();
    glGetFloatv(GL_CURRENT_COLOR, f.as_mut_ptr());
    check!(c, f[0] == 1.0 && f[1] == 1.0);
    let mut vp = [0i32; 4];
    glGetIntegerv(GL_VIEWPORT, vp.as_mut_ptr());
    check!(c, vp[0] == 1 && vp[3] == 4); // Still the inner value
    glPopAttrib();
    glGetIntegerv(GL_VIEWPORT, vp.as_mut_ptr());
    check!(c, vp[0] == 0 && vp[2] == 400);

    // TEXTURE: binding, environment and the bound texture's parameters
    let mut tex = [0u32; 2];
    glGenTextures(2, tex.as_mut_ptr());
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    gl!(glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE));
    glPushAttrib(GL_TEXTURE_BIT);
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    gl!(glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE));
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glActiveTexture(GL_TEXTURE1);
    glPopAttrib();
    glGetIntegerv(GL_ACTIVE_TEXTURE, &mut v);
    check!(c, v == GL_TEXTURE0 as i32);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &mut v);
    check!(c, v == tex[0] as i32);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &mut v);
    check!(c, v == GL_NEAREST as i32);
    glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &mut v);
    check!(c, v == GL_MODULATE as i32);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(2, tex.as_ptr());

    // POLYGON, STENCIL, SCISSOR
    glPushAttrib(GL_POLYGON_BIT | GL_STENCIL_BUFFER_BIT | GL_SCISSOR_BIT);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glCullFace(GL_FRONT);
    glStencilFunc(GL_EQUAL, 7, 0xFF);
    glEnable(GL_SCISSOR_TEST);
    glScissor(5, 5, 5, 5);
    glPopAttrib();
    let mut modes = [0i32; 2];
    glGetIntegerv(GL_POLYGON_MODE, modes.as_mut_ptr());
    check!(c, modes[0] == GL_FILL as i32 && modes[1] == GL_FILL as i32);
    glGetIntegerv(GL_CULL_FACE_MODE, &mut v);
    check!(c, v == GL_BACK as i32);
    glGetIntegerv(GL_STENCIL_REF, &mut v);
    check!(c, v == 0);
    check!(c, glIsEnabled(GL_SCISSOR_TEST) == 0);

    // Errors
    glPopAttrib();
    check!(c, glGetError() == GL_STACK_UNDERFLOW);
    let mut max = 0i32;
    glGetIntegerv(GL_MAX_ATTRIB_STACK_DEPTH, &mut max);
    check!(c, max >= 16);
    for _ in 0..=max {
        glPushAttrib(GL_CURRENT_BIT);
    }
    check!(c, glGetError() == GL_STACK_OVERFLOW);
    for _ in 0..max {
        glPopAttrib();
    }
    glGetIntegerv(GL_ATTRIB_STACK_DEPTH, &mut v);
    check!(c, v == 0);

    // Client: vertex arrays (with buffer binding) and pixel store
    static DATA: [f32; 4] = [0.0; 4];
    let mut buf = 0u32;
    glGenBuffers(1, &mut buf);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl!(glVertexPointer(2, GL_FLOAT, 0, DATA.as_ptr()));
    glDisableClientState(GL_VERTEX_ARRAY);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindBuffer(GL_ARRAY_BUFFER, buf);
    gl!(glVertexPointer(3, GL_SHORT, 6, null::<c_void>()));
    glEnableClientState(GL_VERTEX_ARRAY);
    glClientActiveTexture(GL_TEXTURE2);
    glGetIntegerv(GL_CLIENT_ATTRIB_STACK_DEPTH, &mut v);
    check!(c, v == 1);
    glPopClientAttrib();
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &mut v);
    check!(c, v == 4);
    glGetIntegerv(GL_VERTEX_ARRAY_SIZE, &mut v);
    check!(c, v == 2);
    let mut ptr: *mut c_void = null_mut();
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &mut ptr);
    check!(c, ptr as *const c_void == DATA.as_ptr().cast() && glIsEnabled(GL_VERTEX_ARRAY) == 0);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &mut v);
    check!(c, v == 0);
    glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &mut v);
    check!(c, v == GL_TEXTURE0 as i32);
    glPopClientAttrib();
    check!(c, glGetError() == GL_STACK_UNDERFLOW);
    glDeleteBuffers(1, &buf);
}

/// glReadPixels within a frame: orientation (GL window coordinates, bottom-left origin), formats
/// and types, pack layout, clipping, depth and stencil, drawing on after a read. Leaves a frame in
/// progress, the main loop clears it.
unsafe fn test_read_pixels(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.2, 0.4, 0.6, 0.8); // 51, 102, 153, 204
    glClearDepth(0.25);
    glClearStencil(0x5B);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glColor3ub(255, 0, 0);
    glRecti(0, 0, 8, 8); // Bottom left
    glColor3ub(0, 0, 255);
    glRecti(392, 232, 400, 240); // Top right

    let mut p = [0u8; 64];
    let read = |x: i32, y: i32, w: i32, h: i32, format: u32, ty: u32, out: *mut u8| glReadPixels(x, y, w, h, format, ty, out.cast());
    read(3, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr());
    check!(c, p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255);
    read(396, 236, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr());
    check!(c, p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255);
    read(200, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr());
    check!(c, p[0] == 51 && p[1] == 102 && p[2] == 153 && p[3] == 204);
    // Rows go up from y: row 0 is y = 7 (red), row 1 y = 8 (clear color); columns 7 and 8 likewise
    read(7, 7, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr());
    check!(c, p[0] == 255 && p[4] == 51 && p[8] == 51 && p[12] == 51);

    // Formats and types
    read(3, 3, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, p.as_mut_ptr());
    check!(c, p[0] == 255 && p[1] == 0 && p[2] == 0);
    read(200, 120, 1, 1, GL_ALPHA, GL_UNSIGNED_BYTE, p.as_mut_ptr());
    check!(c, p[0] == 204);
    read(200, 120, 1, 1, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, p.as_mut_ptr());
    check!(c, p[0] == 255 && p[1] == 204); // L = R + G + B, clamped
    let mut f = [0f32; 4];
    read(200, 120, 1, 1, GL_RGBA, GL_FLOAT, f.as_mut_ptr().cast());
    check!(c, near(f[0] as f64, 0.2) && near(f[1] as f64, 0.4) && near(f[2] as f64, 0.6) && near(f[3] as f64, 0.8));
    let mut us = [0u16; 2];
    read(3, 3, 1, 1, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, us.as_mut_ptr().cast());
    check!(c, us[0] == 0xF800);
    read(396, 236, 1, 1, GL_BLUE, GL_UNSIGNED_SHORT, us.as_mut_ptr().cast());
    check!(c, us[0] == 0xFFFF);
    let mut ss = [0i16; 1];
    read(3, 3, 1, 1, GL_RED, GL_SHORT, ss.as_mut_ptr().cast());
    check!(c, ss[0] == 32767);
    gl!(glPixelStorei(GL_PACK_SWAP_BYTES, GL_TRUE));
    read(3, 3, 1, 1, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, us.as_mut_ptr().cast());
    gl!(glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE));
    check!(c, us[0] == 0x00F8);

    // Pack layout: rows of 3 RGB pixels = 9 bytes, rows padded to 12 by GL_PACK_ALIGNMENT 4; skipped pixels stay untouched
    p.fill(0xAA);
    glPixelStorei(GL_PACK_ROW_LENGTH, 3);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 1);
    read(6, 7, 2, 2, GL_RGB, GL_UNSIGNED_BYTE, p.as_mut_ptr());
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    check!(c, p[0] == 0xAA && p[3] == 255 && p[6] == 255 && p[9] == 0xAA); // Row 0: y = 7, x = 6, 7 red
    check!(c, p[12] == 0xAA && p[15] == 51 && p[18] == 51 && p[21] == 0xAA); // Row 1 at 12: y = 8
    check!(c, glGetError() == GL_NO_ERROR);

    // Clipped: only the pixel inside the window is written
    p.fill(0xAA);
    read(-1, -1, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr());
    check!(c, p[0] == 0xAA && p[4] == 0xAA && p[8] == 0xAA && p[12] == 255);

    // Depth (cleared to 0.25, a quad at z = 0 is at 0.5) and stencil
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glRecti(100, 100, 108, 108);
    glDisable(GL_DEPTH_TEST);
    read(50, 50, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, f.as_mut_ptr().cast());
    read(104, 104, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, f.as_mut_ptr().add(1).cast());
    check!(c, (f[0] - 0.25).abs() < 1e-3 && (f[1] - 0.5).abs() < 1e-3);
    read(50, 50, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, p.as_mut_ptr());
    check!(c, p[0] == 0x5B);
    read(50, 50, 8, 1, GL_STENCIL_INDEX, GL_BITMAP, p.as_mut_ptr());
    check!(c, p[0] == 0xFF); // Bit 0 of 0x5B

    // Drawing goes on after a read, earlier draws stay
    glColor3ub(0, 255, 0);
    glRecti(200, 200, 208, 208);
    read(204, 204, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr());
    read(3, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr().add(4));
    check!(c, p[0] == 0 && p[1] == 255 && p[2] == 0 && p[4] == 255 && p[5] == 0);

    // Errors and the ES implementation read format
    read(0, 0, 1, 1, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, p.as_mut_ptr());
    check!(c, glGetError() == GL_INVALID_OPERATION);
    read(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_SHORT_5_6_5, p.as_mut_ptr());
    check!(c, glGetError() == GL_INVALID_OPERATION);
    read(0, 0, -1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr());
    check!(c, glGetError() == GL_INVALID_VALUE);
    read(0, 0, 1, 1, GL_RGBA, GL_BITMAP, p.as_mut_ptr());
    check!(c, glGetError() == GL_INVALID_ENUM);
    read(0, 0, 1, 1, 0x1234, GL_UNSIGNED_BYTE, p.as_mut_ptr());
    check!(c, glGetError() == GL_INVALID_ENUM);
    let mut v = [0i32; 2];
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT_OES, v.as_mut_ptr());
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE_OES, v.as_mut_ptr().add(1));
    check!(c, v[0] == GL_RGBA as i32 && v[1] == GL_UNSIGNED_BYTE as i32);
    glPopAttrib();
}

/// glCopyTexImage2D / glCopyTexSubImage2D within a frame: texel rows go up from y like
/// glReadPixels, component selection per internal format (luminance = R), borders, sub-copies
/// keeping the texture's format, draws before a copy keeping the old texels, errors. Leaves a frame
/// in progress, the main loop clears it.
unsafe fn test_copy_tex_image(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.2, 0.4, 0.6, 0.8); // 51, 102, 153, 204
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3ub(255, 0, 0);
    glRecti(0, 0, 4, 4); // Red bottom left, green to its right, clear color above
    glColor3ub(0, 255, 0);
    glRecti(4, 0, 8, 4);

    let mut tex = [0u32; 2];
    glGenTextures(2, tex.as_mut_ptr());
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    let mut p = [0u8; 8 * 8 * 4];
    gl!(glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 8, 8, 0));
    check!(c, glGetError() == GL_NO_ERROR);
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255); // Texel (0, 0)
    check!(c, p[5 * 4] == 0 && p[5 * 4 + 1] == 255); // (5, 0)
    check!(c, p[5 * 32] == 51 && p[5 * 32 + 1] == 102 && p[5 * 32 + 2] == 153 && p[5 * 32 + 3] == 204); // (0, 5)

    // Components by internal format: luminance is R, alpha is A
    gl!(glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 0, 0, 8, 8, 0));
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[0] == 255 && p[5] == 0 && p[5 * 8] == 51);
    let mut v = 0i32;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &mut v);
    check!(c, v == GL_LUMINANCE as i32);
    gl!(glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 0, 0, 8, 8, 0));
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_ALPHA, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[0] == 255 && p[5 * 8] == 204);
    gl!(glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, 0, 0, 8, 8, 0));
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[5 * 16] == 51 && p[5 * 16 + 1] == 204);

    // Border: the image is the inner 2x2 of the 4x4 rectangle at (3, 0), i.e. x = 4..5, y = 1..2 (green)
    gl!(glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 3, 0, 4, 4, 1));
    check!(c, glGetError() == GL_NO_ERROR);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &mut v);
    check!(c, v == 4);
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[0] == 0 && p[1] == 255 && p[8] == 0 && p[9] == 255); // Rows padded to 8 bytes

    // Sub-copy into an RGBA4 texture: texels (2, 3) and (3, 4) get red and the clear color, the rest stays 0
    let mut us = [0u16; 8 * 8];
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, us.as_ptr()));
    gl!(glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 2, 3, 3, 3, 2, 2));
    check!(c, glGetError() == GL_NO_ERROR);
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, us.as_mut_ptr()));
    check!(c, us[3 * 8 + 2] == 0xF00F && us[4 * 8 + 3] == 0x369C && us[3 * 8 + 1] == 0 && us[5 * 8 + 2] == 0);

    // A quad drawn before the copy keeps the old texels (white), one drawn after it samples the copy (red)
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    p.fill(255);
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, p.as_ptr()));
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    gl!(glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE));
    glEnable(GL_TEXTURE_2D);
    for i in 0..2 {
        let x = 100 + 20 * i;
        glBegin(GL_QUADS);
        glTexCoord2f(0.0, 0.0);
        glVertex2i(x, 100);
        glTexCoord2f(1.0, 0.0);
        glVertex2i(x + 8, 100);
        glTexCoord2f(1.0, 1.0);
        glVertex2i(x + 8, 108);
        glTexCoord2f(0.0, 1.0);
        glVertex2i(x, 108);
        glEnd();
        if i == 0 {
            gl!(glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 8, 8));
        }
    }
    glDisable(GL_TEXTURE_2D);
    gl!(glReadPixels(101, 101, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    gl!(glReadPixels(121, 101, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr().add(4)));
    check!(c, p[0] == 255 && p[1] == 255 && p[2] == 255);
    check!(c, p[4] == 255 && p[5] == 0 && p[6] == 0);
    check!(c, glGetError() == GL_NO_ERROR);

    // Errors
    gl!(glCopyTexImage2D(GL_TEXTURE_2D, 0, 3, 0, 0, 8, 8, 0)); // 1..4 are not allowed for copies
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glCopyTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_RGBA, 0, 0, 8, 8, 0));
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, -1, 8, 0));
    check!(c, glGetError() == GL_INVALID_VALUE);
    gl!(glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 8, 8, 2));
    check!(c, glGetError() == GL_INVALID_VALUE);
    gl!(glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 4, 4, 0, 0, 8, 8)); // Beyond the 8x8 image
    check!(c, glGetError() == GL_INVALID_VALUE);
    gl!(glCopyTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, 0, 0, 4, 4)); // Level 1 not defined
    check!(c, glGetError() == GL_INVALID_OPERATION);
    static ETC1: [u8; 8] = [0; 8];
    gl!(glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 4, 4, 0, 8, ETC1.as_ptr()));
    gl!(glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 4, 4));
    check!(c, glGetError() == GL_INVALID_OPERATION);
    glDeleteTextures(2, tex.as_ptr());
    glPopAttrib();
}

/// Paletted images expand to the palette format (read back with glGetTexImage), ETC1 is stored as
/// is; formats, image sizes and levels are validated.
unsafe fn test_compressed(c: &mut Checks) {
    let (mut n, mut formats) = (0i32, [0i32; 16]);
    glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &mut n);
    check!(c, n == 11);
    glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, formats.as_mut_ptr());
    check!(c, formats[0] == GL_PALETTE4_RGB8_OES as i32 && formats[9] == GL_PALETTE8_RGB5_A1_OES as i32 && formats[10] == GL_ETC1_RGB8_OES as i32);
    let ext = std::ffi::CStr::from_ptr(glGetString(GL_EXTENSIONS).cast()).to_string_lossy().into_owned();
    check!(c, ext.contains("GL_OES_compressed_paletted_texture"));
    check!(c, ext.contains("GL_OES_compressed_ETC1_RGB8_texture"));

    let mut tex = 0u32;
    glGenTextures(1, &mut tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    // 3x1 PALETTE4_RGB8: indices 1, 2, 3, high nibble first, the last nibble unused
    let mut p4 = [0u8; 16 * 3 + 2];
    for i in 0..4 {
        p4[i * 3] = (10 * i) as u8;
        p4[i * 3 + 1] = (10 * i + 1) as u8;
        p4[i * 3 + 2] = (10 * i + 2) as u8;
    }
    p4[48] = 0x12;
    p4[49] = 0x30;
    gl!(glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_PALETTE4_RGB8_OES, 3, 1, 0, p4.len(), p4.as_ptr()));
    check!(c, glGetError() == GL_NO_ERROR);
    let mut rgb = [0u8; 9];
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.as_mut_ptr()));
    check!(c, rgb[0] == 10 && rgb[1] == 11 && rgb[2] == 12 && rgb[3] == 20 && rgb[6] == 30 && rgb[8] == 32);
    let mut value = 0i32;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &mut value);
    check!(c, value == GL_PALETTE4_RGB8_OES as i32);

    // 2x2 PALETTE8_R5_G6_B5 with both levels (level = -1): 512 bytes palette, 4 + 1 indices
    let mut p8 = [0u8; 512 + 5];
    for (index, color) in [(7usize, 0xF800u16), (200, 0x07E0), (255, 0x001F)] {
        p8[index * 2..index * 2 + 2].copy_from_slice(&color.to_ne_bytes());
    }
    p8[512..517].copy_from_slice(&[7, 200, 255, 0, 7]);
    gl!(glCompressedTexImage2D(GL_TEXTURE_2D, -1, GL_PALETTE8_R5_G6_B5_OES, 2, 2, 0, p8.len(), p8.as_ptr()));
    check!(c, glGetError() == GL_NO_ERROR);
    let mut texels = [0u16; 4];
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, texels.as_mut_ptr()));
    check!(c, texels[0] == 0xF800 && texels[1] == 0x07E0 && texels[2] == 0x001F && texels[3] == 0);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &mut value);
    check!(c, value == 1);

    // Errors
    gl!(glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_PALETTE4_RGB8_OES, 3, 1, 0, p4.len() - 1, p4.as_ptr()));
    check!(c, glGetError() == GL_INVALID_VALUE); // imageSize
    gl!(glCompressedTexImage2D(GL_TEXTURE_2D, 1, GL_PALETTE4_RGB8_OES, 3, 1, 0, p4.len(), p4.as_ptr()));
    check!(c, glGetError() == GL_INVALID_VALUE); // Paletted levels are <= 0
    gl!(glCompressedTexImage2D(GL_TEXTURE_2D, -2, GL_PALETTE8_R5_G6_B5_OES, 2, 2, 0, p8.len(), p8.as_ptr()));
    check!(c, glGetError() == GL_INVALID_VALUE); // 2x2 has 2 levels
    gl!(glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_PALETTE4_RGB8_OES, 3, 1, 1, p4.len(), p4.as_ptr()));
    check!(c, glGetError() == GL_INVALID_VALUE); // Border
    gl!(glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 3, 1, 0, p4.len(), p4.as_ptr()));
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_PALETTE4_RGB8_OES, p4.len(), p4.as_ptr()));
    check!(c, glGetError() == GL_INVALID_OPERATION);

    // ETC1: 8 bytes per 4x4 block, 6x5 = 2x2 blocks
    let etc1 = [0u8; 32];
    gl!(glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 6, 5, 0, 24, etc1.as_ptr()));
    check!(c, glGetError() == GL_INVALID_VALUE);
    gl!(glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 6, 5, 0, 32, etc1.as_ptr()));
    check!(c, glGetError() == GL_NO_ERROR);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &mut value);
    check!(c, value == 5);
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.as_mut_ptr()));
    check!(c, glGetError() == GL_INVALID_OPERATION); // Cannot be read back
    gl!(glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_ETC1_RGB8_OES, 8, etc1.as_ptr()));
    check!(c, glGetError() == GL_INVALID_OPERATION);

    // Proxy: too large, then fitting
    gl!(glCompressedTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 2048, 8, 0, 512 * 2 * 8, null::<u8>()));
    glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &mut value);
    check!(c, value == 0);
    gl!(glCompressedTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 64, 8, 0, 16 * 2 * 8, null::<u8>()));
    glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &mut value);
    check!(c, value == 64);
    check!(c, glGetError() == GL_NO_ERROR);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glDeleteTextures(1, &tex);
}

/// Expected result of a GL logic op on one byte.
fn logic_op_result(op: u32, s: u8, d: u8) -> u8 {
    match op {
        GL_CLEAR => 0,
        GL_AND => s & d,
        GL_AND_REVERSE => s & !d,
        GL_COPY => s,
        GL_AND_INVERTED => !s & d,
        GL_NOOP => d,
        GL_XOR => s ^ d,
        GL_OR => s | d,
        GL_NOR => !(s | d),
        GL_EQUIV => !(s ^ d),
        GL_INVERT => !d,
        GL_OR_REVERSE => s | !d,
        GL_COPY_INVERTED => !s,
        GL_OR_INVERTED => !s | d,
        GL_NAND => !(s & d),
        _ => 0xFF, // GL_SET
    }
}

/// glLogicOp: all 16 ops on RGBA read back with glReadPixels, precedence over blending, clears
/// unaffected, queries, attribute groups. Multisampling state (no sample buffers: stored only,
/// drawing unchanged). Leaves a frame in progress.
unsafe fn test_logic_op_and_multisample(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    let mut v = 0i32;
    check!(c, glIsEnabled(GL_COLOR_LOGIC_OP) == 0 && glIsEnabled(GL_INDEX_LOGIC_OP) == 0);
    glGetIntegerv(GL_LOGIC_OP_MODE, &mut v);
    check!(c, v == GL_COPY as i32);

    // Destination 0x5A 0xA5 0x0F 0xF0, source 0x33 0x55 0xFF 0x00 (the clear quad, if any, ignores the logic op)
    let dst = [0x5Au8, 0xA5, 0x0F, 0xF0];
    let src = [0x33u8, 0x55, 0xFF, 0x00];
    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_SET);
    glClearColor(dst[0] as f32 / 255.0, dst[1] as f32 / 255.0, dst[2] as f32 / 255.0, dst[3] as f32 / 255.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND); // Ignored while the logic op is on
    glBlendFunc(GL_ZERO, GL_ONE);
    glColor4ub(src[0], src[1], src[2], src[3]);
    for i in 0..16 {
        glLogicOp(GL_CLEAR + i);
        glRecti(4 * i as i32, 0, 4 * i as i32 + 4, 4);
    }
    check!(c, glGetError() == GL_NO_ERROR);
    let mut p = [0u8; 4];
    let mut failed = 0;
    for i in 0..16u32 {
        let mut px = [0u8; 4];
        gl!(glReadPixels(4 * i + 2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.as_mut_ptr()));
        let ok = (0..4).all(|k| px[k] == logic_op_result(GL_CLEAR + i, src[k], dst[k]));
        failed += !ok as i32;
        check!(c, ok);
    }
    // Azahar's hardware renderers on macOS (Vulkan/MoltenVK, OpenGL) skip logic ops; the software renderer has them
    if failed != 0 {
        print!("{failed} logic ops wrong: Azahar on macOS needs\nthe software renderer for them\n");
    }

    // Disabled: blending is back (GL_ZERO, GL_ONE keeps the destination)
    glDisable(GL_COLOR_LOGIC_OP);
    glRecti(100, 0, 104, 4);
    gl!(glReadPixels(102, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p == dst);

    // Errors, glGet, attribute groups (COLOR_BUFFER and ENABLE)
    glLogicOp(GL_XOR);
    gl!(glLogicOp(0x1234));
    check!(c, glGetError() == GL_INVALID_ENUM);
    glGetIntegerv(GL_LOGIC_OP_MODE, &mut v);
    check!(c, v == GL_XOR as i32);
    glPushAttrib(GL_COLOR_BUFFER_BIT);
    glEnable(GL_COLOR_LOGIC_OP);
    glEnable(GL_INDEX_LOGIC_OP); // Color index mode: stored only
    glLogicOp(GL_NAND);
    let mut b = 0u8;
    glGetBooleanv(GL_COLOR_LOGIC_OP, &mut b);
    check!(c, b != 0 && glIsEnabled(GL_LOGIC_OP) != 0);
    glPopAttrib();
    glGetIntegerv(GL_LOGIC_OP_MODE, &mut v);
    check!(c, v == GL_XOR as i32 && glIsEnabled(GL_COLOR_LOGIC_OP) == 0 && glIsEnabled(GL_INDEX_LOGIC_OP) == 0);
    glPushAttrib(GL_ENABLE_BIT);
    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_NAND);
    glPopAttrib();
    glGetIntegerv(GL_LOGIC_OP_MODE, &mut v);
    check!(c, glIsEnabled(GL_COLOR_LOGIC_OP) == 0 && v == GL_NAND as i32);

    // Multisampling: defaults, no sample buffers
    let mut f = 0f32;
    check!(
        c,
        glIsEnabled(GL_MULTISAMPLE) != 0 && glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE) == 0 && glIsEnabled(GL_SAMPLE_ALPHA_TO_ONE) == 0 && glIsEnabled(GL_SAMPLE_COVERAGE) == 0
    );
    glGetIntegerv(GL_SAMPLE_BUFFERS, &mut v);
    check!(c, v == 0);
    glGetIntegerv(GL_SAMPLES, &mut v);
    check!(c, v == 0);
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &mut f);
    glGetBooleanv(GL_SAMPLE_COVERAGE_INVERT, &mut b);
    check!(c, f == 1.0 && b == 0);
    glSampleCoverage(0.25, 1);
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &mut f);
    glGetBooleanv(GL_SAMPLE_COVERAGE_INVERT, &mut b);
    check!(c, f == 0.25 && b != 0);
    glSampleCoverage(2.0, 0); // Clamped
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &mut f);
    check!(c, f == 1.0);
    glSampleCoveragex(0x8000, 0); // ES fixed point: 0.5
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &mut f);
    check!(c, f == 0.5);

    // Without sample buffers the sample operations do not change what is drawn
    glPushAttrib(GL_MULTISAMPLE_BIT);
    glEnable(GL_SAMPLE_ALPHA_TO_ONE);
    glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glEnable(GL_SAMPLE_COVERAGE);
    glDisable(GL_MULTISAMPLE);
    glSampleCoverage(0.0, 0);
    glDisable(GL_BLEND);
    glColor4ub(10, 20, 30, 40);
    glRecti(110, 0, 114, 4);
    gl!(glReadPixels(112, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p == [10, 20, 30, 40]);
    check!(c, glIsEnabled(GL_SAMPLE_ALPHA_TO_ONE) != 0 && glIsEnabled(GL_MULTISAMPLE) == 0);
    glPopAttrib();
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &mut f);
    check!(
        c,
        f == 0.5 && glIsEnabled(GL_MULTISAMPLE) != 0 && glIsEnabled(GL_SAMPLE_ALPHA_TO_ONE) == 0 && glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE) == 0 && glIsEnabled(GL_SAMPLE_COVERAGE) == 0
    );
    check!(c, glGetError() == GL_NO_ERROR);
    glPopAttrib();
}

/// Color of window pixel (x, y).
unsafe fn pixel_is(x: i32, y: i32, r: u8, g: u8, b: u8) -> bool {
    let mut p = [0u8; 4];
    gl!(glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    p[0] == r && p[1] == g && p[2] == b
}

unsafe fn modelview_translation(x: f32, y: f32, z: f32) -> bool {
    let mut m = [0f32; 16];
    glGetFloatv(GL_MODELVIEW_MATRIX, m.as_mut_ptr());
    near(m[12] as f64, x as f64) && near(m[13] as f64, y as f64) && near(m[14] as f64, z as f64)
}

/// Color of one pixel: 'r' red, 'b' blue (the two halves of the texgen texture), '?' anything else.
unsafe fn tex_gen_pixel(x: i32, y: i32) -> char {
    let mut p = [0u8; 4];
    gl!(glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    if p[0] == 255 && p[1] == 0 && p[2] == 0 {
        'r'
    } else if p[0] == 0 && p[1] == 0 && p[2] == 255 {
        'b'
    } else {
        '?'
    }
}

unsafe fn test_tex_gen(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);

    // Defaults: eye linear, s and t planes along x and y, all off
    let mut mode = 0i32;
    let mut plane = [0f32; 4];
    glGetTexGeniv(GL_S, GL_TEXTURE_GEN_MODE, &mut mode);
    check!(c, mode == GL_EYE_LINEAR as i32);
    glGetTexGenfv(GL_T, GL_OBJECT_PLANE, plane.as_mut_ptr());
    check!(c, plane == [0.0, 1.0, 0.0, 0.0]);
    glGetTexGenfv(GL_R, GL_EYE_PLANE, plane.as_mut_ptr());
    check!(c, plane == [0.0, 0.0, 0.0, 0.0]);
    check!(c, glIsEnabled(GL_TEXTURE_GEN_S) == 0 && glIsEnabled(GL_TEXTURE_GEN_Q) == 0);

    // Errors: sphere map only for s and t, planes only through the v functions
    gl!(glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP));
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_LINEAR));
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glTexGeni(0x1234, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR));
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glTexGenf(GL_S, GL_OBJECT_PLANE, 1.0));
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glGetTexGeniv(GL_S, 0x1234, &mut mode));
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP));
    glGetTexGeniv(GL_T, GL_TEXTURE_GEN_MODE, &mut mode);
    check!(c, mode == GL_SPHERE_MAP as i32 && glGetError() == GL_NO_ERROR);

    // The eye plane is transformed by the inverse modelview: (1, 0, 0, 0) under a translation by 1 in x is x - 1
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(1.0, 0.0, 0.0);
    let eye_plane = [1.0f64, 0.0, 0.0, 0.0];
    glTexGendv(GL_S, GL_EYE_PLANE, eye_plane.as_ptr());
    glLoadIdentity();
    let mut d = [0f64; 4];
    glGetTexGendv(GL_S, GL_EYE_PLANE, d.as_mut_ptr());
    check!(c, near(d[0], 1.0) && near(d[1], 0.0) && near(d[2], 0.0) && near(d[3], -1.0));
    let object_plane = [2i32, 3, 4, 5];
    glTexGeniv(GL_Q, GL_OBJECT_PLANE, object_plane.as_ptr());
    let mut iv = [0i32; 4];
    glGetTexGeniv(GL_Q, GL_OBJECT_PLANE, iv.as_mut_ptr());
    check!(c, iv == [2, 3, 4, 5]);

    // Per texture unit; enables in GL_ENABLE_BIT, all of it in GL_TEXTURE_BIT
    glActiveTexture(GL_TEXTURE1);
    glEnable(GL_TEXTURE_GEN_S);
    glGetTexGeniv(GL_T, GL_TEXTURE_GEN_MODE, &mut mode);
    check!(c, mode == GL_EYE_LINEAR as i32 && glIsEnabled(GL_TEXTURE_GEN_S) != 0);
    glActiveTexture(GL_TEXTURE0);
    let mut b = 1u8;
    glGetBooleanv(GL_TEXTURE_GEN_S, &mut b);
    check!(c, b == 0);
    glPushAttrib(GL_ENABLE_BIT);
    glEnable(GL_TEXTURE_GEN_T);
    gl!(glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR));
    glPopAttrib();
    glGetTexGeniv(GL_S, GL_TEXTURE_GEN_MODE, &mut mode);
    check!(c, glIsEnabled(GL_TEXTURE_GEN_T) == 0 && mode == GL_OBJECT_LINEAR as i32);
    glPushAttrib(GL_TEXTURE_BIT);
    glEnable(GL_TEXTURE_GEN_T);
    gl!(glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR));
    glPopAttrib();
    glGetTexGeniv(GL_S, GL_TEXTURE_GEN_MODE, &mut mode);
    check!(c, glIsEnabled(GL_TEXTURE_GEN_T) == 0 && mode == GL_OBJECT_LINEAR as i32);

    // Display lists record glTexGen
    let list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    gl!(glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP));
    glTexGenfv(GL_S, GL_OBJECT_PLANE, [6.0f32, 7.0, 8.0, 9.0].as_ptr());
    glEndList();
    glGetTexGeniv(GL_S, GL_TEXTURE_GEN_MODE, &mut mode);
    check!(c, mode == GL_OBJECT_LINEAR as i32);
    glCallList(list);
    glGetTexGeniv(GL_S, GL_TEXTURE_GEN_MODE, &mut mode);
    glGetTexGenfv(GL_S, GL_OBJECT_PLANE, plane.as_mut_ptr());
    check!(c, mode == GL_SPHERE_MAP as i32 && plane[0] == 6.0 && plane[3] == 9.0);
    glDeleteLists(list, 1);
    glPopAttrib();
    check!(c, glGetError() == GL_NO_ERROR);

    // Rendering. 8x8 texture, left half red, right half blue: s < 0.5 is red, s > 0.5 blue
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    let mut texels = [0u8; 8 * 8 * 4];
    for i in 0..8 * 8 {
        let right = i % 8 >= 4;
        texels[4 * i..4 * i + 4].copy_from_slice(&[if right { 0 } else { 255 }, 0, if right { 255 } else { 0 }, 255]);
    }
    let mut tex = 0u32;
    glGenTextures(1, &mut tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.as_ptr()));
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP));
    glEnable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3ub(255, 255, 255);

    // Object linear s = x/16 replaces the current s (0.9: blue everywhere)
    let s_plane = [1.0f32 / 16.0, 0.0, 0.0, 0.0];
    gl!(glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR));
    glTexGenfv(GL_S, GL_OBJECT_PLANE, s_plane.as_ptr());
    glEnable(GL_TEXTURE_GEN_S);
    glTexCoord2f(0.9, 0.0);
    glRecti(0, 20, 16, 36);
    check!(c, tex_gen_pixel(3, 28) == 'r' && tex_gen_pixel(12, 28) == 'b');

    // Eye linear: the plane was given under a translation by 100, so s = (x_eye - 100)/16
    glTranslatef(100.0, 0.0, 0.0);
    gl!(glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR));
    glTexGenfv(GL_S, GL_EYE_PLANE, s_plane.as_ptr());
    glLoadIdentity();
    glRecti(100, 20, 116, 36);
    check!(c, tex_gen_pixel(103, 28) == 'r' && tex_gen_pixel(112, 28) == 'b');

    // Generated r goes through the texture matrix (s' = r): s = 0, r = x/16
    gl!(glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR));
    glTexGenfv(GL_S, GL_OBJECT_PLANE, [0.0f32, 0.0, 0.0, 0.0].as_ptr());
    gl!(glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR));
    glTexGenfv(GL_R, GL_OBJECT_PLANE, [1.0f32 / 16.0, 0.0, 0.0, -200.0 / 16.0].as_ptr());
    glEnable(GL_TEXTURE_GEN_R);
    glMatrixMode(GL_TEXTURE);
    let swap_sr = [0.0f32, 0., 0., 0., 0., 1., 0., 0., 1., 0., 0., 0., 0., 0., 0., 1.]; // Column-major: s' = r
    glLoadMatrixf(swap_sr.as_ptr());
    glMatrixMode(GL_MODELVIEW);
    glRecti(200, 20, 216, 36);
    check!(c, tex_gen_pixel(203, 28) == 'r' && tex_gen_pixel(212, 28) == 'b');
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_TEXTURE_GEN_R);

    // Generated q divides (projection mode on unit 0): s = x/16, q = 2
    glTexGenfv(GL_S, GL_OBJECT_PLANE, [1.0f32 / 16.0, 0.0, 0.0, -300.0 / 16.0].as_ptr());
    gl!(glTexGeni(GL_Q, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR));
    glTexGenfv(GL_Q, GL_OBJECT_PLANE, [0.0f32, 0.0, 0.0, 2.0].as_ptr());
    glEnable(GL_TEXTURE_GEN_Q);
    glRecti(300, 20, 332, 36);
    check!(c, tex_gen_pixel(306, 28) == 'r' && tex_gen_pixel(312, 28) == 'r' && tex_gen_pixel(328, 28) == 'b');
    glDisable(GL_TEXTURE_GEN_Q);

    // Unit 1 (unit 0 off): object linear s = (x - 20)/16
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex);
    glEnable(GL_TEXTURE_2D);
    gl!(glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR));
    glTexGenfv(GL_S, GL_OBJECT_PLANE, [1.0f32 / 16.0, 0.0, 0.0, -20.0 / 16.0].as_ptr());
    glEnable(GL_TEXTURE_GEN_S);
    glRecti(20, 60, 36, 76);
    check!(c, tex_gen_pixel(23, 68) == 'r' && tex_gen_pixel(32, 68) == 'b');
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);

    // Sphere map, seen straight on (eye position ~ (0, 0, -500)): normal (1, 0, 1) reflects to s ~ 0.85, (-1, 0, 1) to s ~ 0.15
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-200.0, 200.0, -120.0, 120.0, -1000.0, 1000.0);
    glMatrixMode(GL_MODELVIEW);
    glTranslatef(0.0, 0.0, -500.0);
    glEnable(GL_NORMALIZE);
    gl!(glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP));
    gl!(glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP));
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);
    glNormal3f(1.0, 0.0, 1.0);
    glRecti(-28, -8, -12, 8);
    glNormal3f(-1.0, 0.0, 1.0);
    glRecti(12, -8, 28, 8);
    check!(c, tex_gen_pixel(180, 120) == 'b' && tex_gen_pixel(220, 120) == 'r');
    glDeleteTextures(1, &tex);
    glPopAttrib();
    check!(c, glGetError() == GL_NO_ERROR);
}

/// Window coordinates for the next draws (pushed with the rest of the state by the caller).
unsafe fn window_projection() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/// A w x 8 quad at (x, y) with s from 0 to 1 and t from t0 to t1.
unsafe fn textured_quad(x: i32, y: i32, w: i32, t0: f32, t1: f32) {
    glBegin(GL_QUADS);
    glTexCoord2f(0.0, t0);
    glVertex2i(x, y);
    glTexCoord2f(1.0, t0);
    glVertex2i(x + w, y);
    glTexCoord2f(1.0, t1);
    glVertex2i(x + w, y + 8);
    glTexCoord2f(0.0, t1);
    glVertex2i(x, y + 8);
    glEnd();
}

/// Color of window pixel (x, y) within 1, alpha too unless a < 0; prints it otherwise.
unsafe fn pixel_near(x: i32, y: i32, r: i32, g: i32, b: i32, a: i32) -> bool {
    let mut p = [0u8; 4];
    gl!(glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    let d = |v: u8, e: i32| (v as i32 - e).abs() <= 1;
    let ok = d(p[0], r) && d(p[1], g) && d(p[2], b) && (a < 0 || d(p[3], a));
    if !ok {
        println!("  pixel ({x}, {y}): {} {} {} {}", p[0], p[1], p[2], p[3]);
    }
    ok
}

/// Internal formats (sized, intensity) loaded from any format/type, glGetTexImage conversions,
/// texture priorities, residency and the border color.
unsafe fn test_tex_formats(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    window_projection();
    let mut tex = [0u32; 2];
    glGenTextures(2, tex.as_mut_ptr());
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    let mut v = 0i32;
    let mut p = [0u8; 64];

    // RGBA data into an RGB texture: alpha is dropped and reads back as 1
    static RGBA: [u8; 16] = [255, 0, 0, 10, 0, 255, 0, 20, 0, 0, 255, 30, 10, 20, 30, 40];
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, RGBA.as_ptr()));
    check!(c, glGetError() == GL_NO_ERROR);
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[0] == 255 && p[3] == 255 && p[12] == 10 && p[13] == 20 && p[14] == 30 && p[15] == 255);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &mut v);
    check!(c, v == GL_RGB8 as i32);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_ALPHA_SIZE, &mut v);
    check!(c, v == 0);

    // Sized 16-bit formats from bytes
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA4, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, RGBA.as_ptr()));
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_RED_SIZE, &mut v);
    check!(c, v == 4);
    let mut us = [0u16; 4];
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, us.as_mut_ptr()));
    check!(c, us[0] == 0xF001 && us[1] == 0x0F01);
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_R3_G3_B2, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, RGBA.as_ptr()));
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_GREEN_SIZE, &mut v);
    check!(c, v == 6);
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, us.as_mut_ptr()));
    check!(c, us[0] == 0xF800 && us[1] == 0x07E0);

    // Other types (GL 1.1 table 2.9, clamped), swapped bytes, single components
    static FL: [f32; 8] = [1.0, 0.5, 0.0, 2.0, -1.0, 0.25, 1.0, 0.0];
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_FLOAT, FL.as_ptr()));
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[..8] == [255, 128, 0, 255, 0, 64, 255, 0]);
    let mut f = [0f32; 8];
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, f.as_mut_ptr()));
    check!(c, f[0] == 1.0 && near(f[1] as f64, 128.0 / 255.0) && f[7] == 0.0);
    static SB: [i8; 2] = [127, -128];
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 2, 1, 0, GL_LUMINANCE, GL_BYTE, SB.as_ptr()));
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[0] == 255 && p[1] == 0);
    static GS: [u16; 2] = [0x00FF, 0xFF00];
    gl!(glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE));
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 2, 1, 0, GL_ALPHA, GL_UNSIGNED_SHORT, GS.as_ptr()));
    gl!(glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE));
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_ALPHA, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[0] == 254 && p[1] == 1);
    static RED: [u8; 2] = [200, 100];
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 1, 0, GL_RED, GL_UNSIGNED_BYTE, RED.as_ptr()));
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[0] == 200 && p[1] == 0 && p[2] == 0 && p[3] == 100);
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[0] == 200 && p[1] == 100);

    // Sub images are converted to the texture's format: luminance data into RGB
    gl!(glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 0, 1, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, RED.as_ptr()));
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[3] == 200 && p[4] == 200 && p[5] == 200 && glGetError() == GL_NO_ERROR);

    // Intensity: I is R of the image, read back as R (table 6.1), sampled as (I, I, I, I)
    static LA: [u8; 4] = [200, 50, 100, 255];
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_INTENSITY8, 2, 1, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, LA.as_ptr()));
    let mut lum = -1i32;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTENSITY_SIZE, &mut v);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_LUMINANCE_SIZE, &mut lum);
    check!(c, v == 8 && lum == 0);
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[0] == 200 && p[1] == 0 && p[3] == 255 && p[4] == 100);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    glEnable(GL_TEXTURE_2D);
    glColor4ub(255, 255, 255, 255);
    gl!(glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE));
    textured_quad(20, 20, 8, 0.0, 1.0);
    check!(c, pixel_near(21, 21, 200, 200, 200, 200));
    // GL_BLEND with a zero env color: C = Cf*(1 - I), and for intensity also A = Af*(1 - I)
    gl!(glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND));
    textured_quad(40, 20, 8, 0.0, 1.0);
    check!(c, pixel_near(41, 21, 55, 55, 55, 55));
    // GL_ADD: C = Cf + I, A = Af + I (luminance alpha: Af*At); texel 1 on black with alpha 128
    glColor4ub(0, 0, 0, 128);
    gl!(glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD));
    textured_quad(60, 20, 8, 0.0, 1.0);
    check!(c, pixel_near(66, 21, 100, 100, 100, 228));
    // The same luminance alpha texture blends alpha as Af*At
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, 2, 1, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, LA.as_ptr()));
    glColor4ub(255, 255, 255, 255);
    gl!(glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND));
    textured_quad(80, 20, 8, 0.0, 1.0);
    check!(c, pixel_near(81, 21, 55, 55, 55, 50));
    glDisable(GL_TEXTURE_2D);

    // Errors
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, 5, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, RGBA.as_ptr()));
    check!(c, glGetError() == GL_INVALID_VALUE);
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGB, GL_UNSIGNED_SHORT_4_4_4_4, RGBA.as_ptr()));
    check!(c, glGetError() == GL_INVALID_OPERATION);
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_BITMAP, RGBA.as_ptr()));
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, f.as_mut_ptr()));
    check!(c, glGetError() == GL_INVALID_ENUM);

    // Priorities are stored (clamped), also through display lists as floats; all textures are resident
    let prio = [0.25f32, 2.0];
    glPrioritizeTextures(2, tex.as_ptr(), prio.as_ptr());
    let mut fv = [0f32; 4];
    glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, fv.as_mut_ptr());
    check!(c, fv[0] == 0.25);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, fv.as_mut_ptr());
    check!(c, fv[0] == 1.0);
    let list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, 0.5);
    glEndList();
    glCallList(list);
    glDeleteLists(list, 1);
    glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, fv.as_mut_ptr());
    check!(c, fv[0] == 0.5);
    let mut res = [0u8; 2];
    check!(c, glAreTexturesResident(2, tex.as_ptr(), res.as_mut_ptr()) == 1 && res[0] == 0); // Untouched when all are resident
    let bad = [tex[0], 0];
    check!(c, glAreTexturesResident(2, bad.as_ptr(), res.as_mut_ptr()) == 0 && glGetError() == GL_INVALID_VALUE);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_RESIDENT, &mut v);
    check!(c, v == GL_TRUE as i32);
    let border = [0.5f32, 0.25, 2.0, 0.0];
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border.as_ptr());
    glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, fv.as_mut_ptr());
    check!(c, fv == [0.5, 0.25, 1.0, 0.0]);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, 1.0);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glDeleteTextures(2, tex.as_ptr());
    glPopAttrib();
    check!(c, glGetError() == GL_NO_ERROR);
}

/// 1D textures: binding, images with a border, sub images, proxies, rendering (t has no effect),
/// GL_TEXTURE_2D taking precedence, mipmaps (also generated), copies from the framebuffer, display
/// lists, attribute stacks.
unsafe fn test_1d(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    window_projection();
    let mut tex = [0u32; 3];
    glGenTextures(3, tex.as_mut_ptr());
    let mut v = 0i32;
    let mut p = [0u8; 64 * 3];

    glBindTexture(GL_TEXTURE_1D, tex[0]);
    glGetIntegerv(GL_TEXTURE_BINDING_1D, &mut v);
    check!(c, v == tex[0] as i32);
    glBindTexture(GL_TEXTURE_2D, tex[0]); // Already a 1D texture
    check!(c, glGetError() == GL_INVALID_OPERATION);

    // The border texels are dropped; height 1
    static ROW: [u8; 18] = [9, 9, 9, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0, 9, 9, 9];
    gl!(glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 6, 1, GL_RGB, GL_UNSIGNED_BYTE, ROW.as_ptr()));
    check!(c, glGetError() == GL_NO_ERROR);
    glGetTexLevelParameteriv(GL_TEXTURE_1D, 0, GL_TEXTURE_WIDTH, &mut v);
    check!(c, v == 6);
    glGetTexLevelParameteriv(GL_TEXTURE_1D, 0, GL_TEXTURE_HEIGHT, &mut v);
    check!(c, v == 1);
    gl!(glGetTexImage(GL_TEXTURE_1D, 0, GL_RGB, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[0] == 255 && p[4] == 255 && p[8] == 255 && p[9] == 255 && p[10] == 255 && p[11] == 0);
    static WHITE: [u8; 3] = [255, 255, 255];
    gl!(glTexSubImage1D(GL_TEXTURE_1D, 0, 2, 1, GL_RGB, GL_UNSIGNED_BYTE, WHITE.as_ptr()));
    gl!(glGetTexImage(GL_TEXTURE_1D, 0, GL_RGB, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[6] == 255 && p[7] == 255 && p[8] == 255);
    gl!(glTexSubImage1D(GL_TEXTURE_1D, 0, 2, 1, GL_RGB, GL_UNSIGNED_BYTE, ROW.as_ptr().add(9)));

    // Proxies
    gl!(glTexImage1D(GL_PROXY_TEXTURE_1D, 0, GL_RGBA, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, null::<u8>()));
    glGetTexLevelParameteriv(GL_PROXY_TEXTURE_1D, 0, GL_TEXTURE_WIDTH, &mut v);
    check!(c, v == 64);
    gl!(glTexImage1D(GL_PROXY_TEXTURE_1D, 0, GL_RGBA, 2048, 0, GL_RGBA, GL_UNSIGNED_BYTE, null::<u8>()));
    glGetTexLevelParameteriv(GL_PROXY_TEXTURE_1D, 0, GL_TEXTURE_WIDTH, &mut v);
    check!(c, v == 0 && glGetError() == GL_NO_ERROR);

    // Texel i across s, whatever t is
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    gl!(glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    gl!(glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    gl!(glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE));
    glEnable(GL_TEXTURE_1D);
    textured_quad(100, 100, 32, 5.0, -3.0);
    check!(c, pixel_near(101, 101, 255, 0, 0, -1) && pixel_near(109, 104, 0, 255, 0, -1) && pixel_near(117, 106, 0, 0, 255, -1) && pixel_near(130, 107, 255, 255, 0, -1));

    // GL_TEXTURE_2D takes precedence
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, WHITE.as_ptr()));
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    glEnable(GL_TEXTURE_2D);
    textured_quad(140, 100, 32, 0.0, 1.0);
    glDisable(GL_TEXTURE_2D);
    check!(c, pixel_near(141, 101, 255, 255, 255, -1));

    // Mipmaps: 64 texels red at level 0, green below (stored down to 8 texels, level 3), the rest only for completeness.
    // A 128 pixel wide quad samples level 0, a 2 pixel wide one level 5, clamped to level 3 (sizes with room for
    // Azahar's resolution scaling, which lowers the LOD)
    glBindTexture(GL_TEXTURE_1D, tex[2]);
    for level in 0..=6 {
        for i in 0..64 {
            p[i * 3..i * 3 + 3].copy_from_slice(&if level == 0 { [255, 0, 0] } else { [0, 255, 0] });
        }
        gl!(glTexImage1D(GL_TEXTURE_1D, level, GL_RGB, 64 >> level, 0, GL_RGB, GL_UNSIGNED_BYTE, p.as_ptr()));
    }
    gl!(glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST));
    gl!(glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    textured_quad(150, 100, 128, 0.0, 1.0);
    textured_quad(280, 100, 2, 0.0, 1.0);
    check!(c, pixel_near(207, 104, 255, 0, 0, -1) && pixel_near(280, 104, 0, 255, 0, -1));

    // Generated: red and blue texels alternating average to purple from level 1 on
    gl!(glTexParameteri(GL_TEXTURE_1D, GL_GENERATE_MIPMAP, GL_TRUE));
    for i in 0..64 {
        p[i * 3..i * 3 + 3].copy_from_slice(&if i & 1 == 1 { [0, 0, 255] } else { [255, 0, 0] });
    }
    gl!(glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 64, 0, GL_RGB, GL_UNSIGNED_BYTE, p.as_ptr()));
    textured_quad(290, 100, 2, 0.0, 1.0);
    check!(c, pixel_near(290, 104, 128, 0, 128, -1));
    glDisable(GL_TEXTURE_1D);

    // Copies: red at x = 0..3, green at 4..7 of the bottom row
    glBindTexture(GL_TEXTURE_1D, tex[0]);
    glColor3ub(255, 0, 0);
    glRecti(0, 0, 4, 1);
    glColor3ub(0, 255, 0);
    glRecti(4, 0, 8, 1);
    gl!(glCopyTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 0, 0, 8, 0));
    check!(c, glGetError() == GL_NO_ERROR);
    gl!(glGetTexImage(GL_TEXTURE_1D, 0, GL_RGB, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[0] == 255 && p[1] == 0 && p[5 * 3] == 0 && p[5 * 3 + 1] == 255);
    gl!(glCopyTexSubImage1D(GL_TEXTURE_1D, 0, 0, 4, 0, 2));
    gl!(glGetTexImage(GL_TEXTURE_1D, 0, GL_RGB, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, p[0] == 0 && p[1] == 255 && p[3] == 0 && p[4] == 255 && p[6] == 255);

    // Display lists record 1D images and copies
    let list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    gl!(glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 4, 1, GL_RGB, GL_UNSIGNED_BYTE, ROW.as_ptr()));
    gl!(glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 1, GL_RGB, GL_UNSIGNED_BYTE, WHITE.as_ptr()));
    glEndList();
    gl!(glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, WHITE.as_ptr()));
    glCallList(list);
    glDeleteLists(list, 1);
    glGetTexLevelParameteriv(GL_TEXTURE_1D, 0, GL_TEXTURE_WIDTH, &mut v);
    gl!(glGetTexImage(GL_TEXTURE_1D, 0, GL_RGB, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    check!(c, v == 4 && p[0] == 255 && p[1] == 255 && p[3] == 0 && p[4] == 255);

    // Attribute stacks: GL_TEXTURE_BIT has the 1D enable and binding
    glEnable(GL_TEXTURE_1D);
    glPushAttrib(GL_TEXTURE_BIT);
    glDisable(GL_TEXTURE_1D);
    glBindTexture(GL_TEXTURE_1D, tex[2]);
    glPopAttrib();
    glGetIntegerv(GL_TEXTURE_BINDING_1D, &mut v);
    check!(c, glIsEnabled(GL_TEXTURE_1D) != 0 && v == tex[0] as i32);
    glDisable(GL_TEXTURE_1D);

    // Errors
    gl!(glTexImage1D(GL_TEXTURE_2D, 0, GL_RGB, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, WHITE.as_ptr()));
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glTexImage2D(GL_TEXTURE_1D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, WHITE.as_ptr()));
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glTexSubImage1D(GL_TEXTURE_1D, 0, 4, 1, GL_RGB, GL_UNSIGNED_BYTE, WHITE.as_ptr()));
    check!(c, glGetError() == GL_INVALID_VALUE);
    gl!(glCopyTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 0, 0, 8, 2));
    check!(c, glGetError() == GL_INVALID_VALUE);
    glDeleteTextures(3, tex.as_ptr());
    glPopAttrib();
    check!(c, glGetError() == GL_NO_ERROR);
}

/// glDrawBuffer / glReadBuffer: double-buffered, no stereo or aux buffers; GL_NONE draws and clears no color.
unsafe fn test_color_buffers(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    window_projection();
    let mut v = [0i32; 2];
    let mut b = [0u8, 1];
    glGetIntegerv(GL_DRAW_BUFFER, &mut v[0]);
    glGetIntegerv(GL_READ_BUFFER, &mut v[1]);
    check!(c, v[0] == GL_BACK as i32 && v[1] == GL_BACK as i32);
    glGetBooleanv(GL_DOUBLEBUFFER, &mut b[0]);
    glGetBooleanv(GL_STEREO, &mut b[1]);
    glGetIntegerv(GL_AUX_BUFFERS, &mut v[0]);
    check!(c, b[0] == 1 && b[1] == 0 && v[0] == 0);
    glDrawBuffer(GL_RIGHT);
    check!(c, glGetError() == GL_INVALID_OPERATION);
    glDrawBuffer(GL_AUX0);
    check!(c, glGetError() == GL_INVALID_OPERATION);
    gl!(glDrawBuffer(0x1234));
    check!(c, glGetError() == GL_INVALID_ENUM);
    glReadBuffer(GL_NONE);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glReadBuffer(GL_FRONT_AND_BACK);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glClearColor(1.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawBuffer(GL_NONE);
    glClearColor(0.0, 1.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3ub(0, 0, 255);
    glRecti(10, 10, 20, 20);
    check!(c, pixel_near(15, 15, 255, 0, 0, -1) && pixel_near(100, 100, 255, 0, 0, -1));
    glDrawBuffer(GL_FRONT); // Drawn like the back buffer
    glRecti(10, 10, 20, 20);
    glReadBuffer(GL_FRONT_LEFT);
    check!(c, pixel_near(15, 15, 0, 0, 255, -1));

    // Groups: draw buffer in GL_COLOR_BUFFER_BIT, read buffer in GL_PIXEL_MODE_BIT; both in display lists
    glPushAttrib(GL_COLOR_BUFFER_BIT | GL_PIXEL_MODE_BIT);
    let list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_BACK);
    glEndList();
    glCallList(list);
    glDeleteLists(list, 1);
    glGetIntegerv(GL_DRAW_BUFFER, &mut v[0]);
    glGetIntegerv(GL_READ_BUFFER, &mut v[1]);
    check!(c, v[0] == GL_NONE as i32 && v[1] == GL_BACK as i32);
    glPopAttrib();
    glGetIntegerv(GL_DRAW_BUFFER, &mut v[0]);
    glGetIntegerv(GL_READ_BUFFER, &mut v[1]);
    check!(c, v[0] == GL_FRONT as i32 && v[1] == GL_FRONT_LEFT as i32);
    glPopAttrib();
    glGetIntegerv(GL_DRAW_BUFFER, &mut v[0]);
    check!(c, v[0] == GL_BACK as i32 && glGetError() == GL_NO_ERROR);
}

/// Depth of window pixel (x, y) within 1e-3.
unsafe fn depth_near(x: i32, y: i32, d: f32) -> bool {
    let mut f = -1.0f32;
    gl!(glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &mut f as *mut f32));
    let ok = (f - d).abs() < 1e-3;
    if !ok {
        println!("  depth ({x}, {y}): {f}");
    }
    ok
}

unsafe fn stencil_at(x: i32, y: i32) -> u8 {
    let mut s = 0u8;
    gl!(glReadPixels(x, y, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, &mut s as *mut u8));
    s
}

/// glRasterPos (transform, clipping, lit color, queries), glDrawPixels (formats, unpack modes,
/// zoom, tiles, fragment operations, depth and stencil), glBitmap, glCopyPixels, attribute groups,
/// display lists and errors.
unsafe fn test_pixels(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    let mut f = [0f32; 4];
    let mut v = [0i32; 4];
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f.as_mut_ptr());
    glGetIntegerv(GL_CURRENT_RASTER_POSITION_VALID, v.as_mut_ptr());
    check!(c, f == [0.0, 0.0, 0.0, 1.0] && v[0] == 1);
    glGetFloatv(GL_CURRENT_RASTER_COLOR, f.as_mut_ptr());
    check!(c, f == [1.0, 1.0, 1.0, 1.0]);
    glGetFloatv(GL_ZOOM_X, f.as_mut_ptr());
    glGetFloatv(GL_ZOOM_Y, f.as_mut_ptr().add(1));
    check!(c, f[0] == 1.0 && f[1] == 1.0);

    // Raster position: transformed to window coordinates, current color and texcoords (texture matrix applied)
    window_projection();
    glColor4ub(10, 20, 30, 40);
    glTexCoord2f(0.5, 0.25);
    glMatrixMode(GL_TEXTURE);
    glTranslatef(1.0, 0.0, 0.0);
    glMatrixMode(GL_MODELVIEW);
    glRasterPos3f(30.0, 40.0, 0.5); // Ortho z: window depth 0.25
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f.as_mut_ptr());
    let n = |i: usize| f[i] as f64;
    check!(c, near(n(0), 30.0) && near(n(1), 40.0) && near(n(2), 0.25) && near(n(3), 1.0));
    glGetFloatv(GL_CURRENT_RASTER_COLOR, f.as_mut_ptr());
    check!(c, (f[1] - 20.0 / 255.0).abs() < 1e-3 && (f[3] - 40.0 / 255.0).abs() < 1e-3);
    glGetFloatv(GL_CURRENT_RASTER_TEXTURE_COORDS, f.as_mut_ptr());
    check!(c, near(f[0] as f64, 1.5) && near(f[1] as f64, 0.25) && near(f[3] as f64, 1.0));
    glGetFloatv(GL_CURRENT_RASTER_DISTANCE, f.as_mut_ptr());
    check!(c, (f[0] - (30.0f32 * 30.0 + 40.0 * 40.0 + 0.25).sqrt()).abs() < 1e-3);
    glTranslatef(5.0, 0.0, 0.0);
    glRasterPos4f(20.0, 40.0, 0.0, 2.0); // (10, 20) moved by 5
    glLoadIdentity();
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f.as_mut_ptr());
    check!(c, near(f[0] as f64, 15.0) && near(f[1] as f64, 20.0) && near(f[3] as f64, 2.0));

    // Clipped: invalid, the rest stays
    glRasterPos2i(-5, 10);
    glGetIntegerv(GL_CURRENT_RASTER_POSITION_VALID, v.as_mut_ptr());
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f.as_mut_ptr());
    check!(c, v[0] == 0 && near(f[0] as f64, 15.0));
    glRasterPos2i(5, 10);
    glGetIntegerv(GL_CURRENT_RASTER_POSITION_VALID, v.as_mut_ptr());
    check!(c, v[0] == 1);
    let plane = [-1.0f64, 0.0, 0.0, 4.0]; // x <= 4
    glClipPlane(GL_CLIP_PLANE0, plane.as_ptr());
    glEnable(GL_CLIP_PLANE0);
    glRasterPos2i(5, 10);
    glGetIntegerv(GL_CURRENT_RASTER_POSITION_VALID, v.as_mut_ptr());
    check!(c, v[0] == 0);
    glDisable(GL_CLIP_PLANE0);

    // Lit raster color: ambient only
    glEnable(GL_LIGHTING);
    let white = [1.0f32; 4];
    let ambient = [0.5f32, 0.25, 1.0, 1.0];
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, white.as_ptr());
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambient.as_ptr());
    glRasterPos2i(5, 10);
    glDisable(GL_LIGHTING);
    glGetFloatv(GL_CURRENT_RASTER_COLOR, f.as_mut_ptr());
    check!(c, (f[0] - 0.5).abs() < 0.01 && (f[1] - 0.25).abs() < 0.01 && f[2] == 1.0 && f[3] == 1.0);

    // glDrawPixels: rows go up from the raster position
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClearDepth(0.5);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    static IMAGE: [u8; 16] = [255, 0, 0, 255, 0, 255, 0, 255, // Bottom row: red, green
        0, 0, 255, 255, 255, 255, 255, 128]; // Top row: blue, white
    let draw = |w: i32, h: i32, format: u32, ty: u32, data: *const u8| glDrawPixels(w, h, format, ty, data);
    glRasterPos2i(20, 20);
    draw(2, 2, GL_RGBA, GL_UNSIGNED_BYTE, IMAGE.as_ptr());
    check!(c, pixel_near(20, 20, 255, 0, 0, 255) && pixel_near(21, 20, 0, 255, 0, 255));
    check!(c, pixel_near(20, 21, 0, 0, 255, 255) && pixel_near(21, 21, 255, 255, 255, 128));
    check!(c, pixel_near(22, 20, 0, 0, 0, 0) && pixel_near(20, 22, 0, 0, 0, 0) && pixel_near(19, 19, 0, 0, 0, 0));

    // Zoom (pixel (i, j) covers [x + zx*i, x + zx*(i + 1)) x ...), negative zoom mirrors
    glPixelZoom(2.0, 3.0);
    glRasterPos2i(40, 20);
    draw(2, 1, GL_RGBA, GL_UNSIGNED_BYTE, IMAGE.as_ptr());
    check!(c, pixel_near(40, 20, 255, 0, 0, 255) && pixel_near(41, 22, 255, 0, 0, 255) && pixel_near(42, 20, 0, 255, 0, 255));
    check!(c, pixel_near(43, 22, 0, 255, 0, 255) && pixel_near(44, 20, 0, 0, 0, 0) && pixel_near(40, 23, 0, 0, 0, 0));
    glPixelZoom(-1.0, 1.0);
    glRasterPos2i(60, 20);
    draw(2, 1, GL_RGBA, GL_UNSIGNED_BYTE, IMAGE.as_ptr());
    check!(c, pixel_near(59, 20, 255, 0, 0, 255) && pixel_near(58, 20, 0, 255, 0, 255) && pixel_near(60, 20, 0, 0, 0, 0));
    glPixelZoom(1.0, 1.0);

    // Formats, types and unpack modes
    static LUM: [f32; 2] = [0.5, 1.0];
    glRasterPos2i(70, 20);
    draw(2, 1, GL_LUMINANCE, GL_FLOAT, LUM.as_ptr().cast());
    check!(c, pixel_near(70, 20, 128, 128, 128, 255) && pixel_near(71, 20, 255, 255, 255, 255));
    static RGB565: [u16; 1] = [0x07E0];
    draw(1, 1, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, RGB565.as_ptr().cast());
    check!(c, pixel_near(70, 20, 0, 255, 0, 255));
    // Rows of 8 bytes (alignment 8), each starting with a skipped pixel: red, then blue
    static ROWS2: [u8; 16] = [1, 2, 3, 255, 0, 0, 9, 9, 4, 5, 6, 0, 0, 255, 9, 9];
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
    glRasterPos2i(80, 20);
    draw(1, 2, GL_RGB, GL_UNSIGNED_BYTE, ROWS2.as_ptr());
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    check!(c, pixel_near(80, 20, 255, 0, 0, 255) && pixel_near(80, 21, 0, 0, 255, 255));
    // More than one tile (256 pixels): a 300 x 1 ramp
    let ramp: Vec<u8> = (0..300).flat_map(|i| [(i & 0xFF) as u8, (i >> 8) as u8, 7]).collect();
    glRasterPos2i(10, 30);
    draw(300, 1, GL_RGB, GL_UNSIGNED_BYTE, ramp.as_ptr());
    check!(c, pixel_near(10, 30, 0, 0, 7, 255) && pixel_near(265, 30, 255, 0, 7, 255) && pixel_near(266, 30, 0, 1, 7, 255));
    check!(c, pixel_near(309, 30, 43, 1, 7, 255) && pixel_near(310, 30, 0, 0, 0, 0));
    // Partly off screen
    glRasterPos2i(398, 20);
    draw(2, 2, GL_RGBA, GL_UNSIGNED_BYTE, IMAGE.as_ptr());
    check!(c, pixel_near(399, 21, 255, 255, 255, 128));

    // Fragment operations: blending, depth test with the raster depth (cleared to 0.5), scissor
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glRasterPos2i(20, 20);
    draw(1, 1, GL_RGBA, GL_UNSIGNED_BYTE, IMAGE.as_ptr().add(4)); // Green onto red
    glDisable(GL_BLEND);
    check!(c, pixel_near(20, 20, 255, 255, 0, 255));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glRasterPos3f(100.0, 20.0, -0.5); // Window depth 0.75: behind
    draw(1, 1, GL_RGBA, GL_UNSIGNED_BYTE, IMAGE.as_ptr());
    glRasterPos3f(101.0, 20.0, 0.5); // 0.25: in front
    draw(1, 1, GL_RGBA, GL_UNSIGNED_BYTE, IMAGE.as_ptr());
    glDisable(GL_DEPTH_TEST);
    check!(c, pixel_near(100, 20, 0, 0, 0, 0) && pixel_near(101, 20, 255, 0, 0, 255) && depth_near(101, 20, 0.25));
    glEnable(GL_SCISSOR_TEST);
    glScissor(111, 0, 10, 240);
    glRasterPos2i(110, 20);
    draw(2, 1, GL_RGBA, GL_UNSIGNED_BYTE, IMAGE.as_ptr());
    glDisable(GL_SCISSOR_TEST);
    check!(c, pixel_near(110, 20, 0, 0, 0, 0) && pixel_near(111, 20, 0, 255, 0, 255));

    // Depth images: written with the depth test (color untouched), stencil images directly. Runs of 8 x 4
    // pixels per value: Azahar's upscaled readbacks mix neighboring pixels
    let mut depths = [0f32; 64];
    let depths2 = [0.5f32; 64];
    let mut stencils = [0u8; 64];
    for i in 0..64 {
        depths[i] = if i % 16 < 8 { 0.25 } else { 0.75 };
        stencils[i] = if i % 16 < 8 { 0x3C } else { 0xFF };
    }
    static STENCIL_BITS: [u8; 16] = [0xFF, 0, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0, 0, 0]; // Rows of 4 bytes
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glRasterPos2i(120, 20);
    draw(16, 4, GL_DEPTH_COMPONENT, GL_FLOAT, depths.as_ptr().cast());
    check!(c, depth_near(123, 21, 0.25) && depth_near(131, 21, 0.75) && depth_near(145, 21, 0.5));
    glDepthFunc(GL_LESS);
    draw(16, 4, GL_DEPTH_COMPONENT, GL_FLOAT, depths2.as_ptr().cast());
    check!(c, depth_near(123, 21, 0.25) && depth_near(131, 21, 0.5) && pixel_near(123, 21, 0, 0, 0, 0));
    glDisable(GL_DEPTH_TEST);
    draw(16, 4, GL_DEPTH_COMPONENT, GL_FLOAT, depths.as_ptr().cast()); // Depth test off: depth not written
    check!(c, depth_near(131, 21, 0.5));
    glStencilMask(0x0F);
    draw(16, 4, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencils.as_ptr());
    glStencilMask(0xFF);
    check!(c, stencil_at(123, 21) == 0x0C && stencil_at(131, 21) == 0x0F && stencil_at(145, 21) == 0);
    glRasterPos2i(150, 20);
    draw(16, 4, GL_STENCIL_INDEX, GL_BITMAP, STENCIL_BITS.as_ptr());
    check!(c, stencil_at(153, 21) == 1 && stencil_at(161, 21) == 0);
    check!(c, depth_near(153, 21, 0.5)); // Stencil images leave depth

    // glCopyPixels: color (with zoom), depth, stencil
    glRasterPos2i(140, 40);
    glPixelZoom(2.0, 1.0);
    glCopyPixels(20, 20, 2, 2, GL_COLOR);
    glPixelZoom(1.0, 1.0);
    check!(c, pixel_near(141, 40, 255, 255, 0, 255) && pixel_near(142, 40, 0, 255, 0, 255) && pixel_near(143, 41, 255, 255, 255, 128));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glRasterPos2i(170, 20);
    glCopyPixels(120, 20, 16, 4, GL_DEPTH);
    glDisable(GL_DEPTH_TEST);
    glCopyPixels(120, 20, 16, 4, GL_STENCIL);
    check!(c, depth_near(173, 21, 0.25) && depth_near(181, 21, 0.5) && stencil_at(173, 21) == 0x0C && stencil_at(181, 21) == 0x0F);

    // glBitmap: the raster color where bits are set, at floor(raster - origin); the raster position moves
    static BITS: [u8; 2] = [0xA5, 0x0F]; // Bottom row 10100101, top row 00001111
    glColor3ub(255, 0, 0);
    glRasterPos2f(150.5, 60.5);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBitmap(8, 2, 0.5, 0.0, 10.0, -1.0, BITS.as_ptr());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    check!(c, pixel_near(150, 60, 255, 0, 0, 255) && pixel_near(151, 60, 0, 0, 0, 0) && pixel_near(152, 60, 255, 0, 0, 255));
    check!(c, pixel_near(157, 60, 255, 0, 0, 255) && pixel_near(153, 61, 0, 0, 0, 0) && pixel_near(154, 61, 255, 0, 0, 255));
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f.as_mut_ptr());
    check!(c, near(f[0] as f64, 160.5) && near(f[1] as f64, 59.5));
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_TRUE as i32);
    glColor4ub(0, 0, 255, 0); // Alpha 0 is written too
    glRasterPos2i(170, 60);
    glBitmap(8, 1, 0.0, 0.0, 0.0, 0.0, BITS.as_ptr().add(1)); // LSB first: 11110000
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE as i32);
    check!(c, pixel_near(170, 60, 0, 0, 255, 0) && pixel_near(173, 60, 0, 0, 255, 0) && pixel_near(174, 60, 0, 0, 0, 0));
    glEnable(GL_ALPHA_TEST); // On the raster alpha
    glAlphaFunc(GL_GREATER, 0.5);
    glColor4ub(0, 255, 0, 100);
    glRasterPos2i(180, 60);
    glBitmap(8, 1, 0.0, 0.0, 0.0, 0.0, BITS.as_ptr());
    glColor4ub(0, 255, 0, 200);
    glRasterPos2i(190, 60);
    glBitmap(8, 1, 0.0, 0.0, 0.0, 0.0, BITS.as_ptr());
    glDisable(GL_ALPHA_TEST);
    check!(c, pixel_near(180, 60, 0, 0, 0, 0) && pixel_near(190, 60, 0, 255, 0, 200) && pixel_near(191, 60, 0, 0, 0, 0));
    glBitmap(0, 0, 0.0, 0.0, 5.0, 0.0, null()); // Only moves
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f.as_mut_ptr());
    check!(c, near(f[0] as f64, 195.0));
    glRasterPos2i(-1, 0); // Invalid: nothing drawn, no move
    glBitmap(8, 1, 0.0, 0.0, 5.0, 0.0, BITS.as_ptr());
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f.as_mut_ptr());
    check!(c, near(f[0] as f64, 195.0));

    // Attribute groups: raster position in GL_CURRENT_BIT, zoom in GL_PIXEL_MODE_BIT
    glRasterPos2i(1, 2);
    glPixelZoom(3.0, 4.0);
    glPushAttrib(GL_CURRENT_BIT | GL_PIXEL_MODE_BIT);
    glRasterPos2i(5, 6);
    glPixelZoom(1.0, 1.0);
    glPopAttrib();
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f.as_mut_ptr());
    glGetFloatv(GL_ZOOM_Y, f.as_mut_ptr().add(2));
    check!(c, near(f[0] as f64, 1.0) && near(f[1] as f64, 2.0) && f[2] == 4.0);
    glPixelZoom(1.0, 1.0);

    // Display lists: images are copied with the unpack state of glNewList time
    let list = glGenLists(1);
    let mut list_image = [255u8, 0, 255, 0, 255, 0];
    let mut list_bits = [0xC0u8];
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
    glNewList(list, GL_COMPILE);
    glRasterPos2i(200, 20);
    draw(1, 1, GL_RGB, GL_UNSIGNED_BYTE, list_image.as_ptr()); // Skip 1: green
    glPixelZoom(1.0, 2.0);
    glColor3ub(255, 255, 0);
    glRasterPos2i(210, 20);
    glBitmap(3, 1, 0.0, 0.0, 1.0, 0.0, list_bits.as_ptr()); // Skip 1: bits 1, 0, 0 -> one pixel set
    glCopyPixels(200, 20, 1, 1, GL_COLOR); // At (211, 20) after the bitmap's move, 1 x 2
    glEndList();
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    list_image.fill(0);
    list_bits[0] = 0;
    glPixelZoom(1.0, 1.0);
    glCallList(list);
    glDeleteLists(list, 1);
    glGetFloatv(GL_ZOOM_Y, f.as_mut_ptr());
    check!(c, f[0] == 2.0);
    glPixelZoom(1.0, 1.0);
    check!(c, pixel_near(200, 20, 0, 255, 0, 255) && pixel_near(210, 20, 255, 255, 0, 255) && pixel_near(210, 21, 0, 0, 0, 0));
    check!(c, pixel_near(211, 20, 0, 255, 0, 255) && pixel_near(211, 21, 0, 255, 0, 255) && pixel_near(212, 20, 0, 0, 0, 0));

    // Errors
    draw(-1, 1, GL_RGBA, GL_UNSIGNED_BYTE, IMAGE.as_ptr());
    check!(c, glGetError() == GL_INVALID_VALUE);
    draw(1, 1, GL_RGBA, GL_BITMAP, IMAGE.as_ptr());
    check!(c, glGetError() == GL_INVALID_ENUM);
    draw(1, 1, GL_DEPTH_COMPONENT, GL_BITMAP, IMAGE.as_ptr());
    check!(c, glGetError() == GL_INVALID_ENUM);
    draw(1, 1, GL_RGBA, GL_UNSIGNED_SHORT_5_6_5, IMAGE.as_ptr());
    check!(c, glGetError() == GL_INVALID_OPERATION);
    draw(1, 1, 0x1234, GL_UNSIGNED_BYTE, IMAGE.as_ptr());
    check!(c, glGetError() == GL_INVALID_ENUM);
    glCopyPixels(0, 0, 1, 1, GL_RGBA);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glCopyPixels(0, 0, -1, 1, GL_COLOR);
    check!(c, glGetError() == GL_INVALID_VALUE);
    glBitmap(-1, 1, 0.0, 0.0, 0.0, 0.0, BITS.as_ptr());
    check!(c, glGetError() == GL_INVALID_VALUE);
    glBegin(GL_POINTS);
    glRasterPos2i(0, 0);
    glEnd();
    check!(c, glGetError() == GL_INVALID_OPERATION);
    glPopAttrib();
    glGetFloatv(GL_CURRENT_RASTER_COLOR, f.as_mut_ptr());
    check!(c, f[0] == 1.0 && f[3] == 1.0 && glGetError() == GL_NO_ERROR);
}

/// glPixelTransfer and glPixelMap: state, queries, errors, attribute group and display lists, and
/// their effect on glDrawPixels (color, color index, depth, stencil), glReadPixels, glCopyPixels and
/// texture images (not glGetTexImage). Images are drawn zoomed to 8 x 4 per pixel and checked in the
/// middle: Azahar's upscaled readbacks mix neighbors.
unsafe fn test_pixel_transfer(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    let mut f = [0f32; 4];
    let mut mf = [0f32; 4];
    let mut v = [0i32; 4];
    let mut mui = [0u32; 4];
    let mut mus = [0u16; 4];

    // Defaults
    glGetIntegerv(GL_MAP_COLOR, v.as_mut_ptr());
    glGetIntegerv(GL_MAP_STENCIL, v.as_mut_ptr().add(1));
    glGetIntegerv(GL_INDEX_SHIFT, v.as_mut_ptr().add(2));
    glGetIntegerv(GL_INDEX_OFFSET, v.as_mut_ptr().add(3));
    check!(c, v == [0, 0, 0, 0]);
    glGetFloatv(GL_RED_SCALE, f.as_mut_ptr());
    glGetFloatv(GL_ALPHA_BIAS, f.as_mut_ptr().add(1));
    glGetFloatv(GL_DEPTH_SCALE, f.as_mut_ptr().add(2));
    check!(c, f[0] == 1.0 && f[1] == 0.0 && f[2] == 1.0);
    glGetIntegerv(GL_MAX_PIXEL_MAP_TABLE, v.as_mut_ptr());
    glGetIntegerv(GL_PIXEL_MAP_I_TO_I_SIZE, v.as_mut_ptr().add(1));
    glGetIntegerv(GL_PIXEL_MAP_A_TO_A_SIZE, v.as_mut_ptr().add(2));
    check!(c, v[0] == 256 && v[1] == 1 && v[2] == 1);
    mf[0] = -1.0;
    glGetPixelMapfv(GL_PIXEL_MAP_R_TO_R, mf.as_mut_ptr());
    check!(c, mf[0] == 0.0);

    // Setting and querying, errors
    glPixelTransferf(GL_GREEN_BIAS, 0.25);
    glPixelTransferi(GL_INDEX_SHIFT, -2);
    glPixelTransferf(GL_INDEX_OFFSET, 3.0);
    glPixelTransferi(GL_MAP_STENCIL, GL_TRUE as i32);
    glGetFloatv(GL_GREEN_BIAS, f.as_mut_ptr());
    glGetIntegerv(GL_INDEX_SHIFT, v.as_mut_ptr());
    glGetIntegerv(GL_INDEX_OFFSET, v.as_mut_ptr().add(1));
    glGetIntegerv(GL_MAP_STENCIL, v.as_mut_ptr().add(2));
    check!(c, f[0] == 0.25 && v[0] == -2 && v[1] == 3 && v[2] == 1);
    glPixelTransferf(GL_ZOOM_X, 1.0);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glBegin(GL_POINTS);
    glPixelTransferf(GL_RED_SCALE, 2.0);
    glEnd();
    check!(c, glGetError() == GL_INVALID_OPERATION);

    // Pixel maps: unsigned integers normalized for the color tables, as they are for the index tables; colors clamped
    let ui = [0xFFFF_FFFFu32, 0];
    let us = [7u16, 1, 2, 65535];
    let fl = [2.0f32, 0.5, -1.0];
    glPixelMapuiv(GL_PIXEL_MAP_I_TO_R, 2, ui.as_ptr());
    glPixelMapusv(GL_PIXEL_MAP_S_TO_S, 4, us.as_ptr());
    glPixelMapfv(GL_PIXEL_MAP_G_TO_G, 3, fl.as_ptr()); // Color-to-color tables can have any size
    glGetIntegerv(GL_PIXEL_MAP_I_TO_R_SIZE, v.as_mut_ptr());
    glGetIntegerv(GL_PIXEL_MAP_S_TO_S_SIZE, v.as_mut_ptr().add(1));
    glGetIntegerv(GL_PIXEL_MAP_G_TO_G_SIZE, v.as_mut_ptr().add(2));
    check!(c, v[0] == 2 && v[1] == 4 && v[2] == 3 && glGetError() == GL_NO_ERROR);
    glGetPixelMapfv(GL_PIXEL_MAP_I_TO_R, mf.as_mut_ptr());
    check!(c, mf[0] == 1.0 && mf[1] == 0.0);
    glGetPixelMapfv(GL_PIXEL_MAP_S_TO_S, mf.as_mut_ptr());
    check!(c, mf[0] == 7.0 && mf[3] == 65535.0);
    glGetPixelMapfv(GL_PIXEL_MAP_G_TO_G, mf.as_mut_ptr());
    check!(c, mf[0] == 1.0 && mf[1] == 0.5 && mf[2] == 0.0);
    glGetPixelMapuiv(GL_PIXEL_MAP_G_TO_G, mui.as_mut_ptr());
    glGetPixelMapusv(GL_PIXEL_MAP_G_TO_G, mus.as_mut_ptr());
    check!(c, mui[0] == 0xFFFF_FFFF && mui[2] == 0 && mus[1] == 32768);
    glGetPixelMapuiv(GL_PIXEL_MAP_S_TO_S, mui.as_mut_ptr());
    glGetPixelMapusv(GL_PIXEL_MAP_S_TO_S, mus.as_mut_ptr());
    check!(c, mui[0] == 7 && mui[2] == 2 && mus[3] == 65535);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_G, 3, fl.as_ptr()); // Index tables: 2^n entries
    check!(c, glGetError() == GL_INVALID_VALUE);
    glPixelMapfv(GL_PIXEL_MAP_R_TO_R, 0, fl.as_ptr());
    check!(c, glGetError() == GL_INVALID_VALUE);
    glPixelMapfv(GL_PIXEL_MAP_R_TO_R, 257, fl.as_ptr());
    check!(c, glGetError() == GL_INVALID_VALUE);
    glPixelMapfv(GL_PIXEL_MAP_A_TO_A + 1, 1, fl.as_ptr());
    check!(c, glGetError() == GL_INVALID_ENUM);
    glGetPixelMapfv(GL_PIXEL_MAP_I_TO_R_SIZE, mf.as_mut_ptr());
    check!(c, glGetError() == GL_INVALID_ENUM);

    // Attribute groups: the transfer state is in GL_PIXEL_MODE_BIT, the maps in none
    glPushAttrib(GL_PIXEL_MODE_BIT);
    glPixelTransferf(GL_GREEN_BIAS, 0.5);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_R, 1, fl.as_ptr().add(1));
    glPopAttrib();
    glGetFloatv(GL_GREEN_BIAS, f.as_mut_ptr());
    glGetIntegerv(GL_PIXEL_MAP_I_TO_R_SIZE, v.as_mut_ptr());
    check!(c, f[0] == 0.25 && v[0] == 1);
    glPopAttrib(); // The transfer back to the defaults

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    window_projection();
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClearDepth(0.5);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glPixelZoom(8.0, 4.0);

    // Color images: scale and bias on the unclamped components (float 2.0 scaled by 0.25), then the color tables
    let red2 = [2.0f32, 0.0, 0.0, 1.0];
    let rgba = [255u8, 0, 0, 255, 0, 255, 0, 255];
    let invert = [1.0f32, 0.0];
    glPixelTransferf(GL_RED_SCALE, 0.25);
    glPixelTransferf(GL_GREEN_BIAS, 0.25);
    glRasterPos2i(10, 10);
    gl!(glDrawPixels(1, 1, GL_RGBA, GL_FLOAT, red2.as_ptr()));
    glPixelTransferf(GL_RED_SCALE, 1.0);
    glPixelTransferf(GL_GREEN_BIAS, 0.0);
    glPixelMapfv(GL_PIXEL_MAP_R_TO_R, 2, invert.as_ptr());
    glPixelMapfv(GL_PIXEL_MAP_G_TO_G, 2, invert.as_ptr());
    glPixelTransferi(GL_MAP_COLOR, GL_TRUE as i32); // B_TO_B and A_TO_A are still { 0 }
    glRasterPos2i(20, 10);
    gl!(glDrawPixels(2, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba.as_ptr()));
    glPixelTransferi(GL_MAP_COLOR, GL_FALSE as i32);
    check!(c, pixel_near(14, 12, 128, 64, 0, 255) && pixel_near(24, 12, 0, 255, 0, 0) && pixel_near(32, 12, 255, 0, 0, 0));

    // glReadPixels: transferred before the conversion (luminance = R + G + B afterwards)
    let mut rf = [-1.0f32; 4];
    let mut lum = 0u8;
    glPixelTransferf(GL_RED_SCALE, 0.5);
    glPixelTransferf(GL_BLUE_BIAS, 1.0);
    gl!(glReadPixels(32, 12, 1, 1, GL_RGBA, GL_FLOAT, rf.as_mut_ptr()));
    gl!(glReadPixels(32, 12, 1, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, &mut lum as *mut u8));
    glPixelTransferf(GL_RED_SCALE, 1.0);
    glPixelTransferf(GL_BLUE_BIAS, 0.0);
    check!(c, near(rf[0] as f64, 0.5) && rf[1] == 0.0 && rf[2] == 1.0 && rf[3] == 0.0 && lum == 255);

    // Color index images: shifted, offset and looked up in the I_TO_* tables (wrapping around), not scaled or biased
    let (i_r, i_g, i_b, i_a) = ([1.0f32, 0.0, 0.0, 1.0], [0.0f32, 1.0, 0.0, 1.0], [0.0f32, 0.0, 1.0, 1.0], [1.0f32; 4]);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_R, 4, i_r.as_ptr()); // 0 red, 1 green, 2 blue, 3 white
    glPixelMapfv(GL_PIXEL_MAP_I_TO_G, 4, i_g.as_ptr());
    glPixelMapfv(GL_PIXEL_MAP_I_TO_B, 4, i_b.as_ptr());
    glPixelMapfv(GL_PIXEL_MAP_I_TO_A, 4, i_a.as_ptr());
    let indices = [0u8, 2, 4, 7];
    glPixelTransferi(GL_INDEX_SHIFT, -1); // 0, 1, 2, 3
    glPixelTransferi(GL_INDEX_OFFSET, 1); // 1, 2, 3, 0
    glPixelTransferf(GL_RED_SCALE, 0.0);
    glRasterPos2i(10, 20);
    gl!(glDrawPixels(4, 1, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, indices.as_ptr()));
    glPixelTransferi(GL_INDEX_SHIFT, 0);
    glPixelTransferi(GL_INDEX_OFFSET, 0);
    let index_bits = [0x40u8]; // 0, 1
    glRasterPos2i(50, 20);
    gl!(glDrawPixels(2, 1, GL_COLOR_INDEX, GL_BITMAP, index_bits.as_ptr()));
    glPixelTransferf(GL_RED_SCALE, 1.0);
    check!(c, pixel_near(14, 22, 0, 255, 0, 255) && pixel_near(22, 22, 0, 0, 255, 255) && pixel_near(30, 22, 255, 255, 255, 255));
    check!(c, pixel_near(38, 22, 255, 0, 0, 255) && pixel_near(54, 22, 255, 0, 0, 255) && pixel_near(62, 22, 0, 255, 0, 255));

    // Depth images: scale and bias, also when read
    let depth = [0.5f32];
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glPixelTransferf(GL_DEPTH_SCALE, 0.5);
    glPixelTransferf(GL_DEPTH_BIAS, 0.5);
    glRasterPos2i(10, 30);
    gl!(glDrawPixels(1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, depth.as_ptr()));
    glPixelTransferf(GL_DEPTH_SCALE, 1.0);
    glPixelTransferf(GL_DEPTH_BIAS, 0.0);
    check!(c, depth_near(14, 32, 0.75));
    glPixelTransferf(GL_DEPTH_BIAS, -0.25);
    check!(c, depth_near(100, 100, 0.25)); // Cleared to 0.5
    glPixelTransferf(GL_DEPTH_BIAS, 0.0);

    // Stencil images: shift and offset, the S_TO_S table with GL_MAP_STENCIL; when read too
    let stencil = [3u8];
    let stencil_map = [5.0f32, 9.0];
    glPixelTransferi(GL_INDEX_SHIFT, 1);
    glPixelTransferi(GL_INDEX_OFFSET, 1);
    glRasterPos2i(10, 40);
    gl!(glDrawPixels(1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencil.as_ptr())); // 7
    glPixelTransferi(GL_INDEX_SHIFT, 0);
    glPixelTransferi(GL_INDEX_OFFSET, 0);
    glPixelMapfv(GL_PIXEL_MAP_S_TO_S, 2, stencil_map.as_ptr());
    glPixelTransferi(GL_MAP_STENCIL, GL_TRUE as i32);
    glRasterPos2i(20, 40);
    gl!(glDrawPixels(1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencil.as_ptr())); // 3 & 1 -> 9
    glPixelTransferi(GL_MAP_STENCIL, GL_FALSE as i32);
    check!(c, stencil_at(14, 42) == 7 && stencil_at(24, 42) == 9);
    glPixelTransferi(GL_INDEX_OFFSET, 100);
    check!(c, stencil_at(24, 42) == 109);
    glPixelTransferi(GL_INDEX_OFFSET, 0);

    // glCopyPixels: transferred once (green 64 biased by 0.25 is 128, not 192); depth and stencil too
    glPixelZoom(1.0, 1.0);
    glPixelTransferf(GL_GREEN_BIAS, 0.25);
    glRasterPos2i(100, 10);
    glCopyPixels(10, 10, 8, 4, GL_COLOR);
    glPixelTransferf(GL_GREEN_BIAS, 0.0);
    check!(c, pixel_near(104, 12, 128, 128, 0, 255));
    glPixelTransferf(GL_DEPTH_BIAS, 0.125);
    glPixelTransferi(GL_INDEX_OFFSET, 1);
    glRasterPos2i(110, 30);
    glCopyPixels(10, 30, 8, 4, GL_DEPTH);
    glRasterPos2i(110, 40);
    glCopyPixels(10, 40, 8, 4, GL_STENCIL);
    glPixelTransferf(GL_DEPTH_BIAS, 0.0);
    glPixelTransferi(GL_INDEX_OFFSET, 0);
    glDisable(GL_DEPTH_TEST);
    check!(c, depth_near(114, 32, 0.875) && stencil_at(114, 42) == 8);

    // Textures: loaded and copied with the transfer (copies once), glGetTexImage without it
    let mut tex = 0u32;
    let mut texels = [0u8; 8 * 8 * 4];
    let mut tex_indices = [0u8; 64];
    for (i, t) in tex_indices.iter_mut().enumerate() {
        *t = (i & 3) as u8;
    }
    glGenTextures(1, &mut tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, tex_indices.as_ptr()));
    glPixelTransferf(GL_RED_SCALE, 0.0);
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.as_mut_ptr()));
    check!(c, texels[0] == 255 && texels[1] == 0 && texels[5] == 255 && texels[10] == 255 && texels[12] == 255 && texels[15] == 255);
    gl!(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba.as_ptr())); // Red scaled to 0
    glPixelTransferf(GL_RED_SCALE, 1.0);
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.as_mut_ptr()));
    check!(c, texels[0] == 0 && texels[3] == 255 && texels[5] == 255);
    glPixelTransferf(GL_GREEN_BIAS, 0.25);
    gl!(glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 12, 12, 1, 1)); // (128, 64, 0)
    glPixelTransferf(GL_GREEN_BIAS, 0.0);
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.as_mut_ptr()));
    check!(c, (texels[0] as i32 - 128).abs() <= 2 && (texels[1] as i32 - 128).abs() <= 2);
    let tex_bits = [0x40u8; 8]; // Columns 0, 1: index 0, 1
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_COLOR_INDEX, GL_BITMAP, tex_bits.as_ptr()));
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.as_mut_ptr()));
    check!(c, texels[0] == 255 && texels[1] == 0 && texels[4] == 0 && texels[5] == 255 && texels[8] == 255);
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, texels.as_mut_ptr()));
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_BITMAP, tex_bits.as_ptr()));
    check!(c, glGetError() == GL_INVALID_ENUM);

    // Display lists: glPixelTransfer and glPixelMap are recorded (the values copied), bitmap texture images too
    let mut list_map = [0.0f32, 1.0];
    let mut list_bits = [0x80u8; 8]; // Column 0: index 1
    let list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glPixelTransferf(GL_BLUE_SCALE, 0.5);
    glPixelMapfv(GL_PIXEL_MAP_B_TO_B, 2, list_map.as_ptr());
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_COLOR_INDEX, GL_BITMAP, list_bits.as_ptr()));
    glEndList();
    list_map[1] = 0.5;
    list_bits.fill(0);
    glGetFloatv(GL_BLUE_SCALE, f.as_mut_ptr());
    check!(c, f[0] == 1.0);
    glCallList(list);
    glDeleteLists(list, 1);
    glGetFloatv(GL_BLUE_SCALE, f.as_mut_ptr());
    glGetPixelMapfv(GL_PIXEL_MAP_B_TO_B, mf.as_mut_ptr());
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.as_mut_ptr()));
    check!(c, f[0] == 0.5 && mf[1] == 1.0 && texels[0] == 0 && texels[1] == 255 && texels[4] == 255 && texels[5] == 0);
    glDeleteTextures(1, &tex);
    glPopAttrib();
    glGetFloatv(GL_BLUE_SCALE, f.as_mut_ptr());
    check!(c, f[0] == 1.0 && glGetError() == GL_NO_ERROR);
}

/// Display lists: names, compile modes, state and errors at execution time, commands executed
/// immediately while compiling, nesting (late binding, self calls up to the nesting limit),
/// glCallLists with glListBase, client data (vertex arrays, pixels, control points) copied at
/// compile time, drawing from lists. Leaves a frame in progress.
unsafe fn test_display_lists(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Defaults and names
    let mut v = [-1i32; 4];
    glGetIntegerv(GL_LIST_BASE, &mut v[0]);
    glGetIntegerv(GL_LIST_INDEX, &mut v[1]);
    glGetIntegerv(GL_LIST_MODE, &mut v[2]);
    glGetIntegerv(GL_MAX_LIST_NESTING, &mut v[3]);
    check!(c, v[0] == 0 && v[1] == 0 && v[2] == 0 && v[3] >= 64);
    let base = glGenLists(3);
    check!(c, base != 0 && glIsList(base) != 0 && glIsList(base + 2) != 0 && glIsList(base + 3) == 0 && glIsList(0) == 0);
    check!(c, glGenLists(0) == 0 && glGetError() == GL_NO_ERROR);
    check!(c, glGenLists(-1) == 0 && glGetError() == GL_INVALID_VALUE);
    let more = glGenLists(2);
    check!(c, more >= base + 3);
    glDeleteLists(base + 1, 1); // A gap of one name, then two lists
    check!(c, glIsList(base + 1) == 0 && glIsList(base) != 0);
    check!(c, glGenLists(1) == base + 1); // The gap is reused
    glDeleteLists(more, 2);
    glDeleteLists(0, -1);
    check!(c, glGetError() == GL_INVALID_VALUE);

    // Errors of glNewList / glEndList
    glNewList(0, GL_COMPILE);
    check!(c, glGetError() == GL_INVALID_VALUE);
    gl!(glNewList(base, 0x1234));
    check!(c, glGetError() == GL_INVALID_ENUM);
    glEndList();
    check!(c, glGetError() == GL_INVALID_OPERATION);

    // GL_COMPILE: recorded only. Queries, client state and pixel store run immediately; errors come at execution
    glNewList(base, GL_COMPILE);
    glGetIntegerv(GL_LIST_INDEX, &mut v[0]);
    glGetIntegerv(GL_LIST_MODE, &mut v[1]);
    check!(c, v[0] == base as i32 && v[1] == GL_COMPILE as i32);
    glNewList(base + 1, GL_COMPILE); // Nested glNewList: error, compiling goes on
    check!(c, glGetError() == GL_INVALID_OPERATION);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glLineWidth(3.0);
    glTranslatef(1.0, 2.0, 3.0);
    gl!(glMatrixMode(0x1234)); // Invalid: recorded, fails when the list runs
    glEnableClientState(GL_NORMAL_ARRAY); // Client state: executed now
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glEndList();
    check!(c, glGetError() == GL_NO_ERROR);
    check!(c, glIsEnabled(GL_BLEND) == 0 && modelview_translation(0.0, 0.0, 0.0));
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &mut v[0]);
    check!(c, glIsEnabled(GL_NORMAL_ARRAY) != 0 && v[0] == 2);
    glDisableClientState(GL_NORMAL_ARRAY);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGetIntegerv(GL_LIST_INDEX, &mut v[0]);
    glGetIntegerv(GL_LIST_MODE, &mut v[1]);
    check!(c, v[0] == 0 && v[1] == 0);
    glCallList(base);
    check!(c, glGetError() == GL_INVALID_ENUM);
    let mut f = [0f32; 4];
    glGetFloatv(GL_LINE_WIDTH, f.as_mut_ptr());
    glGetIntegerv(GL_BLEND_SRC, &mut v[0]);
    glGetIntegerv(GL_BLEND_DST, &mut v[1]);
    check!(c, glIsEnabled(GL_BLEND) != 0 && f[0] == 3.0 && v[0] == GL_SRC_ALPHA as i32 && v[1] == GL_ONE as i32);
    check!(c, modelview_translation(1.0, 2.0, 3.0));
    glCallList(base); // Again: the translation adds up
    glGetError();
    check!(c, modelview_translation(2.0, 4.0, 6.0));
    glDisable(GL_BLEND);
    glLineWidth(1.0);
    glLoadIdentity();

    // GL_COMPILE_AND_EXECUTE: executed while compiling too; glEndList replaces the old contents
    glNewList(base, GL_COMPILE_AND_EXECUTE);
    glLineWidth(5.0);
    glGetFloatv(GL_LINE_WIDTH, f.as_mut_ptr());
    check!(c, f[0] == 5.0);
    glScalef(2.0, 2.0, 2.0);
    glEndList();
    check!(c, glGetError() == GL_NO_ERROR);
    glLineWidth(1.0);
    glLoadIdentity();
    glCallList(base);
    glGetFloatv(GL_LINE_WIDTH, f.as_mut_ptr());
    let mut m = [0f32; 16];
    glGetFloatv(GL_MODELVIEW_MATRIX, m.as_mut_ptr());
    check!(c, f[0] == 5.0 && m[0] == 2.0 && m[12] == 0.0); // The new contents only (no translation)
    glLineWidth(1.0);

    // Nesting: base + 1 calls base + 2, which is defined later (lists are looked up when executed)
    glNewList(base + 1, GL_COMPILE);
    glTranslatef(10.0, 0.0, 0.0);
    glCallList(base + 2);
    glEndList();
    glNewList(base + 2, GL_COMPILE);
    glTranslatef(0.0, 20.0, 0.0);
    glEndList();
    glLoadIdentity();
    glCallList(base + 1);
    check!(c, modelview_translation(10.0, 20.0, 0.0));

    // A list calling itself stops at the nesting limit
    glNewList(base + 2, GL_COMPILE);
    glTranslatef(1.0, 0.0, 0.0);
    glCallList(base + 2);
    glEndList();
    glLoadIdentity();
    glCallList(base + 2);
    glGetIntegerv(GL_MAX_LIST_NESTING, &mut v[0]);
    check!(c, modelview_translation(v[0] as f32, 0.0, 0.0));

    // While a list is compiled with GL_COMPILE_AND_EXECUTE, calling it runs its old contents
    glNewList(base + 1, GL_COMPILE); // base + 1: translate by 100 in y
    glTranslatef(0.0, 100.0, 0.0);
    glEndList();
    glLoadIdentity();
    glNewList(base + 1, GL_COMPILE_AND_EXECUTE);
    glCallList(base + 1); // Old contents: y + 100
    glTranslatef(1.0, 0.0, 0.0);
    glEndList();
    check!(c, modelview_translation(1.0, 100.0, 0.0));
    glLoadIdentity();
    glCallList(base + 1); // New contents: the call (now of the new list itself)...
    glGetFloatv(GL_MODELVIEW_MATRIX, m.as_mut_ptr());
    check!(c, m[12] == v[0] as f32); // ... recurses to the limit: x + 1 per level

    // glCallLists: types, list base (also from a list), errors
    let lists = glGenLists(4);
    for i in 0..4u32 {
        glNewList(lists + i, GL_COMPILE);
        glTranslatef((1 << i) as f32, 0.0, 0.0); // 1, 2, 4, 8
        glEndList();
    }
    glListBase(lists);
    glGetIntegerv(GL_LIST_BASE, &mut v[0]);
    check!(c, v[0] == lists as i32);
    let ub = [0u8, 2, 3];
    glLoadIdentity();
    gl!(glCallLists(3, GL_UNSIGNED_BYTE, ub.as_ptr()));
    check!(c, modelview_translation(13.0, 0.0, 0.0));
    let two_bytes = [0u8, 1, 0, 2]; // GL_2_BYTES: big-endian 1 and 2
    let floats = [3.0f32, 0.0];
    glLoadIdentity();
    gl!(glCallLists(2, GL_2_BYTES, two_bytes.as_ptr()));
    gl!(glCallLists(2, GL_FLOAT, floats.as_ptr()));
    check!(c, modelview_translation(15.0, 0.0, 0.0));
    gl!(glCallLists(1, 0x1234, ub.as_ptr()));
    check!(c, glGetError() == GL_INVALID_ENUM);
    gl!(glCallLists(-1, GL_UNSIGNED_BYTE, ub.as_ptr()));
    check!(c, glGetError() == GL_INVALID_VALUE);

    // In a list: the names are copied at compile time, the base is the one when the list runs
    let mut names = [1u8, 2];
    glNewList(base, GL_COMPILE);
    glListBase(lists + 1); // Recorded
    gl!(glCallLists(2, GL_UNSIGNED_BYTE, names.as_ptr()));
    glEndList();
    names = [0, 0];
    glGetIntegerv(GL_LIST_BASE, &mut v[0]);
    check!(c, v[0] == lists as i32); // glListBase was not executed
    glLoadIdentity();
    glCallList(base); // lists + 2, lists + 3
    check!(c, modelview_translation(12.0, 0.0, 0.0));
    glPushAttrib(GL_LIST_BIT);
    glListBase(7);
    glPopAttrib();
    glGetIntegerv(GL_LIST_BASE, &mut v[0]);
    check!(c, v[0] == (lists + 1) as i32);
    glListBase(0);
    glDeleteLists(lists, 4);
    check!(c, glIsList(lists) == 0 && glIsList(lists + 3) == 0);
    glLoadIdentity();
    glCallList(lists); // Deleted: nothing happens
    check!(c, modelview_translation(0.0, 0.0, 0.0) && glGetError() == GL_NO_ERROR);

    // Lighting: a light position is transformed by the modelview when the list runs
    let light_pos = [1.0f32, 0.0, 0.0, 0.0];
    glNewList(base, GL_COMPILE);
    glLightfv(GL_LIGHT1, GL_POSITION, light_pos.as_ptr());
    glMaterialf(GL_FRONT, GL_SHININESS, 17.0);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE as i32);
    glEndList();
    glLoadIdentity();
    glRotatef(90.0, 0.0, 0.0, 1.0); // x -> y
    glCallList(base);
    glLoadIdentity();
    glGetLightfv(GL_LIGHT1, GL_POSITION, f.as_mut_ptr());
    let mut shininess = 0f32;
    glGetMaterialfv(GL_FRONT, GL_SHININESS, &mut shininess);
    glGetIntegerv(GL_LIGHT_MODEL_TWO_SIDE, &mut v[0]);
    check!(c, near(f[0] as f64, 0.0) && near(f[1] as f64, 1.0) && shininess == 17.0 && v[0] == GL_TRUE as i32);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE as i32);

    // Evaluator control points are copied at compile time
    let mut points = [[1.0f32, 2.0, 3.0], [4.0, 5.0, 6.0]];
    glNewList(base, GL_COMPILE);
    glMap1f(GL_MAP1_VERTEX_3, 0.0, 1.0, 3, 2, points[0].as_ptr());
    glEndList();
    points[1][2] = 99.0;
    glCallList(base);
    let mut coeff = [0f32; 6];
    glGetMapfv(GL_MAP1_VERTEX_3, GL_COEFF, coeff.as_mut_ptr());
    check!(c, coeff[0] == 1.0 && coeff[5] == 6.0);

    // Drawing. Begin/End and glRect in a list, nothing is drawn while compiling with GL_COMPILE
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3ub(0, 0, 255);
    glNewList(base, GL_COMPILE);
    glColor3ub(255, 0, 0);
    glBegin(GL_QUADS);
    glVertex2i(0, 0);
    glVertex2i(8, 0);
    glVertex2i(8, 8);
    glVertex2i(0, 8);
    glEnd();
    glColor3ub(0, 255, 0);
    glRecti(8, 0, 16, 8);
    glEndList();
    glGetFloatv(GL_CURRENT_COLOR, f.as_mut_ptr()); // Not changed by compiling
    check!(c, f[0] == 0.0 && f[2] == 1.0);
    check!(c, pixel_is(4, 4, 0, 0, 0));
    glCallList(base);
    check!(c, pixel_is(4, 4, 255, 0, 0) && pixel_is(12, 4, 0, 255, 0));
    glPushMatrix();
    glTranslatef(100.0, 0.0, 0.0); // The same list elsewhere
    glCallList(base);
    glPopMatrix();
    check!(c, pixel_is(104, 4, 255, 0, 0) && pixel_is(112, 4, 0, 255, 0));

    // Vertex arrays are dereferenced at compile time: changing them afterwards does not change the list
    let mut quad = [[20i16, 0], [28, 0], [28, 8], [20, 8]];
    let mut colors = [[255u8, 255, 0, 255]; 4];
    let indices = [0u8, 1, 2, 0, 2, 3];
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    gl!(glVertexPointer(2, GL_SHORT, 0, quad.as_ptr()));
    gl!(glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors.as_ptr()));
    glNewList(base + 1, GL_COMPILE);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4); // Yellow at x = 20..28
    glTranslatef(10.0, 0.0, 0.0);
    gl!(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, indices.as_ptr())); // x = 30..38
    glTranslatef(10.0, 0.0, 0.0);
    glBegin(GL_QUADS); // x = 40..48
    for i in 0..4 {
        glArrayElement(i);
    }
    glEnd();
    glEndList();
    for i in 0..4 {
        colors[i][0] = 0;
        quad[i][1] += 100;
    }
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    check!(c, pixel_is(24, 4, 0, 0, 0));
    glLoadIdentity();
    glCallList(base + 1);
    glLoadIdentity();
    check!(c, pixel_is(24, 4, 255, 255, 0) && pixel_is(34, 4, 255, 255, 0) && pixel_is(44, 4, 255, 255, 0));
    glGetFloatv(GL_CURRENT_COLOR, f.as_mut_ptr());
    check!(c, f[0] == 1.0 && f[1] == 1.0 && f[2] == 0.0); // glArrayElement in a list sets the current color

    // Texture images are copied at compile time, unpacked with the pixel store state of then
    let mut tex = 0u32;
    glGenTextures(1, &mut tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    let mut image = [[[0u8; 4]; 3]; 2]; // 3 pixels per row, 2 of them used (row length 3, skip 1 pixel)
    for y in 0..2 {
        for x in 0..3 {
            image[y][x] = [(10 * y + x) as u8, 1, 2, 3];
        }
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 3);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
    glNewList(base + 2, GL_COMPILE);
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.as_ptr()));
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    gl!(glTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_RGBA, 64, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, null::<u8>())); // Executed now
    glEndList();
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &mut v[0]);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &mut v[1]);
    check!(c, v[0] == 64 && v[1] == 0);
    image = [[[0u8; 4]; 3]; 2];
    glCallList(base + 2);
    let mut texels = [[[0xAAu8; 4]; 2]; 2];
    gl!(glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.as_mut_ptr()));
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &mut v[0]);
    check!(c, texels[0][0][0] == 1 && texels[0][1][0] == 2 && texels[1][0][0] == 11 && texels[1][1][0] == 12 && texels[1][1][3] == 3 && v[0] == GL_NEAREST as i32);
    glDeleteTextures(1, &tex);
    glDeleteLists(base, 3);
    check!(c, glGetError() == GL_NO_ERROR);
    glPopAttrib();
}

/// Four white 60x60 squares with different vertex calls, pixel coordinates y down.
unsafe fn draw_squares() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 240.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0, -35.0, 0.0); // Row 1 at y = 15..75
    glColor3ub(255, 255, 255);
    glRectf(30.0, 50.0, 90.0, 110.0);
    glRecti(125, 50, 185, 110);
    static CORNERS: [[i16; 2]; 4] = [[215, 50], [275, 50], [275, 110], [215, 110]];
    glBegin(GL_QUADS);
    for corner in &CORNERS {
        glVertex2sv(corner.as_ptr());
    }
    glEnd();
    glBegin(GL_QUADS); // Homogeneous: (2x, 2y, 0, 2)
    glVertex4f(620.0, 100.0, 0.0, 2.0);
    glVertex4f(740.0, 100.0, 0.0, 2.0);
    glVertex4f(740.0, 220.0, 0.0, 2.0);
    glVertex4f(620.0, 220.0, 0.0, 2.0);
    glEnd();
}

/// Second row: four ORANGE (1, 0.5, 0) 60x60 squares from arrays of different types, colors in the
/// array's own type. A wrong normalization shows as a different color (white/yellow), a wrong
/// position type as a misplaced square.
unsafe fn draw_array_squares() {
    glLoadIdentity();
    glTranslatef(0.0, -80.0, 0.0); // Row 2 at y = 90..150
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    // GL_BYTE positions (unit square, scaled by the modelview) and GL_BYTE colors: 127 = 1.0, 63 = 0.5
    static BYTE_POS: [i8; 8] = [0, 0, 1, 0, 1, 1, 0, 1];
    static BYTE_COLOR: [i8; 16] = [127, 63, -128, 127, 127, 63, -128, 127, 127, 63, -128, 127, 127, 63, -128, 127];
    glPushMatrix();
    glTranslatef(30.0, 170.0, 0.0);
    glScalef(60.0, 60.0, 1.0);
    gl!(glVertexPointer(2, GL_BYTE, 0, BYTE_POS.as_ptr()));
    gl!(glColorPointer(4, GL_BYTE, 0, BYTE_COLOR.as_ptr()));
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glPopMatrix();

    // GL_SHORT positions with a stride, GL_UNSIGNED_SHORT colors (size 3)
    static SHORT_POS: [[i16; 3]; 4] = [[125, 170, 99], [185, 170, 99], [185, 230, 99], [125, 230, 99]];
    static USHORT_COLOR: [u16; 12] = [65535, 32768, 0, 65535, 32768, 0, 65535, 32768, 0, 65535, 32768, 0];
    gl!(glVertexPointer(2, GL_SHORT, 3 * 2, SHORT_POS.as_ptr()));
    gl!(glColorPointer(3, GL_UNSIGNED_SHORT, 0, USHORT_COLOR.as_ptr()));
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // ES: GL_FIXED positions and colors, matrices set with the x API
    static FIXED_POS: [i32; 8] = [0, 0, 0x10000, 0, 0x10000, 0x10000, 0, 0x10000];
    static FIXED_COLOR: [i32; 16] = [0x10000, 0x8000, 0, 0x10000, 0x10000, 0x8000, 0, 0x10000, 0x10000, 0x8000, 0, 0x10000, 0x10000, 0x8000, 0, 0x10000];
    glPushMatrix();
    glTranslatex(215 << 16, 170 << 16, 0);
    glScalex(60 << 16, 60 << 16, 0x10000);
    gl!(glVertexPointer(2, GL_FIXED, 0, FIXED_POS.as_ptr()));
    gl!(glColorPointer(4, GL_FIXED, 0, FIXED_COLOR.as_ptr()));
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glPopMatrix();
    glDisableClientState(GL_COLOR_ARRAY);

    // glInterleavedArrays GL_C4UB_V2F, vertices sent with glArrayElement inside glBegin/glEnd
    #[repr(C)]
    struct Interleaved {
        color: [u8; 4],
        pos: [f32; 2],
    }
    static INTERLEAVED: [Interleaved; 4] = [
        Interleaved { color: [255, 128, 0, 255], pos: [310.0, 170.0] },
        Interleaved { color: [255, 128, 0, 255], pos: [370.0, 170.0] },
        Interleaved { color: [255, 128, 0, 255], pos: [370.0, 230.0] },
        Interleaved { color: [255, 128, 0, 255], pos: [310.0, 230.0] },
    ];
    gl!(glInterleavedArrays(GL_C4UB_V2F, 0, INTERLEAVED.as_ptr()));
    glBegin(GL_QUADS);
    for i in 0..4 {
        glArrayElement(i);
    }
    glEnd();
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glColor3ub(255, 255, 255);
}

/// Third row (y = 165..225): four CYAN squares from buffer objects.
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct VboVertex {
    pos: [i16; 2],
    color: [u8; 4],
}

#[derive(Default)]
struct Buffers {
    vbo: u32,
    ibo: u32,
    vbo_colors: u32,
}

unsafe fn create_buffers() -> Buffers {
    let mut b = Buffers::default();
    // Interleaved positions and colors of all four squares, 4 corners each
    let mut vertices = [VboVertex::default(); 16];
    const COLUMNS: [i16; 4] = [30, 125, 215, 310]; // Same as the rows above
    for (q, &x) in COLUMNS.iter().enumerate() {
        let y = 165i16;
        let corners = [[x, y], [x + 60, y], [x + 60, y + 60], [x, y + 60]];
        for (k, corner) in corners.iter().enumerate() {
            vertices[q * 4 + k] = VboVertex { pos: *corner, color: [0, 230, 230, 255] };
        }
    }
    glGenBuffers(1, &mut b.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
    gl!(glBufferData(GL_ARRAY_BUFFER, std::mem::size_of_val(&vertices), vertices.as_ptr(), GL_STATIC_DRAW));

    // Indices for square 2 (two triangles), at an offset of 4 bytes in the element buffer
    let indices = [0xDEADu16, 0xBEEF, 4, 5, 6, 4, 6, 7];
    glGenBuffers(1, &mut b.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b.ibo);
    gl!(glBufferData(GL_ELEMENT_ARRAY_BUFFER, std::mem::size_of_val(&indices), indices.as_ptr(), GL_STATIC_DRAW));

    // Separate color buffer, rewritten every frame with glBufferSubData (square 3)
    glGenBuffers(1, &mut b.vbo_colors);
    glBindBuffer(GL_ARRAY_BUFFER, b.vbo_colors);
    gl!(glBufferData(GL_ARRAY_BUFFER, 16 * 4, null::<u8>(), GL_DYNAMIC_DRAW));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    b
}

unsafe fn draw_buffer_squares(b: &Buffers) {
    let stride = std::mem::size_of::<VboVertex>();
    glLoadIdentity();
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    // 1: glDrawArrays from the interleaved VBO
    glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
    gl!(glVertexPointer(2, GL_SHORT, stride, null::<c_void>()));
    gl!(glColorPointer(4, GL_UNSIGNED_BYTE, stride, 4usize as *const c_void));
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // 2: glDrawElements with indices from the element buffer, at offset 4
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b.ibo);
    gl!(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 4usize as *const c_void));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // 3: positions from the VBO, colors from a buffer updated by glBufferSubData: cyan
    let colors: Vec<u8> = (0..16).flat_map(|_| [0u8, 230, 230, 255]).collect();
    glBindBuffer(GL_ARRAY_BUFFER, b.vbo_colors);
    gl!(glBufferSubData(GL_ARRAY_BUFFER, 0, colors.len(), colors.as_ptr()));
    gl!(glColorPointer(4, GL_UNSIGNED_BYTE, 0, null::<c_void>()));
    glDrawArrays(GL_TRIANGLE_FAN, 8, 4);

    // 4: VBO positions mixed with a client-side color array (pointer set with no buffer bound)
    let mut client_colors = [0u8; 16 * 4];
    for i in 12..16 {
        client_colors[i * 4..i * 4 + 4].copy_from_slice(&[0, 230, 230, 255]);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl!(glColorPointer(4, GL_UNSIGNED_BYTE, 0, client_colors.as_ptr()));
    glDrawArrays(GL_TRIANGLE_FAN, 12, 4);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glColor3ub(255, 255, 255);
}

/// Texture 0 of each target is a texture of its own (GL 1.0 style code without glBindTexture).
unsafe fn test_default_textures(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    let mut v = -1i32;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &mut v);
    check!(c, v == 0);
    check!(c, glIsTexture(0) == 0);

    // Defaults of the unloaded texture, then a 2x2 image: red, green / blue, white
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &mut v);
    check!(c, v == GL_NEAREST_MIPMAP_LINEAR as i32);
    static RGBA: [u8; 16] = [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255];
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, RGBA.as_ptr()));
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    check!(c, glGetError() == GL_NO_ERROR);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &mut v);
    check!(c, v == 2);

    // The 1D default is separate from the 2D one
    static LUM: [u8; 4] = [10, 20, 30, 40];
    gl!(glTexImage1D(GL_TEXTURE_1D, 0, GL_LUMINANCE, 4, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, LUM.as_ptr()));
    glGetTexLevelParameteriv(GL_TEXTURE_1D, 0, GL_TEXTURE_WIDTH, &mut v);
    check!(c, v == 4);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &mut v);
    check!(c, v == 2);
    glGetTexParameteriv(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, &mut v);
    check!(c, v == GL_NEAREST_MIPMAP_LINEAR as i32);

    // Deleting a bound texture binds texture 0 again, which keeps its image and parameters
    let mut tex = 0u32;
    glGenTextures(1, &mut tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &mut v);
    check!(c, v == 0);
    glDeleteTextures(1, &tex);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &mut v);
    check!(c, v == 0);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &mut v);
    check!(c, v == 2);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &mut v);
    check!(c, v == GL_NEAREST as i32);

    // GL_TEXTURE_BIT saves and restores its parameters
    glPushAttrib(GL_TEXTURE_BIT);
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP));
    glPopAttrib();
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &mut v);
    check!(c, v == GL_REPEAT as i32);

    // Drawn with it: one 4x4 block per texel
    gl!(glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE));
    glEnable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0, 0.0);
    glVertex2i(300, 100);
    glTexCoord2f(1.0, 0.0);
    glVertex2i(308, 100);
    glTexCoord2f(1.0, 1.0);
    glVertex2i(308, 108);
    glTexCoord2f(0.0, 1.0);
    glVertex2i(300, 108);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    let mut p = [0u8; 16];
    for (i, (x, y)) in [(301, 101), (306, 101), (301, 106), (306, 106)].iter().enumerate() {
        gl!(glReadPixels(*x, *y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr().add(4 * i)));
    }
    check!(c, p[0] == 255 && p[1] == 0 && p[2] == 0);
    check!(c, p[4] == 0 && p[5] == 255 && p[6] == 0);
    check!(c, p[8] == 0 && p[9] == 0 && p[10] == 255);
    check!(c, p[12] == 255 && p[13] == 255 && p[14] == 255);

    // Copies into it
    gl!(glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 300, 100, 8, 8, 0));
    check!(c, glGetError() == GL_NO_ERROR);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &mut v);
    check!(c, v == 8);
    glPopAttrib();
}

fn nearf(a: f32, b: f32) -> bool {
    (a - b).abs() < 1e-3
}

/// Values of a feedback vertex in window coordinates.
fn fb_vertex(f: &[f32], x: f32, y: f32, z: f32) -> bool {
    nearf(f[0], x) && nearf(f[1], y) && nearf(f[2], z)
}

fn hit_depth(z: u32, expected: f64) -> bool {
    (z as f64 / 4294967295.0 - expected).abs() < 1e-4
}

/// Feedback (tokens in window coordinates, clipped, culled, polygon modes, raster tokens, overflow)
/// and selection (hit records, name stack); nothing is drawn meanwhile.
unsafe fn test_feedback(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    window_projection();
    let mut v = -1i32;
    glGetIntegerv(GL_RENDER_MODE, &mut v);
    check!(c, v == GL_RENDER as i32);
    glGetIntegerv(GL_FEEDBACK_BUFFER_TYPE, &mut v);
    check!(c, v == GL_2D as i32);
    glGetIntegerv(GL_MAX_NAME_STACK_DEPTH, &mut v);
    check!(c, v >= 64);

    // Errors: no buffer yet, bad arguments
    check!(c, glRenderMode(GL_FEEDBACK) == 0 && glGetError() == GL_INVALID_OPERATION);
    check!(c, glRenderMode(GL_SELECT) == 0 && glGetError() == GL_INVALID_OPERATION);
    check!(c, glRenderMode(0x1234) == 0 && glGetError() == GL_INVALID_ENUM);
    static mut FB: [f32; 256] = [0.0; 256];
    let fb: &mut [f32; 256] = &mut *std::ptr::addr_of_mut!(FB);
    let fbp = fb.as_mut_ptr();
    glFeedbackBuffer(-1, GL_3D, fbp);
    check!(c, glGetError() == GL_INVALID_VALUE);
    glFeedbackBuffer(256, 0x1234, fbp);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glPopName(); // Ignored outside selection mode
    glLoadName(1);
    check!(c, glGetError() == GL_NO_ERROR);
    let tok = |fb: &[f32; 256], i: usize, t: u32| fb[i] == t as f32;

    // GL_3D: a point (and one outside), a pass-through, a line clipped at the window edge, a triangle; z = 0 is window depth 0.5
    glFeedbackBuffer(256, GL_3D, fbp);
    let mut ptr: *mut c_void = null_mut();
    glGetPointerv(GL_FEEDBACK_BUFFER_POINTER, &mut ptr);
    glGetIntegerv(GL_FEEDBACK_BUFFER_SIZE, &mut v);
    check!(c, ptr as *mut f32 == fbp && v == 256);
    check!(c, glRenderMode(GL_FEEDBACK) == 0);
    glGetIntegerv(GL_RENDER_MODE, &mut v);
    check!(c, v == GL_FEEDBACK as i32);
    glFeedbackBuffer(256, GL_3D, fbp);
    check!(c, glGetError() == GL_INVALID_OPERATION);
    glBegin(GL_POINTS);
    glVertex2f(10.0, 20.0);
    glVertex2f(-5.0, 20.0);
    glEnd();
    glPassThrough(7.0);
    glBegin(GL_LINES);
    glVertex2f(100.0, 50.0);
    glVertex2f(500.0, 50.0);
    glEnd();
    glBegin(GL_TRIANGLES);
    glVertex2f(100.0, 100.0);
    glVertex2f(200.0, 100.0);
    glVertex2f(150.0, 200.0);
    glEnd();
    check!(c, glRenderMode(GL_RENDER) == 24);
    check!(c, tok(fb, 0, GL_POINT_TOKEN) && fb_vertex(&fb[1..], 10.0, 20.0, 0.5));
    check!(c, tok(fb, 4, GL_PASS_THROUGH_TOKEN) && fb[5] == 7.0);
    check!(c, tok(fb, 6, GL_LINE_RESET_TOKEN) && fb_vertex(&fb[7..], 100.0, 50.0, 0.5) && fb_vertex(&fb[10..], 400.0, 50.0, 0.5));
    check!(
        c,
        tok(fb, 13, GL_POLYGON_TOKEN) && fb[14] == 3.0 && fb_vertex(&fb[15..], 100.0, 100.0, 0.5) && fb_vertex(&fb[18..], 200.0, 100.0, 0.5) && fb_vertex(&fb[21..], 150.0, 200.0, 0.5)
    );

    // GL_2D: line strips and loops reset the stipple once, separate lines each time; a polygon stays
    // one; a quad clipped at x = 0; a culled triangle; outlines and vertices of polygons (an edge flag off)
    glFeedbackBuffer(256, GL_2D, fbp);
    glRenderMode(GL_FEEDBACK);
    glBegin(GL_LINE_LOOP);
    glVertex2i(10, 10);
    glVertex2i(20, 10);
    glVertex2i(20, 20);
    glEnd();
    glBegin(GL_LINES);
    glVertex2i(10, 10);
    glVertex2i(20, 10);
    glVertex2i(30, 10);
    glVertex2i(40, 10);
    glEnd();
    let mut n = glRenderMode(GL_RENDER);
    check!(c, n == 5 * 5);
    check!(c, tok(fb, 0, GL_LINE_RESET_TOKEN) && tok(fb, 5, GL_LINE_TOKEN) && tok(fb, 10, GL_LINE_TOKEN));
    check!(c, nearf(fb[11], 20.0) && nearf(fb[12], 20.0) && nearf(fb[13], 10.0) && nearf(fb[14], 10.0)); // Closing segment
    check!(c, tok(fb, 15, GL_LINE_RESET_TOKEN) && tok(fb, 20, GL_LINE_RESET_TOKEN) && nearf(fb[21], 30.0));
    glRenderMode(GL_FEEDBACK);
    glBegin(GL_POLYGON);
    glVertex2i(10, 10);
    glVertex2i(30, 10);
    glVertex2i(40, 20);
    glVertex2i(30, 30);
    glVertex2i(10, 30);
    glEnd();
    glRecti(-100, 50, 100, 60);
    glEnable(GL_CULL_FACE);
    glBegin(GL_TRIANGLES);
    glVertex2i(10, 10);
    glVertex2i(10, 20);
    glVertex2i(20, 10); // Clockwise: back facing
    glEnd();
    glDisable(GL_CULL_FACE);
    n = glRenderMode(GL_RENDER);
    check!(c, n == 12 + 10);
    check!(c, tok(fb, 0, GL_POLYGON_TOKEN) && fb[1] == 5.0 && nearf(fb[6], 40.0));
    check!(c, tok(fb, 12, GL_POLYGON_TOKEN) && fb[13] == 4.0 && nearf(fb[14], 0.0) && nearf(fb[16], 100.0) && nearf(fb[20], 0.0));
    glRenderMode(GL_FEEDBACK);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glBegin(GL_TRIANGLES);
    glVertex2i(10, 10);
    glEdgeFlag(0);
    glVertex2i(20, 10);
    glEdgeFlag(1);
    glVertex2i(20, 20);
    glEnd();
    glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
    glBegin(GL_TRIANGLES);
    glVertex2i(10, 10);
    glVertex2i(20, 10);
    glVertex2i(20, 20);
    glEnd();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    n = glRenderMode(GL_RENDER);
    check!(c, n == 2 * 5 + 3 * 3);
    check!(c, tok(fb, 0, GL_LINE_RESET_TOKEN) && tok(fb, 5, GL_LINE_TOKEN) && nearf(fb[6], 20.0) && nearf(fb[8], 10.0));
    check!(c, tok(fb, 10, GL_POINT_TOKEN) && tok(fb, 13, GL_POINT_TOKEN) && tok(fb, 16, GL_POINT_TOKEN) && nearf(fb[17], 20.0));

    // User clip planes come first: x >= 50
    static PLANE: [f64; 4] = [1.0, 0.0, 0.0, -50.0];
    glClipPlane(GL_CLIP_PLANE0, PLANE.as_ptr());
    glEnable(GL_CLIP_PLANE0);
    glRenderMode(GL_FEEDBACK);
    glBegin(GL_LINES);
    glVertex2i(0, 10);
    glVertex2i(100, 10);
    glEnd();
    glDisable(GL_CLIP_PLANE0);
    check!(c, glRenderMode(GL_RENDER) == 5 && nearf(fb[1], 50.0) && nearf(fb[3], 100.0));

    // Depth clipping: identity projection, a triangle reaching z = 2 is cut at the far plane (window depth 1)
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glFeedbackBuffer(256, GL_3D, fbp);
    glRenderMode(GL_FEEDBACK);
    glBegin(GL_TRIANGLES);
    glVertex3f(-0.5, -0.5, 0.0);
    glVertex3f(0.5, -0.5, 0.0);
    glVertex3f(0.0, 0.5, 2.0);
    glEnd();
    check!(c, glRenderMode(GL_RENDER) == 2 + 4 * 3);
    check!(c, fb[1] == 4.0 && nearf(fb[4], 0.5) && nearf(fb[10], 1.0) && nearf(fb[13], 1.0));
    window_projection();

    // GL_3D_COLOR: 8-bit colors, flat shading takes the last vertex of a line
    glFeedbackBuffer(256, GL_3D_COLOR, fbp);
    glRenderMode(GL_FEEDBACK);
    glShadeModel(GL_FLAT);
    glBegin(GL_LINES);
    glColor4f(1.0, 0.0, 0.0, 1.0);
    glVertex2i(10, 10);
    glColor4f(0.0, 0.5, 1.0, 0.25);
    glVertex2i(20, 10);
    glEnd();
    glShadeModel(GL_SMOOTH);
    check!(c, glRenderMode(GL_RENDER) == 1 + 2 * 7);
    check!(c, nearf(fb[4], 0.0) && (fb[5] - 0.5).abs() < 0.01 && nearf(fb[6], 1.0) && (fb[7] - 0.25).abs() < 0.01);
    check!(c, nearf(fb[11], 0.0) && nearf(fb[13], 1.0));

    // GL_4D_COLOR_TEXTURE: w and texcoords through the texture matrix, also r from a texcoord array
    glFeedbackBuffer(256, GL_4D_COLOR_TEXTURE, fbp);
    glMatrixMode(GL_TEXTURE);
    glTranslatef(0.5, 0.0, 0.0);
    glMatrixMode(GL_MODELVIEW);
    glRenderMode(GL_FEEDBACK);
    glColor4f(1.0, 1.0, 1.0, 1.0);
    glTexCoord4f(0.25, 0.5, 0.75, 1.0);
    glBegin(GL_POINTS);
    glVertex2i(10, 10);
    glEnd();
    static ARRAY_POS: [f32; 2] = [30.0, 40.0];
    static ARRAY_TEX: [f32; 3] = [0.0, 0.125, 0.375];
    gl!(glVertexPointer(2, GL_FLOAT, 0, ARRAY_POS.as_ptr()));
    gl!(glTexCoordPointer(3, GL_FLOAT, 0, ARRAY_TEX.as_ptr()));
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDrawArrays(GL_POINTS, 0, 1);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    check!(c, glRenderMode(GL_RENDER) == 2 * 13);
    check!(c, fb_vertex(&fb[1..], 10.0, 10.0, 0.5) && nearf(fb[4], 1.0) && nearf(fb[8], 1.0));
    check!(c, nearf(fb[9], 0.75) && nearf(fb[10], 0.5) && nearf(fb[11], 0.75) && nearf(fb[12], 1.0));
    check!(c, nearf(fb[14], 30.0) && nearf(fb[22], 0.5) && nearf(fb[23], 0.125) && nearf(fb[24], 0.375));

    // Raster tokens: the raster position (glBitmap still moves it); nothing for an invalid one
    static PIXEL: [u8; 4] = [0, 0, 255, 255];
    glFeedbackBuffer(256, GL_3D, fbp);
    glRenderMode(GL_FEEDBACK);
    glRasterPos2i(30, 40);
    glBitmap(0, 0, 0.0, 0.0, 5.0, 0.0, null());
    gl!(glDrawPixels(1, 1, GL_RGBA, GL_UNSIGNED_BYTE, PIXEL.as_ptr()));
    glCopyPixels(0, 0, 1, 1, GL_COLOR);
    glRasterPos2i(-10, 0);
    glBitmap(0, 0, 0.0, 0.0, 5.0, 0.0, null());
    check!(c, glRenderMode(GL_RENDER) == 3 * 4);
    check!(c, tok(fb, 0, GL_BITMAP_TOKEN) && fb_vertex(&fb[1..], 30.0, 40.0, 0.5));
    check!(c, tok(fb, 4, GL_DRAW_PIXEL_TOKEN) && fb_vertex(&fb[5..], 35.0, 40.0, 0.5));
    check!(c, tok(fb, 8, GL_COPY_PIXEL_TOKEN) && nearf(fb[9], 35.0));

    // Overflow: -1, the buffer is filled as far as it goes
    fb[3] = -2.0;
    glFeedbackBuffer(3, GL_3D, fbp);
    glRenderMode(GL_FEEDBACK);
    glBegin(GL_POINTS);
    glVertex2i(10, 10);
    glEnd();
    check!(c, glRenderMode(GL_RENDER) == -1 && tok(fb, 0, GL_POINT_TOKEN) && nearf(fb[2], 10.0) && fb[3] == -2.0);

    // Nothing is drawn or cleared
    glColor3f(0.0, 0.0, 1.0);
    glRecti(380, 220, 390, 230);
    glFeedbackBuffer(256, GL_2D, fbp);
    glRenderMode(GL_FEEDBACK);
    glClearColor(1.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1.0, 1.0, 1.0);
    glRecti(380, 220, 390, 230);
    glRasterPos2i(380, 220);
    gl!(glDrawPixels(1, 1, GL_RGBA, GL_UNSIGNED_BYTE, PIXEL.as_ptr()));
    glRenderMode(GL_RENDER);
    check!(c, pixel_near(385, 225, 0, 0, 255, 255));

    // Display lists record glPassThrough and the name stack commands
    let list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glPassThrough(9.0);
    glInitNames();
    glPushName(5);
    glRecti(10, 10, 20, 20);
    glEndList();
    glRenderMode(GL_FEEDBACK);
    glCallList(list);
    check!(c, glRenderMode(GL_RENDER) == 2 + 2 + 4 * 2 && tok(fb, 0, GL_PASS_THROUGH_TOKEN) && fb[1] == 9.0);

    // Selection: a hit record per name stack change after a hit, with the window depth range
    static mut SEL: [u32; 64] = [0; 64];
    let sel: &mut [u32; 64] = &mut *std::ptr::addr_of_mut!(SEL);
    glSelectBuffer(64, sel.as_mut_ptr());
    glGetPointerv(GL_SELECTION_BUFFER_POINTER, &mut ptr);
    glGetIntegerv(GL_SELECTION_BUFFER_SIZE, &mut v);
    check!(c, ptr as *mut u32 == sel.as_mut_ptr() && v == 64);
    check!(c, glRenderMode(GL_SELECT) == 0);
    glLoadName(1);
    check!(c, glGetError() == GL_INVALID_OPERATION); // Empty stack
    glPopName();
    check!(c, glGetError() == GL_STACK_UNDERFLOW);
    glInitNames();
    glPushName(1);
    glRecti(10, 10, 20, 20); // Hit at depth 0.5
    glLoadName(2);
    glBegin(GL_POINTS);
    glVertex2i(-5, 10); // Outside: no hit
    glEnd();
    glLoadName(3);
    glPushName(4);
    glBegin(GL_LINES);
    glVertex3f(10.0, 10.0, 0.5); // Window depth 0.25 .. 0.75
    glVertex3f(20.0, 10.0, -0.5);
    glEnd();
    glGetIntegerv(GL_NAME_STACK_DEPTH, &mut v);
    check!(c, v == 2);
    glLoadName(5);
    glEnable(GL_CULL_FACE);
    glRecti(20, 10, 10, 20); // Clockwise: culled, no hit
    glDisable(GL_CULL_FACE);
    glLoadName(6);
    glRasterPos3f(10.0, 10.0, -0.8); // A valid raster position hits (depth 0.9)
    glPopName();
    glPopName();
    glCallList(list); // glInitNames, name 5, a hit
    check!(c, glRenderMode(GL_RENDER) == 4);
    check!(c, sel[0] == 1 && hit_depth(sel[1], 0.5) && hit_depth(sel[2], 0.5) && sel[3] == 1);
    check!(c, sel[4] == 2 && hit_depth(sel[5], 0.25) && hit_depth(sel[6], 0.75) && sel[7] == 3 && sel[8] == 4);
    check!(c, sel[9] == 2 && hit_depth(sel[10], 0.9) && hit_depth(sel[11], 0.9) && sel[12] == 3 && sel[13] == 6);
    check!(c, sel[14] == 1 && hit_depth(sel[15], 0.5) && sel[17] == 5);
    glGetIntegerv(GL_NAME_STACK_DEPTH, &mut v);
    check!(c, v == 0);

    // Name stack overflow, selection buffer overflow
    glSelectBuffer(2, sel.as_mut_ptr());
    glRenderMode(GL_SELECT);
    for i in 0..64 {
        glPushName(i);
    }
    check!(c, glGetError() == GL_NO_ERROR);
    glPushName(64);
    check!(c, glGetError() == GL_STACK_OVERFLOW);
    glRecti(10, 10, 20, 20);
    check!(c, glRenderMode(GL_RENDER) == -1);
    glDeleteLists(list, 1);

    // glRenderMode between glBegin and glEnd
    glBegin(GL_POINTS);
    check!(c, glRenderMode(GL_SELECT) == 0);
    glEnd();
    check!(c, glGetError() == GL_INVALID_OPERATION);
    glGetIntegerv(GL_RENDER_MODE, &mut v);
    check!(c, v == GL_RENDER as i32);
    glPopAttrib();
}

/// Pixel (x, y) is white (true) or black (false); prints it otherwise.
unsafe fn white_at(x: i32, y: i32, white: bool) -> bool {
    let cval = if white { 255 } else { 0 };
    pixel_near(x, y, cval, cval, cval, -1)
}

/// Line stipple and polygon stipple: defaults, clamping, queries, attribute groups, display lists,
/// unpack/pack of the pattern, then rendered and read back: dashes along both axes and directions,
/// the counter carried along strips and reset by separate lines and outlines, wide lines, and the
/// window-aligned polygon pattern (orthographic, with perspective, textured, with the alpha test).
unsafe fn test_stipple(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    let mut v = -1i32;
    let mut mask = [0u8; 8 * 33];
    check!(c, glIsEnabled(GL_LINE_STIPPLE) == 0 && glIsEnabled(GL_POLYGON_STIPPLE) == 0);
    glGetIntegerv(GL_LINE_STIPPLE_PATTERN, &mut v);
    check!(c, v == 0xFFFF);
    glGetIntegerv(GL_LINE_STIPPLE_REPEAT, &mut v);
    check!(c, v == 1);
    glGetPolygonStipple(mask.as_mut_ptr());
    check!(c, mask[..128].iter().all(|&b| b == 0xFF) && mask[128] == 0);
    glLineStipple(0, 0x1234); // The factor is clamped to 1..256
    glGetIntegerv(GL_LINE_STIPPLE_REPEAT, &mut v);
    check!(c, v == 1);
    glLineStipple(300, 0xF0F0);
    glGetIntegerv(GL_LINE_STIPPLE_REPEAT, &mut v);
    check!(c, v == 256);
    glGetIntegerv(GL_LINE_STIPPLE_PATTERN, &mut v);
    check!(c, v == 0xF0F0);
    glEnable(GL_LINE_STIPPLE);
    let mut b = 0u8;
    glGetBooleanv(GL_LINE_STIPPLE, &mut b);
    check!(c, b == 1);
    glDisable(GL_LINE_STIPPLE);
    check!(c, glGetError() == GL_NO_ERROR);

    // Attribute groups: GL_LINE_BIT (pattern, factor, enable), GL_POLYGON_BIT (enable), GL_POLYGON_STIPPLE_BIT
    // (pattern), GL_ENABLE_BIT (both enables)
    glLineStipple(3, 0x00FF);
    glPushAttrib(GL_LINE_BIT);
    glLineStipple(5, 0xAAAA);
    glEnable(GL_LINE_STIPPLE);
    glPopAttrib();
    glGetIntegerv(GL_LINE_STIPPLE_REPEAT, &mut v);
    check!(c, v == 3);
    glGetIntegerv(GL_LINE_STIPPLE_PATTERN, &mut v);
    check!(c, v == 0x00FF);
    check!(c, glIsEnabled(GL_LINE_STIPPLE) == 0);
    glPushAttrib(GL_ENABLE_BIT);
    glEnable(GL_LINE_STIPPLE);
    glEnable(GL_POLYGON_STIPPLE);
    glPopAttrib();
    check!(c, glIsEnabled(GL_LINE_STIPPLE) == 0 && glIsEnabled(GL_POLYGON_STIPPLE) == 0);
    glPushAttrib(GL_POLYGON_BIT);
    glEnable(GL_POLYGON_STIPPLE);
    glPopAttrib();
    check!(c, glIsEnabled(GL_POLYGON_STIPPLE) == 0);
    let mut pattern = [0u8; 128];
    for (i, p) in pattern.iter_mut().enumerate() {
        *p = i as u8;
    }
    glPushAttrib(GL_POLYGON_STIPPLE_BIT);
    glPolygonStipple(pattern.as_ptr());
    glGetPolygonStipple(mask.as_mut_ptr());
    check!(c, mask[..128] == pattern);
    glPopAttrib();
    glGetPolygonStipple(mask.as_mut_ptr());
    check!(c, mask[0] == 0xFF && mask[127] == 0xFF);

    // Unpacked like a bitmap (LSB first, row length, skip pixels), packed with the pack state
    let mut wide = [0u8; 8 * 32];
    for y in 0..32 {
        wide[8 * y + 1] = 0x01; // LSB first, skip 4: bit 8 - 4 = x 4 of each row
    }
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_TRUE as i32);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 64);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 4);
    glPolygonStipple(wide.as_ptr());
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE as i32);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glGetPolygonStipple(mask.as_mut_ptr());
    check!(c, mask[0] == 0x08 && mask[1] == 0 && mask[124] == 0x08);
    mask.fill(0xEE);
    glPixelStorei(GL_PACK_ROW_LENGTH, 40);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 4);
    glPixelStorei(GL_PACK_SKIP_ROWS, 1);
    glGetPolygonStipple(mask.as_mut_ptr());
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    // Rows of 40 bits padded to 8 bytes (alignment 4), the first row skipped; x 4 lands on bit 8 (byte 1, MSB), the
    // bits before column 4 and after column 35 keep their 0xEE
    check!(c, mask[0] == 0xEE && mask[8] == 0xE0 && mask[9] == 0x80 && mask[12] == 0x0E && mask[8 * 32 + 1] == 0x80);

    // Display lists: recorded with the unpack state at compile time
    let list = glGenLists(1);
    for (i, p) in pattern.iter_mut().enumerate() {
        *p = 0x80u8 >> (i & 7);
    }
    glNewList(list, GL_COMPILE);
    glLineStipple(7, 0x1111);
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_TRUE as i32); // Not recorded, executed now
    glPolygonStipple(pattern.as_ptr());
    glEndList();
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE as i32);
    glGetIntegerv(GL_LINE_STIPPLE_REPEAT, &mut v);
    check!(c, v == 3);
    glGetPolygonStipple(mask.as_mut_ptr());
    check!(c, mask[0] == 0x08);
    glCallList(list);
    glGetIntegerv(GL_LINE_STIPPLE_REPEAT, &mut v);
    check!(c, v == 7);
    glGetPolygonStipple(mask.as_mut_ptr());
    check!(c, mask[0] == 0x01 && mask[1] == 0x02 && mask[7] == 0x80);
    glDeleteLists(list, 1);

    // Rendered lines: white on black in window coordinates
    window_projection();
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1.0, 1.0, 1.0);
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(1, 0x00FF);
    let line = |x0: f32, y0: f32, x1: f32, y1: f32| {
        glVertex2f(x0, y0);
        glVertex2f(x1, y1);
    };
    glBegin(GL_LINES);
    line(10.0, 100.5, 60.0, 100.5); // Fragments x 10.. (s = x - 10)
    line(200.5, 20.0, 200.5, 60.0); // y 20..
    line(60.0, 140.5, 10.0, 140.5); // Right to left: x 59, 58, ...
    line(10.0, 130.5, 14.0, 130.5); // Separate lines: the second one starts again
    line(14.0, 130.5, 30.0, 130.5);
    glEnd();
    glBegin(GL_LINE_STRIP); // A strip carries the counter over
    glVertex2f(10.0, 120.5);
    glVertex2f(14.0, 120.5);
    glVertex2f(30.0, 120.5);
    glEnd();
    glLineStipple(3, 0x0001);
    glBegin(GL_LINES);
    line(10.0, 110.5, 70.0, 110.5); // On for s 0..2 and 48..50
    glEnd();
    glLineStipple(1, 0x00FF);
    glLineWidth(3.0);
    glBegin(GL_LINES);
    line(10.0, 200.5, 60.0, 200.5); // Rows 199..201
    glEnd();
    glLineWidth(1.0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glRectf(80.5, 160.5, 120.5, 180.5); // Each outline starts again
    glRectf(80.5, 190.5, 120.5, 210.5);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_LINE_STIPPLE);
    check!(c, white_at(10, 100, true) && white_at(17, 100, true) && white_at(18, 100, false) && white_at(25, 100, false) && white_at(26, 100, true) && white_at(17, 101, false) && white_at(9, 100, false));
    check!(c, white_at(200, 20, true) && white_at(200, 27, true) && white_at(200, 28, false) && white_at(200, 35, false) && white_at(200, 36, true));
    check!(c, white_at(59, 140, true) && white_at(52, 140, true) && white_at(51, 140, false) && white_at(44, 140, false) && white_at(43, 140, true));
    check!(c, white_at(17, 120, true) && white_at(18, 120, false) && white_at(21, 120, false) && white_at(26, 120, true));
    check!(c, white_at(17, 130, true) && white_at(18, 130, true) && white_at(21, 130, true) && white_at(22, 130, false));
    check!(c, white_at(12, 110, true) && white_at(13, 110, false) && white_at(57, 110, false) && white_at(58, 110, true) && white_at(60, 110, true) && white_at(61, 110, false));
    check!(c, white_at(17, 199, true) && white_at(17, 201, true) && white_at(18, 199, false) && white_at(18, 201, false) && white_at(26, 200, true));
    check!(c, white_at(87, 160, true) && white_at(88, 160, false) && white_at(87, 190, true) && white_at(88, 190, false));

    // Polygon stipple: the diagonal x % 32 == y % 32 of the window, whatever the viewport
    for y in 0..32 {
        pattern[4 * y..4 * y + 4].fill(0);
        pattern[4 * y + y / 8] = 0x80u8 >> (y & 7);
    }
    glPolygonStipple(pattern.as_ptr());
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_POLYGON_STIPPLE);
    glViewport(3, 5, 400, 240);
    glRectf(97.0, 15.0, 161.0, 79.0); // Window 100..164, 20..84
    glViewport(0, 0, 400, 240);
    check!(c, white_at(100, 36, true) && white_at(101, 36, false) && white_at(132, 36, true) && white_at(100, 68, true) && white_at(121, 57, true) && white_at(121, 58, false) && white_at(99, 35, false));

    // Checker (x + y even) with perspective, a red texture on unit 0 and unit 1 (moved to PICA units 1 and 2)
    for y in 0..32 {
        pattern[4 * y..4 * y + 4].fill(if y & 1 == 1 { 0x55 } else { 0xAA });
    }
    glPolygonStipple(pattern.as_ptr());
    let mut tex = [0u32; 2];
    glGenTextures(2, tex.as_mut_ptr());
    static RED: [u8; 4] = [255, 0, 0, 255];
    static YELLOW: [u8; 4] = [255, 255, 0, 255];
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST)); // 1x1 in 8x8: no padding sampled
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, YELLOW.as_ptr()));
    glEnable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, RED.as_ptr()));
    glEnable(GL_TEXTURE_2D);
    gl!(glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE)); // yellow * red = red
    glActiveTexture(GL_TEXTURE0);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -0.6, 0.6, 1.0, 10.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0, 0.0, -3.0);
    glRotatef(-60.0, 1.0, 0.0, 0.0);
    glRotatef(20.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);
    glTexCoord2f(0.5, 0.5);
    glVertex2f(-2.0, -2.0);
    glVertex2f(2.0, -2.0);
    glVertex2f(2.0, 2.0);
    glVertex2f(-2.0, 2.0);
    glEnd();
    glActiveTexture(GL_TEXTURE1);
    glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_TEXTURE_2D);
    let mut checker = true;
    for y in (110..130).step_by(3) {
        for x in (190..212).step_by(5) {
            checker = checker && pixel_near(x, y, if (x + y) & 1 == 1 { 0 } else { 255 }, 0, 0, -1);
        }
    }
    check!(c, checker);
    glDeleteTextures(2, tex.as_ptr());

    // Alpha test: fragments that fail it stay away; without it alpha 1/255 is drawn (stipple off: the whole quad)
    // and alpha keeps its exact value (alpha 0 is dropped then, see README)
    window_projection();
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5);
    glColor4f(0.0, 1.0, 0.0, 0.25);
    glRectf(10.0, 10.0, 20.0, 20.0);
    glColor4f(0.0, 1.0, 0.0, 0.75);
    glRectf(30.0, 10.0, 40.0, 20.0);
    glAlphaFunc(GL_LESS, 0.5);
    glRectf(50.0, 10.0, 60.0, 20.0);
    glColor4f(0.0, 1.0, 0.0, 0.25);
    glRectf(70.0, 10.0, 80.0, 20.0);
    glDisable(GL_ALPHA_TEST);
    glColor4ub(0, 0, 255, 1);
    glRectf(90.0, 10.0, 100.0, 20.0);
    glColor4ub(255, 0, 0, 200);
    glRectf(130.0, 10.0, 140.0, 20.0);
    glColor4ub(255, 0, 0, 255);
    glRectf(150.0, 10.0, 160.0, 20.0);
    glDisable(GL_POLYGON_STIPPLE);
    glColor4ub(0, 0, 255, 1);
    glRectf(110.0, 10.0, 120.0, 20.0);
    check!(c, pixel_near(10, 10, 0, 0, 0, 255) && pixel_near(11, 11, 0, 0, 0, 255));
    check!(c, pixel_near(30, 10, 0, 255, 0, -1) && pixel_near(31, 10, 0, 0, 0, 255) && pixel_near(50, 10, 0, 0, 0, 255));
    check!(c, pixel_near(70, 10, 0, 255, 0, -1) && pixel_near(71, 10, 0, 0, 0, 255));
    check!(c, pixel_near(90, 10, 0, 0, 255, 1) && pixel_near(91, 10, 0, 0, 0, 255) && pixel_near(111, 10, 0, 0, 255, 1));
    let mut alpha = [[0u8; 4]; 3];
    gl!(glReadPixels(90, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, alpha[0].as_mut_ptr()));
    gl!(glReadPixels(130, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, alpha[1].as_mut_ptr()));
    gl!(glReadPixels(150, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, alpha[2].as_mut_ptr()));
    check!(c, alpha[0][3] == 1 && alpha[1][3] == 200 && alpha[2][3] == 255); // Exact in the pattern
    check!(c, pixel_near(131, 10, 0, 0, 0, 255) && pixel_near(151, 10, 0, 0, 0, 255));
    check!(c, glGetError() == GL_NO_ERROR);
    glPopAttrib();
}

/// Accumulation buffer: queries, clamping, errors, attribute group, display lists, then rendered
/// and read back: clear, load/accumulate (averaging two frames), add, mult, return with clamping,
/// scissor box, color mask, return without the other fragment operations, draws before and after
/// the operations, nothing in feedback mode.
unsafe fn test_accum(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    let mut bits = [0i32; 4];
    glGetIntegerv(GL_ACCUM_RED_BITS, &mut bits[0]);
    glGetIntegerv(GL_ACCUM_GREEN_BITS, &mut bits[1]);
    glGetIntegerv(GL_ACCUM_BLUE_BITS, &mut bits[2]);
    glGetIntegerv(GL_ACCUM_ALPHA_BITS, &mut bits[3]);
    check!(c, bits == [16, 16, 16, 16]);
    let mut cv = [-9.0f32; 4];
    glGetFloatv(GL_ACCUM_CLEAR_VALUE, cv.as_mut_ptr());
    check!(c, cv == [0.0, 0.0, 0.0, 0.0]);
    glClearAccum(2.0, -3.0, 0.5, -0.25); // Clamped to [-1, 1]
    glGetFloatv(GL_ACCUM_CLEAR_VALUE, cv.as_mut_ptr());
    check!(c, cv == [1.0, -1.0, 0.5, -0.25]);
    gl!(glAccum(0x1234, 1.0));
    check!(c, glGetError() == GL_INVALID_ENUM);
    glBegin(GL_POINTS);
    glAccum(GL_RETURN, 1.0);
    glEnd();
    check!(c, glGetError() == GL_INVALID_OPERATION);
    glClear(GL_CURRENT_BIT);
    check!(c, glGetError() == GL_INVALID_VALUE);

    // GL_ACCUM_BUFFER_BIT holds the clear value, GL_COLOR_BUFFER_BIT does not
    glPushAttrib(GL_ACCUM_BUFFER_BIT);
    glClearAccum(0.1, 0.2, 0.3, 0.4);
    glPopAttrib();
    glGetFloatv(GL_ACCUM_CLEAR_VALUE, cv.as_mut_ptr());
    check!(c, cv[0] == 1.0 && cv[3] == -0.25);
    glPushAttrib(GL_COLOR_BUFFER_BIT);
    glClearAccum(0.1, 0.2, 0.3, 0.4);
    glPopAttrib();
    glGetFloatv(GL_ACCUM_CLEAR_VALUE, cv.as_mut_ptr());
    check!(c, (cv[0] - 0.1).abs() < 1e-6);

    // Rendered: window coordinates, the accumulation buffer returned with value 1 is read back
    window_projection();
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearAccum(0.25, 0.5, 0.75, 1.0);
    glClear(GL_ACCUM_BUFFER_BIT);
    glAccum(GL_RETURN, 1.0);
    check!(c, pixel_near(0, 0, 64, 128, 191, 255) && pixel_near(399, 239, 64, 128, 191, 255));
    glAccum(GL_RETURN, 4.0); // Clamped to 1
    check!(c, pixel_near(200, 100, 255, 255, 255, 255));

    // Two frames averaged; GL_LOAD replaces, GL_ACCUM adds
    glClearColor(1.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glAccum(GL_LOAD, 0.5);
    glClearColor(0.0, 0.0, 1.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(0.0, 1.0, 0.0);
    glRectf(100.0, 100.0, 110.0, 110.0); // Drawn before the read: in the accumulated image
    glAccum(GL_ACCUM, 0.5);
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glAccum(GL_RETURN, 1.0);
    check!(c, pixel_near(10, 10, 128, 0, 128, 255) && pixel_near(105, 105, 128, 128, 0, 255));

    // GL_MULT and GL_ADD on every component, negative values clamp to 0 when returned
    glAccum(GL_MULT, 0.5);
    glAccum(GL_RETURN, 2.0);
    check!(c, pixel_near(10, 10, 128, 0, 128, 255));
    glAccum(GL_ADD, 0.25); // (0.25, 0, 0.25, 0.5) + 0.25
    glAccum(GL_RETURN, 1.0);
    check!(c, pixel_near(10, 10, 128, 64, 128, 191));
    glAccum(GL_ADD, -1.0);
    glAccum(GL_RETURN, 1.0);
    check!(c, pixel_near(10, 10, 0, 0, 0, 0));
    glAccum(GL_ADD, -1.0);
    glAccum(GL_ADD, 0.5); // -1 (clamped) + 0.5
    glAccum(GL_RETURN, -1.0);
    check!(c, pixel_near(10, 10, 128, 128, 128, 128));

    // Scissor box: clears and operations change only the pixels in it, the return writes only them
    glClearAccum(0.0, 0.0, 0.0, 1.0);
    glClear(GL_ACCUM_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(20, 20, 10, 10);
    glClearAccum(1.0, 1.0, 1.0, 1.0);
    glClear(GL_ACCUM_BUFFER_BIT);
    glAccum(GL_ADD, -0.5);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0, 0.0, 1.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(0, 0, 25, 240);
    glEnable(GL_SCISSOR_TEST);
    glAccum(GL_RETURN, 1.0);
    glDisable(GL_SCISSOR_TEST);
    check!(c, pixel_near(20, 20, 128, 128, 128, 128) && pixel_near(24, 29, 128, 128, 128, 128));
    check!(c, pixel_near(19, 20, 0, 0, 0, 255) && pixel_near(20, 30, 0, 0, 0, 255) && pixel_near(25, 20, 0, 0, 255, 255));

    // The return goes through the color mask, but not blending, alpha, depth or stencil tests, logic ops or fog;
    // draws after it are drawn over it
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearAccum(1.0, 0.5, 1.0, 1.0);
    glClear(GL_ACCUM_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_ZERO);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_NEVER, 0.0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_NEVER);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_NEVER, 0, 0xFF);
    glEnable(GL_FOG);
    glColorMask(1, 1, 0, 1);
    glAccum(GL_RETURN, 1.0);
    glColorMask(1, 1, 1, 1);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_FOG);
    glColor3f(0.0, 1.0, 0.0);
    glRectf(50.0, 50.0, 60.0, 60.0);
    check!(c, pixel_near(40, 40, 255, 128, 0, 255) && pixel_near(55, 55, 0, 255, 0, 255));
    glDrawBuffer(GL_NONE); // Returns nothing
    glClearAccum(0.0, 0.0, 0.0, 0.0);
    glClear(GL_ACCUM_BUFFER_BIT);
    glAccum(GL_RETURN, 1.0);
    glDrawBuffer(GL_BACK);
    check!(c, pixel_near(40, 40, 255, 128, 0, 255));

    // Display lists: compiled, executed when called
    let list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glClearAccum(0.0, 1.0, 0.0, 1.0);
    glClear(GL_ACCUM_BUFFER_BIT);
    glAccum(GL_RETURN, 1.0);
    glEndList();
    glGetFloatv(GL_ACCUM_CLEAR_VALUE, cv.as_mut_ptr());
    check!(c, cv[1] == 0.0);
    check!(c, pixel_near(40, 40, 255, 128, 0, 255));
    glCallList(list);
    check!(c, pixel_near(40, 40, 0, 255, 0, 255));
    glDeleteLists(list, 1);

    // Feedback mode: no accumulation buffer operation
    let mut feedback = [0f32; 8];
    glFeedbackBuffer(8, GL_2D, feedback.as_mut_ptr());
    glRenderMode(GL_FEEDBACK);
    glAccum(GL_ADD, -1.0);
    glRenderMode(GL_RENDER);
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    glAccum(GL_RETURN, 1.0);
    check!(c, pixel_near(40, 40, 0, 255, 0, 255));
    check!(c, glGetError() == GL_NO_ERROR);
    glPopAttrib();
}

/// Color of window pixel (x, y) within tol (filtered texels in Azahar's upscaled renderers); prints it otherwise.
unsafe fn pixel_about(x: i32, y: i32, r: i32, g: i32, b: i32, tol: i32) -> bool {
    let mut p = [0u8; 4];
    gl!(glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.as_mut_ptr()));
    let d = |v: u8, e: i32| (v as i32 - e).abs() <= tol;
    let ok = d(p[0], r) && d(p[1], g) && d(p[2], b);
    if !ok {
        println!("  pixel ({x}, {y}): {} {} {}", p[0], p[1], p[2]);
    }
    ok
}

/// Wrap modes on a non-power-of-two texture (12x10, stored 16x16): the padding holds what GL
/// samples past the image, so linear filtering across the edges and clamped coordinates past 1 see
/// the image, not the padding. Blue first column and row, red elsewhere, magnified 10 times.
unsafe fn test_npot_wrap(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    let mut texels = [[[0u8; 4]; 12]; 10];
    for y in 0..10 {
        for x in 0..12 {
            let blue = x == 0 || y == 0;
            texels[y][x] = [if blue { 0 } else { 255 }, 0, if blue { 255 } else { 0 }, 255];
        }
    }
    let mut tex = 0u32;
    glGenTextures(1, &mut tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    gl!(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 12, 10, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.as_ptr()));
    window_projection();
    glEnable(GL_TEXTURE_2D);
    gl!(glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE));
    glClearColor(0.0, 0.0, 0.0, 1.0);
    // s over 120 pixels at the middle of row 4, t over 120 pixels in the middle of column 5
    let (t_row, s_col) = (4.5f32 / 10.0, 5.5f32 / 12.0);
    for pass in 0..2 {
        let repeat = pass == 0;
        let wrap = if repeat { GL_REPEAT } else { GL_CLAMP_TO_EDGE };
        gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap));
        gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap));
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0, t_row);
        glVertex2i(10, 50);
        glTexCoord2f(1.0, t_row);
        glVertex2i(130, 50);
        glTexCoord2f(1.0, t_row);
        glVertex2i(130, 60);
        glTexCoord2f(0.0, t_row);
        glVertex2i(10, 60);
        glTexCoord2f(s_col, 0.0);
        glVertex2i(200, 50);
        glTexCoord2f(s_col, 0.0);
        glVertex2i(210, 50);
        glTexCoord2f(s_col, 1.0);
        glVertex2i(210, 170);
        glTexCoord2f(s_col, 1.0);
        glVertex2i(200, 170);
        glTexCoord2f(0.0, t_row);
        glVertex2i(10, 80); // Clamped up to s = 2
        glTexCoord2f(2.0, t_row);
        glVertex2i(130, 80);
        glTexCoord2f(2.0, t_row);
        glVertex2i(130, 90);
        glTexCoord2f(0.0, t_row);
        glVertex2i(10, 90);
        glEnd();
        // Last pixel: 0.45 of the texel past the image end; first pixel: 0.45 of the texel before the image
        // start. Repeat: the other edge (blue, red); clamp: the edge itself
        if repeat {
            check!(c, pixel_about(129, 55, 140, 0, 115, 25) && pixel_about(10, 55, 115, 0, 140, 25));
            check!(c, pixel_about(205, 169, 140, 0, 115, 25) && pixel_about(205, 50, 115, 0, 140, 25));
        } else {
            check!(c, pixel_about(129, 55, 255, 0, 0, 25) && pixel_about(10, 55, 0, 0, 255, 25));
            check!(c, pixel_about(205, 169, 255, 0, 0, 25) && pixel_about(205, 50, 0, 0, 255, 25));
            check!(c, pixel_about(120, 85, 255, 0, 0, 25) && pixel_about(100, 85, 255, 0, 0, 25));
        }
    }
    glDisable(GL_TEXTURE_2D);
    glDeleteTextures(1, &tex);
    check!(c, glGetError() == GL_NO_ERROR);
    glPopAttrib();
}

/// Texture updates within a frame (glTexSubImage2D, glTexImage2D of the same size, a new level,
/// glDeleteTextures and reuse of the id) leave the draws issued before them as they were.
unsafe fn fill_texture(target: u32, level: i32, sub: bool, size: usize, r: u8, g: u8, b: u8) {
    let texels: Vec<u8> = (0..size * size).flat_map(|_| [r, g, b, 255]).collect();
    if sub {
        gl!(glTexSubImage2D(target, level, 0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, texels.as_ptr()));
    } else {
        gl!(glTexImage2D(target, level, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.as_ptr()));
    }
}

unsafe fn textured_square(x: i32) {
    glBegin(GL_QUADS);
    glTexCoord2f(0.0, 0.0);
    glVertex2i(x, 120);
    glTexCoord2f(1.0, 0.0);
    glVertex2i(x + 20, 120);
    glTexCoord2f(1.0, 1.0);
    glVertex2i(x + 20, 140);
    glTexCoord2f(0.0, 1.0);
    glVertex2i(x, 140);
    glEnd();
}

unsafe fn test_tex_update_in_frame(c: &mut Checks) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    window_projection();
    glEnable(GL_TEXTURE_2D);
    gl!(glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE));
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    let mut tex = 0u32;
    glGenTextures(1, &mut tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    fill_texture(GL_TEXTURE_2D, 0, false, 16, 255, 0, 0);
    textured_square(10);
    fill_texture(GL_TEXTURE_2D, 0, true, 16, 0, 255, 0);
    textured_square(40);
    fill_texture(GL_TEXTURE_2D, 0, false, 16, 0, 0, 255);
    textured_square(70);
    fill_texture(GL_TEXTURE_2D, 1, false, 8, 0, 0, 0); // Becomes a mip chain (level 0 is copied over)
    textured_square(100);
    fill_texture(GL_TEXTURE_2D, 0, true, 16, 255, 255, 0); // Changes the whole chain
    textured_square(130);
    glDeleteTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex); // Same id again
    gl!(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    fill_texture(GL_TEXTURE_2D, 0, false, 16, 255, 0, 255);
    textured_square(160);
    check!(c, pixel_near(20, 130, 255, 0, 0, -1));
    check!(c, pixel_near(50, 130, 0, 255, 0, -1));
    check!(c, pixel_near(80, 130, 0, 0, 255, -1));
    check!(c, pixel_near(110, 130, 0, 0, 255, -1));
    check!(c, pixel_near(140, 130, 255, 255, 0, -1));
    check!(c, pixel_near(170, 130, 255, 0, 255, -1));
    glDisable(GL_TEXTURE_2D);
    glDeleteTextures(1, &tex);
    check!(c, glGetError() == GL_NO_ERROR);
    glPopAttrib();
}

unsafe fn run_tests(c: &mut Checks) -> Buffers {
    test_errors(c);
    test_capabilities(c);
    test_queries(c);
    test_matrices(c);
    test_arrays_and_es(c);
    test_buffers(c);
    test_attrib_stacks(c);
    test_compressed(c);
    test_read_pixels(c);
    test_copy_tex_image(c);
    test_logic_op_and_multisample(c);
    test_display_lists(c);
    test_tex_gen(c);
    test_tex_formats(c);
    test_1d(c);
    test_color_buffers(c);
    test_pixels(c);
    test_pixel_transfer(c);
    test_default_textures(c);
    test_feedback(c);
    test_stipple(c);
    test_accum(c);
    test_npot_wrap(c);
    test_tex_update_in_frame(c);
    let buffers = create_buffers();
    check!(c, glGetError() == GL_NO_ERROR); // Nothing left over
    buffers
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut app = App::new();
    let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };

    print!("c3dgl api test\n\n\n\n\n(C3DGL warnings below come from\n the error checks, they are expected)\n\n");
    let mut checks = Checks { log: std::fs::File::create("sdmc:/c3dgl-api.log").ok(), ..Default::default() };
    let buffers = unsafe { run_tests(&mut checks) };
    if let Some(log) = &mut checks.log {
        use std::io::Write;
        let _ = writeln!(log, "checks={} failures={}", checks.total, checks.failures);
    }
    checks.log = None;
    print!(
        "\n{}/{} checks passed\n\nExpected on the top screen:\n- GREEN background (all passed)\n- four identical white squares\n  in a row, below them four\n  identical ORANGE squares and\n  four identical CYAN squares\n\nSTART: exit\n",
        checks.passed(),
        checks.total
    );

    let mut stats = StatsOverlay::new();
    while app.frame() {
        stats.print();
        if app.down(KeyPad::START) {
            break;
        }
        unsafe {
            if checks.failures == 0 {
                glClearColor(0.1, 0.5, 0.2, 1.0);
            } else {
                glClearColor(0.7, 0.1, 0.1, 1.0);
            }
            glClear(GL_COLOR_BUFFER_BIT);
            draw_squares();
            draw_array_squares();
            draw_buffer_squares(&buffers);
            c3dglSwapBuffers();
        }
    }
}
