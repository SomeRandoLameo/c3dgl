//! Line and polygon stipple. Left: stippled lines (dashes, factor 2, dash-dot, dots, a wide line),
//! a turning star drawn as one line loop (the pattern runs on around the corners) and two
//! rectangles with complementary halftone patterns that overlap into one solid rectangle. Right,
//! in perspective: a cube with screen-door transparency (polygon stipple, textured) circles an
//! opaque cube whose edges are outlined with a dashed glPolygonMode(GL_LINE) pass. The stipple
//! pattern is fixed to the window, so it does not move with the turning cube. A: pause.
#[path = "../common/mod.rs"]
mod common;

use c3dgl::raw::*;
use common::gl::*;
use common::{shaded_cube, App, Renderer, StatsOverlay, SCREEN_HEIGHT, SCREEN_WIDTH};
use ctru::prelude::*;

/// 32x32 patterns, rows from the bottom, most significant bit first.
fn checker_pattern(odd: bool) -> [u8; 128] {
    let mut pattern = [0u8; 128];
    for y in 0..32 {
        for i in 0..4 {
            pattern[4 * y + i] = if ((y & 1) == 1) != odd { 0x55 } else { 0xAA };
        }
    }
    pattern
}

/// Halftone dots of 4x4 cells: the cell's center 2x2 pixels (or everything else).
fn dot_pattern(inverse: bool) -> [u8; 128] {
    let mut pattern = [0u8; 128];
    for y in 0..32 {
        let middle = (y & 3) == 1 || (y & 3) == 2;
        for i in 0..4 {
            let dots: u8 = if middle { 0x66 } else { 0x00 };
            pattern[4 * y + i] = if inverse { !dots } else { dots };
        }
    }
    pattern
}

unsafe fn line(x0: f32, y0: f32, x1: f32, y1: f32) {
    glBegin(GL_LINES);
    glVertex2f(x0, y0);
    glVertex2f(x1, y1);
    glEnd();
}

unsafe fn lines(angle: f32) {
    glEnable(GL_LINE_STIPPLE);
    glColor3f(1.0, 1.0, 1.0);
    glLineStipple(1, 0x00FF);
    line(10.0, 225.5, 190.0, 225.5);
    glColor3f(1.0, 0.8, 0.3);
    glLineStipple(2, 0x0F0F);
    line(10.0, 213.5, 190.0, 213.5);
    glColor3f(0.4, 1.0, 0.5);
    glLineStipple(1, 0x1C47);
    line(10.0, 201.5, 190.0, 201.5);
    glColor3f(0.5, 0.7, 1.0);
    glLineStipple(1, 0xAAAA);
    line(10.0, 189.5, 190.0, 189.5);
    glColor3f(1.0, 0.4, 0.4);
    glLineWidth(3.0);
    glLineStipple(3, 0x3F3F);
    line(10.0, 176.5, 190.0, 176.5);
    glLineWidth(1.0);

    // Star: one loop, smooth colors
    glLineStipple(2, 0x3F07);
    glBegin(GL_LINE_LOOP);
    for i in 0..5 {
        let a = (angle + i as f32 * 144.0) * 3.14159265 / 180.0;
        glColor3f(0.6 + 0.4 * a.cos(), 0.6 + 0.4 * a.sin(), 1.0);
        glVertex2f(70.0 + 55.0 * a.sin(), 100.0 + 55.0 * a.cos());
    }
    glEnd();
    glDisable(GL_LINE_STIPPLE);
}

unsafe fn halftone() {
    glEnable(GL_POLYGON_STIPPLE);
    glPolygonStipple(dot_pattern(false).as_ptr());
    glColor3f(1.0, 0.85, 0.2);
    glRectf(135.0, 20.0, 185.0, 110.0);
    glPolygonStipple(dot_pattern(true).as_ptr());
    glColor3f(0.2, 0.5, 1.0);
    glRectf(150.0, 35.0, 195.0, 160.0); // Overlap: the yellow dots and their blue complement fill it solid
    glDisable(GL_POLYGON_STIPPLE);
}

unsafe fn cubes(angle: f32, texture: u32) {
    glViewport(200, 0, 200, 240);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.42, 0.42, -0.5, 0.5, 1.0, 30.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0, 0.0, -9.0);
    glRotatef(20.0, 1.0, 0.0, 0.0);

    // Opaque cube with a dashed outline (polygon offset keeps the outline in front of its faces)
    glPushMatrix();
    glRotatef(angle * 0.7, 0.3, 1.0, 0.0);
    glScalef(0.7, 0.7, 0.7);
    glColor3f(0.3, 0.5, 0.9);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0, 1.0);
    shaded_cube(true);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(1, 0x0F0F);
    glColor3f(1.0, 1.0, 1.0);
    shaded_cube(true);
    glDisable(GL_LINE_STIPPLE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glPopMatrix();

    // Screen door cube, textured: half of its pixels are left out
    glPolygonStipple(checker_pattern(false).as_ptr());
    glEnable(GL_POLYGON_STIPPLE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPushMatrix();
    glRotatef(angle, 0.0, 1.0, 0.0);
    glTranslatef(0.0, 0.0, 2.2);
    glRotatef(angle * 2.0, 1.0, 0.4, 0.2);
    glScalef(0.75, 0.75, 0.75);
    glColor3f(1.0, 0.6, 0.2);
    shaded_cube(true);
    glPopMatrix();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_POLYGON_STIPPLE);
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

/// 4x4 checker of light and dark texels.
unsafe fn create_texture() -> u32 {
    let mut texels = [0u8; 16];
    for (i, texel) in texels.iter_mut().enumerate() {
        *texel = if ((i & 3) + (i >> 2)) & 1 == 1 { 255 } else { 110 };
    }
    let mut texture = 0;
    glGenTextures(1, &mut texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST as i32);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE as i32, 4, 4, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, texels.as_ptr().cast());
    texture
}

unsafe fn setup_scene() {
    glClearColor(0.08, 0.08, 0.15, 1.0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

unsafe fn draw_frame(angle: f32, texture: u32) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, SCREEN_WIDTH as f64, 0.0, SCREEN_HEIGHT as f64, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    lines(angle);
    halftone();
    glEnable(GL_DEPTH_TEST);
    cubes(angle, texture);
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut app = App::new();
    let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };

    let texture = unsafe {
        let texture = create_texture();
        setup_scene();
        texture
    };

    print!(
        "c3dgl stipple\n\n\n\n\nLeft: stippled lines (dashes,\nfactor 2, dash-dot, dots, wide),\na star as one line loop and two\nhalftone rectangles whose dots\nfill each other in where they\noverlap (solid there).\n\nRight: a textured screen door\ncube (polygon stipple, pattern\nfixed to the window) circles an\nopaque cube with dashed edges.\n\nA: pause   START: exit\n"
    );

    let (mut paused, mut angle) = (false, 0.0f32);
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
            angle += 0.5;
        }
        unsafe {
            draw_frame(angle, texture);
            c3dglSwapBuffers();
        }
    }
    unsafe { glDeleteTextures(1, &texture) };
}
