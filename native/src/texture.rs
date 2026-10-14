//! One texture unit, level-zero RGB/RGBA uploads into resident Morton-tiled RGBA8.
use crate::{
    platform::gpu as p,
    renderer::{with_frame, Buffer},
};
use alloc::vec::Vec;
use core::{ffi::c_void, ptr};

const TARGET: u32 = 0x0de1;
const LIMIT: usize = 64;

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
    rgb: bool,
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
            rgb: false,
        }
    }

    fn complete(&self) -> bool {
        self.storage.is_some() && matches!(self.min, 0x2600 | 0x2601)
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
}

pub(crate) struct Textures {
    pub enabled: bool,
    pub dirty: bool,
    bound: u32,
    alignment: usize,
    env_replace: bool,
    objects: Vec<Object>,
    retired: Vec<Buffer>,
}

impl Textures {
    pub fn new() -> Self {
        Self {
            enabled: false,
            dirty: false,
            bound: 0,
            alignment: 4,
            env_replace: false,
            objects: Vec::new(),
            retired: Vec::new(),
        }
    }

    pub fn collected(&mut self) {
        self.retired.clear();
        for o in &mut self.objects {
            o.used = false;
        }
    }

    fn index(&mut self) -> Result<usize, u32> {
        if let Some(i) = self.objects.iter().position(|o| o.name == self.bound) {
            return Ok(i);
        }

        self.insert(self.bound, true)
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
        let mut s = Packet {
            enabled: self.enabled,
            complete: false,
            address: 0,
            dimensions: 0,
            parameters: 0,
            replace: self.env_replace,
            scale: [1.; 2],
        };

        if let Some(o) = self.objects.iter().find(|o| o.name == self.bound) {
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
                s.parameters = ((o.mag == 0x2601) as u32) << 1
                    | ((o.min == 0x2601) as u32) << 2
                    | wrap(o.wrap[1]) << 8
                    | wrap(o.wrap[0]) << 12;
            }
        }

        s
    }

    pub fn sampled(&mut self) {
        if self.enabled {
            if let Some(o) = self
                .objects
                .iter_mut()
                .find(|o| o.name == self.bound && o.complete())
            {
                o.used = true;
            }
        }
    }

    pub fn query(&self, pname: u32) -> Option<([f64; 16], usize)> {
        let mut v = [0.; 16];
        v[0] = match pname {
            0x0de1 => self.enabled as u8 as f64,
            0x8069 => self.bound as f64,
            0x0cf5 => self.alignment as f64,
            0x0d33 => 1024.,
            0x84e0 => 0x84c0 as f64,
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
    if t != 0x1401 {
        return Err(0x0500);
    }

    match f {
        0x1907 => Ok(3),
        0x1908 => Ok(4),
        _ => Err(0x0500),
    }
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
    let (tw, th) = (stored(w), stored(h));
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
                out.add(offset(tw, th, sx, sy)),
                out.add(offset(tw, th, x, y)),
                4,
            );
        }
    }
}

/// GL bottom row first; PICA's tiled image runs top to bottom and stores A,B,G,R.
fn offset(width: usize, height: usize, x: usize, y: usize) -> usize {
    let y = height - 1 - y;
    let tile = (y / 8) * (width / 8) + x / 8;
    let morton = (x & 1)
        | ((y & 1) << 1)
        | ((x & 2) << 1)
        | ((y & 2) << 2)
        | ((x & 4) << 2)
        | ((y & 4) << 3);
    (tile * 64 + morton) * 4
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
) {
    let stride = (w * bpp + alignment - 1) & !(alignment - 1);
    for y in 0..h {
        for x in 0..w {
            let s = pixels.add(y * stride + x * bpp);
            let d = out.add(offset(tw, th, x0 + x, y0 + y));
            d.write(if bpp == 4 { s.add(3).read() } else { 255 });
            d.add(1).write(s.add(2).read());
            d.add(2).write(s.add(1).read());
            d.add(3).write(s.read());
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
    with_frame(|_, m| {
        if unit != 0x84c0 {
            m.fail(0x0500);
        }
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
        if t.bound != name {
            t.bound = name;
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
                if t.bound == name {
                    t.bound = 0;
                    t.dirty = true;
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
#[no_mangle]
pub unsafe extern "C" fn glTexParameteri(kind: u32, pname: u32, value: i32) {
    api!(|t: &mut Textures| {
        target(kind)?;
        let value = value as u32;
        let valid = match pname {
            0x2801 => matches!(value, 0x2600 | 0x2601 | 0x2700..=0x2703),
            0x2800 => matches!(value, 0x2600 | 0x2601),
            0x2802 | 0x2803 => matches!(value, 0x2901 | 0x812f | 0x8370),
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
            _ => o.wrap[1],
        };
        if current != value {
            if matches!(pname, 0x2802 | 0x2803)
                && o.storage.is_some()
                && (o.width != stored(o.width) || o.height != stored(o.height))
            {
                let mut wrap = o.wrap;
                wrap[(pname == 0x2803) as usize] = value;
                let (w, h) = (o.width, o.height);
                let bytes = stored(w) * stored(h) * 4;
                let storage = Buffer::new(bytes, false).ok_or(0x0505u32)?;
                ptr::copy_nonoverlapping(
                    o.storage.as_ref().unwrap().pointer,
                    storage.pointer,
                    bytes,
                );
                padding(storage.pointer, w, h, wrap);
                if p::c3dglPlatformFlush(storage.pointer, bytes as u32) < 0 {
                    return Err(0x0502);
                }
                t.replace(i, Some(storage), w, h)?;
            }
            let o = &mut t.objects[i];
            match pname {
                0x2801 => o.min = value,
                0x2800 => o.mag = value,
                0x2802 => o.wrap[0] = value,
                _ => o.wrap[1] = value,
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
        if !matches!(pname, 0x2800..=0x2803) {
            return Err(0x0500);
        }
        let i = t.index()?;
        let o = &t.objects[i];
        let value = match pname {
            0x2801 => o.min,
            0x2800 => o.mag,
            0x2802 => o.wrap[0],
            0x2803 => o.wrap[1],
            _ => return Err(0x0500),
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
        if !matches!(internal, 0x1907 | 0x1908) {
            return Err(0x0501);
        }
        if level != 0 || border != 0 || !dimension(w) || !dimension(h) {
            return Err(0x0501);
        }
        let i = t.index()?;
        let w = w as usize;
        let h = h as usize;
        if w == 0 || h == 0 {
            return t.replace(i, None, w, h);
        }
        let (tw, th) = (stored(w), stored(h));
        let bytes = tw * th * 4;
        let storage = Buffer::new(bytes, false).ok_or(0x0505u32)?;
        ptr::write_bytes(storage.pointer, 0, bytes);
        if !pixels.is_null() {
            upload(
                storage.pointer,
                tw,
                th,
                0,
                0,
                w,
                h,
                pixels.cast(),
                bpp,
                t.alignment,
            );
        }
        if internal == 0x1907 {
            for i in 0..tw * th {
                storage.pointer.add(i * 4).write(255);
            }
        }
        padding(storage.pointer, w, h, t.objects[i].wrap);
        if p::c3dglPlatformFlush(storage.pointer, bytes as u32) < 0 {
            return Err(0x0502);
        }
        t.replace(i, Some(storage), w, h)?;
        t.objects[i].rgb = internal == 0x1907;
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
        if level != 0 || x < 0 || y < 0 || w < 0 || h < 0 {
            return Err(0x0501);
        }

        let i = t.index()?;
        let o = &t.objects[i];
        let (x, y, w, h) = (x as usize, y as usize, w as usize, h as usize);
        if x > o.width || y > o.height || w > o.width - x || h > o.height - y {
            return Err(0x0501);
        }

        let old = o.storage.as_ref().ok_or(0x0502u32)?;
        if w == 0 || h == 0 {
            return Ok(());
        }

        if pixels.is_null() {
            return Err(0x0501);
        }

        // Copy on update: queued draws retain their previous texture image.
        let (iw, ih) = (o.width, o.height);
        let (tw, th) = (stored(iw), stored(ih));
        let bytes = tw * th * 4;
        let storage = Buffer::new(bytes, false).ok_or(0x0505u32)?;
        ptr::copy_nonoverlapping(old.pointer, storage.pointer, bytes);
        upload(
            storage.pointer,
            tw,
            th,
            x,
            y,
            w,
            h,
            pixels.cast(),
            bpp,
            t.alignment,
        );

        if o.rgb {
            for row in y..y + h {
                for col in x..x + w {
                    storage.pointer.add(offset(tw, th, col, row)).write(255);
                }
            }
        }

        padding(storage.pointer, iw, ih, o.wrap);
        if p::c3dglPlatformFlush(storage.pointer, bytes as u32) < 0 {
            return Err(0x0502);
        }

        t.replace(i, Some(storage), iw, ih)
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
            upload(image.as_mut_ptr(), 16, 8, 9, 2, 1, 2, pixels.as_ptr(), 3, 4);
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
        if target != 0x2300 || pname != 0x2200 || !matches!(value, 0x2100 | 0x1e01) {
            return Err(0x0500);
        }

        let replace = value == 0x1e01;
        if t.env_replace != replace {
            t.env_replace = replace;
            t.dirty = true;
        }

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
        assert_eq!(s.textures.bound, 0);
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
