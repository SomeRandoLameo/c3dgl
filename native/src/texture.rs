//! One texture unit, level-zero RGB/RGBA uploads into resident Morton-tiled RGBA8.
use crate::{
    platform::gpu as p,
    renderer::{with_frame, Buffer},
};
use alloc::vec::Vec;
use core::{ffi::c_void, ptr};

const TARGET: u32 = 0x0de1;
const LIMIT: usize = 64;

/// GL definition of a mipmap level above 0 (level 0 lives in `Object`).
#[derive(Clone, Copy, Default)]
struct Level {
    defined: bool,
    width: usize,
    height: usize,
    base: u32,
    packed: bool,
}

struct Object {
    name: u32,
    instantiated: bool,
    storage: Option<Buffer>,
    width: usize,
    height: usize,
    min: u32,
    mag: u32,
    wrap: [u32; 2],
    used: bool,
    base: u32,
    /// Stored levels: level l of the chain is `stored(width) >> l` wide, minimum 8.
    levels: usize,
    defs: [Level; 12],
    generate: bool,
    /// RGB565 storage (GL_UNSIGNED_SHORT_5_6_5 uploads): texels hold 5/6/5-bit precision.
    packed: bool,
}

impl Object {
    fn new(name: u32, instantiated: bool) -> Self {
        Self {
            name,
            instantiated,
            storage: None,
            width: 0,
            height: 0,
            min: 0x2702,
            mag: 0x2601,
            wrap: [0x2901; 2],
            used: false,
            base: 0x1908,
            levels: 1,
            defs: [Level::default(); 12],
            generate: false,
            packed: false,
        }
    }

    /// GL mipmap completeness: levels 1.. defined with halved sizes and level 0's format.
    fn gl_complete(&self) -> bool {
        if self.width == 0 || self.height == 0 {
            return false;
        }
        let mut l = 1;
        while self.width >> l > 0 || self.height >> l > 0 {
            let d = self.defs[l];
            if !(d.defined
                && d.width == level_size(self.width, l)
                && d.height == level_size(self.height, l)
                && d.base == self.base
                && d.packed == self.packed)
            {
                return false;
            }
            l += 1;
        }
        true
    }

    /// A mipmap filter without all levels disables texturing, as in the C renderer.
    fn complete(&self) -> bool {
        self.storage.is_some() && (!mip_filter(self.min) || self.gl_complete())
    }
}

#[derive(Clone, Copy)]
pub(crate) struct Packet {
    pub enabled: bool,
    pub complete: bool,
    pub address: u32,
    pub dimensions: u32,
    pub parameters: u32,
    pub replace: bool,
    pub scale: [f32; 2],
    pub tev: [u32; 5],
    pub lod: u32,
    /// PICA texture format register: 0 RGBA8, 3 RGB565.
    pub format: u32,
}

#[derive(Clone, Copy)]
struct Unit {
    enabled: bool,
    bound: u32,
    env: Environment,
}
impl Unit {
    fn new() -> Self {
        Self {
            enabled: false,
            bound: 0,
            env: Environment::new(),
        }
    }
}

#[derive(Clone, Copy)]
pub(crate) struct Environment {
    mode: u32,
    color: u32,
    functions: [u32; 2],
    sources: [[u32; 3]; 2],
    operands: [[u32; 3]; 2],
    scale: [u32; 2],
}
impl Environment {
    fn new() -> Self {
        Self {
            mode: 0x2100,
            color: 0,
            functions: [0x2100; 2],
            sources: [[0x1702, 0x8578, 0x8576]; 2],
            operands: [[0x300, 0x300, 0x302], [0x302; 3]],
            scale: [1; 2],
        }
    }
    pub(crate) fn words(self, unit: usize, enabled: bool, complete: bool, base: u32) -> [u32; 5] {
        let prev = if unit == 0 { 0 } else { 15 };
        let tex = if complete { 3 + unit as u32 } else { 14 };
        let color = if complete { self.color } else { 0xff000000 };
        let pack = |v: [u32; 3]| v[0] | v[1] << 4 | v[2] << 8;
        if !enabled {
            return [prev | prev << 16, 0, 0, color, 0];
        }
        let function = |f| match f {
            0x1e01 => 0,
            0x2100 => 1,
            0x104 => 2,
            0x8574 => 3,
            0x8575 => 4,
            0x84e7 => 5,
            0x86ae => 6,
            0x86af => 7,
            _ => unreachable!(),
        };
        if self.mode == 0x8570 {
            let source = |src| match src {
                0x1702 => tex,
                0x84c0..=0x84c2 => 3 + src - 0x84c0,
                0x8576 => 14,
                0x8577 => 0,
                _ => prev,
            };
            let rgb = self.operands[0].map(|op| match op {
                0x301 => 1,
                0x302 => 2,
                0x303 => 3,
                _ => 0,
            });
            let alpha = self.operands[1].map(|op| (op == 0x303) as u32);
            let scale = self.scale.map(|v| match v {
                2 => 1,
                4 => 2,
                _ => 0,
            });
            return [
                pack(self.sources[0].map(source)) | pack(self.sources[1].map(source)) << 16,
                pack(rgb) | pack(alpha) << 12,
                function(self.functions[0]) | function(self.functions[1]) << 16,
                color,
                scale[0] | scale[1] << 16,
            ];
        }
        let has_color = base != 0x1906;
        let has_alpha = matches!(base, 0x1906 | 0x1908 | 0x190a);
        let (rgb_src, rgb_func, operands) = if !has_color {
            ([prev; 3], 0, 0)
        } else {
            match self.mode {
                0x1e01 => ([tex, prev, prev], 0, 0),
                0x2101 => ([tex, prev, tex], 4, 2 << 8),
                0x0be2 => ([14, prev, tex], 4, 0),
                0x104 => ([prev, tex, prev], 2, 0),
                _ => ([tex, prev, prev], 1, 0),
            }
        };
        let (alpha_src, alpha_func) = if self.mode == 0x2101 || self.mode == 0x1e01 && !has_alpha {
            ([prev; 3], 0)
        } else if self.mode == 0x1e01 {
            ([tex, prev, prev], 0)
        } else {
            ([tex, prev, prev], 1)
        };
        [
            pack(rgb_src) | pack(alpha_src) << 16,
            operands,
            rgb_func | alpha_func << 16,
            color,
            0,
        ]
    }
}

pub(crate) struct Textures {
    units: [Unit; 3],
    pub active: usize,
    pub dirty: bool,
    /// A unit 0 texcoord with q != 1 was submitted: projection mode for good (exact with q = 1 too).
    pub q_used: bool,
    alignment: usize,
    objects: Vec<Object>,
    retired: Vec<Buffer>,
}

impl Textures {
    pub fn new() -> Self {
        Self {
            units: [Unit::new(); 3],
            active: 0,
            dirty: false,
            q_used: false,
            alignment: 4,
            objects: Vec::new(),
            retired: Vec::new(),
        }
    }

    pub fn mark_q(&mut self) {
        if !self.q_used {
            self.q_used = true;
            self.dirty = true;
        }
    }

    pub fn collected(&mut self) {
        self.retired.clear();
        for o in &mut self.objects {
            o.used = false;
        }
    }

    fn index(&mut self) -> Result<usize, u32> {
        if let Some(i) = self
            .objects
            .iter()
            .position(|o| o.name == self.units[self.active].bound)
        {
            return Ok(i);
        }

        self.insert(self.units[self.active].bound, true)
    }

    fn insert(&mut self, name: u32, instantiated: bool) -> Result<usize, u32> {
        if self.objects.len() >= LIMIT {
            return Err(0x0505);
        }

        self.objects.try_reserve(1).map_err(|_| 0x0505u32)?;
        self.objects.push(Object::new(name, instantiated));

        Ok(self.objects.len() - 1)
    }

    pub unsafe fn packet(&self) -> Packet {
        self.packet_for(self.active)
    }

    pub unsafe fn packets(&self) -> [Packet; 3] {
        core::array::from_fn(|unit| self.packet_for(unit))
    }
    pub fn enabled(&self) -> bool {
        self.units[self.active].enabled
    }
    pub fn set_enabled(&mut self, on: bool) {
        if self.enabled() != on {
            self.units[self.active].enabled = on;
            self.dirty = true;
        }
    }

    unsafe fn packet_for(&self, unit: usize) -> Packet {
        let mut s = Packet {
            enabled: self.units[unit].enabled,
            complete: false,
            address: 0,
            dimensions: 0,
            parameters: 0,
            replace: self.units[unit].env.mode == 0x1e01,
            scale: [1.; 2],
            tev: [0; 5],
            lod: 0,
            format: 0,
        };

        if let Some(o) = self
            .objects
            .iter()
            .find(|o| o.name == self.units[unit].bound)
        {
            s.complete = o.complete();
            if s.complete {
                s.address = o.storage.as_ref().unwrap().physical();
                let (tw, th) = (stored(o.width), stored(o.height));
                s.dimensions = (tw as u32) << 16 | th as u32;
                s.scale = [o.width as f32 / tw as f32, o.height as f32 / th as f32];
                let wrap = |v| match v {
                    0x812f => 0,
                    0x2901 => 2,
                    _ => 3,
                };
                // Mipmap filters sample the stored levels; the others only level 0.
                let min_linear = matches!(o.min, 0x2601 | 0x2701 | 0x2703);
                let mip_linear = matches!(o.min, 0x2702 | 0x2703);
                s.parameters = ((o.mag == 0x2601) as u32) << 1
                    | (min_linear as u32) << 2
                    | wrap(o.wrap[1]) << 8
                    | wrap(o.wrap[0]) << 12
                    | (mip_linear as u32) << 24;
                s.format = if o.packed { 3 } else { 0 };
                s.lod = if mip_filter(o.min) {
                    (o.levels as u32 - 1) << 16
                } else {
                    0
                };
            }
        }

        // An incomplete or missing texture disables the unit, as in the C renderer.
        if !s.complete {
            s.enabled = false;
        }
        let base = self
            .objects
            .iter()
            .find(|o| o.name == self.units[unit].bound)
            .map_or(0x1908, |o| o.base);
        s.tev = self.units[unit]
            .env
            .words(unit, s.enabled, s.complete, base);
        s
    }

    pub fn sampled(&mut self) {
        for unit in &self.units {
            if unit.enabled {
                if let Some(o) = self
                    .objects
                    .iter_mut()
                    .find(|o| o.name == unit.bound && o.complete())
                {
                    o.used = true;
                }
            }
        }
    }

    pub fn query(&self, pname: u32) -> Option<([f64; 16], usize)> {
        let mut v = [0.; 16];
        v[0] = match pname {
            0x0de1 => self.units[self.active].enabled as u8 as f64,
            0x8069 => self.units[self.active].bound as f64,
            0x0cf5 => self.alignment as f64,
            0x0d33 => 1024.,
            0x84e0 => (0x84c0 + self.active) as f64,
            0x84e2 => 3.,
            _ => return None,
        };

        Some((v, 1))
    }

    unsafe fn replace(
        &mut self,
        i: usize,
        storage: Option<Buffer>,
        width: usize,
        height: usize,
    ) -> Result<(), u32> {
        if self.objects[i].used && self.objects[i].storage.is_some() {
            self.retired.try_reserve(1).map_err(|_| 0x0505u32)?;
        }

        let o = &mut self.objects[i];
        if let Some(old) = core::mem::replace(&mut o.storage, storage) {
            if o.used {
                self.retired.push(old);
            }
        }
        o.width = width;
        o.height = height;
        o.used = false;

        self.dirty = true;

        Ok(())
    }
}

fn target(t: u32) -> Result<(), u32> {
    if t == TARGET {
        Ok(())
    } else {
        Err(0x0500)
    }
}

fn format(f: u32, t: u32) -> Result<usize, u32> {
    if t == 0x8363 && f == 0x1907 {
        return Ok(2);
    }
    if t != 0x1401 {
        return Err(0x0500);
    }

    match f {
        0x1907 => Ok(3),
        0x1908 => Ok(4),
        0x1909 | 0x1906 => Ok(1),
        0x190a => Ok(2),
        _ => Err(0x0500),
    }
}

fn bpp_of(packed: bool) -> usize {
    if packed {
        2
    } else {
        4
    }
}

fn mip_filter(filter: u32) -> bool {
    matches!(filter, 0x2700..=0x2703)
}

fn level_size(size: usize, level: usize) -> usize {
    (size >> level).max(1)
}

/// Last level of a chain stored `tw` x `th` (powers of two): levels stay at least 8x8.
fn max_stored(tw: usize, th: usize) -> usize {
    (tw.min(th).trailing_zeros() as usize).saturating_sub(3)
}

fn level_offset(tw: usize, th: usize, level: usize, bpp: usize) -> usize {
    (0..level).map(|k| (tw >> k) * (th >> k) * bpp).sum()
}

fn dimension(v: i32) -> bool {
    (0..=1024).contains(&v)
}

fn stored(size: usize) -> usize {
    size.max(8).next_power_of_two()
}

fn padding_source(x: usize, size: usize, padded: usize, wrap: u32) -> usize {
    let pad = x - size;
    let back = padded - x;
    match wrap {
        0x2901 if pad < back => pad % size,
        0x2901 => size - 1 - (back - 1) % size,
        0x8370 => size - 1 - pad % size,
        _ => size - 1,
    }
}

unsafe fn padding(out: *mut u8, w: usize, h: usize, wrap: [u32; 2]) {
    padding_in(out, w, h, stored(w), stored(h), wrap, 4);
}

/// Fill the padding of an image `w` x `h` inside a stored level `tw` x `th`.
unsafe fn padding_in(
    out: *mut u8,
    w: usize,
    h: usize,
    tw: usize,
    th: usize,
    wrap: [u32; 2],
    bpp: usize,
) {
    let (w, h) = (w.min(tw), h.min(th));
    for y in 0..th {
        for x in 0..tw {
            if x < w && y < h {
                continue;
            }
            let sx = if x < w {
                x
            } else {
                padding_source(x, w, tw, wrap[0])
            };
            let sy = if y < h {
                y
            } else {
                padding_source(y, h, th, wrap[1])
            };
            ptr::copy_nonoverlapping(
                out.add(offset_bpp(tw, th, sx, sy, bpp)),
                out.add(offset_bpp(tw, th, x, y, bpp)),
                bpp,
            );
        }
    }
}

/// C `pack16` for GPU_RGB565 from 8-bit components.
fn pack565(c: [u32; 3]) -> u16 {
    (((c[0] * 31 + 127) / 255) << 11 | ((c[1] * 63 + 127) / 255) << 5 | ((c[2] * 31 + 127) / 255))
        as u16
}

/// C `unpack16` for GPU_RGB565 to 8-bit components.
fn unpack565(v: u16) -> [u32; 3] {
    let v = v as u32;
    [
        (v >> 11) * 255 / 31,
        ((v >> 5) & 63) * 255 / 63,
        (v & 31) * 255 / 31,
    ]
}

unsafe fn put565(out: *mut u8, tw: usize, th: usize, x: usize, y: usize, v: u16) {
    out.add(offset_bpp(tw, th, x, y, 2))
        .cast::<u16>()
        .write_unaligned(v);
}

unsafe fn get565(out: *const u8, tw: usize, th: usize, x: usize, y: usize) -> u16 {
    out.add(offset_bpp(tw, th, x, y, 2))
        .cast::<u16>()
        .read_unaligned()
}

/// Upload GL_UNSIGNED_SHORT_5_6_5 texels (little endian) into RGB565 storage.
unsafe fn upload565(
    out: *mut u8,
    tw: usize,
    th: usize,
    x0: usize,
    y0: usize,
    w: usize,
    h: usize,
    pixels: *const u8,
    alignment: usize,
) {
    let stride = (w * 2 + alignment - 1) & !(alignment - 1);
    for y in 0..h {
        for x in 0..w {
            let s = pixels.add(y * stride + x * 2);
            put565(
                out,
                tw,
                th,
                x0 + x,
                y0 + y,
                u16::from_le_bytes([s.read(), s.add(1).read()]),
            );
        }
    }
}

/// Upload 8-bit GL data into RGB565 storage (red, green, blue rounded; luminance in all three).
unsafe fn upload_to_565(
    out: *mut u8,
    tw: usize,
    th: usize,
    x0: usize,
    y0: usize,
    w: usize,
    h: usize,
    pixels: *const u8,
    bpp: usize,
    alignment: usize,
) {
    let stride = (w * bpp + alignment - 1) & !(alignment - 1);
    for y in 0..h {
        for x in 0..w {
            let s = pixels.add(y * stride + x * bpp);
            let c = if bpp >= 3 {
                [
                    s.read() as u32,
                    s.add(1).read() as u32,
                    s.add(2).read() as u32,
                ]
            } else {
                [s.read() as u32; 3]
            };
            put565(out, tw, th, x0 + x, y0 + y, pack565(c));
        }
    }
}

/// GL_GENERATE_MIPMAP: every stored level from level 0 with a 2x2 box filter
/// (`(sum + 2) / 4`; RGB565 textures averaged at 8-bit precision and repacked).
unsafe fn generate(
    out: *mut u8,
    tw: usize,
    th: usize,
    levels: usize,
    w: usize,
    h: usize,
    packed: bool,
    wrap: [u32; 2],
) {
    let bpp = if packed { 2 } else { 4 };
    for l in 1..levels {
        let (sw, sh) = (level_size(w, l - 1), level_size(h, l - 1));
        let (dw, dh) = (level_size(w, l), level_size(h, l));
        let (stw, sth) = (tw >> (l - 1), th >> (l - 1));
        let (dtw, dth) = (tw >> l, th >> l);
        let src = out.add(level_offset(tw, th, l - 1, bpp));
        let dst = out.add(level_offset(tw, th, l, bpp));
        for y in 0..dh.min(dth) {
            for x in 0..dw.min(dtw) {
                let xs = [2 * x, if 2 * x + 1 < sw { 2 * x + 1 } else { 2 * x }];
                let ys = [2 * y, if 2 * y + 1 < sh { 2 * y + 1 } else { 2 * y }];
                let mut sum = [0u32; 4];
                for i in 0..4 {
                    let (sx, sy) = (xs[i & 1], ys[i >> 1]);
                    if packed {
                        let c = unpack565(get565(src, stw, sth, sx, sy));
                        for k in 0..3 {
                            sum[k] += c[k];
                        }
                    } else {
                        let p = src.add(offset(stw, sth, sx, sy));
                        for k in 0..4 {
                            sum[k] += p.add(k).read() as u32;
                        }
                    }
                }
                let avg = sum.map(|v| (v + 2) / 4);
                if packed {
                    put565(dst, dtw, dth, x, y, pack565([avg[0], avg[1], avg[2]]));
                } else {
                    let d = dst.add(offset(dtw, dth, x, y));
                    for k in 0..4 {
                        d.add(k).write(avg[k] as u8);
                    }
                }
            }
        }
        padding_in(dst, dw, dh, dtw, dth, wrap, bpp);
    }
}

/// GL bottom row first; PICA's tiled image runs top to bottom and stores A,B,G,R.
fn offset(width: usize, height: usize, x: usize, y: usize) -> usize {
    offset_bpp(width, height, x, y, 4)
}

/// Byte offset of a texel in Morton-tiled storage with `bpp` bytes per texel.
fn offset_bpp(width: usize, height: usize, x: usize, y: usize, bpp: usize) -> usize {
    let y = height - 1 - y;
    let tile = (y / 8) * (width / 8) + x / 8;
    let morton = (x & 1)
        | ((y & 1) << 1)
        | ((x & 2) << 1)
        | ((y & 2) << 2)
        | ((x & 4) << 2)
        | ((y & 4) << 3);
    (tile * 64 + morton) * bpp
}

unsafe fn convert_base(
    out: *mut u8,
    tw: usize,
    th: usize,
    x: usize,
    y: usize,
    w: usize,
    h: usize,
    base: u32,
) {
    for row in y..y + h {
        for col in x..x + w {
            let d = out.add(offset(tw, th, col, row));
            if matches!(base, 0x1907 | 0x1909) {
                d.write(255);
            }
            if matches!(base, 0x1909 | 0x190a) {
                let l = d.add(3).read();
                d.add(1).write(l);
                d.add(2).write(l);
            }
            if base == 0x1906 {
                d.add(1).write(0);
                d.add(2).write(0);
                d.add(3).write(0);
            }
        }
    }
}

unsafe fn upload(
    out: *mut u8,
    tw: usize,
    th: usize,
    x0: usize,
    y0: usize,
    w: usize,
    h: usize,
    pixels: *const u8,
    bpp: usize,
    alignment: usize,
    alpha_only: bool,
) {
    let stride = (w * bpp + alignment - 1) & !(alignment - 1);
    for y in 0..h {
        for x in 0..w {
            let s = pixels.add(y * stride + x * bpp);
            let d = out.add(offset(tw, th, x0 + x, y0 + y));
            d.write(match bpp {
                4 => s.add(3).read(),
                2 => s.add(1).read(),
                _ => 255,
            });
            d.add(1)
                .write(if bpp >= 3 { s.add(2).read() } else { s.read() });
            d.add(2)
                .write(if bpp >= 3 { s.add(1).read() } else { s.read() });
            d.add(3).write(s.read());
            if alpha_only {
                d.write(s.read());
                d.add(1).write(0);
                d.add(2).write(0);
                d.add(3).write(0);
            }
        }
    }
}

macro_rules! api {
    ($body:expr) => {
        with_frame(|s, m| {
            if let Err(e) = ($body)(&mut s.textures) {
                m.fail(e);
            }
        })
    };
}

#[no_mangle]
pub unsafe extern "C" fn glActiveTexture(unit: u32) {
    with_frame(|s, m| {
        if !(0x84c0..=0x84c2).contains(&unit) {
            m.fail(0x0500);
            return;
        }
        s.textures.active = (unit - 0x84c0) as usize;
        m.active_texture(s.textures.active);
    });
}

#[no_mangle]
pub unsafe extern "C" fn glGenTextures(n: i32, out: *mut u32) {
    api!(|t: &mut Textures| {
        if n < 0 || n > 0 && out.is_null() {
            return Err(0x0501);
        }

        if n as usize > LIMIT - t.objects.len() {
            return Err(0x0505);
        }

        t.objects.try_reserve(n as usize).map_err(|_| 0x0505u32)?;
        let mut name = 1;
        for i in 0..n as usize {
            while t.objects.iter().any(|o| o.name == name) {
                name = name.checked_add(1).ok_or(0x0505u32)?;
            }
            t.objects.push(Object::new(name, false));
            out.add(i).write(name);
        }

        Ok(())
    });
}

#[no_mangle]
pub unsafe extern "C" fn glBindTexture(kind: u32, name: u32) {
    api!(|t: &mut Textures| {
        target(kind)?;
        let i = if let Some(i) = t.objects.iter().position(|o| o.name == name) {
            i
        } else {
            t.insert(name, true)?
        };

        t.objects[i].instantiated = true;
        if t.units[t.active].bound != name {
            t.units[t.active].bound = name;
            t.dirty = true;
        }

        Ok(())
    });
}

#[no_mangle]
pub unsafe extern "C" fn glDeleteTextures(n: i32, names: *const u32) {
    api!(|t: &mut Textures| {
        if n < 0 || n > 0 && names.is_null() {
            return Err(0x0501);
        }
        t.retired
            .try_reserve(t.objects.len())
            .map_err(|_| 0x0505u32)?;
        for i in 0..n as usize {
            let name = names.add(i).read();
            if name == 0 {
                continue;
            }
            if let Some(i) = t.objects.iter().position(|o| o.name == name) {
                let o = t.objects.swap_remove(i);
                if o.used {
                    if let Some(b) = o.storage {
                        t.retired.push(b);
                    }
                }
                for unit in &mut t.units {
                    if unit.bound == name {
                        unit.bound = 0;
                        t.dirty = true;
                    }
                }
            }
        }
        Ok(())
    });
}
#[no_mangle]
pub unsafe extern "C" fn glIsTexture(name: u32) -> u8 {
    let mut value = 0;
    with_frame(|s, _| {
        value = (name != 0
            && s.textures
                .objects
                .iter()
                .any(|o| o.name == name && o.instantiated)) as u8
    });
    value
}
#[no_mangle]
pub unsafe extern "C" fn glPixelStorei(pname: u32, value: i32) {
    api!(|t: &mut Textures| {
        if pname != 0x0cf5 {
            return Err(0x0500);
        }
        if !matches!(value, 1 | 2 | 4 | 8) {
            return Err(0x0501);
        }
        t.alignment = value as usize;
        Ok(())
    });
}
/// Chain buffer of `levels` levels for an image of `w` x `h`, optionally keeping the old contents.
unsafe fn chain_buffer(
    o: &Object,
    levels: usize,
    w: usize,
    h: usize,
    packed: bool,
    keep: bool,
) -> Result<Buffer, u32> {
    let (tw, th) = (stored(w), stored(h));
    let bytes = level_offset(tw, th, levels, bpp_of(packed));
    let buffer = Buffer::new(bytes, false).ok_or(0x0505u32)?;
    ptr::write_bytes(buffer.pointer, 0, bytes);
    if keep {
        if let Some(old) = &o.storage {
            let n = level_offset(
                stored(o.width),
                stored(o.height),
                o.levels,
                bpp_of(o.packed),
            )
            .min(bytes);
            ptr::copy_nonoverlapping(old.pointer, buffer.pointer, n);
        }
    }
    Ok(buffer)
}

/// Mark all levels as defined by GL_GENERATE_MIPMAP.
fn define_generated(o: &mut Object) {
    let mut l = 1;
    while o.width >> l > 0 || o.height >> l > 0 {
        o.defs[l] = Level {
            defined: true,
            width: level_size(o.width, l),
            height: level_size(o.height, l),
            base: o.base,
            packed: o.packed,
        };
        l += 1;
    }
}

#[no_mangle]
pub unsafe extern "C" fn glTexParameteri(kind: u32, pname: u32, value: i32) {
    api!(|t: &mut Textures| {
        target(kind)?;
        let value = value as u32;
        let valid = match pname {
            0x2801 => matches!(value, 0x2600 | 0x2601 | 0x2700..=0x2703),
            0x2800 => matches!(value, 0x2600 | 0x2601),
            0x2802 | 0x2803 => matches!(value, 0x2901 | 0x812f | 0x8370),
            0x8191 => true,
            _ => false,
        };
        if !valid {
            return Err(0x0500);
        }
        let i = t.index()?;
        let o = &t.objects[i];
        let current = match pname {
            0x2801 => o.min,
            0x2800 => o.mag,
            0x2802 => o.wrap[0],
            0x2803 => o.wrap[1],
            _ => o.generate as u32,
        };
        let value = if pname == 0x8191 {
            (value != 0) as u32
        } else {
            value
        };
        if current != value {
            let (w, h) = (o.width, o.height);
            let (tw, th) = (stored(w), stored(h));
            if pname == 0x8191 && value != 0 && o.storage.is_some() {
                // Enabling generation rebuilds every level from level 0.
                let levels = 1 + max_stored(tw, th);
                let storage = chain_buffer(o, levels, w, h, o.packed, true)?;
                generate(storage.pointer, tw, th, levels, w, h, o.packed, o.wrap);
                let bytes = level_offset(tw, th, levels, bpp_of(o.packed));
                if p::c3dglPlatformFlush(storage.pointer, bytes as u32) < 0 {
                    return Err(0x0502);
                }
                t.replace(i, Some(storage), w, h)?;
                let o = &mut t.objects[i];
                o.levels = levels;
                define_generated(o);
            } else if matches!(pname, 0x2802 | 0x2803)
                && o.storage.is_some()
                && (0..o.levels).any(|l| level_size(w, l) != tw >> l || level_size(h, l) != th >> l)
            {
                let mut wrap = o.wrap;
                wrap[(pname == 0x2803) as usize] = value;
                let levels = o.levels;
                let bpp = bpp_of(o.packed);
                let storage = chain_buffer(o, levels, w, h, o.packed, true)?;
                for l in 0..levels {
                    padding_in(
                        storage.pointer.add(level_offset(tw, th, l, bpp)),
                        level_size(w, l),
                        level_size(h, l),
                        tw >> l,
                        th >> l,
                        wrap,
                        bpp,
                    );
                }
                if p::c3dglPlatformFlush(storage.pointer, level_offset(tw, th, levels, bpp) as u32)
                    < 0
                {
                    return Err(0x0502);
                }
                t.replace(i, Some(storage), w, h)?;
            }
            let o = &mut t.objects[i];
            match pname {
                0x2801 => o.min = value,
                0x2800 => o.mag = value,
                0x2802 => o.wrap[0] = value,
                0x2803 => o.wrap[1] = value,
                _ => o.generate = value != 0,
            }
            t.dirty = true;
        }
        Ok(())
    });
}
#[no_mangle]
pub unsafe extern "C" fn glGetTexParameteriv(kind: u32, pname: u32, out: *mut i32) {
    api!(|t: &mut Textures| {
        target(kind)?;
        if out.is_null() {
            return Err(0x0501);
        }
        if !matches!(pname, 0x2800..=0x2803 | 0x8191) {
            return Err(0x0500);
        }
        let i = t.index()?;
        let o = &t.objects[i];
        let value = match pname {
            0x2801 => o.min,
            0x2800 => o.mag,
            0x2802 => o.wrap[0],
            0x2803 => o.wrap[1],
            _ => o.generate as u32,
        };
        out.write(value as i32);
        Ok(())
    });
}
#[no_mangle]
pub unsafe extern "C" fn glTexImage2D(
    kind: u32,
    level: i32,
    internal: i32,
    w: i32,
    h: i32,
    border: i32,
    f: u32,
    ty: u32,
    pixels: *const c_void,
) {
    api!(|t: &mut Textures| {
        target(kind)?;
        let bpp = format(f, ty)?;
        if !matches!(internal, 0x1906..=0x190a) {
            return Err(0x0501);
        }
        if !(0..=10).contains(&level)
            || border != 0
            || !dimension(w)
            || !dimension(h)
            || w > 1024 >> level
            || h > 1024 >> level
        {
            return Err(0x0501);
        }
        let i = t.index()?;
        let (level, w, h) = (level as usize, w as usize, h as usize);
        let (internal, packed_in) = (internal as u32, ty == 0x8363);
        let alignment = t.alignment;
        // Fill a level region of a chain stored `packed` (RGB565) or as RGBA8.
        let fill = |region: *mut u8, tw: usize, th: usize, packed: bool| {
            if !pixels.is_null() {
                match (packed, packed_in) {
                    (true, true) => upload565(region, tw, th, 0, 0, w, h, pixels.cast(), alignment),
                    (true, false) => {
                        upload_to_565(region, tw, th, 0, 0, w, h, pixels.cast(), bpp, alignment)
                    }
                    _ => upload(
                        region,
                        tw,
                        th,
                        0,
                        0,
                        w,
                        h,
                        pixels.cast(),
                        bpp,
                        alignment,
                        f == 0x1906,
                    ),
                }
            }
            if !packed {
                convert_base(region, tw, th, 0, 0, w, h, internal);
            }
        };

        if level > 0 {
            // Levels before level 0 are ignored; levels below 8x8 only count for completeness.
            let o = &t.objects[i];
            if o.storage.is_none() || w == 0 || h == 0 {
                return Ok(());
            }
            let (tw, th) = (stored(o.width), stored(o.height));
            let fits = w == level_size(o.width, level)
                && h == level_size(o.height, level)
                && internal == o.base;
            let packed = o.packed;
            let o = &mut t.objects[i];
            o.defs[level] = Level {
                defined: true,
                width: w,
                height: h,
                base: internal,
                packed: if fits { packed } else { packed_in },
            };
            if fits && level <= max_stored(tw, th) {
                let o = &t.objects[i];
                let levels = 1 + max_stored(tw, th);
                let bpp = bpp_of(packed);
                let storage = chain_buffer(o, levels, o.width, o.height, packed, true)?;
                let region = storage.pointer.add(level_offset(tw, th, level, bpp));
                let (lw, lh) = (tw >> level, th >> level);
                ptr::write_bytes(region, 0, lw * lh * bpp);
                fill(region, lw, lh, packed);
                padding_in(region, w, h, lw, lh, o.wrap, bpp);
                if p::c3dglPlatformFlush(storage.pointer, level_offset(tw, th, levels, bpp) as u32)
                    < 0
                {
                    return Err(0x0502);
                }
                let (ow, oh) = (o.width, o.height);
                t.replace(i, Some(storage), ow, oh)?;
                t.objects[i].levels = levels;
            }
            t.dirty = true;
            return Ok(());
        }

        if w == 0 || h == 0 {
            let o = &mut t.objects[i];
            o.levels = 1;
            o.defs = [Level::default(); 12];
            return t.replace(i, None, w, h);
        }
        let (tw, th) = (stored(w), stored(h));
        let o = &t.objects[i];
        let same = o.storage.is_some()
            && o.width == w
            && o.height == h
            && o.base == internal
            && o.packed == packed_in;
        let levels = if o.generate && max_stored(tw, th) > 0 {
            1 + max_stored(tw, th)
        } else if same {
            o.levels
        } else {
            1
        };
        let bpp = bpp_of(packed_in);
        let storage = chain_buffer(o, levels, w, h, packed_in, same)?;
        ptr::write_bytes(storage.pointer, 0, tw * th * bpp);
        fill(storage.pointer, tw, th, packed_in);
        padding_in(storage.pointer, w, h, tw, th, o.wrap, bpp);
        let generating = o.generate;
        if generating {
            generate(storage.pointer, tw, th, levels, w, h, packed_in, o.wrap);
        }
        if p::c3dglPlatformFlush(storage.pointer, level_offset(tw, th, levels, bpp) as u32) < 0 {
            return Err(0x0502);
        }
        t.replace(i, Some(storage), w, h)?;
        let o = &mut t.objects[i];
        if !same {
            o.defs = [Level::default(); 12];
        }
        o.base = internal;
        o.packed = packed_in;
        o.levels = levels;
        if generating {
            define_generated(o);
        }
        Ok(())
    });
}

#[no_mangle]
pub unsafe extern "C" fn glTexSubImage2D(
    kind: u32,
    level: i32,
    x: i32,
    y: i32,
    w: i32,
    h: i32,
    f: u32,
    ty: u32,
    pixels: *const c_void,
) {
    api!(|t: &mut Textures| {
        target(kind)?;
        let bpp = format(f, ty)?;
        if !(0..=10).contains(&level) || x < 0 || y < 0 || w < 0 || h < 0 {
            return Err(0x0501);
        }

        let i = t.index()?;
        let o = &t.objects[i];
        let level = level as usize;
        let (x, y, w, h) = (x as usize, y as usize, w as usize, h as usize);
        if o.storage.is_none() {
            return Err(0x0502);
        }
        let (iw, ih) = if level == 0 {
            (o.width, o.height)
        } else if o.defs[level].defined {
            (o.defs[level].width, o.defs[level].height)
        } else {
            return Err(0x0502);
        };
        if x > iw || y > ih || w > iw - x || h > ih - y {
            return Err(0x0501);
        }
        if w == 0 || h == 0 || level >= o.levels {
            return Ok(());
        }

        if pixels.is_null() {
            return Err(0x0501);
        }

        // Copy on update: queued draws retain their previous texture image.
        let (tw, th) = (stored(o.width), stored(o.height));
        let (lw, lh) = (tw >> level, th >> level);
        let (levels, packed) = (o.levels, o.packed);
        let pbpp = bpp_of(packed);
        let storage = chain_buffer(o, levels, o.width, o.height, packed, true)?;
        let region = storage.pointer.add(level_offset(tw, th, level, pbpp));
        match (packed, ty == 0x8363) {
            (true, true) => upload565(region, lw, lh, x, y, w, h, pixels.cast(), t.alignment),
            (true, false) => {
                upload_to_565(region, lw, lh, x, y, w, h, pixels.cast(), bpp, t.alignment)
            }
            _ => {
                upload(
                    region,
                    lw,
                    lh,
                    x,
                    y,
                    w,
                    h,
                    pixels.cast(),
                    bpp,
                    t.alignment,
                    f == 0x1906,
                );
                convert_base(region, lw, lh, x, y, w, h, o.base);
            }
        }
        padding_in(region, iw, ih, lw, lh, o.wrap, pbpp);
        if level == 0 && o.generate {
            generate(
                storage.pointer,
                tw,
                th,
                levels,
                o.width,
                o.height,
                packed,
                o.wrap,
            );
        }
        if p::c3dglPlatformFlush(storage.pointer, level_offset(tw, th, levels, pbpp) as u32) < 0 {
            return Err(0x0502);
        }

        let (ow, oh) = (o.width, o.height);
        t.replace(i, Some(storage), ow, oh)
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn npot_padding_matches_reference_wrap_and_preserves_image() {
        for wrap in [0x2901, 0x8370, 0x812f] {
            for (w, h) in [(12, 12), (1, 3), (9, 17)] {
                let (tw, th) = (stored(w), stored(h));
                let mut image = alloc::vec![0; tw * th * 4];
                for y in 0..h {
                    for x in 0..w {
                        image[offset(tw, th, x, y)..][..4]
                            .copy_from_slice(&[255, x as u8, y as u8, 17]);
                    }
                }
                unsafe {
                    padding(image.as_mut_ptr(), w, h, [wrap; 2]);
                }
                for y in 0..th {
                    for x in 0..tw {
                        let sx = if x < w {
                            x
                        } else {
                            padding_source(x, w, tw, wrap)
                        };
                        let sy = if y < h {
                            y
                        } else {
                            padding_source(y, h, th, wrap)
                        };
                        assert_eq!(
                            &image[offset(tw, th, x, y)..][..4],
                            &[255, sx as u8, sy as u8, 17]
                        );
                    }
                }
            }
        }
        assert_eq!(padding_source(12, 12, 16, 0x2901), 0);
        assert_eq!(padding_source(15, 12, 16, 0x2901), 11);
        assert_eq!(padding_source(12, 12, 16, 0x8370), 11);
        assert_eq!(padding_source(15, 12, 16, 0x8370), 8);
    }

    #[test]
    fn mip_chain_sizes_box_filter_and_rgb565_rounding() {
        assert_eq!(max_stored(8, 8), 0);
        assert_eq!(max_stored(64, 16), 1);
        assert_eq!(level_size(5, 3), 1);
        assert_eq!(level_offset(16, 16, 2, 4), 16 * 16 * 4 + 8 * 8 * 4);
        assert_eq!(level_offset(16, 16, 2, 2), 16 * 16 * 2 + 8 * 8 * 2);
        // 16x16 RGBA8, 2x2 blocks of 0/4/8/9 average to (sum + 2) / 4 = 5.
        let mut chain = alloc::vec![0u8; level_offset(16, 16, 2, 4)];
        for y in 0..16 {
            for x in 0..16 {
                let v = [0u8, 4][x & 1] + [0u8, 8][y & 1];
                chain[offset(16, 16, x, y)..][..4].copy_from_slice(&[v, v, v, 200]);
            }
        }
        unsafe { generate(chain.as_mut_ptr(), 16, 16, 2, 16, 16, false, [0x2901; 2]) };
        let base = level_offset(16, 16, 1, 4);
        assert_eq!(&chain[base + offset(8, 8, 3, 5)..][..4], &[6, 6, 6, 200]);
        // RGB565: white and blue average at 8-bit precision, then repack.
        assert_eq!(unpack565(0xffff), [255; 3]);
        assert_eq!(unpack565(0x001f), [0, 0, 255]);
        assert_eq!(pack565([128, 128, 255]), (16 << 11) | (32 << 5) | 31);
        let mut packed = alloc::vec![0u8; level_offset(16, 16, 2, 2)];
        for y in 0..16 {
            for x in 0..16 {
                let v: u16 = if (x + y) % 2 == 0 { 0xffff } else { 0x001f };
                unsafe { put565(packed.as_mut_ptr(), 16, 16, x, y, v) };
            }
        }
        unsafe { generate(packed.as_mut_ptr(), 16, 16, 2, 16, 16, true, [0x2901; 2]) };
        let level1 = unsafe { packed.as_ptr().add(level_offset(16, 16, 1, 2)) };
        assert_eq!(
            unsafe { get565(level1, 8, 8, 2, 2) },
            (16 << 11) | (32 << 5) | 31
        );
    }

    #[test]
    fn tiled_layout_channels_orientation_and_unpack_stride() {
        let mut image = [0u8; 16 * 8 * 4];
        let mut seen = [false; 128];
        for y in 0..8 {
            for x in 0..16 {
                let o = offset(16, 8, x, y) / 4;
                assert!(!seen[o]);
                seen[o] = true;
            }
        }

        assert!(seen.iter().all(|v| *v));
        assert_eq!(offset(8, 8, 0, 0), 42 * 4);
        assert_eq!(offset(8, 8, 0, 7), 0);

        let pixels = [1, 2, 3, 99, 4, 5, 6, 99];
        unsafe {
            upload(
                image.as_mut_ptr(),
                16,
                8,
                9,
                2,
                1,
                2,
                pixels.as_ptr(),
                3,
                4,
                false,
            );
        }

        assert_eq!(&image[offset(16, 8, 9, 2)..][..4], &[255, 3, 2, 1]);
        assert_eq!(&image[offset(16, 8, 9, 3)..][..4], &[255, 6, 5, 4]);

        for v in [0, 1, 7, 8, 9, 12, 16, 1024] {
            assert!(dimension(v));
        }

        for v in [-1, 1025, 2048] {
            assert!(!dimension(v));
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn glTexEnvi(target: u32, pname: u32, value: i32) {
    api!(|t: &mut Textures| {
        if target != 0x2300 {
            return Err(0x0500);
        }
        let value = value as u32;
        let env = &mut t.units[t.active].env;
        match pname {
            0x2200 if matches!(value, 0x2100 | 0x1e01 | 0x104 | 0x2101 | 0xbe2 | 0x8570) => {
                env.mode = value
            }
            0x8571
                if matches!(
                    value,
                    0x1e01 | 0x2100 | 0x104 | 0x8574 | 0x8575 | 0x84e7 | 0x86ae | 0x86af
                ) =>
            {
                env.functions[0] = value
            }
            0x8572 if matches!(value, 0x1e01 | 0x2100 | 0x104 | 0x8574 | 0x8575 | 0x84e7) => {
                env.functions[1] = value
            }
            0x8580..=0x8582 | 0x8588..=0x858a
                if matches!(value, 0x1702 | 0x8576 | 0x8577 | 0x8578 | 0x84c0..=0x84c2) =>
            {
                env.sources[(pname >= 0x8588) as usize][(pname & 3) as usize] = value;
            }
            0x8590..=0x8592 if matches!(value, 0x300..=0x303) => {
                env.operands[0][(pname & 3) as usize] = value
            }
            0x8598..=0x859a if matches!(value, 0x302 | 0x303) => {
                env.operands[1][(pname & 3) as usize] = value
            }
            0x8573 | 0x0d1c => {
                if !matches!(value, 1 | 2 | 4) {
                    return Err(0x0501);
                }
                env.scale[(pname == 0x0d1c) as usize] = value;
            }
            _ => return Err(0x0500),
        }
        t.dirty = true;
        Ok(())
    });
}
#[no_mangle]
pub unsafe extern "C" fn glTexEnvf(target: u32, pname: u32, value: f32) {
    if matches!(pname, 0x8573 | 0x0d1c) && !matches!(value, 1. | 2. | 4.) {
        with_frame(|_, m| m.fail(0x0501));
        return;
    }
    glTexEnvi(target, pname, value as i32);
}
#[no_mangle]
pub unsafe extern "C" fn glTexEnvfv(target: u32, pname: u32, values: *const f32) {
    if values.is_null() {
        with_frame(|_, m| m.fail(0x0501));
        return;
    }
    if pname != 0x2201 {
        glTexEnvf(target, pname, ptr::read_unaligned(values));
        return;
    }
    api!(|t: &mut Textures| {
        if target != 0x2300 {
            return Err(0x0500);
        }
        let mut color = 0;
        for i in 0..4 {
            color |= (crate::immediate::gpu_color_component(ptr::read_unaligned(values.add(i)))
                as u32)
                << (i * 8);
        }
        t.units[t.active].env.color = color;
        t.dirty = true;
        Ok(())
    });
}
#[no_mangle]
pub unsafe extern "C" fn glGetTexEnviv(target: u32, pname: u32, out: *mut i32) {
    api!(|t: &mut Textures| {
        if target != 0x2300 {
            return Err(0x0500);
        }
        if out.is_null() {
            return Err(0x0501);
        }
        let e = t.units[t.active].env;
        let v = match pname {
            0x2200 => e.mode,
            0x8571 => e.functions[0],
            0x8572 => e.functions[1],
            0x8580..=0x8582 | 0x8588..=0x858a => {
                e.sources[(pname >= 0x8588) as usize][(pname & 3) as usize]
            }
            0x8590..=0x8592 | 0x8598..=0x859a => {
                e.operands[(pname >= 0x8598) as usize][(pname & 3) as usize]
            }
            0x8573 => e.scale[0],
            0x0d1c => e.scale[1],
            _ => return Err(0x0500),
        };
        out.write(v as i32);
        Ok(())
    });
}

// Called from the renderer's single global-context test to avoid parallel mocks.
#[cfg(test)]
pub(crate) unsafe fn lifecycle(mut fail_next: impl FnMut()) {
    use crate::{blend::*, frame::*, immediate::*, matrix::glGetError, raster::*, renderer::*};
    let mut names = [0; 2];
    glGenTextures(2, names.as_mut_ptr());
    assert_ne!(names[0], names[1]);
    assert_eq!(glIsTexture(names[0]), 0);
    glBindTexture(TARGET, names[0]);
    assert_eq!(glIsTexture(names[0]), 1);
    let pixels = [17u8; 8 * 8 * 4];
    glTexImage2D(
        TARGET,
        0,
        0x1908,
        8,
        8,
        0,
        0x1908,
        0x1401,
        pixels.as_ptr().cast(),
    );
    with_frame(|s, _| assert!(!s.textures.packet().complete));
    glTexParameteri(TARGET, 0x2801, 0x2600);
    glTexParameteri(TARGET, 0x2800, 0x2600);
    glTexParameteri(TARGET, 0x2802, 0x812f);
    glTexParameteri(TARGET, 0x2803, 0x8370);
    glEnable(TARGET);
    assert_eq!(glIsEnabled(TARGET), 1);
    with_frame(|s, _| {
        let p = s.textures.packet();
        assert!(p.complete);
        assert_eq!(p.dimensions, 0x00080008);
        assert_eq!(p.parameters, 0x300);
    });
    glEnable(0x0be2);
    glBlendFuncSeparate(0x302, 0x303, 1, 0x303);
    glBlendEquationSeparate(0x8006, 0x800b);
    glBlendColor(0.25, 0.5, 0.75, 1.);
    let mut value = 0;
    glGetIntegerv(0x80cb, &mut value);
    assert_eq!(value, 1);
    glBlendFunc(0xffff, 1);
    assert_eq!(glGetError(), 0x0500);
    glGetIntegerv(0x80c9, &mut value);
    assert_eq!(value, 0x302);
    assert_eq!(c3dglNativeDraw(0, 3), 0);
    let mut old = ptr::null_mut();
    with_frame(|s, _| {
        old = s
            .textures
            .objects
            .iter()
            .find(|o| o.name == names[0])
            .unwrap()
            .storage
            .as_ref()
            .unwrap()
            .pointer
    });
    let patch = [1u8, 2, 3, 4];
    fail_next();
    glTexSubImage2D(TARGET, 0, 2, 3, 1, 1, 0x1908, 0x1401, patch.as_ptr().cast());
    assert_eq!(glGetError(), 0x0505);
    with_frame(|s, _| {
        assert_eq!(
            s.textures
                .objects
                .iter()
                .find(|o| o.name == names[0])
                .unwrap()
                .storage
                .as_ref()
                .unwrap()
                .pointer,
            old
        )
    });
    glTexSubImage2D(TARGET, 0, 2, 3, 1, 1, 0x1908, 0x1401, patch.as_ptr().cast());
    with_frame(|s, _| {
        let t = &s.textures;
        let o = t.objects.iter().find(|o| o.name == names[0]).unwrap();
        let now = o.storage.as_ref().unwrap().pointer;
        assert_ne!(now, old);
        assert_eq!(t.retired.len(), 1);
        assert_eq!(
            core::slice::from_raw_parts(old.add(offset(8, 8, 2, 3)), 4),
            &[17; 4]
        );
        assert_eq!(
            core::slice::from_raw_parts(now.add(offset(8, 8, 2, 3)), 4),
            &[4, 3, 2, 1]
        );
        assert_eq!(
            core::slice::from_raw_parts(now.add(offset(8, 8, 1, 3)), 4),
            &[17; 4]
        );
    });
    assert_eq!(c3dglNativeDraw(0, 3), 0);
    glTexSubImage2D(TARGET, 0, 7, 7, 2, 1, 0x1908, 0x1401, patch.as_ptr().cast());
    assert_eq!(glGetError(), 0x0501);
    glBegin(4);
    glBindTexture(TARGET, names[1]);
    assert_eq!(glGetError(), 0x0502);
    glActiveTexture(0x84c0);
    assert_eq!(glGetError(), 0x0502);
    glBlendFunc(1, 1);
    assert_eq!(glGetError(), 0x0502);
    glEnd();
    // Deletion cannot free an image referenced by previously queued draws.
    glDeleteTextures(1, names.as_ptr());
    assert_eq!(glIsTexture(names[0]), 0);
    with_frame(|s, _| {
        assert_eq!(s.textures.units[s.textures.active].bound, 0);
        assert_eq!(s.textures.retired.len(), 2);
    });
    assert_eq!(c3dglNativePresent(), 0);
    with_frame(|s, _| assert_eq!(s.textures.retired.len(), 2));
    assert_eq!(c3dglNativeBegin(0xff, false), 0);
    with_frame(|s, _| assert!(s.textures.retired.is_empty()));
    // RGB storage must discard alpha in RGBA subimage sources.
    glBindTexture(TARGET, names[1]);
    glTexImage2D(
        TARGET,
        0,
        0x1907,
        8,
        8,
        0,
        0x1908,
        0x1401,
        pixels.as_ptr().cast(),
    );
    glTexSubImage2D(TARGET, 0, 0, 0, 1, 1, 0x1908, 0x1401, patch.as_ptr().cast());
    with_frame(|s, _| {
        let t = &s.textures;
        let o = t.objects.iter().find(|o| o.name == names[1]).unwrap();
        let p = o.storage.as_ref().unwrap().pointer.add(offset(8, 8, 0, 0));
        assert_eq!(core::slice::from_raw_parts(p, 4), &[255, 3, 2, 1]);
    });
    let npot = [23u8; 12 * 12 * 4];
    glTexImage2D(
        TARGET,
        0,
        0x1908,
        12,
        12,
        0,
        0x1908,
        0x1401,
        npot.as_ptr().cast(),
    );
    glTexParameteri(TARGET, 0x2801, 0x2600);
    with_frame(|s, _| {
        let packet = s.textures.packet();
        assert_eq!(packet.dimensions, 0x00100010);
        assert_eq!(packet.scale, [0.75; 2]);
        s.textures.sampled();
    });
    glTexSubImage2D(TARGET, 0, 0, 0, 1, 1, 0x1908, 0x1401, patch.as_ptr().cast());
    with_frame(|s, _| {
        let o = s
            .textures
            .objects
            .iter()
            .find(|o| o.name == names[1])
            .unwrap();
        assert_eq!((o.width, o.height), (12, 12));
        let p = o
            .storage
            .as_ref()
            .unwrap()
            .pointer
            .add(offset(16, 16, 12, 12));
        assert_eq!(core::slice::from_raw_parts(p, 4), &[4, 3, 2, 1]);
        s.textures.sampled();
    });
    fail_next();
    glTexParameteri(TARGET, 0x2802, 0x812f);
    assert_eq!(glGetError(), 0x0505);
    glGetTexParameteriv(TARGET, 0x2802, &mut value);
    assert_eq!(value, 0x2901);
    glTexParameteri(TARGET, 0x2802, 0x812f);
    with_frame(|s, _| {
        let o = s
            .textures
            .objects
            .iter()
            .find(|o| o.name == names[1])
            .unwrap();
        let p = o
            .storage
            .as_ref()
            .unwrap()
            .pointer
            .add(offset(16, 16, 12, 12));
        assert_eq!(core::slice::from_raw_parts(p, 4), &[23; 4]);
    });
    glDeleteTextures(2, names.as_ptr());
    glDisable(TARGET);
    glDisable(0x0be2);
    assert_eq!(c3dglNativePresent(), 0);
    assert_eq!(glGetError(), 0);
}
