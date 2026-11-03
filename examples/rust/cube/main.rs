//! Two textured cubes in separate viewports, a PNG texture, scissor test and a line outline.
//! The checker cube (right) is tinted per face, the image cube (top left) is drawn untinted.
#[path = "../common/mod.rs"]
mod common;

use c3dgl::*;
use common::gl::*;
use common::{App, Renderer, StatsOverlay, SCREEN_HEIGHT, SCREEN_WIDTH};
use ctru::prelude::*;

const TEX_SIZE: usize = 12; // Not a power of two on purpose

static TONK_PNG: &[u8] = include_bytes!("tonk.png");

struct Face {
    vertices: [[f32; 3]; 4], // Counter-clockwise seen from outside
    color: [u8; 3],
}

const CUBE: [Face; 6] = [
    Face { vertices: [[-1., -1., 1.], [1., -1., 1.], [1., 1., 1.], [-1., 1., 1.]], color: [255, 80, 80] }, // +z
    Face { vertices: [[1., -1., -1.], [-1., -1., -1.], [-1., 1., -1.], [1., 1., -1.]], color: [80, 255, 80] }, // -z
    Face { vertices: [[-1., -1., -1.], [-1., -1., 1.], [-1., 1., 1.], [-1., 1., -1.]], color: [80, 80, 255] }, // -x
    Face { vertices: [[1., -1., 1.], [1., -1., -1.], [1., 1., -1.], [1., 1., 1.]], color: [255, 255, 80] }, // +x
    Face { vertices: [[-1., 1., 1.], [1., 1., 1.], [1., 1., -1.], [-1., 1., -1.]], color: [80, 255, 255] }, // +y
    Face { vertices: [[-1., -1., -1.], [1., -1., -1.], [1., -1., 1.], [-1., -1., 1.]], color: [255, 80, 255] }, // -y
];

const UVS: [[f32; 2]; 4] = [[0., 0.], [1., 0.], [1., 1.], [0., 1.]];

unsafe fn upload(width: i32, height: i32, pixels: &[u8], filter: u32) -> u32 {
    let mut id = 0;
    glGenTextures(1, &mut id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA as i32, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.as_ptr().cast());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE as i32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE as i32);
    id
}

unsafe fn create_checker_texture() -> u32 {
    let mut pixels = [255u8; TEX_SIZE * TEX_SIZE * 4];
    for y in 0..TEX_SIZE {
        for x in 0..TEX_SIZE {
            let value = if (x / 2 + y / 2) % 2 == 0 { 255 } else { 90 };
            pixels[(y * TEX_SIZE + x) * 4..][..3].fill(value);
        }
    }
    upload(TEX_SIZE as i32, TEX_SIZE as i32, &pixels, GL_NEAREST)
}

/// PNG as RGBA8 texture, 0 on failure. Rows are stored top to bottom, so v = 0 is the top of the image.
unsafe fn load_png_texture(data: &[u8]) -> u32 {
    let mut decoder = png::Decoder::new(data);
    decoder.set_transformations(png::Transformations::EXPAND | png::Transformations::STRIP_16);
    let Ok(mut reader) = decoder.read_info() else { return 0 };
    let mut buffer = vec![0; reader.output_buffer_size()];
    let Ok(info) = reader.next_frame(&mut buffer) else { return 0 };
    let bytes = &buffer[..info.buffer_size()];
    let rgba: Vec<u8> = match info.color_type {
        png::ColorType::Rgba => bytes.to_vec(),
        png::ColorType::Rgb => bytes.chunks(3).flat_map(|p| [p[0], p[1], p[2], 255]).collect(),
        png::ColorType::GrayscaleAlpha => bytes.chunks(2).flat_map(|p| [p[0], p[0], p[0], p[1]]).collect(),
        png::ColorType::Grayscale => bytes.iter().flat_map(|&g| [g, g, g, 255]).collect(),
        png::ColorType::Indexed => return 0,
    };
    upload(info.width as i32, info.height as i32, &rgba, GL_LINEAR)
}

/// Cube with the texture on every face, in the given viewport. Face colors tint the texture,
/// white leaves it unchanged.
unsafe fn draw_cube(texture: u32, angle: f32, x: i32, y: i32, w: i32, h: i32, tinted: bool) {
    glViewport(x, y, w, h);

    let aspect = w as f64 / h as f64;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.1 * aspect, 0.1 * aspect, -0.1, 0.1, 0.1, 100.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0., 0., -5.);
    glRotatef(25., 1., 0., 0.);
    glRotatef(angle, 0., 1., 0.);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBegin(GL_QUADS);
    for face in &CUBE {
        if tinted {
            glColor4ub(face.color[0], face.color[1], face.color[2], 255);
        } else {
            glColor4ub(255, 255, 255, 255);
        }
        for (uv, vertex) in UVS.iter().zip(&face.vertices) {
            // Vertices go counter-clockwise from the bottom left, the image's v = 0 is its top row.
            glTexCoord2f(uv[0], if tinted { uv[1] } else { 1. - uv[1] });
            glVertex3f(vertex[0], vertex[1], vertex[2]);
        }
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
}

unsafe fn draw_overlay() {
    // Full screen, y down like most 2D code.
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0., SCREEN_WIDTH as f64, SCREEN_HEIGHT as f64, 0., -1., 1.);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Scissor box in GL coordinates: 10 px from the left, 10 px from the BOTTOM, 120x50.
    let (sx, sy, sw, sh) = (10, 10, 120, 50);
    glEnable(GL_SCISSOR_TEST);
    glScissor(sx, sy, sw, sh);

    // Large quad, only the scissor box of it must be visible.
    glColor4ub(255, 200, 0, 255);
    glBegin(GL_QUADS);
    glVertex2f(0., 0.);
    glVertex2f(200., 0.);
    glVertex2f(200., 240.);
    glVertex2f(0., 240.);
    glEnd();
    glDisable(GL_SCISSOR_TEST);

    // Outline 2 px outside the scissor box (y down: top = height - (sy + sh)).
    let (left, right) = (sx as f32 - 2., (sx + sw) as f32 + 2.);
    let (top, bottom) = ((SCREEN_HEIGHT - (sy + sh)) as f32 - 2., (SCREEN_HEIGHT - sy) as f32 + 2.);
    glColor4ub(255, 255, 255, 255);
    glBegin(GL_LINES);
    for (a, b) in [((left, top), (right, top)), ((right, top), (right, bottom)), ((right, bottom), (left, bottom)), ((left, bottom), (left, top))] {
        glVertex2f(a.0, a.1);
        glVertex2f(b.0, b.1);
    }
    glEnd();
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut app = App::new();
    let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };

    print!(
        "c3dgl cube test\n\n\n\n\nExpected on the top screen:\n- cube in the RIGHT half, solid\n  (no see-through faces), 6x6\n  checker per face, no dark band\n- yellow box BOTTOM LEFT, inside\n  a white outline with 2px gap\n- cube TOP LEFT with the tonk\n  image upright on the side faces\n\nSTART: exit\n"
    );

    // SAFETY: single thread, renderer initialised and kept alive by `_renderer`.
    let (texture, image) = unsafe {
        let texture = create_checker_texture();
        let image = load_png_texture(TONK_PNG);
        glClearColor(0.12, 0.12, 0.15, 1.);
        (texture, image)
    };

    let mut stats = StatsOverlay::new();
    let mut angle = 0f32;
    while app.frame() {
        stats.print();
        if app.down(KeyPad::START) {
            break;
        }

        unsafe {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            // Checker cube in the right half, image cube in the top left quarter (GL viewport: y up).
            let (half_w, h) = (SCREEN_WIDTH / 2, SCREEN_HEIGHT);
            draw_cube(texture, angle, half_w, 0, half_w, h, true);
            draw_cube(image, -angle, 0, h / 2, half_w, h / 2, false);
            draw_overlay();
            c3dglSwapBuffers();
        }
        angle += 1.;
    }

    unsafe {
        glDeleteTextures(1, &image);
        glDeleteTextures(1, &texture);
    }
}
