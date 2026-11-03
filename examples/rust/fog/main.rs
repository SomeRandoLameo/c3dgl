//! Fog. The top screen shows one fog setup per cell (4x2 grid, 100x120 px per cell). Most cells
//! draw pairs of squares: left a fogged square at a known eye depth, right an unfogged square in
//! the color GL's fog equation gives for that depth (computed here). Each pair must look the same.
//! The bottom screen shows the result of the non-visual checks and what each cell must look like.
#[macro_use]
#[path = "../common/mod.rs"]
mod common;

use c3dgl::raw::*;
use common::gl::*;
use common::{App, Checks, Renderer, StatsOverlay, SCREEN_HEIGHT, SCREEN_WIDTH};
use ctru::prelude::*;

const COLUMNS: i32 = 4;
const ROWS: i32 = 2;
const CELL_W: i32 = SCREEN_WIDTH / COLUMNS;
const CELL_H: i32 = SCREEN_HEIGHT / ROWS;
const NEAR: f32 = 1.0;
const FAR: f32 = 30.0;
const HALF_W: f32 = 0.5;
const HALF_H: f32 = HALF_W * CELL_H as f32 / CELL_W as f32; // Frustum at the near plane, square pixels

#[derive(Clone, Copy)]
struct Color {
    r: f32,
    g: f32,
    b: f32,
    a: f32,
}

const OBJECT: Color = Color { r: 0.9, g: 0.15, b: 0.1, a: 1.0 }; // Red squares
const FOG: Color = Color { r: 0.75, g: 0.8, b: 0.9, a: 1.0 }; // Light blue-gray fog

fn near(a: f32, b: f32) -> bool {
    (a - b).abs() < 1e-4
}

/// GL fog factor for eye distance c (1 = no fog).
fn fog_factor(mode: u32, density: f32, start: f32, end: f32, c: f32) -> f32 {
    let f = if mode == GL_LINEAR {
        (end - c) / (end - start)
    } else if mode == GL_EXP2 {
        (-(density * c) * (density * c)).exp()
    } else {
        (-density * c).exp()
    };
    f.clamp(0.0, 1.0)
}

fn fogged(c: Color, f: f32) -> Color {
    Color { r: f * c.r + (1.0 - f) * FOG.r, g: f * c.g + (1.0 - f) * FOG.g, b: f * c.b + (1.0 - f) * FOG.b, a: c.a }
}

unsafe fn set_fog_color() {
    let color = [FOG.r, FOG.g, FOG.b, FOG.a];
    glFogfv(GL_FOG_COLOR, color.as_ptr());
}

/// Cell (column, row from the top) as viewport with a perspective projection (near 1, far 30).
unsafe fn begin_cell(column: i32, row: i32) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-HALF_W as f64, HALF_W as f64, -HALF_H as f64, HALF_H as f64, NEAR as f64, FAR as f64);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glPushAttrib(GL_FOG_BIT | GL_ENABLE_BIT | GL_VIEWPORT_BIT | GL_COLOR_BUFFER_BIT);
    set_fog_color();
}

unsafe fn end_cell() {
    glPopAttrib();
}

/// Square at NDC (x, y) with half size s (NDC), at eye depth z < 0 (perspective: object size grows with |z|).
unsafe fn draw_square(x: f32, y: f32, s: f32, z: f32, c: Color, perspective: bool) {
    let (kx, ky) = if perspective { (-z * HALF_W / NEAR, -z * HALF_H / NEAR) } else { (1.0, 1.0) };
    glColor4f(c.r, c.g, c.b, c.a);
    glBegin(GL_QUADS);
    glVertex3f((x - s) * kx, (y - s) * ky, z);
    glVertex3f((x + s) * kx, (y - s) * ky, z);
    glVertex3f((x + s) * kx, (y + s) * ky, z);
    glVertex3f((x - s) * kx, (y + s) * ky, z);
    glEnd();
}

/// Four rows of pairs at the eye depths zs: left fogged, right the expected color without fog.
unsafe fn draw_pairs(mode: u32, density: f32, start: f32, end: f32, zs: [f32; 4], perspective: bool) {
    glFogi(GL_FOG_MODE, mode as i32);
    glFogf(GL_FOG_DENSITY, density);
    glFogf(GL_FOG_START, start);
    glFogf(GL_FOG_END, end);
    for (i, z) in zs.iter().enumerate() {
        let y = 0.72 - 0.48 * i as f32;
        glEnable(GL_FOG);
        draw_square(-0.45, y, 0.2, *z, OBJECT, perspective);
        glDisable(GL_FOG);
        draw_square(0.45, y, 0.2, *z, fogged(OBJECT, fog_factor(mode, density, start, end, -z)), perspective);
    }
}

/// 1: GL_LINEAR from 2 to 12 at depths 3, 5.5, 8, 10.5 (f = 0.9, 0.65, 0.4, 0.15).
unsafe fn draw_linear() {
    draw_pairs(GL_LINEAR, 1.0, 2.0, 12.0, [-3.0, -5.5, -8.0, -10.5], true);
}

/// 2: GL_EXP, density 0.15.
unsafe fn draw_exp() {
    draw_pairs(GL_EXP, 0.15, 0.0, 1.0, [-2.0, -5.0, -9.0, -15.0], true);
}

/// 3: GL_EXP2, density 0.12.
unsafe fn draw_exp2() {
    draw_pairs(GL_EXP2, 0.12, 0.0, 1.0, [-2.0, -6.0, -10.0, -14.0], true);
}

/// 4: GL_LINEAR with an orthographic projection (depth is linear in z there).
unsafe fn draw_ortho() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, 0.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    draw_pairs(GL_LINEAR, 1.0, 1.0, 16.0, [-2.0, -6.0, -10.0, -14.0], false);
}

/// 5: a floor (checkerboard of 1x1 tiles) receding from z = -1 to -30 under linear fog 3..20: tiles
/// fade smoothly into the fog color toward the horizon, no bands.
unsafe fn draw_floor() {
    glFogi(GL_FOG_MODE, GL_LINEAR as i32);
    glFogf(GL_FOG_START, 3.0);
    glFogf(GL_FOG_END, 20.0);
    glEnable(GL_FOG);
    glBegin(GL_QUADS);
    for z in 1..30 {
        for x in -12..12 {
            let light = (x + z) & 1 != 0;
            glColor3f(if light { 0.95 } else { 0.2 }, if light { 0.6 } else { 0.3 }, if light { 0.2 } else { 0.1 });
            let (x, z) = (x as f32, z as f32);
            glVertex3f(x, -1.0, -z);
            glVertex3f(x, -1.0, -z - 1.0);
            glVertex3f(x + 1.0, -1.0, -z - 1.0);
            glVertex3f(x + 1.0, -1.0, -z);
        }
    }
    glEnd();
}

/// 6: like cell 1, but with glDepthRange(0.25, 0.75): the table follows the depth range, pairs still match.
unsafe fn draw_depth_range() {
    glDepthRange(0.25, 0.75);
    draw_linear();
}

/// 7: lines and points (expanded on the CPU, drawn in NDC) get fog by depth too: three horizontal
/// 3 px lines at depths 3, 7, 11 and a row of 6 px points below each, all red fading to the fog
/// color; small reference squares on the right in the expected color.
unsafe fn draw_lines_points() {
    glFogi(GL_FOG_MODE, GL_LINEAR as i32);
    glFogf(GL_FOG_START, 2.0);
    glFogf(GL_FOG_END, 12.0);
    glLineWidth(3.0);
    glPointSize(6.0);
    for (i, z) in [-3.0f32, -7.0, -11.0].iter().enumerate() {
        let y = 0.7 - 0.6 * i as f32;
        let (kx, ky) = (-z * HALF_W / NEAR, -z * HALF_H / NEAR);
        glEnable(GL_FOG);
        glColor3f(OBJECT.r, OBJECT.g, OBJECT.b);
        glBegin(GL_LINES);
        glVertex3f(-0.85 * kx, y * ky, *z);
        glVertex3f(0.35 * kx, y * ky, *z);
        glEnd();
        glBegin(GL_POINTS);
        for p in 0..4 {
            glVertex3f((-0.8 + 0.35 * p as f32) * kx, (y - 0.2) * ky, *z);
        }
        glEnd();
        glDisable(GL_FOG);
        draw_square(0.65, y - 0.1, 0.15, *z, fogged(OBJECT, fog_factor(GL_LINEAR, 1.0, 2.0, 12.0, -z)), true);
    }
    glLineWidth(1.0);
    glPointSize(1.0);
}

/// 8: fog keeps alpha: half transparent squares over a white background, blended. Left fogged,
/// right the expected color with the same alpha; each pair must match, the white shining through both.
unsafe fn draw_alpha() {
    draw_square(0.0, 0.0, 0.95, -20.0, Color { r: 1.0, g: 1.0, b: 1.0, a: 1.0 }, true); // Background (unfogged)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    let half = Color { a: 0.5, ..OBJECT };
    glFogi(GL_FOG_MODE, GL_LINEAR as i32);
    glFogf(GL_FOG_START, 2.0);
    glFogf(GL_FOG_END, 12.0);
    for (i, z) in [-3.0f32, -6.0, -9.0].iter().enumerate() {
        let y = 0.6 - 0.6 * i as f32;
        glEnable(GL_FOG);
        draw_square(-0.45, y, 0.22, *z, half, true);
        glDisable(GL_FOG);
        draw_square(0.45, y, 0.22, *z, fogged(half, fog_factor(GL_LINEAR, 1.0, 2.0, 12.0, -z)), true);
    }
}

//----------------------------------------------------------------------------------
// Non-visual checks
//----------------------------------------------------------------------------------

unsafe fn test_fog_api(c: &mut Checks) {
    let (mut v, mut f) = (0i32, [0f32; 4]);
    check!(c, glIsEnabled(GL_FOG) == 0);
    glGetIntegerv(GL_FOG_MODE, &mut v);
    check!(c, v == GL_EXP as i32);
    glGetFloatv(GL_FOG_DENSITY, f.as_mut_ptr());
    check!(c, near(f[0], 1.0));
    glGetFloatv(GL_FOG_START, f.as_mut_ptr());
    check!(c, near(f[0], 0.0));
    glGetFloatv(GL_FOG_END, f.as_mut_ptr());
    check!(c, near(f[0], 1.0));
    glGetFloatv(GL_FOG_COLOR, f.as_mut_ptr());
    check!(c, near(f[0], 0.0) && near(f[1], 0.0) && near(f[2], 0.0) && near(f[3], 0.0));

    glFogi(GL_FOG_MODE, GL_LINEAR as i32);
    glGetIntegerv(GL_FOG_MODE, &mut v);
    check!(c, v == GL_LINEAR as i32);
    glFogf(GL_FOG_START, 2.5);
    glFogi(GL_FOG_END, 40);
    glGetFloatv(GL_FOG_START, f.as_mut_ptr());
    check!(c, near(f[0], 2.5));
    glGetFloatv(GL_FOG_END, f.as_mut_ptr());
    check!(c, near(f[0], 40.0));

    let color = [0.25f32, 0.5, 2.0, -1.0]; // Clamped to [0, 1]
    glFogfv(GL_FOG_COLOR, color.as_ptr());
    glGetFloatv(GL_FOG_COLOR, f.as_mut_ptr());
    check!(c, near(f[0], 0.25) && near(f[1], 0.5) && near(f[2], 1.0) && near(f[3], 0.0));
    let icolor = [2147483647i32, 0, 0, 2147483647];
    glFogiv(GL_FOG_COLOR, icolor.as_ptr());
    glGetFloatv(GL_FOG_COLOR, f.as_mut_ptr());
    check!(c, near(f[0], 1.0) && near(f[1], 0.0) && near(f[3], 1.0));
    let mut ic = [0i32; 4];
    glGetIntegerv(GL_FOG_COLOR, ic.as_mut_ptr());
    check!(c, ic[0] == 2147483647 && ic[1] == 0);

    glEnable(GL_FOG);
    check!(c, glIsEnabled(GL_FOG) != 0);
    let mut b = 0u8;
    glGetBooleanv(GL_FOG, &mut b);
    check!(c, b == GL_TRUE as u8);
    glDisable(GL_FOG);

    // Fixed point variants (OpenGL ES)
    glFogx(GL_FOG_MODE, GL_EXP2 as i32);
    glGetIntegerv(GL_FOG_MODE, &mut v);
    check!(c, v == GL_EXP2 as i32);
    glFogx(GL_FOG_DENSITY, 0x8000);
    glGetFloatv(GL_FOG_DENSITY, f.as_mut_ptr());
    check!(c, near(f[0], 0.5));
    let xcolor = [0x10000i32, 0x8000, 0, 0x10000];
    glFogxv(GL_FOG_COLOR, xcolor.as_ptr());
    let mut x = [0i32; 4];
    glGetFixedv(GL_FOG_COLOR, x.as_mut_ptr());
    check!(c, x[0] == 0x10000 && x[1] == 0x8000 && x[2] == 0);
    check!(c, glGetError() == GL_NO_ERROR);

    // Errors leave the state alone
    glFogi(GL_FOG_MODE, GL_LINEAR_ATTENUATION as i32);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glFogf(GL_FOG_DENSITY, -1.0);
    check!(c, glGetError() == GL_INVALID_VALUE);
    glFogf(GL_FOG_COLOR, 1.0);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glFogf(0x1234, 1.0);
    check!(c, glGetError() == GL_INVALID_ENUM);
    glGetIntegerv(GL_FOG_MODE, &mut v);
    check!(c, v == GL_EXP2 as i32); // Unchanged by the failed calls

    // Attribute stack: GL_FOG_BIT restores the fog state, GL_ENABLE_BIT the enable flag
    glPushAttrib(GL_FOG_BIT);
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR as i32);
    glFogf(GL_FOG_END, 99.0);
    glPopAttrib();
    check!(c, glIsEnabled(GL_FOG) == 0);
    glGetIntegerv(GL_FOG_MODE, &mut v);
    check!(c, v == GL_EXP2 as i32);
    glGetFloatv(GL_FOG_END, f.as_mut_ptr());
    check!(c, near(f[0], 40.0));
    glPushAttrib(GL_ENABLE_BIT);
    glEnable(GL_FOG);
    glFogf(GL_FOG_START, 7.0);
    glPopAttrib();
    check!(c, glIsEnabled(GL_FOG) == 0);
    glGetFloatv(GL_FOG_START, f.as_mut_ptr());
    check!(c, near(f[0], 7.0));
    check!(c, glGetError() == GL_NO_ERROR);
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut app = App::new();
    let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };

    print!("c3dgl fog\n\n\n\n\n");
    let mut checks = Checks::default();
    unsafe { test_fog_api(&mut checks) };
    print!(
        "\n{}/{} checks passed\n\nTop screen: left/right squares\nof every pair must match\ntop row:\n- LINEAR, EXP, EXP2: red squares\n  fading into blue-gray, deeper\n  rows more fogged\n- LINEAR with glOrtho\nbottom row:\n- checker floor fading smoothly\n  into the fog, no bands\n- LINEAR with glDepthRange\n- fogged lines + points, matching\n  squares on the right\n- half transparent pairs on a\n  white background\n\nSTART: exit\n",
        checks.passed(),
        checks.total
    );
    unsafe {
        glClearColor(0.12, 0.12, 0.15, 1.0);
        glEnable(GL_DEPTH_TEST);
    }

    let mut stats = StatsOverlay::new();
    while app.frame() {
        stats.print();
        if app.down(KeyPad::START) {
            break;
        }
        unsafe {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            begin_cell(0, 0); draw_linear(); end_cell();
            begin_cell(1, 0); draw_exp(); end_cell();
            begin_cell(2, 0); draw_exp2(); end_cell();
            begin_cell(3, 0); draw_ortho(); end_cell();
            begin_cell(0, 1); draw_floor(); end_cell();
            begin_cell(1, 1); draw_depth_range(); end_cell();
            begin_cell(2, 1); draw_lines_points(); end_cell();
            begin_cell(3, 1); draw_alpha(); end_cell();
            c3dglSwapBuffers();
        }
    }
}
