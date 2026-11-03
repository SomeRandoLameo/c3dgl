//! c3dgl Rust example: a spinning, textured and vertex-colored cube.
//!
//! c3dgl is a plain Cargo dependency (imported as `c3dgl`); `c3dgl::raw` exposes the
//! GL entry points. They are `unsafe`: call them from one thread, only while the
//! renderer is initialised. A toggles nearest/linear filtering, START exits.
use c3dgl::raw::*;
use ctru::prelude::*;
use std::ffi::c_void;
use std::mem::size_of;

// The raw module takes plain GL enum values.
const GL_TRIANGLES: u32 = 0x0004;
const GL_DEPTH_BUFFER_BIT: u32 = 0x0100;
const GL_COLOR_BUFFER_BIT: u32 = 0x4000;
const GL_CULL_FACE: u32 = 0x0b44;
const GL_DEPTH_TEST: u32 = 0x0b71;
const GL_TEXTURE_2D: u32 = 0x0de1;
const GL_BACK: u32 = 0x0405;
const GL_CCW: u32 = 0x0901;
const GL_UNSIGNED_BYTE: u32 = 0x1401;
const GL_UNSIGNED_SHORT: u32 = 0x1403;
const GL_FLOAT: u32 = 0x1406;
const GL_RGBA: u32 = 0x1908;
const GL_NEAREST: i32 = 0x2600;
const GL_LINEAR: i32 = 0x2601;
const GL_TEXTURE_MAG_FILTER: u32 = 0x2800;
const GL_TEXTURE_MIN_FILTER: u32 = 0x2801;
const GL_MODELVIEW: u32 = 0x1700;
const GL_PROJECTION: u32 = 0x1701;
const GL_VERTEX_ARRAY: u32 = 0x8074;
const GL_COLOR_ARRAY: u32 = 0x8076;
const GL_TEXTURE_COORD_ARRAY: u32 = 0x8078;

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct Vertex {
    position: [f32; 3],
    uv: [f32; 2],
    color: [u8; 4],
}

/// Normal, two tangent axes (counter-clockwise seen from outside) and a tint per face.
const FACES: [([f32; 3], [f32; 3], [f32; 3], [u8; 3]); 6] = [
    ([0., 0., 1.], [1., 0., 0.], [0., 1., 0.], [255, 255, 255]),
    ([0., 0., -1.], [-1., 0., 0.], [0., 1., 0.], [255, 160, 160]),
    ([1., 0., 0.], [0., 0., -1.], [0., 1., 0.], [160, 255, 160]),
    ([-1., 0., 0.], [0., 0., 1.], [0., 1., 0.], [160, 160, 255]),
    ([0., 1., 0.], [1., 0., 0.], [0., 0., -1.], [255, 255, 160]),
    ([0., -1., 0.], [1., 0., 0.], [0., 0., 1.], [160, 255, 255]),
];

fn cube() -> ([Vertex; 24], [u16; 36]) {
    const CORNERS: [([f32; 2], [f32; 2]); 4] = [
        ([-1., -1.], [0., 0.]),
        ([1., -1.], [1., 0.]),
        ([1., 1.], [1., 1.]),
        ([-1., 1.], [0., 1.]),
    ];
    let mut vertices = [Vertex::default(); 24];
    let mut indices = [0u16; 36];
    for (f, (n, u, v, rgb)) in FACES.iter().enumerate() {
        for (c, (corner, uv)) in CORNERS.iter().enumerate() {
            let vertex = &mut vertices[f * 4 + c];
            for axis in 0..3 {
                vertex.position[axis] = (n[axis] + corner[0] * u[axis] + corner[1] * v[axis]) * 0.5;
            }
            vertex.uv = *uv;
            vertex.color = [rgb[0], rgb[1], rgb[2], 255];
        }
        for (i, q) in [0u16, 1, 2, 0, 2, 3].iter().enumerate() {
            indices[f * 6 + i] = (f * 4) as u16 + q;
        }
    }
    (vertices, indices)
}

fn checkerboard() -> [u8; 16 * 16 * 4] {
    let mut texels = [255u8; 16 * 16 * 4];
    for y in 0..16 {
        for x in 0..16 {
            let value = if ((x / 4) ^ (y / 4)) & 1 == 1 { 255 } else { 90 };
            texels[(y * 16 + x) * 4..][..3].fill(value);
        }
    }
    texels
}

/// Owns the renderer; closes it on drop. Declare it after `Gfx` so it drops first.
struct Renderer;

impl Renderer {
    fn new() -> Option<Self> {
        // SAFETY: gfx is initialised by the caller and this is the only renderer.
        unsafe { c3dglInit() }.then_some(Renderer)
    }
}

impl Drop for Renderer {
    fn drop(&mut self) {
        // SAFETY: the renderer was initialised in `new`.
        unsafe { c3dglClose() }
    }
}

fn set_filter(filter: i32) {
    unsafe {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    }
}

fn main() {
    let gfx = Gfx::new().expect("gfx");
    let apt = Apt::new().expect("apt");
    let mut hid = Hid::new().expect("hid");
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let Some(_renderer) = Renderer::new() else {
        println!("c3dglInit failed");
        std::thread::sleep(std::time::Duration::from_secs(2));
        return;
    };

    // Client arrays are read at draw time, so these must stay alive for the whole loop.
    let (vertices, indices) = cube();
    let texels = checkerboard();
    let stride = size_of::<Vertex>() as i32;
    let mut texture = 0;
    unsafe {
        glGenTextures(1, &mut texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA as i32, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.as_ptr().cast());
        set_filter(GL_NEAREST);
        glEnable(GL_TEXTURE_2D);

        glVertexPointer(3, GL_FLOAT, stride, vertices[0].position.as_ptr().cast());
        glTexCoordPointer(2, GL_FLOAT, stride, vertices[0].uv.as_ptr().cast());
        glColorPointer(4, GL_UNSIGNED_BYTE, stride, vertices[0].color.as_ptr().cast());
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(-1., 1., -0.6, 0.6, 1., 10.); // 400x240 top screen is 5:3
        glMatrixMode(GL_MODELVIEW);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glClearColor(0.09, 0.14, 0.24, 1.);
    }

    let (mut angle, mut frame, mut linear) = (0f32, 0u32, false);
    while apt.main_loop() {
        hid.scan_input();
        let down = hid.keys_down();
        if down.contains(KeyPad::START) {
            break;
        }
        if down.contains(KeyPad::A) {
            linear = !linear;
            set_filter(if linear { GL_LINEAR } else { GL_NEAREST });
        }

        let (present, error) = unsafe {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glLoadIdentity();
            glTranslatef(0., 0., -2.6);
            glRotatef(30., 1., 0., 0.);
            glRotatef(angle, 0., 1., 0.);
            glDrawElements(GL_TRIANGLES, indices.len() as i32, GL_UNSIGNED_SHORT, indices.as_ptr() as *const c_void);
            (c3dglNativePresent(), glGetError())
        };
        if present != 0 || error != 0 {
            println!("frame {frame} failed (present {present}, GL {error:#x})");
            break;
        }

        if frame % 30 == 0 {
            let mut stats = Stats { cpu_ticks: 0, gpu_ticks: 0, wait_ticks: 0, command_bytes: 0, draws: 0 };
            if unsafe { c3dglNativeGetStats(&mut stats) } == 0 {
                println!(
                    "\x1b[1;1Hc3dgl Rust example\nCPU {:6.3} ms  draws {}   \n{} filtering\n\nA: toggle filter\nSTART: exit",
                    stats.cpu_ticks as f64 * 1000. / 268_111_856.,
                    stats.draws,
                    if linear { "linear " } else { "nearest" },
                );
            }
        }
        frame += 1;
        angle = (angle + 0.8) % 360.;
    }

    unsafe { glDeleteTextures(1, &texture) };
}
