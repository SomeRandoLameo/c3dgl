use c3dgl::raw::*;
use ctru::prelude::*;
use std::ptr;
const POSITIONS: [[f32; 3]; 4] = [
    [-0.8, -0.8, 0.],
    [0.8, -0.8, 0.],
    [0.8, 0.8, 0.],
    [-0.8, 0.8, 0.],
];
const ORDERS: [[u32; 6]; 6] = [
    [0, 1, 2, 0, 2, 3],
    [0, 1, 3, 2, 0, 0],
    [0, 1, 2, 3, 0, 0],
    [0, 1, 2, 3, 0, 0],
    [0, 1, 3, 2, 0, 0],
    [0, 1, 2, 3, 0, 0],
];
const COLORS: [[u8; 4]; 6] = [
    [240, 80, 40, 255],
    [30, 200, 230, 255],
    [240, 210, 30, 255],
    [150, 70, 220, 255],
    [40, 210, 70, 255],
    [230, 100, 170, 255],
];
fn main() {
    let gfx = Gfx::new().unwrap();
    let apt = Apt::new().unwrap();
    let mut hid = Hid::new().unwrap();
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    unsafe {
        assert!(c3dglInit());
        let mut buffers = [0; 2];
        glGenBuffers(2, buffers.as_mut_ptr());
        glBindBuffer(0x8892, buffers[0]);
        glBufferData(
            0x8892,
            std::mem::size_of_val(&POSITIONS) as isize,
            POSITIONS.as_ptr().cast(),
            0x88e4,
        );
        glBindBuffer(0x8893, buffers[1]);
        glBufferData(0x8893, 24, ptr::null(), 0x88e8);
        glEnable(0x0b44);
        glClearColor(24. / 255., 48. / 255., 80. / 255., 1.);
        glClear(0x4000);
        for row in 0..3 {
            for col in 0..6 {
                let mode = 4 + col as u32;
                let count = if col == 0 { 6 } else { 4 };
                glViewport(col as i32 * 66, row * 80, 66, 80);
                glColor4ubv(COLORS[col].as_ptr());
                if row == 0 {
                    let mut input = [[0f32; 3]; 6];
                    for i in 0..count as usize {
                        input[i] = POSITIONS[ORDERS[col][i] as usize];
                    }
                    glBindBuffer(0x8892, 0);
                    glBindBuffer(0x8893, 0);
                    glVertexPointer(3, 0x1406, 12, input.as_ptr().cast());
                    glEnableClientState(0x8074);
                    glDrawArrays(mode, 0, count);
                } else if row == 1 {
                    glBindBuffer(0x8892, buffers[0]);
                    glBindBuffer(0x8893, buffers[1]);
                    glVertexPointer(3, 0x1406, 12, ptr::null());
                    glBufferSubData(0x8893, 0, count as isize * 4, ORDERS[col].as_ptr().cast());
                    glDrawElements(mode, count, 0x1405, ptr::null());
                } else {
                    glBegin(mode);
                    for i in 0..count as usize {
                        glTexCoord2fv([0f32, 0.].as_ptr());
                        glVertex3fv(POSITIONS[ORDERS[col][i] as usize].as_ptr());
                    }
                    glEnd();
                }
            }
        }
        assert_eq!(c3dglNativePresent(), 0);
        assert_eq!(glGetError(), 0);
        println!("Six topologies: arrays / VBO / immediate\nSTART exits");
        while apt.main_loop() {
            hid.scan_input();
            if hid.keys_down().contains(KeyPad::START) {
                break;
            }
            gfx.wait_for_vblank();
        }
        glDeleteBuffers(2, buffers.as_ptr());
        c3dglClose();
    }
}
