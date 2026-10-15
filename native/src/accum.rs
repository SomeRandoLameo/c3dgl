//! Color buffer selection (glDrawBuffer, glReadBuffer) and the accumulation buffer (glAccum,
//! glClearAccum, glClear(GL_ACCUM_BUFFER_BIT)). The accumulation buffer lives in normal memory,
//! signed 16 bits per component with 32767 = 1.0, window column by column like the framebuffer.
//! GL_ACCUM and GL_LOAD read the color buffer (waiting for the GPU), GL_RETURN draws the result
//! as RGBA8 images over the scissor box.
use crate::renderer::{with_immediate, Renderer};
use alloc::vec::Vec;

extern "C" {
    fn rintf(x: f32) -> f32;
}

const ONE: f32 = 32767.;
const WIDTH: usize = 400;
const HEIGHT: usize = 240;
/// Pixel rectangles are drawn in tiles of up to TILE x TILE pixels.
const TILE: usize = 256;

pub(crate) const ACCUM_BUFFER_BIT: u32 = 0x0200;

#[derive(Clone)]
pub(crate) struct Accum {
    pub clear: [f32; 4],
    pub draw_buffer: u32,
    pub read_buffer: u32,
    /// Two words per pixel: R | G << 16 and B | A << 16; allocated at the first use.
    buffer: Option<Vec<u32>>,
}

impl Accum {
    pub fn new() -> Self {
        Self {
            clear: [0.; 4],
            draw_buffer: 0x0405,
            read_buffer: 0x0405,
            buffer: None,
        }
    }

    pub fn query(&self, pname: u32) -> Option<([f64; 16], usize)> {
        let mut v = [0.; 16];
        let n = match pname {
            0x0b80 => {
                for i in 0..4 {
                    v[i] = self.clear[i] as f64;
                }
                4
            }
            0x0c01 => {
                v[0] = self.draw_buffer as f64;
                1
            }
            0x0c02 => {
                v[0] = self.read_buffer as f64;
                1
            }
            // GL_ACCUM_*_BITS
            0x0d58..=0x0d5b => {
                v[0] = 16.;
                1
            }
            _ => return None,
        };
        Some((v, n))
    }
}

fn clamp(v: f32) -> i16 {
    unsafe { rintf(v.clamp(-ONE, ONE)) as i16 }
}

fn pair(low: i32, high: i32) -> u32 {
    low as u16 as u32 | (high as u32) << 16
}

fn halves(w: u32) -> (i32, i32) {
    (w as u16 as i16 as i32, (w >> 16) as u16 as i16 as i32)
}

fn add_saturated(a: u32, b: u32) -> u32 {
    crate::dsp::qadd16(a, b)
}

/// The window rectangle [x0, x1) x [y0, y1) accumulation operations change: the scissor box if
/// enabled.
fn rect(r: &Renderer) -> Option<(usize, usize, usize, usize)> {
    let (mut x0, mut y0, mut x1, mut y1) = (0i32, 0i32, WIDTH as i32, HEIGHT as i32);
    let s = &r.frame.raster;
    if s.scissor {
        let b = s.scissor_box;
        x0 = x0.max(b[0]);
        y0 = y0.max(b[1]);
        x1 = x1.min(b[0] + b[2]);
        y1 = y1.min(b[1] + b[3]);
    }
    (x0 < x1 && y0 < y1).then_some((x0 as usize, y0 as usize, x1 as usize, y1 as usize))
}

fn pixel(x: usize, y: usize) -> usize {
    (x * HEIGHT + y) * 2
}

/// The accumulation buffer, allocated and cleared at its first use.
fn buffer(a: &mut Accum) -> Result<&mut Vec<u32>, u32> {
    if a.buffer.is_none() {
        let mut v = Vec::new();
        v.try_reserve_exact(WIDTH * HEIGHT * 2)
            .map_err(|_| 0x0505u32)?;
        v.resize(WIDTH * HEIGHT * 2, 0);
        a.buffer = Some(v);
    }
    Ok(a.buffer.as_mut().unwrap())
}

pub(crate) unsafe fn clear_buffer() {
    with_immediate(|r| {
        let Some((x0, y0, x1, y1)) = rect(r) else {
            return;
        };
        let c = r.frame.accum.clear;
        let rg = pair(clamp(c[0] * ONE) as i32, clamp(c[1] * ONE) as i32);
        let ba = pair(clamp(c[2] * ONE) as i32, clamp(c[3] * ONE) as i32);
        match buffer(&mut r.frame.accum) {
            Ok(acc) => {
                for x in x0..x1 {
                    for y in y0..y1 {
                        let i = pixel(x, y);
                        acc[i] = rg;
                        acc[i + 1] = ba;
                    }
                }
            }
            Err(e) => r.matrices.fail(e),
        }
    });
}

/// GL_ACCUM (add) and GL_LOAD: accumulation buffer (+)= value * color buffer.
unsafe fn from_color(
    r: &mut Renderer,
    rect: (usize, usize, usize, usize),
    value: f32,
    load: bool,
) -> Result<(), u32> {
    let (x0, y0, x1, y1) = rect;
    // value * c for every 8-bit component in the low and in the high half of a word.
    let (mut low, mut high) = ([0u32; 256], [0u32; 256]);
    for c in 0..256 {
        let v = clamp(value * ONE * c as f32 / 255.) as u16 as u32;
        low[c] = v;
        high[c] = v << 16;
    }
    let line0 = x0 & !7;
    let lines = ((x1 + 7) & !7) - line0;
    let fb = crate::renderer::read_color(r, line0, lines)?;
    let acc = buffer(&mut r.frame.accum)?;
    for x in x0..x1 {
        // A pixel is the word A | B << 8 | G << 16 | R << 24.
        let column = core::slice::from_raw_parts(
            fb.pointer.add((x - line0) * HEIGHT * 4).cast::<u32>(),
            HEIGHT,
        );
        let words = &mut acc[pixel(x, y0)..pixel(x, y1)];
        for (&p, w) in column[y0..y1].iter().zip(words.chunks_exact_mut(2)) {
            let rg = low[(p >> 24) as usize] | high[(p >> 16 & 255) as usize];
            let ba = low[(p >> 8 & 255) as usize] | high[(p & 255) as usize];
            if load {
                w[0] = rg;
                w[1] = ba;
            } else {
                w[0] = add_saturated(w[0], rg);
                w[1] = add_saturated(w[1], ba);
            }
        }
    }
    Ok(())
}

/// The 8-bit color of a component times m (in 8.24 fixed point): the product in 1/256, rounded
/// and saturated (the reference for `texel`).
#[cfg(test)]
fn component(f: i32, value: i32) -> u32 {
    let v = ((f as i64 * value as i64) >> 16) as i32 + 128;
    (v >> 8).clamp(0, 255) as u32
}

/// The texel (ABGR bytes in a little-endian word) of the components of an accumulation pair
/// times m (`component` with the DSP instructions).
#[inline(always)]
fn texel(f: i32, rg: u32, ba: u32) -> u32 {
    use crate::dsp::{smlawb, smlawt, usat8_asr8};
    let r = usat8_asr8(smlawb(f, rg, 128));
    let g = usat8_asr8(smlawt(f, rg, 128));
    let b = usat8_asr8(smlawb(f, ba, 128));
    let a = usat8_asr8(smlawt(f, ba, 128));
    a | b << 8 | g << 16 | r << 24
}

/// GL_RETURN: value * accumulation buffer, clamped to [0, 1], into the color buffer through the
/// scissor test and the color mask only.
unsafe fn to_color(
    r: &mut Renderer,
    rect: (usize, usize, usize, usize),
    value: f32,
) -> Result<(), u32> {
    if r.frame.accum.draw_buffer == 0 || r.frame.raster.color_mask == 0 {
        return Ok(());
    }
    let (x0, y0, x1, y1) = rect;
    let m = value * 255. / ONE;
    let fast = value.abs() < 16.;
    let f = if fast { rintf(m * 16777216.) as i32 } else { 0 };
    let mut ty = y0;
    while ty < y1 {
        let th = (y1 - ty).min(TILE);
        let mut tx = x0;
        while tx < x1 {
            let tw = (x1 - tx).min(TILE);
            // The buffer is taken out while the renderer fills the texture.
            buffer(&mut r.frame.accum)?;
            let acc = r.frame.accum.buffer.take().unwrap();
            let mut fill = |texture: *mut u8, texw: usize, texh: usize| {
                // Tiled offsets: a part per row plus a part per column.
                let mut rows = [0usize; TILE];
                for (y, o) in rows.iter_mut().enumerate().take(th) {
                    *o = crate::texture::tiled_y(texw, texh, y, 4);
                }
                for x in 0..tw {
                    let column = &acc[pixel(tx + x, ty)..pixel(tx + x, ty + th)];
                    let col = texture.add(crate::texture::tiled_x(x, 4));
                    for (w, &row) in column.chunks_exact(2).zip(&rows[..th]) {
                        let at = col.add(row);
                        if fast {
                            at.cast::<u32>().write_unaligned(texel(f, w[0], w[1]));
                            continue;
                        }
                        let (c0, c1) = (halves(w[0]), halves(w[1]));
                        let c = [c0.0, c0.1, c1.0, c1.1].map(|v| v as f32 * m);
                        let out = c.map(|v| {
                            if v <= 0. {
                                0
                            } else if v >= 255. {
                                255
                            } else {
                                (v + 0.5) as u32
                            }
                        });
                        // PICA RGBA8 is ABGR.
                        at.write(out[3] as u8);
                        at.add(1).write(out[2] as u8);
                        at.add(2).write(out[1] as u8);
                        at.add(3).write(out[0] as u8);
                    }
                }
            };
            let drawn = crate::renderer::draw_image(r, (tx, ty, tw, th), &mut fill);
            r.frame.accum.buffer = Some(acc);
            drawn?;
            tx += TILE;
        }
        ty += TILE;
    }
    Ok(())
}

#[no_mangle]
pub unsafe extern "C" fn glAccum(op: u32, value: f32) {
    if crate::lists::record(crate::lists::Cmd::Accum(op, value)) {
        return;
    }
    with_immediate(|r| {
        if r.immediate.active {
            r.matrices.fail(0x0502);
            return;
        }
        if !matches!(op, 0x0100..=0x0104) {
            r.matrices.fail(0x0500);
            return;
        }
        // Like Mesa: nothing changes in feedback and selection mode.
        if r.frame.select.active() {
            return;
        }
        let Some(area) = rect(r) else {
            return;
        };
        let result = match op {
            // GL_ACCUM, GL_LOAD
            0x0100 | 0x0101 => from_color(r, area, value, op == 0x0101),
            // GL_RETURN
            0x0102 => to_color(r, area, value),
            // GL_MULT
            0x0103 => buffer(&mut r.frame.accum).map(|acc| {
                use crate::dsp::{pack, smulwb, smulwt, ssat16};
                let f = rintf(value.clamp(-32767., 32767.) * 65536.) as i32;
                for x in area.0..area.2 {
                    for w in &mut acc[pixel(x, area.1)..pixel(x, area.3)] {
                        *w = pack(ssat16(smulwb(f, *w)), ssat16(smulwt(f, *w)));
                    }
                }
            }),
            // GL_ADD, in two halves so that +-2 saturates the whole range.
            _ => buffer(&mut r.frame.accum).map(|acc| {
                let k = clamp(value * ONE / 2.) as i32;
                let kk = pair(k, k);
                for x in area.0..area.2 {
                    for w in &mut acc[pixel(x, area.1)..pixel(x, area.3)] {
                        *w = add_saturated(add_saturated(*w, kk), kk);
                    }
                }
            }),
        };
        if let Err(e) = result {
            r.matrices.fail(e);
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glClearAccum(red: f32, green: f32, blue: f32, alpha: f32) {
    if crate::lists::record(crate::lists::Cmd::ClearAccum([red, green, blue, alpha])) {
        return;
    }
    crate::renderer::with_frame(|s, _| {
        s.accum.clear = [red, green, blue, alpha].map(|c| c.clamp(-1., 1.));
    });
}

/// Error of glDrawBuffer / glReadBuffer: buffers that do not exist are GL_INVALID_OPERATION. The
/// framebuffer is double-buffered RGBA without stereo or auxiliary buffers, drawing always goes to
/// the frame being rendered.
fn buffer_error(mode: u32, draw: bool) -> Option<u32> {
    match mode {
        0 | 0x0408 => (!draw).then_some(0x0500),
        0x0400 | 0x0402 | 0x0404 | 0x0405 | 0x0406 => None,
        0x0401 | 0x0403 | 0x0407 | 0x0409..=0x040c => Some(0x0502),
        _ => Some(0x0500),
    }
}

#[no_mangle]
pub unsafe extern "C" fn glDrawBuffer(mode: u32) {
    if crate::lists::record(crate::lists::Cmd::DrawBuffer(mode)) {
        return;
    }
    crate::renderer::with_frame(|s, m| match buffer_error(mode, true) {
        Some(e) => m.fail(e),
        None => {
            s.accum.draw_buffer = mode;
            s.raster.dirty = true;
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glReadBuffer(mode: u32) {
    if crate::lists::record(crate::lists::Cmd::ReadBuffer(mode)) {
        return;
    }
    crate::renderer::with_frame(|s, m| match buffer_error(mode, false) {
        Some(e) => m.fail(e),
        None => s.accum.read_buffer = mode,
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dsp_texel_matches_the_component_formula() {
        for value in [0.25f32, 1., 0.2, 3.7, -1., 15.9] {
            let f = unsafe { rintf(value * 255. / ONE * 16777216.) } as i32;
            for (a, b) in [
                (0, 0),
                (32767, -32768),
                (12345, -77),
                (-1, 1),
                (20000, 32767),
            ] {
                for (c, d) in [(0, 255), (-32768, 32767), (500, 9000)] {
                    let (rg, ba) = (pair(a, b), pair(c, d));
                    let want = component(f, a) << 24
                        | component(f, b) << 16
                        | component(f, c) << 8
                        | component(f, d);
                    assert_eq!(texel(f, rg, ba), want);
                }
            }
        }
    }
}
