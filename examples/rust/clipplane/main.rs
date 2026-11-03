//! User clip planes (glClipPlane). The top screen shows one clip setup per cell (4x2 grid, 100x120
//! px per cell). Where a cell has a reference, the clipped shape is drawn in the upper half and the
//! shape it must have, drawn without clipping, in the lower half. The bottom screen shows the result
//! of the non-visual checks and what each cell must look like.
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
const ASPECT: f32 = CELL_H as f32 / CELL_W as f32; // Cells span x -1..1, y -ASPECT..ASPECT
const PI: f32 = 3.14159265;

fn near(a: f64, b: f64) -> bool {
    (a - b).abs() < 1e-4
}

static mut CHECKER_TEXTURE: u32 = 0;

/// Cell (column, row from the top) as viewport with an orthographic projection, square pixels.
unsafe fn begin_cell(column: i32, row: i32) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -ASPECT as f64, ASPECT as f64, -10.0, 10.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glPushAttrib(GL_TRANSFORM_BIT | GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_POLYGON_BIT | GL_LINE_BIT | GL_POINT_BIT | GL_TEXTURE_BIT);
}

unsafe fn end_cell() {
    glPopAttrib();
}

unsafe fn set_plane(plane: u32, a: f64, b: f64, c: f64, d: f64) {
    let eq = [a, b, c, d];
    glClipPlane(plane, eq.as_ptr());
    glEnable(plane);
}

unsafe fn rect(x0: f32, y0: f32, x1: f32, y1: f32) {
    glBegin(GL_QUADS);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

/// 1: a gradient quad cut at x = 0 (top: clipped, bottom: the left half drawn directly).
unsafe fn draw_gradient() {
    set_plane(GL_CLIP_PLANE0, -1.0, 0.0, 0.0, 0.0);
    glBegin(GL_QUADS);
    glColor3f(1.0, 0.0, 0.0);
    glVertex2f(-0.8, 0.15);
    glColor3f(0.0, 0.0, 1.0);
    glVertex2f(0.8, 0.15);
    glColor3f(0.0, 0.0, 1.0);
    glVertex2f(0.8, 1.0);
    glColor3f(1.0, 0.0, 0.0);
    glVertex2f(-0.8, 1.0);
    glEnd();
    glDisable(GL_CLIP_PLANE0);
    glBegin(GL_QUADS);
    glColor3f(1.0, 0.0, 0.0);
    glVertex2f(-0.8, -1.0);
    glColor3f(0.5, 0.0, 0.5);
    glVertex2f(0.0, -1.0);
    glColor3f(0.5, 0.0, 0.5);
    glVertex2f(0.0, -0.15);
    glColor3f(1.0, 0.0, 0.0);
    glVertex2f(-0.8, -0.15);
    glEnd();
}

/// 2: the plane is set under one modelview matrix and used under another: it stays in eye space.
/// Top: a green square cut along its diagonal; bottom: the remaining triangle drawn directly.
unsafe fn draw_transformed() {
    glPushMatrix();
    glTranslatef(0.0, 0.6, 0.0);
    glRotatef(45.0, 0.0, 0.0, 1.0);
    set_plane(GL_CLIP_PLANE2, 1.0, 0.0, 0.0, 0.0); // Keeps x + y >= 0.6 after the transform
    glPopMatrix();
    glColor3f(0.2, 0.8, 0.3);
    glPushMatrix();
    glTranslatef(0.0, 0.6, 0.0);
    glScalef(0.4, 0.4, 1.0);
    rect(-1.0, -1.0, 1.0, 1.0);
    glPopMatrix();
    glDisable(GL_CLIP_PLANE2);
    glBegin(GL_TRIANGLES);
    glVertex2f(0.4, -1.0);
    glVertex2f(0.4, -0.2);
    glVertex2f(-0.4, -0.2);
    glEnd();
}

/// 3: all six planes: |x| <= 0.5, |y| <= 0.6, -1 <= z <= 1, including a magenta square at z = 3
/// over the whole cell that must not appear.
unsafe fn draw_six_planes() {
    set_plane(GL_CLIP_PLANE0, 1.0, 0.0, 0.0, 0.5);
    set_plane(GL_CLIP_PLANE1, -1.0, 0.0, 0.0, 0.5);
    set_plane(GL_CLIP_PLANE2, 0.0, 1.0, 0.0, 0.6);
    set_plane(GL_CLIP_PLANE3, 0.0, -1.0, 0.0, 0.6);
    set_plane(GL_CLIP_PLANE4, 0.0, 0.0, 1.0, 1.0);
    set_plane(GL_CLIP_PLANE5, 0.0, 0.0, -1.0, 1.0);
    for y in 0..6 {
        for x in 0..5 {
            let light = (x + y) & 1 != 0;
            glColor3f(if light { 0.95 } else { 0.25 }, if light { 0.85 } else { 0.25 }, if light { 0.3 } else { 0.6 });
            let (x, y) = (x as f32, y as f32);
            rect(-1.0 + 0.4 * x, -1.2 + 0.4 * y, -0.6 + 0.4 * x, -0.8 + 0.4 * y);
        }
    }
    glColor3f(1.0, 0.0, 1.0);
    glBegin(GL_QUADS);
    glVertex3f(-1.0, -1.2, 3.0);
    glVertex3f(1.0, -1.2, 3.0);
    glVertex3f(1.0, 1.2, 3.0);
    glVertex3f(-1.0, 1.2, 3.0);
    glEnd();
}

/// 4: lines and points against the plane x >= 0: lines are cut at the boundary, points left of it
/// vanish, only the 4 right points of each row of 8 remain.
unsafe fn draw_lines_points() {
    set_plane(GL_CLIP_PLANE1, 1.0, 0.0, 0.0, 0.0);
    glLineWidth(3.0);
    glBegin(GL_LINES);
    glColor3f(1.0, 0.4, 0.2);
    glVertex2f(-0.9, 0.9);
    glVertex2f(0.9, 0.9);
    glColor3f(0.3, 0.9, 0.3);
    glVertex2f(-0.9, 0.2);
    glVertex2f(0.9, 0.7);
    glColor3f(0.4, 0.6, 1.0);
    glVertex2f(-0.9, 0.5);
    glVertex2f(0.9, 0.0);
    glColor3f(1.0, 1.0, 0.3);
    glVertex2f(-0.9, -0.2);
    glVertex2f(-0.1, -0.2); // Completely clipped
    glEnd();
    glPointSize(6.0);
    glColor3f(1.0, 0.4, 0.2);
    glBegin(GL_POINTS);
    for row in 0..2 {
        for i in 0..8 {
            glVertex2f(-0.875 + 0.25 * i as f32, -0.5 - 0.4 * row as f32);
        }
    }
    glEnd();
    glDisable(GL_CLIP_PLANE1);
    glLineWidth(1.0);
    glColor3f(1.0, 1.0, 1.0);
    glBegin(GL_LINES);
    glVertex2f(0.0, -1.2);
    glVertex2f(0.0, 1.2);
    glEnd();
}

/// 5: polygon mode GL_LINE: clipped edges are open at the cut. Top: a triangle cut at y = 0.6
/// (open trapezoid, no top edge); bottom: a square cut at x = 0.3 (open on the right).
unsafe fn draw_outlines() {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(2.0);
    glColor3f(0.4, 0.9, 1.0);
    set_plane(GL_CLIP_PLANE0, 0.0, -1.0, 0.0, 0.6);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.8, 0.0);
    glVertex2f(0.8, 0.0);
    glVertex2f(0.0, 1.1);
    glEnd();
    glDisable(GL_CLIP_PLANE0);
    glColor3f(1.0, 0.6, 0.3);
    set_plane(GL_CLIP_PLANE1, -1.0, 0.0, 0.0, 0.3);
    rect(-0.7, -1.0, 0.7, -0.25);
}

/// Lat/long sphere of radius 1 with normals.
unsafe fn sphere(slices: i32, stacks: i32) {
    for i in 0..stacks {
        let t0 = PI * i as f32 / stacks as f32 - PI / 2.0;
        let t1 = PI * (i + 1) as f32 / stacks as f32 - PI / 2.0;
        glBegin(GL_QUAD_STRIP);
        for j in 0..=slices {
            let p = 2.0 * PI * j as f32 / slices as f32;
            for t in [t0, t1] {
                // Counter-clockwise seen from outside
                let (x, y, z) = (t.cos() * p.cos(), t.sin(), t.cos() * p.sin());
                glNormal3f(x, y, z);
                glVertex3f(x, y, z);
            }
        }
        glEnd();
    }
}

/// 6: a lit sphere with its top cap cut off, tilted toward the viewer: blue outside, the inside
/// (back faces, two-sided lighting) yellow through the opening.
unsafe fn draw_sphere() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5, 0.5, -0.5 * ASPECT as f64, 0.5 * ASPECT as f64, 1.0, 10.0);
    glMatrixMode(GL_MODELVIEW);
    glTranslatef(0.0, 0.0, -3.2);
    glRotatef(35.0, 1.0, 0.0, 0.0);
    set_plane(GL_CLIP_PLANE3, 0.0, -1.0, 0.0, 0.35); // Keeps y <= 0.35 in sphere coordinates
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE as i32);
    let (light, outside, inside) = ([0.3f32, 1.0, 1.0, 0.0], [0.2f32, 0.4, 1.0, 1.0], [1.0f32, 0.85, 0.2, 1.0]);
    glPushMatrix();
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_POSITION, light.as_ptr());
    glPopMatrix();
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, outside.as_ptr());
    glMaterialfv(GL_BACK, GL_AMBIENT_AND_DIFFUSE, inside.as_ptr());
    sphere(24, 16);
}

unsafe fn bind_checker() {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, CHECKER_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE as i32);
}

/// 7: six planes forming a hexagon around a textured square: the hexagon is cut from the square's
/// edges, its squares are not distorted at the cuts.
unsafe fn draw_hexagon() {
    for i in 0..6 {
        let a = PI / 3.0 * i as f32 + PI / 6.0;
        set_plane(GL_CLIP_PLANE0 + i, -a.cos() as f64, -a.sin() as f64, 0.0, 0.75);
    }
    bind_checker();
    glBegin(GL_QUADS);
    glTexCoord2f(0.0, 0.0);
    glVertex2f(-0.9, -0.9);
    glTexCoord2f(1.0, 0.0);
    glVertex2f(0.9, -0.9);
    glTexCoord2f(1.0, 1.0);
    glVertex2f(0.9, 0.9);
    glTexCoord2f(0.0, 1.0);
    glVertex2f(-0.9, 0.9);
    glEnd();
}

/// 8: a textured floor in perspective, cut by the eye space plane z >= -6 set before the camera
/// moves: the floor ends in a straight horizontal edge, the checker stays perspective-correct up to it.
unsafe fn draw_floor() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5, 0.5, -0.5 * ASPECT as f64, 0.5 * ASPECT as f64, 1.0, 40.0);
    glMatrixMode(GL_MODELVIEW);
    set_plane(GL_CLIP_PLANE4, 0.0, 0.0, 1.0, 6.0);
    glRotatef(10.0, 1.0, 0.0, 0.0);
    glTranslatef(0.0, -1.0, 0.0);
    bind_checker();
    glBegin(GL_QUADS);
    glTexCoord2f(0.0, 0.0);
    glVertex3f(-4.0, 0.0, -1.0);
    glTexCoord2f(4.0, 0.0);
    glVertex3f(4.0, 0.0, -1.0);
    glTexCoord2f(4.0, 30.0);
    glVertex3f(4.0, 0.0, -31.0);
    glTexCoord2f(0.0, 30.0);
    glVertex3f(-4.0, 0.0, -31.0);
    glEnd();
}

unsafe fn create_checker() {
    let mut texels = [0u8; 8 * 8 * 4];
    for y in 0..8 {
        for x in 0..8 {
            let light = ((x / 2 + y / 2) & 1) != 0;
            texels[(y * 8 + x) * 4..][..4].copy_from_slice(&if light { [240, 240, 240, 255] } else { [40, 90, 200, 255] });
        }
    }
    glGenTextures(1, &raw mut CHECKER_TEXTURE);
    glBindTexture(GL_TEXTURE_2D, CHECKER_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST as i32);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA as i32, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.as_ptr().cast());
}

//----------------------------------------------------------------------------------
// Non-visual checks
//----------------------------------------------------------------------------------

unsafe fn plane_is(plane: u32, a: f64, b: f64, c: f64, d: f64) -> bool {
    let mut e = [9.0f64; 4];
    glGetClipPlane(plane, e.as_mut_ptr());
    near(e[0], a) && near(e[1], b) && near(e[2], c) && near(e[3], d)
}

unsafe fn test_clip_api(c: &mut Checks) {
    let mut v = 0i32;
    glGetIntegerv(GL_MAX_CLIP_PLANES, &mut v);
    check!(c, v == 6);
    for i in 0..6 {
        check!(c, glIsEnabled(GL_CLIP_PLANE0 + i) == 0);
        check!(c, plane_is(GL_CLIP_PLANE0 + i, 0.0, 0.0, 0.0, 0.0));
    }

    // glClipPlane transforms the plane by the inverse of the modelview matrix at the time of the call
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef(2.0, 0.0, 0.0);
    let x_plane = [1.0f64, 0.0, 0.0, 0.0];
    glClipPlane(GL_CLIP_PLANE0, x_plane.as_ptr()); // x_obj >= 0 is x_eye >= 2
    check!(c, plane_is(GL_CLIP_PLANE0, 1.0, 0.0, 0.0, -2.0));
    glLoadIdentity();
    glScalef(2.0, 4.0, 1.0);
    let y_plane = [0.0f64, 1.0, 0.0, -1.0];
    glClipPlane(GL_CLIP_PLANE1, y_plane.as_ptr()); // y_obj >= 1 is y_eye >= 4
    check!(c, plane_is(GL_CLIP_PLANE1, 0.0, 0.25, 0.0, -1.0));
    glLoadIdentity();
    glRotatef(90.0, 0.0, 0.0, 1.0);
    glClipPlane(GL_CLIP_PLANE2, x_plane.as_ptr()); // x_obj >= 0 is y_eye >= 0
    check!(c, plane_is(GL_CLIP_PLANE2, 0.0, 1.0, 0.0, 0.0));
    glPopMatrix();
    check!(c, plane_is(GL_CLIP_PLANE0, 1.0, 0.0, 0.0, -2.0)); // Unaffected by later matrix changes

    // float and fixed point variants
    let f_plane = [0.5f32, -1.0, 2.0, 3.0];
    glClipPlanef(GL_CLIP_PLANE3, f_plane.as_ptr());
    let mut f = [0f32; 4];
    glGetClipPlanef(GL_CLIP_PLANE3, f.as_mut_ptr());
    check!(c, near(f[0] as f64, 0.5) && near(f[1] as f64, -1.0) && near(f[2] as f64, 2.0) && near(f[3] as f64, 3.0));
    let x_plane_fixed = [0x8000i32, 0, -0x10000, 0x20000];
    glClipPlanex(GL_CLIP_PLANE4, x_plane_fixed.as_ptr());
    let mut x = [0i32; 4];
    glGetClipPlanex(GL_CLIP_PLANE4, x.as_mut_ptr());
    check!(c, x[0] == 0x8000 && x[1] == 0 && x[2] == -0x10000 && x[3] == 0x20000);

    // enable flags
    glEnable(GL_CLIP_PLANE5);
    check!(c, glIsEnabled(GL_CLIP_PLANE5) != 0);
    let mut b = 0u8;
    glGetBooleanv(GL_CLIP_PLANE5, &mut b);
    check!(c, b == GL_TRUE as u8);
    glDisable(GL_CLIP_PLANE5);
    check!(c, glIsEnabled(GL_CLIP_PLANE5) == 0);
    check!(c, glGetError() == GL_NO_ERROR);

    // errors
    glClipPlane(GL_CLIP_PLANE0 + 6, x_plane.as_ptr());
    check!(c, glGetError() == GL_INVALID_ENUM);
    glEnable(GL_CLIP_PLANE0 + 6);
    check!(c, glGetError() == GL_INVALID_ENUM);
    let mut e = [0f64; 4];
    glGetClipPlane(GL_LIGHT0, e.as_mut_ptr());
    check!(c, glGetError() == GL_INVALID_ENUM);
    glBegin(GL_POINTS);
    glClipPlane(GL_CLIP_PLANE0, y_plane.as_ptr());
    glEnd();
    check!(c, glGetError() == GL_INVALID_OPERATION);
    check!(c, plane_is(GL_CLIP_PLANE0, 1.0, 0.0, 0.0, -2.0)); // Unchanged by the failed calls

    // attribute stack: GL_TRANSFORM_BIT keeps the planes and their enables, GL_ENABLE_BIT only the enables
    glPushAttrib(GL_TRANSFORM_BIT);
    glEnable(GL_CLIP_PLANE1);
    glClipPlane(GL_CLIP_PLANE0, y_plane.as_ptr());
    glPopAttrib();
    check!(c, glIsEnabled(GL_CLIP_PLANE1) == 0);
    check!(c, plane_is(GL_CLIP_PLANE0, 1.0, 0.0, 0.0, -2.0));
    glPushAttrib(GL_ENABLE_BIT);
    glEnable(GL_CLIP_PLANE2);
    glClipPlane(GL_CLIP_PLANE0, y_plane.as_ptr());
    glPopAttrib();
    check!(c, glIsEnabled(GL_CLIP_PLANE2) == 0);
    check!(c, plane_is(GL_CLIP_PLANE0, 0.0, 1.0, 0.0, -1.0));
    check!(c, glGetError() == GL_NO_ERROR);
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut app = App::new();
    let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };

    print!("c3dgl clip planes\n\n\n\n\n");
    let mut checks = Checks::default();
    unsafe { test_clip_api(&mut checks) };
    print!(
        "\n{}/{} checks passed\n\nTop screen, upper/lower halves\nmust match where there are two\ntop row:\n- red->blue square cut at x=0\n- square cut along a diagonal\n- checker window, no magenta\n- lines/points right of the\n  white line only\nbottom row:\n- outlines open at the cuts\n- blue sphere, cap cut off,\n  yellow inside\n- checker hexagon\n- floor ending in a straight\n  edge\n\nSTART: exit\n",
        checks.passed(),
        checks.total
    );
    unsafe {
        create_checker();
        glClearColor(0.12, 0.12, 0.15, 1.0);
    }

    let mut stats = StatsOverlay::new();
    while app.frame() {
        stats.print();
        if app.down(KeyPad::START) {
            break;
        }
        unsafe {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            begin_cell(0, 0); draw_gradient(); end_cell();
            begin_cell(1, 0); draw_transformed(); end_cell();
            begin_cell(2, 0); draw_six_planes(); end_cell();
            begin_cell(3, 0); draw_lines_points(); end_cell();
            begin_cell(0, 1); draw_outlines(); end_cell();
            begin_cell(1, 1); draw_sphere(); end_cell();
            begin_cell(2, 1); draw_hexagon(); end_cell();
            begin_cell(3, 1); draw_floor(); end_cell();
            c3dglSwapBuffers();
        }
    }
}
