//! GL blend factors/equations mapped to PICA RGB and alpha fields.
use crate::renderer::with_frame;

#[derive(Clone, Copy)]
pub(crate) struct Blend {
    pub enabled: bool,
    pub factors: [u32; 4], // src RGB, dst RGB, src alpha, dst alpha
    pub equations: [u32; 2],
    pub color: [f32; 4],
    /// GL_COLOR_LOGIC_OP: a logic operation replaces blending.
    pub logic: bool,
    /// glLogicOp (GL_CLEAR .. GL_SET).
    pub logic_op: u32,
}

fn factor(f: u32) -> Option<u32> {
    Some(match f {
        0 => 0,
        1 => 1,
        0x0300 => 2,
        0x0301 => 3,
        0x0306 => 4,
        0x0307 => 5,
        0x0302 => 6,
        0x0303 => 7,
        0x0304 => 8,
        0x0305 => 9,
        0x8001 => 10,
        0x8002 => 11,
        0x8003 => 12,
        0x8004 => 13,
        0x0308 => 14,
        _ => return None,
    })
}

fn equation(e: u32) -> Option<u32> {
    Some(match e {
        0x8006 => 0,
        0x800a => 1,
        0x800b => 2,
        0x8007 => 3,
        0x8008 => 4,
        _ => return None,
    })
}

impl Blend {
    pub fn new() -> Self {
        Self {
            enabled: false,
            factors: [1, 0, 1, 0],
            equations: [0x8006; 2],
            color: [0.; 4],
            logic: false,
            logic_op: 0x1503,
        }
    }

    /// The PICA logic operation of the GL one: GL_CLEAR + n.
    pub fn logic_word(self) -> u32 {
        const OPS: [u32; 16] = [0, 1, 2, 3, 13, 6, 11, 9, 10, 12, 7, 14, 5, 15, 8, 4];
        OPS[(self.logic_op.wrapping_sub(0x1500) & 15) as usize]
    }

    /// COLOR_OPERATION: the blend unit is off while a logic operation is enabled.
    pub fn operation_word(self) -> u32 {
        if self.logic {
            0x00e40000
        } else {
            0x00e40100
        }
    }

    pub fn word(self) -> u32 {
        if !self.enabled {
            return 0x01010000;
        }

        let f = self.factors.map(|v| factor(v).unwrap());
        let e = self.equations.map(|v| equation(v).unwrap());
        // MIN/MAX disregard GL factors, so explicitly select ONE on PICA.
        let rgb = if e[0] >= 3 { [1, 1] } else { [f[0], f[1]] };
        let alpha = if e[1] >= 3 { [1, 1] } else { [f[2], f[3]] };
        e[0] | e[1] << 8 | rgb[0] << 16 | rgb[1] << 20 | alpha[0] << 24 | alpha[1] << 28
    }

    pub fn color_word(self) -> u32 {
        self.color.iter().enumerate().fold(0, |v, (i, c)| {
            v | ((c.clamp(0., 1.) * 255. + 0.5) as u32) << (i * 8)
        })
    }

    pub fn query(self, pname: u32) -> Option<([f64; 16], usize)> {
        let mut v = [0.; 16];
        v[0] = match pname {
            0x0be2 => self.enabled as u8 as f64,
            0x0bf2 => self.logic as u8 as f64,
            0x0bf0 => self.logic_op as f64,
            0x0be1 | 0x80c9 => self.factors[0] as f64,
            0x0be0 | 0x80c8 => self.factors[1] as f64,
            0x80cb => self.factors[2] as f64,
            0x80ca => self.factors[3] as f64,
            0x8009 => self.equations[0] as f64,
            0x883d => self.equations[1] as f64,
            0x8005 => {
                for (o, c) in v.iter_mut().zip(self.color) {
                    *o = c as f64;
                }
                return Some((v, 4));
            }
            _ => return None,
        };

        Some((v, 1))
    }
}

#[no_mangle]
pub unsafe extern "C" fn glBlendFuncSeparate(sr: u32, dr: u32, sa: u32, da: u32) {
    if crate::lists::record(crate::lists::Cmd::BlendFuncSeparate([sr, dr, sa, da])) {
        return;
    }
    with_frame(|s, m| {
        let factors = [sr, dr, sa, da];
        if factors.iter().any(|f| factor(*f).is_none()) || dr == 0x0308 || da == 0x0308 {
            m.fail(0x0500);
            return;
        }

        if s.raster.blend.factors != factors {
            s.raster.blend.factors = factors;
            s.raster.dirty = true;
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glLogicOp(op: u32) {
    if crate::lists::record(crate::lists::Cmd::LogicOp(op)) {
        return;
    }
    with_frame(|s, m| {
        if !(0x1500..=0x150f).contains(&op) {
            m.fail(0x0500);
            return;
        }
        if s.raster.blend.logic_op != op {
            s.raster.blend.logic_op = op;
            s.raster.dirty = true;
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glBlendFunc(src: u32, dst: u32) {
    glBlendFuncSeparate(src, dst, src, dst);
}

#[no_mangle]
pub unsafe extern "C" fn glBlendEquationSeparate(rgb: u32, alpha: u32) {
    if crate::lists::record(crate::lists::Cmd::BlendEquationSeparate(rgb, alpha)) {
        return;
    }
    with_frame(|s, m| {
        if equation(rgb).is_none() || equation(alpha).is_none() {
            m.fail(0x0500);
            return;
        }

        if s.raster.blend.equations != [rgb, alpha] {
            s.raster.blend.equations = [rgb, alpha];
            s.raster.dirty = true;
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glBlendEquation(mode: u32) {
    glBlendEquationSeparate(mode, mode);
}

#[no_mangle]
pub unsafe extern "C" fn glBlendColor(r: f32, g: f32, b: f32, a: f32) {
    if crate::lists::record(crate::lists::Cmd::BlendColor([r, g, b, a])) {
        return;
    }
    with_frame(|s, m| {
        if [r, g, b, a].iter().any(|v| !v.is_finite()) {
            m.fail(0x0501);
            return;
        }

        let color = [r, g, b, a].map(|v| v.clamp(0., 1.));
        if s.raster.blend.color != color {
            s.raster.blend.color = color;
            s.raster.dirty = true;
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn factors_equations_and_disabled_copy() {
        let mut s = Blend::new();
        assert_eq!(s.word(), 0x01010000);
        s.enabled = true;
        s.factors = [0x302, 0x303, 1, 0x303];
        assert_eq!(s.word(), 0x71760000);
        s.equations = [0x8007, 0x8008];
        assert_eq!(s.word(), 0x11110403);
        s.color = [1., 0.5, 0., 1.];
        assert_eq!(s.color_word(), 0xff0080ff);
        for f in [
            0, 1, 0x300, 0x301, 0x306, 0x307, 0x302, 0x303, 0x304, 0x305, 0x8001, 0x8002, 0x8003,
            0x8004, 0x308,
        ] {
            assert!(factor(f).is_some());
        }
        assert_eq!(factor(7), None);
        assert_eq!(equation(0), None);
    }
}
