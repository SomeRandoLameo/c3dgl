//! Retained libctru services and GPU-visible linear/VRAM storage.
//! C/C++ uses the platform allocator; rust3ds uses its application std allocator.
#[cfg(not(any(test, feature = "host-tests", feature = "rust3ds", feature = "host-bench")))]
mod runtime {
    use core::alloc::{GlobalAlloc, Layout};

    extern "C" {
        fn c3dglPlatformAllocate(size: usize, alignment: usize) -> *mut u8;
        fn c3dglPlatformFree(pointer: *mut u8);
    }

    struct PlatformAllocator;
    unsafe impl GlobalAlloc for PlatformAllocator {
        unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
            c3dglPlatformAllocate(layout.size(), layout.align())
        }
        unsafe fn dealloc(&self, pointer: *mut u8, _: Layout) {
            c3dglPlatformFree(pointer);
        }
    }

    #[global_allocator]
    static ALLOCATOR: PlatformAllocator = PlatformAllocator;
}

#[cfg(any(target_os = "horizon", test, feature = "host-bench"))]
pub mod gpu {
    extern "C" {
        pub fn c3dglPlatformLinearAllocate(size: usize) -> *mut u8;
        pub fn c3dglPlatformLinearFree(pointer: *mut u8);
        pub fn c3dglPlatformVramAllocate(size: usize) -> *mut u8;
        pub fn c3dglPlatformVramFree(pointer: *mut u8);
        pub fn c3dglPlatformPhysical(pointer: *const u8) -> u32;
        pub fn c3dglPlatformTick() -> u64;
        pub fn c3dglPlatformFlush(pointer: *const u8, bytes: u32) -> i32;
        pub fn c3dglPlatformAcquire() -> i32;
        pub fn c3dglPlatformRelease();
        pub fn c3dglPlatformWait() -> u64;
        pub fn c3dglPlatformReadback(pixels: *mut u8, capacity: u32) -> i32;
        pub fn c3dglPlatformReadColor(color: *mut u8, line0: u32, lines: u32, out: *mut u8) -> i32;
        pub fn c3dglPlatformWriteLines(
            target: *mut u8,
            line0: u32,
            lines: u32,
            input: *mut u8,
        ) -> i32;
        pub fn c3dglPlatformGeneration() -> u32;
        pub fn c3dglPlatformVblank();
        pub fn c3dglPlatformClear(color: *mut u8, depth: *mut u8, value: u32, bytes: u32) -> i32;
        pub fn c3dglPlatformClearMasked(
            color: *mut u8,
            depth: *mut u8,
            value: u32,
            depth_value: u32,
            bytes: u32,
            mask: u32,
        ) -> i32;
        pub fn c3dglPlatformSubmit(commands: *mut u8, bytes: u32, color: *mut u8) -> i32;
    }
}
