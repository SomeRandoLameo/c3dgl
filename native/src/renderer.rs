//! Single application-thread renderer. All service calls go through platform.
use crate::{frame::FrameState, gpu, matrix::Matrices, platform::gpu as p, Commands};
use core::{ptr, slice};
// 512 draws with a matrix upload each need 21,504 words plus frame commands.
const COMMAND_WORDS: usize = 32768;
const STATE_WORDS: usize = 1024;
const MAX_VERTICES: usize = 4096;
const TARGET_BYTES: usize = 240 * 400 * 4;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Vertex {
    pub position: [f32; 4],
    pub color: [f32; 4],
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Stats {
    pub cpu_ticks: u64,
    pub gpu_ticks: u64,
    pub wait_ticks: u64,
    pub command_bytes: u32,
    pub draws: u32,
}

struct Buffer {
    pointer: *mut u8,
    vram: bool,
}

impl Buffer {
    unsafe fn new(size: usize, vram: bool) -> Option<Self> {
        let pointer = if vram {
            p::c3dglPlatformVramAllocate(size)
        } else {
            p::c3dglPlatformLinearAllocate(size)
        };

        if pointer.is_null() {
            None
        } else {
            Some(Self { pointer, vram })
        }
    }

    unsafe fn physical(&self) -> u32 {
        p::c3dglPlatformPhysical(self.pointer)
    }
}

impl Drop for Buffer {
    fn drop(&mut self) {
        unsafe {
            if self.vram {
                p::c3dglPlatformVramFree(self.pointer)
            } else {
                p::c3dglPlatformLinearFree(self.pointer)
            }
        }
    }
}

struct Renderer {
    frame: FrameState,
    work_wait_ticks: u64,
    work_gpu_ticks: u64,
    submitted_bytes: u32,
    matrices: Matrices,
    commands: Buffer,
    state: Buffer,
    vertices: Buffer,
    color: Buffer,
    depth: Buffer,
    state_words: usize,
    used: usize,
    vertex_count: usize,
    generation: u32,
    active: bool,
    started: u64,
    wait_ticks: u64,
    draws: u32,
    stats: Stats,
    error: i32,
}

static mut CONTEXT: Option<Renderer> = None;

pub unsafe fn with_matrices(f: impl FnOnce(&mut Matrices)) {
    if let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() {
        f(&mut r.matrices);
    }
}

pub unsafe fn with_frame(f: impl FnOnce(&mut FrameState, &mut Matrices)) {
    if let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() {
        f(&mut r.frame, &mut r.matrices);
    }
}
pub unsafe fn clear_gl(mask: u32) {
    if mask & !(crate::frame::COLOR | crate::frame::DEPTH) != 0 {
        with_matrices(|m| m.fail(0x0501));
        return;
    }
    if mask == 0 {
        return;
    }
    let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() else {
        return;
    };
    if !r.active {
        if begin_frame(r, true, None) != 0 {
            r.matrices.fail(0x0502);
            return;
        }
    } else {
        // Complete prior commands before a memory fill, preserving clear/draw order.
        let mut commands = r.commands();
        if gpu::finish(&mut commands).is_err() {
            r.matrices.fail(0x0505);
            return;
        }
        r.used = commands.used;
        let wait = p::c3dglPlatformTick();
        if p::c3dglPlatformSubmit(r.commands.pointer, (r.used * 4) as u32, ptr::null_mut()) < 0 {
            r.matrices.fail(0x0502);
            return;
        }
        r.work_gpu_ticks += p::c3dglPlatformWait();
        r.submitted_bytes += (r.used * 4) as u32;
        r.work_wait_ticks += p::c3dglPlatformTick() - wait;
        r.used = 0;
        let mut commands = r.commands();
        if gpu::begin(&mut commands).is_err() {
            r.matrices.fail(0x0505);
            return;
        }
        r.used = commands.used;
    }
    let depth = ((1. - r.frame.clear_depth) * 16777215.) as u32;
    if p::c3dglPlatformClearMasked(
        r.color.pointer,
        r.depth.pointer,
        r.frame.clear_color,
        depth,
        TARGET_BYTES as u32,
        mask,
    ) < 0
    {
        r.matrices.fail(0x0502);
    }
}

impl Renderer {
    unsafe fn create() -> Option<Self> {
        // Fallible allocation and RAII rollback for every intermediate failure.
        let commands = Buffer::new(COMMAND_WORDS * 4, false)?;
        let state = Buffer::new(STATE_WORDS * 4, false)?;
        let vertices = Buffer::new(MAX_VERTICES * 32, false)?;
        let color = Buffer::new(TARGET_BYTES, true)?;
        let depth = Buffer::new(TARGET_BYTES, true)?;

        let mut encoded =
            Commands::new(slice::from_raw_parts_mut(state.pointer.cast(), STATE_WORDS));

        gpu::initialize(
            &mut encoded,
            color.physical(),
            depth.physical(),
            vertices.physical(),
        )
        .ok()?;

        let state_words = encoded.used;

        Some(Self {
            frame: FrameState::new(),
            work_wait_ticks: 0,
            work_gpu_ticks: 0,
            submitted_bytes: 0,
            matrices: Matrices::new(),
            commands,
            state,
            vertices,
            color,
            depth,
            state_words,
            used: 0,
            vertex_count: 0,
            generation: 0,
            active: false,
            started: 0,
            wait_ticks: 0,
            draws: 0,
            stats: Stats::default(),
            error: 0,
        })
    }

    unsafe fn restore(&mut self) -> bool {
        let generation = p::c3dglPlatformGeneration();

        if self.generation == generation {
            return true;
        }

        if p::c3dglPlatformSubmit(
            self.state.pointer,
            (self.state_words * 4) as u32,
            ptr::null_mut(),
        ) < 0
        {
            return false;
        }

        p::c3dglPlatformWait();

        self.generation = generation;
        self.matrices.dirty = true;
        self.frame.viewport_dirty = true;

        true
    }

    unsafe fn commands(&mut self) -> Commands<'_> {
        Commands {
            words: slice::from_raw_parts_mut(self.commands.pointer.cast(), COMMAND_WORDS),
            used: self.used,
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn c3dglInit() -> bool {
    let context = &mut *ptr::addr_of_mut!(CONTEXT);
    if context.is_some() {
        return true;
    }

    if p::c3dglPlatformAcquire() < 0 {
        return false;
    }

    match Renderer::create() {
        Some(mut renderer) => {
            if renderer.restore() {
                *context = Some(renderer);

                true
            } else {
                p::c3dglPlatformRelease();

                false
            }
        }
        _ => {
            p::c3dglPlatformRelease();

            false
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn c3dglClose() {
    let context = &mut *ptr::addr_of_mut!(CONTEXT);
    if context.is_some() {
        p::c3dglPlatformRelease(); // Drain before RAII releases any GPU storage.
        *context = None;
    }
}

/// Upload persistent object-space geometry between frames;
/// the default matrices are identity; screen/depth mapping happens in the shader.
#[no_mangle]
pub unsafe extern "C" fn c3dglNativeUpload(vertices: *const Vertex, count: usize) -> i32 {
    let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() else {
        return -1;
    };

    if r.active || vertices.is_null() || count == 0 || count > MAX_VERTICES {
        return -2;
    }

    p::c3dglPlatformWait(); // Never overwrite vertices still being read by GPU.

    let input = slice::from_raw_parts(vertices, count);
    if input.iter().any(|v| {
        v.position
            .iter()
            .chain(v.color.iter())
            .any(|x| !x.is_finite())
    }) {
        return -2;
    }

    let output = slice::from_raw_parts_mut(r.vertices.pointer.cast::<Vertex>(), count);
    output.copy_from_slice(input);

    let result = p::c3dglPlatformFlush(r.vertices.pointer, (count * 32) as u32);
    if result < 0 {
        r.vertex_count = 0;
        return result;
    }

    r.vertex_count = count;

    0
}

#[no_mangle]
pub unsafe extern "C" fn c3dglNativeBegin(clear: u32, sync: bool) -> i32 {
    let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() else {
        return -1;
    };

    begin_frame(r, sync, Some(clear))
}
unsafe fn begin_frame(r: &mut Renderer, sync: bool, clear: Option<u32>) -> i32 {
    if r.active {
        return -2;
    }

    let wait = p::c3dglPlatformTick();

    r.stats.gpu_ticks = p::c3dglPlatformWait();

    if sync {
        p::c3dglPlatformVblank();
    }

    if !r.restore() {
        return -3;
    }

    r.wait_ticks = p::c3dglPlatformTick() - wait;
    r.started = p::c3dglPlatformTick();
    r.used = 0;
    r.draws = 0;
    r.error = 0;
    r.work_wait_ticks = 0;
    r.work_gpu_ticks = 0;
    r.submitted_bytes = 0;

    let mut commands = r.commands();
    if gpu::begin(&mut commands).is_err() {
        return -3;
    }

    r.used = commands.used;

    if let Some(clear) = clear {
        let result =
            p::c3dglPlatformClear(r.color.pointer, r.depth.pointer, clear, TARGET_BYTES as u32);
        if result < 0 {
            return result;
        }
    }

    r.active = true;

    0
}

#[no_mangle]
pub unsafe extern "C" fn c3dglNativeDraw(first: usize, count: usize) -> i32 {
    let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() else {
        return -1;
    };

    if !r.active
        || count == 0
        || count % 3 != 0
        || first > r.vertex_count
        || count > r.vertex_count - first
    {
        return -2;
    }

    if !r.frame.drawable {
        r.draws += 1;
        return 0;
    }

    if (r.frame.viewport_dirty || r.matrices.dirty) && update_draw_state(r).is_err() {
        r.error = -4;
        return -4;
    }
    let mut commands = r.commands();
    if gpu::draw(&mut commands, first as u32, count as u32).is_err() {
        r.error = -4;
        return -4;
    }

    r.used = commands.used;
    r.draws += 1;

    0
}

// Keep matrix/viewport packet work out of the unchanged-state draw loop.
#[inline(never)]
unsafe fn update_draw_state(r: &mut Renderer) -> Result<(), ()> {
    let viewport = if r.frame.viewport_dirty {
        Some(r.frame.viewport)
    } else {
        None
    };

    let update = if r.matrices.dirty {
        Some(r.matrices.combined())
    } else {
        None
    };

    let mut commands = r.commands();
    // Single-state writers already reserve draw + finish. Reserve both only
    // when both updates are needed, keeping the unchanged-state draw path cheap.
    if viewport.is_some()
        && update.is_some()
        && commands.words.len() - commands.used < gpu::DRAW_WORDS + gpu::FINISH_WORDS + 28
    {
        return Err(());
    }

    if let Some(v) = viewport {
        if gpu::viewport(&mut commands, v).is_err() {
            return Err(());
        }
    }

    if let Some(matrix) = update {
        if gpu::matrix(&mut commands, &matrix).is_err() {
            return Err(());
        }
    }

    r.used = commands.used;
    r.matrices.dirty = false;
    r.frame.viewport_dirty = false;
    Ok(())
}

#[no_mangle]
pub unsafe extern "C" fn c3dglNativePresent() -> i32 {
    let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() else {
        return -1;
    };

    if !r.active {
        return -2;
    }

    let mut commands = r.commands();

    if gpu::finish(&mut commands).is_err() {
        return -4;
    }

    r.used = commands.used;

    let result = p::c3dglPlatformSubmit(r.commands.pointer, (r.used * 4) as u32, r.color.pointer);

    r.active = false;
    r.stats.cpu_ticks = p::c3dglPlatformTick() - r.started - r.work_wait_ticks;
    r.stats.wait_ticks = r.wait_ticks + r.work_wait_ticks;
    r.stats.command_bytes = r.submitted_bytes + (r.used * 4) as u32;
    r.stats.draws = r.draws;

    if result < 0 {
        result
    } else {
        r.error
    }
}

/// Complete submitted work and copy its stats. No VBlank wait is included in GPU time.
#[no_mangle]
pub unsafe extern "C" fn c3dglNativeGetStats(output: *mut Stats) -> i32 {
    let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() else {
        return -1;
    };

    if output.is_null() || r.active {
        return -2;
    }

    r.stats.gpu_ticks = r.work_gpu_ticks + p::c3dglPlatformWait();

    output.write(r.stats);

    0
}

#[no_mangle]
pub unsafe extern "C" fn c3dglSwapBuffers() {
    let _ = c3dglNativePresent();
}

#[no_mangle]
pub unsafe extern "C" fn c3dglNativeReadback(pixels: *mut u8, capacity: usize) -> i32 {
    let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() else {
        return -1;
    };

    if r.active || pixels.is_null() || capacity < 400 * 240 * 3 || capacity > u32::MAX as usize {
        return -2;
    }

    p::c3dglPlatformReadback(pixels, capacity as u32)
}

#[cfg(test)]
mod lifecycle_tests {
    use super::*;
    use std::{
        alloc::{alloc, dealloc, Layout},
        sync::Mutex,
    };

    #[derive(Default)]
    struct Mock {
        allocations: usize,
        fail_at: usize,
        live: Vec<(usize, usize)>,
        acquired: bool,
        generation: u32,
        submits: usize,
        clear_calls: Vec<(u32, u32, u32)>,
        waits: usize,
        tick: u64,
    }

    static MOCK: Mutex<Mock> = Mutex::new(Mock {
        allocations: 0,
        fail_at: 0,
        live: Vec::new(),
        acquired: false,
        generation: 1,
        submits: 0,
        clear_calls: Vec::new(),
        waits: 0,
        tick: 0,
    });

    unsafe fn allocate(size: usize) -> *mut u8 {
        let mut m = MOCK.lock().unwrap();
        m.allocations += 1;

        if m.allocations == m.fail_at {
            return ptr::null_mut();
        }

        let pointer = alloc(Layout::from_size_align(size, 128).unwrap());
        if !pointer.is_null() {
            m.live.push((pointer as usize, size));
        }

        pointer
    }

    unsafe fn free(pointer: *mut u8) {
        let mut m = MOCK.lock().unwrap();
        let index = m.live.iter().position(|a| a.0 == pointer as usize).unwrap();
        let (_, size) = m.live.remove(index);
        dealloc(pointer, Layout::from_size_align(size, 128).unwrap());
    }

    #[no_mangle]
    unsafe extern "C" fn c3dglPlatformLinearAllocate(size: usize) -> *mut u8 {
        allocate(size)
    }

    #[no_mangle]
    unsafe extern "C" fn c3dglPlatformLinearFree(p: *mut u8) {
        free(p);
    }

    #[no_mangle]
    unsafe extern "C" fn c3dglPlatformVramAllocate(size: usize) -> *mut u8 {
        allocate(size)
    }

    #[no_mangle]
    unsafe extern "C" fn c3dglPlatformVramFree(p: *mut u8) {
        free(p);
    }

    #[no_mangle]
    extern "C" fn c3dglPlatformPhysical(_: *const u8) -> u32 {
        0x20000000
    }

    #[no_mangle]
    extern "C" fn c3dglPlatformTick() -> u64 {
        let mut m = MOCK.lock().unwrap();
        m.tick += 1;
        m.tick
    }

    #[no_mangle]
    extern "C" fn c3dglPlatformFlush(_: *const u8, _: u32) -> i32 {
        0
    }

    #[no_mangle]
    extern "C" fn c3dglPlatformAcquire() -> i32 {
        let mut m = MOCK.lock().unwrap();
        if m.acquired {
            -1
        } else {
            m.acquired = true;
            0
        }
    }

    #[no_mangle]
    extern "C" fn c3dglPlatformRelease() {
        let mut m = MOCK.lock().unwrap();
        m.acquired = false;
        m.waits += 1;
    }

    #[no_mangle]
    extern "C" fn c3dglPlatformWait() -> u64 {
        MOCK.lock().unwrap().waits += 1;
        5
    }

    #[no_mangle]
    extern "C" fn c3dglPlatformGeneration() -> u32 {
        MOCK.lock().unwrap().generation
    }

    #[no_mangle]
    extern "C" fn c3dglPlatformVblank() {}

    #[no_mangle]
    extern "C" fn c3dglPlatformClear(_: *mut u8, _: *mut u8, _: u32, _: u32) -> i32 {
        0
    }

    #[no_mangle]
    extern "C" fn c3dglPlatformReadback(_: *mut u8, _: u32) -> i32 {
        0
    }

    #[no_mangle]
    unsafe extern "C" fn c3dglPlatformClearMasked(
        _c: *mut u8,
        _d: *mut u8,
        v: u32,
        dv: u32,
        _b: u32,
        m: u32,
    ) -> i32 {
        MOCK.lock().unwrap().clear_calls.push((m, v, dv));
        0
    }

    #[no_mangle]
    unsafe extern "C" fn c3dglPlatformSubmit(commands: *mut u8, bytes: u32, _: *mut u8) -> i32 {
        assert_eq!(commands as usize % 128, 0);
        assert_eq!(bytes % 16, 0);
        let words = slice::from_raw_parts(commands.cast::<u32>(), bytes as usize / 4);
        assert_eq!(words[words.len() - 1] & 0x3ff, 0x10);
        MOCK.lock().unwrap().submits += 1;
        0
    }

    #[test]
    fn allocation_rollback_lifecycle_and_overflow() {
        unsafe {
            for fail in 1..=5 {
                {
                    let mut m = MOCK.lock().unwrap();
                    m.fail_at = fail;
                    m.allocations = 0;
                }
                assert!(!c3dglInit());
                let m = MOCK.lock().unwrap();
                assert!(m.live.is_empty());
                assert!(!m.acquired);
            }
            {
                let mut m = MOCK.lock().unwrap();
                m.fail_at = 0;
            }
            for _ in 0..8 {
                assert!(c3dglInit());
                assert!(c3dglInit());
                assert_eq!(c3dglNativeDraw(0, 3), -2);
                use crate::matrix::*;
                glMatrixMode(0xffff);
                glPopMatrix();
                assert_eq!(glGetError(), 0x0500);
                assert_eq!(glGetError(), 0);
                glPopMatrix();
                assert_eq!(glGetError(), 0x0504);
                glPushMatrix();
                glTranslatef(1., 2., 3.);
                glScalef(2., 3., 4.);
                with_matrices(|m| {
                    let a = m.combined();
                    assert_eq!(a[12], 1.);
                    assert_eq!(a[5], 3.);
                });
                glPopMatrix();
                with_matrices(|m| assert_eq!(m.combined(), IDENTITY));
                glMatrixMode(0x1701);
                glPushMatrix();
                glPushMatrix();
                assert_eq!(glGetError(), 0x0503);
                glFrustum(-1., 1., -1., 1., 1., 9.);
                glMatrixMode(0x1700);
                glTranslatef(0., 0., -2.);
                with_matrices(|m| {
                    let a = m.combined();
                    assert_eq!(a[15], 2.);
                    assert_eq!(a[0], 1.);
                });
                glLoadIdentity();
                glRotatef(90., 0., 0., 2.);
                with_matrices(|m| {
                    let a = m.combined();
                    assert!(a[0].abs() < 1e-6);
                    assert!((a[1] - 1.).abs() < 1e-6);
                });
                glLoadIdentity();
                glMatrixMode(0x1701);
                glPopMatrix();
                glOrtho(-2., 2., -4., 4., -1., 1.);
                with_matrices(|m| {
                    let a = m.combined();
                    assert_eq!(a[0], 0.5);
                    assert_eq!(a[5], 0.25);
                });
                glLoadIdentity();
                glMatrixMode(0x1700);
                glFrustum(-1., 1., -1., 1., 0., 9.);
                assert_eq!(glGetError(), 0x0501);
                let vertices = [Vertex {
                    position: [0.0, 0.0, 0.0, 1.0],
                    color: [1.0; 4],
                }; 3];
                assert_eq!(c3dglNativeUpload(vertices.as_ptr(), 3), 0);
                use crate::frame::*;
                glViewport(0, 0, -1, 240);
                assert_eq!(glGetError(), 0x0501);
                let mut vp = [0; 4];
                glGetIntegerv(0x0ba2, vp.as_mut_ptr());
                assert_eq!(vp, [0, 0, 400, 240]);
                glClear(0);
                assert!(!(&*ptr::addr_of!(CONTEXT)).as_ref().unwrap().active);
                glClear(1);
                assert_eq!(glGetError(), 0x0501);
                glClearColor(1., 0., 0., 1.);
                glClearDepth(-1.);
                let mut color = [0; 4];
                glGetIntegerv(0x0c22, color.as_mut_ptr());
                assert_eq!(color, [i32::MAX, 0, 0, i32::MAX]);
                glClear(COLOR);
                assert_eq!(
                    MOCK.lock().unwrap().clear_calls.last(),
                    Some(&(COLOR, 0xff0000ff, 0xffffff))
                );
                assert_eq!(c3dglNativeDraw(0, 3), 0);
                let before = MOCK.lock().unwrap().submits;
                glClear(DEPTH);
                assert_eq!(MOCK.lock().unwrap().submits, before + 1);
                assert_eq!(MOCK.lock().unwrap().clear_calls.last().unwrap().0, DEPTH);
                glViewport(0, 0, 0, 0);
                let before = (&*ptr::addr_of!(CONTEXT)).as_ref().unwrap().used;
                assert_eq!(c3dglNativeDraw(0, 3), 0);
                assert_eq!((&*ptr::addr_of!(CONTEXT)).as_ref().unwrap().used, before);
                glViewport(0, 0, 400, 240);
                glClearDepth(1.);
                assert_eq!(c3dglNativePresent(), 0);
                assert_eq!(glGetError(), 0);
                assert_eq!(c3dglNativeBegin(0xff, false), 0);
                assert_eq!(c3dglNativeUpload(vertices.as_ptr(), 3), -2);
                assert_eq!(c3dglNativeBegin(0xff, false), -2);
                assert_eq!(c3dglNativeDraw(0, 4), -2);
                assert_eq!(c3dglNativeDraw(0, 3), 0);
                let first_used = (&*ptr::addr_of!(CONTEXT)).as_ref().unwrap().used;
                glLoadIdentity();
                assert_eq!(c3dglNativeDraw(0, 3), 0);
                let second_used = (&*ptr::addr_of!(CONTEXT)).as_ref().unwrap().used;
                assert_eq!(second_used - first_used, gpu::DRAW_WORDS);
                glTranslatef(0.125, 0., 0.);
                assert_eq!(c3dglNativeDraw(0, 3), 0);
                let third_used = (&*ptr::addr_of!(CONTEXT)).as_ref().unwrap().used;
                assert_eq!(third_used - second_used, gpu::DRAW_WORDS + 20);
                let mut draws = 3;
                while c3dglNativeDraw(0, 3) == 0 {
                    draws += 1;
                }
                assert!(draws > 512);
                assert_eq!(c3dglNativePresent(), -4);
                let mut stats = Stats::default();
                assert_eq!(c3dglNativeGetStats(&mut stats), 0);
                assert_eq!(stats.draws, draws);
                assert!(stats.command_bytes <= COMMAND_WORDS as u32 * 4);
                assert_eq!(c3dglNativePresent(), -2);
                // A rejected draw does not poison the next frame.
                assert_eq!(c3dglNativeBegin(0xff, false), 0);
                assert_eq!(c3dglNativeDraw(0, 3), 0);
                assert_eq!(c3dglNativePresent(), 0);
                let before = MOCK.lock().unwrap().submits;
                MOCK.lock().unwrap().generation += 1;
                assert_eq!(c3dglNativeBegin(0xff, false), 0);
                assert_eq!(MOCK.lock().unwrap().submits, before + 1);
                c3dglClose();
                c3dglClose();
                let m = MOCK.lock().unwrap();
                assert!(m.live.is_empty());
                assert!(!m.acquired);
            }
        }
    }
}
