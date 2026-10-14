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

// Internal GPU layout; the public native upload ABI remains float4 position/color.
#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct GpuVertex {
    pub position: [f32; 4],
    pub color: [f32; 4],
    pub texcoord: [f32; 4],
    pub extra_texcoords: [[f32; 4]; 2],
}

const VERTEX_BYTES: usize = core::mem::size_of::<GpuVertex>();

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Stats {
    pub cpu_ticks: u64,
    pub gpu_ticks: u64,
    pub wait_ticks: u64,
    pub command_bytes: u32,
    pub draws: u32,
}

pub(crate) struct Buffer {
    pub(crate) pointer: *mut u8,
    vram: bool,
}

impl Buffer {
    pub(crate) unsafe fn new(size: usize, vram: bool) -> Option<Self> {
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

    pub(crate) unsafe fn physical(&self) -> u32 {
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

pub(crate) struct Renderer {
    pub(crate) arrays: crate::arrays::Arrays,
    pub(crate) immediate: crate::immediate::Immediate,
    transient: alloc::vec::Vec<Buffer>,
    pub(crate) frame: FrameState,
    work_wait_ticks: u64,
    work_gpu_ticks: u64,
    submitted_bytes: u32,
    pub(crate) matrices: Matrices,
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
    /// The GPU currently holds the clip-space state (identity matrix, no culling).
    pub(crate) stacks: crate::attrib::Stacks,
    /// The fog state the GPU holds (None: no fog).
    fog_key: Option<crate::fog::Key>,
    clip_applied: bool,
    /// Polygon offset folded into the uploaded matrix.
    bias_applied: f32,
}

// Boxed: the renderer is large and the 3DS main thread stack is only 32 KiB.
static mut CONTEXT: Option<alloc::boxed::Box<Renderer>> = None;

pub unsafe fn with_matrices(f: impl FnOnce(&mut Matrices)) {
    if let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() {
        if r.immediate.active {
            r.matrices.fail(0x0502);
            return;
        }

        f(&mut r.matrices);
    }
}

pub unsafe fn with_frame(f: impl FnOnce(&mut FrameState, &mut Matrices)) {
    if let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() {
        if r.immediate.active {
            r.matrices.fail(0x0502);
            return;
        }

        f(&mut r.frame, &mut r.matrices);
    }
}

pub unsafe fn clear_gl(mask: u32) {
    const STENCIL: u32 = 0x0400;
    if mask & !(crate::frame::COLOR | crate::frame::DEPTH | STENCIL) != 0 {
        with_matrices(|m| m.fail(0x0501));
        return;
    }

    if mask == 0 {
        return;
    }

    let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() else {
        return;
    };

    if r.immediate.active {
        r.matrices.fail(0x0502);
        return;
    }

    // Write masks apply to clears.
    let raster = r.frame.raster;
    let color = mask & crate::frame::COLOR != 0 && raster.color_mask != 0;
    let depth = mask & crate::frame::DEPTH != 0 && raster.depth_mask;
    let stencil = mask & STENCIL != 0 && raster.stencil_write_mask != 0;
    if !color && !depth && !stencil {
        return;
    }

    // The memory fill clears whole buffers, and depth and stencil only together. Stencil may be
    // overwritten as long as it was never used.
    let fill = !raster.scissor
        && (!color || raster.color_mask == 0xf)
        && (!stencil || raster.stencil_write_mask == 0xff)
        && (depth == stencil || (depth && !r.frame.stencil_used));
    if !fill {
        clear_quad(r, color, depth, stencil);
        return;
    }
    let mask = if color { crate::frame::COLOR } else { 0 }
        | if depth || stencil {
            crate::frame::DEPTH
        } else {
            0
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

    // D24S8: stencil in the top byte, depth reversed.
    let depth =
        (r.frame.clear_stencil as u32) << 24 | ((1. - r.frame.clear_depth) * 16777215.) as u32;
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

/// Clear by drawing a full-screen quad at the clear depth: honors the scissor box and all write
/// masks (the state of the draw is built here, then the user's state is re-emitted).
unsafe fn clear_quad(r: &mut Renderer, color: bool, depth: bool, stencil: bool) {
    if !r.active && begin_frame(r, true, None) != 0 {
        r.matrices.fail(0x0502);
        return;
    }
    let saved = (
        r.frame.raster,
        r.frame.viewport,
        r.frame.drawable,
        r.frame.fog.enabled,
    );
    let mut q = crate::raster::Raster::new();
    q.depth = true; // Depth writes need the test enabled.
    q.function = 0x0207;
    q.depth_mask = depth;
    q.scissor = saved.0.scissor;
    q.scissor_box = saved.0.scissor_box;
    q.color_mask = if color { saved.0.color_mask } else { 0 };
    if stencil {
        q.stencil = true;
        q.stencil_ref = r.frame.clear_stencil;
        q.stencil_write_mask = saved.0.stencil_write_mask;
        q.stencil_ops = [0x1e01; 3];
    }
    q.dirty = true;
    r.frame.raster = q;
    // glClear ignores the viewport.
    r.frame.viewport = [0, 0, 400, 240];
    r.frame.drawable = true;
    r.frame.viewport_dirty = true;
    r.frame.fog.enabled = false;
    r.frame.textures.suspended = true;
    r.frame.textures.dirty = true;

    let z = 2. * r.frame.clear_depth - 1.;
    let rgba = r.frame.clear_color;
    let bytes: [f32; 4] = core::array::from_fn(|i| ((rgba >> (24 - 8 * i)) & 255) as f32);
    let corner = |x: f32, y: f32| GpuVertex {
        position: [x, y, z, 1.],
        color: bytes,
        texcoord: [0., 0., 0., 1.],
        extra_texcoords: [[0., 0., 0., 1.]; 2],
    };
    let vertices = [
        corner(-1., -1.),
        corner(1., -1.),
        corner(1., 1.),
        corner(-1., 1.),
    ];
    let indices = [0u16, 1, 2, 0, 2, 3];
    if let Err(e) = draw_transient(r, &vertices, &indices, 6, true, 0.) {
        r.matrices.fail(e);
    }

    r.frame.raster = saved.0;
    r.frame.raster.dirty = true;
    r.frame.viewport = saved.1;
    r.frame.drawable = saved.2;
    r.frame.viewport_dirty = true;
    r.frame.fog.enabled = saved.3;
    r.frame.textures.suspended = false;
    r.frame.textures.dirty = true;
}

impl Renderer {
    unsafe fn create() -> Option<alloc::boxed::Box<Self>> {
        // Fallible allocation and RAII rollback for every intermediate failure.
        let commands = Buffer::new(COMMAND_WORDS * 4, false)?;
        let state = Buffer::new(STATE_WORDS * 4, false)?;
        let vertices = Buffer::new(MAX_VERTICES * VERTEX_BYTES, false)?;
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

        let layout = core::alloc::Layout::new::<Self>();
        let slot = alloc::alloc::alloc(layout).cast::<Self>();
        if slot.is_null() {
            return None;
        }
        core::ptr::addr_of_mut!((*slot).arrays).write(crate::arrays::Arrays::new());
        core::ptr::addr_of_mut!((*slot).immediate).write(crate::immediate::Immediate::new());
        core::ptr::addr_of_mut!((*slot).transient).write(alloc::vec::Vec::new());
        core::ptr::addr_of_mut!((*slot).frame).write(FrameState::new());
        core::ptr::addr_of_mut!((*slot).work_wait_ticks).write(0);
        core::ptr::addr_of_mut!((*slot).work_gpu_ticks).write(0);
        core::ptr::addr_of_mut!((*slot).submitted_bytes).write(0);
        Matrices::init(core::ptr::addr_of_mut!((*slot).matrices));
        core::ptr::addr_of_mut!((*slot).commands).write(commands);
        core::ptr::addr_of_mut!((*slot).state).write(state);
        core::ptr::addr_of_mut!((*slot).vertices).write(vertices);
        core::ptr::addr_of_mut!((*slot).color).write(color);
        core::ptr::addr_of_mut!((*slot).depth).write(depth);
        core::ptr::addr_of_mut!((*slot).state_words).write(state_words);
        core::ptr::addr_of_mut!((*slot).used).write(0);
        core::ptr::addr_of_mut!((*slot).vertex_count).write(0);
        core::ptr::addr_of_mut!((*slot).generation).write(0);
        core::ptr::addr_of_mut!((*slot).active).write(false);
        core::ptr::addr_of_mut!((*slot).started).write(0);
        core::ptr::addr_of_mut!((*slot).wait_ticks).write(0);
        core::ptr::addr_of_mut!((*slot).draws).write(0);
        core::ptr::addr_of_mut!((*slot).stats).write(Stats::default());
        core::ptr::addr_of_mut!((*slot).error).write(0);
        core::ptr::addr_of_mut!((*slot).stacks).write(crate::attrib::Stacks::default());
        core::ptr::addr_of_mut!((*slot).fog_key).write(None);
        core::ptr::addr_of_mut!((*slot).clip_applied).write(false);
        core::ptr::addr_of_mut!((*slot).bias_applied).write(0.);
        Some(alloc::boxed::Box::from_raw(slot))
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
        self.clip_applied = false;
        self.fog_key = None;
        self.bias_applied = 0.;
        self.matrices.dirty = true;
        self.frame.viewport_dirty = true;
        self.frame.raster.dirty = true;
        self.frame.textures.dirty = true;
        self.matrices.texture_dirty = true;

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

    let output = slice::from_raw_parts_mut(r.vertices.pointer.cast::<GpuVertex>(), count);
    for (out, v) in output.iter_mut().zip(input) {
        *out = GpuVertex {
            position: v.position,
            color: v.color.map(|c| c * 255.),
            texcoord: [0., 0., 0., 1.],
            extra_texcoords: [[0., 0., 0., 1.]; 2],
        };
    }

    let result = p::c3dglPlatformFlush(r.vertices.pointer, (count * VERTEX_BYTES) as u32);
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
    r.arrays.collected();
    r.frame.textures.collected();
    r.transient.clear();

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

    if !r.frame.drawable || !r.frame.raster.drawable() {
        r.draws += 1;
        return 0;
    }

    if (r.frame.viewport_dirty
        || r.matrices.dirty
        || r.matrices.texture_dirty
        || r.frame.raster.dirty
        || r.frame.textures.dirty
        || r.clip_applied
        || r.bias_applied != 0.)
        && update_draw_state(r, false, 0.).is_err()
    {
        r.error = -4;
        return -4;
    }

    let address = r.vertices.physical();
    let mut commands = r.commands();
    if gpu::geometry(&mut commands, address, first as u32, count as u32, None).is_err() {
        r.error = -4;
        return -4;
    }

    r.used = commands.used;
    r.frame.textures.sampled();
    r.draws += 1;

    0
}

// Keep matrix/viewport packet work out of the unchanged-state draw loop.
#[inline(never)]
unsafe fn update_draw_state(r: &mut Renderer, clip: bool, bias: f32) -> Result<(), ()> {
    let clip_changed = clip != r.clip_applied;
    let bias = if clip { 0. } else { bias };
    let viewport = if r.frame.viewport_dirty {
        Some(r.frame.viewport)
    } else {
        None
    };

    // Clip-space draws (expanded lines and points) carry NDC positions: identity projection.
    let update = if clip {
        clip_changed.then_some(crate::matrix::IDENTITY)
    } else if r.matrices.dirty || clip_changed || bias != r.bias_applied {
        Some(r.matrices.combined())
    } else {
        None
    };

    let raster = (r.frame.raster.dirty || clip_changed).then_some(r.frame.raster);
    let texgen_units = r.frame.texgen.unit_mask();
    let tex_matrix = |unit: usize| {
        if texgen_units >> unit & 1 != 0 {
            crate::matrix::IDENTITY
        } else {
            r.matrices.texture_for(unit)
        }
    };
    // Unit 0 divides by q per pixel after a texcoord with q != 1, a projective texture matrix
    // or generated coordinates.
    let projective = r.frame.textures.q_used
        || (texgen_units & 1 != 0 && r.frame.textures.packets()[0].enabled)
        || {
            let m = tex_matrix(0);
            m[3] != 0. || m[7] != 0. || m[15] != 1.
        };

    let texture =
        (r.frame.textures.dirty || r.matrices.texture_dirty).then(|| r.frame.textures.packets());
    let texmatrix = (r.matrices.texture_dirty || texture.is_some()).then(|| {
        let packets = r.frame.textures.packets();
        (
            core::array::from_fn(tex_matrix),
            core::array::from_fn(|unit| packets[unit].scale),
            core::array::from_fn(|unit| packets[unit].flip),
        )
    });

    let fog_key = r
        .frame
        .fog
        .key(&r.matrices.projection(), r.frame.raster.range);
    let fog = (fog_key != r.fog_key).then_some(fog_key);
    let needed = fog.map_or(0, |_| gpu::FOG_WORDS)
        + texmatrix.map_or(0, |_| gpu::TEXMATRIX_WORDS)
        + viewport.map_or(0, |_| 8)
        + update.map_or(0, |_| 20)
        + raster.map_or(0, |_| gpu::RASTER_WORDS)
        + texture.map_or(0, |_| gpu::TEXTURE_WORDS);
    let mut commands = r.commands();
    if needed != 0
        && commands.words.len() - commands.used < needed + gpu::DRAW_WORDS + 8 + gpu::FINISH_WORDS
    {
        return Err(());
    }

    if let Some(s) = raster {
        gpu::raster(&mut commands, s, clip)?;
    }

    if let Some(key) = fog {
        match key {
            Some(k) => gpu::fog(&mut commands, Some((k.color(), &crate::fog::table(&k))))?,
            None => gpu::fog(&mut commands, None)?,
        }
    }

    if let Some(t) = texture {
        gpu::texture(&mut commands, t, projective)?;
    }

    if let Some((matrices, scales, flips)) = texmatrix {
        gpu::texture_matrix(&mut commands, &matrices, &scales, &flips)?;
    }
    if let Some(v) = viewport {
        if gpu::viewport(&mut commands, v).is_err() {
            return Err(());
        }
    }

    if let Some(matrix) = update {
        if gpu::matrix(&mut commands, &matrix, bias).is_err() {
            return Err(());
        }
    }

    r.used = commands.used;
    r.fog_key = fog_key;
    r.clip_applied = clip;
    if clip {
        r.bias_applied = 0.;
    } else {
        r.bias_applied = bias;
        r.matrices.dirty = false;
    }
    r.matrices.texture_dirty = false;
    r.frame.viewport_dirty = false;
    r.frame.raster.dirty = false;
    r.frame.textures.dirty = false;

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
                use crate::{frame::*, raster::*};
                assert_eq!(glIsEnabled(0x0b71), 0);
                assert_eq!(glIsEnabled(0x0b44), 0);
                let mut boolean = 0;
                glGetBooleanv(0x0b72, &mut boolean);
                assert_eq!(boolean, 1);
                let mut range = [-1.; 2];
                glGetFloatv(0x0b70, range.as_mut_ptr());
                assert_eq!(range, [0., 1.]);
                glEnable(0xffff);
                glDepthRange(f64::NAN, 0.);
                assert_eq!(glGetError(), 0x0500);
                assert_eq!(glGetError(), 0);
                glDepthFunc(0xffff);
                assert_eq!(glGetError(), 0x0500);
                glCullFace(0xffff);
                assert_eq!(glGetError(), 0x0500);
                glFrontFace(0xffff);
                assert_eq!(glGetError(), 0x0500);
                glDepthRange(f64::INFINITY, 0.);
                assert_eq!(glGetError(), 0x0501);
                glDepthRange(-2., 3.);
                glGetFloatv(0x0b70, range.as_mut_ptr());
                assert_eq!(range, [0., 1.]);
                glEnable(0x0b71);
                with_frame(|s, _| {
                    assert!(s.raster.dirty);
                    s.raster.dirty = false;
                });
                glEnable(0x0b71);
                with_frame(|s, _| assert!(!s.raster.dirty));
                glDisable(0x0b71);
                glDepthMask(0);
                let clears = MOCK.lock().unwrap().clear_calls.len();
                glClear(DEPTH);
                assert_eq!(MOCK.lock().unwrap().clear_calls.len(), clears);
                assert!(!(&*ptr::addr_of!(CONTEXT)).as_ref().unwrap().active);
                glDepthMask(2);
                glGetBooleanv(0x0b72, &mut boolean);
                assert_eq!(boolean, 1);
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
                // Client memory is copied per draw; VBOs survive frames and updates.
                use crate::arrays::*;
                glVertexPointer(4, 0x1406, 32, vertices.as_ptr().cast());
                glColorPointer(4, 0x1406, 32, vertices.as_ptr().cast::<u8>().add(16).cast());
                glEnableClientState(0x8074);
                glEnableClientState(0x8076);
                let live = MOCK.lock().unwrap().live.len();
                {
                    let mut mock = MOCK.lock().unwrap();
                    mock.fail_at = mock.allocations + 1;
                }
                glDrawArrays(4, 0, 3);
                assert_eq!(glGetError(), 0x0505);
                assert_eq!(MOCK.lock().unwrap().live.len(), live);
                MOCK.lock().unwrap().fail_at = 0;
                glDrawArrays(4, 0, 3);
                assert_eq!(glGetError(), 0);
                let indices = [2u8, 0, 1];
                glDrawElements(4, 3, 0x1401, indices.as_ptr().cast());
                assert_eq!(glGetError(), 0);
                assert_eq!(c3dglNativePresent(), 0);
                let mut names = [0; 3];
                glGenBuffers(3, names.as_mut_ptr());
                glBindBuffer(0x8892, names[0]);
                glBufferData(0x8892, 96, vertices.as_ptr().cast(), 0x88e4);
                glVertexPointer(4, 0x1406, 32, ptr::null());
                glColorPointer(4, 0x1406, 32, 16usize as *const _);
                glBindBuffer(0x8893, names[1]);
                glBufferData(0x8893, 3, indices.as_ptr().cast(), 0x88e4);
                assert_eq!(c3dglNativeBegin(0xff, false), 0);
                let live = MOCK.lock().unwrap().live.len();
                glColorPointer(4, 0x1406, 32, 96usize as *const _);
                glDrawElements(4, 3, 0x1401, ptr::null());
                assert_eq!(glGetError(), 0x0502);
                assert_eq!(MOCK.lock().unwrap().live.len(), live);
                glColorPointer(4, 0x1406, 32, 16usize as *const _);
                glDrawElements(4, 3, 0x1401, ptr::null());
                let allocations = MOCK.lock().unwrap().allocations;
                glDrawElements(4, 3, 0x1401, ptr::null());
                assert_eq!(MOCK.lock().unwrap().allocations, allocations);
                assert_eq!(c3dglNativePresent(), 0);
                glDrawElements(4, 3, 0x1401, ptr::null());
                assert_eq!(MOCK.lock().unwrap().allocations, allocations);
                // An unrelated object does not evict the cached mesh.
                glBindBuffer(0x8892, names[2]);
                glBufferData(0x8892, 4, ptr::null(), 0x88e8);
                glDrawElements(4, 3, 0x1401, ptr::null());
                assert_eq!(MOCK.lock().unwrap().allocations, allocations);
                glBindBuffer(0x8892, names[0]);
                glBufferSubData(0x8892, 96, 4, vertices.as_ptr().cast());
                assert_eq!(glGetError(), 0x0501);
                glBufferSubData(0x8892, 0, 16, vertices.as_ptr().cast());
                glDrawElements(4, 3, 0x1401, ptr::null());
                assert_eq!(MOCK.lock().unwrap().allocations, allocations + 1);
                assert_eq!(glGetError(), 0);
                crate::matrix::texture_stack_lifecycle();
                crate::arrays::uv_lifecycle();
                crate::texture::lifecycle(|| {
                    let mut m = MOCK.lock().unwrap();
                    m.fail_at = m.allocations + 1;
                });
                // Texture image replacement must leave a resident VBO mesh reusable.
                let after_textures = MOCK.lock().unwrap().allocations;
                glDrawElements(4, 3, 0x1401, ptr::null());
                assert_eq!(MOCK.lock().unwrap().allocations, after_textures);
                glDeleteBuffers(3, names.as_ptr());
                crate::arrays::topology_lifecycle();
                assert_eq!(c3dglNativePresent(), 0);
                // A rejected draw does not poison the next frame.
                assert_eq!(c3dglNativeBegin(0xff, false), 0);
                assert_eq!(c3dglNativeDraw(0, 3), 0);
                assert_eq!(c3dglNativePresent(), 0);
                {
                    use crate::immediate::*;
                    glVertex3fv(ptr::null());
                    assert_eq!(glGetError(), 0x0501);
                    glVertex3f(0., 0., 0.);
                    assert_eq!(glGetError(), 0x0502);
                    glBegin(4);
                    glBegin(4);
                    assert_eq!(glGetError(), 0x0502);
                    crate::matrix::glLoadIdentity();
                    assert_eq!(glGetError(), 0x0502);
                    glColor4ubv([255, 0, 0, 255].as_ptr());
                    glTexCoord2f(0.25, 0.75);
                    glVertex2fv([-1f32, -1.].as_ptr());
                    glColor3fv([0f32, 1., 0.].as_ptr());
                    glTexCoord2f(0.5, 1.);
                    glVertex3fv([1f32, -1., 0.].as_ptr());
                    glColor4fv([0f32, 0., 1., 1.].as_ptr());
                    glVertex4fv([0f32, 1., 0., 1.].as_ptr());
                    glEnd();
                    assert_eq!(glGetError(), 0);
                    with_immediate(|r| {
                        assert_eq!(r.transient.len(), 1);
                        let v =
                            slice::from_raw_parts(r.transient[0].pointer.cast::<GpuVertex>(), 3);
                        assert_eq!(v[0].color, [255., 0., 0., 255.]);
                        assert_eq!(v[0].texcoord, [0.25, 0.75, 0., 1.]);
                        assert_eq!(v[1].texcoord, [0.5, 1., 0., 1.]);
                        assert_eq!(v[1].position, [1., -1., 0., 1.]);
                        assert_eq!(v[2].color, [0., 0., 255., 255.]);
                    });
                    assert_eq!(c3dglNativePresent(), 0);
                    with_immediate(|r| assert_eq!(r.transient.len(), 1));
                    assert_eq!(c3dglNativeBegin(0xff, false), 0);
                    with_immediate(|r| assert!(r.transient.is_empty()));
                    assert_eq!(c3dglNativePresent(), 0);
                    glEnd();
                    assert_eq!(glGetError(), 0x0502);
                }
                let before = MOCK.lock().unwrap().submits;
                MOCK.lock().unwrap().generation += 1;
                assert_eq!(c3dglNativeBegin(0xff, false), 0);
                assert_eq!(MOCK.lock().unwrap().submits, before + 1);
                with_frame(|s, _| assert!(s.raster.dirty));
                c3dglClose();
                c3dglClose();
                let m = MOCK.lock().unwrap();
                assert!(m.live.is_empty());
                assert!(!m.acquired);
            }
        }
    }
}

pub(crate) unsafe fn with_immediate(f: impl FnOnce(&mut Renderer)) {
    if let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() {
        f(r);
    }
}

pub(crate) unsafe fn with_renderer(f: impl FnOnce(&mut Renderer)) {
    with_immediate(|r| {
        if r.immediate.active {
            r.matrices.fail(0x0502);
        } else {
            f(r);
        }
    });
}

/// Submit one immediate-mode vertex: lighting turns the current color into the lit front color
/// (and the back color with two-sided lighting). `color`/`texcoord` override the current ones.
pub(crate) unsafe fn emit_vertex(
    r: &mut Renderer,
    position: [f32; 4],
    color: Option<[f32; 4]>,
    texcoord: Option<[f32; 4]>,
) {
    let mut gpu = color
        .unwrap_or(r.immediate.color)
        .map(crate::immediate::gpu_color_component);
    let mut back = None;
    // Lighting and texture generation work on the homogeneous position divided by w.
    let p = if position[3] != 1. && position[3] != 0. {
        [
            position[0] / position[3],
            position[1] / position[3],
            position[2] / position[3],
            1.,
        ]
    } else {
        position
    };
    let mv = r.matrices.modelview();
    if r.frame.light.enabled {
        let bytes = gpu.map(|c| c as u8);
        let (front, b) = r
            .frame
            .light
            .light_vertex(&mv, p, r.immediate.normal, bytes);
        gpu = front.map(|c| c as f32);
        back = b.map(|b| b.map(|c| c as f32));
    }
    let (mut tex, mut extra) = (
        texcoord.unwrap_or(r.immediate.texcoord),
        r.immediate.extra_texcoords,
    );
    let generated = r.frame.texgen.active();
    if generated {
        texgen_vertex(r, &mv, p, r.immediate.normal, &mut tex, &mut extra);
    }
    let extra = generated.then_some(extra);
    if let Err(e) = r.immediate.push(position, gpu, back, Some(tex), extra) {
        r.matrices.fail(e);
    }
}

/// Texture coordinate generation of one vertex (see texgen.rs).
unsafe fn texgen_vertex(
    r: &mut Renderer,
    mv: &[f32; 16],
    position: [f32; 4],
    normal: [f32; 3],
    tex: &mut [f32; 4],
    extra: &mut [[f32; 4]; 2],
) {
    let textures: [[f32; 16]; 3] = core::array::from_fn(|u| r.matrices.texture_for(u));
    let Renderer { frame, .. } = r;
    let light = &mut frame.light;
    frame.texgen.generate(
        mv,
        &textures,
        position,
        || light.eye_normal_of(mv, normal),
        tex,
        extra,
    );
}

/// Light every vertex of a draw in place; the back colors with two-sided lighting (else empty).
unsafe fn light_vertices(
    r: &mut Renderer,
    vertices: &mut [GpuVertex],
    normals: &[[f32; 3]],
) -> alloc::vec::Vec<[f32; 4]> {
    let mut backs = alloc::vec::Vec::new();
    if !r.frame.light.enabled {
        return backs;
    }
    let mv = r.matrices.modelview();
    for (v, n) in vertices.iter_mut().zip(normals) {
        let bytes = v.color.map(|c| c as u8);
        let (front, back) = r.frame.light.light_vertex(&mv, v.position, *n, bytes);
        v.color = front.map(|c| c as f32);
        if let Some(b) = back {
            backs.push(b.map(|c| c as f32));
        }
    }
    backs
}

fn line_style(r: &Renderer) -> crate::lines::Style {
    crate::lines::Style {
        pmv: r.matrices.combined(),
        viewport: r.frame.viewport,
        line_width: r.frame.line_width,
        point_size: r.frame.point_size,
    }
}

fn poly_settings(r: &Renderer) -> crate::polygon::Settings {
    let (poly, raster) = (&r.frame.poly, &r.frame.raster);
    crate::polygon::Settings {
        flat: poly.flat,
        two_sided: r.frame.light.enabled && r.frame.light.two_side,
        modes: poly.modes,
        offset: poly.offset,
        factor: poly.factor,
        units: poly.units,
        front_ccw: raster.front == 0x0901,
        cull: raster.cull,
        cull_face: raster.face,
        range: raster.range,
        style: line_style(r),
    }
}

unsafe fn draw_items(
    r: &mut Renderer,
    items: alloc::vec::Vec<crate::polygon::Item>,
) -> Result<(), u32> {
    for item in items {
        match item {
            crate::polygon::Item::Fill { vertices, bias } => {
                draw_transient(r, &vertices, &[], vertices.len() as u32, false, bias)?
            }
            crate::polygon::Item::Clip { vertices, indices } => {
                draw_transient(r, &vertices, &indices, indices.len() as u32, true, 0.)?
            }
        }
    }
    Ok(())
}

/// Upload vertices (and indices) for one draw of the frame and draw them. `clip`: positions are
/// already NDC (expanded lines and points).
unsafe fn draw_transient(
    r: &mut Renderer,
    vertices: &[GpuVertex],
    indices: &[u16],
    count: u32,
    clip: bool,
    bias: f32,
) -> Result<(), u32> {
    r.transient.try_reserve(1).map_err(|_| 0x0505u32)?;
    let bytes = vertices.len() * core::mem::size_of::<GpuVertex>();
    let total = bytes + indices.len() * 2;
    let storage = Buffer::new(total, false).ok_or(0x0505u32)?;
    ptr::copy_nonoverlapping(vertices.as_ptr().cast::<u8>(), storage.pointer, bytes);
    ptr::copy_nonoverlapping(
        indices.as_ptr().cast::<u8>(),
        storage.pointer.add(bytes),
        indices.len() * 2,
    );

    if p::c3dglPlatformFlush(storage.pointer, total as u32) < 0 {
        return Err(0x0502);
    }

    update_draw_state(r, clip, bias).map_err(|_| 0x0505u32)?;
    let mut c = r.commands();
    gpu::geometry(
        &mut c,
        storage.physical(),
        0,
        count,
        (!indices.is_empty()).then_some(bytes as u32 | 0x80000000),
    )
    .map_err(|_| 0x0505u32)?;

    r.used = c.used;
    r.transient.push(storage);
    r.frame.textures.sampled();
    r.draws += 1;

    Ok(())
}

pub(crate) unsafe fn end_immediate() {
    with_immediate(|r| {
        let result = (|| -> Result<(), u32> {
            let batch = r.immediate.end()?;
            if batch.count == 0 {
                return Ok(());
            }

            if !r.active && begin_frame(r, true, None) != 0 {
                return Err(0x0502);
            }

            if !r.frame.drawable {
                return Ok(());
            }

            if batch.primitive < 4 {
                let (vertices, indices) = crate::lines::expand(
                    batch.primitive,
                    &batch.vertices,
                    &line_style(r),
                    r.frame.poly.flat,
                )?;
                if indices.is_empty() {
                    return Ok(());
                }
                return draw_transient(r, &vertices, &indices, indices.len() as u32, true, 0.);
            }

            let settings = poly_settings(r);
            if settings.needed() {
                let items = crate::polygon::assemble(
                    batch.primitive,
                    &batch.vertices,
                    &batch.backs,
                    &batch.edges,
                    &settings,
                )?;
                return draw_items(r, items);
            }

            if !r.frame.raster.drawable() {
                return Ok(());
            }

            draw_transient(r, &batch.vertices, &batch.indices, batch.count, false, 0.)
        })();

        if let Err(e) = result {
            r.matrices.fail(e);
        }
    });
}

pub(crate) unsafe fn draw_arrays(mode: u32, first: i32, count: i32, kind: u32, indices: usize) {
    with_renderer(|r| {
        let result = (|| -> Result<(), u32> {
            if first < 0 || count < 0 {
                return Err(0x0501);
            }

            if kind != 0 && !matches!(kind, 0x1401 | 0x1403 | 0x1405) {
                return Err(0x0500);
            }

            let plan = crate::primitive::Plan::new(mode, count as usize)?;
            let count = plan.consumed;
            if count == 0 {
                return Ok(());
            }

            if !r.active && begin_frame(r, true, None) != 0 {
                return Err(0x0502);
            }

            if !r.frame.drawable {
                return Ok(());
            }

            if r.arrays.texcoord0_has_q() {
                r.frame.textures.mark_q();
            }

            let settings = poly_settings(r);
            let lit = r.frame.light.enabled;
            let generated = r.frame.texgen.active();
            if mode < 4 || lit || generated || settings.needed() {
                let (mut source, normals) = r.arrays.gather(
                    first as usize,
                    count,
                    kind,
                    indices,
                    r.immediate.color,
                    r.immediate.texcoord,
                    r.immediate.extra_texcoords,
                    r.immediate.normal,
                )?;
                if generated {
                    let mv = r.matrices.modelview();
                    for (v, n) in source.iter_mut().zip(&normals) {
                        let p = if v.position[3] != 1. && v.position[3] != 0. {
                            let w = v.position[3];
                            [v.position[0] / w, v.position[1] / w, v.position[2] / w, 1.]
                        } else {
                            v.position
                        };
                        let (mut tex, mut extra) = (v.texcoord, v.extra_texcoords);
                        texgen_vertex(r, &mv, p, *n, &mut tex, &mut extra);
                        v.texcoord = tex;
                        v.extra_texcoords = extra;
                    }
                }
                let backs = light_vertices(r, &mut source, &normals);
                if mode < 4 {
                    let (vertices, tris) =
                        crate::lines::expand(mode, &source, &line_style(r), r.frame.poly.flat)?;
                    if tris.is_empty() {
                        return Ok(());
                    }
                    return draw_transient(r, &vertices, &tris, tris.len() as u32, true, 0.);
                }
                if settings.needed() {
                    let edges = alloc::vec![true; source.len()];
                    let items = crate::polygon::assemble(mode, &source, &backs, &edges, &settings)?;
                    return draw_items(r, items);
                }
                if !r.frame.raster.drawable() {
                    return Ok(());
                }
                // Lit vertices are drawn as an indexed triangle list.
                let tris =
                    crate::primitive::Plan::new(mode, source.len())?.indices(|i| i as u16)?;
                let count = if mode == 4 { source.len() } else { tris.len() };
                let indices = if mode == 4 { &[][..] } else { &tris[..] };
                return draw_transient(r, &source, indices, count as u32, false, 0.);
            }

            if !r.frame.raster.drawable() {
                return Ok(());
            }

            let (address, index, draw_count) = r.arrays.prepare(
                mode,
                first as usize,
                count,
                kind,
                indices,
                r.immediate.color,
                r.immediate.texcoord,
                r.immediate.extra_texcoords,
            )?;

            update_draw_state(r, false, 0.).map_err(|_| 0x0505u32)?;
            let mut c = r.commands();
            gpu::geometry(&mut c, address, 0, draw_count, index).map_err(|_| 0x0505u32)?;
            r.used = c.used;
            r.frame.textures.sampled();
            r.draws += 1;

            Ok(())
        })();

        if let Err(e) = result {
            r.matrices.fail(e);
        }
    });
}
