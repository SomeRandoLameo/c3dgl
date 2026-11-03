//! Lighting. The top screen shows one lit scene per cell (4x2 grid, 100x120 px per cell), the
//! bottom screen the result of the non-visual checks (glGetLight, glGetMaterial, errors, color
//! material, attribute stack) and what each cell must look like.
#[macro_use]
#[path = "../common/mod.rs"]
mod common;

use c3dgl::*;
use common::gl::*;
use common::{App, Checks, Renderer, StatsOverlay, SCREEN_HEIGHT, SCREEN_WIDTH};
use ctru::prelude::*;

const COLUMNS: i32 = 4;
const ROWS: i32 = 2;
const CELL_W: i32 = SCREEN_WIDTH / COLUMNS;
const CELL_H: i32 = SCREEN_HEIGHT / ROWS;

fn near(a: f32, b: f32) -> bool {
    (a - b).abs() < 1e-4
}

fn near4(v: &[f32; 4], a: f32, b: f32, c: f32, d: f32) -> bool {
    near(v[0], a) && near(v[1], b) && near(v[2], c) && near(v[3], d)
}

//----------------------------------------------------------------------------------
// Geometry
//----------------------------------------------------------------------------------

/// Unit sphere for glDrawElements: positions = normals, colors from red (top) to blue (bottom).
struct Sphere {
    positions: Vec<f32>,
    colors: Vec<u8>,
    indices: Vec<u16>,
}

impl Sphere {
    fn new(slices: usize, stacks: usize) -> Sphere {
        let pi = std::f32::consts::PI;
        let mut s = Sphere { positions: vec![], colors: vec![], indices: vec![] };
        for i in 0..=stacks {
            let phi = pi * i as f32 / stacks as f32;
            for j in 0..=slices {
                let theta = 2.0 * pi * j as f32 / slices as f32;
                s.positions.extend([phi.sin() * theta.sin(), phi.cos(), phi.sin() * theta.cos()]);
                let t = (255 * i / stacks) as u8;
                s.colors.extend([255 - t, 40, t, 255]);
            }
        }
        for i in 0..stacks {
            for j in 0..slices {
                let a = (i * (slices + 1) + j) as u16;
                let b = a + slices as u16 + 1;
                s.indices.extend([a, b, a + 1, a + 1, b, b + 1]);
            }
        }
        s
    }

    /// Through vertex arrays: GL_NORMAL_ARRAY (+ GL_COLOR_ARRAY with colors).
    unsafe fn draw_arrays(&self, colors: bool) {
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, self.positions.as_ptr().cast());
        glNormalPointer(GL_FLOAT, 0, self.positions.as_ptr().cast());
        if colors {
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, self.colors.as_ptr().cast());
        }
        glDrawElements(GL_TRIANGLES, self.indices.len() as i32, GL_UNSIGNED_SHORT, self.indices.as_ptr().cast());
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
    }

    /// Through immediate mode (glNormal3fv + glVertex3fv per vertex).
    unsafe fn draw_immediate(&self) {
        glBegin(GL_TRIANGLES);
        for &index in &self.indices {
            let p = self.positions[index as usize * 3..].as_ptr();
            glNormal3fv(p);
            glVertex3fv(p);
        }
        glEnd();
    }
}

/// n x n quads in the z = 0 plane from (-size, -size) to (size, size), normal +z. Lighting is per
/// vertex, so local light effects need the tessellation.
unsafe fn draw_grid(size: f32, n: i32) {
    glNormal3f(0.0, 0.0, 1.0);
    for y in 0..n {
        glBegin(GL_QUAD_STRIP);
        for x in 0..=n {
            let px = -size + 2.0 * size * x as f32 / n as f32;
            glVertex2f(px, -size + 2.0 * size * (y + 1) as f32 / n as f32);
            glVertex2f(px, -size + 2.0 * size * y as f32 / n as f32);
        }
        glEnd();
    }
}

//----------------------------------------------------------------------------------
// Cells
//----------------------------------------------------------------------------------

/// Cell (column, row from the top) as viewport; x -1..1, y -1.2..1.2 (square pixels), z = 2 is
/// nearest. The lighting state of every cell is undone by the attribute stack (end_cell).
unsafe fn begin_cell(column: i32, row: i32) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.2, 1.2, -2.0, 2.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glPushAttrib(GL_LIGHTING_BIT | GL_ENABLE_BIT | GL_TRANSFORM_BIT | GL_CURRENT_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

unsafe fn end_cell() {
    glPopAttrib();
}

unsafe fn set_light(light: u32, pname: u32, a: f32, b: f32, c: f32, d: f32) {
    let v = [a, b, c, d];
    glLightfv(light, pname, v.as_ptr());
}

unsafe fn set_material(pname: u32, a: f32, b: f32, c: f32, d: f32) {
    let v = [a, b, c, d];
    glMaterialfv(GL_FRONT_AND_BACK, pname, v.as_ptr());
}

/// 1: red diffuse sphere, directional light from the upper left.
unsafe fn draw_diffuse(s: &Sphere) {
    glEnable(GL_LIGHT0);
    set_light(GL_LIGHT0, GL_POSITION, -1.0, 1.0, 1.0, 0.0);
    set_material(GL_DIFFUSE, 0.9, 0.15, 0.1, 1.0);
    glScalef(0.8, 0.8, 0.8);
    glEnable(GL_NORMALIZE);
    s.draw_arrays(false);
}

/// 2: blue sphere with a white specular highlight in the upper left.
unsafe fn draw_specular(s: &Sphere) {
    glEnable(GL_LIGHT0);
    set_light(GL_LIGHT0, GL_POSITION, -1.0, 1.0, 1.0, 0.0);
    set_material(GL_DIFFUSE, 0.1, 0.25, 0.8, 1.0);
    set_material(GL_SPECULAR, 1.0, 1.0, 1.0, 1.0);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 40.0);
    glScalef(0.8, 0.8, 0.8);
    glEnable(GL_NORMALIZE);
    s.draw_immediate();
}

/// 3: positional light just above the center of a flat grid, quadratic attenuation: bright center, dark corners.
unsafe fn draw_point_light() {
    glEnable(GL_LIGHT0);
    set_light(GL_LIGHT0, GL_POSITION, 0.0, 0.0, 0.4, 1.0);
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 0.0);
    glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 6.0);
    set_material(GL_DIFFUSE, 1.0, 0.85, 0.4, 1.0);
    draw_grid(0.9, 24);
}

/// 4: spot light from above pointing down at a grid: a lit disc (radius ~0.45), dark outside.
unsafe fn draw_spot_light() {
    glEnable(GL_LIGHT0);
    set_light(GL_LIGHT0, GL_POSITION, 0.0, 0.0, 1.0, 1.0);
    let down = [0.0f32, 0.0, -1.0];
    glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, down.as_ptr());
    glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 25.0);
    glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 4.0);
    set_material(GL_DIFFUSE, 0.3, 1.0, 0.4, 1.0);
    draw_grid(0.9, 32);
}

/// 5: white sphere, red light from the left, green from the right, blue from the top.
unsafe fn draw_three_lights(s: &Sphere) {
    let lights = [GL_LIGHT0, GL_LIGHT1, GL_LIGHT2];
    let dirs = [[-1.0f32, 0.0], [1.0, 0.0], [0.0, 1.0]];
    for i in 0..3 {
        glEnable(lights[i]);
        set_light(lights[i], GL_POSITION, dirs[i][0], dirs[i][1], 0.4, 0.0);
        set_light(lights[i], GL_DIFFUSE, (i == 0) as u8 as f32, (i == 1) as u8 as f32, (i == 2) as u8 as f32, 1.0);
        set_light(lights[i], GL_SPECULAR, 0.0, 0.0, 0.0, 1.0);
    }
    set_material(GL_DIFFUSE, 1.0, 1.0, 1.0, 1.0);
    glScalef(0.8, 0.8, 0.8);
    glEnable(GL_NORMALIZE);
    s.draw_arrays(false);
}

/// 6: GL_COLOR_MATERIAL with a color array: the sphere is red at the top and blue at the bottom,
/// lit from the upper left, with a white highlight.
unsafe fn draw_color_material(s: &Sphere) {
    glEnable(GL_LIGHT0);
    set_light(GL_LIGHT0, GL_POSITION, -1.0, 1.0, 1.0, 0.0);
    set_material(GL_SPECULAR, 0.6, 0.6, 0.6, 1.0);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 20.0);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);
    glScalef(0.8, 0.8, 0.8);
    glEnable(GL_NORMALIZE);
    s.draw_arrays(true);
}

/// Quad from (x0, y0) to (x1, y1) with normal (0, 0, nz), counter-clockwise (front facing) or clockwise.
unsafe fn draw_quad(x0: f32, y0: f32, x1: f32, y1: f32, nz: f32, ccw: bool) {
    glNormal3f(0.0, 0.0, nz);
    glBegin(GL_QUADS);
    if ccw {
        glVertex2f(x0, y0);
        glVertex2f(x1, y0);
        glVertex2f(x1, y1);
        glVertex2f(x0, y1);
    } else {
        glVertex2f(x0, y0);
        glVertex2f(x0, y1);
        glVertex2f(x1, y1);
        glVertex2f(x1, y0);
    }
    glEnd();
}

/// 7: two-sided lighting, light from the viewer. Left quads face the viewer (front material:
/// green), right quads show their back (normal away from the viewer, back material: magenta).
/// Top: two-sided, so the back is lit with the reversed normal (magenta); bottom: one-sided, the
/// back stays dark.
unsafe fn draw_two_sided() {
    glEnable(GL_LIGHT0);
    let (green, magenta) = ([0.2f32, 0.9, 0.2, 1.0], [0.9f32, 0.2, 0.9, 1.0]);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, green.as_ptr());
    glMaterialfv(GL_BACK, GL_DIFFUSE, magenta.as_ptr());
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE as i32);
    draw_quad(-0.85, 0.15, -0.05, 1.0, 1.0, true);
    draw_quad(0.05, 0.15, 0.85, 1.0, -1.0, false);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE as i32);
    draw_quad(-0.85, -1.0, -0.05, -0.15, 1.0, true);
    draw_quad(0.05, -1.0, 0.85, -0.15, -1.0, false);
}

/// 8: flat shading of lit colors on small (glScale 0.4) spheres: top left GL_NORMALIZE and top
/// right GL_RESCALE_NORMAL must look the same (faceted, orange); bottom: neither, the scaled
/// normals are 2.5 times too long and the sphere is washed out.
unsafe fn draw_flat_scaled(s: &Sphere) {
    glEnable(GL_LIGHT0);
    set_light(GL_LIGHT0, GL_POSITION, -1.0, 1.0, 1.0, 0.0);
    set_material(GL_DIFFUSE, 0.9, 0.5, 0.1, 1.0);
    glShadeModel(GL_FLAT);
    let centers = [[-0.48f32, 0.55], [0.48, 0.55], [0.0, -0.6]];
    for (i, center) in centers.iter().enumerate() {
        glDisable(GL_NORMALIZE);
        glDisable(GL_RESCALE_NORMAL);
        if i == 0 {
            glEnable(GL_NORMALIZE);
        }
        if i == 1 {
            glEnable(GL_RESCALE_NORMAL);
        }
        glPushMatrix();
        glTranslatef(center[0], center[1], 0.0);
        glScalef(0.4, 0.4, 0.4);
        s.draw_immediate();
        glPopMatrix();
    }
    glShadeModel(GL_SMOOTH);
}

//----------------------------------------------------------------------------------
// Non-visual checks
//----------------------------------------------------------------------------------

unsafe fn test_defaults(c: &mut Checks) {
    let (mut f, mut v) = ([0f32; 4], 0i32);
    check!(c, glIsEnabled(GL_LIGHTING) == 0 && glIsEnabled(GL_LIGHT0) == 0 && glIsEnabled(GL_COLOR_MATERIAL) == 0);
    check!(c, glIsEnabled(GL_NORMALIZE) == 0 && glIsEnabled(GL_RESCALE_NORMAL) == 0);
    glGetIntegerv(GL_MAX_LIGHTS, &mut v);
    check!(c, v == 8);
    glGetLightfv(GL_LIGHT0, GL_DIFFUSE, f.as_mut_ptr());
    check!(c, near4(&f, 1.0, 1.0, 1.0, 1.0));
    glGetLightfv(GL_LIGHT1, GL_DIFFUSE, f.as_mut_ptr());
    check!(c, near4(&f, 0.0, 0.0, 0.0, 1.0));
    glGetLightfv(GL_LIGHT1, GL_SPECULAR, f.as_mut_ptr());
    check!(c, near4(&f, 0.0, 0.0, 0.0, 1.0));
    glGetLightfv(GL_LIGHT7, GL_AMBIENT, f.as_mut_ptr());
    check!(c, near4(&f, 0.0, 0.0, 0.0, 1.0));
    glGetLightfv(GL_LIGHT0, GL_POSITION, f.as_mut_ptr());
    check!(c, near4(&f, 0.0, 0.0, 1.0, 0.0));
    glGetLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, f.as_mut_ptr());
    check!(c, near(f[0], 0.0) && near(f[1], 0.0) && near(f[2], -1.0));
    glGetLightfv(GL_LIGHT0, GL_SPOT_CUTOFF, f.as_mut_ptr());
    check!(c, near(f[0], 180.0));
    glGetLightfv(GL_LIGHT0, GL_CONSTANT_ATTENUATION, f.as_mut_ptr());
    check!(c, near(f[0], 1.0));
    glGetMaterialfv(GL_FRONT, GL_AMBIENT, f.as_mut_ptr());
    check!(c, near4(&f, 0.2, 0.2, 0.2, 1.0));
    glGetMaterialfv(GL_BACK, GL_DIFFUSE, f.as_mut_ptr());
    check!(c, near4(&f, 0.8, 0.8, 0.8, 1.0));
    glGetMaterialfv(GL_FRONT, GL_EMISSION, f.as_mut_ptr());
    check!(c, near4(&f, 0.0, 0.0, 0.0, 1.0));
    glGetMaterialfv(GL_FRONT, GL_SHININESS, f.as_mut_ptr());
    check!(c, near(f[0], 0.0));
    glGetFloatv(GL_LIGHT_MODEL_AMBIENT, f.as_mut_ptr());
    check!(c, near4(&f, 0.2, 0.2, 0.2, 1.0));
    let mut b = GL_TRUE as u8;
    glGetBooleanv(GL_LIGHT_MODEL_TWO_SIDE, &mut b);
    check!(c, b == GL_FALSE as u8);
    glGetIntegerv(GL_COLOR_MATERIAL_FACE, &mut v);
    check!(c, v == GL_FRONT_AND_BACK as i32);
    glGetIntegerv(GL_COLOR_MATERIAL_PARAMETER, &mut v);
    check!(c, v == GL_AMBIENT_AND_DIFFUSE as i32);
    check!(c, glGetError() == GL_NO_ERROR);
}

unsafe fn test_parameters(c: &mut Checks) {
    let (mut f, mut iv) = ([0f32; 4], [0i32; 4]);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glTranslatef(1.0, 2.0, 3.0);
    glRotatef(90.0, 0.0, 0.0, 1.0); // x -> y
    set_light(GL_LIGHT3, GL_POSITION, 1.0, 0.0, 0.0, 1.0);
    glGetLightfv(GL_LIGHT3, GL_POSITION, f.as_mut_ptr());
    check!(c, near4(&f, 1.0, 3.0, 3.0, 1.0));
    set_light(GL_LIGHT3, GL_POSITION, 1.0, 0.0, 0.0, 0.0); // Directional: no translation
    glGetLightfv(GL_LIGHT3, GL_POSITION, f.as_mut_ptr());
    check!(c, near4(&f, 0.0, 1.0, 0.0, 0.0));
    let dir = [1.0f32, 0.0, 0.0];
    glLightfv(GL_LIGHT3, GL_SPOT_DIRECTION, dir.as_ptr());
    glGetLightfv(GL_LIGHT3, GL_SPOT_DIRECTION, f.as_mut_ptr());
    check!(c, near(f[0], 0.0) && near(f[1], 1.0) && near(f[2], 0.0));
    glPopMatrix();

    let white = [2147483647i32, 0, 2147483647, 2147483647];
    glLightiv(GL_LIGHT3, GL_DIFFUSE, white.as_ptr());
    glGetLightfv(GL_LIGHT3, GL_DIFFUSE, f.as_mut_ptr());
    check!(c, near(f[0], 1.0) && near(f[1], 0.0) && near(f[3], 1.0));
    glGetLightiv(GL_LIGHT3, GL_DIFFUSE, iv.as_mut_ptr());
    check!(c, iv[0] == 2147483647 && iv[1] == 0);
    let pos = [2i32, 3, 4, 1];
    glLoadIdentity();
    glLightiv(GL_LIGHT3, GL_POSITION, pos.as_ptr());
    glGetLightiv(GL_LIGHT3, GL_POSITION, iv.as_mut_ptr());
    check!(c, iv[0] == 2 && iv[1] == 3 && iv[2] == 4 && iv[3] == 1);
    glLighti(GL_LIGHT3, GL_SPOT_CUTOFF, 45);
    glGetLightiv(GL_LIGHT3, GL_SPOT_CUTOFF, iv.as_mut_ptr());
    check!(c, iv[0] == 45);

    let red = [1.0f32, 0.0, 0.0, 1.0];
    glMaterialfv(GL_BACK, GL_EMISSION, red.as_ptr());
    glGetMaterialfv(GL_BACK, GL_EMISSION, f.as_mut_ptr());
    check!(c, near4(&f, 1.0, 0.0, 0.0, 1.0));
    glGetMaterialfv(GL_FRONT, GL_EMISSION, f.as_mut_ptr());
    check!(c, near4(&f, 0.0, 0.0, 0.0, 1.0));
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, red.as_ptr());
    glGetMaterialfv(GL_FRONT, GL_AMBIENT, f.as_mut_ptr());
    check!(c, near4(&f, 1.0, 0.0, 0.0, 1.0));
    glGetMaterialfv(GL_BACK, GL_DIFFUSE, f.as_mut_ptr());
    check!(c, near4(&f, 1.0, 0.0, 0.0, 1.0));
    glMateriali(GL_FRONT, GL_SHININESS, 64);
    glGetMaterialiv(GL_FRONT, GL_SHININESS, iv.as_mut_ptr());
    check!(c, iv[0] == 64);

    // Fixed point variants (OpenGL ES)
    glLightx(GL_LIGHT4, GL_LINEAR_ATTENUATION, 0x8000);
    glGetLightfv(GL_LIGHT4, GL_LINEAR_ATTENUATION, f.as_mut_ptr());
    check!(c, near(f[0], 0.5));
    let half = [0x8000i32, 0x8000, 0x8000, 0x10000];
    glLightModelxv(GL_LIGHT_MODEL_AMBIENT, half.as_ptr());
    let mut x = [0i32; 4];
    glGetFixedv(GL_LIGHT_MODEL_AMBIENT, x.as_mut_ptr());
    check!(c, x[0] == 0x8000 && x[3] == 0x10000);
    glMaterialxv(GL_FRONT_AND_BACK, GL_SPECULAR, half.as_ptr());
    glGetMaterialxv(GL_BACK, GL_SPECULAR, x.as_mut_ptr());
    check!(c, x[1] == 0x8000);
    glLightModelx(GL_LIGHT_MODEL_TWO_SIDE, 1);
    let mut b = GL_FALSE as u8;
    glGetBooleanv(GL_LIGHT_MODEL_TWO_SIDE, &mut b);
    check!(c, b == GL_TRUE as u8);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 0);
    check!(c, glGetError() == GL_NO_ERROR);
}

unsafe fn test_errors(c: &mut Checks) {
    let mut f = [0f32; 4];
    glLightf(GL_LIGHT0 + 8, GL_SPOT_EXPONENT, 1.0);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glLightf(GL_LIGHT0, GL_POSITION, 1.0); // Vector parameter through the scalar call
    check!(c, glGetError() == GL_INVALID_ENUM);
    glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 100.0);
    check!(c, glGetError() == GL_INVALID_VALUE);
    glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 129.0);
    check!(c, glGetError() == GL_INVALID_VALUE);
    glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, -1.0);
    check!(c, glGetError() == GL_INVALID_VALUE);
    glMaterialf(GL_FRONT, GL_SHININESS, 200.0);
    check!(c, glGetError() == GL_INVALID_VALUE);
    glMaterialf(GL_FRONT, GL_DIFFUSE, 1.0);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glGetMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, f.as_mut_ptr());
    check!(c, glGetError() == GL_INVALID_ENUM);
    glColorMaterial(GL_FRONT, GL_SHININESS);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glLightModelf(GL_LIGHT_MODEL_AMBIENT, 1.0);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glGetLightfv(GL_LIGHT0, GL_SHININESS, f.as_mut_ptr());
    check!(c, glGetError() == GL_INVALID_ENUM);
}

unsafe fn test_color_material(c: &mut Checks) {
    let mut f = [0f32; 4];
    glColorMaterial(GL_FRONT, GL_EMISSION);
    glColor4f(0.0, 1.0, 0.0, 1.0);
    glEnable(GL_COLOR_MATERIAL);
    glGetMaterialfv(GL_FRONT, GL_EMISSION, f.as_mut_ptr());
    check!(c, near4(&f, 0.0, 1.0, 0.0, 1.0));
    glColor4f(0.0, 0.0, 1.0, 1.0);
    glGetMaterialfv(GL_FRONT, GL_EMISSION, f.as_mut_ptr());
    check!(c, near4(&f, 0.0, 0.0, 1.0, 1.0));
    glGetMaterialfv(GL_BACK, GL_EMISSION, f.as_mut_ptr());
    check!(c, !near4(&f, 0.0, 0.0, 1.0, 1.0)); // Back not tracked
    glDisable(GL_COLOR_MATERIAL);
    glColor4f(1.0, 1.0, 1.0, 1.0);
    glGetMaterialfv(GL_FRONT, GL_EMISSION, f.as_mut_ptr());
    check!(c, near4(&f, 0.0, 0.0, 1.0, 1.0));
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    check!(c, glGetError() == GL_NO_ERROR);
}

unsafe fn test_attrib_stack(c: &mut Checks) {
    let mut f = [0f32; 4];
    glPushAttrib(GL_LIGHTING_BIT | GL_TRANSFORM_BIT);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT5);
    glEnable(GL_NORMALIZE);
    glEnable(GL_RESCALE_NORMAL);
    set_light(GL_LIGHT5, GL_SPECULAR, 0.5, 0.5, 0.5, 1.0);
    glMaterialf(GL_BACK, GL_SHININESS, 7.0);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE as i32);
    glPopAttrib();
    check!(c, glIsEnabled(GL_LIGHTING) == 0 && glIsEnabled(GL_LIGHT5) == 0);
    check!(c, glIsEnabled(GL_NORMALIZE) == 0 && glIsEnabled(GL_RESCALE_NORMAL) == 0);
    glGetLightfv(GL_LIGHT5, GL_SPECULAR, f.as_mut_ptr());
    check!(c, near4(&f, 0.0, 0.0, 0.0, 1.0));
    glGetMaterialfv(GL_BACK, GL_SHININESS, f.as_mut_ptr());
    check!(c, near(f[0], 0.0));
    let mut b = GL_TRUE as u8;
    glGetBooleanv(GL_LIGHT_MODEL_LOCAL_VIEWER, &mut b);
    check!(c, b == GL_FALSE as u8);
    glPushAttrib(GL_ENABLE_BIT);
    glEnable(GL_LIGHT6);
    glEnable(GL_COLOR_MATERIAL);
    set_light(GL_LIGHT6, GL_AMBIENT, 0.25, 0.0, 0.0, 1.0);
    glPopAttrib();
    check!(c, glIsEnabled(GL_LIGHT6) == 0 && glIsEnabled(GL_COLOR_MATERIAL) == 0);
    glGetLightfv(GL_LIGHT6, GL_AMBIENT, f.as_mut_ptr());
    check!(c, near4(&f, 0.25, 0.0, 0.0, 1.0));
    set_light(GL_LIGHT6, GL_AMBIENT, 0.0, 0.0, 0.0, 1.0);
    check!(c, glGetError() == GL_NO_ERROR);
}

fn print_expected(c: &Checks) {
    print!(
        "\n{}/{} checks passed\n\nTop screen, top row:\n- red sphere lit from upper left\n- blue sphere, white highlight\n  upper left\n- yellow glow fading from the\n  center (point light)\n- green spot light disc\nBottom row:\n- white sphere: red left, green\n  right, blue top (mixes)\n- sphere red top -> blue bottom,\n  lit, white highlight\n- green | magenta on top,\n  green | black below\n- 2 identical faceted orange\n  spheres, a washed-out one below\n\nSTART: exit\n",
        c.passed(),
        c.total
    );
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut app = App::new();
    let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };

    print!("c3dgl lighting\n\n\n\n\n");
    let mut checks = Checks::default();
    unsafe {
        test_defaults(&mut checks);
        test_parameters(&mut checks);
        test_errors(&mut checks);
        test_color_material(&mut checks);
        test_attrib_stack(&mut checks);
        let ambient = [0.2f32, 0.2, 0.2, 1.0];
        let black = [0.0f32, 0.0, 0.0, 1.0];
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient.as_ptr());
        let diffuse = [0.8f32, 0.8, 0.8, 1.0];
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambient.as_ptr());
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuse.as_ptr());
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, black.as_ptr());
        glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, black.as_ptr());
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 0.0);
        check!(checks, glGetError() == GL_NO_ERROR);
        glClearColor(0.12, 0.12, 0.15, 1.0);
    }
    print_expected(&checks);
    let sphere = Sphere::new(24, 16);

    let mut stats = StatsOverlay::new();
    while app.frame() {
        stats.print();
        if app.down(KeyPad::START) {
            break;
        }
        unsafe {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            begin_cell(0, 0); draw_diffuse(&sphere); end_cell();
            begin_cell(1, 0); draw_specular(&sphere); end_cell();
            begin_cell(2, 0); draw_point_light(); end_cell();
            begin_cell(3, 0); draw_spot_light(); end_cell();
            begin_cell(0, 1); draw_three_lights(&sphere); end_cell();
            begin_cell(1, 1); draw_color_material(&sphere); end_cell();
            begin_cell(2, 1); draw_two_sided(); end_cell();
            begin_cell(3, 1); draw_flat_scaled(&sphere); end_cell();
            c3dglSwapBuffers();
        }
    }
}
