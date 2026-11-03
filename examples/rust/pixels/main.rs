//! Drawing pixels. A spinning cube with a text label at one of its corners (glRasterPos in 3D,
//! glBitmap text depth tested against the cube), its reflection copied below it with glCopyPixels
//! (zoom 1 x -0.5), and an animated glDrawPixels image with a pulsing glPixelZoom next to its mirror
//! image (zoom -1). The image is a color index image: its palette is in the glPixelMap
//! I_TO_R/G/B tables and cycles by GL_INDEX_OFFSET; the mirror image draws the same bytes as
//! luminance, tinted by glPixelTransfer scale/bias. The text is the console font as glBitmap
//! display lists, drawn with glCallLists. A: pause the animation.
#[path = "../common/mod.rs"]
mod common;

use c3dgl::raw::*;
use common::gl::*;
use common::{App, Renderer, StatsOverlay, SCREEN_HEIGHT, SCREEN_WIDTH};
use ctru::prelude::*;

const IMAGE_SIZE: usize = 64;

/// One display list per character (32..127) with the glBitmap of the console font, advancing 8 pixels.
unsafe fn create_font() -> u32 {
    let font = &(*ctru_sys::consoleGetDefault()).font;
    let base = glGenLists(128);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for c in 32..128usize {
        // Console font rows go down, bitmap rows up
        let glyph = font.gfx.add(8 * (c - font.asciiOffset as usize));
        let mut rows = [0u8; 8];
        for (r, row) in rows.iter_mut().enumerate() {
            *row = *glyph.add(7 - r);
        }
        glNewList(base + c as u32, GL_COMPILE);
        glBitmap(8, 8, 0.0, 0.0, 8.0, 0.0, rows.as_ptr());
        glEndList();
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    base
}

/// Text at the current raster position.
unsafe fn text(font: u32, s: &str) {
    glListBase(font);
    glCallLists(s.len() as i32, GL_UNSIGNED_BYTE, s.as_ptr().cast());
}

unsafe fn window_text(font: u32, x: i32, y: i32, s: &str) {
    glRasterPos2i(x, y);
    text(font, s);
}

/// Palette of the color index image: glPixelMap tables I_TO_R, I_TO_G, I_TO_B (256 entries),
/// I_TO_A (1 entry: opaque).
unsafe fn set_palette() {
    static mut PALETTE: [[f32; 256]; 3] = [[0.0; 256]; 3];
    let palette = &mut *std::ptr::addr_of_mut!(PALETTE);
    for i in 0..256 {
        let v = (i as f32 / 255.0 * 6.0 - 3.0) * 2.0;
        for (c, table) in palette.iter_mut().enumerate() {
            table[i] = 0.5 + 0.5 * (v + c as f32 * 2.1).sin();
        }
    }
    glPixelMapfv(GL_PIXEL_MAP_I_TO_R, 256, palette[0].as_ptr());
    glPixelMapfv(GL_PIXEL_MAP_I_TO_G, 256, palette[1].as_ptr());
    glPixelMapfv(GL_PIXEL_MAP_I_TO_B, 256, palette[2].as_ptr());
    let opaque = 1.0f32;
    glPixelMapfv(GL_PIXEL_MAP_I_TO_A, 1, &opaque);
}

/// Plasma, color indices: the sum of three sine waves (per column, row and diagonal).
fn make_image(image: &mut [u8; IMAGE_SIZE * IMAGE_SIZE], t: f32) {
    let mut wave_x = [0f32; IMAGE_SIZE];
    let mut wave_y = [0f32; IMAGE_SIZE];
    let mut wave_xy = [0f32; 2 * IMAGE_SIZE];
    for i in 0..IMAGE_SIZE {
        wave_x[i] = (i as f32 * 0.2 + t).sin();
        wave_y[i] = (i as f32 * 0.15 - t * 1.3).sin();
    }
    for (i, w) in wave_xy.iter_mut().enumerate() {
        *w = (i as f32 * 0.1 + t * 0.7).sin();
    }
    for y in 0..IMAGE_SIZE {
        for x in 0..IMAGE_SIZE {
            let v = wave_x[x] + wave_y[y] + wave_xy[x + y]; // -3..3
            image[y * IMAGE_SIZE + x] = ((v + 3.0) * (255.0 / 6.0)) as u8;
        }
    }
}

unsafe fn cube() {
    const FACES: [[[f32; 3]; 4]; 6] = [
        [[-1., -1., 1.], [1., -1., 1.], [1., 1., 1.], [-1., 1., 1.]],
        [[1., -1., -1.], [-1., -1., -1.], [-1., 1., -1.], [1., 1., -1.]],
        [[1., -1., 1.], [1., -1., -1.], [1., 1., -1.], [1., 1., 1.]],
        [[-1., -1., -1.], [-1., -1., 1.], [-1., 1., 1.], [-1., 1., -1.]],
        [[-1., 1., 1.], [1., 1., 1.], [1., 1., -1.], [-1., 1., -1.]],
        [[-1., -1., -1.], [1., -1., -1.], [1., -1., 1.], [-1., -1., 1.]],
    ];
    const COLORS: [[u8; 3]; 6] = [[230, 60, 60], [60, 200, 80], [70, 110, 230], [230, 200, 50], [200, 80, 220], [60, 210, 210]];
    glBegin(GL_QUADS);
    for (face, color) in FACES.iter().zip(&COLORS) {
        glColor3ubv(color.as_ptr());
        for vertex in face {
            glVertex3fv(vertex.as_ptr());
        }
    }
    glEnd();
}

unsafe fn setup_scene() -> u32 {
    let font = create_font();
    set_palette();
    glClearColor(0.08, 0.08, 0.15, 1.0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    font
}

unsafe fn draw_frame(font: u32, image: &mut [u8; IMAGE_SIZE * IMAGE_SIZE], t: f32) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // The cube in the upper left, in perspective
    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    let aspect = SCREEN_WIDTH as f64 / SCREEN_HEIGHT as f64;
    glFrustum(-aspect * 0.5, aspect * 0.5, -0.5, 0.5, 1.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(-1.6, 1.0, -6.0);
    glRotatef(t * 40.0, 0.3, 1.0, 0.1);
    glScalef(0.6, 0.6, 0.6);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    cube();

    // Label just outside a corner: hidden by the cube when the corner turns away
    glColor3ub(255, 255, 255);
    glRasterPos3f(1.15, 1.15, 1.15);
    text(font, "<corner");
    glDisable(GL_DEPTH_TEST);

    // Window coordinates from here on
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, SCREEN_WIDTH as f64, 0.0, SCREEN_HEIGHT as f64, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Reflection: the band above y = 112 copied upside down and squashed below it
    glRasterPos2i(0, 110);
    glPixelZoom(1.0, -0.5);
    glCopyPixels(0, 112, 200, 112, GL_COLOR);

    // Animated image, zoomed around its center, and its mirror image
    make_image(image, t * 2.0);
    let zoom = 1.25 + 0.25 * (t * 2.0).sin();
    let half = IMAGE_SIZE as f32 * zoom * 0.5;
    glRasterPos2f(270.0 - half, 150.0 - half);
    glPixelZoom(zoom, zoom);
    glPixelTransferi(GL_INDEX_OFFSET, (t * 60.0) as i32 & 255);
    glDrawPixels(IMAGE_SIZE as i32, IMAGE_SIZE as i32, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, image.as_ptr());
    glPixelTransferi(GL_INDEX_OFFSET, 0);
    glRasterPos2i(370, 30);
    glPixelZoom(-1.0, 1.0);
    glPixelTransferf(GL_RED_SCALE, 0.3);
    glPixelTransferf(GL_GREEN_SCALE, 0.7);
    glPixelTransferf(GL_BLUE_BIAS, 0.4);
    glDrawPixels(IMAGE_SIZE as i32, IMAGE_SIZE as i32, GL_LUMINANCE, GL_UNSIGNED_BYTE, image.as_ptr());
    glPixelTransferf(GL_RED_SCALE, 1.0);
    glPixelTransferf(GL_GREEN_SCALE, 1.0);
    glPixelTransferf(GL_BLUE_BIAS, 0.0);
    glPixelZoom(1.0, 1.0);

    glColor3ub(255, 220, 90);
    window_text(font, 8, 228, "glBitmap / glDrawPixels / glCopyPixels");
    glColor3ub(150, 200, 255);
    window_text(font, 40, 4, "glCopyPixels");
    window_text(font, 278, 18, "zoom -1, tinted");
    window_text(font, 214, 214, "glPixelMap palette");
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut app = App::new();
    let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };

    let font = unsafe { setup_scene() };
    let mut image = [0u8; IMAGE_SIZE * IMAGE_SIZE];

    print!(
        "c3dgl pixels\n\n\n\n\nLeft: a cube with a glBitmap label\nat a corner (glRasterPos in 3D,\ndepth tested), its reflection by\nglCopyPixels (zoom 1 x -0.5).\nRight: a color index glDrawPixels\n(glPixelMap palette cycled by\nGL_INDEX_OFFSET), pulsing zoom;\nmirrored by zoom -1 as luminance\ntinted by glPixelTransfer.\nText: glBitmap display lists.\n\nA: pause\nSTART: exit\n\n"
    );
    println!("{}", if unsafe { glGetError() } == GL_NO_ERROR { "No GL errors" } else { "GL ERROR" });

    let (mut paused, mut t) = (false, 0.0f32);
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
            t += 1.0 / 60.0;
        }
        unsafe {
            draw_frame(font, &mut image, t);
            c3dglSwapBuffers();
        }
    }
}
