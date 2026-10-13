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
        }
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
        values[0] = match pname {
            0x0b71 => self.depth as u8 as f64,
            0x0b72 => self.depth_mask as u8 as f64,
            0x0b74 => self.function as f64,
            0x0b44 => self.cull as u8 as f64,
            0x0b45 => self.face as f64,
            0x0b46 => self.front as f64,
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
            if s.textures.enabled != on {
                s.textures.enabled = on;
                s.textures.dirty = true;
            }
        });
        return;
    }
    with_frame(|s, m| {
        let r = &mut s.raster;
        let value = match cap {
            0x0b71 => &mut r.depth,
            0x0b44 => &mut r.cull,
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
            0x0be2 => s.raster.blend.enabled as u8,
            0x0de1 => s.textures.enabled as u8,
            _ => {
                m.fail(0x0500);
                0
            }
        };
    });

    result
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
