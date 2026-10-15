//! Raster position, pixel transfer, glBitmap, glDrawPixels, glCopyPixels, glReadPixels (color).
//! Images and bitmaps become textures drawn as quads in window coordinates through the fragment
//! pipeline (`renderer::draw_pixel_quad`); the framebuffer is read through a display transfer.
//! Depth and stencil images are not supported and report GL_INVALID_OPERATION.
use crate::{
    immediate::gpu_color_component,
    renderer::{
        draw_pixel_quad, read_color, with_renderer, PixelFormat, PixelMode, PixelQuad, Renderer,
    },
    texture::{offset_bpp, PixelStore},
};
use alloc::vec::Vec;

extern "C" {
    fn floorf(x: f32) -> f32;
    fn ceilf(x: f32) -> f32;
    fn roundf(x: f32) -> f32;
    fn lroundf(x: f32) -> i32;
}

const WIDTH: usize = 400;
const HEIGHT: usize = 240;
const TILE: usize = 256;

pub(crate) const I_TO_I: usize = 0;
pub(crate) const S_TO_S: usize = 1;
pub(crate) const I_TO_R: usize = 2;
pub(crate) const R_TO_R: usize = 6;

#[derive(Clone, Copy)]
pub(crate) struct RasterPos {
    pub valid: bool,
    pub pos: [f32; 4],
    pub color: [u8; 4],
    pub tex: [f32; 4],
    pub distance: f32,
}

#[derive(Clone, Copy)]
pub(crate) struct Transfer {
    pub map_color: bool,
    pub map_stencil: bool,
    pub index_shift: i32,
    pub index_offset: i32,
    /// R, G, B, A, depth.
    pub scale: [f32; 5],
    pub bias: [f32; 5],
}

impl Transfer {
    fn new() -> Self {
        Self {
            map_color: false,
            map_stencil: false,
            index_shift: 0,
            index_offset: 0,
            scale: [1.; 5],
            bias: [0.; 5],
        }
    }

    fn color_active(&self) -> bool {
        self.map_color || (0..4).any(|i| self.scale[i] != 1. || self.bias[i] != 0.)
    }
}

#[derive(Clone)]
pub(crate) struct Pixels {
    pub raster: RasterPos,
    pub zoom: [f32; 2],
    pub transfer: Transfer,
    /// GL_PIXEL_MAP_I_TO_I .. A_TO_A (not in an attribute group).
    maps: [Vec<f32>; 10],
}

impl Pixels {
    pub fn new() -> Self {
        Self {
            raster: RasterPos {
                valid: true,
                pos: [0., 0., 0., 1.],
                color: [255; 4],
                tex: [0., 0., 0., 1.],
                distance: 0.,
            },
            zoom: [1.; 2],
            transfer: Transfer::new(),
            maps: core::array::from_fn(|_| alloc::vec![0.]),
        }
    }

    pub fn query(&self, pname: u32) -> Option<([f64; 16], usize)> {
        let mut v = [0.; 16];
        let t = &self.transfer;
        let n = match pname {
            0x0b04 => {
                for i in 0..4 {
                    v[i] = self.raster.color[i] as f64 / 255.;
                }
                4
            }
            0x0b05 => 1,
            0x0b06 => {
                for i in 0..4 {
                    v[i] = self.raster.tex[i] as f64;
                }
                4
            }
            0x0b07 => {
                for i in 0..4 {
                    v[i] = self.raster.pos[i] as f64;
                }
                4
            }
            0x0b08 => {
                v[0] = self.raster.valid as u8 as f64;
                1
            }
            0x0b09 => {
                v[0] = self.raster.distance as f64;
                1
            }
            0x0d16 | 0x0d17 => {
                v[0] = self.zoom[(pname - 0x0d16) as usize] as f64;
                1
            }
            0x0d10 => {
                v[0] = t.map_color as u8 as f64;
                1
            }
            0x0d11 => {
                v[0] = t.map_stencil as u8 as f64;
                1
            }
            0x0d12 => {
                v[0] = t.index_shift as f64;
                1
            }
            0x0d13 => {
                v[0] = t.index_offset as f64;
                1
            }
            0x0d14 | 0x0d15 | 0x0d18..=0x0d1f => {
                let (component, bias) = transfer_component(pname)?;
                v[0] = if bias {
                    t.bias[component]
                } else {
                    t.scale[component]
                } as f64;
                1
            }
            0x0d34 => {
                v[0] = 256.;
                1
            }
            0x0cb0..=0x0cb9 => {
                v[0] = self.maps[(pname - 0x0cb0) as usize].len() as f64;
                1
            }
            _ => return None,
        };
        Some((v, n))
    }
}

/// Component (R, G, B, A, depth) and whether it is the bias of a glPixelTransfer scale/bias.
fn transfer_component(pname: u32) -> Option<(usize, bool)> {
    Some(match pname {
        0x0d14 => (0, false),
        0x0d15 => (0, true),
        0x0d18 => (1, false),
        0x0d19 => (1, true),
        0x0d1a => (2, false),
        0x0d1b => (2, true),
        0x0d1c => (3, false),
        0x0d1d => (3, true),
        0x0d1e => (4, false),
        0x0d1f => (4, true),
        _ => return None,
    })
}

// ---------------------------------------------------------------------------------------------
// Image layouts and conversion
// ---------------------------------------------------------------------------------------------
const BITMAP: u32 = 0x1a00;
const COLOR_INDEX: u32 = 0x1900;

/// Components of a color format: indices into r, g, b, a, 4 = luminance (r + g + b).
fn components(format: u32) -> Option<&'static [usize]> {
    Some(match format {
        0x1908 => &[0, 1, 2, 3],
        0x1907 => &[0, 1, 2],
        0x1903 => &[0],
        0x1904 => &[1],
        0x1905 => &[2],
        0x1906 => &[3],
        0x1909 => &[4],
        0x190a => &[4, 3],
        _ => return None,
    })
}

fn type_size(ty: u32) -> usize {
    match ty {
        0x1400 | 0x1401 => 1,
        0x1402 | 0x1403 => 2,
        0x1404 | 0x1405 | 0x1406 => 4,
        _ => 0,
    }
}

/// The format a packed 16-bit type requires, 0 for the other types.
fn packed_format(ty: u32) -> u32 {
    match ty {
        0x8363 => 0x1907,
        0x8033 | 0x8034 => 0x1908,
        _ => 0,
    }
}

fn unpack16(ty: u32, v: u16) -> [i32; 4] {
    let v = v as i32;
    match ty {
        0x8363 => [
            (v >> 11) * 255 / 31,
            ((v >> 5) & 63) * 255 / 63,
            (v & 31) * 255 / 31,
            255,
        ],
        0x8034 => [
            (v >> 11) * 255 / 31,
            ((v >> 6) & 31) * 255 / 31,
            ((v >> 1) & 31) * 255 / 31,
            (v & 1) * 255,
        ],
        _ => [
            (v >> 12) * 17,
            ((v >> 8) & 15) * 17,
            ((v >> 4) & 15) * 17,
            (v & 15) * 17,
        ],
    }
}

fn pack16(ty: u32, c: [i32; 4]) -> u16 {
    (match ty {
        0x8363 => {
            ((c[0] * 31 + 127) / 255) << 11
                | ((c[1] * 63 + 127) / 255) << 5
                | (c[2] * 31 + 127) / 255
        }
        0x8034 => {
            ((c[0] * 31 + 127) / 255) << 11
                | ((c[1] * 31 + 127) / 255) << 6
                | ((c[2] * 31 + 127) / 255) << 1
                | (c[3] >= 128) as i32
        }
        _ => {
            ((c[0] * 15 + 127) / 255) << 12
                | ((c[1] * 15 + 127) / 255) << 8
                | ((c[2] * 15 + 127) / 255) << 4
                | (c[3] * 15 + 127) / 255
        }
    }) as u16
}

/// (components, element bytes, group bytes) of a color image, 0 element/group bytes for bitmaps;
/// the error of an invalid pair otherwise.
pub(crate) fn color_layout(format: u32, ty: u32) -> Result<(usize, usize, usize), u32> {
    let n = if format == COLOR_INDEX {
        1
    } else {
        components(format).ok_or(0x0500u32)?.len()
    };
    if ty == BITMAP {
        return if format == COLOR_INDEX {
            Ok((1, 0, 0))
        } else {
            Err(0x0500)
        };
    }
    let packed = packed_format(ty);
    if packed != 0 {
        return if format == packed {
            Ok((n, 2, 2))
        } else {
            Err(0x0502)
        };
    }
    if type_size(ty) == 0 {
        return Err(0x0500);
    }
    Ok((n, type_size(ty), n * type_size(ty)))
}

/// Bytes per row of an image `width` groups wide.
fn row_bytes(store: &PixelStore, width: usize, elem: usize, group: usize) -> usize {
    let bytes = store.row_length_or(width) * group;
    if elem < store.alignment {
        (bytes + store.alignment - 1) / store.alignment * store.alignment
    } else {
        bytes
    }
}

/// The bytes of one element, reversed with GL_*_SWAP_BYTES.
unsafe fn element<const N: usize>(src: *const u8, swap: bool) -> [u8; N] {
    core::array::from_fn(|i| src.add(if swap { N - 1 - i } else { i }).read())
}

unsafe fn load_raw(src: *const u8, ty: u32, swap: bool) -> f32 {
    match ty {
        0x1401 => src.read() as f32 / 255.,
        0x1400 => (2. * (src.read() as i8) as f32 + 1.) / 255.,
        0x1403 => u16::from_le_bytes(element(src, swap)) as f32 / 65535.,
        0x1402 => (2. * i16::from_le_bytes(element(src, swap)) as f32 + 1.) / 65535.,
        0x1405 => (u32::from_le_bytes(element(src, swap)) as f64 / 4294967295.) as f32,
        0x1404 => ((2. * i32::from_le_bytes(element(src, swap)) as f64 + 1.) / 4294967295.) as f32,
        _ => f32::from_le_bytes(element(src, swap)),
    }
}

unsafe fn load_clamped(src: *const u8, ty: u32, swap: bool) -> f32 {
    let v = load_raw(src, ty, swap);
    if !(v > 0.) {
        0.
    } else if v > 1. {
        1.
    } else {
        v
    }
}

unsafe fn load_index(src: *const u8, ty: u32, swap: bool) -> i64 {
    match ty {
        0x1401 => src.read() as i64,
        0x1400 => src.read() as i8 as i64,
        0x1403 => u16::from_le_bytes(element(src, swap)) as i64,
        0x1402 => i16::from_le_bytes(element(src, swap)) as i64,
        0x1405 => u32::from_le_bytes(element(src, swap)) as i64,
        0x1404 => i32::from_le_bytes(element(src, swap)) as i64,
        _ => {
            let f = f32::from_le_bytes(element(src, swap));
            if f > -2147483648. && f < 2147483648. {
                f as i64
            } else {
                0
            }
        }
    }
}

fn bitmap_bit(data: *const u8, store: &PixelStore, width: usize, x: usize, y: usize) -> bool {
    let bytes = (store.row_length_or(width) + 7) / 8;
    let bytes = (bytes + store.alignment - 1) / store.alignment * store.alignment;
    let bit = store.skip_pixels + x;
    let b = unsafe { data.add((store.skip_rows + y) * bytes + bit / 8).read() };
    (if store.lsb_first {
        b >> (bit & 7)
    } else {
        b >> (7 - (bit & 7))
    }) & 1
        != 0
}

impl Pixels {
    fn map_entry(&self, map: usize, index: i64) -> f32 {
        let m = &self.maps[map];
        m[(index & (m.len() as i64 - 1)) as usize]
    }

    fn shift_index(&self, index: i64) -> i64 {
        let t = &self.transfer;
        let shifted = if t.index_shift > 0 {
            if t.index_shift < 64 {
                ((index as u64) << t.index_shift) as i64
            } else {
                0
            }
        } else if t.index_shift < 0 {
            if t.index_shift > -64 {
                index >> -t.index_shift
            } else if index < 0 {
                -1
            } else {
                0
            }
        } else {
            index
        };
        shifted.wrapping_add(t.index_offset as i64)
    }

    /// A color index to RGBA through the I_TO_R .. I_TO_A tables.
    fn index_color(&self, index: i64) -> [f32; 4] {
        let index = self.shift_index(index);
        core::array::from_fn(|i| self.map_entry(I_TO_R + i, index))
    }

    /// RGBA scaled and biased, clamped to [0, 1], then looked up in R_TO_R .. A_TO_A with GL_MAP_COLOR.
    fn transfer_color(&self, c: &mut [f32; 4]) {
        let t = &self.transfer;
        for i in 0..4 {
            let mut v = c[i] * t.scale[i] + t.bias[i];
            v = if !(v > 0.) {
                0.
            } else if v > 1. {
                1.
            } else {
                v
            };
            if t.map_color {
                let m = &self.maps[R_TO_R + i];
                v = m[unsafe { lroundf(v * (m.len() - 1) as f32) } as usize];
            }
            c[i] = v;
        }
    }

    /// A color image as w x h RGBA8 texels, rows from the bottom (GL 1.1 section 3.6.3: missing
    /// color components are 0, a missing alpha 1, luminance goes to R, G and B).
    pub(crate) unsafe fn unpack_color(
        &self,
        store: &PixelStore,
        pixels: *const u8,
        w: usize,
        h: usize,
        format: u32,
        ty: u32,
    ) -> Result<Vec<u8>, u32> {
        let (n, elem, group) = color_layout(format, ty)?;
        let comp = components(format).unwrap_or(&[0]);
        let row = row_bytes(store, w, elem, group);
        let base = pixels.add(store.skip_rows * row + store.skip_pixels * group);
        let packed = packed_format(ty) != 0;
        let swap = store.swap_bytes && elem > 1;
        let index = format == COLOR_INDEX;
        let transfer = self.transfer.color_active();
        let mut out = Vec::new();
        out.try_reserve_exact(w * h * 4).map_err(|_| 0x0505u32)?;
        out.resize(w * h * 4, 0);
        for y in 0..h {
            for x in 0..w {
                let src = base.add(y * row + x * group);
                let dst = &mut out[(y * w + x) * 4..][..4];
                if index || transfer {
                    let mut c = [0., 0., 0., 1.];
                    if index {
                        let i = if ty == BITMAP {
                            bitmap_bit(pixels, store, w, x, y) as i64
                        } else {
                            load_index(src, ty, swap)
                        };
                        c = self.index_color(i);
                    } else {
                        if packed {
                            let v = unpack16(ty, u16::from_le_bytes(element(src, swap)));
                            c = v.map(|v| v as f32 / 255.);
                        } else {
                            for (i, k) in comp.iter().enumerate() {
                                let v = load_raw(src.add(i * elem), ty, swap);
                                if *k == 4 {
                                    c[0] = v;
                                    c[1] = v;
                                    c[2] = v;
                                } else {
                                    c[*k] = v;
                                }
                            }
                        }
                        self.transfer_color(&mut c);
                    }
                    for k in 0..4 {
                        dst[k] = gpu_color_component(c[k]) as u8;
                    }
                    continue;
                }
                if packed {
                    let v = unpack16(ty, u16::from_le_bytes(element(src, swap)));
                    for k in 0..4 {
                        dst[k] = v[k] as u8;
                    }
                    continue;
                }
                dst[3] = 255;
                for (i, k) in comp.iter().enumerate().take(n) {
                    let c = if ty == 0x1401 {
                        src.add(i).read()
                    } else {
                        (load_clamped(src.add(i * elem), ty, swap) * 255. + 0.5) as u8
                    };
                    if *k == 4 {
                        dst[0] = c;
                        dst[1] = c;
                        dst[2] = c;
                    } else {
                        dst[*k] = c;
                    }
                }
            }
        }
        Ok(out)
    }
}

// ---------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------
/// Columns (rows) [i0, i1) of an n pixels wide (high) rectangle at window position p with zoom z
/// that land on a screen `size` pixels wide (high).
fn visible_range(p: f32, z: f32, n: usize, size: usize) -> Option<(usize, usize)> {
    if z == 0. || n == 0 {
        return None;
    }
    let (a, b) = (-p / z, (size as f32 - p) / z);
    let (lo, hi) = (a.min(b), a.max(b));
    let n_f = n as f32;
    let i0 = if !(lo > 0.) {
        0
    } else if lo >= n_f {
        n
    } else {
        unsafe { floorf(lo) as usize }
    };
    let i1 = if !(hi < n_f) {
        n
    } else if hi <= 0. {
        0
    } else {
        unsafe { ceilf(hi) as usize }
    };
    (i0 < i1).then_some((i0, i1))
}

fn pow2(v: usize) -> usize {
    let mut p = 8;
    while p < v {
        p <<= 1;
    }
    p
}

/// A w x h pixel rectangle at window position (x, y) with zoom (zx, zy): the visible part in
/// tiles of up to TILE x TILE pixels, each a texture on a quad over the zoomed pixels. `fill`
/// writes the texels of the tile at (tx, ty) of size (tw, th) into a texture of the padded size.
unsafe fn draw_rect(
    r: &mut Renderer,
    mode: PixelMode,
    (w, h): (usize, usize),
    (x, y): (f32, f32),
    (zx, zy): (f32, f32),
    fill: &mut dyn FnMut(*mut u8, (usize, usize), (usize, usize), (usize, usize)),
) -> Result<(), u32> {
    let (Some((i0, i1)), Some((j0, j1))) = (
        visible_range(x, zx, w, WIDTH),
        visible_range(y, zy, h, HEIGHT),
    ) else {
        return Ok(());
    };
    let format = if mode == PixelMode::Image {
        PixelFormat::Rgba8
    } else {
        PixelFormat::A8
    };
    let mut ty = j0;
    while ty < j1 {
        let mut tx = i0;
        while tx < i1 {
            let tw = (i1 - tx).min(TILE);
            let th = (j1 - ty).min(TILE);
            let (texw, texh) = (pow2(tw), pow2(th));
            let x0 = x + zx * tx as f32;
            let x1 = x + zx * (tx + tw) as f32;
            let y0 = y + zy * ty as f32;
            let y1 = y + zy * (ty + th) as f32;
            let (s1, t1) = (tw as f32 / texw as f32, th as f32 / texh as f32);
            draw_pixel_quad(
                r,
                PixelQuad {
                    mode,
                    format,
                    size: (texw, texh),
                    fill: &mut |p| fill(p, (texw, texh), (tx, ty), (tw, th)),
                    wx: [x0, x1, x1, x0],
                    wy: [y0, y0, y1, y1],
                    s: [0., s1, s1, 0.],
                    t: [0., 0., t1, t1],
                    user_state: true,
                },
            )?;
            tx += TILE;
        }
        ty += TILE;
    }
    Ok(())
}

/// Draw a w x h RGBA8 image (rows from the bottom) at the raster position with the pixel zoom.
unsafe fn draw_rgba(r: &mut Renderer, rgba: &[u8], w: usize, h: usize) -> Result<(), u32> {
    let (x, y) = (r.frame.pixels.raster.pos[0], r.frame.pixels.raster.pos[1]);
    let zoom = (r.frame.pixels.zoom[0], r.frame.pixels.zoom[1]);
    draw_rect(r, PixelMode::Image, (w, h), (x, y), zoom, &mut |tex,
                                                               (
        texw,
        texh,
    ),
                                                               (
        tx,
        ty,
    ),
                                                               (
        tw,
        th,
    )| {
        for yy in 0..th {
            for xx in 0..tw {
                let p = &rgba[((ty + yy) * w + tx + xx) * 4..][..4];
                // PICA RGBA8 is ABGR.
                let d = tex.add(offset_bpp(texw, texh, xx, yy, 4));
                d.write(p[3]);
                d.add(1).write(p[2]);
                d.add(2).write(p[1]);
                d.add(3).write(p[0]);
            }
        }
    })
}

fn compare(func: u32, a: u32, b: u32) -> bool {
    match func {
        0x0200 => false,
        0x0201 => a < b,
        0x0202 => a == b,
        0x0203 => a <= b,
        0x0204 => a > b,
        0x0205 => a != b,
        0x0206 => a >= b,
        _ => true,
    }
}

// ---------------------------------------------------------------------------------------------
// Raster position
// ---------------------------------------------------------------------------------------------
unsafe fn set_raster_pos(v: [f32; 4]) {
    if crate::lists::record(crate::lists::Cmd::RasterPos(v)) {
        return;
    }
    with_renderer(|r| {
        let [x, y, z, w] = v;
        let mv = r.matrices.modelview();
        let proj = r.matrices.projection();
        let eye: [f32; 4] =
            core::array::from_fn(|i| mv[i] * x + mv[4 + i] * y + mv[8 + i] * z + mv[12 + i] * w);
        let clip: [f32; 4] = core::array::from_fn(|i| {
            proj[i] * eye[0] + proj[4 + i] * eye[1] + proj[8 + i] * eye[2] + proj[12 + i] * eye[3]
        });
        let mut inside = clip[3] > 0.;
        for i in 0..3 {
            if inside && clip[i].abs() > clip[3] {
                inside = false;
            }
        }
        let planes = r.frame.clip;
        for i in 0..crate::clip::MAX_PLANES {
            let p = planes.planes[i];
            if inside
                && planes.enabled >> i & 1 != 0
                && p[0] * eye[0] + p[1] * eye[1] + p[2] * eye[2] + p[3] * eye[3] < 0.
            {
                inside = false;
            }
        }
        if !inside {
            r.frame.pixels.raster.valid = false;
            return;
        }

        let (vp, range) = (r.frame.viewport, r.frame.raster.range);
        let (n, f) = (range[0], range[1]);
        let mut pos = [
            vp[0] as f32 + 0.5 * (clip[0] / clip[3] + 1.) * vp[2] as f32,
            vp[1] as f32 + 0.5 * (clip[1] / clip[3] + 1.) * vp[3] as f32,
            n + 0.5 * (clip[2] / clip[3] + 1.) * (f - n),
            clip[3],
        ];
        // Float matrices put integer positions slightly off (glOrtho(0, 400, ...) maps x = 210 to
        // 209.99998), which glBitmap's floor(x - xorig) would move by a whole pixel: snap what
        // is within 1/1024 of an integer.
        for p in pos.iter_mut().take(2) {
            let rounded = roundf(*p);
            if (*p - rounded).abs() < 1. / 1024. {
                *p = rounded;
            }
        }
        if r.frame.select.mode == crate::feedback::SELECT {
            r.frame.select.hit(pos[2]);
        }
        let ew = if eye[3] != 0. { eye[3] } else { 1. };
        let distance =
            crate::lines::sqrt(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]) / ew.abs();

        // The current color, lit like a vertex.
        let mut color = r.immediate.color.map(|c| gpu_color_component(c) as u8);
        if r.frame.light.enabled {
            let iw = if w != 0. { 1. / w } else { 1. };
            let (front, _) = r.frame.light.light_vertex(
                &mv,
                [x * iw, y * iw, z * iw, 1.],
                r.immediate.normal,
                color,
            );
            color = front;
        }
        let tm = r.matrices.texture_for(0);
        let tc = r.immediate.texcoord;
        let tex: [f32; 4] = core::array::from_fn(|i| {
            tm[i] * tc[0] + tm[4 + i] * tc[1] + tm[8 + i] * tc[2] + tm[12 + i] * tc[3]
        });
        r.frame.pixels.raster = RasterPos {
            valid: true,
            pos,
            color,
            tex,
            distance,
        };
    });
}

macro_rules! raster_pos {
    ($name:ident($($a:ident: $t:ty),*) => $e:expr) => {
        #[no_mangle]
        pub unsafe extern "C" fn $name($($a: $t),*) {
            set_raster_pos($e)
        }
    };
}
macro_rules! raster_pos_v {
    ($name:ident($t:ty) => |$v:ident| $e:expr) => {
        #[no_mangle]
        pub unsafe extern "C" fn $name($v: *const $t) {
            set_raster_pos($e)
        }
    };
}

raster_pos!(glRasterPos4f(x: f32, y: f32, z: f32, w: f32) => [x, y, z, w]);
raster_pos!(glRasterPos2d(x: f64, y: f64) => [x as f32, y as f32, 0., 1.]);
raster_pos!(glRasterPos2f(x: f32, y: f32) => [x, y, 0., 1.]);
raster_pos!(glRasterPos2i(x: i32, y: i32) => [x as f32, y as f32, 0., 1.]);
raster_pos!(glRasterPos2s(x: i16, y: i16) => [x as f32, y as f32, 0., 1.]);
raster_pos!(glRasterPos3d(x: f64, y: f64, z: f64) => [x as f32, y as f32, z as f32, 1.]);
raster_pos!(glRasterPos3f(x: f32, y: f32, z: f32) => [x, y, z, 1.]);
raster_pos!(glRasterPos3i(x: i32, y: i32, z: i32) => [x as f32, y as f32, z as f32, 1.]);
raster_pos!(glRasterPos3s(x: i16, y: i16, z: i16) => [x as f32, y as f32, z as f32, 1.]);
raster_pos!(glRasterPos4d(x: f64, y: f64, z: f64, w: f64) => [x as f32, y as f32, z as f32, w as f32]);
raster_pos!(glRasterPos4i(x: i32, y: i32, z: i32, w: i32) => [x as f32, y as f32, z as f32, w as f32]);
raster_pos!(glRasterPos4s(x: i16, y: i16, z: i16, w: i16) => [x as f32, y as f32, z as f32, w as f32]);
raster_pos_v!(glRasterPos2dv(f64) => |v| [*v as f32, *v.add(1) as f32, 0., 1.]);
raster_pos_v!(glRasterPos2fv(f32) => |v| [*v, *v.add(1), 0., 1.]);
raster_pos_v!(glRasterPos2iv(i32) => |v| [*v as f32, *v.add(1) as f32, 0., 1.]);
raster_pos_v!(glRasterPos2sv(i16) => |v| [*v as f32, *v.add(1) as f32, 0., 1.]);
raster_pos_v!(glRasterPos3dv(f64) => |v| [*v as f32, *v.add(1) as f32, *v.add(2) as f32, 1.]);
raster_pos_v!(glRasterPos3fv(f32) => |v| [*v, *v.add(1), *v.add(2), 1.]);
raster_pos_v!(glRasterPos3iv(i32) => |v| [*v as f32, *v.add(1) as f32, *v.add(2) as f32, 1.]);
raster_pos_v!(glRasterPos3sv(i16) => |v| [*v as f32, *v.add(1) as f32, *v.add(2) as f32, 1.]);
raster_pos_v!(glRasterPos4dv(f64) => |v| [*v as f32, *v.add(1) as f32, *v.add(2) as f32, *v.add(3) as f32]);
raster_pos_v!(glRasterPos4fv(f32) => |v| [*v, *v.add(1), *v.add(2), *v.add(3)]);
raster_pos_v!(glRasterPos4iv(i32) => |v| [*v as f32, *v.add(1) as f32, *v.add(2) as f32, *v.add(3) as f32]);
raster_pos_v!(glRasterPos4sv(i16) => |v| [*v as f32, *v.add(1) as f32, *v.add(2) as f32, *v.add(3) as f32]);

// ---------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------
#[no_mangle]
pub unsafe extern "C" fn glPixelZoom(x: f32, y: f32) {
    if crate::lists::record(crate::lists::Cmd::PixelZoom(x, y)) {
        return;
    }
    crate::renderer::with_frame(|s, _| s.pixels.zoom = [x, y]);
}

#[no_mangle]
pub unsafe extern "C" fn glPixelTransferf(pname: u32, param: f32) {
    if crate::lists::record(crate::lists::Cmd::PixelTransfer(pname, param)) {
        return;
    }
    crate::renderer::with_frame(|s, m| {
        let t = &mut s.pixels.transfer;
        if let Some((i, bias)) = transfer_component(pname) {
            if bias {
                t.bias[i] = param;
            } else {
                t.scale[i] = param;
            }
            return;
        }
        match pname {
            0x0d10 => t.map_color = param != 0.,
            0x0d11 => t.map_stencil = param != 0.,
            0x0d12 => t.index_shift = unsafe { lroundf(param) },
            0x0d13 => t.index_offset = unsafe { lroundf(param) },
            _ => m.fail(0x0500),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glPixelTransferi(pname: u32, param: i32) {
    glPixelTransferf(pname, param as f32);
}

/// Set a pixel map from float values. Tables looked up by index (I_TO_*, S_TO_S) need 2^n
/// entries; color components are clamped to [0, 1].
pub(crate) unsafe fn set_pixel_map(map: u32, size: i32, values: &[f32]) {
    crate::renderer::with_frame(|s, m| {
        if !(0x0c70..=0x0c79).contains(&map) {
            m.fail(0x0500);
            return;
        }
        let i = (map - 0x0c70) as usize;
        let power = i < R_TO_R;
        if !(1..=256).contains(&size)
            || (power && size & (size - 1) != 0)
            || values.len() < size as usize
        {
            m.fail(0x0501);
            return;
        }
        s.pixels.maps[i] = values[..size as usize]
            .iter()
            .map(|v| {
                if i < I_TO_R || *v > 0. && *v <= 1. {
                    *v
                } else if !(*v > 0.) {
                    0.
                } else {
                    1.
                }
            })
            .collect();
    });
}

unsafe fn pixel_map(map: u32, size: i32, values: *const u8, ty: u32) {
    let index = (0x0c70..=0x0c79).contains(&map) && ((map - 0x0c70) as usize) < I_TO_R;
    let count = if (1..=256).contains(&size) && !values.is_null() {
        size as usize
    } else {
        0
    };
    let mut v = Vec::new();
    if v.try_reserve_exact(count).is_err() {
        crate::renderer::with_frame(|_, m| m.fail(0x0505));
        return;
    }
    for i in 0..count {
        v.push(match ty {
            0x1406 => (values as *const f32).add(i).read_unaligned(),
            0x1405 => {
                let u = (values as *const u32).add(i).read_unaligned();
                if index {
                    u as f32
                } else {
                    (u as f64 / 4294967295.) as f32
                }
            }
            _ => {
                let u = (values as *const u16).add(i).read_unaligned();
                if index {
                    u as f32
                } else {
                    u as f32 / 65535.
                }
            }
        });
    }
    if crate::lists::record(crate::lists::Cmd::PixelMap(map, size, v.clone())) {
        return;
    }
    set_pixel_map(map, size, &v);
}

#[no_mangle]
pub unsafe extern "C" fn glPixelMapfv(map: u32, size: i32, values: *const f32) {
    pixel_map(map, size, values.cast(), 0x1406);
}
#[no_mangle]
pub unsafe extern "C" fn glPixelMapuiv(map: u32, size: i32, values: *const u32) {
    pixel_map(map, size, values.cast(), 0x1405);
}
#[no_mangle]
pub unsafe extern "C" fn glPixelMapusv(map: u32, size: i32, values: *const u16) {
    pixel_map(map, size, values.cast(), 0x1403);
}

// ---------------------------------------------------------------------------------------------
// glBitmap, glDrawPixels, glCopyPixels, glReadPixels
// ---------------------------------------------------------------------------------------------
/// The bitmap tightly packed (rows of whole bytes, most significant bit first).
unsafe fn pack_bitmap(data: *const u8, store: &PixelStore, w: usize, h: usize) -> Option<Vec<u8>> {
    let bytes = (w + 7) / 8;
    let mut out = Vec::new();
    out.try_reserve_exact(bytes * h).ok()?;
    out.resize(bytes * h, 0);
    for y in 0..h {
        for x in 0..w {
            if bitmap_bit(data, store, w, x, y) {
                out[y * bytes + x / 8] |= 0x80 >> (x & 7);
            }
        }
    }
    Some(out)
}

#[no_mangle]
pub unsafe extern "C" fn glBitmap(
    width: i32,
    height: i32,
    xorig: f32,
    yorig: f32,
    xmove: f32,
    ymove: f32,
    bitmap: *const u8,
) {
    if crate::lists::compiling() {
        let mut store = PixelStore::default();
        crate::renderer::with_immediate(|r| store = r.frame.textures.unpack());
        let bits = if bitmap.is_null() || width <= 0 || height <= 0 {
            None
        } else {
            pack_bitmap(bitmap, &store, width as usize, height as usize)
        };
        let cmd = crate::lists::Cmd::Bitmap {
            size: [width, height],
            motion: [xorig, yorig, xmove, ymove],
            bits,
        };
        if crate::lists::record(cmd) {
            return;
        }
    }
    with_renderer(|r| {
        if width < 0 || height < 0 {
            r.matrices.fail(0x0501);
            return;
        }
        if !r.frame.pixels.raster.valid {
            return;
        }
        if r.frame.select.mode != crate::feedback::RENDER {
            raster_token(r, 0x0704);
            r.frame.pixels.raster.pos[0] += xmove;
            r.frame.pixels.raster.pos[1] += ymove;
            return;
        }
        // The fragments have the raster color: the alpha test is decided here, the bitmap modes
        // discard where no bit is set.
        let alpha = r.frame.pixels.raster.color[3];
        let s = &r.frame.raster;
        if width > 0
            && height > 0
            && !bitmap.is_null()
            && (!s.alpha || compare(s.alpha_func, alpha as u32, s.alpha_ref as u32))
        {
            let mut store = PixelStore::default();
            store = r.frame.textures.unpack();
            let (w, h) = (width as usize, height as usize);
            let pos = r.frame.pixels.raster.pos;
            let (x, y) = (floorf(pos[0] - xorig), floorf(pos[1] - yorig));
            let mode = if alpha != 0 {
                PixelMode::Bitmap
            } else {
                PixelMode::BitmapZero
            };
            let result = draw_rect(
                r,
                mode,
                (w, h),
                (x, y),
                (1., 1.),
                &mut |tex, (texw, texh), (tx, ty), (tw, th)| {
                    for yy in 0..th {
                        for xx in 0..tw {
                            let set = bitmap_bit(bitmap, &store, w, tx + xx, ty + yy);
                            tex.add(offset_bpp(texw, texh, xx, yy, 1)).write(if set {
                                255
                            } else {
                                0
                            });
                        }
                    }
                },
            );
            if let Err(e) = result {
                r.matrices.fail(e);
            }
        }
        r.frame.pixels.raster.pos[0] += xmove;
        r.frame.pixels.raster.pos[1] += ymove;
    });
}

/// A feedback token with the raster position (glBitmap, glDrawPixels, glCopyPixels).
unsafe fn raster_token(r: &mut Renderer, token: u32) {
    let p = r.frame.pixels.raster;
    let color = p.color.map(|c| c as f32 * (1. / 255.));
    crate::feedback::raster_token(&mut r.frame.select, token, p.pos, color, p.tex);
}

#[no_mangle]
pub unsafe extern "C" fn glDrawPixels(
    width: i32,
    height: i32,
    format: u32,
    ty: u32,
    pixels: *const u8,
) {
    if crate::lists::compiling() {
        let mut store = PixelStore::default();
        crate::renderer::with_immediate(|r| store = r.frame.textures.unpack());
        let data = capture_image(&store, width, height, format, ty, pixels);
        let cmd = crate::lists::Cmd::DrawPixels {
            size: [width, height],
            format,
            ty,
            data,
            swap: store.swap_bytes,
        };
        if crate::lists::record(cmd) {
            return;
        }
    }
    with_renderer(|r| {
        if width < 0 || height < 0 {
            r.matrices.fail(0x0501);
            return;
        }
        let depth_stencil = format == 0x1902 || format == 0x1901;
        if !depth_stencil {
            if let Err(e) = color_layout(format, ty) {
                r.matrices.fail(e);
                return;
            }
        } else if ty == BITMAP {
            // Only a stencil image can be a bitmap.
            if format != 0x1901 {
                r.matrices.fail(0x0500);
                return;
            }
        } else if type_size(ty) == 0 || packed_format(ty) != 0 {
            r.matrices.fail(if packed_format(ty) != 0 {
                0x0502
            } else {
                0x0500
            });
            return;
        }
        if r.frame.select.mode != crate::feedback::RENDER {
            raster_token(r, 0x0705);
            return;
        }
        let (w, h) = (width as usize, height as usize);
        if !r.frame.pixels.raster.valid || w == 0 || h == 0 || pixels.is_null() {
            return;
        }
        if depth_stencil {
            // PICA cannot output a per-pixel depth: the image is written on the CPU.
            let stencil = format == 0x1901;
            if !depth_stencil_writes(r, stencil) {
                return;
            }
            let store = r.frame.textures.unpack();
            let result = depth_stencil_image(r, &store, pixels, w, h, stencil, ty)
                .and_then(|values| draw_depth_stencil(r, &values, w, h, stencil));
            if let Err(e) = result {
                r.matrices.fail(e);
            }
            return;
        }
        let store = r.frame.textures.unpack();
        let rgba = match r
            .frame
            .pixels
            .unpack_color(&store, pixels, w, h, format, ty)
        {
            Ok(v) => v,
            Err(e) => {
                r.matrices.fail(e);
                return;
            }
        };
        if let Err(e) = draw_rgba(r, &rgba, w, h) {
            r.matrices.fail(e);
        }
    });
}

/// A color image copied tightly packed (rows without padding), for display lists.
pub(crate) unsafe fn capture_image(
    store: &PixelStore,
    width: i32,
    height: i32,
    format: u32,
    ty: u32,
    pixels: *const u8,
) -> Option<Vec<u8>> {
    if pixels.is_null() || width <= 0 || height <= 0 {
        return None;
    }
    let (w, h) = (width as usize, height as usize);
    if ty == BITMAP {
        return pack_bitmap(pixels, store, w, h);
    }
    let (_, elem, group) = color_layout(format, ty).ok()?;
    let row = row_bytes(store, w, elem, group);
    let base = pixels.add(store.skip_rows * row + store.skip_pixels * group);
    let mut out = Vec::new();
    out.try_reserve_exact(w * group * h).ok()?;
    for y in 0..h {
        out.extend_from_slice(core::slice::from_raw_parts(base.add(y * row), w * group));
    }
    Some(out)
}

/// A window rectangle as RGBA8 texels read like glReadPixels (pixel transfer included); pixels
/// outside the window are 0.
unsafe fn read_rgba(r: &mut Renderer, x: i32, y: i32, w: usize, h: usize) -> Result<Vec<u8>, u32> {
    let mut out = Vec::new();
    out.try_reserve_exact(w * h * 4).map_err(|_| 0x0505u32)?;
    out.resize(w * h * 4, 0);
    let (x0, y0) = (x.max(0), y.max(0));
    let x1 = (x + w as i32).min(WIDTH as i32);
    let y1 = (y + h as i32).min(HEIGHT as i32);
    if x0 >= x1 || y0 >= y1 {
        return Ok(out);
    }
    let (x0, y0, x1, y1) = (x0 as usize, y0 as usize, x1 as usize, y1 as usize);
    let line0 = x0 & !7;
    let fb = read_color(r, line0, ((x1 + 7) & !7) - line0)?;
    let transfer = r.frame.pixels.transfer.color_active();
    for wx in x0..x1 {
        for wy in y0..y1 {
            let p = fb.pointer.add((wx - line0) * HEIGHT * 4 + wy * 4);
            let (a, b, g, red) = (p.read(), p.add(1).read(), p.add(2).read(), p.add(3).read());
            let dst =
                &mut out[((wy as i32 - y) as usize * w + (wx as i32 - x) as usize) * 4..][..4];
            if transfer {
                let mut c = [a, b, g, red].map(|v| v as f32 / 255.);
                // Alpha, blue, green, red order -> red, green, blue, alpha.
                c.reverse();
                r.frame.pixels.transfer_color(&mut c);
                for k in 0..4 {
                    dst[k] = gpu_color_component(c[k]) as u8;
                }
            } else {
                dst.copy_from_slice(&[red, g, b, a]);
            }
        }
    }
    Ok(out)
}

/// A window rectangle as RGBA8 texels read like glReadPixels (glCopyTexImage); None with a GL
/// error when it cannot be read.
pub(crate) unsafe fn copy_pixels(x: i32, y: i32, w: usize, h: usize) -> Option<Vec<u8>> {
    let mut out = None;
    with_renderer(|r| match read_rgba(r, x, y, w, h) {
        Ok(v) => out = Some(v),
        Err(e) => r.matrices.fail(e),
    });
    out
}

/// Store RGBA8 texels (rows from the bottom) as a color image laid out like the pack state
/// describes (glGetTexImage).
pub(crate) unsafe fn pack_image(
    store: &PixelStore,
    rgba: &[u8],
    w: usize,
    h: usize,
    format: u32,
    ty: u32,
    pixels: *mut u8,
) -> Result<(), u32> {
    let (_, elem, group) = color_layout(format, ty)?;
    let row = row_bytes(store, w, elem, group);
    let base = pixels.add(store.skip_rows * row + store.skip_pixels * group);
    for y in 0..h {
        for x in 0..w {
            let c: [f32; 4] = core::array::from_fn(|k| rgba[(y * w + x) * 4 + k] as f32 / 255.);
            store_color(
                base.add(y * row + x * group),
                c,
                format,
                ty,
                store.swap_bytes && elem > 1,
            );
        }
    }
    Ok(())
}

#[no_mangle]
pub unsafe extern "C" fn glCopyPixels(x: i32, y: i32, width: i32, height: i32, ty: u32) {
    if crate::lists::record(crate::lists::Cmd::CopyPixels([x, y, width, height], ty)) {
        return;
    }
    with_renderer(|r| {
        if !matches!(ty, 0x1800..=0x1802) {
            r.matrices.fail(0x0500);
            return;
        }
        if width < 0 || height < 0 {
            r.matrices.fail(0x0501);
            return;
        }
        if r.frame.select.mode != crate::feedback::RENDER {
            raster_token(r, 0x0706);
            return;
        }
        if !r.frame.pixels.raster.valid || width == 0 || height == 0 {
            return;
        }
        let (w, h) = (width as usize, height as usize);
        if ty != 0x1800 {
            let stencil = ty == 0x1802;
            if !depth_stencil_writes(r, stencil) {
                return;
            }
            let result = copy_depth_stencil(r, x, y, w, h, stencil)
                .and_then(|values| draw_depth_stencil(r, &values, w, h, stencil));
            if let Err(e) = result {
                r.matrices.fail(e);
            }
            return;
        }
        let result = read_rgba(r, x, y, w, h).and_then(|rgba| draw_rgba(r, &rgba, w, h));
        if let Err(e) = result {
            r.matrices.fail(e);
        }
    });
}

/// Whether a depth (stencil) image changes the depth/stencil buffer at all. The fragments of a
/// depth image have the raster color, which goes through the alpha test; depth is only written
/// with the depth test on. Stencil images are written directly (scissor and write mask).
fn depth_stencil_writes(r: &Renderer, stencil: bool) -> bool {
    let st = &r.frame.raster;
    if stencil {
        return st.stencil_write_mask != 0;
    }
    if !st.depth && !st.stencil {
        return false;
    }
    if st.alpha
        && !compare(
            st.alpha_func,
            r.frame.pixels.raster.color[3] as u32,
            st.alpha_ref as u32,
        )
    {
        return false;
    }
    st.depth_mask || (st.stencil && st.stencil_write_mask != 0)
}

fn apply_stencil_op(op: u32, s: u8, reference: u8) -> u8 {
    match op {
        0 => 0,
        0x1e01 => reference,
        0x1e02 => s.saturating_add(1),
        0x1e03 => s.saturating_sub(1),
        0x150a => !s,
        0x8507 => s.wrapping_add(1),
        0x8508 => s.wrapping_sub(1),
        _ => s,
    }
}

/// The values of a depth (stored form (1 - window depth) * 0xFFFFFF) or stencil image of the client.
unsafe fn depth_stencil_image(
    r: &mut Renderer,
    store: &PixelStore,
    pixels: *const u8,
    w: usize,
    h: usize,
    stencil: bool,
    ty: u32,
) -> Result<Vec<u32>, u32> {
    let bitmap = ty == BITMAP;
    let (elem, group) = if bitmap {
        (0, 0)
    } else {
        (type_size(ty), type_size(ty))
    };
    let row = row_bytes(store, w, elem, group);
    let base = pixels.add(store.skip_rows * row + store.skip_pixels * group);
    let swap = store.swap_bytes && elem > 1;
    let mut values = Vec::new();
    values.try_reserve_exact(w * h).map_err(|_| 0x0505u32)?;
    for y in 0..h {
        for x in 0..w {
            let src = if bitmap {
                pixels
            } else {
                base.add(y * row + x * group)
            };
            values.push(if stencil {
                let i = if bitmap {
                    bitmap_bit(pixels, store, w, x, y) as i64
                } else {
                    load_index(src, ty, swap)
                };
                r.frame.pixels.transfer_stencil(i) as u32
            } else {
                ((1. - r.frame.pixels.transfer_depth(load_raw(src, ty, swap))) * 16777215.) as u32
            });
        }
    }
    Ok(values)
}

/// The depth/stencil rectangle (glCopyPixels) as the values of a depth or stencil image; pixels
/// outside the window are 0.
unsafe fn copy_depth_stencil(
    r: &mut Renderer,
    x: i32,
    y: i32,
    w: usize,
    h: usize,
    stencil: bool,
) -> Result<Vec<u32>, u32> {
    let mut values = Vec::new();
    values.try_reserve_exact(w * h).map_err(|_| 0x0505u32)?;
    values.resize(w * h, 0);
    let (x0, y0) = (x.max(0), y.max(0));
    let x1 = (x + w as i32).min(WIDTH as i32);
    let y1 = (y + h as i32).min(HEIGHT as i32);
    if x0 >= x1 || y0 >= y1 {
        return Ok(values);
    }
    let (x0, y0, x1, y1) = (x0 as usize, y0 as usize, x1 as usize, y1 as usize);
    let line0 = x0 & !7;
    let fb = crate::renderer::read_depth(r, line0, ((x1 + 7) & !7) - line0)?;
    for wx in x0..x1 {
        for wy in y0..y1 {
            let p = fb.pointer.add((wx - line0) * HEIGHT * 4 + wy * 4);
            let v = &mut values[(wy as i32 - y) as usize * w + (wx as i32 - x) as usize];
            if stencil {
                *v = r.frame.pixels.transfer_stencil(p.add(3).read() as i64) as u32;
            } else {
                *v = p.read() as u32
                    | (p.add(1).read() as u32) << 8
                    | (p.add(2).read() as u32) << 16;
                if r.frame.pixels.depth_transfer_active() {
                    let z = r.frame.pixels.transfer_depth(1. - *v as f32 / 16777215.);
                    *v = ((1. - z) * 16777215.) as u32;
                }
            }
        }
    }
    Ok(values)
}

/// A w x h depth or stencil image at the raster position with the pixel zoom, written on the CPU:
/// the buffer is read, changed by the scissor, stencil and depth test (fragment depth = the
/// image's) and the masks, and written back.
unsafe fn draw_depth_stencil(
    r: &mut Renderer,
    values: &[u32],
    w: usize,
    h: usize,
    stencil: bool,
) -> Result<(), u32> {
    let st = r.frame.raster;
    let pos = r.frame.pixels.raster.pos;
    let (zx, zy) = (r.frame.pixels.zoom[0], r.frame.pixels.zoom[1]);
    let (xr, yr) = (pos[0], pos[1]);
    let (Some((i0, i1)), Some((j0, j1))) = (
        visible_range(xr, zx, w, WIDTH),
        visible_range(yr, zy, h, HEIGHT),
    ) else {
        return Ok(());
    };
    // The window rectangle of the visible pixels, clipped to the screen and the scissor box.
    let (ax, bx) = (xr + zx * i0 as f32, xr + zx * i1 as f32);
    let (ay, by) = (yr + zy * j0 as f32, yr + zy * j1 as f32);
    let mut x0 = floorf(ax.min(bx)) as i32;
    let mut x1 = ceilf(ax.max(bx)) as i32;
    let mut y0 = floorf(ay.min(by)) as i32;
    let mut y1 = ceilf(ay.max(by)) as i32;
    x0 = x0.max(0);
    y0 = y0.max(0);
    x1 = x1.min(WIDTH as i32);
    y1 = y1.min(HEIGHT as i32);
    if st.scissor {
        let b = st.scissor_box;
        x0 = x0.max(b[0]);
        y0 = y0.max(b[1]);
        x1 = x1.min(b[0] + b[2]);
        y1 = y1.min(b[1] + b[3]);
    }
    if x0 >= x1 || y0 >= y1 {
        return Ok(());
    }
    let (x0, y0, x1, y1) = (x0 as usize, y0 as usize, x1 as usize, y1 as usize);
    let line0 = x0 & !7;
    let lines = ((x1 + 7) & !7) - line0;
    let fb = crate::renderer::read_depth(r, line0, lines)?;
    let (reference, func_mask, write_mask) =
        (st.stencil_ref, st.stencil_func_mask, st.stencil_write_mask);
    for wx in x0..x1 {
        // Pixel (i, j) covers the zoomed rectangle from (xr + zx*i, yr + zy*j): fragments at the
        // centers inside it.
        let fi = (wx as f32 + 0.5 - xr) / zx;
        if !(fi >= 0.) || !(fi < w as f32) {
            continue;
        }
        let i = fi as usize;
        for wy in y0..y1 {
            let fj = (wy as f32 + 0.5 - yr) / zy;
            if !(fj >= 0.) || !(fj < h as f32) {
                continue;
            }
            let v = values[fj as usize * w + i];
            let p = fb.pointer.add((wx - line0) * HEIGHT * 4 + wy * 4);
            let mut s = p.add(3).read();
            if stencil {
                p.add(3).write((s & !write_mask) | (v as u8 & write_mask));
                continue;
            }
            let mut d =
                p.read() as u32 | (p.add(1).read() as u32) << 8 | (p.add(2).read() as u32) << 16;
            let ns;
            if st.stencil
                && !compare(
                    st.stencil_func,
                    (reference & func_mask) as u32,
                    (s & func_mask) as u32,
                )
            {
                ns = apply_stencil_op(st.stencil_ops[0], s, reference);
            } else if !st.depth {
                ns = apply_stencil_op(st.stencil_ops[2], s, reference);
            } else if compare(st.function, d, v) {
                // The stored depth is reversed: z_frag < z_buffer <=> v > d.
                if st.depth_mask {
                    d = v;
                }
                ns = apply_stencil_op(st.stencil_ops[2], s, reference);
            } else {
                ns = apply_stencil_op(st.stencil_ops[1], s, reference);
            }
            if st.stencil {
                s = (s & !write_mask) | (ns & write_mask);
            }
            p.write(d as u8);
            p.add(1).write((d >> 8) as u8);
            p.add(2).write((d >> 16) as u8);
            p.add(3).write(s);
        }
    }
    crate::renderer::write_depth(r, line0, lines, &fb)
}

/// Store one color as a group of a color image: luminance is R + G + B, clamped.
unsafe fn store_color(dst: *mut u8, c: [f32; 4], format: u32, ty: u32, swap: bool) {
    let comp = components(format).unwrap_or(&[0]);
    let byte = |v: f32| gpu_color_component(v) as i32;
    if ty == 0x1401 || packed_format(ty) != 0 {
        let b = [byte(c[0]), byte(c[1]), byte(c[2]), byte(c[3])];
        let l = (b[0] + b[1] + b[2]).min(255);
        let v = |k: usize| if k == 4 { l } else { b[k] };
        if packed_format(ty) != 0 {
            let bytes = pack16(ty, b).to_le_bytes();
            dst.write(bytes[swap as usize]);
            dst.add(1).write(bytes[!swap as usize]);
        } else {
            for (i, k) in comp.iter().enumerate() {
                dst.add(i).write(v(*k) as u8);
            }
        }
        return;
    }
    let l = (c[0] + c[1] + c[2]).min(1.);
    for (i, k) in comp.iter().enumerate() {
        let v = if *k == 4 { l } else { c[*k] } as f64;
        store_element(dst.add(i * type_size(ty)), ty, v, swap);
    }
}

/// Store a normalized value in [0, 1] as an element of the type (GL 1.1 table 2.9 inverted).
unsafe fn store_element(dst: *mut u8, ty: u32, v: f64, swap: bool) {
    let round = |x: f64| {
        if x >= 0. {
            (x + 0.5) as i64
        } else {
            -((-x + 0.5) as i64)
        }
    };
    let bytes: [u8; 4] = match ty {
        0x1400 => [round((v * 255. - 1.) / 2.) as i8 as u8, 0, 0, 0],
        0x1402 => {
            let b = (round((v * 65535. - 1.) / 2.) as i16).to_le_bytes();
            [b[0], b[1], 0, 0]
        }
        0x1403 => {
            let b = (round(v * 65535.) as u16).to_le_bytes();
            [b[0], b[1], 0, 0]
        }
        0x1404 => (round((v * 4294967295. - 1.) / 2.) as i32).to_le_bytes(),
        0x1405 => (round(v * 4294967295.) as u32).to_le_bytes(),
        _ => (v as f32).to_le_bytes(),
    };
    let n = type_size(ty);
    for i in 0..n {
        dst.add(i).write(bytes[if swap { n - 1 - i } else { i }]);
    }
}

impl Pixels {
    /// A stencil index: shifted and offset, then looked up in S_TO_S with GL_MAP_STENCIL.
    fn transfer_stencil(&self, s: i64) -> i64 {
        let mut s = self.shift_index(s);
        if self.transfer.map_stencil {
            let v = self.map_entry(S_TO_S, s);
            s = unsafe { lroundf(v) } as i64;
        }
        s
    }

    fn depth_transfer_active(&self) -> bool {
        self.transfer.scale[4] != 1. || self.transfer.bias[4] != 0.
    }

    /// A window depth scaled and biased, clamped to [0, 1].
    fn transfer_depth(&self, d: f32) -> f32 {
        let d = d * self.transfer.scale[4] + self.transfer.bias[4];
        if !(d > 0.) {
            0.
        } else if d > 1. {
            1.
        } else {
            d
        }
    }
}

/// Store an index as one element of the type, masked to the bits of table 4.6; GL_FLOAT takes it
/// as it is.
unsafe fn store_index(dst: *mut u8, ty: u32, index: i64, swap: bool) {
    let masked = match ty {
        0x1401 => index & 0xff,
        0x1400 => index & 0x7f,
        0x1403 => index & 0xffff,
        0x1402 => index & 0x7fff,
        0x1405 => index & 0xffff_ffff,
        0x1404 => index & 0x7fff_ffff,
        _ => index,
    };
    let bytes: [u8; 4] = match ty {
        0x1401 | 0x1400 => [masked as u8, 0, 0, 0],
        0x1403 | 0x1402 => {
            let b = (masked as u16).to_le_bytes();
            [b[0], b[1], 0, 0]
        }
        0x1405 | 0x1404 => (masked as u32).to_le_bytes(),
        _ => (masked as f32).to_le_bytes(),
    };
    let n = type_size(ty);
    for i in 0..n {
        dst.add(i).write(bytes[if swap { n - 1 - i } else { i }]);
    }
}

/// glReadPixels of depth and stencil: the depth/stencil buffer read through a display transfer.
unsafe fn read_depth_stencil(
    r: &mut Renderer,
    (x, y, w, h): (i32, i32, usize, usize),
    stencil: bool,
    ty: u32,
    pixels: *mut u8,
) -> Result<(), u32> {
    let (x0, y0) = (x.max(0), y.max(0));
    let x1 = (x + w as i32).min(WIDTH as i32);
    let y1 = (y + h as i32).min(HEIGHT as i32);
    if x0 >= x1 || y0 >= y1 {
        return Ok(());
    }
    let (x0, y0, x1, y1) = (x0 as usize, y0 as usize, x1 as usize, y1 as usize);
    let line0 = x0 & !7;
    let fb = crate::renderer::read_depth(r, line0, ((x1 + 7) & !7) - line0)?;
    let store = r.frame.textures.pack();
    let bitmap = ty == BITMAP;
    let (elem, group) = if bitmap {
        (1, 1)
    } else {
        (type_size(ty), type_size(ty))
    };
    let row = if bitmap {
        let b = (store.row_length_or(w) + 7) / 8;
        (b + store.alignment - 1) / store.alignment * store.alignment
    } else {
        row_bytes(&store, w, elem, group)
    };
    let base = pixels.add(store.skip_rows * row);
    let swap = store.swap_bytes && elem > 1;
    for wy in y0..y1 {
        let line = base.add((wy as i32 - y) as usize * row);
        for wx in x0..x1 {
            let p = fb.pointer.add((wx - line0) * HEIGHT * 4 + wy * 4);
            let col = store.skip_pixels + (wx as i32 - x) as usize;
            let s = p.add(3).read();
            if bitmap {
                // Stencil bit 0, most significant bit first unless GL_PACK_LSB_FIRST.
                let bit = if store.lsb_first {
                    1u8 << (col & 7)
                } else {
                    0x80u8 >> (col & 7)
                };
                let at = line.add(col / 8);
                if r.frame.pixels.transfer_stencil(s as i64) & 1 != 0 {
                    at.write(at.read() | bit);
                } else {
                    at.write(at.read() & !bit);
                }
                continue;
            }
            let dst = line.add(col * group);
            if stencil {
                store_index(dst, ty, r.frame.pixels.transfer_stencil(s as i64), swap);
            } else {
                let d = p.read() as u32
                    | (p.add(1).read() as u32) << 8
                    | (p.add(2).read() as u32) << 16;
                let mut z = 1. - d as f64 / 16777215.;
                if r.frame.pixels.depth_transfer_active() {
                    z = r.frame.pixels.transfer_depth(z as f32) as f64;
                }
                store_element(dst, ty, z, swap);
            }
        }
    }
    Ok(())
}

#[no_mangle]
pub unsafe extern "C" fn glReadPixels(
    x: i32,
    y: i32,
    width: i32,
    height: i32,
    format: u32,
    ty: u32,
    pixels: *mut u8,
) {
    with_renderer(|r| {
        if width < 0 || height < 0 {
            r.matrices.fail(0x0501);
            return;
        }
        let depth = format == 0x1902;
        let stencil = format == 0x1901;
        let comp: &[usize] = if depth || stencil {
            &[0]
        } else if format == COLOR_INDEX {
            // The framebuffer is RGBA only.
            r.matrices.fail(0x0502);
            return;
        } else if let Some(c) = components(format) {
            c
        } else {
            r.matrices.fail(0x0500);
            return;
        };
        let packed = packed_format(ty);
        let bitmap = ty == BITMAP;
        if packed == 0 && !bitmap && type_size(ty) == 0 {
            r.matrices.fail(0x0500);
            return;
        }
        if bitmap && !stencil {
            r.matrices.fail(0x0500);
            return;
        }
        if packed != 0 && format != packed {
            r.matrices.fail(0x0502);
            return;
        }
        let (w, h) = (width as usize, height as usize);
        if pixels.is_null() || w == 0 || h == 0 {
            return;
        }
        if depth || stencil {
            if let Err(e) = read_depth_stencil(r, (x, y, w, h), stencil, ty, pixels) {
                r.matrices.fail(e);
            }
            return;
        }
        let rgba = match read_rgba(r, x, y, w, h) {
            Ok(v) => v,
            Err(e) => {
                r.matrices.fail(e);
                return;
            }
        };
        let store = r.frame.textures.pack();
        let (elem, group) = if packed != 0 {
            (2, 2)
        } else {
            (type_size(ty), comp.len() * type_size(ty))
        };
        let row = row_bytes(&store, w, elem, group);
        let base = pixels.add(store.skip_rows * row + store.skip_pixels * group);
        // Pixels outside the window are left untouched.
        let (x0, y0) = (x.max(0), y.max(0));
        let x1 = (x + width).min(WIDTH as i32);
        let y1 = (y + height).min(HEIGHT as i32);
        for wy in y0..y1.max(y0) {
            for wx in x0..x1.max(x0) {
                let src = &rgba[(((wy - y) as usize) * w + (wx - x) as usize) * 4..][..4];
                let c: [f32; 4] = core::array::from_fn(|k| src[k] as f32 / 255.);
                let dst = base.add((wy - y) as usize * row + (wx - x) as usize * group);
                store_color(dst, c, format, ty, store.swap_bytes && elem > 1);
            }
        }
    });
}

/// glGetPixelMap{fv,uiv,usv}: color components scaled to the integer range, indices rounded.
unsafe fn get_pixel_map(map: u32, out: *mut u8, ty: u32) {
    if out.is_null() {
        return with_renderer(|r| r.matrices.fail(0x0501));
    }
    with_renderer(|r| {
        if !(0x0c70..=0x0c79).contains(&map) {
            r.matrices.fail(0x0500);
            return;
        }
        let m = (map - 0x0c70) as usize;
        let color = m >= I_TO_R;
        for (i, v) in r.frame.pixels.maps[m].iter().enumerate() {
            let v = *v as f64;
            let round = |x: f64| {
                if x >= 0. {
                    (x + 0.5) as i64
                } else {
                    -((-x + 0.5) as i64)
                }
            };
            match ty {
                0x1406 => (out as *mut f32).add(i).write_unaligned(v as f32),
                0x1405 => (out as *mut u32).add(i).write_unaligned(round(if color {
                    v * 4294967295.
                } else {
                    v
                }) as u32),
                _ => (out as *mut u16).add(i).write_unaligned(round(if color {
                    v * 65535.
                } else {
                    v
                }) as u16),
            }
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glGetPixelMapfv(map: u32, values: *mut f32) {
    get_pixel_map(map, values.cast(), 0x1406);
}
#[no_mangle]
pub unsafe extern "C" fn glGetPixelMapuiv(map: u32, values: *mut u32) {
    get_pixel_map(map, values.cast(), 0x1405);
}
#[no_mangle]
pub unsafe extern "C" fn glGetPixelMapusv(map: u32, values: *mut u16) {
    get_pixel_map(map, values.cast(), 0x1403);
}
