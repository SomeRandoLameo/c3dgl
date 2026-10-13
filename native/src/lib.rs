#![cfg_attr(not(any(test, feature = "host-tests", feature = "rust3ds")), no_std)]

extern crate alloc;
#[cfg(any(target_os = "horizon", test))]
mod frame;
#[cfg(any(target_os = "horizon", test))]
mod gpu;
#[cfg(any(target_os = "horizon", test))]
mod matrix;
mod platform;
#[cfg(any(target_os = "horizon", test))]
mod registers;
#[cfg(any(target_os = "horizon", test))]
mod renderer;
#[cfg(any(target_os = "horizon", test))]
mod shader;

/// PICA command encoding is independent of GPU services and memory allocation.
/// The caller owns the storage; no allocation and no partial writes on overflow.
pub struct Commands<'a> {
    words: &'a mut [u32],
    used: usize,
}

impl<'a> Commands<'a> {
    pub fn new(words: &'a mut [u32]) -> Self {
        Self { words, used: 0 }
    }

    pub fn as_slice(&self) -> &[u32] {
        &self.words[..self.used]
    }

    pub fn write(&mut self, register: u16, mask: u8, incremental: bool, values: &[u32]) -> bool {
        if register > 0x3ff || mask > 15 || values.is_empty() || values.len() > 256 {
            return false;
        }

        let count = (values.len() + 2) & !1;
        if self.words.len() - self.used < count {
            return false;
        }

        let start = self.used;
        self.words[start] = values[0];
        self.words[start + 1] = u32::from(register)
            | (u32::from(mask) << 16)
            | (((values.len() - 1) as u32) << 20)
            | ((incremental as u32) << 31);
        self.words[start + 2..start + values.len() + 1].copy_from_slice(&values[1..]);

        if values.len() % 2 == 0 {
            self.words[start + count - 1] = 0;
        }

        self.used += count;
        true
    }
}

/// Returns the number of words written; zero reports invalid input or insufficient
/// capacity. Input and output must be valid, aligned, non-overlapping allocations.
#[no_mangle]
pub unsafe extern "C" fn c3dglNativeEncodeCommand(
    output: *mut u32,
    capacity: usize,
    register: u16,
    mask: u8,
    incremental: bool,
    values: *const u32,
    count: usize,
) -> usize {
    if output.is_null()
        || values.is_null()
        || capacity > isize::MAX as usize / 4
        || count == 0
        || count > 256
    {
        return 0;
    }

    let words = core::slice::from_raw_parts_mut(output, capacity);
    let values = core::slice::from_raw_parts(values, count);
    let mut commands = Commands::new(words);
    if commands.write(register, mask, incremental, values) {
        commands.used
    } else {
        0
    }
}

#[cfg(not(any(test, feature = "host-tests", feature = "rust3ds")))]
#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn packet_layout_and_padding() {
        let mut storage = [0xdeadbeef; 8];
        let mut commands = Commands::new(&mut storage);
        assert!(commands.write(0x200, 15, false, &[42]));
        assert!(commands.write(0x201, 3, true, &[1, 2]));
        assert_eq!(commands.as_slice(), &[42, 0x000f0200, 1, 0x80130201, 2, 0]);
    }

    #[test]
    fn overflow_is_transactional() {
        let mut storage = [7; 2];
        let mut commands = Commands::new(&mut storage);
        assert!(!commands.write(0x200, 15, false, &[1, 2]));
        assert!(commands.as_slice().is_empty());
        assert_eq!(storage, [7; 2]);
    }

    #[test]
    fn invalid_inputs_and_largest_packet() {
        let mut storage = [0; 258];
        let mut commands = Commands::new(&mut storage);
        assert!(!commands.write(0x400, 15, false, &[1]));
        assert!(!commands.write(0, 16, false, &[1]));
        assert!(!commands.write(0, 15, false, &[]));
        assert!(commands.write(0x200, 15, false, &[1; 256]));
        assert_eq!(commands.as_slice().len(), 258);
        assert_eq!(commands.as_slice()[1], 0x0fff0200);
    }
}

/// Raw Rust entry points with the same single-thread/exclusive-GPU contract as C.
/// `rust3ds` builds use the application's std allocator and panic runtime.
#[cfg(target_os = "horizon")]
pub mod raw {
    pub use crate::frame::{
        glClear, glClearColor, glClearDepth, glClearDepthf, glGetFloatv, glGetIntegerv, glViewport,
    };

    pub use crate::matrix::*;
    pub use crate::renderer::{
        c3dglClose, c3dglInit, c3dglNativeBegin, c3dglNativeDraw, c3dglNativeGetStats,
        c3dglNativePresent, c3dglNativeReadback, c3dglNativeUpload, c3dglSwapBuffers, Stats,
        Vertex,
    };
}
