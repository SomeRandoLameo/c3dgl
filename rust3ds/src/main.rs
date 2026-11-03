use c3dgl::raw::*;
mod depth_scene;
use ctru::prelude::*;
use std::{fs::File, io::Write};

const VERTICES: [Vertex; 3] = [
    Vertex {
        position: [-0.8, -0.7, 0., 1.],
        color: [1., 0., 0., 1.],
    },
    Vertex {
        position: [0.8, -0.7, 0., 1.],
        color: [0., 1., 0., 1.],
    },
    Vertex {
        position: [0., 0.8, 0., 1.],
        color: [0., 0., 1., 1.],
    },
];

fn report(file: &mut File, text: String) {
    print!("{text}");
    file.write_all(text.as_bytes()).unwrap();
    file.flush().unwrap();
}

#[repr(C)]
#[derive(Clone, Copy)]
struct PackedVertex {
    position: [f32; 3],
    color: [u8; 4],
}
#[repr(C)]
#[derive(Clone, Copy)]
struct CompactVertex {
    position: [i16; 4],
    tex: [i16; 2],
    color: [u8; 4],
}
const COMPACT: [CompactVertex; 3] = [
    CompactVertex {
        position: [-80, -70, 0, 0],
        tex: [0; 2],
        color: [255, 0, 0, 255],
    },
    CompactVertex {
        position: [80, -70, 0, 0],
        tex: [0; 2],
        color: [0, 255, 0, 255],
    },
    CompactVertex {
        position: [0, 80, 0, 0],
        tex: [0; 2],
        color: [0, 0, 255, 255],
    },
];
const BENCH_VERTICES: [Vertex; 3] = [
    Vertex {
        position: [-80., -70., 0., 1.],
        color: [1., 0., 0., 1.],
    },
    Vertex {
        position: [80., -70., 0., 1.],
        color: [0., 1., 0., 1.],
    },
    Vertex {
        position: [0., 80., 0., 1.],
        color: [0., 0., 1., 1.],
    },
];
static mut BENCH_WORKLOAD: usize = 0;
unsafe fn indexed_setup() -> [u32; 2] {
    let mut names = [0; 2];
    glGenBuffers(2, names.as_mut_ptr());
    glBindBuffer(0x8892, names[0]);
    glBufferData(0x8892, 48, COMPACT.as_ptr().cast(), 0x88e4);
    glVertexPointer(3, 0x1402, 16, std::ptr::null());
    glColorPointer(4, 0x1401, 16, 12usize as *const _);
    glEnableClientState(0x8074);
    glEnableClientState(0x8076);
    glBindBuffer(0x8893, names[1]);
    let indices = [0u16, 1, 2];
    glBufferData(0x8893, 6, indices.as_ptr().cast(), 0x88e4);
    names
}

unsafe fn frame(
    clear: u32,
    draws: usize,
    matrix: bool,
    phase: usize,
    viewport: bool,
    gl_frame: bool,
) {
    if gl_frame {
        glClear(0x4100);
    } else {
        assert_eq!(c3dglNativeBegin(clear, true), 0);
    }
    if BENCH_WORKLOAD >= 5 {
        depth_scene::transform(phase);
    }
    if BENCH_WORKLOAD == 4 {
        let position: [i16; 3] = [-80 + if phase & 1 == 1 { 5 } else { -5 }, -70, 0];
        let indices = [0u16, 1, 2];
        glBufferSubData(0x8892, 0, 6, position.as_ptr().cast());
        glBufferSubData(0x8893, 0, 6, indices.as_ptr().cast());
    }
    for j in 0..draws {
        if BENCH_WORKLOAD == 6 {
            depth_scene::state(j + phase);
        }
        if matrix {
            let m = [
                0.005,
                0.,
                0.,
                0.,
                0.,
                0.005,
                0.,
                0.,
                0.,
                0.,
                0.01,
                0.,
                if (j + phase) & 1 == 1 { 0.125 } else { -0.125 },
                0.,
                0.,
                1.,
            ];
            glLoadMatrixf(m.as_ptr());
        }

        if viewport {
            glViewport(
                if (j + phase) & 1 == 1 { 200 } else { 0 },
                if (j + phase) & 1 == 1 { 120 } else { 0 },
                200,
                120,
            );
        }
        if BENCH_WORKLOAD >= 3 {
            glDrawElements(
                4,
                if BENCH_WORKLOAD >= 5 { 36 } else { 3 },
                0x1403,
                std::ptr::null(),
            );
        } else {
            assert_eq!(c3dglNativeDraw(0, 3), 0);
        }
    }

    assert_eq!(c3dglNativePresent(), 0);
}

unsafe fn image(workload: usize) {
    let mut pixels = vec![0; 400 * 240 * 3];
    assert_eq!(c3dglNativeReadback(pixels.as_mut_ptr(), pixels.len()), 0);

    let mut file = File::create(if workload == 9 {
        "sdmc:/c3dgl-rust3ds-depth.ppm"
    } else if workload == 8 {
        "sdmc:/c3dgl-rust3ds-raster.ppm"
    } else if workload == 7 {
        "sdmc:/c3dgl-rust3ds-cube.ppm"
    } else if workload == 6 {
        "sdmc:/c3dgl-rust3ds-arrays.ppm"
    } else if workload == 5 {
        "sdmc:/c3dgl-rust3ds-update.ppm"
    } else if workload == 4 {
        "sdmc:/c3dgl-rust3ds-indexed.ppm"
    } else if workload == 3 {
        "sdmc:/c3dgl-rust3ds-state.ppm"
    } else if workload == 2 {
        "sdmc:/c3dgl-rust3ds-viewport.ppm"
    } else if workload == 1 {
        "sdmc:/c3dgl-rust3ds-matrix.ppm"
    } else {
        "sdmc:/c3dgl-rust3ds.ppm"
    })
    .unwrap();
    file.write_all(b"P6\n400 240\n255\n").unwrap();

    for y in (0..240).rev() {
        for x in 0..400 {
            let p = &pixels[(x * 240 + y) * 3..][..3];
            file.write_all(&[p[2], p[1], p[0]]).unwrap();
        }
    }
}

unsafe fn readback() -> Vec<u8> {
    let mut pixels = vec![0; 400 * 240 * 3];
    assert_eq!(c3dglNativeReadback(pixels.as_mut_ptr(), pixels.len()), 0);
    pixels
}
unsafe fn array_compare(results: &mut File, expected: &[u8], name: &str) {
    assert_eq!(c3dglNativePresent(), 0);
    let pixels = readback();
    if pixels != expected || glGetError() != 0 {
        report(results, format!("FAIL array image {name}\n"));
        panic!("array image mismatch");
    }
    report(results, format!("PASS array image {name}\n"));
}
unsafe fn array_scene(results: &mut File) {
    BENCH_WORKLOAD = 0;
    glLoadIdentity();
    glViewport(0, 0, 400, 240);
    assert_eq!(c3dglNativeUpload(VERTICES.as_ptr(), 3), 0);
    frame(0x183050ff, 1, false, 0, false, false);
    let mut expected = readback();
    glVertexPointer(4, 0x1406, 32, VERTICES.as_ptr().cast());
    glColorPointer(4, 0x1406, 32, VERTICES[0].color.as_ptr().cast());
    glEnableClientState(0x8074);
    glEnableClientState(0x8076);
    assert_eq!(c3dglNativeBegin(0x183050ff, false), 0);
    glDrawArrays(4, 0, 3);
    array_compare(results, &expected, "client arrays");
    let indices8 = [2u8, 0, 1];
    let indices16 = [2u16, 0, 1];
    assert_eq!(c3dglNativeBegin(0x183050ff, false), 0);
    glDrawElements(4, 3, 0x1401, indices8.as_ptr().cast());
    array_compare(results, &expected, "client u8 indices");
    assert_eq!(c3dglNativeBegin(0x183050ff, false), 0);
    glDrawElements(4, 3, 0x1403, indices16.as_ptr().cast());
    array_compare(results, &expected, "client u16 indices");
    let mut names = [0; 2];
    glGenBuffers(2, names.as_mut_ptr());
    glBindBuffer(0x8892, names[0]);
    glBufferData(0x8892, 96, VERTICES.as_ptr().cast(), 0x88e4);
    glVertexPointer(4, 0x1406, 32, std::ptr::null());
    glColorPointer(4, 0x1406, 32, 16usize as *const _);
    glBindBuffer(0x8893, names[1]);
    glBufferData(0x8893, 6, indices16.as_ptr().cast(), 0x88e4);
    for _ in 0..3 {
        assert_eq!(c3dglNativeBegin(0x183050ff, false), 0);
        glDrawElements(4, 3, 0x1403, std::ptr::null());
        array_compare(results, &expected, "cached VBO elements");
    }
    let mut both = [VERTICES[0]; 6];
    both[..3].copy_from_slice(&VERTICES);
    both[3..].copy_from_slice(&VERTICES);
    for v in &mut both[3..] {
        v.color = [1., 1., 0., 1.];
    }
    assert_eq!(c3dglNativeUpload(both.as_ptr(), 6), 0);
    assert_eq!(c3dglNativeBegin(0x183050ff, false), 0);
    glViewport(0, 0, 200, 240);
    assert_eq!(c3dglNativeDraw(0, 3), 0);
    glViewport(200, 0, 200, 240);
    assert_eq!(c3dglNativeDraw(3, 3), 0);
    assert_eq!(c3dglNativePresent(), 0);
    expected = readback();
    assert_eq!(c3dglNativeBegin(0x183050ff, false), 0);
    glViewport(0, 0, 200, 240);
    glDrawElements(4, 3, 0x1403, std::ptr::null());
    for i in 0..3 {
        glBufferSubData(
            0x8892,
            (i * 32 + 16) as isize,
            16,
            both[i + 3].color.as_ptr().cast(),
        );
    }
    glViewport(200, 0, 200, 240);
    glDrawElements(4, 3, 0x1403, std::ptr::null());
    array_compare(
        results,
        &expected,
        "in-frame subdata preserves previous draw",
    );
    assert_eq!(c3dglNativeBegin(0x183050ff, false), 0);
    glViewport(0, 0, 200, 240);
    glBufferData(0x8892, 96, VERTICES.as_ptr().cast(), 0x88e8);
    glDrawElements(4, 3, 0x1403, std::ptr::null());
    glBufferData(0x8892, 96, both[3..].as_ptr().cast(), 0x88e8);
    glViewport(200, 0, 200, 240);
    glDrawElements(4, 3, 0x1403, std::ptr::null());
    glDeleteBuffers(2, names.as_ptr());
    array_compare(results, &expected, "in-frame data replacement and deletion");
    image(6);
    glDisableClientState(0x8074);
    glDisableClientState(0x8076);
    glViewport(0, 0, 400, 240);
}

fn main() {
    let gfx = Gfx::new().unwrap();
    let apt = Apt::new().unwrap();
    let mut hid = Hid::new().unwrap();
    let _console = Console::new(gfx.bottom_screen.borrow_mut());
    let mut results = File::create("sdmc:/c3dgl-rust3ds-results.txt").unwrap();
    report(
        &mut results,
        "rust3ds ctru-rs / native Rust dependency\n".into(),
    );

    // Exercise the application allocator independently of renderer-owned GPU memory.
    let data: Vec<_> = (0..1024u32).collect();
    assert_eq!(data.iter().sum::<u32>(), 523776);
    drop(data);
    unsafe {
        let prime = ctru_sys::vramAlloc(128);
        assert!(!prime.is_null());
        ctru_sys::vramFree(prime);
        let free = (ctru_sys::linearSpaceFree(), ctru_sys::vramSpaceFree());
        for cycle in 0..8 {
            assert!(c3dglInit());
            assert_eq!(c3dglNativeUpload(VERTICES.as_ptr(), 3), 0);
            for i in 0..30 {
                assert!(apt.main_loop());
                let clear = if i & 1 == 1 { 0x183050ff } else { 0x704020ff };
                frame(
                    clear,
                    if i % 3 == 0 { 0 } else { 1 },
                    false,
                    0,
                    false,
                    false,
                );

                let mut pixels = vec![0; 400 * 240 * 3];
                assert_eq!(c3dglNativeReadback(pixels.as_mut_ptr(), pixels.len()), 0);
                assert_eq!(
                    &pixels[(5 * 240 + 5) * 3..][..3],
                    &[
                        ((clear >> 8) & 255) as u8,
                        ((clear >> 16) & 255) as u8,
                        (clear >> 24) as u8
                    ]
                );

                if i % 3 != 0 {
                    let p = &pixels[(200 * 240 + 100) * 3..][..3];
                    for (a, b) in p.iter().zip([91u8, 82, 82]) {
                        assert!((*a as i32 - b as i32).abs() <= 4);
                    }
                }
            }

            c3dglClose();
            c3dglClose();

            assert_eq!(
                (ctru_sys::linearSpaceFree(), ctru_sys::vramSpaceFree()),
                free
            );

            report(&mut results, format!("cycle {}/8 errors=0\n", cycle + 1));
        }

        assert!(c3dglInit());
        assert_eq!(c3dglNativeUpload(BENCH_VERTICES.as_ptr(), 3), 0);

        let mut buffers = [0; 2];
        for workload in 0..7 {
            BENCH_WORKLOAD = workload;
            if workload == 3 {
                buffers = indexed_setup();
            }
            if workload == 5 {
                glDeleteBuffers(2, buffers.as_ptr());
                buffers = depth_scene::cube_setup();
            }
            let matrix = workload == 1;
            glViewport(0, 0, 400, 240);
            glClearColor(24. / 255., 48. / 255., 80. / 255., 1.);
            glLoadIdentity();
            if workload < 5 {
                glScalef(0.01, 0.01, 0.01);
            }
            for draws in [1, 64, 512] {
                let (mut cpu, mut gpu, mut total) = (0u64, 0u64, 0u64);
                for i in 0..150 {
                    assert!(apt.main_loop());
                    let start = ctru_sys::svcGetSystemTick();
                    frame(0x183050ff, draws, matrix, i, workload == 2, true);
                    let mut stats = Stats::default();
                    assert_eq!(c3dglNativeGetStats(&mut stats), 0);
                    assert_eq!(stats.draws, draws as u32);

                    if i >= 30 {
                        cpu += stats.cpu_ticks;
                        gpu += stats.gpu_ticks;
                        total += ctru_sys::svcGetSystemTick() - start;
                    }
                }

                let ms = 1000. / ctru_sys::SYSCLOCK_ARM11 as f64 / 120.;
                report(
                    &mut results,
                    format!(
                        "workload={} draws={} CPU {:0.4} GPU {:0.4}\ntotal {:0.4} ms/frame\n",
                        if workload == 6 {
                            "raster"
                        } else if workload == 5 {
                            "cube"
                        } else if workload == 4 {
                            "update"
                        } else if workload == 3 {
                            "indexed"
                        } else if workload == 2 {
                            "viewport"
                        } else if matrix {
                            "matrix"
                        } else {
                            "static"
                        },
                        draws,
                        cpu as f64 * ms,
                        gpu as f64 * ms,
                        total as f64 * ms
                    ),
                );
            }

            image(if workload >= 3 {
                if workload >= 5 {
                    workload + 2
                } else {
                    workload + 1
                }
            } else {
                workload
            });
        }

        glDeleteBuffers(2, buffers.as_ptr());
        glDisableClientState(0x8074);
        glDisableClientState(0x8076);
        BENCH_WORKLOAD = 0;
        depth_scene::defaults();
        assert_eq!(c3dglNativeUpload(VERTICES.as_ptr(), 3), 0);
        glLoadIdentity();
        glViewport(0, 0, 400, 240);
        glClearColor(0.75, 0.25, 0.125, 1.);
        glClear(0x4100);
        assert_eq!(c3dglNativeDraw(0, 3), 0);
        glClearColor(24. / 255., 48. / 255., 80. / 255., 1.);
        glClear(0x4000);
        glViewport(0, 0, 0, 0);
        assert_eq!(c3dglNativeDraw(0, 3), 0);
        glViewport(0, 0, 400, 240);
        assert_eq!(c3dglNativeDraw(0, 3), 0);
        glClear(0x0100);
        glViewport(0, 0, 200, 120);
        assert_eq!(c3dglNativeDraw(0, 3), 0);
        assert_eq!(c3dglNativePresent(), 0);
        image(3);
        assert_eq!(glGetError(), 0);
        array_scene(&mut results);
        depth_scene::scene(&mut results);
        c3dglClose();
        assert_eq!(
            (ctru_sys::linearSpaceFree(), ctru_sys::vramSpaceFree()),
            free
        );
        report(&mut results, "PASS VBO GPU memory recovery\n".into());
    }

    report(&mut results, "ALL CHECKS PASSED - START exits\n".into());

    while apt.main_loop() {
        hid.scan_input();
        if hid.keys_down().contains(KeyPad::START) {
            break;
        }

        gfx.wait_for_vblank();
    }
}
