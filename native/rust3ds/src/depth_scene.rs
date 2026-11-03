// Shared inputs match tests/depth_scene.h; all payload components are f32/u8/u16.
use super::*;
const CUBE: [PackedVertex; 24] = [
    PackedVertex {
        position: [-0.5, -0.5, 0.5],
        color: [255, 0, 0, 255],
    },
    PackedVertex {
        position: [0.5, -0.5, 0.5],
        color: [255, 0, 0, 255],
    },
    PackedVertex {
        position: [0.5, 0.5, 0.5],
        color: [255, 0, 0, 255],
    },
    PackedVertex {
        position: [-0.5, 0.5, 0.5],
        color: [255, 0, 0, 255],
    },
    PackedVertex {
        position: [-0.5, -0.5, -0.5],
        color: [0, 255, 0, 255],
    },
    PackedVertex {
        position: [-0.5, 0.5, -0.5],
        color: [0, 255, 0, 255],
    },
    PackedVertex {
        position: [0.5, 0.5, -0.5],
        color: [0, 255, 0, 255],
    },
    PackedVertex {
        position: [0.5, -0.5, -0.5],
        color: [0, 255, 0, 255],
    },
    PackedVertex {
        position: [0.5, -0.5, -0.5],
        color: [0, 0, 255, 255],
    },
    PackedVertex {
        position: [0.5, 0.5, -0.5],
        color: [0, 0, 255, 255],
    },
    PackedVertex {
        position: [0.5, 0.5, 0.5],
        color: [0, 0, 255, 255],
    },
    PackedVertex {
        position: [0.5, -0.5, 0.5],
        color: [0, 0, 255, 255],
    },
    PackedVertex {
        position: [-0.5, -0.5, 0.5],
        color: [255, 255, 0, 255],
    },
    PackedVertex {
        position: [-0.5, 0.5, 0.5],
        color: [255, 255, 0, 255],
    },
    PackedVertex {
        position: [-0.5, 0.5, -0.5],
        color: [255, 255, 0, 255],
    },
    PackedVertex {
        position: [-0.5, -0.5, -0.5],
        color: [255, 255, 0, 255],
    },
    PackedVertex {
        position: [-0.5, 0.5, 0.5],
        color: [255, 0, 255, 255],
    },
    PackedVertex {
        position: [0.5, 0.5, 0.5],
        color: [255, 0, 255, 255],
    },
    PackedVertex {
        position: [0.5, 0.5, -0.5],
        color: [255, 0, 255, 255],
    },
    PackedVertex {
        position: [-0.5, 0.5, -0.5],
        color: [255, 0, 255, 255],
    },
    PackedVertex {
        position: [-0.5, -0.5, -0.5],
        color: [0, 255, 255, 255],
    },
    PackedVertex {
        position: [0.5, -0.5, -0.5],
        color: [0, 255, 255, 255],
    },
    PackedVertex {
        position: [0.5, -0.5, 0.5],
        color: [0, 255, 255, 255],
    },
    PackedVertex {
        position: [-0.5, -0.5, 0.5],
        color: [0, 255, 255, 255],
    },
];
const CUBE_INDICES: [u16; 36] = [
    0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11, 12, 13, 14, 12, 14, 15, 16, 17, 18,
    16, 18, 19, 20, 21, 22, 20, 22, 23,
];
const PHASES: [[f32; 2]; 16] = [
    [1.0, 0.0],
    [0.9238795, 0.3826834],
    [0.7071068, 0.7071068],
    [0.3826834, 0.9238795],
    [0.0, 1.0],
    [-0.3826834, 0.9238795],
    [-0.7071068, 0.7071068],
    [-0.9238795, 0.3826834],
    [-1.0, 0.0],
    [-0.9238795, -0.3826834],
    [-0.7071068, -0.7071068],
    [-0.3826834, -0.9238795],
    [0.0, -1.0],
    [0.3826834, -0.9238795],
    [0.7071068, -0.7071068],
    [0.9238795, -0.3826834],
];
const DEPTH_VERTICES: [PackedVertex; 9] = [
    PackedVertex {
        position: [-0.8, -0.7, -0.5],
        color: [255, 0, 0, 255],
    },
    PackedVertex {
        position: [0.8, -0.7, -0.5],
        color: [255, 0, 0, 255],
    },
    PackedVertex {
        position: [0.0, 0.8, -0.5],
        color: [255, 0, 0, 255],
    },
    PackedVertex {
        position: [-0.8, -0.7, 0.5],
        color: [0, 255, 0, 255],
    },
    PackedVertex {
        position: [0.8, -0.7, 0.5],
        color: [0, 255, 0, 255],
    },
    PackedVertex {
        position: [0.0, 0.8, 0.5],
        color: [0, 255, 0, 255],
    },
    PackedVertex {
        position: [-0.8, -0.7, -0.5],
        color: [0, 0, 255, 255],
    },
    PackedVertex {
        position: [0.8, -0.7, -0.5],
        color: [0, 0, 255, 255],
    },
    PackedVertex {
        position: [0.0, 0.8, -0.5],
        color: [0, 0, 255, 255],
    },
];
const EXPECTED: [[u8; 3]; 20] = [
    [255, 0, 0],
    [0, 0, 255],
    [0, 255, 0],
    [0, 0, 255],
    [255, 0, 0],
    [0, 255, 0],
    [255, 0, 0],
    [0, 255, 0],
    [0, 255, 0],
    [255, 0, 0],
    [0, 0, 255],
    [255, 0, 0],
    [255, 0, 0],
    [24, 48, 80],
    [24, 48, 80],
    [24, 48, 80],
    [255, 0, 0],
    [255, 0, 0],
    [255, 0, 0],
    [255, 0, 0],
];
pub unsafe fn defaults() {
    glMatrixMode(0x1701);
    glLoadIdentity();
    glMatrixMode(0x1700);
    glLoadIdentity();
    glViewport(0, 0, 400, 240);
    glDepthMask(1);
    glDepthFunc(0x0201);
    glDepthRange(0., 1.);
    glClearDepth(1.);
    glCullFace(0x0405);
    glFrontFace(0x0901);
    glDisable(0x0b44);
    glDisable(0x0b71);
}
unsafe fn setup(vertices: &[PackedVertex], indices: &[u16]) -> [u32; 2] {
    let mut names = [0; 2];
    glGenBuffers(2, names.as_mut_ptr());
    glBindBuffer(0x8892, names[0]);
    glBufferData(
        0x8892,
        std::mem::size_of_val(vertices) as isize,
        vertices.as_ptr().cast(),
        0x88e4,
    );
    glVertexPointer(3, 0x1406, 16, std::ptr::null());
    glColorPointer(4, 0x1401, 16, 12usize as *const _);
    glEnableClientState(0x8074);
    glEnableClientState(0x8076);
    glBindBuffer(0x8893, names[1]);
    glBufferData(
        0x8893,
        std::mem::size_of_val(indices) as isize,
        indices.as_ptr().cast(),
        0x88e4,
    );
    names
}
pub unsafe fn cube_setup() -> [u32; 2] {
    defaults();
    let packed = CUBE.map(|v| CompactVertex {
        position: [
            (v.position[0] * 1024.) as i16,
            (v.position[1] * 1024.) as i16,
            (v.position[2] * 1024.) as i16,
            0,
        ],
        tex: [0; 2],
        color: v.color,
    });
    let mut names = [0; 2];
    glGenBuffers(2, names.as_mut_ptr());
    glBindBuffer(0x8892, names[0]);
    glBufferData(
        0x8892,
        std::mem::size_of_val(&packed) as isize,
        packed.as_ptr().cast(),
        0x88e4,
    );
    glVertexPointer(3, 0x1402, 16, std::ptr::null());
    glColorPointer(4, 0x1401, 16, 12usize as *const _);
    glEnableClientState(0x8074);
    glEnableClientState(0x8076);
    glBindBuffer(0x8893, names[1]);
    glBufferData(
        0x8893,
        std::mem::size_of_val(&CUBE_INDICES) as isize,
        CUBE_INDICES.as_ptr().cast(),
        0x88e4,
    );
    glMatrixMode(0x1701);
    glFrustum(-1., 1., -0.6, 0.6, 1., 10.);
    glMatrixMode(0x1700);
    glEnable(0x0b71);
    glEnable(0x0b44);
    names
}
pub unsafe fn transform(phase: usize) {
    let [c, s] = PHASES[phase % 16];
    let m: [f32; 16] = [
        c / 1024.,
        0.,
        -s / 1024.,
        0.,
        0.6 * s / 1024.,
        0.8 / 1024.,
        0.6 * c / 1024.,
        0.,
        0.8 * s / 1024.,
        -0.6 / 1024.,
        0.8 * c / 1024.,
        0.,
        0.,
        0.,
        -3.,
        1.,
    ];
    glLoadMatrixf(m.as_ptr());
}
pub unsafe fn state(phase: usize) {
    glDepthFunc(if phase & 1 == 1 { 0x0203 } else { 0x0201 });
    glFrontFace(if phase & 1 == 1 { 0x0900 } else { 0x0901 });
    glDepthRange(
        if phase & 1 == 1 { 0.2 } else { 0. },
        if phase & 1 == 1 { 0.8 } else { 1. },
    );
}
unsafe fn draw(triangle: usize) {
    glDrawElements(4, 3, 0x1403, (triangle * 6) as *const _);
}
pub unsafe fn scene(results: &mut File) {
    defaults();
    let names = setup(&DEPTH_VERTICES, &[0, 1, 2, 3, 4, 5, 6, 7, 8]);
    glClearColor(24. / 255., 48. / 255., 80. / 255., 1.);
    glClear(0x4100);
    for cell in 0..20 {
        glViewport((cell % 5 * 80) as i32, (cell / 5 * 60) as i32, 80, 60);
        glEnable(0x0b71);
        glDisable(0x0b44);
        glDepthMask(1);
        glDepthRange(0., 1.);
        glDepthFunc(0x0207);
        match cell {
            0..=7 => {
                let funcs = [
                    0x0201, 0x0203, 0x0204, 0x0206, 0x0202, 0x0205, 0x0200, 0x0207,
                ];
                draw(0);
                glDepthFunc(funcs[cell]);
                if cell == 5 {
                    draw(2);
                }
                draw(if cell == 1 || cell == 3 { 2 } else { 1 });
            }
            8 => {
                glDepthFunc(0x0201);
                glDepthMask(0);
                draw(0);
                draw(1);
            }
            9 => {
                glDepthFunc(0x0201);
                draw(0);
                glDepthMask(0);
                glClear(0x0100);
                glDepthMask(1);
                draw(1);
            }
            10 => {
                glDisable(0x0b71);
                draw(0);
                draw(1);
                glEnable(0x0b71);
                glDepthFunc(0x0201);
                draw(2);
            }
            11 => {
                glDepthRange(1., 0.);
                draw(1);
                glDepthFunc(0x0204);
                draw(0);
            }
            16 => {
                glDepthRange(0.5, 0.5);
                draw(0);
                glDepthFunc(0x0201);
                draw(1);
            }
            17 => {
                glDepthRange(0.2, 0.8);
                draw(1);
                glDepthFunc(0x0201);
                draw(0);
            }
            _ => {
                glEnable(0x0b44);
                glCullFace(if cell == 13 || cell == 18 {
                    0x0404
                } else if cell == 15 || cell == 19 {
                    0x0408
                } else {
                    0x0405
                });
                glFrontFace(if cell == 14 || cell == 18 {
                    0x0900
                } else {
                    0x0901
                });
                if cell == 19 {
                    glDisable(0x0b44);
                }
                draw(0);
            }
        }
    }
    assert_eq!(glGetError(), 0);
    assert_eq!(c3dglNativePresent(), 0);
    let pixels = readback();
    for (cell, expected) in EXPECTED.iter().enumerate() {
        let p = &pixels[((cell % 5 * 80 + 40) * 240 + cell / 5 * 60 + 30) * 3..][..3];
        if p != [expected[2], expected[1], expected[0]] {
            report(results, format!("FAIL depth cell {cell}: {p:?}\n"));
            panic!("depth cell mismatch");
        }
    }
    image(9);
    report(results, "PASS 20 depth/cull cells\n".into());
    glDeleteBuffers(2, names.as_ptr());
    defaults();
    glDisableClientState(0x8074);
    glDisableClientState(0x8076);
}
