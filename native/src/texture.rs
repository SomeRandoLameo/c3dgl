//! One texture unit, level-zero RGB/RGBA uploads into resident Morton-tiled RGBA8.
use crate::{
    platform::gpu as p,
    renderer::{with_frame, Buffer},
};
use alloc::vec::Vec;
use core::{ffi::c_void, ptr};

const TARGET: u32 = 0x0de1;
const TARGET_1D: u32 = 0x0de0;
const LIMIT: usize = 64;

/// Texel storage of a texture chain.
#[derive(Clone, Copy, PartialEq, Eq, Default, Debug)]
enum Fmt {
    /// Resident RGBA8 (also used for RGB, luminance and alpha data).
    #[default]
    Rgba8,
    Rgb565,
    Rgba4,
    Rgba5551,
    /// ETC1 blocks, sampled natively.
    Etc1,
}

impl Fmt {
    fn bpp(self) -> usize {
        match self {
            Fmt::Rgba8 => 4,
            Fmt::Etc1 => 0,
            _ => 2,
        }
    }

    fn packed(self) -> bool {
        matches!(self, Fmt::Rgb565 | Fmt::Rgba4 | Fmt::Rgba5551)
    }

    /// PICA texture format register value.
    fn gpu(self) -> u32 {
        match self {
            Fmt::Rgba8 => 0,
            Fmt::Rgba5551 => 2,
            Fmt::Rgb565 => 3,
            Fmt::Rgba4 => 4,
            Fmt::Etc1 => 12,
        }
    }

    fn level_bytes(self, w: usize, h: usize) -> usize {
        if self == Fmt::Etc1 {
            w * h / 2
        } else {
            w * h * self.bpp()
        }
    }
}

/// GL definition of a mipmap level above 0 (level 0 lives in `Object`).
#[derive(Clone, Copy, Default)]
struct Level {
    defined: bool,
    width: usize,
    height: usize,
    base: u32,
    internal: u32,
    fmt: Fmt,
}

struct Object {
    name: u32,
    /// A GL_TEXTURE_1D object: stored square with every row holding the image.
    one_d: bool,
    instantiated: bool,
    storage: Option<Buffer>,
    width: usize,
    height: usize,
    min: u32,
    mag: u32,
    wrap: [u32; 2],
    used: bool,
    base: u32,
    /// The internal format as given by the application.
    internal: u32,
    /// Stored levels: level l of the chain is `stored(width) >> l` wide, minimum 8.
    levels: usize,
    defs: [Level; 12],
    generate: bool,
    fmt: Fmt,
}

impl Object {
    fn new(name: u32, one_d: bool, instantiated: bool) -> Self {
        Self {
            name,
            one_d,
            instantiated,
            storage: None,
            width: 0,
            height: 0,
            min: 0x2702,
            mag: 0x2601,
            wrap: [0x2901; 2],
            used: false,
            base: 0x1908,
            internal: 0x1908,
            levels: 1,
            defs: [Level::default(); 12],
            generate: false,
            fmt: Fmt::Rgba8,
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
                && d.fmt == self.fmt)
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
    /// ETC1 is stored upside down: the texture matrix maps t to q - t.
    pub flip: bool,
    /// 1D texture: the texture matrix maps t to s.
    pub one_d: bool,
}

#[derive(Clone, Copy)]
struct Unit {
    enabled: bool,
    bound: u32,
    /// GL_TEXTURE_1D: its own enable flag and binding.
    enabled1: bool,
    bound1: u32,
    env: Environment,
}
impl Unit {
    fn new() -> Self {
        Self {
            enabled: false,
            bound: 0,
            enabled1: false,
            bound1: 0,
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
    /// Texturing is off for an internal draw (the clear quad).
    pub suspended: bool,
    proxy2: [Proxy; 12],
    proxy1: [Proxy; 12],
    unpack: PixelStore,
    pack: PixelStore,
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
            suspended: false,
            proxy2: [Proxy::default(); 12],
            proxy1: [Proxy::default(); 12],
            unpack: PixelStore::default(),
            pack: PixelStore::default(),
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
        self.index_for(false)
    }

    /// The bound object of the active unit for GL_TEXTURE_2D or GL_TEXTURE_1D.
    fn index_for(&mut self, one_d: bool) -> Result<usize, u32> {
        let unit = &self.units[self.active];
        let name = if one_d { unit.bound1 } else { unit.bound };
        if let Some(i) = self.find(name, one_d) {
            return Ok(i);
        }

        self.insert(name, one_d, true)
    }

    fn find(&self, name: u32, one_d: bool) -> Option<usize> {
        self.objects
            .iter()
            .position(|o| o.name == name && o.one_d == one_d)
    }

    /// The texture a unit samples: GL_TEXTURE_2D takes precedence over GL_TEXTURE_1D.
    fn sampling(&self, unit: usize) -> Option<(u32, bool)> {
        let u = &self.units[unit];
        if u.enabled {
            Some((u.bound, false))
        } else if u.enabled1 {
            Some((u.bound1, true))
        } else {
            None
        }
    }

    fn insert(&mut self, name: u32, one_d: bool, instantiated: bool) -> Result<usize, u32> {
        if self.objects.len() >= LIMIT {
            return Err(0x0505);
        }

        self.objects.try_reserve(1).map_err(|_| 0x0505u32)?;
        self.objects.push(Object::new(name, one_d, instantiated));

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
    pub fn enabled_1d(&self) -> bool {
        self.units[self.active].enabled1
    }
    pub fn set_enabled_1d(&mut self, on: bool) {
        if self.units[self.active].enabled1 != on {
            self.units[self.active].enabled1 = on;
            self.dirty = true;
        }
    }

    pub fn set_enabled(&mut self, on: bool) {
        if self.enabled() != on {
            self.units[self.active].enabled = on;
            self.dirty = true;
        }
    }

    unsafe fn packet_for(&self, unit: usize) -> Packet {
        let sampling = self.sampling(unit);
        let mut s = Packet {
            enabled: sampling.is_some(),
            complete: false,
            address: 0,
            dimensions: 0,
            parameters: 0,
            replace: self.units[unit].env.mode == 0x1e01,
            scale: [1.; 2],
            tev: [0; 5],
            lod: 0,
            format: 0,
            flip: false,
            one_d: false,
        };

        let object = sampling
            .and_then(|(name, one_d)| self.find(name, one_d))
            .map(|i| &self.objects[i]);
        if let Some(o) = object {
            s.complete = o.complete();
            s.one_d = o.one_d;
            if s.complete {
                s.address = o.storage.as_ref().unwrap().physical();
                let (tw, th) = (stored(o.width), stored(o.height));
                s.dimensions = (tw as u32) << 16 | th as u32;
                s.scale = [o.width as f32 / tw as f32, o.height as f32 / th as f32];
                let wrap = |v| match v {
                    0x812f | 0x2900 => 0,
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
                s.format = o.fmt.gpu();
                s.flip = o.fmt == Fmt::Etc1;
                s.lod = if mip_filter(o.min) {
                    (o.levels as u32 - 1) << 16
                } else {
                    0
                };
            }
        }

        // An incomplete or missing texture disables the unit, as in the C renderer.
        if !s.complete || self.suspended {
            s.enabled = false;
        }
        let base = object.map_or(0x1908, |o| o.base);
        s.tev = self.units[unit]
            .env
            .words(unit, s.enabled, s.complete, base);
        s
    }

    pub fn sampled(&mut self) {
        for unit in 0..3 {
            if let Some((name, one_d)) = self.sampling(unit) {
                if let Some(i) = self.find(name, one_d) {
                    if self.objects[i].complete() {
                        self.objects[i].used = true;
                    }
                }
            }
        }
    }

    pub fn query(&self, pname: u32) -> Option<([f64; 16], usize)> {
        let mut v = [0.; 16];
        v[0] = match pname {
            0x0de1 => self.units[self.active].enabled as u8 as f64,
            0x0de0 => self.units[self.active].enabled1 as u8 as f64,
            0x8069 => self.units[self.active].bound as f64,
            0x8068 => self.units[self.active].bound1 as f64,
            0x0cf0 | 0x0cf1 | 0x0d00 | 0x0d01 => 0.,
            0x0cf2 => self.unpack.row_length as f64,
            0x0cf3 => self.unpack.skip_rows as f64,
            0x0cf4 => self.unpack.skip_pixels as f64,
            0x0cf5 => self.unpack.alignment as f64,
            0x0d02 => self.pack.row_length as f64,
            0x0d03 => self.pack.skip_rows as f64,
            0x0d04 => self.pack.skip_pixels as f64,
            0x0d05 => self.pack.alignment as f64,
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

/// GL_TEXTURE_2D (false) or GL_TEXTURE_1D (true).
fn target_dims(t: u32) -> Result<bool, u32> {
    match t {
        TARGET => Ok(false),
        TARGET_1D => Ok(true),
        _ => Err(0x0500),
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
    if (t == 0x8363 && f == 0x1907) || (matches!(t, 0x8033 | 0x8034) && f == 0x1908) {
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

fn level_offset(tw: usize, th: usize, level: usize, fmt: Fmt) -> usize {
    (0..level).map(|k| fmt.level_bytes(tw >> k, th >> k)).sum()
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

/// C `pack16`: 8-bit RGBA components to a 16-bit texel of `fmt`.
fn pack16(fmt: Fmt, c: [u32; 4]) -> u16 {
    let v = match fmt {
        Fmt::Rgb565 => {
            ((c[0] * 31 + 127) / 255) << 11
                | ((c[1] * 63 + 127) / 255) << 5
                | ((c[2] * 31 + 127) / 255)
        }
        Fmt::Rgba5551 => {
            ((c[0] * 31 + 127) / 255) << 11
                | ((c[1] * 31 + 127) / 255) << 6
                | ((c[2] * 31 + 127) / 255) << 1
                | (c[3] >= 128) as u32
        }
        _ => {
            ((c[0] * 15 + 127) / 255) << 12
                | ((c[1] * 15 + 127) / 255) << 8
                | ((c[2] * 15 + 127) / 255) << 4
                | ((c[3] * 15 + 127) / 255)
        }
    };
    v as u16
}

/// C `unpack16`: a 16-bit texel of `fmt` to 8-bit RGBA components.
fn unpack16(fmt: Fmt, v: u16) -> [u32; 4] {
    let v = v as u32;
    match fmt {
        Fmt::Rgb565 => [
            (v >> 11) * 255 / 31,
            ((v >> 5) & 63) * 255 / 63,
            (v & 31) * 255 / 31,
            255,
        ],
        Fmt::Rgba5551 => [
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

unsafe fn put16(out: *mut u8, tw: usize, th: usize, x: usize, y: usize, v: u16) {
    out.add(offset_bpp(tw, th, x, y, 2))
        .cast::<u16>()
        .write_unaligned(v);
}

unsafe fn get16(out: *const u8, tw: usize, th: usize, x: usize, y: usize) -> u16 {
    out.add(offset_bpp(tw, th, x, y, 2))
        .cast::<u16>()
        .read_unaligned()
}

/// Where the texels of an image come from.
#[derive(Clone, Copy)]
enum Source<'a> {
    /// Client data in GL format `f` and type `ty`, `bpp` bytes per pixel.
    Gl {
        pixels: *const u8,
        f: u32,
        ty: u32,
        bpp: usize,
        alignment: usize,
        /// Pixels per source row (0: the image width).
        row: usize,
    },
    /// Tightly packed RGBA bytes (expanded palette indices).
    Rgba(&'a [u8]),
    /// ETC1 blocks, row-major from t = 0.
    Etc1(&'a [u8]),
}

/// One GL pixel as 8-bit RGBA (C `unpackColorImage`).
unsafe fn read_rgba(p: *const u8, f: u32, ty: u32) -> [u32; 4] {
    let at = |i: usize| p.add(i).read() as u32;
    match ty {
        0x8363 => unpack16(Fmt::Rgb565, u16::from_le_bytes([p.read(), p.add(1).read()])),
        0x8033 => unpack16(Fmt::Rgba4, u16::from_le_bytes([p.read(), p.add(1).read()])),
        0x8034 => unpack16(
            Fmt::Rgba5551,
            u16::from_le_bytes([p.read(), p.add(1).read()]),
        ),
        _ => match f {
            0x1907 => [at(0), at(1), at(2), 255],
            0x1908 => [at(0), at(1), at(2), at(3)],
            0x1909 => [at(0), at(0), at(0), 255],
            0x190a => [at(0), at(0), at(0), at(1)],
            _ => [0, 0, 0, at(0)],
        },
    }
}

/// ETC1 blocks into a stored level: PICA groups 4x4 blocks into 8x8 tiles (Z order) and reads
/// each block as a little-endian u64, so the bytes are reversed. The rows stay upside down;
/// the texture matrix flips t instead.
unsafe fn upload_etc1(out: *mut u8, tw: usize, w: usize, h: usize, data: &[u8]) {
    let (bx, by) = ((w + 3) / 4, (h + 3) / 4);
    for y in 0..by {
        for x in 0..bx {
            let (px, py) = (x * 4, y * 4);
            let dst = out.add(
                ((py >> 3) * (tw >> 3) + (px >> 3)) * 32 + (((px & 4) >> 2) | ((py & 4) >> 1)) * 8,
            );
            for i in 0..8 {
                dst.add(i).write(data[(y * bx + x) * 8 + 7 - i]);
            }
        }
    }
}

/// Write the image `src` of `w` x `h` at (`x0`, `y0`) of a level region stored `tw` x `th`.
unsafe fn fill_level(
    region: *mut u8,
    tw: usize,
    th: usize,
    x0: usize,
    y0: usize,
    w: usize,
    h: usize,
    fmt: Fmt,
    base: u32,
    src: Source,
) {
    match (fmt, src) {
        (Fmt::Etc1, Source::Etc1(data)) => upload_etc1(region, tw, w, h, data),
        (
            Fmt::Rgba8,
            Source::Gl {
                pixels,
                f,
                ty,
                bpp,
                alignment,
                row,
            },
        ) if ty != 0x1401 => {
            // Packed client data into an RGBA8 chain: through 8-bit components.
            if pixels.is_null() {
                return;
            }
            let stride = (row_of(row, w) * bpp + alignment - 1) & !(alignment - 1);
            for y in 0..h {
                for x in 0..w {
                    let c = read_rgba(pixels.add(y * stride + x * bpp), f, ty);
                    let d = region.add(offset(tw, th, x0 + x, y0 + y));
                    d.write(c[3] as u8);
                    d.add(1).write(c[2] as u8);
                    d.add(2).write(c[1] as u8);
                    d.add(3).write(c[0] as u8);
                }
            }
            convert_base(region, tw, th, x0, y0, w, h, base);
        }
        (
            Fmt::Rgba8,
            Source::Gl {
                pixels,
                f,
                bpp,
                alignment,
                row,
                ..
            },
        ) => {
            if !pixels.is_null() {
                upload(
                    region,
                    tw,
                    th,
                    x0,
                    y0,
                    w,
                    h,
                    pixels,
                    bpp,
                    alignment,
                    row,
                    f == 0x1906,
                );
            }
            convert_base(region, tw, th, x0, y0, w, h, base);
        }
        (
            _,
            Source::Gl {
                pixels,
                f,
                ty,
                bpp,
                alignment,
                row,
            },
        ) if fmt.packed() => {
            if pixels.is_null() {
                return;
            }
            let stride = (row_of(row, w) * bpp + alignment - 1) & !(alignment - 1);
            for y in 0..h {
                for x in 0..w {
                    let c = read_rgba(pixels.add(y * stride + x * bpp), f, ty);
                    put16(region, tw, th, x0 + x, y0 + y, pack16(fmt, c));
                }
            }
        }
        (_, Source::Rgba(data)) if fmt != Fmt::Etc1 => {
            for y in 0..h {
                for x in 0..w {
                    let s = &data[(y * w + x) * 4..][..4];
                    if fmt == Fmt::Rgba8 {
                        let d = region.add(offset(tw, th, x0 + x, y0 + y));
                        d.write(s[3]);
                        d.add(1).write(s[2]);
                        d.add(2).write(s[1]);
                        d.add(3).write(s[0]);
                    } else {
                        let c = [s[0] as u32, s[1] as u32, s[2] as u32, s[3] as u32];
                        put16(region, tw, th, x0 + x, y0 + y, pack16(fmt, c));
                    }
                }
            }
        }
        _ => {}
    }
}

/// GL_GENERATE_MIPMAP: every stored level from level 0 with a 2x2 box filter
/// (`(sum + 2) / 4`; packed textures averaged at 8-bit precision and repacked).
unsafe fn generate(
    out: *mut u8,
    tw: usize,
    th: usize,
    levels: usize,
    w: usize,
    h: usize,
    fmt: Fmt,
    wrap: [u32; 2],
) {
    if fmt == Fmt::Etc1 {
        return;
    }
    let bpp = fmt.bpp();
    for l in 1..levels {
        let (sw, sh) = (level_size(w, l - 1), level_size(h, l - 1));
        let (dw, dh) = (level_size(w, l), level_size(h, l));
        let (stw, sth) = (tw >> (l - 1), th >> (l - 1));
        let (dtw, dth) = (tw >> l, th >> l);
        let src = out.add(level_offset(tw, th, l - 1, fmt));
        let dst = out.add(level_offset(tw, th, l, fmt));
        for y in 0..dh.min(dth) {
            for x in 0..dw.min(dtw) {
                let xs = [2 * x, if 2 * x + 1 < sw { 2 * x + 1 } else { 2 * x }];
                let ys = [2 * y, if 2 * y + 1 < sh { 2 * y + 1 } else { 2 * y }];
                let mut sum = [0u32; 4];
                for i in 0..4 {
                    let (sx, sy) = (xs[i & 1], ys[i >> 1]);
                    if fmt.packed() {
                        let c = unpack16(fmt, get16(src, stw, sth, sx, sy));
                        for k in 0..4 {
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
                if fmt.packed() {
                    put16(dst, dtw, dth, x, y, pack16(fmt, avg));
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
    row: usize,
    alpha_only: bool,
) {
    let stride = (row_of(row, w) * bpp + alignment - 1) & !(alignment - 1);
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

/// The texels of a client image copied tightly packed (rows without padding), for display lists.
unsafe fn capture(pixels: *const c_void, w: i32, h: i32, f: u32, ty: u32) -> Option<Vec<u8>> {
    let bpp = format(f, ty).ok()?;
    if pixels.is_null() || !(0..=1024).contains(&w) || !(0..=1024).contains(&h) {
        return None;
    }
    let mut store = PixelStore::default();
    crate::renderer::with_immediate(|r| store = r.frame.textures.unpack());
    let (w, h) = (w as usize, h as usize);
    let pixels = store.start(pixels.cast(), bpp, w);
    let stride = (row_of(store.row_length, w) * bpp + store.alignment - 1) & !(store.alignment - 1);
    let mut out = Vec::new();
    out.try_reserve_exact(w * h * bpp).ok()?;
    for y in 0..h {
        out.extend_from_slice(core::slice::from_raw_parts(pixels.add(y * stride), w * bpp));
    }
    Some(out)
}

/// Set the unpack state; returns the previous one (display list execution).
pub(crate) unsafe fn set_unpack(store: PixelStore) -> PixelStore {
    let mut old = PixelStore::default();
    crate::renderer::with_immediate(|r| {
        old = r.frame.textures.unpack();
        r.frame.textures.set_unpack(store);
    });
    old
}

/// Pixel storage modes of one direction (GL_UNPACK_* / GL_PACK_*). Byte swapping and LSB-first
/// bit order are not supported and cannot be enabled.
#[derive(Clone, Copy)]
pub(crate) struct PixelStore {
    pub alignment: usize,
    pub row_length: usize,
    pub skip_rows: usize,
    pub skip_pixels: usize,
}

impl Default for PixelStore {
    fn default() -> Self {
        Self {
            alignment: 4,
            row_length: 0,
            skip_rows: 0,
            skip_pixels: 0,
        }
    }
}

impl PixelStore {
    /// First pixel of an image of width `w` (GL_UNPACK_SKIP_ROWS / SKIP_PIXELS applied).
    unsafe fn start(&self, pixels: *const u8, bpp: usize, w: usize) -> *const u8 {
        if pixels.is_null() {
            return pixels;
        }
        let stride =
            (row_of(self.row_length, w) * bpp + self.alignment - 1) & !(self.alignment - 1);
        pixels.add(self.skip_rows * stride + self.skip_pixels * bpp)
    }
}

fn row_of(row: usize, w: usize) -> usize {
    if row == 0 {
        w
    } else {
        row
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
    if crate::lists::record(crate::lists::Cmd::ActiveTexture(unit)) {
        return;
    }
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
            t.objects.push(Object::new(name, false, false));
            out.add(i).write(name);
        }

        Ok(())
    });
}

#[no_mangle]
pub unsafe extern "C" fn glBindTexture(kind: u32, name: u32) {
    if crate::lists::record(crate::lists::Cmd::BindTexture(kind, name)) {
        return;
    }
    api!(|t: &mut Textures| {
        let one_d = target_dims(kind)?;
        // Generated names are bound for the first time to one target; the default texture 0 exists
        // once per target.
        let i = match t.objects.iter().position(|o| o.name == name) {
            Some(i) if name != 0 => {
                if t.objects[i].instantiated && t.objects[i].one_d != one_d {
                    return Err(0x0502);
                }
                t.objects[i].one_d = one_d;
                i
            }
            _ => match t.find(name, one_d) {
                Some(i) => i,
                None => t.insert(name, one_d, true)?,
            },
        };

        t.objects[i].instantiated = true;
        let unit = &mut t.units[t.active];
        let slot = if one_d {
            &mut unit.bound1
        } else {
            &mut unit.bound
        };
        if *slot != name {
            *slot = name;
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
                    if unit.bound == name && !o.one_d {
                        unit.bound = 0;
                        t.dirty = true;
                    }
                    if unit.bound1 == name && o.one_d {
                        unit.bound1 = 0;
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
        let store = match pname {
            0x0cf0..=0x0cf5 => &mut t.unpack,
            0x0d00..=0x0d05 => &mut t.pack,
            _ => return Err(0x0500),
        };
        match pname & 0xf {
            // GL_*_SWAP_BYTES and GL_*_LSB_FIRST: only the default (off) exists.
            0 | 1 => {
                if value != 0 {
                    return Err(0x0501);
                }
            }
            2..=4 => {
                if value < 0 {
                    return Err(0x0501);
                }
                let v = value as usize;
                match pname & 0xf {
                    2 => store.row_length = v,
                    3 => store.skip_rows = v,
                    _ => store.skip_pixels = v,
                }
            }
            _ => {
                if !matches!(value, 1 | 2 | 4 | 8) {
                    return Err(0x0501);
                }
                store.alignment = value as usize;
            }
        }
        Ok(())
    });
}
/// Chain buffer of `levels` levels for an image of `w` x `h`, optionally keeping the old contents.
unsafe fn chain_buffer(
    o: &Object,
    levels: usize,
    w: usize,
    h: usize,
    fmt: Fmt,
    keep: bool,
) -> Result<Buffer, u32> {
    let (tw, th) = (stored(w), stored(h));
    let bytes = level_offset(tw, th, levels, fmt);
    let buffer = Buffer::new(bytes, false).ok_or(0x0505u32)?;
    ptr::write_bytes(buffer.pointer, 0, bytes);
    if keep {
        if let Some(old) = &o.storage {
            let n = level_offset(stored(o.width), stored(o.height), o.levels, o.fmt).min(bytes);
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
            internal: o.internal,
            fmt: o.fmt,
        };
        l += 1;
    }
}

#[no_mangle]
pub unsafe extern "C" fn glTexParameteri(kind: u32, pname: u32, value: i32) {
    if crate::lists::record(crate::lists::Cmd::TexParameter(kind, pname, value)) {
        return;
    }
    api!(|t: &mut Textures| {
        let one_d = target_dims(kind)?;
        let value = value as u32;
        let valid = match pname {
            0x2801 => matches!(value, 0x2600 | 0x2601 | 0x2700..=0x2703),
            0x2800 => matches!(value, 0x2600 | 0x2601),
            0x2802 | 0x2803 => matches!(value, 0x2900 | 0x2901 | 0x812f | 0x8370),
            0x8191 => true,
            _ => false,
        };
        if !valid {
            return Err(0x0500);
        }
        let i = t.index_for(one_d)?;
        t.set_param(i, pname, value)
    });
}

/// What glPushAttrib keeps of a texture unit.
#[derive(Clone, Copy)]
pub(crate) struct UnitSave {
    enabled: bool,
    enabled1: bool,
    bound: u32,
    bound1: u32,
    env: Environment,
    /// min filter, mag filter, wrap s, wrap t, generate mipmap of the bound object.
    params: Option<[u32; 5]>,
}

#[derive(Clone, Copy)]
pub(crate) struct TexSave {
    units: [UnitSave; 3],
    active: usize,
}

impl Textures {
    pub fn unpack(&self) -> PixelStore {
        self.unpack
    }

    pub fn set_unpack(&mut self, store: PixelStore) {
        self.unpack = store;
    }

    pub fn pack(&self) -> PixelStore {
        self.pack
    }

    pub fn set_pack(&mut self, store: PixelStore) {
        self.pack = store;
    }

    pub fn save(&self) -> TexSave {
        TexSave {
            units: core::array::from_fn(|u| {
                let unit = &self.units[u];
                UnitSave {
                    enabled: unit.enabled,
                    enabled1: unit.enabled1,
                    bound: unit.bound,
                    bound1: unit.bound1,
                    env: unit.env,
                    params: self
                        .objects
                        .iter()
                        .find(|o| o.name == unit.bound)
                        .map(|o| [o.min, o.mag, o.wrap[0], o.wrap[1], o.generate as u32]),
                }
            }),
            active: self.active,
        }
    }

    /// Restore the enable flags and/or the bindings, sampler parameters, environments and the
    /// active unit of a saved state.
    pub unsafe fn restore(&mut self, save: &TexSave, enables: bool, state: bool) {
        for (u, saved) in save.units.iter().enumerate() {
            if enables {
                self.units[u].enabled = saved.enabled;
                self.units[u].enabled1 = saved.enabled1;
            }
            if !state {
                continue;
            }
            self.units[u].env = saved.env;
            let exists1 = self.objects.iter().any(|o| o.name == saved.bound1);
            self.units[u].bound1 = if exists1 { saved.bound1 } else { 0 };
            let exists = self.objects.iter().any(|o| o.name == saved.bound);
            self.units[u].bound = if exists || saved.bound == 0 {
                saved.bound
            } else {
                0
            };
            if let (Some(i), Some(p)) = (
                self.objects.iter().position(|o| o.name == saved.bound),
                saved.params,
            ) {
                for (pname, value) in [0x2801, 0x2800, 0x2802, 0x2803, 0x8191].into_iter().zip(p) {
                    let _ = self.set_param(i, pname, value);
                }
            }
        }
        if state {
            self.active = save.active;
        }
        self.dirty = true;
    }

    /// Set a sampler parameter of object `i` (GL_TEXTURE_MIN_FILTER .. GL_GENERATE_MIPMAP).
    unsafe fn set_param(&mut self, i: usize, pname: u32, value: u32) -> Result<(), u32> {
        let t = self;
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
            let fmt = o.fmt;
            if pname == 0x8191 && value != 0 && o.storage.is_some() && fmt != Fmt::Etc1 {
                // Enabling generation rebuilds every level from level 0.
                let levels = 1 + max_stored(tw, th);
                let storage = chain_buffer(o, levels, w, h, fmt, true)?;
                generate(storage.pointer, tw, th, levels, w, h, fmt, o.wrap);
                let bytes = level_offset(tw, th, levels, fmt);
                if p::c3dglPlatformFlush(storage.pointer, bytes as u32) < 0 {
                    return Err(0x0502);
                }
                t.replace(i, Some(storage), w, h)?;
                let o = &mut t.objects[i];
                o.levels = levels;
                define_generated(o);
            } else if matches!(pname, 0x2802 | 0x2803)
                && o.storage.is_some()
                && fmt != Fmt::Etc1
                && (0..o.levels).any(|l| level_size(w, l) != tw >> l || level_size(h, l) != th >> l)
            {
                let mut wrap = o.wrap;
                wrap[(pname == 0x2803) as usize] = value;
                let levels = o.levels;
                let storage = chain_buffer(o, levels, w, h, fmt, true)?;
                for l in 0..levels {
                    padding_in(
                        storage.pointer.add(level_offset(tw, th, l, fmt)),
                        level_size(w, l),
                        level_size(h, l),
                        tw >> l,
                        th >> l,
                        wrap,
                        fmt.bpp(),
                    );
                }
                if p::c3dglPlatformFlush(storage.pointer, level_offset(tw, th, levels, fmt) as u32)
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
    }
}

#[no_mangle]
pub unsafe extern "C" fn glGetTexParameteriv(kind: u32, pname: u32, out: *mut i32) {
    api!(|t: &mut Textures| {
        let one_d = target_dims(kind)?;
        if out.is_null() {
            return Err(0x0501);
        }
        if !matches!(pname, 0x2800..=0x2803 | 0x8191) {
            return Err(0x0500);
        }
        let i = t.index_for(one_d)?;
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

fn level_valid(level: i32, w: i32, h: i32) -> bool {
    (0..=10).contains(&level)
        && dimension(w)
        && dimension(h)
        && w <= 1024 >> level
        && h <= 1024 >> level
}

/// Define `level` of the bound texture and store its image (the shared part of glTexImage2D and
/// glCompressedTexImage2D). Levels above 0 keep the chain's format when the base format matches.
unsafe fn define_image(
    t: &mut Textures,
    level: usize,
    base: u32,
    internal: u32,
    fmt: Fmt,
    w: usize,
    h: usize,
    src: Source,
    one_d: bool,
) -> Result<(), u32> {
    let i = t.index_for(one_d)?;
    if level > 0 {
        // Levels before level 0 are ignored; levels below 8x8 only count for completeness.
        let o = &t.objects[i];
        if o.storage.is_none() || w == 0 || h == 0 {
            return Ok(());
        }
        let (tw, th) = (stored(o.width), stored(o.height));
        let fmt = match src {
            Source::Gl { .. } if base == o.base && o.fmt != Fmt::Etc1 => o.fmt,
            _ => fmt,
        };
        let fits = w == level_size(o.width, level)
            && h == level_size(o.height, level)
            && base == o.base
            && fmt == o.fmt;
        let o = &mut t.objects[i];
        o.defs[level] = Level {
            defined: true,
            width: w,
            height: h,
            base,
            internal,
            fmt,
        };
        if fits && level <= max_stored(tw, th) {
            let o = &t.objects[i];
            let levels = 1 + max_stored(tw, th);
            let storage = chain_buffer(o, levels, o.width, o.height, fmt, true)?;
            let region = storage.pointer.add(level_offset(tw, th, level, fmt));
            let (lw, lh) = (tw >> level, th >> level);
            ptr::write_bytes(region, 0, fmt.level_bytes(lw, lh));
            fill_level(region, lw, lh, 0, 0, w, h, fmt, base, src);
            if fmt != Fmt::Etc1 {
                padding_in(region, w, h, lw, lh, o.wrap, fmt.bpp());
            }
            if p::c3dglPlatformFlush(storage.pointer, level_offset(tw, th, levels, fmt) as u32) < 0
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
    let same =
        o.storage.is_some() && o.width == w && o.height == h && o.base == base && o.fmt == fmt;
    let generating = o.generate && fmt != Fmt::Etc1;
    let levels = if generating && max_stored(tw, th) > 0 {
        1 + max_stored(tw, th)
    } else if same {
        o.levels
    } else {
        1
    };
    let storage = chain_buffer(o, levels, w, h, fmt, same)?;
    ptr::write_bytes(storage.pointer, 0, fmt.level_bytes(tw, th));
    fill_level(storage.pointer, tw, th, 0, 0, w, h, fmt, base, src);
    if fmt != Fmt::Etc1 {
        padding_in(storage.pointer, w, h, tw, th, o.wrap, fmt.bpp());
    }
    if generating {
        generate(storage.pointer, tw, th, levels, w, h, fmt, o.wrap);
    }
    if p::c3dglPlatformFlush(storage.pointer, level_offset(tw, th, levels, fmt) as u32) < 0 {
        return Err(0x0502);
    }
    t.replace(i, Some(storage), w, h)?;
    let o = &mut t.objects[i];
    if !same {
        o.defs = [Level::default(); 12];
    }
    o.base = base;
    o.internal = internal;
    o.fmt = fmt;
    o.levels = levels;
    if generating {
        define_generated(o);
    }
    Ok(())
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
    tex_image(false, kind, level, internal, w, h, border, f, ty, pixels);
}

/// A 1D image is a w x 1 image; every row of the padded storage repeats it.
#[no_mangle]
pub unsafe extern "C" fn glTexImage1D(
    kind: u32,
    level: i32,
    internal: i32,
    w: i32,
    border: i32,
    f: u32,
    ty: u32,
    pixels: *const c_void,
) {
    tex_image(true, kind, level, internal, w, 1, border, f, ty, pixels);
}

unsafe fn tex_image(
    one_d: bool,
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
    if kind != PROXY_2D && kind != PROXY_1D && crate::lists::compiling() {
        let cmd = crate::lists::Cmd::TexImage {
            sub: false,
            target: kind,
            level,
            internal,
            x: 0,
            y: 0,
            w,
            h,
            border,
            f,
            ty,
            pixels: capture(pixels, w, h, f, ty),
        };
        if crate::lists::record(cmd) {
            return;
        }
    }
    api!(|t: &mut Textures| {
        if one_d {
            if kind != TARGET_1D && kind != PROXY_1D {
                return Err(0x0500);
            }
        } else if kind != PROXY_2D {
            target(kind)?;
        }
        let bpp = format(f, ty)?;
        let Some(base) = base_internal(internal) else {
            return Err(0x0501);
        };
        if border != 0 || !(0..=10).contains(&level) || !dimension(w) || !dimension(h) {
            return Err(0x0501);
        }
        let fmt = storage_format(internal as u32, base, ty);
        if kind == PROXY_2D || kind == PROXY_1D {
            // Only records whether the image would be accepted.
            let fits = w <= 1024 >> level && h <= 1024 >> level;
            let slot = if kind == PROXY_1D {
                &mut t.proxy1[level as usize]
            } else {
                &mut t.proxy2[level as usize]
            };
            *slot = if fits {
                Proxy {
                    width: w,
                    height: h,
                    internal: internal as u32,
                    base,
                    fmt,
                }
            } else {
                Proxy::default()
            };
            return Ok(());
        }
        if !level_valid(level, w, h) {
            return Err(0x0501);
        }
        let src = Source::Gl {
            pixels: t.unpack.start(pixels.cast(), bpp, w as usize),
            f,
            ty,
            bpp,
            alignment: t.unpack.alignment,
            row: t.unpack.row_length,
        };
        define_image(
            t,
            level as usize,
            base,
            internal as u32,
            fmt,
            w as usize,
            h as usize,
            src,
            one_d,
        )
    });
}

const PROXY_2D: u32 = 0x8064;
const PROXY_1D: u32 = 0x8063;

/// An image the application asked for with a proxy target.
#[derive(Clone, Copy, Default)]
struct Proxy {
    width: i32,
    height: i32,
    internal: u32,
    base: u32,
    fmt: Fmt,
}

/// Base internal format of a texture internal format (GL 1.1 tables 3.15 and 3.16); None for
/// intensity formats and anything else not supported.
fn base_internal(internal: i32) -> Option<u32> {
    Some(match internal as u32 {
        0x1906 | 0x803b..=0x803e => 0x1906,
        1 | 0x1909 | 0x803f..=0x8042 => 0x1909,
        2 | 0x190a | 0x8043..=0x8048 => 0x190a,
        3 | 0x1907 | 0x2a10 | 0x804f..=0x8054 => 0x1907,
        4 | 0x1908 | 0x8055..=0x805b => 0x1908,
        _ => return None,
    })
}

/// The storage that holds internal format `internal`: the closest PICA one for sized formats;
/// unsized RGB/RGBA keep packed 16-bit data in its format.
fn storage_format(internal: u32, base: u32, ty: u32) -> Fmt {
    let unsized_format = matches!(internal, 1..=4 | 0x1906 | 0x1907 | 0x1908 | 0x1909 | 0x190a);
    match base {
        0x1907
            if matches!(internal, 0x2a10 | 0x804f | 0x8050) || (unsized_format && ty == 0x8363) =>
        {
            Fmt::Rgb565
        }
        0x1908 if matches!(internal, 0x8055 | 0x8056) => Fmt::Rgba4,
        0x1908 if internal == 0x8057 => Fmt::Rgba5551,
        0x1908 if unsized_format && ty == 0x8033 => Fmt::Rgba4,
        0x1908 if unsized_format && ty == 0x8034 => Fmt::Rgba5551,
        _ => Fmt::Rgba8,
    }
}

/// Bits per channel (red, green, blue, alpha, luminance, intensity) of a storage.
fn channel_bits(fmt: Fmt, base: u32) -> [i32; 6] {
    match fmt {
        Fmt::Rgb565 => [5, 6, 5, 0, 0, 0],
        Fmt::Rgba4 => [4, 4, 4, 4, 0, 0],
        Fmt::Rgba5551 => [5, 5, 5, 1, 0, 0],
        Fmt::Etc1 => [8, 8, 8, 0, 0, 0],
        Fmt::Rgba8 => match base {
            0x1907 => [8, 8, 8, 0, 0, 0],
            0x1906 => [0, 0, 0, 8, 0, 0],
            0x1909 => [0, 0, 0, 0, 8, 0],
            0x190a => [0, 0, 0, 8, 8, 0],
            _ => [8, 8, 8, 8, 0, 0],
        },
    }
}

/// glGetTexLevelParameteriv: the image of `level` of the bound texture (or a proxy).
#[no_mangle]
pub unsafe extern "C" fn glGetTexLevelParameteriv(
    kind: u32,
    level: i32,
    pname: u32,
    params: *mut i32,
) {
    api!(|t: &mut Textures| {
        if !(0..=10).contains(&level) {
            return Err(0x0501);
        }
        if params.is_null() {
            return Err(0x0501);
        }
        let l = level as usize;
        // Width, height, internal format and storage of the level; zero without an image.
        let (width, height, internal, base, fmt, has_image) = match kind {
            TARGET | TARGET_1D => {
                let i = t.index_for(kind == TARGET_1D)?;
                let o = &t.objects[i];
                if o.storage.is_some() && l == 0 {
                    (
                        o.width as i32,
                        o.height as i32,
                        o.internal,
                        o.base,
                        o.fmt,
                        true,
                    )
                } else if o.storage.is_some() && o.defs[l].defined {
                    let d = o.defs[l];
                    (
                        d.width as i32,
                        d.height as i32,
                        d.internal,
                        d.base,
                        d.fmt,
                        true,
                    )
                } else {
                    (0, 0, 0, 0, Fmt::Rgba8, false)
                }
            }
            PROXY_2D | PROXY_1D => {
                let p = if kind == PROXY_1D {
                    t.proxy1[l]
                } else {
                    t.proxy2[l]
                };
                (p.width, p.height, p.internal, p.base, p.fmt, p.width > 0)
            }
            _ => return Err(0x0500),
        };
        let bits = if has_image {
            channel_bits(fmt, base)
        } else {
            [0; 6]
        };
        params.write(match pname {
            0x1000 => width,
            0x1001 => height,
            0x1005 => 0,
            // GL default: one component.
            0x1003 => {
                if has_image {
                    internal as i32
                } else {
                    1
                }
            }
            0x805c => bits[0],
            0x805d => bits[1],
            0x805e => bits[2],
            0x805f => bits[3],
            0x8060 => bits[4],
            0x8061 => bits[5],
            _ => return Err(0x0500),
        });
        Ok(())
    });
}

#[no_mangle]
pub unsafe extern "C" fn glGetTexLevelParameterfv(
    kind: u32,
    level: i32,
    pname: u32,
    params: *mut f32,
) {
    let mut v = 0;
    glGetTexLevelParameteriv(kind, level, pname, &mut v);
    if !params.is_null() {
        params.write(v as f32);
    }
}

/// GL_OES_compressed_paletted_texture entry: (index bits, storage, base format, GL format, type, bytes).
fn palette_entry(internal: u32) -> Option<(usize, Fmt, u32, u32, u32, usize)> {
    if !(0x8b90..=0x8b99).contains(&internal) {
        return None;
    }
    let i = (internal - 0x8b90) as usize;
    let bits = if i < 5 { 4 } else { 8 };
    let (fmt, base, f, ty, size) = match i % 5 {
        0 => (Fmt::Rgba8, 0x1907, 0x1907, 0x1401, 3),
        1 => (Fmt::Rgba8, 0x1908, 0x1908, 0x1401, 4),
        2 => (Fmt::Rgb565, 0x1907, 0x1907, 0x8363, 2),
        3 => (Fmt::Rgba4, 0x1908, 0x1908, 0x8033, 2),
        _ => (Fmt::Rgba5551, 0x1908, 0x1908, 0x8034, 2),
    };
    Some((bits, fmt, base, f, ty, size))
}

#[no_mangle]
pub unsafe extern "C" fn glCompressedTexImage2D(
    kind: u32,
    level: i32,
    internal: u32,
    w: i32,
    h: i32,
    border: i32,
    size: i32,
    data: *const c_void,
) {
    if crate::lists::compiling() {
        let copy = (!data.is_null() && (0..=2 * 1024 * 1024).contains(&size))
            .then(|| core::slice::from_raw_parts(data.cast::<u8>(), size as usize).to_vec());
        let cmd = crate::lists::Cmd::CompressedTexImage {
            target: kind,
            level,
            internal,
            w,
            h,
            border,
            data: copy,
        };
        if crate::lists::record(cmd) {
            return;
        }
    }
    api!(|t: &mut Textures| {
        target(kind)?;
        let palette = palette_entry(internal);
        if palette.is_none() && internal != 0x8d64 {
            return Err(0x0500);
        }
        if border != 0 || w < 0 || h < 0 || size < 0 {
            return Err(0x0501);
        }
        let (w, h, size) = (w as usize, h as usize, size as usize);
        let data: Option<&[u8]> = if data.is_null() {
            None
        } else {
            Some(core::slice::from_raw_parts(data.cast(), size))
        };
        if let Some((bits, fmt, base, f, ty, entry)) = palette {
            // Levels 0..=-level share one palette; indices follow it, level 0 first.
            let mut max_level = 0;
            while w >> max_level > 1 || h >> max_level > 1 {
                max_level += 1;
            }
            if level > 0 || (-level) as usize > max_level || w > 1024 || h > 1024 {
                return Err(0x0501);
            }
            let count = (-level) as usize;
            let palette_bytes = (1usize << bits) * entry;
            let mut expected = palette_bytes;
            for l in 0..=count {
                expected += (level_size(w, l) * level_size(h, l) * bits + 7) / 8;
            }
            if size != expected {
                return Err(0x0501);
            }
            let mut indices = data.map(|d| &d[palette_bytes..]);
            for l in 0..=count {
                let (lw, lh) = (level_size(w, l), level_size(h, l));
                let mut rgba = alloc::vec![0u8; lw * lh * 4];
                if let (Some(d), Some(idx)) = (data, indices) {
                    for n in 0..lw * lh {
                        let index = if bits == 8 {
                            idx[n] as usize
                        } else {
                            ((idx[n / 2] >> if n & 1 == 1 { 0 } else { 4 }) & 15) as usize
                        };
                        let c = read_rgba(d.as_ptr().add(index * entry), f, ty);
                        for k in 0..4 {
                            rgba[n * 4 + k] = c[k] as u8;
                        }
                    }
                    indices = Some(&idx[(lw * lh * bits + 7) / 8..]);
                }
                define_image(
                    t,
                    l,
                    base,
                    internal,
                    fmt,
                    lw,
                    lh,
                    Source::Rgba(&rgba),
                    false,
                )?;
            }
            return Ok(());
        }
        if !level_valid(level, w as i32, h as i32) || size != w.div_ceil(4) * h.div_ceil(4) * 8 {
            return Err(0x0501);
        }
        let zeros;
        let blocks = match data {
            Some(d) => d,
            None => {
                zeros = alloc::vec![0u8; size];
                &zeros
            }
        };
        define_image(
            t,
            level as usize,
            0x1907,
            0x8d64,
            Fmt::Etc1,
            w,
            h,
            Source::Etc1(blocks),
            false,
        )
    });
}

#[no_mangle]
pub unsafe extern "C" fn glCompressedTexSubImage2D(
    kind: u32,
    _level: i32,
    _x: i32,
    _y: i32,
    _w: i32,
    _h: i32,
    format: u32,
    _size: i32,
    _data: *const c_void,
) {
    // Neither paletted nor ETC1 images can be updated in part.
    api!(|_: &mut Textures| {
        target(kind)?;
        Err::<(), u32>(if palette_entry(format).is_some() || format == 0x8d64 {
            0x0502
        } else {
            0x0500
        })
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
    tex_sub_image(false, kind, level, x, y, w, h, f, ty, pixels);
}

#[no_mangle]
pub unsafe extern "C" fn glTexSubImage1D(
    kind: u32,
    level: i32,
    x: i32,
    w: i32,
    f: u32,
    ty: u32,
    pixels: *const c_void,
) {
    tex_sub_image(true, kind, level, x, 0, w, 1, f, ty, pixels);
}

unsafe fn tex_sub_image(
    one_d: bool,
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
    if crate::lists::compiling() {
        let cmd = crate::lists::Cmd::TexImage {
            sub: true,
            target: kind,
            level,
            internal: 0,
            x,
            y,
            w,
            h,
            border: 0,
            f,
            ty,
            pixels: capture(pixels, w, h, f, ty),
        };
        if crate::lists::record(cmd) {
            return;
        }
    }
    api!(|t: &mut Textures| {
        if one_d {
            if kind != TARGET_1D {
                return Err(0x0500);
            }
        } else {
            target(kind)?;
        }
        let bpp = format(f, ty)?;
        if !(0..=10).contains(&level) || x < 0 || y < 0 || w < 0 || h < 0 {
            return Err(0x0501);
        }

        let i = t.index_for(one_d)?;
        let o = &t.objects[i];
        let level = level as usize;
        let (x, y, w, h) = (x as usize, y as usize, w as usize, h as usize);
        if o.storage.is_none() || o.fmt == Fmt::Etc1 {
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
        let (levels, fmt) = (o.levels, o.fmt);
        let storage = chain_buffer(o, levels, o.width, o.height, fmt, true)?;
        let region = storage.pointer.add(level_offset(tw, th, level, fmt));
        let src = Source::Gl {
            pixels: t.unpack.start(pixels.cast(), bpp, w as usize),
            f,
            ty,
            bpp,
            alignment: t.unpack.alignment,
            row: t.unpack.row_length,
        };
        fill_level(region, lw, lh, x, y, w, h, fmt, o.base, src);
        padding_in(region, iw, ih, lw, lh, o.wrap, fmt.bpp());
        if level == 0 && o.generate {
            generate(
                storage.pointer,
                tw,
                th,
                levels,
                o.width,
                o.height,
                fmt,
                o.wrap,
            );
        }
        if p::c3dglPlatformFlush(storage.pointer, level_offset(tw, th, levels, fmt) as u32) < 0 {
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
    fn mip_chain_sizes_box_filter_and_packed_formats() {
        assert_eq!(max_stored(8, 8), 0);
        assert_eq!(max_stored(64, 16), 1);
        assert_eq!(level_size(5, 3), 1);
        assert_eq!(level_offset(16, 16, 2, Fmt::Rgba8), 16 * 16 * 4 + 8 * 8 * 4);
        assert_eq!(
            level_offset(16, 16, 2, Fmt::Rgb565),
            16 * 16 * 2 + 8 * 8 * 2
        );
        assert_eq!(
            level_offset(32, 32, 2, Fmt::Etc1),
            32 * 32 / 2 + 16 * 16 / 2
        );
        // 16x16 RGBA8, 2x2 blocks of 0/4/8/12 average to (sum + 2) / 4 = 6.
        let mut chain = alloc::vec![0u8; level_offset(16, 16, 2, Fmt::Rgba8)];
        for y in 0..16 {
            for x in 0..16 {
                let v = [0u8, 4][x & 1] + [0u8, 8][y & 1];
                chain[offset(16, 16, x, y)..][..4].copy_from_slice(&[v, v, v, 200]);
            }
        }
        unsafe {
            generate(
                chain.as_mut_ptr(),
                16,
                16,
                2,
                16,
                16,
                Fmt::Rgba8,
                [0x2901; 2],
            )
        };
        let base = level_offset(16, 16, 1, Fmt::Rgba8);
        assert_eq!(&chain[base + offset(8, 8, 3, 5)..][..4], &[6, 6, 6, 200]);
        // 16-bit formats round trip and average at 8-bit precision.
        assert_eq!(unpack16(Fmt::Rgb565, 0xffff), [255, 255, 255, 255]);
        assert_eq!(unpack16(Fmt::Rgb565, 0x001f), [0, 0, 255, 255]);
        assert_eq!(
            pack16(Fmt::Rgb565, [128, 128, 255, 255]),
            (16 << 11) | (32 << 5) | 31
        );
        for fmt in [Fmt::Rgb565, Fmt::Rgba4, Fmt::Rgba5551] {
            for v in [0u16, 0x1234, 0xffff, 0x8421] {
                let c = unpack16(fmt, v);
                assert_eq!(unpack16(fmt, pack16(fmt, c)), c, "{v:#x}");
            }
        }
        let mut packed = alloc::vec![0u8; level_offset(16, 16, 2, Fmt::Rgb565)];
        for y in 0..16 {
            for x in 0..16 {
                let v: u16 = if (x + y) % 2 == 0 { 0xffff } else { 0x001f };
                unsafe { put16(packed.as_mut_ptr(), 16, 16, x, y, v) };
            }
        }
        unsafe {
            generate(
                packed.as_mut_ptr(),
                16,
                16,
                2,
                16,
                16,
                Fmt::Rgb565,
                [0x2901; 2],
            )
        };
        let level1 = unsafe { packed.as_ptr().add(level_offset(16, 16, 1, Fmt::Rgb565)) };
        assert_eq!(
            unsafe { get16(level1, 8, 8, 2, 2) },
            (16 << 11) | (32 << 5) | 31
        );
    }

    #[test]
    fn etc1_blocks_use_pica_tile_order_with_reversed_bytes() {
        // 8x8 image: four 4x4 blocks, each filled with its block number, row-major from t = 0.
        let data: alloc::vec::Vec<u8> = (0..4u8)
            .flat_map(|b| (0..8).map(move |i| b * 16 + i))
            .collect();
        let mut out = alloc::vec![0u8; 32];
        unsafe { upload_etc1(out.as_mut_ptr(), 8, 8, 8, &data) };
        // PICA order inside a tile: (0,0), (1,0), (0,1), (1,1); bytes reversed per block.
        for (slot, block) in [0usize, 1, 2, 3].into_iter().enumerate() {
            for i in 0..8 {
                assert_eq!(out[slot * 8 + i], (block as u8) * 16 + 7 - i as u8);
            }
        }
        let c = palette_entry(0x8b93).unwrap();
        assert_eq!((c.0, c.1, c.2), (4, Fmt::Rgba4, 0x1908));
        assert_eq!(palette_entry(0x8b95).unwrap().0, 8);
        assert!(palette_entry(0x8b8f).is_none());
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
                0,
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
    if crate::lists::record(crate::lists::Cmd::TexEnvi(target, pname, value)) {
        return;
    }
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
    if crate::lists::compiling() {
        let cmd = if target != 0x2300 {
            crate::lists::Cmd::Raise(0x0500)
        } else {
            crate::lists::Cmd::TexEnvColor(core::array::from_fn(|i| {
                ptr::read_unaligned(values.add(i))
            }))
        };
        if crate::lists::record(cmd) {
            return;
        }
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
