//! Line and polygon stipple state (GL 1.1 sections 3.4.2 and 3.5.2). Line stipple is applied
//! while lines are expanded (`lines.rs`); the polygon pattern is a 32x32 bitmap.
use crate::renderer::with_frame;
use alloc::vec::Vec;

#[derive(Clone, Copy)]
pub(crate) struct Stipple {
    pub line: bool,
    /// 1..=256.
    pub factor: i32,
    pub pattern: u16,
    pub polygon: bool,
    /// 32 rows of 4 bytes, most significant bit first (bit 0 of the pattern is the left pixel).
    pub mask: [u8; 128],
}

impl Stipple {
    pub fn new() -> Self {
        Self {
            line: false,
            factor: 1,
            pattern: 0xffff,
            polygon: false,
            mask: [0xff; 128],
        }
    }

    pub fn is_enabled(&self, cap: u32) -> Option<bool> {
        match cap {
            0x0b24 => Some(self.line),
            0x0b42 => Some(self.polygon),
            _ => None,
        }
    }

    pub fn set_enabled(&mut self, cap: u32, on: bool) -> bool {
        match cap {
            0x0b24 => self.line = on,
            0x0b42 => self.polygon = on,
            _ => return false,
        }
        true
    }

    pub fn query(&self, pname: u32) -> Option<f64> {
        match pname {
            0x0b25 => Some(self.pattern as f64),
            0x0b26 => Some(self.factor as f64),
            _ => None,
        }
    }

    /// The line stipple as (factor, pattern) when enabled.
    pub fn line_style(&self) -> Option<(u32, u16)> {
        self.line.then_some((self.factor as u32, self.pattern))
    }
}

#[no_mangle]
pub unsafe extern "C" fn glLineStipple(factor: i32, pattern: u16) {
    if crate::lists::record(crate::lists::Cmd::LineStipple(factor, pattern)) {
        return;
    }
    with_frame(|s, _| {
        s.stipple.factor = factor.clamp(1, 256);
        s.stipple.pattern = pattern;
    });
}

/// Set the pattern from the 128 bytes of a 32x32 bitmap (display list execution).
pub(crate) unsafe fn set_polygon_mask(mask: [u8; 128]) {
    with_frame(|s, _| {
        s.stipple.mask = mask;
        s.raster.dirty = true;
    });
}

/// Unpack a 32x32 bitmap like a glBitmap image (unpack alignment, row length and skips).
unsafe fn unpack(mask: *const u8) -> [u8; 128] {
    let mut store = crate::texture::PixelStore::default();
    crate::renderer::with_immediate(|r| store = r.frame.textures.unpack());
    let row_bytes = (store.row_length_or(32) + 7) / 8;
    let row_bytes = (row_bytes + store.alignment - 1) / store.alignment * store.alignment;
    let mut out = [0u8; 128];
    for y in 0..32 {
        for x in 0..32 {
            let col = store.skip_pixels + x;
            let byte = mask.add((store.skip_rows + y) * row_bytes + col / 8).read();
            let bit = if store.lsb_first {
                byte >> (col & 7) & 1 != 0
            } else {
                (byte << (col & 7)) & 0x80 != 0
            };
            if bit {
                out[4 * y + x / 8] |= 0x80 >> (x & 7);
            }
        }
    }
    out
}

#[no_mangle]
pub unsafe extern "C" fn glPolygonStipple(mask: *const u8) {
    if mask.is_null() {
        return;
    }
    let packed = unpack(mask);
    if crate::lists::record(crate::lists::Cmd::PolygonStipple(Vec::from(packed))) {
        return;
    }
    set_polygon_mask(packed);
}

#[no_mangle]
pub unsafe extern "C" fn glGetPolygonStipple(mask: *mut u8) {
    if mask.is_null() {
        return;
    }
    let mut store = crate::texture::PixelStore::default();
    let mut pattern = [0u8; 128];
    crate::renderer::with_immediate(|r| {
        store = r.frame.textures.pack();
        pattern = r.frame.stipple.mask;
    });
    let row_bytes = (store.row_length_or(32) + 7) / 8;
    let row_bytes = (row_bytes + store.alignment - 1) / store.alignment * store.alignment;
    for y in 0..32 {
        let row = mask.add((store.skip_rows + y) * row_bytes);
        for x in 0..32 {
            let col = store.skip_pixels + x;
            let bit = if store.lsb_first {
                1u8 << (col & 7)
            } else {
                0x80u8 >> (col & 7)
            };
            let p = row.add(col / 8);
            if (pattern[4 * y + x / 8] << (x & 7)) & 0x80 != 0 {
                p.write(p.read() | bit);
            } else {
                p.write(p.read() & !bit);
            }
        }
    }
}

/// The alpha test with polygon stipple: stage 3 gives the fragments outside the pattern an alpha
/// that fails it. Returns (GL function, reference, `high`, exact): `high` selects alpha + (1 -
/// texel alpha) over alpha - (1 - texel alpha), both saturate exactly. A test that passes 0 and 1
/// becomes alpha != 0, so fragments of alpha 0 are dropped as well; alpha != r (0 < r < 255)
/// becomes alpha != 0 too and is not exact.
pub(crate) fn alpha_test(enabled: bool, func: u32, r: u8) -> (u32, u8, bool, bool) {
    let f = if enabled { func } else { 0x0207 };
    match f {
        0x0200 | 0x0204 => return (f, r, false, true),
        0x0201 => return (f, r, true, true),
        0x0203 if r < 255 => return (f, r, true, true),
        0x0206 if r > 0 => return (f, r, false, true),
        0x0202 => return (f, r, r == 0, true),
        0x0205 if r == 0 || r == 255 => return (f, r, r == 255, true),
        0x0205 => return (0x0205, 0, false, false),
        _ => {}
    }
    (0x0205, 0, false, true)
}

/// Texcoord rows (s, t, q) = (window x / 32, window y / 32, 1) times w_clip for the object
/// position, so PICA's per-pixel division samples the texel centers.
pub(crate) fn rows(m: &[f32; 16], viewport: [i32; 4]) -> [[f32; 4]; 3] {
    let (half_w, half_h) = (0.5 * viewport[2] as f32, 0.5 * viewport[3] as f32);
    let (ox, oy) = (viewport[0] as f32 + half_w, viewport[1] as f32 + half_h);
    let mut out = [[0.; 4]; 3];
    for i in 0..4 {
        out[0][i] = (m[4 * i] * half_w + m[4 * i + 3] * ox) / 32.;
        out[1][i] = (m[4 * i + 1] * half_h + m[4 * i + 3] * oy) / 32.;
        out[2][i] = m[4 * i + 3];
    }
    out
}

/// The pattern as a tiled 32x32 A8 texture.
pub(crate) fn texels(mask: &[u8; 128]) -> [u8; 1024] {
    let mut out = [0; 1024];
    for y in 0..32 {
        for x in 0..32 {
            if (mask[4 * y + x / 8] << (x & 7)) & 0x80 != 0 {
                out[crate::texture::offset_bpp(32, 32, x, y, 1)] = 255;
            }
        }
    }
    out
}
