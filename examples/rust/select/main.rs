//! Feedback and selection. A ring of cubes turns in front of the camera; the cursor (circle pad or
//! D-pad) picks the nearest cube under it with glRenderMode(GL_SELECT) and a pick matrix (one name
//! per cube, the hit with the smallest depth wins). The picked cube is rendered once more in
//! GL_FEEDBACK mode (GL_2D, back faces culled) and the polygons it gives back are drawn as a yellow
//! outline in window coordinates, which must sit exactly on the cube. The bottom screen lists the
//! hit records. A: pause the ring.
#[path = "../common/mod.rs"]
mod common;

use c3dgl::raw::*;
use common::gl::*;
use common::{glu_pick_matrix, App, Renderer, StatsOverlay, SCREEN_HEIGHT, SCREEN_WIDTH};
use ctru::prelude::*;

const CUBES: usize = 6;
const SELECT_SIZE: usize = 256;
const FEEDBACK_SIZE: usize = 4096;

static mut SELECT_BUFFER: [u32; SELECT_SIZE] = [0; SELECT_SIZE];
static mut FEEDBACK_BUFFER: [f32; FEEDBACK_SIZE] = [0.0; FEEDBACK_SIZE];

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

unsafe fn projection() {
    glMatrixMode(GL_PROJECTION);
    let aspect = SCREEN_WIDTH as f64 / SCREEN_HEIGHT as f64;
    glFrustum(-aspect * 0.5, aspect * 0.5, -0.5, 0.5, 1.0, 30.0);
    glMatrixMode(GL_MODELVIEW);
}

/// The ring: cube i gets name i + 1 (the names are ignored outside selection mode).
unsafe fn scene(angle: f32, picked: usize) {
    const COLORS: [[u8; 3]; CUBES] = [[230, 70, 70], [70, 200, 90], [80, 120, 240], [230, 200, 60], [200, 90, 220], [70, 210, 210]];
    glLoadIdentity();
    glTranslatef(0.0, -0.3, -9.0);
    glRotatef(15.0, 1.0, 0.0, 0.0);
    glInitNames();
    glPushName(0);
    for (i, color) in COLORS.iter().enumerate() {
        glLoadName(i as u32 + 1);
        glPushMatrix();
        glRotatef(angle + i as f32 * 360.0 / CUBES as f32, 0.0, 1.0, 0.0);
        glTranslatef(3.2, 0.0, 0.0);
        glRotatef(angle * 2.0 + i as f32 * 40.0, 1.0, 0.3, 0.0);
        glScalef(0.7, 0.7, 0.7);
        let k = if i + 1 == picked { 1.0 } else { 0.75 };
        glColor3f(color[0] as f32 / 255.0 * k, color[1] as f32 / 255.0 * k, color[2] as f32 / 255.0 * k);
        cube();
        glPopMatrix();
    }
    glPopName();
}

unsafe fn transform_cube(angle: f32, i: usize) {
    glLoadIdentity();
    glTranslatef(0.0, -0.3, -9.0);
    glRotatef(15.0, 1.0, 0.0, 0.0);
    glRotatef(angle + i as f32 * 360.0 / CUBES as f32, 0.0, 1.0, 0.0);
    glTranslatef(3.2, 0.0, 0.0);
    glRotatef(angle * 2.0 + i as f32 * 40.0, 1.0, 0.3, 0.0);
    glScalef(0.7, 0.7, 0.7);
}

unsafe fn setup_scene() {
    glSelectBuffer(SELECT_SIZE as i32, std::ptr::addr_of_mut!(SELECT_BUFFER).cast());
    glFeedbackBuffer(FEEDBACK_SIZE as i32, GL_2D, std::ptr::addr_of_mut!(FEEDBACK_BUFFER).cast());
    glClearColor(0.08, 0.08, 0.15, 1.0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

unsafe fn draw_frame(angle: f32, cursor_x: f32, cursor_y: f32, picked: &mut usize) {
    let select = &*std::ptr::addr_of!(SELECT_BUFFER);
    let feedback = &*std::ptr::addr_of!(FEEDBACK_BUFFER);

    // Selection: a 5x5 pixel pick region around the cursor, the hit with the smallest depth wins
    let mut viewport = [0i32; 4];
    glGetIntegerv(GL_VIEWPORT, viewport.as_mut_ptr());
    glRenderMode(GL_SELECT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glu_pick_matrix(cursor_x as f64, cursor_y as f64, 5.0, 5.0, &viewport);
    projection();
    scene(angle, 0);
    let hits = glRenderMode(GL_RENDER);
    *picked = 0;
    let mut nearest = 0xFFFF_FFFFu32;
    println!("\x1b[15;1HHits: {hits}\x1b[K");
    let mut record = 0usize;
    for h in 0..hits.max(0) as usize {
        let (names, zmin) = (select[record] as usize, select[record + 1]);
        if h < 6 {
            println!("  cube {}  z {:.4} .. {:.4}\x1b[K", select[record + 3], zmin as f64 / 4294967295.0, select[record + 2] as f64 / 4294967295.0);
        }
        if names > 0 && zmin < nearest {
            nearest = zmin;
            *picked = select[record + 2 + names] as usize;
        }
        record += 3 + names;
    }
    for _ in hits.max(0)..6 {
        println!("\x1b[K");
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    projection();
    scene(angle, *picked);

    // Feedback: the window coordinates of the picked cube's front faces
    let mut values = 0;
    if *picked > 0 {
        glRenderMode(GL_FEEDBACK);
        transform_cube(angle, *picked - 1);
        cube();
        values = glRenderMode(GL_RENDER);
    }
    print!("\x1b[23;1HPicked: {}, feedback values: {values}\x1b[K", *picked);

    // Outlines and cursor in window coordinates
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, SCREEN_WIDTH as f64, 0.0, SCREEN_HEIGHT as f64, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0);
    glColor3f(1.0, 0.9, 0.2);
    let mut i = 0usize;
    while (i as i32) < values {
        let token = feedback[i] as u32;
        i += 1;
        if token != GL_POLYGON_TOKEN {
            break; // Only polygons are fed back here
        }
        let n = feedback[i] as usize;
        i += 1;
        glBegin(GL_LINE_LOOP);
        for _ in 0..n {
            glVertex2f(feedback[i], feedback[i + 1]);
            i += 2;
        }
        glEnd();
    }
    glLineWidth(1.0);
    glColor3f(1.0, 1.0, 1.0);
    let (x, y) = (cursor_x, cursor_y);
    glBegin(GL_LINES);
    for (a, b) in [((x - 8.0, y), (x - 2.0, y)), ((x + 2.0, y), (x + 8.0, y)), ((x, y - 8.0), (x, y - 2.0)), ((x, y + 2.0), (x, y + 8.0))] {
        glVertex2f(a.0, a.1);
        glVertex2f(b.0, b.1);
    }
    glEnd();
    glEnable(GL_DEPTH_TEST);
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut app = App::new();
    let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };

    unsafe { setup_scene() };

    print!(
        "c3dgl select\n\n\n\n\nCursor: circle pad / D-pad.\nThe nearest cube under the cursor\nis picked by GL_SELECT with\na pick matrix (brighter); its front\nfaces come back from GL_FEEDBACK\nand are outlined in yellow.\n\nA: pause   START: exit\n\n"
    );

    let (mut paused, mut angle, mut cursor_x, mut cursor_y) = (false, 0.0f32, 200.0f32, 120.0f32);
    let mut picked = 0usize;
    let mut stats = StatsOverlay::new();
    while app.frame() {
        stats.print();
        if app.down(KeyPad::START) {
            break;
        }
        if app.down(KeyPad::A) {
            paused = !paused;
        }
        if !paused {
            angle += 0.4;
        }
        let (dx, dy) = app.circle_pad();
        let axis = |plus: KeyPad, minus: KeyPad| (if app.held(plus) { 2.0 } else { 0.0 }) - (if app.held(minus) { 2.0 } else { 0.0 });
        cursor_x += dx as f32 / 40.0 + axis(KeyPad::DPAD_RIGHT, KeyPad::DPAD_LEFT);
        cursor_y += dy as f32 / 40.0 + axis(KeyPad::DPAD_UP, KeyPad::DPAD_DOWN);
        cursor_x = cursor_x.clamp(0.0, SCREEN_WIDTH as f32 - 1.0);
        cursor_y = cursor_y.clamp(0.0, SCREEN_HEIGHT as f32 - 1.0);
        unsafe {
            draw_frame(angle, cursor_x, cursor_y, &mut picked);
            c3dglSwapBuffers();
        }
    }
}
