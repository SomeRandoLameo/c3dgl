//! Depth/cull state in GL coordinates. PICA stores reversed window depth.
use crate::renderer::with_frame;

#[derive(Clone, Copy)]
pub(crate) struct Raster {
    pub dirty: bool,
    pub depth: bool,
    pub blend: crate::blend::Blend,
    pub depth_mask: bool,
    pub function: u32,
    pub range: [f32; 2],
    pub cull: bool,
    pub face: u32,
    pub front: u32,
    pub alpha: bool,
    pub alpha_func: u32,
    pub alpha_ref: u8,
    pub scissor: bool,
    pub scissor_box: [i32; 4],
}

impl Raster {
    pub fn new() -> Self {
        Self {
            dirty: false,
            depth: false,
            blend: crate::blend::Blend::new(),
            depth_mask: true,
            function: 0x0201,
            range: [0., 1.],
            cull: false,
            face: 0x0405,
            front: 0x0901,
            alpha: false,
            alpha_func: 0x0207,
            alpha_ref: 0,
            scissor: false,
            scissor_box: [0, 0, 400, 240],
        }
    }

    /// SCISSORTEST_MODE/POS/DIM for the rotated 240x400 target (logical x is physical -y).
    pub fn scissor_words(&self) -> [u32; 3] {
        if !self.scissor {
            return [0; 3];
        }
        let [bx, by, bw, bh] = self.scissor_box;
        let (mut x, mut y, mut w, mut h) = (by, 400 - bx.wrapping_add(bw), bh, bw);
        if x < 0 {
            w += x;
            x = 0;
        }
        if y < 0 {
            h += y;
            y = 0;
        }
        w = w.max(0);
        h = h.max(0);
        [
            3,
            (y as u32) << 16 | (x as u32 & 0xffff),
            ((y + h) as u32).wrapping_sub(1) << 16 | ((x + w) as u32).wrapping_sub(1) & 0xffff,
        ]
    }

    /// FRAGOP_ALPHA_TEST: enable, PICA comparison (GL order mapped directly) and reference byte.
    pub fn alpha_word(&self) -> u32 {
        let function = match self.alpha_func {
            0x0200 => 0,
            0x0201 => 4,
            0x0202 => 2,
            0x0203 => 5,
            0x0204 => 6,
            0x0205 => 3,
            0x0206 => 7,
            _ => 1,
        };
        self.alpha as u32 | function << 4 | (self.alpha_ref as u32) << 8
    }

    pub fn drawable(&self) -> bool {
        !(self.cull && self.face == 0x0408)
    }

    pub fn comparison(&self) -> u32 {
        if !self.depth {
            return 1;
        }

        match self.function {
            0x0200 => 0,
            0x0201 => 6,
            0x0202 => 2,
            0x0203 => 7,
            0x0204 => 4,
            0x0205 => 3,
            0x0206 => 5,
            _ => 1,
        }
    }

    pub fn depth_word(&self) -> u32 {
        0x0f00
            | self.depth as u32
            | (self.comparison() << 4)
            | if self.depth && self.depth_mask {
                0x1000
            } else {
                0
            }
    }

    pub fn cull_word(&self) -> u32 {
        if !self.cull || self.face == 0x0408 {
            0
        } else if (self.face == 0x0404) == (self.front == 0x0901) {
            1
        } else {
            2
        }
    }

    pub fn query(&self, pname: u32) -> Option<([f64; 16], usize)> {
        if let Some(v) = self.blend.query(pname) {
            return Some(v);
        }

        let mut values = [0.; 16];
        if pname == 0x0c10 {
            for (i, v) in self.scissor_box.iter().enumerate() {
                values[i] = *v as f64;
            }
            return Some((values, 4));
        }
        values[0] = match pname {
            0x0b71 => self.depth as u8 as f64,
            0x0b72 => self.depth_mask as u8 as f64,
            0x0b74 => self.function as f64,
            0x0b44 => self.cull as u8 as f64,
            0x0b45 => self.face as f64,
            0x0b46 => self.front as f64,
            0x0bc0 => self.alpha as u8 as f64,
            0x0c11 => self.scissor as u8 as f64,
            0x0bc1 => self.alpha_func as f64,
            0x0bc2 => self.alpha_ref as f64 / 255.,
            0x0b70 => {
                values[1] = self.range[1] as f64;
                self.range[0] as f64
            }
            _ => return None,
        };

        Some((values, if pname == 0x0b70 { 2 } else { 1 }))
    }
}

unsafe fn enable(cap: u32, on: bool) {
    if cap == 0x0de1 {
        with_frame(|s, _| {
            s.textures.set_enabled(on);
        });
        return;
    }

    with_frame(|s, m| {
        let r = &mut s.raster;
        let value = match cap {
            0x0b71 => &mut r.depth,
            0x0b44 => &mut r.cull,
            0x0bc0 => &mut r.alpha,
            0x0c11 => &mut r.scissor,
            0x0be2 => &mut r.blend.enabled,
            _ => {
                m.fail(0x0500);
                return;
            }
        };

        if *value != on {
            *value = on;
            r.dirty = true;
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glEnable(cap: u32) {
    enable(cap, true);
}

#[no_mangle]
pub unsafe extern "C" fn glDisable(cap: u32) {
    enable(cap, false);
}

#[no_mangle]
pub unsafe extern "C" fn glIsEnabled(cap: u32) -> u8 {
    let mut result = 0;
    with_frame(|s, m| {
        result = match cap {
            0x0b71 => s.raster.depth as u8,
            0x0b44 => s.raster.cull as u8,
            0x0bc0 => s.raster.alpha as u8,
            0x0c11 => s.raster.scissor as u8,
            0x0be2 => s.raster.blend.enabled as u8,
            0x0de1 => s.textures.enabled() as u8,
            _ => {
                m.fail(0x0500);
                0
            }
        };
    });

    result
}

#[no_mangle]
pub unsafe extern "C" fn glScissor(x: i32, y: i32, w: i32, h: i32) {
    with_frame(|s, m| {
        if w < 0 || h < 0 {
            m.fail(0x0501);
            return;
        }

        let r = &mut s.raster;
        if r.scissor_box != [x, y, w, h] {
            r.scissor_box = [x, y, w, h];
            if r.scissor {
                r.dirty = true;
            }
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glAlphaFunc(function: u32, reference: f32) {
    with_frame(|s, m| {
        if !(0x0200..=0x0207).contains(&function) {
            m.fail(0x0500);
            return;
        }

        let byte = if reference <= 0. || reference.is_nan() {
            0
        } else if reference >= 1. {
            255
        } else {
            (reference * 255. + 0.5) as u8
        };
        let r = &mut s.raster;
        if r.alpha_func != function || r.alpha_ref != byte {
            r.alpha_func = function;
            r.alpha_ref = byte;
            r.dirty = true;
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glDepthMask(flag: u8) {
    with_frame(|s, _| {
        let r = &mut s.raster;
        if r.depth_mask != (flag != 0) {
            r.depth_mask = flag != 0;
            r.dirty = true;
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glDepthFunc(function: u32) {
    with_frame(|s, m| {
        if !(0x0200..=0x0207).contains(&function) {
            m.fail(0x0500);
            return;
        }

        let r = &mut s.raster;
        if r.function != function {
            r.function = function;
            r.dirty = true;
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glCullFace(face: u32) {
    with_frame(|s, m| {
        if !matches!(face, 0x0404 | 0x0405 | 0x0408) {
            m.fail(0x0500);
            return;
        }

        let r = &mut s.raster;
        if r.face != face {
            r.face = face;
            r.dirty = true;
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glFrontFace(front: u32) {
    with_frame(|s, m| {
        if !matches!(front, 0x0900 | 0x0901) {
            m.fail(0x0500);
            return;
        }

        let r = &mut s.raster;
        if r.front != front {
            r.front = front;
            r.dirty = true;
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glDepthRange(near: f64, far: f64) {
    with_frame(|s, m| {
        if !near.is_finite() || !far.is_finite() {
            m.fail(0x0501);
            return;
        }

        let range = [near.clamp(0., 1.) as f32, far.clamp(0., 1.) as f32];
        let r = &mut s.raster;
        if r.range != range {
            r.range = range;
            r.dirty = true;
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glDepthRangef(near: f32, far: f32) {
    glDepthRange(near as f64, far as f64);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn alpha_test_word_maps_gl_functions() {
        let mut s = Raster::new();
        assert_eq!(s.alpha_word(), 1 << 4);
        s.alpha = true;
        s.alpha_func = 0x0204;
        s.alpha_ref = 128;
        assert_eq!(s.alpha_word(), 1 | 6 << 4 | 128 << 8);
        for (gl, pica) in [
            (0x0200, 0),
            (0x0201, 4),
            (0x0202, 2),
            (0x0203, 5),
            (0x0205, 3),
            (0x0206, 7),
        ] {
            s.alpha_func = gl;
            assert_eq!(s.alpha_word() >> 4 & 7, pica);
        }
    }

    #[test]
    fn reversed_comparisons_and_winding() {
        let mut s = Raster::new();
        assert_eq!(s.depth_word(), 0x0f10);
        s.depth = true;
        for (function, expected) in [
            (0x0200, 0),
            (0x0201, 6),
            (0x0202, 2),
            (0x0203, 7),
            (0x0204, 4),
            (0x0205, 3),
            (0x0206, 5),
            (0x0207, 1),
        ] {
            s.function = function;
            assert_eq!(s.comparison(), expected);
            assert_eq!(s.depth_word(), 0x1f01 | expected << 4);
        }

        s.depth_mask = false;
        assert_eq!(s.depth_word(), 0x0f11);
        s.cull = true;
        assert_eq!(s.cull_word(), 2);
        s.front = 0x0900;
        assert_eq!(s.cull_word(), 1);
        s.face = 0x0404;
        assert_eq!(s.cull_word(), 2);
        s.front = 0x0901;
        assert_eq!(s.cull_word(), 1);
        s.face = 0x0408;
        assert!(!s.drawable());
    }
}
