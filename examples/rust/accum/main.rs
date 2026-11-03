//! The accumulation buffer. Left: motion blur, a cube racing around a circle leaves a fading trail
//! (every frame the accumulation buffer is scaled down with GL_MULT and the new frame added with
//! GL_ACCUM). Right: depth of field, a row of cubes rendered from 6 eye positions on a lens around
//! the focus on the middle cube, averaged with GL_LOAD / GL_ACCUM: the cubes in front and behind it
//! are blurred. The scissor box keeps the two halves apart, accumulation buffer operations only
//! change the pixels in it. A: pause, B: accumulation buffer on/off.
#[path = "../common/mod.rs"]
mod common;

use c3dgl::raw::*;
use common::gl::*;
use common::{App, Renderer, StatsOverlay, SCREEN_HEIGHT, SCREEN_WIDTH};
use ctru::prelude::*;

const PI: f32 = 3.14159265;
const HALF: i32 = SCREEN_WIDTH / 2;
const VIEWS: usize = 6;

unsafe fn cube() {
    const FACES: [[[f32; 3]; 4]; 6] = [
        [[-1., -1., 1.], [1., -1., 1.], [1., 1., 1.], [-1., 1., 1.]],
        [[1., -1., -1.], [-1., -1., -1.], [-1., 1., -1.], [1., 1., -1.]],
        [[1., -1., 1.], [1., -1., -1.], [1., 1., -1.], [1., 1., 1.]],
        [[-1., -1., -1.], [-1., -1., 1.], [-1., 1., 1.], [-1., 1., -1.]],
        [[-1., 1., 1.], [1., 1., 1.], [1., 1., -1.], [-1., 1., -1.]],
        [[-1., -1., -1.], [1., -1., -1.], [1., -1., 1.], [-1., -1., 1.]],
    ];
    const SHADE: [f32; 6] = [1.0, 0.55, 0.8, 0.7, 0.9, 0.45];
    let mut color = [0f32; 4];
    glGetFloatv(GL_CURRENT_COLOR, color.as_mut_ptr());
    glBegin(GL_QUADS);
    for (face, shade) in FACES.iter().zip(SHADE) {
        glColor3f(color[0] * shade, color[1] * shade, color[2] * shade);
        for vertex in face {
            glVertex3fv(vertex.as_ptr());
        }
    }
    glEnd();
    glColor4fv(color.as_ptr());
}

/// Ground grid from z = -2 to -22.
unsafe fn grid() {
    glColor3f(0.35, 0.4, 0.55);
    glBegin(GL_LINES);
    for i in -5..=5 {
        glVertex3f(i as f32, -1.2, -2.0);
        glVertex3f(i as f32, -1.2, -22.0);
    }
    for z in (2..=22).step_by(2) {
        glVertex3f(-5.0, -1.2, -(z as f32));
        glVertex3f(5.0, -1.2, -(z as f32));
    }
    glEnd();
}

/// Perspective of one half of the screen; the eye moved by (eye_x, eye_y) in the lens plane with the
/// frustum sheared so that the plane at distance `focus` stays in place (the classic accFrustum of
/// the OpenGL Programming Guide).
unsafe fn half_projection(eye_x: f32, eye_y: f32, focus: f32) {
    let (z_near, top) = (1.0f32, 0.5f32);
    let right = top * HALF as f32 / SCREEN_HEIGHT as f32;
    let (dx, dy) = (-eye_x * z_near / focus, -eye_y * z_near / focus);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum((-right + dx) as f64, (right + dx) as f64, (-top + dy) as f64, (top + dy) as f64, z_near as f64, 40.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(-eye_x, -eye_y, 0.0);
}

unsafe fn motion_scene(time: f32) {
    half_projection(0.0, 0.0, 1.0);
    glTranslatef(0.0, 0.0, -9.0);
    glRotatef(25.0, 1.0, 0.0, 0.0);
    glPushMatrix();
    glTranslatef(0.0, 0.0, 6.0);
    grid();
    glPopMatrix();

    // Fast cube on a circle, a slow one in the middle
    glPushMatrix();
    glRotatef(time * 4.0, 0.0, 1.0, 0.0);
    glTranslatef(2.6, 0.0, 0.0);
    glRotatef(time * 9.0, 1.0, 0.3, 0.0);
    glScalef(0.5, 0.5, 0.5);
    glColor3f(1.0, 0.55, 0.15);
    cube();
    glPopMatrix();
    glPushMatrix();
    glRotatef(time * 0.8, 0.2, 1.0, 0.0);
    glScalef(0.6, 0.6, 0.6);
    glColor3f(0.3, 0.6, 1.0);
    cube();
    glPopMatrix();
}

unsafe fn depth_scene(time: f32, eye_x: f32, eye_y: f32) {
    let focus = 9.0; // Eye distance of the middle cube
    half_projection(eye_x, eye_y, focus);
    glTranslatef(0.0, -0.4, 0.0);
    grid();
    const COLORS: [[f32; 3]; 5] = [[1.0, 0.35, 0.3], [1.0, 0.8, 0.2], [0.35, 1.0, 0.45], [0.3, 0.7, 1.0], [0.8, 0.45, 1.0]];
    for (i, color) in COLORS.iter().enumerate() {
        let i = i as f32;
        glPushMatrix();
        glTranslatef(-1.6 + 0.8 * i, 0.0, -3.5 - 2.75 * i);
        glRotatef(time + 30.0 * i, 0.3, 1.0, 0.1);
        glScalef(0.45, 0.45, 0.45);
        glColor3fv(color.as_ptr());
        cube();
        glPopMatrix();
    }
}

unsafe fn setup_scene() {
    glClearColor(0.08, 0.08, 0.15, 1.0);
    glClearAccum(0.0, 0.0, 0.0, 0.0);
    glClear(GL_ACCUM_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_SCISSOR_TEST);
}

/// Eye positions on the lens: a ring of 6.
fn make_lens() -> [[f32; 2]; VIEWS] {
    let mut lens = [[0f32; 2]; VIEWS];
    for (i, eye) in lens.iter_mut().enumerate() {
        let angle = i as f32 * 2.0 * PI / VIEWS as f32;
        *eye = [0.22 * angle.cos(), 0.22 * angle.sin()];
    }
    lens
}

unsafe fn draw_frame(time: f32, accum: bool, lens: &[[f32; 2]; VIEWS]) {
    // Left half: motion blur
    glViewport(0, 0, HALF, SCREEN_HEIGHT);
    glScissor(0, 0, HALF, SCREEN_HEIGHT);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    motion_scene(time);
    if accum {
        glAccum(GL_MULT, 0.7);
        glAccum(GL_ACCUM, 0.3);
        glAccum(GL_RETURN, 1.0);
    }

    // Right half: depth of field
    glViewport(HALF, 0, HALF, SCREEN_HEIGHT);
    glScissor(HALF, 0, HALF, SCREEN_HEIGHT);
    for i in 0..if accum { VIEWS } else { 1 } {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        let (x, y) = if accum { (lens[i][0], lens[i][1]) } else { (0.0, 0.0) };
        depth_scene(time, x, y);
        if accum {
            glAccum(if i > 0 { GL_ACCUM } else { GL_LOAD }, 1.0 / VIEWS as f32);
        }
    }
    if accum {
        glAccum(GL_RETURN, 1.0);
    }
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut app = App::new();
    let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };

    unsafe { setup_scene() };

    print!(
        "c3dgl accum\n\n\n\n\nLeft: motion blur. Each frame\nfades the accumulation buffer\n(GL_MULT 0.7) and adds the new\nframe (GL_ACCUM 0.3).\n\nRight: depth of field. 6 views\nfrom points on a lens, focused\non the middle cube, averaged\n(GL_LOAD/GL_ACCUM 1/6).\n\nA: pause   B: accumulation\nSTART: exit\n\n"
    );

    let lens = make_lens();
    let (mut paused, mut accum, mut time) = (false, true, 0.0f32);
    let mut stats = StatsOverlay::new();
    print!("\x1b[s\x1b[19;1HAccumulation: on \x1b[u");
    while app.frame() {
        stats.print();
        if app.down(KeyPad::START) {
            break;
        }
        if app.down(KeyPad::A) {
            paused = !paused;
        }
        if app.down(KeyPad::B) {
            accum = !accum;
            print!("\x1b[s\x1b[19;1HAccumulation: {}\x1b[u", if accum { "on " } else { "off" });
        }
        if !paused {
            time += 1.0;
        }

        unsafe {
            draw_frame(time, accum, &lens);
            c3dglSwapBuffers();
        }
    }
}
