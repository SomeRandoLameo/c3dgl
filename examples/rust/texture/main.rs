//! Texture features, one cell each on the top screen (4x2 grid, 100x120 px per cell). All cells use
//! an asymmetric "F" texture (white F on blue, red block in the texel corner s = 0, t = 0).
//! Page 1: texture matrix, the same square with texcoords 0..1 in every cell, only the GL_TEXTURE
//! matrix differs. Page 2: texture coordinates: per-vertex q, texcoord array types.
//! Page 3: multitexturing (3 units) and GL_COMBINE. Page 4: mipmaps on a floor that recedes into the
//! distance. Page 5: compressed textures (paletted, ETC1). A switches pages.
//! The expected result is printed on the bottom screen.
#[path = "../common/mod.rs"]
mod common;

use c3dgl::*;
use common::gl::*;
use common::{App, Renderer, StatsOverlay, SCREEN_HEIGHT, SCREEN_WIDTH};
use ctru::prelude::*;

const COLUMNS: i32 = 4;
const ROWS: i32 = 2;
const CELL_W: i32 = SCREEN_WIDTH / COLUMNS;
const CELL_H: i32 = SCREEN_HEIGHT / ROWS;
const MIP_SIZE: usize = 64; // Levels 64x64 .. 1x1 (0..6), PICA stores 64 .. 8 (0..3)

/// Texel (x, y) of the "F" image of size x size, y = 0 at the bottom: 0 = blue background, 1 = white F, 2 = red marker.
fn f_texel(x: usize, y: usize, size: usize) -> usize {
    let (gx, gy) = (x * 16 / size, y * 16 / size);
    let stem = (4..=6).contains(&gx) && (2..=13).contains(&gy);
    let top = (4..=12).contains(&gx) && (11..=13).contains(&gy);
    let middle = (4..=10).contains(&gx) && (7..=8).contains(&gy);
    let marker = gx <= 1 && gy <= 1;
    if marker {
        2
    } else if stem || top || middle {
        1
    } else {
        0
    }
}

const F_COLORS: [[u8; 3]; 3] = [[40, 70, 200], [255, 255, 255], [230, 30, 30]];

/// Solid color per level: red, green, blue, yellow, then magenta/cyan/white for the levels PICA does not store.
const LEVEL_COLORS: [[u8; 3]; 7] = [[220, 40, 40], [40, 200, 40], [50, 80, 230], [230, 220, 40], [220, 40, 220], [40, 220, 220], [255, 255, 255]];

unsafe fn tex_param(pname: u32, value: u32) {
    glTexParameteri(GL_TEXTURE_2D, pname, value as i32);
}

/// 16x16 (or 12x12 for the NPOT version) RGB image, row 0 = t = 0 = bottom: upright "F", red block bottom left.
unsafe fn create_f(size: usize) -> u32 {
    let mut packed = vec![0u8; size * size * 3];
    for y in 0..size {
        for x in 0..size {
            packed[(y * size + x) * 3..][..3].copy_from_slice(&F_COLORS[f_texel(x, y, size)]);
        }
    }
    let mut id = 0;
    glGenTextures(1, &mut id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB as i32, size as i32, size as i32, 0, GL_RGB, GL_UNSIGNED_BYTE, packed.as_ptr().cast());
    tex_param(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    tex_param(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    let wrap = if size == 16 { GL_REPEAT } else { GL_CLAMP_TO_EDGE };
    tex_param(GL_TEXTURE_WRAP_S, wrap);
    tex_param(GL_TEXTURE_WRAP_T, wrap);
    id
}

unsafe fn cell_viewport(column: i32, row: i32) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.2, 1.2, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/// Square of 80x80 px in the cell, texcoords 0..1 counter-clockwise from the bottom left.
unsafe fn draw_cell(column: i32, row: i32, texture: u32) {
    cell_viewport(column, row);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor3f(1.0, 1.0, 1.0);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0, 0.0);
    glVertex2f(-0.8, -0.8);
    glTexCoord2f(1.0, 0.0);
    glVertex2f(0.8, -0.8);
    glTexCoord2f(1.0, 1.0);
    glVertex2f(0.8, 0.8);
    glTexCoord2f(0.0, 1.0);
    glVertex2f(-0.8, 0.8);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

/// Set the texture matrix for the next cell (GL_TEXTURE mode), back to modelview afterwards.
unsafe fn texture_matrix(setup: impl FnOnce()) {
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    setup();
    glMatrixMode(GL_MODELVIEW);
}

unsafe fn reset_texture_matrix() {
    texture_matrix(|| {});
}

// Page 2 ------------------------------------------------------------------------------------------

unsafe fn begin_cell(column: i32, row: i32, texture: u32) {
    cell_viewport(column, row);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor3f(1.0, 1.0, 1.0);
}

/// Trapezoid, bottom edge twice as long as the top. q = edge length makes the mapping
/// perspective correct (projective = true): the F looks like it lies on a floor tilted away.
/// Without q (projective = false) both triangles are mapped affinely and the F bends along the diagonal.
unsafe fn draw_trapezoid(projective: bool) {
    let q = if projective { 2.0 } else { 1.0 };
    glBegin(GL_QUADS);
    glTexCoord4f(0.0, 0.0, 0.0, q);
    glVertex2f(-0.8, -0.8);
    glTexCoord4f(q, 0.0, 0.0, q);
    glVertex2f(0.8, -0.8);
    glTexCoord4f(1.0, 1.0, 0.0, 1.0);
    glVertex2f(0.4, 0.8);
    glTexCoord4f(0.0, 1.0, 0.0, 1.0);
    glVertex2f(-0.4, 0.8);
    glEnd();
}

/// Square from texcoord arrays of the given type; size 4 arrays carry q.
unsafe fn draw_array_square(kind: u32, size: i32, texcoords: *const std::ffi::c_void) {
    static POSITIONS: [f32; 8] = [-0.8, -0.8, 0.8, -0.8, 0.8, 0.8, -0.8, 0.8];
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, POSITIONS.as_ptr().cast());
    glTexCoordPointer(size, kind, 0, texcoords);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

unsafe fn draw_texcoord_page(tex: u32) {
    begin_cell(0, 0, tex);
    draw_trapezoid(true);
    begin_cell(1, 0, tex);
    draw_trapezoid(false);
    begin_cell(2, 0, tex);
    glBegin(GL_QUADS);
    glTexCoord3f(0.0, 0.0, 0.7);
    glVertex2f(-0.8, -0.8);
    glTexCoord3f(1.0, 0.0, 0.7);
    glVertex2f(0.8, -0.8);
    glTexCoord3f(1.0, 1.0, 0.7);
    glVertex2f(0.8, 0.8);
    glTexCoord3f(0.0, 1.0, 0.7);
    glVertex2f(-0.8, 0.8);
    glEnd();
    static SHORTS: [i16; 8] = [0, 0, 2, 0, 2, 2, 0, 2];
    begin_cell(3, 0, tex);
    draw_array_square(GL_SHORT, 2, SHORTS.as_ptr().cast());
    static BYTES: [i8; 4] = [0, 1, 1, 0];
    begin_cell(0, 1, tex);
    draw_array_square(GL_BYTE, 1, BYTES.as_ptr().cast());
    static FIXEDS: [i32; 8] = [0, 0, 0x10000, 0, 0x10000, 0x10000, 0, 0x10000];
    begin_cell(1, 1, tex);
    draw_array_square(GL_FIXED, 2, FIXEDS.as_ptr().cast());
    static FLOATS4: [f32; 16] = [0., 0., 0., 2., 2., 0., 0., 2., 2., 2., 0., 2., 0., 2., 0., 2.];
    begin_cell(2, 1, tex);
    draw_array_square(GL_FLOAT, 4, FLOATS4.as_ptr().cast());
    static DOUBLES: [f64; 8] = [1., 0., 0., 0., 0., 1., 1., 1.];
    begin_cell(3, 1, tex);
    draw_array_square(GL_DOUBLE, 2, DOUBLES.as_ptr().cast());
    glDisable(GL_TEXTURE_2D);
}

// Page 3 ------------------------------------------------------------------------------------------

/// 16x16 GL_LUMINANCE images.
unsafe fn create_luminance(value: impl Fn(usize, usize) -> u8, filter: u32) -> u32 {
    let mut pixels = [0u8; 16 * 16];
    for y in 0..16 {
        for x in 0..16 {
            pixels[y * 16 + x] = value(x, y);
        }
    }
    let mut id = 0;
    glGenTextures(1, &mut id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE as i32, 16, 16, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, pixels.as_ptr().cast());
    tex_param(GL_TEXTURE_MIN_FILTER, filter);
    tex_param(GL_TEXTURE_MAG_FILTER, filter);
    id
}

/// "Normal map" of 4 vertical stripes, normals encoded as RGB: +z, 45 degrees toward +x, +x, 45 degrees toward -x.
unsafe fn create_normal_stripes() -> u32 {
    const NORMALS: [[u8; 3]; 4] = [[128, 128, 255], [218, 128, 218], [255, 128, 128], [38, 128, 218]];
    let mut pixels = [0u8; 8 * 8 * 3];
    for y in 0..8 {
        for x in 0..8 {
            pixels[(y * 8 + x) * 3..][..3].copy_from_slice(&NORMALS[x / 2]);
        }
    }
    let mut id = 0;
    glGenTextures(1, &mut id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB as i32, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.as_ptr().cast());
    tex_param(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    tex_param(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    id
}

struct MultiTextures {
    f: u32,
    checker: u32,
    light: u32,
    ramp_up: u32,
    ramp_right: u32,
    normals: u32,
}

unsafe fn create_multi_textures(f: u32) -> MultiTextures {
    MultiTextures {
        f,
        checker: create_luminance(|x, y| if (x / 2 + y / 2) % 2 == 1 { 255 } else { 110 }, GL_NEAREST),
        light: create_luminance(
            |x, y| {
                let (dx, dy) = (x as f32 - 7.5, y as f32 - 7.5);
                let v = 1.0 - (dx * dx + dy * dy) / 120.0;
                (if v > 0.15 { v * 255.0 } else { 0.15 * 255.0 }) as u8
            },
            GL_LINEAR,
        ),
        ramp_up: create_luminance(|_, y| (40 + y * 14) as u8, GL_LINEAR),
        ramp_right: create_luminance(|x, _| (40 + x * 14) as u8, GL_LINEAR),
        normals: create_normal_stripes(),
    }
}

/// Bind `texture` to `unit` with mode `mode` (0: disable the unit).
unsafe fn set_unit(unit: u32, texture: u32, mode: u32) {
    glActiveTexture(GL_TEXTURE0 + unit);
    if texture == 0 {
        glDisable(GL_TEXTURE_2D);
    } else {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode as i32);
    }
    glActiveTexture(GL_TEXTURE0);
}

unsafe fn reset_units() {
    for unit in 0..3 {
        glActiveTexture(GL_TEXTURE0 + unit);
        glDisable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE as i32);
        glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0);
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
    }
    glActiveTexture(GL_TEXTURE0);
}

unsafe fn tex_env(pname: u32, value: u32) {
    glTexEnvi(GL_TEXTURE_ENV, pname, value as i32);
}

/// Square with the same texcoords 0..1 on all three units; `alpha_ramp`: vertex alpha 0 left, 1 right.
unsafe fn multi_square(column: i32, row: i32, alpha_ramp: bool) {
    cell_viewport(column, row);
    const CORNERS: [[f32; 2]; 4] = [[0., 0.], [1., 0.], [1., 1.], [0., 1.]];
    glBegin(GL_QUADS);
    for c in &CORNERS {
        glColor4f(1.0, 1.0, 1.0, if alpha_ramp { c[0] } else { 1.0 });
        for unit in 0..3 {
            glMultiTexCoord2f(GL_TEXTURE0 + unit, c[0], c[1]);
        }
        glVertex2f(-0.8 + 1.6 * c[0], -0.8 + 1.6 * c[1]);
    }
    glEnd();
    glColor4f(1.0, 1.0, 1.0, 1.0);
}

unsafe fn draw_multitexture_page(t: &MultiTextures) {
    // F times a round light: bright in the center, dark corners
    reset_units();
    set_unit(0, t.f, GL_MODULATE);
    set_unit(1, t.light, GL_MODULATE);
    multi_square(0, 0, false);

    // GL_ADD with a luminance checker: washed out F with white checker squares
    reset_units();
    set_unit(0, t.f, GL_MODULATE);
    set_unit(1, t.checker, GL_ADD);
    multi_square(1, 0, false);

    // GL_COMBINE INTERPOLATE by vertex alpha: F on the left, checker on the right
    reset_units();
    set_unit(0, t.f, GL_MODULATE);
    set_unit(1, t.checker, GL_COMBINE);
    glActiveTexture(GL_TEXTURE1);
    tex_env(GL_COMBINE_RGB, GL_INTERPOLATE);
    tex_env(GL_SRC0_RGB, GL_TEXTURE);
    tex_env(GL_SRC1_RGB, GL_PREVIOUS);
    tex_env(GL_SRC2_RGB, GL_PRIMARY_COLOR);
    tex_env(GL_OPERAND2_RGB, GL_SRC_ALPHA);
    tex_env(GL_COMBINE_ALPHA, GL_REPLACE);
    tex_env(GL_SRC0_ALPHA, GL_CONSTANT);
    glActiveTexture(GL_TEXTURE0);
    multi_square(2, 0, true);
    glActiveTexture(GL_TEXTURE1); // Back to the defaults for the next cells
    tex_env(GL_SRC0_RGB, GL_TEXTURE);
    tex_env(GL_SRC1_RGB, GL_PREVIOUS);
    tex_env(GL_SRC2_RGB, GL_CONSTANT);
    tex_env(GL_SRC0_ALPHA, GL_TEXTURE);
    tex_env(GL_COMBINE_ALPHA, GL_MODULATE);
    glActiveTexture(GL_TEXTURE0);

    // DOT3 with the normal stripes against a constant light direction
    reset_units();
    set_unit(0, t.normals, GL_COMBINE);
    let light = [0.5f32, 0.5, 1.0, 1.0];
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, light.as_ptr());
    tex_env(GL_COMBINE_RGB, GL_DOT3_RGB);
    tex_env(GL_SRC0_RGB, GL_TEXTURE);
    tex_env(GL_SRC1_RGB, GL_CONSTANT);
    multi_square(3, 0, false);
    tex_env(GL_SRC1_RGB, GL_PREVIOUS);

    // SUBTRACT 0.5 with RGB scale 2
    reset_units();
    set_unit(0, t.f, GL_COMBINE);
    let half = [0.5f32, 0.5, 0.5, 1.0];
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, half.as_ptr());
    tex_env(GL_COMBINE_RGB, GL_SUBTRACT);
    tex_env(GL_SRC1_RGB, GL_CONSTANT);
    glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 2.0);
    multi_square(0, 1, false);
    tex_env(GL_SRC1_RGB, GL_PREVIOUS);
    tex_env(GL_COMBINE_RGB, GL_MODULATE);

    // Three units
    reset_units();
    set_unit(0, t.f, GL_MODULATE);
    set_unit(1, t.ramp_up, GL_MODULATE);
    set_unit(2, t.ramp_right, GL_MODULATE);
    multi_square(1, 1, false);

    // Unit 1 only, with its own texture matrix
    reset_units();
    set_unit(1, t.f, GL_MODULATE);
    glActiveTexture(GL_TEXTURE1);
    glMatrixMode(GL_TEXTURE);
    glTranslatef(0.0, 1.0, 0.0);
    glScalef(1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glActiveTexture(GL_TEXTURE0);
    multi_square(2, 1, false);

    // Array texcoords per unit
    reset_units();
    set_unit(0, t.f, GL_MODULATE);
    set_unit(1, t.checker, GL_MODULATE);
    glViewport(3 * CELL_W, 0, CELL_W, CELL_H);
    static POSITIONS: [f32; 8] = [-0.8, -0.8, 0.8, -0.8, 0.8, 0.8, -0.8, 0.8];
    static TC0: [f32; 8] = [0., 0., 1., 0., 1., 1., 0., 1.];
    static TC1: [f32; 8] = [0., 0., 4., 0., 4., 4., 0., 4.];
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, POSITIONS.as_ptr().cast());
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, TC0.as_ptr().cast());
    glClientActiveTexture(GL_TEXTURE1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, TC1.as_ptr().cast());
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTexture(GL_TEXTURE0);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    reset_units();
}

// Page 4 ------------------------------------------------------------------------------------------

unsafe fn create_level_colors(levels: usize) -> u32 {
    let mut id = 0;
    glGenTextures(1, &mut id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for level in 0..levels {
        let size = MIP_SIZE >> level;
        let pixels: Vec<u8> = (0..size * size).flat_map(|_| LEVEL_COLORS[level]).collect();
        glTexImage2D(GL_TEXTURE_2D, level as i32, GL_RGB as i32, size as i32, size as i32, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.as_ptr().cast());
    }
    id
}

/// 1 px black/white checker with GL_GENERATE_MIPMAP (or without mipmaps).
unsafe fn create_fine_checker(generate: bool, rgb565: bool) -> u32 {
    let mut pixels = vec![0u8; MIP_SIZE * MIP_SIZE * 3];
    let mut pixels565 = vec![0u16; MIP_SIZE * MIP_SIZE];
    for y in 0..MIP_SIZE {
        for x in 0..MIP_SIZE {
            let light = (x + y) % 2 == 0;
            pixels[(y * MIP_SIZE + x) * 3..][..3].fill(if light { 255 } else { 0 });
            pixels565[y * MIP_SIZE + x] = if light { 0xFFFF } else { 0x001F }; // White / blue
        }
    }
    let mut id = 0;
    glGenTextures(1, &mut id);
    glBindTexture(GL_TEXTURE_2D, id);
    if generate {
        tex_param(GL_GENERATE_MIPMAP, GL_TRUE);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    let size = MIP_SIZE as i32;
    if rgb565 {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB as i32, size, size, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, pixels565.as_ptr().cast());
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB as i32, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.as_ptr().cast());
    }
    tex_param(GL_TEXTURE_MIN_FILTER, if generate { GL_LINEAR_MIPMAP_LINEAR } else { GL_NEAREST });
    id
}

/// Green texture with GL_GENERATE_MIPMAP, then level 0 overwritten with red by glTexSubImage2D: all levels red.
unsafe fn create_regenerated() -> u32 {
    let size = MIP_SIZE as i32;
    let green: Vec<u8> = (0..MIP_SIZE * MIP_SIZE).flat_map(|_| [40u8, 200, 40]).collect();
    let red: Vec<u8> = (0..MIP_SIZE * MIP_SIZE).flat_map(|_| [220u8, 40, 40]).collect();
    let mut id = 0;
    glGenTextures(1, &mut id);
    glBindTexture(GL_TEXTURE_2D, id);
    tex_param(GL_GENERATE_MIPMAP, GL_TRUE);
    tex_param(GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB as i32, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, green.as_ptr().cast());
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, size, GL_RGB, GL_UNSIGNED_BYTE, red.as_ptr().cast());
    id
}

struct MipTextures {
    levels: u32,
    incomplete: u32,
    checker_mip: u32,
    checker_flat: u32,
    regenerated: u32,
    checker565: u32,
}

unsafe fn create_mip_textures() -> MipTextures {
    MipTextures {
        levels: create_level_colors(7),
        incomplete: create_level_colors(3), // Levels 0..2 only
        checker_mip: create_fine_checker(true, false),
        checker_flat: create_fine_checker(false, false),
        regenerated: create_regenerated(),
        checker565: create_fine_checker(true, true),
    }
}

/// Floor from z = -1 to z = -40 in a perspective view, texture repeated 4x across and 40x along it.
unsafe fn draw_floor(column: i32, row: i32, texture: u32, min_filter: u32) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.1, 0.1, -0.12, 0.12, 0.1, 100.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    if min_filter != 0 {
        tex_param(GL_TEXTURE_MIN_FILTER, min_filter);
    }
    tex_param(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glColor3f(1.0, 1.0, 1.0);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0, 0.0);
    glVertex3f(-2.0, -1.0, -1.0);
    glTexCoord2f(4.0, 0.0);
    glVertex3f(2.0, -1.0, -1.0);
    glTexCoord2f(4.0, 40.0);
    glVertex3f(2.0, -1.0, -40.0);
    glTexCoord2f(0.0, 40.0);
    glVertex3f(-2.0, -1.0, -40.0);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

unsafe fn draw_mipmap_page(t: &MipTextures) {
    draw_floor(0, 0, t.levels, GL_LINEAR_MIPMAP_NEAREST);
    draw_floor(1, 0, t.levels, GL_LINEAR_MIPMAP_LINEAR);
    draw_floor(2, 0, t.levels, GL_LINEAR);
    draw_floor(3, 0, t.incomplete, GL_LINEAR_MIPMAP_NEAREST);
    draw_floor(0, 1, t.checker_mip, 0);
    draw_floor(1, 1, t.checker_flat, 0);
    draw_floor(2, 1, t.regenerated, 0);
    draw_floor(3, 1, t.checker565, 0);
}

// Page 5 ------------------------------------------------------------------------------------------

/// "F" as a paletted texture: palette entries 0..2 for background, F and marker (entry_size bytes
/// each), then the 4- or 8-bit indices of all texels without row padding.
unsafe fn create_paletted_f(format: u32, size: usize, entries: &[u8], entry_size: usize) -> u32 {
    let four_bit = format <= GL_PALETTE4_RGB5_A1_OES;
    let palette_bytes = (if four_bit { 16 } else { 256 }) * entry_size;
    let mut data = vec![0u8; 256 * 4 + 16 * 16];
    data[..3 * entry_size].copy_from_slice(&entries[..3 * entry_size]);
    for i in 0..size * size {
        let index = f_texel(i % size, i / size, size) as u8;
        if four_bit {
            data[palette_bytes + i / 2] |= index << if i & 1 == 1 { 0 } else { 4 }; // First texel in the high nibble
        } else {
            data[palette_bytes + i] = index;
        }
    }
    let image_size = palette_bytes + if four_bit { (size * size + 1) / 2 } else { size * size };
    let mut id = 0;
    glGenTextures(1, &mut id);
    glBindTexture(GL_TEXTURE_2D, id);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, format, size as i32, size as i32, 0, image_size as i32, data.as_ptr().cast());
    tex_param(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    tex_param(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    id
}

/// All MIP_SIZE levels in one paletted image (level = -6): palette entry n is the color of level n.
unsafe fn create_paletted_levels() -> u32 {
    const LEVELS: usize = 7;
    let mut data = vec![0u8; 16 * 2 + (MIP_SIZE * MIP_SIZE * 4 / 3 + 16) / 2];
    for (level, c) in LEVEL_COLORS.iter().enumerate() {
        let entry = ((c[0] as u16 >> 3) << 11) | ((c[1] as u16 >> 3) << 6) | ((c[2] as u16 >> 3) << 1) | 1;
        data[level * 2..level * 2 + 2].copy_from_slice(&entry.to_ne_bytes());
    }
    let mut offset = 16 * 2;
    for level in 0..LEVELS {
        let texels = (MIP_SIZE >> level) * (MIP_SIZE >> level);
        for i in 0..texels {
            data[offset + i / 2] |= (level as u8) << if i & 1 == 1 { 0 } else { 4 };
        }
        offset += (texels + 1) / 2;
    }
    let mut id = 0;
    glGenTextures(1, &mut id);
    glBindTexture(GL_TEXTURE_2D, id);
    glCompressedTexImage2D(GL_TEXTURE_2D, -(LEVELS as i32 - 1), GL_PALETTE4_RGB5_A1_OES, MIP_SIZE as i32, MIP_SIZE as i32, 0, offset as i32, data.as_ptr().cast());
    id
}

/// ETC1 block in individual mode: two sub-blocks with 4-bit colors (columns 0-1 and 2-3, or with
/// flip rows 0-1 and 2-3), modifier table 0 with every pixel at +2.
fn etc1_block(out: &mut [u8], c1: [u8; 3], c2: [u8; 3], flip: bool) {
    for i in 0..3 {
        out[i] = (((c1[i] as u32 * 15 + 127) / 255) << 4 | ((c2[i] as u32 * 15 + 127) / 255)) as u8;
    }
    out[3] = flip as u8;
    out[4..8].fill(0);
}

/// "F" in ETC1 with one solid color per 4x4 block, size a multiple of 4. Block (0, 0) is red at the
/// bottom (rows 0-1) and yellow above, block (1, 0) red left and yellow right. The bottom right
/// block is light gray with a black L along its bottom and right edges (per-pixel modifiers of table 7).
unsafe fn create_etc1_f(size: usize) -> u32 {
    const RED: [u8; 3] = [230, 30, 30];
    const YELLOW: [u8; 3] = [230, 220, 40];
    const GRAY: [u8; 3] = [128, 128, 128];
    let blocks = size / 4;
    let mut data = vec![0u8; 8 * 8 * 8];
    for by in 0..blocks {
        for bx in 0..blocks {
            let block = &mut data[(by * blocks + bx) * 8..][..8];
            let color = F_COLORS[f_texel(bx * 4 + 2, by * 4 + 2, size)];
            etc1_block(block, color, color, false);
            if by == 0 && bx == 0 {
                etc1_block(block, RED, YELLOW, true);
            }
            if by == 0 && bx == 1 {
                etc1_block(block, RED, YELLOW, false);
            }
            if by == 0 && bx == blocks - 1 {
                etc1_block(block, GRAY, GRAY, false);
                block[3] = 7 << 5 | 7 << 2; // Tables 7: +47, +183, -47, -183
                // Pixel (x, y) is bit 4x + y of both index halves; index 3 (-183) is black, 0 (+47) light gray
                let mut bits = 0u32;
                for x in 0..4 {
                    for y in 0..4 {
                        if x == 3 || y == 0 {
                            bits |= 1 << (4 * x + y);
                        }
                    }
                }
                block[4] = (bits >> 8) as u8;
                block[6] = (bits >> 8) as u8;
                block[5] = bits as u8;
                block[7] = bits as u8;
            }
        }
    }
    let mut id = 0;
    glGenTextures(1, &mut id);
    glBindTexture(GL_TEXTURE_2D, id);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, size as i32, size as i32, 0, (blocks * blocks * 8) as i32, data.as_ptr().cast());
    tex_param(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    tex_param(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    id
}

/// ETC1 levels 64x64 .. 1x1, each a solid level color.
unsafe fn create_etc1_levels() -> u32 {
    let mut data = vec![0u8; 16 * 16 * 8];
    let mut id = 0;
    glGenTextures(1, &mut id);
    glBindTexture(GL_TEXTURE_2D, id);
    for level in 0..7 {
        let size = MIP_SIZE >> level;
        let blocks = (size + 3) / 4;
        for i in 0..blocks * blocks {
            etc1_block(&mut data[i * 8..][..8], LEVEL_COLORS[level], LEVEL_COLORS[level], false);
        }
        glCompressedTexImage2D(GL_TEXTURE_2D, level as i32, GL_ETC1_RGB8_OES, size as i32, size as i32, 0, (blocks * blocks * 8) as i32, data.as_ptr().cast());
    }
    id
}

struct CompressedTextures {
    rgb8: u32,
    rgba4_npot: u32,
    rgba8_alpha: u32,
    palette_levels: u32,
    etc1: u32,
    etc1_npot: u32,
    etc1_levels: u32,
}

unsafe fn create_compressed_textures() -> CompressedTextures {
    let rgb8: Vec<u8> = F_COLORS.iter().flatten().copied().collect();
    let rgba4: Vec<u8> = [0x24CFu16, 0xFFFF, 0xE22F].iter().flat_map(|v| v.to_ne_bytes()).collect();
    let rgba8: [u8; 12] = [40, 70, 200, 0, 255, 255, 255, 255, 230, 30, 30, 255];
    CompressedTextures {
        rgb8: create_paletted_f(GL_PALETTE4_RGB8_OES, 16, &rgb8, 3),
        rgba4_npot: create_paletted_f(GL_PALETTE4_RGBA4_OES, 13, &rgba4, 2),
        rgba8_alpha: create_paletted_f(GL_PALETTE8_RGBA8_OES, 16, &rgba8, 4),
        palette_levels: create_paletted_levels(),
        etc1: create_etc1_f(32),
        etc1_npot: create_etc1_f(28), // 7x7 blocks, padded to 32
        etc1_levels: create_etc1_levels(),
    }
}

unsafe fn draw_compressed_page(t: &CompressedTextures) {
    reset_texture_matrix();
    draw_cell(0, 0, t.rgb8);
    draw_cell(1, 0, t.rgba4_npot);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5);
    draw_cell(2, 0, t.rgba8_alpha);
    glDisable(GL_ALPHA_TEST);
    draw_floor(3, 0, t.palette_levels, GL_LINEAR_MIPMAP_NEAREST);
    draw_cell(0, 1, t.etc1);
    texture_matrix(|| {
        glTranslatef(0.25, 1.0, 0.0);
        glScalef(1.0, -1.0, 1.0);
    });
    glBindTexture(GL_TEXTURE_2D, t.etc1);
    tex_param(GL_TEXTURE_WRAP_S, GL_REPEAT);
    draw_cell(1, 1, t.etc1);
    reset_texture_matrix();
    draw_cell(2, 1, t.etc1_npot);
    draw_floor(3, 1, t.etc1_levels, GL_LINEAR_MIPMAP_NEAREST);
}

fn print_page(page: usize) {
    print!("\x1b[2J\x1b[1;1Hc3dgl texture test, page {}/5\n\n\n\n\n", page + 1);
    match page {
        4 => print!(
            "Compressed textures. Expected,\nleft to right, top row:\n- PALETTE4_RGB8: upright F, red\n  block bottom left\n- PALETTE4_RGBA4 13x13: same F\n- PALETTE8_RGBA8, alpha test:\n  white F and red block, no blue\n- PALETTE4_RGB5_A1 mipmaps: red,\n  green, blue, yellow bands\nbottom row (ETC1, blocky F):\n- upright F; bottom left: red\n  stripe under yellow, then red|\n  yellow; bottom right: gray with\n  black L at bottom/right edge\n- matrix: F upside down, shifted\n  left by a quarter\n- NPOT 28x28: upright F\n- ETC1 mipmaps: color bands\n"
        ),
        0 => print!(
            "Texture matrix. Expected on the\ntop screen, left to right, top row:\n- identity: upright white F,\n  red block bottom left\n- translate s by 0.25: F shifted\n  LEFT by a quarter, wrapping\n- scale 2: 2x2 small F tiles\n- rotate 90 around the center:\n  F lying on its back, rotated\n  counter-clockwise (red block\n  bottom right)\nbottom row:\n- scrolling diagonally\n- projective q = 2: the bottom\n  left quarter of the F, 2x\n- projective q = 1 + s: F corner\n  in perspective, texels wider\n  to the right\n- NPOT 12x12, scale -1 in t:\n  F upside down\n"
        ),
        3 => print!(
            "Mipmaps on a floor. Expected,\nleft to right, top row:\n- MIPMAP_NEAREST: color bands red,\n  green, blue, yellow (far)\n- MIPMAP_LINEAR: same, blended\n- GL_LINEAR: only level 0 (red)\n- incomplete levels: texturing\n  off, plain white floor\nbottom row:\n- generated mipmaps: checker\n  fades to smooth gray\n- no mipmaps: flickering moire\n  in the distance\n- regenerated after\n  glTexSubImage2D: all red\n- RGB565 generated: blue/white\n  fades to smooth light blue\n"
        ),
        2 => print!(
            "Multitexturing. Expected,\nleft to right, top row:\n- F x round light: F bright in\n  the center, dark corners\n- ADD checker: F washed out,\n  white checker squares\n- INTERPOLATE by vertex alpha:\n  F left, checker right\n- DOT3: white, gray, black,\n  gray vertical stripes\nbottom row:\n- SUBTRACT 0.5, scale 2: white F\n  on dark blue, red block\n- 3 units: F dark bottom left,\n  bright top right\n- unit 1 only, own texture\n  matrix: F upside down\n- array texcoords per unit:\n  F with a fine checker on it\n"
        ),
        _ => print!(
            "Texture coordinates. Expected,\nleft to right, top row:\n- trapezoid with q: F on a floor\n  tilted away, bars straight\n- same without q: F bent along\n  the diagonal (affine)\n- glTexCoord3f: plain upright F\n- GL_SHORT 0..2: 2x2 F tiles\nbottom row:\n- GL_BYTE size 1: the bottom\n  texel row stretched: red\n  stripe left, rest blue\n- GL_FIXED: plain upright F\n- size 4, (2s, 2t, q = 2): plain\n  upright F (2x2 tiles = q lost)\n- GL_DOUBLE: F mirrored left-right\n"
        ),
    }
    print!("\nA: next page   START: exit\n");
}

unsafe fn draw_texture_matrix_page(tex_pot: u32, tex_npot: u32, time: f32) {
    reset_texture_matrix();
    draw_cell(0, 0, tex_pot);
    texture_matrix(|| glTranslatef(0.25, 0.0, 0.0));
    draw_cell(1, 0, tex_pot);
    texture_matrix(|| glScalef(2.0, 2.0, 1.0));
    draw_cell(2, 0, tex_pot);
    texture_matrix(|| {
        glTranslatef(0.5, 0.5, 0.0);
        glRotatef(-90.0, 0.0, 0.0, 1.0);
        glTranslatef(-0.5, -0.5, 0.0);
    });
    draw_cell(3, 0, tex_pot);
    texture_matrix(|| glTranslatef(time, time * 0.5, 0.0));
    draw_cell(0, 1, tex_pot);
    texture_matrix(|| {
        let m = [1.0f32, 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 2.];
        glLoadMatrixf(m.as_ptr());
    });
    draw_cell(1, 1, tex_pot);
    texture_matrix(|| {
        let m = [1.0f32, 0., 0., 1., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.];
        glLoadMatrixf(m.as_ptr());
    });
    draw_cell(2, 1, tex_pot);
    texture_matrix(|| {
        glTranslatef(0.0, 1.0, 0.0);
        glScalef(1.0, -1.0, 1.0);
    });
    draw_cell(3, 1, tex_npot);
    reset_texture_matrix();
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut app = App::new();
    let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };

    let (tex_pot, tex_npot, multi, mips, compressed) = unsafe {
        let tex_pot = create_f(16);
        let tex_npot = create_f(12);
        let multi = create_multi_textures(tex_pot);
        let mips = create_mip_textures();
        let compressed = create_compressed_textures();
        glClearColor(0.12, 0.12, 0.15, 1.0);
        (tex_pot, tex_npot, multi, mips, compressed)
    };

    let (mut time, mut page) = (0.0f32, 0usize);
    print_page(page);
    let mut stats = StatsOverlay::new();
    while app.frame() {
        stats.print();
        if app.down(KeyPad::START) {
            break;
        }
        if app.down(KeyPad::A) {
            page = (page + 1) % 5;
            print_page(page);
        }
        unsafe {
            glClear(GL_COLOR_BUFFER_BIT);
            match page {
                4 => draw_compressed_page(&compressed),
                3 => draw_mipmap_page(&mips),
                2 => draw_multitexture_page(&multi),
                1 => draw_texcoord_page(tex_pot),
                _ => {
                    draw_texture_matrix_page(tex_pot, tex_npot, time);
                    time += 1.0 / 240.0;
                }
            }
            c3dglSwapBuffers();
        }
    }
    unsafe {
        glDeleteTextures(1, &tex_pot);
        glDeleteTextures(1, &tex_npot);
    }
}
