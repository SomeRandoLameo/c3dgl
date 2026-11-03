use c3dgl::raw::*;
use ctru::prelude::*;
use std::ptr;
#[repr(C)]
struct TexturedVertex {
    position: [f32; 3],
    uv: [f32; 2],
}
const MESH: [TexturedVertex; 3] = [
    TexturedVertex {
        position: [-0.8, -0.7, 0.],
        uv: [0., 0.],
    },
    TexturedVertex {
        position: [0.8, -0.7, 0.],
        uv: [2., 0.],
    },
    TexturedVertex {
        position: [0., 0.8, 0.],
        uv: [1., 2.],
    },
];
fn main() {
    let gfx = Gfx::new().unwrap();
    let apt = Apt::new().unwrap();
    let mut hid = Hid::new().unwrap();
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    unsafe {
        assert!(c3dglInit());
        let mut pixels = [0u8; 16 * 16 * 4];
        for y in 0..16 {
            for x in 0..16 {
                let i = (y * 16 + x) * 4;
                let c = ((x / 4) ^ (y / 4)) & 1;
                pixels[i..i + 4].copy_from_slice(if c == 0 {
                    &[240, 80, 40, 255]
                } else {
                    &[30, 200, 230, 255]
                });
            }
        }
        let mut tex = 0;
        glGenTextures(1, &mut tex);
        glBindTexture(0x0de1, tex);
        glTexImage2D(
            0x0de1,
            0,
            0x1908,
            16,
            16,
            0,
            0x1908,
            0x1401,
            pixels.as_ptr().cast(),
        );
        glTexParameteri(0x0de1, 0x2801, 0x2600);
        glTexParameteri(0x0de1, 0x2800, 0x2600);
        glEnable(0x0de1);
        let mut buffer = 0;
        glGenBuffers(1, &mut buffer);
        glBindBuffer(0x8892, buffer);
        glBufferData(
            0x8892,
            std::mem::size_of_val(&MESH) as isize,
            MESH.as_ptr().cast(),
            0x88e4,
        );
        glVertexPointer(3, 0x1406, 20, ptr::null());
        glTexCoordPointer(2, 0x1406, 20, 12usize as *const _);
        glEnableClientState(0x8074);
        glEnableClientState(0x8078);
        glColor4f(1., 1., 1., 1.);
        glClearColor(0.08, 0.12, 0.2, 1.);
        println!("Rust texture + blend demo; START exits");
        while apt.main_loop() {
            hid.scan_input();
            if hid.keys_down().contains(KeyPad::START) {
                break;
            }
            glClear(0x4000);
            glDisable(0x0be2);
            glDrawArrays(4, 0, 3);
            glEnable(0x0be2);
            glBlendFunc(0x0302, 0x0303);
            glColor4f(1., 1., 1., 0.5);
            glBegin(4);
            glTexCoord2f(0., 0.);
            glVertex2f(-0.4, -0.4);
            glTexCoord2f(1., 0.);
            glVertex2f(0.9, -0.4);
            glTexCoord2f(0.5, 1.);
            glVertex2f(0.6, 0.8);
            glEnd();
            glColor4f(1., 1., 1., 1.);
            assert_eq!(c3dglNativePresent(), 0);
            assert_eq!(glGetError(), 0);
        }
        glDeleteBuffers(1, &buffer);
        glDeleteTextures(1, &tex);
        c3dglClose();
    }
}
