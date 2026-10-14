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
    let ((al, ah), (bl, bh)) = (halves(a), halves(b));
    pair(
        (al + bl).clamp(-32768, 32767),
        (ah + bh).clamp(-32768, 32767),
    )
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
    let mut table = [0i32; 256];
    for (c, v) in table.iter_mut().enumerate() {
        *v = clamp(value * ONE * c as f32 / 255.) as i32;
    }
    let line0 = x0 & !7;
    let lines = ((x1 + 7) & !7) - line0;
    let fb = crate::renderer::read_color(r, line0, lines)?;
    let acc = buffer(&mut r.frame.accum)?;
    for x in x0..x1 {
        let column = fb.pointer.add((x - line0) * HEIGHT * 4);
        for y in y0..y1 {
            // Bytes A, B, G, R.
            let p = column.add(y * 4);
            let (a, b, g, red) = (p.read(), p.add(1).read(), p.add(2).read(), p.add(3).read());
            let rg = pair(table[red as usize], table[g as usize]);
            let ba = pair(table[b as usize], table[a as usize]);
            let i = pixel(x, y);
            if load {
                acc[i] = rg;
                acc[i + 1] = ba;
            } else {
                acc[i] = add_saturated(acc[i], rg);
                acc[i + 1] = add_saturated(acc[i + 1], ba);
            }
        }
    }
    Ok(())
}

/// The 8-bit color of a component times m (in 8.24 fixed point): the product in 1/256, rounded
/// and saturated.
fn component(f: i32, value: i32) -> u32 {
    let v = ((f as i64 * value as i64) >> 16) as i32 + 128;
    (v >> 8).clamp(0, 255) as u32
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
            // Copy the words out: filling the texture needs the renderer.
            let acc = buffer(&mut r.frame.accum)?;
            let mut words = Vec::new();
            words
                .try_reserve_exact(tw * th * 2)
                .map_err(|_| 0x0505u32)?;
            for x in 0..tw {
                for y in 0..th {
                    let i = pixel(tx + x, ty + y);
                    words.push(acc[i]);
                    words.push(acc[i + 1]);
                }
            }
            let mut fill = |texture: *mut u8, texw: usize, texh: usize| {
                for x in 0..tw {
                    for y in 0..th {
                        let (a0, a1) = (words[(x * th + y) * 2], words[(x * th + y) * 2 + 1]);
                        let (c0, c1) = (halves(a0), halves(a1));
                        let out: [u32; 4] = if fast {
                            [
                                component(f, c0.0),
                                component(f, c0.1),
                                component(f, c1.0),
                                component(f, c1.1),
                            ]
                        } else {
                            let c = [c0.0, c0.1, c1.0, c1.1].map(|v| v as f32 * m);
                            c.map(|v| {
                                if v <= 0. {
                                    0
                                } else if v >= 255. {
                                    255
                                } else {
                                    (v + 0.5) as u32
                                }
                            })
                        };
                        // PICA RGBA8 is ABGR.
                        let at = texture.add(crate::texture::offset_bpp(texw, texh, x, y, 4));
                        at.write(out[3] as u8);
                        at.add(1).write(out[2] as u8);
                        at.add(2).write(out[1] as u8);
                        at.add(3).write(out[0] as u8);
                    }
                }
            };
            crate::renderer::draw_image(r, (tx, ty, tw, th), &mut fill)?;
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
                let f = rintf(value.clamp(-32767., 32767.) * 65536.) as i32;
                for x in area.0..area.2 {
                    for i in pixel(x, area.1)..pixel(x, area.3) {
                        let (l, h) = halves(acc[i]);
                        let mul =
                            |v: i32| ((f as i64 * v as i64) >> 16).clamp(-32768, 32767) as i32;
                        acc[i] = pair(mul(l), mul(h));
                    }
                }
            }),
            // GL_ADD, in two halves so that +-2 saturates the whole range.
            _ => buffer(&mut r.frame.accum).map(|acc| {
                let k = clamp(value * ONE / 2.) as i32;
                let kk = pair(k, k);
                for x in area.0..area.2 {
                    for i in pixel(x, area.1)..pixel(x, area.3) {
                        acc[i] = add_saturated(add_saturated(acc[i], kk), kk);
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
        None => s.accum.draw_buffer = mode,
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
