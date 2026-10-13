use c3dgl::raw::*;
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
    for j in 0..draws {
        if matrix {
            let m = [
                0.5,
                0.,
                0.,
                0.,
                0.,
                0.5,
                0.,
                0.,
                0.,
                0.,
                1.,
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
        assert_eq!(c3dglNativeDraw(0, 3), 0);
    }

    assert_eq!(c3dglNativePresent(), 0);
}

unsafe fn image(workload: usize) {
    let mut pixels = vec![0; 400 * 240 * 3];
    assert_eq!(c3dglNativeReadback(pixels.as_mut_ptr(), pixels.len()), 0);

    let mut file = File::create(if workload == 3 {
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
        assert_eq!(c3dglNativeUpload(VERTICES.as_ptr(), 3), 0);

        for workload in 0..3 {
            let matrix = workload == 1;
            glViewport(0, 0, 400, 240);
            glClearColor(24. / 255., 48. / 255., 80. / 255., 1.);
            glLoadIdentity();
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
                        "workload={} draws={} CPU {:.4} GPU {:.4}\ntotal {:.4} ms/frame\n",
                        if workload == 2 {
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

            image(workload);
        }

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
        c3dglClose();
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
