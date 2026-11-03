//! Texture coordinate generation. Tori without texcoords, glTexGen makes them:
//! left GL_SPHERE_MAP (chrome), middle GL_OBJECT_LINEAR (stripes turn with the torus), right
//! GL_EYE_LINEAR (stripes stay fixed on the screen). A: scroll the stripes (texture matrix),
//! circle pad: turn the tori.
#[path = "../common/mod.rs"]
mod common;

use c3dgl::raw::*;
use common::gl::*;
use common::{App, Renderer, StatsOverlay, SCREEN_HEIGHT, SCREEN_WIDTH};
use ctru::prelude::*;

const PI: f32 = 3.14159265;

/// Torus around the z axis as indexed triangles, positions and normals only (texgen makes the texcoords).
struct Torus {
    positions: Vec<f32>,
    normals: Vec<f32>,
    indices: Vec<u16>,
}

impl Torus {
    fn new(major: f32, minor: f32, rings: usize, sides: usize) -> Torus {
        let mut torus = Torus { positions: vec![], normals: vec![], indices: vec![] };
        for i in 0..=rings {
            let u = i as f32 * 2.0 * PI / rings as f32;
            for j in 0..=sides {
                let v = j as f32 * 2.0 * PI / sides as f32;
                let (nx, ny, nz) = (u.cos() * v.cos(), u.sin() * v.cos(), v.sin());
                torus.positions.extend([u.cos() * major + minor * nx, u.sin() * major + minor * ny, minor * nz]);
                torus.normals.extend([nx, ny, nz]);
            }
        }
        for i in 0..rings {
            for j in 0..sides {
                let a = (i * (sides + 1) + j) as u16;
                let b = a + sides as u16 + 1;
                torus.indices.extend([a, b, a + 1, a + 1, b, b + 1]);
            }
        }
        torus
    }

    unsafe fn draw(&self) {
        glVertexPointer(3, GL_FLOAT, 0, self.positions.as_ptr().cast());
        glNormalPointer(GL_FLOAT, 0, self.normals.as_ptr().cast());
        glDrawElements(GL_TRIANGLES, self.indices.len() as i32, GL_UNSIGNED_SHORT, self.indices.as_ptr().cast());
    }
}

/// Sphere map of a simple world: sky gradient above the horizon, a checkered floor below. Texel
/// (s, t) of a sphere map is the reflection of the view direction (0, 0, -1) at the normal
/// n = (2s - 1, 2t - 1, nz).
unsafe fn make_environment() -> u32 {
    const SIZE: usize = 128;
    let mut texels = vec![0u8; SIZE * SIZE * 4];
    for y in 0..SIZE {
        for x in 0..SIZE {
            let p = &mut texels[(y * SIZE + x) * 4..][..4];
            let nx = 2.0 * (x as f32 + 0.5) / SIZE as f32 - 1.0;
            let ny = 2.0 * (y as f32 + 0.5) / SIZE as f32 - 1.0;
            let nn = nx * nx + ny * ny;
            let mut c = [0f32; 3];
            if nn < 1.0 {
                let nz = (1.0 - nn).sqrt();
                let r = [2.0 * nz * nx, 2.0 * nz * ny, 2.0 * nz * nz - 1.0];
                if r[1] >= 0.0 {
                    // Sky: white at the horizon, deep blue above, a warm sun towards +x
                    let h = r[1];
                    c = [1.0 - 0.8 * h, 1.0 - 0.6 * h, 1.0 - 0.1 * h];
                    let sun = (r[0] * 0.8 + r[1] * 0.6).max(0.0).powf(64.0);
                    c[0] += sun;
                    c[1] += 0.9 * sun;
                    c[2] += 0.6 * sun;
                } else {
                    // Floor checkers, fading to grey towards the horizon
                    let (fx, fz) = (r[0] / -r[1], r[2] / -r[1]);
                    let odd = (fx.floor() as i32 + fz.floor() as i32) & 1 == 1;
                    let fade = (-r[1] * 3.0).min(1.0);
                    let base = if odd { 0.15 } else { 0.75 };
                    c = [0.5 + (base - 0.5) * fade, 0.45 + (base * 0.8 - 0.45) * fade, 0.4 + (base * 0.5 - 0.4) * fade];
                }
            }
            for k in 0..3 {
                p[k] = (c[k].min(1.0) * 255.0) as u8;
            }
            p[3] = 255;
        }
    }
    let mut tex = 0;
    glGenTextures(1, &mut tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA as i32, SIZE as i32, SIZE as i32, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.as_ptr().cast());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP as i32);
    tex
}

/// Stripes along s: a quarter dark red, the rest cream (repeats).
unsafe fn make_stripes() -> u32 {
    const W: usize = 16;
    const H: usize = 8;
    let mut texels = [0u8; W * H * 4];
    for i in 0..W * H {
        let dark = i % W < 4;
        texels[4 * i..4 * i + 4].copy_from_slice(&[if dark { 140 } else { 255 }, if dark { 20 } else { 240 }, if dark { 20 } else { 200 }, 255]);
    }
    let mut tex = 0;
    glGenTextures(1, &mut tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA as i32, W as i32, H as i32, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.as_ptr().cast());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR as i32);
    tex
}

unsafe fn setup_scene() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    let aspect = SCREEN_WIDTH as f64 / SCREEN_HEIGHT as f64;
    glFrustum(-aspect * 0.5, aspect * 0.5, -0.5, 0.5, 1.0, 50.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_POSITION, [0.3f32, 0.6, 1.0, 0.0].as_ptr());
    let stripe_plane = [1.5f32, 1.5, 0.0, 0.0];
    glTexGenfv(GL_S, GL_EYE_PLANE, stripe_plane.as_ptr());
    glTexGenfv(GL_S, GL_OBJECT_PLANE, stripe_plane.as_ptr());
    let white = [1.0f32; 4];
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, white.as_ptr());
    glMaterialfv(GL_FRONT, GL_SPECULAR, white.as_ptr());
    glMaterialf(GL_FRONT, GL_SHININESS, 40.0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHT0);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glClearColor(0.1, 0.1, 0.15, 1.0);
}

unsafe fn draw_frame(torus: &Torus, environment: u32, stripes: u32, angle: f32, offset: f32, tilt_x: f32, tilt_y: f32) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glTranslatef(offset, 0.0, 0.0);
    glMatrixMode(GL_MODELVIEW);
    for i in 0..3 {
        glPushMatrix();
        glTranslatef((i as f32 - 1.0) * 2.9, 0.0, -8.0);
        glRotatef(tilt_x, 1.0, 0.0, 0.0);
        glRotatef(tilt_y + angle, 0.0, 1.0, 0.0);
        glRotatef(angle * 0.7, 0.0, 0.0, 1.0);
        if i == 0 {
            // Chrome: the environment replaces the color; the texture matrix stays out of it
            glMatrixMode(GL_TEXTURE);
            glPushMatrix();
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glBindTexture(GL_TEXTURE_2D, environment);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE as i32);
            glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP as i32);
            glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP as i32);
            glEnable(GL_TEXTURE_GEN_S);
            glEnable(GL_TEXTURE_GEN_T);
            torus.draw();
            glDisable(GL_TEXTURE_GEN_T);
            glMatrixMode(GL_TEXTURE);
            glPopMatrix();
            glMatrixMode(GL_MODELVIEW);
        } else {
            // Lit stripes: s from a plane, t from the current texcoord (rows of the texture are all the same)
            glBindTexture(GL_TEXTURE_2D, stripes);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE as i32);
            glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, if i == 1 { GL_OBJECT_LINEAR } else { GL_EYE_LINEAR } as i32);
            glEnable(GL_TEXTURE_GEN_S);
            glEnable(GL_LIGHTING);
            torus.draw();
            glDisable(GL_LIGHTING);
        }
        glDisable(GL_TEXTURE_GEN_S);
        glPopMatrix();
    }
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut app = App::new();
    let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };

    let torus = Torus::new(1.0, 0.45, 32, 16);
    let (environment, stripes) = unsafe {
        let environment = make_environment();
        let stripes = make_stripes();
        setup_scene();
        (environment, stripes)
    };

    print!(
        "c3dgl texgen\n\n\n\n\nTori without texcoords, glTexGen\nmakes them:\n- left: GL_SPHERE_MAP (chrome)\n- middle: GL_OBJECT_LINEAR,\n  stripes turn with the torus\n- right: GL_EYE_LINEAR, stripes\n  stay fixed on the screen\n\nA: scroll the stripes (texture\n   matrix)\nCircle pad: turn the tori\nSTART: exit\n\n"
    );
    println!("{}", if unsafe { glGetError() } == GL_NO_ERROR { "No GL errors" } else { "GL ERROR" });

    let (mut scroll, mut angle, mut offset, mut tilt_x, mut tilt_y) = (false, 0.0f32, 0.0f32, 30.0f32, 0.0f32);
    let mut stats = StatsOverlay::new();
    while app.frame() {
        stats.print();
        if app.down(KeyPad::START) {
            break;
        }
        if app.down(KeyPad::A) {
            scroll = !scroll;
        }
        let (dx, dy) = app.circle_pad();
        tilt_y += dx as f32 / 50.0;
        tilt_x -= dy as f32 / 50.0;
        unsafe {
            draw_frame(&torus, environment, stripes, angle, offset, tilt_x, tilt_y);
            c3dglSwapBuffers();
        }
        angle += 1.0;
        if scroll {
            offset += 0.01;
        }
    }
    unsafe {
        glDeleteTextures(1, &environment);
        glDeleteTextures(1, &stripes);
    }
}
