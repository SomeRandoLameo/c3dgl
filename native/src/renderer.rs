//! Single application-thread renderer. All service calls go through platform.
use crate::{gpu, platform::gpu as p, Commands};
use core::{ptr, slice};
const COMMAND_WORDS: usize = 16384;
const STATE_WORDS: usize = 1024;
const MAX_VERTICES: usize = 4096;
const TARGET_BYTES: usize = 240 * 400 * 4;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Vertex { 
    pub position: [f32; 4], 
    pub color: [f32; 4] 
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
    vram: bool 
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

impl Renderer {
    unsafe fn create() -> Option<Self> {
        // Fallible allocation and RAII rollback for every intermediate failure.
        let commands = Buffer::new(COMMAND_WORDS * 4, false)?;
        let state = Buffer::new(STATE_WORDS * 4, false)?;
        let vertices = Buffer::new(MAX_VERTICES * 32, false)?;
        let color = Buffer::new(TARGET_BYTES, true)?;
        let depth = Buffer::new(TARGET_BYTES, true)?;
        
        let mut encoded = Commands::new(slice::from_raw_parts_mut(state.pointer.cast(), STATE_WORDS));
        
        gpu::initialize(&mut encoded, color.physical(), depth.physical(), vertices.physical()).ok()?;
        
        let state_words = encoded.used;
        
        Some(
            Self { 
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
                error: 0 
            }
        )
    }
    
    unsafe fn restore(&mut self) -> bool {
        let generation = p::c3dglPlatformGeneration();
        
        if self.generation == generation { 
            return true; 
        }
        
        if p::c3dglPlatformSubmit(self.state.pointer, (self.state_words * 4) as u32, ptr::null_mut()) < 0 {
            return false;
        }
        
        p::c3dglPlatformWait();
        
        self.generation = generation;
        
        true
    }
    
    unsafe fn commands(&mut self) -> Commands<'_> {
        Commands { 
            words: slice::from_raw_parts_mut(self.commands.pointer.cast(), COMMAND_WORDS), 
            used: self.used 
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

/// Upload persistent geometry between frames. Positions use OpenGL clip space;
/// rotation and depth remapping happen once on upload, without matrix uniforms.
#[no_mangle]
pub unsafe extern "C" fn c3dglNativeUpload(vertices: *const Vertex, count: usize) -> i32 {
    let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() 
        else { 
            return -1; 
        };
    
    if r.active || vertices.is_null() || count == 0 || count > MAX_VERTICES { 
        return -2; 
    }
    
    p::c3dglPlatformWait(); // Never overwrite vertices still being read by GPU.
    
    let input = slice::from_raw_parts(vertices, count);
    if input.iter().any(|v| v.position.iter().chain(v.color.iter()).any(|x| !x.is_finite())) { 
        return -2; 
    }
    
    let output = slice::from_raw_parts_mut(r.vertices.pointer.cast::<Vertex>(), count);
    for (src, dst) in input.iter().zip(output) {
        let [x, y, z, w] = src.position;
        *dst = Vertex { 
            position: [y, -x, (z - w) * 0.5, w], 
            color: src.color 
        };
    }
    
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
    let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() 
        else { 
            return -1; 
        };
    
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
    
    let mut commands = r.commands();
    if gpu::begin(&mut commands).is_err() { 
        return -3; 
    }
    
    r.used = commands.used;
    
    let result = p::c3dglPlatformClear(r.color.pointer, r.depth.pointer, clear, TARGET_BYTES as u32);
    if result < 0 { 
        return result; 
    }
    
    r.active = true;
    
    0
}

#[no_mangle]
pub unsafe extern "C" fn c3dglNativeDraw(first: usize, count: usize) -> i32 {
    let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() 
        else { 
            return -1; 
        };
    
    if !r.active || count == 0 || count % 3 != 0 || first > r.vertex_count || count > r.vertex_count - first {
        return -2;
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

#[no_mangle]
pub unsafe extern "C" fn c3dglNativePresent() -> i32 {
    let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() 
        else { 
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
    r.stats.cpu_ticks = p::c3dglPlatformTick() - r.started;
    r.stats.wait_ticks = r.wait_ticks;
    r.stats.command_bytes = (r.used * 4) as u32;
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
    let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() 
        else { 
            return -1; 
        };
    
    if output.is_null() || r.active { 
        return -2; 
    }
    
    r.stats.gpu_ticks = p::c3dglPlatformWait();
    
    output.write(r.stats);
    
    0
}

#[no_mangle]
pub unsafe extern "C" fn c3dglSwapBuffers() { 
    let _ = c3dglNativePresent(); 
}

#[no_mangle]
pub unsafe extern "C" fn c3dglNativeReadback(pixels: *mut u8, capacity: usize) -> i32 {
    let Some(r) = (&mut *ptr::addr_of_mut!(CONTEXT)).as_mut() 
        else { 
            return -1; 
        };
    
    if r.active || pixels.is_null() || capacity < 400*240*3 || capacity > u32::MAX as usize { 
        return -2; 
    }
    
    p::c3dglPlatformReadback(pixels, capacity as u32)
}
