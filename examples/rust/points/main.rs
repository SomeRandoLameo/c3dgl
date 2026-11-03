//! Point parameters, point sprites and the point size array (OpenGL ES 1.1). The top screen shows
//! one setup per cell (4x2 grid, 100x120 px per cell). Most cells draw pairs: left the points under
//! test, right a reference drawn without the feature (plain points of the expected size, or
//! textured quads with the expected texcoords). Each pair must look the same. The last cell is a
//! particle field (soft round sprites with distance attenuation). The bottom screen shows the
//! result of the non-visual checks and what each cell must look like.
#[macro_use]
#[path = "../common/mod.rs"]
mod common;

use c3dgl::*;
use common::gl::*;
use common::{App, Checks, Renderer, StatsOverlay, SCREEN_HEIGHT, SCREEN_WIDTH};
use ctru::prelude::*;
use std::ffi::{c_void, CStr};
use std::mem::{offset_of, size_of};
use std::ptr::null;

const COLUMNS: i32 = 4;
const ROWS: i32 = 2;
const CELL_W: i32 = SCREEN_WIDTH / COLUMNS;
const CELL_H: i32 = SCREEN_HEIGHT / ROWS;
const NEAR: f32 = 1.0;
const HALF_W: f32 = 0.5;
const HALF_H: f32 = HALF_W * CELL_H as f32 / CELL_W as f32; // Frustum at the near plane, square pixels
const PARTICLES: usize = 256;

fn near(a: f32, b: f32) -> bool {
    (a - b).abs() < 1e-4
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct Particle {
    pos: [f32; 3],
    color: [u8; 4],
    size: f32, // Before attenuation
}

/// Textures, buffers and particles shared by the cells.
struct Scene {
    quadrants: u32,
    disc: u32,
    size_buffer: u32,
    particles: Vec<Particle>,
}

/// 2x2: red, green in the first row (t = 0), blue, yellow in the second. A sprite shows red top left.
unsafe fn create_quadrants() -> u32 {
    let pixels: [u8; 12] = [255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0];
    let mut id = 0;
    glGenTextures(1, &mut id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB as i32, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.as_ptr().cast());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE as i32);
    id
}

/// 32x32 white disc with an alpha falloff to the edge (soft particle).
unsafe fn create_disc() -> u32 {
    let mut pixels = [0u8; 32 * 32 * 2];
    for y in 0..32 {
        for x in 0..32 {
            let (dx, dy) = ((x as f32 + 0.5 - 16.0) / 16.0, (y as f32 + 0.5 - 16.0) / 16.0);
            let a = 1.0 - (dx * dx + dy * dy).sqrt();
            pixels[(y * 32 + x) * 2] = 255;
            pixels[(y * 32 + x) * 2 + 1] = (a.clamp(0.0, 1.0) * 255.0) as u8;
        }
    }
    let mut id = 0;
    glGenTextures(1, &mut id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA as i32, 32, 32, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, pixels.as_ptr().cast());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE as i32);
    id
}

/// Cell (column, row from the top) as viewport. Pixel coordinates (origin bottom left of the cell) or a frustum.
unsafe fn begin_cell(column: i32, row: i32, perspective: bool) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if perspective {
        glFrustumf(-HALF_W, HALF_W, -HALF_H, HALF_H, NEAR, 30.0);
    } else {
        glOrthof(0.0, CELL_W as f32, 0.0, CELL_H as f32, -1.0, 1.0);
    }
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glPushAttrib(GL_POINT_BIT | GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT);
}

unsafe fn end_cell() {
    glPopAttrib();
}

/// One point through a vertex array (the ES way).
unsafe fn draw_point(x: f32, y: f32, z: f32) {
    let v = [x, y, z];
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, v.as_ptr().cast());
    glDrawArrays(GL_POINTS, 0, 1);
    glDisableClientState(GL_VERTEX_ARRAY);
}

const CORNERS: [[f32; 4]; 4] = [[-1., -1., 0., 1.], [1., -1., 1., 1.], [1., 1., 1., 0.], [-1., 1., 0., 0.]];

/// Square of `size` pixels around (x, y), texcoords like a point sprite: (0, 0) top left, (1, 1) bottom right.
unsafe fn draw_sprite_quad(x: f32, y: f32, size: f32, units: u32) {
    let r = 0.5 * size;
    glBegin(GL_QUADS);
    for c in &CORNERS {
        for u in 0..units {
            glMultiTexCoord2f(GL_TEXTURE0 + u, c[2], c[3]);
        }
        glVertex2f(x + c[0] * r, y + c[1] * r);
    }
    glEnd();
}

/// Point size array of the lower row of cell 1: GL_FIXED sizes 4, 8, 12 and 30 (clamped to 16 by
/// GL_POINT_SIZE_MAX) with a gap of 4 bytes after each, in a buffer object.
unsafe fn create_size_buffer() -> u32 {
    let sizes: [i32; 8] = [4 << 16, -1, 8 << 16, -1, 12 << 16, -1, 30 << 16, -1];
    let mut id = 0;
    glGenBuffers(1, &mut id);
    glBindBuffer(GL_ARRAY_BUFFER, id);
    glBufferData(GL_ARRAY_BUFFER, size_of::<[i32; 8]>() as isize, sizes.as_ptr().cast(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    id
}

/// 1: GL_POINT_SIZE_MIN/MAX clamp glPointSize: 20 with max 12, 2 with min 8. Right: plain 12 and 8.
/// Bottom: four points in one draw with sizes 4, 8, 12, 16 from the point size array; glPointSize (2) is ignored.
unsafe fn draw_clamp(s: &Scene) {
    glColor4f(1.0, 0.6, 0.1, 1.0);
    glPointSize(20.0);
    glPointParameterf(GL_POINT_SIZE_MAX, 12.0);
    draw_point(30.5, 95.5, 0.0);
    glPointSize(2.0);
    glPointParameterf(GL_POINT_SIZE_MIN, 8.0);
    draw_point(30.5, 60.5, 0.0);
    glPointParameterf(GL_POINT_SIZE_MIN, 0.0);
    glPointParameterf(GL_POINT_SIZE_MAX, 256.0);
    glPointSize(12.0);
    draw_point(70.5, 95.5, 0.0);
    glPointSize(8.0);
    draw_point(70.5, 60.5, 0.0);

    glColor4f(0.6, 1.0, 0.3, 1.0);
    let xs = [6.0f32, 15.0, 27.0, 42.0];
    let v = [[xs[0], 22.0f32], [xs[1], 22.0], [xs[2], 22.0], [xs[3], 22.0]];
    glPointSize(2.0);
    glPointParameterf(GL_POINT_SIZE_MAX, 16.0);
    glBindBuffer(GL_ARRAY_BUFFER, s.size_buffer);
    glPointSizePointerOES(GL_FIXED, 2 * size_of::<i32>() as i32, null());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableClientState(GL_POINT_SIZE_ARRAY_OES);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, v.as_ptr().cast());
    glDrawArrays(GL_POINTS, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_POINT_SIZE_ARRAY_OES);
    glPointParameterf(GL_POINT_SIZE_MAX, 256.0);
    for (i, size) in [4.0f32, 8.0, 12.0, 16.0].iter().enumerate() {
        glPointSize(*size);
        draw_point(xs[i] + 50.0, 22.0, 0.0);
    }
}

/// Eye distance of a point at (x, y, z).
fn eye_distance(x: f32, y: f32, z: f32) -> f32 {
    (x * x + y * y + z * z).sqrt()
}

/// 2: quadratic attenuation (0, 0, 1/16): size = 32/sqrt(d^2/16) = 128/d at eye distances ~4, 8, 16.
/// 3: same setup with constant + linear terms through the ES fixed-point API: (2, 0.5, 0).
unsafe fn draw_attenuation(fixed_point: bool) {
    let zs = [-4.0f32, -8.0, -16.0];
    if fixed_point {
        let att = [2i32 << 16, 1 << 15, 0];
        glPointParameterxv(GL_POINT_DISTANCE_ATTENUATION, att.as_ptr());
    } else {
        let att = [0.0f32, 0.0, 1.0 / 16.0];
        glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, att.as_ptr());
    }
    glPointSize(if fixed_point { 24.0 } else { 32.0 });
    glColor4f(0.3, 0.8, 1.0, 1.0);
    for (i, &z) in zs.iter().enumerate() {
        let (ky, kx) = (-z * HALF_H / NEAR, -z * HALF_W / NEAR);
        draw_point(-0.45 * kx, (0.6 - 0.6 * i as f32) * ky, z);
    }
    let none = [1.0f32, 0.0, 0.0];
    glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, none.as_ptr());
    for (i, &z) in zs.iter().enumerate() {
        let (ky, kx) = (-z * HALF_H / NEAR, -z * HALF_W / NEAR);
        let (x, y) = (0.45 * kx, (0.6 - 0.6 * i as f32) * ky);
        // Mirrored position: same distance as the left point
        let d = eye_distance(-x, y, z);
        let k = if fixed_point { 2.0 + 0.5 * d } else { d * d / 16.0 };
        glPointSize((if fixed_point { 24.0 } else { 32.0 }) / k.sqrt());
        draw_point(x, y, z);
    }
}

unsafe fn enable_quadrants(s: &Scene) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s.quadrants);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE as i32);
    glColor4f(1.0, 1.0, 1.0, 1.0);
}

/// 4: sprite with GL_COORD_REPLACE_OES: red top left, green top right, blue bottom left, yellow bottom right.
/// 5: the same with a texture matrix, which must not apply to sprite coordinates.
unsafe fn draw_sprite(s: &Scene, texture_matrix: bool) {
    enable_quadrants(s);
    if texture_matrix {
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glRotatef(90.0, 0.0, 0.0, 1.0);
        glScalef(3.0, 3.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
    }
    glEnable(GL_POINT_SPRITE_OES);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_TRUE as i32);
    glPointSize(32.0);
    draw_point(28.0, 80.0, 0.0);
    glPointSize(16.0);
    draw_point(28.0, 30.0, 0.0);
    glDisable(GL_POINT_SPRITE_OES);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_FALSE as i32);
    if texture_matrix {
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
    }
    draw_sprite_quad(72.0, 80.0, 32.0, 1);
    draw_sprite_quad(72.0, 30.0, 16.0, 1);
}

/// 6: sprites without GL_COORD_REPLACE_OES: the whole point samples the vertex texcoord (0.75, 0.25) -> green.
/// Lower pair: GL_COORD_REPLACE_OES on, but GL_POINT_SPRITE_OES off -> green too.
unsafe fn draw_no_replace(s: &Scene) {
    enable_quadrants(s);
    glTexCoord2f(0.75, 0.25);
    glPointSize(24.0);
    glEnable(GL_POINT_SPRITE_OES);
    draw_point(28.0, 80.0, 0.0);
    glDisable(GL_POINT_SPRITE_OES);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_TRUE as i32);
    draw_point(28.0, 30.0, 0.0);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_FALSE as i32);
    glDisable(GL_TEXTURE_2D);
    glColor4f(0.0, 1.0, 0.0, 1.0);
    draw_point(72.0, 80.0, 0.0);
    draw_point(72.0, 30.0, 0.0);
}

/// 7: two units: quadrants on unit 0 and the soft disc on unit 1, both with GL_COORD_REPLACE_OES ->
/// round colored sprites fading out to the edge. Upper pair: sprites (left) vs quads; lower pair:
/// only unit 1 replaces, unit 0 gets the vertex texcoord (0.25, 0.75) -> a blue disc on both sides.
unsafe fn draw_multitexture(s: &Scene) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    enable_quadrants(s);
    glActiveTexture(GL_TEXTURE1);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s.disc);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE as i32);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_TRUE as i32);
    glActiveTexture(GL_TEXTURE0);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_TRUE as i32);
    glEnable(GL_POINT_SPRITE_OES);
    glPointSize(40.0);
    draw_point(28.0, 82.0, 0.0);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_FALSE as i32);
    glMultiTexCoord2f(GL_TEXTURE0, 0.25, 0.75);
    draw_point(28.0, 32.0, 0.0);
    glDisable(GL_POINT_SPRITE_OES);
    draw_sprite_quad(72.0, 82.0, 40.0, 2);
    let (r, x, y) = (20.0f32, 72.0f32, 32.0f32);
    glBegin(GL_QUADS);
    for c in &CORNERS {
        glMultiTexCoord2f(GL_TEXTURE0, 0.25, 0.75);
        glMultiTexCoord2f(GL_TEXTURE1, c[2], c[3]);
        glVertex2f(x + c[0] * r, y + c[1] * r);
    }
    glEnd();
    glActiveTexture(GL_TEXTURE1);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_FALSE as i32);
    glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
}

fn create_particles() -> Vec<Particle> {
    let mut seed = 12345u32;
    let mut rnd = move || {
        seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
        (seed >> 8) as f32 / 16777216.0
    };
    (0..PARTICLES)
        .map(|_| {
            // Uniform in a ball of radius 2
            let (mut x, mut y, mut z);
            loop {
                x = 2.0 * rnd() - 1.0;
                y = 2.0 * rnd() - 1.0;
                z = 2.0 * rnd() - 1.0;
                if x * x + y * y + z * z <= 1.0 {
                    break;
                }
            }
            Particle {
                pos: [2.0 * x, 2.0 * y, 2.0 * z],
                color: [(128.0 + 127.0 * rnd()) as u8, (64.0 + 191.0 * rnd()) as u8, (255.0 * rnd()) as u8, 96],
                size: 24.0 + 48.0 * rnd(),
            }
        })
        .collect()
}

/// 8: particle field: soft sprites with sizes 24..72 from the point size array, attenuated by
/// distance (0, 0, 1) -> size/d px (at most 20), additive blending, turning.
unsafe fn draw_particles(s: &Scene, angle: f32) {
    glTranslatef(0.0, 0.0, -5.0);
    glRotatef(angle, 0.3, 1.0, 0.0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s.disc);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE as i32);
    glEnable(GL_POINT_SPRITE_OES);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_TRUE as i32);
    let att = [0.0f32, 0.0, 1.0];
    glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, att.as_ptr());
    glPointParameterf(GL_POINT_SIZE_MAX, 20.0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_POINT_SIZE_ARRAY_OES);
    let base = s.particles.as_ptr() as *const u8;
    let stride = size_of::<Particle>() as i32;
    glVertexPointer(3, GL_FLOAT, stride, base.add(offset_of!(Particle, pos)).cast());
    glColorPointer(4, GL_UNSIGNED_BYTE, stride, base.add(offset_of!(Particle, color)).cast());
    glPointSizePointerOES(GL_FLOAT, stride, base.add(offset_of!(Particle, size)).cast());
    glDrawArrays(GL_POINTS, 0, PARTICLES as i32);
    glDisableClientState(GL_POINT_SIZE_ARRAY_OES);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_FALSE as i32);
}

unsafe fn extensions() -> String {
    let ext = glGetString(GL_EXTENSIONS);
    if ext.is_null() {
        String::new()
    } else {
        CStr::from_ptr(ext.cast()).to_string_lossy().into_owned()
    }
}

unsafe fn test_point_api(c: &mut Checks) {
    let (mut f, mut i, mut b) = ([0f32; 4], [0i32; 4], 0u8);
    glGetFloatv(GL_POINT_SIZE_MIN, f.as_mut_ptr());
    check!(c, f[0] == 0.0);
    glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, f.as_mut_ptr());
    check!(c, f[0] == 1.0 && f[1] >= 64.0);
    let max_size = f[1];
    glGetFloatv(GL_POINT_SIZE_MAX, f.as_mut_ptr());
    check!(c, f[0] == max_size);
    glGetFloatv(GL_POINT_FADE_THRESHOLD_SIZE, f.as_mut_ptr());
    check!(c, f[0] == 1.0);
    glGetFloatv(GL_POINT_DISTANCE_ATTENUATION, f.as_mut_ptr());
    check!(c, f[0] == 1.0 && f[1] == 0.0 && f[2] == 0.0);
    check!(c, glIsEnabled(GL_POINT_SPRITE_OES) == 0);
    for unit in 0..3 {
        glActiveTexture(GL_TEXTURE0 + unit);
        glGetTexEnviv(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, i.as_mut_ptr());
        check!(c, i[0] == GL_FALSE as i32);
    }
    glActiveTexture(GL_TEXTURE0);
    check!(c, extensions().contains("GL_OES_point_sprite"));
    check!(c, glGetError() == GL_NO_ERROR);

    // Setting and reading back, float, integer and fixed point
    glPointParameterf(GL_POINT_SIZE_MIN, 2.5);
    glPointParameteri(GL_POINT_SIZE_MAX, 40);
    glPointParameterx(GL_POINT_FADE_THRESHOLD_SIZE, 3 << 16);
    let iatt = [1i32, 2, 3];
    glPointParameteriv(GL_POINT_DISTANCE_ATTENUATION, iatt.as_ptr());
    glGetFloatv(GL_POINT_SIZE_MIN, f.as_mut_ptr());
    check!(c, near(f[0], 2.5));
    glGetIntegerv(GL_POINT_SIZE_MAX, i.as_mut_ptr());
    check!(c, i[0] == 40);
    let mut x = [0i32; 3];
    glGetFixedv(GL_POINT_FADE_THRESHOLD_SIZE, x.as_mut_ptr());
    check!(c, x[0] == 3 << 16);
    glGetFloatv(GL_POINT_DISTANCE_ATTENUATION, f.as_mut_ptr());
    check!(c, f[0] == 1.0 && f[1] == 2.0 && f[2] == 3.0);
    let xatt = [1i32 << 16, 0, 1 << 15];
    glPointParameterxv(GL_POINT_DISTANCE_ATTENUATION, xatt.as_ptr());
    glGetFixedv(GL_POINT_DISTANCE_ATTENUATION, x.as_mut_ptr());
    check!(c, x[0] == 1 << 16 && x[1] == 0 && x[2] == 1 << 15);

    // Sprite state: the enable flag is global, GL_COORD_REPLACE_OES per texture unit
    glEnable(GL_POINT_SPRITE_OES);
    glGetBooleanv(GL_POINT_SPRITE_OES, &mut b);
    check!(c, b == GL_TRUE as u8);
    glActiveTexture(GL_TEXTURE1);
    glTexEnvx(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_TRUE as i32);
    glGetTexEnvxv(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, x.as_mut_ptr());
    check!(c, x[0] == GL_TRUE as i32);
    glActiveTexture(GL_TEXTURE0);
    glGetTexEnviv(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, i.as_mut_ptr());
    check!(c, i[0] == GL_FALSE as i32);
    check!(c, glGetError() == GL_NO_ERROR);

    // Errors leave the state alone
    glPointParameterf(GL_POINT_DISTANCE_ATTENUATION, 1.0);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glPointParameterf(GL_POINT_SIZE_MIN, -1.0);
    check!(c, glGetError() == GL_INVALID_VALUE);
    glPointParameterf(GL_POINT_SIZE, 1.0);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glPointSize(0.0);
    check!(c, glGetError() == GL_INVALID_VALUE);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_TEXTURE_ENV_MODE, GL_REPLACE as i32);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glTexEnvi(GL_TEXTURE_ENV, GL_COORD_REPLACE_OES, GL_TRUE as i32);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glGetFloatv(GL_POINT_SIZE_MIN, f.as_mut_ptr());
    check!(c, near(f[0], 2.5)); // Unchanged by the failed calls

    // Attribute stack: GL_POINT_BIT covers the parameters, the sprite flag and the per-unit replace flag
    glPushAttrib(GL_POINT_BIT);
    glPointParameterf(GL_POINT_SIZE_MIN, 9.0);
    glDisable(GL_POINT_SPRITE_OES);
    glActiveTexture(GL_TEXTURE1);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_FALSE as i32);
    glActiveTexture(GL_TEXTURE0);
    glPopAttrib();
    glGetFloatv(GL_POINT_SIZE_MIN, f.as_mut_ptr());
    check!(c, near(f[0], 2.5));
    check!(c, glIsEnabled(GL_POINT_SPRITE_OES) != 0);
    glActiveTexture(GL_TEXTURE1);
    glGetTexEnviv(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, i.as_mut_ptr());
    check!(c, i[0] == GL_TRUE as i32);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_FALSE as i32);
    glActiveTexture(GL_TEXTURE0);
    glPushAttrib(GL_ENABLE_BIT);
    glDisable(GL_POINT_SPRITE_OES);
    glPointParameterf(GL_POINT_SIZE_MIN, 4.0);
    glPopAttrib();
    check!(c, glIsEnabled(GL_POINT_SPRITE_OES) != 0);
    glGetFloatv(GL_POINT_SIZE_MIN, f.as_mut_ptr());
    check!(c, near(f[0], 4.0));

    // Back to the defaults
    glDisable(GL_POINT_SPRITE_OES);
    glPointParameterf(GL_POINT_SIZE_MIN, 0.0);
    glPointParameterf(GL_POINT_SIZE_MAX, max_size);
    glPointParameterf(GL_POINT_FADE_THRESHOLD_SIZE, 1.0);
    let none = [1.0f32, 0.0, 0.0];
    glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, none.as_ptr());
    check!(c, glGetError() == GL_NO_ERROR);
}

unsafe fn test_point_size_array_api(c: &mut Checks, size_buffer: u32) {
    let (mut i, mut b) = ([0i32; 1], 0u8);
    let mut ptr: *mut c_void = null::<c_void>() as *mut c_void;
    check!(c, glIsEnabled(GL_POINT_SIZE_ARRAY_OES) == 0);
    glGetIntegerv(GL_POINT_SIZE_ARRAY_TYPE_OES, i.as_mut_ptr());
    check!(c, i[0] == GL_FLOAT as i32);
    glGetIntegerv(GL_POINT_SIZE_ARRAY_STRIDE_OES, i.as_mut_ptr());
    check!(c, i[0] == 0);
    glGetIntegerv(GL_POINT_SIZE_ARRAY_BUFFER_BINDING_OES, i.as_mut_ptr());
    check!(c, i[0] == 0);
    glGetPointerv(GL_POINT_SIZE_ARRAY_POINTER_OES, &mut ptr);
    check!(c, ptr.is_null());
    check!(c, extensions().contains("GL_OES_point_size_array"));
    check!(c, glGetError() == GL_NO_ERROR);

    // Pointer, enable flag and the state queries
    static SIZES: [i32; 2] = [1 << 16, 2 << 16];
    glPointSizePointerOES(GL_FIXED, 8, SIZES.as_ptr().cast());
    glEnableClientState(GL_POINT_SIZE_ARRAY_OES);
    glGetBooleanv(GL_POINT_SIZE_ARRAY_OES, &mut b);
    check!(c, b == GL_TRUE as u8);
    glGetIntegerv(GL_POINT_SIZE_ARRAY_TYPE_OES, i.as_mut_ptr());
    check!(c, i[0] == GL_FIXED as i32);
    glGetIntegerv(GL_POINT_SIZE_ARRAY_STRIDE_OES, i.as_mut_ptr());
    check!(c, i[0] == 8);
    glGetPointerv(GL_POINT_SIZE_ARRAY_POINTER_OES, &mut ptr);
    check!(c, ptr as *const c_void == SIZES.as_ptr().cast());

    // From a buffer object the pointer is an offset
    glBindBuffer(GL_ARRAY_BUFFER, size_buffer);
    glPointSizePointerOES(GL_FLOAT, 0, 8usize as *const c_void);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGetIntegerv(GL_POINT_SIZE_ARRAY_BUFFER_BINDING_OES, i.as_mut_ptr());
    check!(c, i[0] == size_buffer as i32);
    check!(c, glGetError() == GL_NO_ERROR);

    // Errors leave the state alone
    glPointSizePointerOES(GL_SHORT, 0, SIZES.as_ptr().cast());
    check!(c, glGetError() == GL_INVALID_ENUM);
    glPointSizePointerOES(GL_FIXED, -4, SIZES.as_ptr().cast());
    check!(c, glGetError() == GL_INVALID_VALUE);
    glGetIntegerv(GL_POINT_SIZE_ARRAY_TYPE_OES, i.as_mut_ptr());
    check!(c, i[0] == GL_FLOAT as i32);

    // Client attribute stack
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glDisableClientState(GL_POINT_SIZE_ARRAY_OES);
    glPointSizePointerOES(GL_FIXED, 4, SIZES.as_ptr().cast());
    glPopClientAttrib();
    check!(c, glIsEnabled(GL_POINT_SIZE_ARRAY_OES) != 0);
    glGetIntegerv(GL_POINT_SIZE_ARRAY_BUFFER_BINDING_OES, i.as_mut_ptr());
    check!(c, i[0] == size_buffer as i32);
    glDisableClientState(GL_POINT_SIZE_ARRAY_OES);
    glPointSizePointerOES(GL_FLOAT, 0, null());
    check!(c, glGetError() == GL_NO_ERROR);
}

unsafe fn setup_scene(checks: &mut Checks) -> Scene {
    let size_buffer = create_size_buffer();
    test_point_api(checks);
    test_point_size_array_api(checks, size_buffer);
    let scene = Scene { quadrants: create_quadrants(), disc: create_disc(), size_buffer, particles: create_particles() };
    glClearColor(0.12, 0.12, 0.15, 1.0);
    scene
}

unsafe fn draw_frame(s: &Scene, angle: f32) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    begin_cell(0, 0, false); draw_clamp(s); end_cell();
    begin_cell(1, 0, true); draw_attenuation(false); end_cell();
    begin_cell(2, 0, true); draw_attenuation(true); end_cell();
    begin_cell(3, 0, false); draw_sprite(s, false); end_cell();
    begin_cell(0, 1, false); draw_sprite(s, true); end_cell();
    begin_cell(1, 1, false); draw_no_replace(s); end_cell();
    begin_cell(2, 1, false); draw_multitexture(s); end_cell();
    begin_cell(3, 1, true); draw_particles(s, angle); end_cell();
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut app = App::new();
    let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };

    print!("c3dgl points\n\n\n\n\n");
    let mut checks = Checks::default();
    let scene = unsafe { setup_scene(&mut checks) };
    print!(
        "{}/{} checks passed\n\nTop screen: left/right of every\npair must match\ntop row:\n- orange squares 12px and 8px\n  (size clamped by min/max),\n  green 4, 8, 12, 16px (array)\n- cyan squares, smaller with\n  distance (attenuation)\n- the same with the ES x API\n- sprites: red top left, green\n  top right, blue bottom left,\n  yellow bottom right\nbottom row:\n- same sprites (texture matrix\n  must be ignored)\n- four green squares\n- soft round 4-color sprite,\n  soft blue disc below\n- turning ball of glowing\n  particles\n\nSTART: exit\n",
        checks.passed(),
        checks.total
    );

    let mut stats = StatsOverlay::new();
    let mut angle = 0.0f32;
    while app.frame() {
        stats.print();
        if app.down(KeyPad::START) {
            break;
        }
        unsafe {
            draw_frame(&scene, angle);
            c3dglSwapBuffers();
        }
        angle += 0.5;
    }
    unsafe {
        glDeleteBuffers(1, &scene.size_buffer);
        glDeleteTextures(1, &scene.disc);
        glDeleteTextures(1, &scene.quadrants);
    }
}
