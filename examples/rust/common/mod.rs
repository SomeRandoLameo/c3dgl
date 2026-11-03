//! Shared support for the examples: application setup, renderer lifetime and the statistics
//! overlay of the bottom screen console. Not part of the c3dgl API.
#![allow(dead_code)]

pub mod gl;

use c3dgl::raw::{c3dglClose, c3dglInit, c3dglNativeGetStats, Stats};
use ctru::prelude::*;
use std::time::Instant;

pub const SCREEN_WIDTH: i32 = 400;
pub const SCREEN_HEIGHT: i32 = 240;
const SYSCLOCK_ARM11: f64 = 268_111_856.0;
/// The GPU command buffer is 32768 words.
const COMMAND_BYTES: f64 = 32768.0 * 4.0;

/// Apt and Hid services. Typical start of an example (drop order matters, the renderer goes first):
///
/// ```ignore
/// let gfx = Gfx::new().unwrap();
/// let _console = Console::new(gfx.bottom_screen.borrow_mut());
/// let mut app = App::new();
/// let Some(_renderer) = Renderer::new(&mut app, &gfx) else { return };
/// ```
pub struct App {
    pub apt: Apt,
    pub hid: Hid,
}

impl App {
    pub fn new() -> App {
        App { apt: Apt::new().expect("apt"), hid: Hid::new().expect("hid") }
    }

    /// `aptMainLoop` plus input scan; false when the application should end.
    pub fn frame(&mut self) -> bool {
        if !self.apt.main_loop() {
            return false;
        }
        self.hid.scan_input();
        true
    }

    /// Circle pad position, -156..156 per axis.
    pub fn circle_pad(&self) -> (i16, i16) {
        self.hid.circlepad_position()
    }

    pub fn down(&self, key: KeyPad) -> bool {
        self.hid.keys_down().contains(key)
    }

    pub fn held(&self, key: KeyPad) -> bool {
        self.hid.keys_held().contains(key)
    }
}

/// Owns the renderer: `c3dglClose` on drop.
pub struct Renderer;

impl Renderer {
    /// Initialises the renderer; on failure waits for START so the message stays readable.
    pub fn new(app: &mut App, gfx: &Gfx) -> Option<Renderer> {
        // SAFETY: graphics are initialised by `App` and this is the only renderer.
        if unsafe { c3dglInit() } {
            return Some(Renderer);
        }
        println!("c3dglInit failed");
        while app.frame() {
            if app.down(KeyPad::START) {
                break;
            }
            gfx.wait_for_vblank();
        }
        None
    }
}

impl Drop for Renderer {
    fn drop(&mut self) {
        // SAFETY: initialised in `new`.
        unsafe { c3dglClose() }
    }
}

pub fn frame_stats() -> Stats {
    let mut stats = Stats { cpu_ticks: 0, gpu_ticks: 0, wait_ticks: 0, command_bytes: 0, draws: 0 };
    // SAFETY: only called while the renderer is initialised.
    unsafe { c3dglNativeGetStats(&mut stats) };
    stats
}

/// Rows 2-5 of the console: timings of the last frame, frames per second over the last second;
/// the cursor stays where the text ended.
pub struct StatsOverlay {
    start: Instant,
    frames: u32,
    fps: f32,
}

impl StatsOverlay {
    pub fn new() -> Self {
        StatsOverlay { start: Instant::now(), frames: 0, fps: 0.0 }
    }

    pub fn print(&mut self) {
        let stats = frame_stats();
        self.frames += 1;
        let elapsed = self.start.elapsed().as_secs_f32();
        if elapsed >= 1.0 {
            self.fps = self.frames as f32 / elapsed;
            self.frames = 0;
            self.start = Instant::now();
        }
        print!(
            "\x1b[s\x1b[2;1HCPU:     {:6.2}ms\x1b[K\x1b[3;1HGPU:     {:6.2}ms\x1b[K\x1b[4;1HCmdBuf:  {:6.2}%\x1b[K\x1b[5;1HFPS:     {:6.2}\x1b[K\x1b[u",
            stats.cpu_ticks as f64 * 1000.0 / SYSCLOCK_ARM11,
            stats.gpu_ticks as f64 * 1000.0 / SYSCLOCK_ARM11,
            stats.command_bytes as f64 / COMMAND_BYTES * 100.0,
            self.fps,
        );
    }
}

/// The part of `gluPickMatrix` the examples need: restricts the projection to a small region
/// around a window position (call after `glLoadIdentity` on the projection matrix, before the
/// real projection).
///
/// # Safety
/// Renderer initialised, one thread.
pub unsafe fn glu_pick_matrix(x: f64, y: f64, width: f64, height: f64, viewport: &[i32; 4]) {
    use c3dgl::raw::{glScalef, glTranslatef};
    if width <= 0.0 || height <= 0.0 {
        return;
    }
    let (vx, vy, vw, vh) = (viewport[0] as f64, viewport[1] as f64, viewport[2] as f64, viewport[3] as f64);
    glTranslatef(((vw - 2.0 * (x - vx)) / width) as f32, ((vh - 2.0 * (y - vy)) / height) as f32, 0.0);
    glScalef((vw / width) as f32, (vh / height) as f32, 1.0);
}

/// Unit cube (-1..1) in immediate mode: each face shaded from the current color by a fixed factor,
/// optionally with texture coordinates 0..1 per face.
///
/// # Safety
/// Renderer initialised, one thread.
pub unsafe fn shaded_cube(texcoords: bool) {
    use c3dgl::raw::{glBegin, glColor3f, glColor4fv, glEnd, glGetFloatv, glTexCoord2fv, glVertex3fv};
    const FACES: [[[f32; 3]; 4]; 6] = [
        [[-1., -1., 1.], [1., -1., 1.], [1., 1., 1.], [-1., 1., 1.]],
        [[1., -1., -1.], [-1., -1., -1.], [-1., 1., -1.], [1., 1., -1.]],
        [[1., -1., 1.], [1., -1., -1.], [1., 1., -1.], [1., 1., 1.]],
        [[-1., -1., -1.], [-1., -1., 1.], [-1., 1., 1.], [-1., 1., -1.]],
        [[-1., 1., 1.], [1., 1., 1.], [1., 1., -1.], [-1., 1., -1.]],
        [[-1., -1., -1.], [1., -1., -1.], [1., -1., 1.], [-1., -1., 1.]],
    ];
    const SHADE: [f32; 6] = [1.0, 0.55, 0.8, 0.7, 0.9, 0.45];
    const TEX: [[f32; 2]; 4] = [[0., 0.], [1., 0.], [1., 1.], [0., 1.]];
    let mut color = [0f32; 4];
    glGetFloatv(gl::GL_CURRENT_COLOR, color.as_mut_ptr());
    glBegin(gl::GL_QUADS);
    for (face, shade) in FACES.iter().zip(SHADE) {
        glColor3f(color[0] * shade, color[1] * shade, color[2] * shade);
        for (vertex, tex) in face.iter().zip(&TEX) {
            if texcoords {
                glTexCoord2fv(tex.as_ptr());
            }
            glVertex3fv(vertex.as_ptr());
        }
    }
    glEnd();
    glColor4fv(color.as_ptr());
}

/// Counter for the non-visual checks shown on the console.
#[derive(Default)]
pub struct Checks {
    pub total: u32,
    pub failures: u32,
}

impl Checks {
    pub fn check(&mut self, ok: bool, what: &str, line: u32) {
        self.total += 1;
        if !ok {
            self.failures += 1;
            println!("FAIL {line}: {what}");
        }
    }

    pub fn passed(&self) -> u32 {
        self.total - self.failures
    }
}

/// `check!(checks, condition)`: records the result and prints the failing condition and line.
macro_rules! check {
    ($checks:expr, $cond:expr) => {
        $checks.check($cond, stringify!($cond), line!())
    };
}
