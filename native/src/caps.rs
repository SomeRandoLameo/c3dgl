//! Capabilities that programs commonly toggle but that have no effect on this framebuffer (color
//! index mode, no sample buffers, no antialiasing, no dithering): stored for glIsEnabled and the
//! attribute stack.

const CAPS: [u32; 9] = [
    0x0bd0, // GL_DITHER
    0x0b20, // GL_LINE_SMOOTH
    0x0b10, // GL_POINT_SMOOTH
    0x0b41, // GL_POLYGON_SMOOTH
    0x0bf1, // GL_INDEX_LOGIC_OP
    0x809d, // GL_MULTISAMPLE
    0x809e, // GL_SAMPLE_ALPHA_TO_COVERAGE
    0x809f, // GL_SAMPLE_ALPHA_TO_ONE
    0x80a0, // GL_SAMPLE_COVERAGE
];

/// The groups of the attribute stack that restore stored-only capabilities.
pub(crate) const POINT: u16 = 1 << 2;
pub(crate) const LINE: u16 = 1 << 1;
pub(crate) const POLYGON: u16 = 1 << 3;
pub(crate) const COLOR_BUFFER: u16 = 1 << 0 | 1 << 4;
pub(crate) const MULTISAMPLE: u16 = 0b1111 << 5;

#[derive(Clone, Copy)]
pub(crate) struct Caps {
    bits: u16,
}

impl Caps {
    /// GL_DITHER and GL_MULTISAMPLE are enabled by default.
    pub fn new() -> Self {
        Self {
            bits: 1 << 0 | 1 << 5,
        }
    }

    fn index(cap: u32) -> Option<u16> {
        CAPS.iter().position(|c| *c == cap).map(|i| i as u16)
    }

    pub fn is_enabled(&self, cap: u32) -> Option<bool> {
        Self::index(cap).map(|i| self.bits >> i & 1 != 0)
    }

    /// False when `cap` is not one of the stored-only capabilities.
    pub fn set(&mut self, cap: u32, on: bool) -> bool {
        let Some(i) = Self::index(cap) else {
            return false;
        };
        if on {
            self.bits |= 1 << i;
        } else {
            self.bits &= !(1 << i);
        }
        true
    }

    /// Restore the capabilities of the groups in `mask` from a saved state.
    pub fn restore(&mut self, saved: &Caps, mask: u16) {
        self.bits = self.bits & !mask | saved.bits & mask;
    }

    pub fn restore_all(&mut self, saved: &Caps) {
        self.bits = saved.bits;
    }
}
