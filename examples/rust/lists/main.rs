//! Display lists. Three lit gears (the classic "gears" scene), each compiled into a display list
//! once (geometry, normals, material and shade model) and drawn with glCallList every frame.
//! A switches to drawing the same gears in immediate mode, which must look identical; the circle
//! pad turns the view.
#[path = "../common/mod.rs"]
mod common;

use c3dgl::*;
use common::gl::*;
use common::{App, Renderer, SCREEN_HEIGHT, SCREEN_WIDTH};
use ctru::prelude::*;

const PI: f32 = 3.14159265;

/// A gear in the xy plane, centered on the z axis: a hole of radius `inner`, `teeth` teeth of depth
/// `tooth_depth` around radius `outer`, `width` thick. Flat shaded, except the smooth hole.
unsafe fn gear(inner: f32, outer: f32, width: f32, teeth: i32, tooth_depth: f32) {
    let (r0, r1, r2) = (inner, outer - tooth_depth / 2.0, outer + tooth_depth / 2.0);
    let da = 2.0 * PI / teeth as f32 / 4.0; // A tooth: rise, top, fall, gap
    let z = width * 0.5;
    let angle_of = |i: i32| i as f32 * 2.0 * PI / teeth as f32;

    glShadeModel(GL_FLAT);

    // Front and back faces: rings between the hole and the tooth roots, then the faces of the teeth
    for side in 0..2 {
        let s = if side == 1 { -1.0 } else { 1.0 };
        glNormal3f(0.0, 0.0, s);
        glBegin(GL_QUAD_STRIP);
        for i in 0..=teeth {
            let angle = angle_of(i);
            let (a, b) = (angle, angle + 3.0 * da);
            if side == 0 {
                glVertex3f(r0 * a.cos(), r0 * a.sin(), z);
                glVertex3f(r1 * a.cos(), r1 * a.sin(), z);
                if i < teeth {
                    glVertex3f(r0 * a.cos(), r0 * a.sin(), z);
                    glVertex3f(r1 * b.cos(), r1 * b.sin(), z);
                }
            } else {
                glVertex3f(r1 * a.cos(), r1 * a.sin(), -z);
                glVertex3f(r0 * a.cos(), r0 * a.sin(), -z);
                if i < teeth {
                    glVertex3f(r1 * b.cos(), r1 * b.sin(), -z);
                    glVertex3f(r0 * a.cos(), r0 * a.sin(), -z);
                }
            }
        }
        glEnd();
        glBegin(GL_QUADS);
        for i in 0..teeth {
            let angle = angle_of(i);
            let rs = [r1, r2, r2, r1];
            for k in 0..4 {
                let corner = if side == 1 { 3 - k } else { k }; // Counter-clockwise seen from outside
                let a = angle + corner as f32 * da;
                glVertex3f(rs[corner] * a.cos(), rs[corner] * a.sin(), s * z);
            }
        }
        glEnd();
    }

    // Outward faces of the teeth: rise, top, fall and the gap to the next tooth, each with its own normal
    glBegin(GL_QUAD_STRIP);
    for i in 0..teeth {
        let angle = angle_of(i);
        let a = [angle, angle + da, angle + 2.0 * da, angle + 3.0 * da, angle + 4.0 * da];
        let r = [r1, r2, r2, r1, r1];
        for k in 0..4 {
            // Normal of the face from point k to point k + 1: perpendicular to it in the xy plane
            let (x0, y0) = (r[k] * a[k].cos(), r[k] * a[k].sin());
            let (x1, y1) = (r[k + 1] * a[k + 1].cos(), r[k + 1] * a[k + 1].sin());
            glVertex3f(x0, y0, z);
            glVertex3f(x0, y0, -z);
            glNormal3f(y1 - y0, x0 - x1, 0.0);
        }
    }
    glVertex3f(r1, 0.0, z);
    glVertex3f(r1, 0.0, -z);
    glEnd();

    // The hole, smooth shaded with normals pointing inwards
    glShadeModel(GL_SMOOTH);
    glBegin(GL_QUAD_STRIP);
    for i in 0..=teeth {
        let angle = angle_of(i);
        glNormal3f(-angle.cos(), -angle.sin(), 0.0);
        glVertex3f(r0 * angle.cos(), r0 * angle.sin(), -z);
        glVertex3f(r0 * angle.cos(), r0 * angle.sin(), z);
    }
    glEnd();
}

struct GearDef {
    color: [f32; 4],
    inner: f32,
    outer: f32,
    width: f32,
    teeth: i32,
}

const GEARS: [GearDef; 3] = [
    GearDef { color: [0.8, 0.1, 0.0, 1.0], inner: 1.0, outer: 4.0, width: 1.0, teeth: 20 },
    GearDef { color: [0.0, 0.8, 0.2, 1.0], inner: 0.5, outer: 2.0, width: 2.0, teeth: 10 },
    GearDef { color: [0.2, 0.2, 1.0, 1.0], inner: 1.3, outer: 2.0, width: 0.5, teeth: 10 },
];

/// Everything a gear's list contains.
unsafe fn draw_gear(i: usize) {
    let g = &GEARS[i];
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, g.color.as_ptr());
    gear(g.inner, g.outer, g.width, g.teeth, 0.7);
}

unsafe fn draw_scene(lists: u32, use_lists: bool, angle: f32, view_x: f32, view_y: f32) {
    glPushMatrix();
    glRotatef(view_x, 1.0, 0.0, 0.0);
    glRotatef(view_y, 0.0, 1.0, 0.0);

    // Position and rotation of each gear, so that the teeth mesh
    let place = [[-3.0, -2.0, angle], [3.1, -2.0, -2.0 * angle - 9.0], [-3.1, 4.2, -2.0 * angle - 25.0]];
    for (i, p) in place.iter().enumerate() {
        glPushMatrix();
        glTranslatef(p[0], p[1], 0.0);
        glRotatef(p[2], 0.0, 0.0, 1.0);
        if use_lists {
            glCallList(lists + i as u32);
        } else {
            draw_gear(i);
        }
        glPopMatrix();
    }
    glPopMatrix();
}

unsafe fn create_lists() -> u32 {
    // One list per gear, compiled once
    let lists = glGenLists(3);
    for i in 0..3 {
        glNewList(lists + i as u32, GL_COMPILE);
        draw_gear(i);
        glEndList();
    }
    lists
}

unsafe fn setup_scene() {
    let light_pos = [5.0f32, 5.0, 10.0, 0.0];
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    let aspect = SCREEN_WIDTH as f64 / SCREEN_HEIGHT as f64;
    glFrustum(-aspect, aspect, -1.0, 1.0, 5.0, 60.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0, 0.0, -40.0);
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos.as_ptr());
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);
    glClearColor(0.0, 0.0, 0.0, 1.0);
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut app = App::new();
    let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };

    let lists = unsafe {
        let lists = create_lists();
        setup_scene();
        lists
    };

    print!(
        "c3dgl display lists\n\n\n\n\nThree gears, each one a display\nlist (glNewList once, glCallList\nevery frame). Lists and immediate\nmode must look identical.\n\nA: lists / immediate mode\nCircle pad: turn the view\nSTART: exit\n\n"
    );
    println!("{}", if unsafe { glGetError() } == GL_NO_ERROR { "No GL errors" } else { "GL ERROR" });

    let (mut use_lists, mut angle, mut view_x, mut view_y) = (true, 0.0f32, 20.0f32, 30.0f32);
    while app.frame() {
        if app.down(KeyPad::START) {
            break;
        }
        if app.down(KeyPad::A) {
            use_lists = !use_lists;
        }
        let (dx, dy) = app.circle_pad();
        view_y += dx as f32 / 50.0;
        view_x -= dy as f32 / 50.0;
        print!("\x1b[s\x1b[16;1HDrawing: {}\x1b[K\x1b[u", if use_lists { "display lists" } else { "immediate mode" });

        unsafe {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            draw_scene(lists, use_lists, angle, view_x, view_y);
            c3dglSwapBuffers();
        }
        angle += 2.0;
    }

    unsafe { glDeleteLists(lists, 3) };
}
