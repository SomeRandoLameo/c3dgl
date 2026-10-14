//! Fog: the GL factor of the eye distance, sampled into the PICA fog table (indexed by window
//! depth, 128 entries with interpolation) like the C renderer does.
use crate::renderer::with_frame;

extern "C" {
    fn expf(x: f32) -> f32;
}

pub(crate) const LINEAR: u32 = 0x2601;
pub(crate) const EXP: u32 = 0x0800;
pub(crate) const EXP2: u32 = 0x0801;

#[derive(Clone, Copy)]
pub(crate) struct Fog {
    pub enabled: bool,
    pub mode: u32,
    density: f32,
    start: f32,
    end: f32,
    color: [f32; 4],
    index: f32,
}

/// What the GPU holds: the inputs of the table (projection z/w rows, depth range, parameters)
/// and the color.
#[derive(Clone, Copy, PartialEq)]
pub(crate) struct Key {
    inputs: [u32; 10],
    color: u32,
}

impl Key {
    pub fn color(&self) -> u32 {
        self.color
    }
}

impl Fog {
    pub fn new() -> Self {
        Self {
            enabled: false,
            mode: EXP,
            density: 1.,
            start: 0.,
            end: 1.,
            color: [0.; 4],
            index: 0.,
        }
    }

    /// The GPU key for the current state, or None without fog.
    pub fn key(&self, projection: &[f32; 16], range: [f32; 2]) -> Option<Key> {
        if !self.enabled {
            return None;
        }
        let byte = |c: f32| {
            if c <= 0. {
                0
            } else if c >= 1. {
                255
            } else {
                (c * 255. + 0.5) as u32
            }
        };
        let (a, b, c, d) = (
            projection[10],
            projection[14],
            projection[11],
            projection[15],
        );
        Some(Key {
            inputs: [
                a.to_bits(),
                b.to_bits(),
                c.to_bits(),
                d.to_bits(),
                range[0].to_bits(),
                range[1].to_bits(),
                self.mode,
                self.density.to_bits(),
                self.start.to_bits(),
                self.end.to_bits(),
            ],
            color: byte(self.color[0]) | byte(self.color[1]) << 8 | byte(self.color[2]) << 16,
        })
    }

    pub fn restore_all(&mut self, from: &Fog) {
        *self = *from;
    }

    pub fn query(&self, pname: u32) -> Option<([f64; 16], usize)> {
        let mut v = [0.; 16];
        let n = match pname {
            0x0b65 => {
                v[0] = self.mode as f64;
                1
            }
            0x0b62 => {
                v[0] = self.density as f64;
                1
            }
            0x0b63 => {
                v[0] = self.start as f64;
                1
            }
            0x0b64 => {
                v[0] = self.end as f64;
                1
            }
            0x0b61 => {
                v[0] = self.index as f64;
                1
            }
            0x0b66 => {
                for (o, c) in v.iter_mut().zip(self.color) {
                    *o = c as f64;
                }
                4
            }
            _ => return None,
        };
        Some((v, n))
    }
}

/// GL fog factor for eye distance `c` (clamped, 1 = no fog).
fn factor(mode: u32, density: f32, start: f32, end: f32, c: f32) -> f32 {
    let f = if mode == LINEAR {
        if end != start {
            (end - c) / (end - start)
        } else {
            1.
        }
    } else if mode == EXP2 {
        unsafe { expf(-(density * c) * (density * c)) }
    } else {
        unsafe { expf(-density * c) }
    };
    if f < 0. {
        0.
    } else if f > 1. {
        1.
    } else {
        f
    }
}

/// The PICA fog table of `key`: factor at 128 entries of window depth and the 13-bit
/// differences to the next entry (C3D FogLut_FromArray layout).
pub(crate) fn table(key: &Key) -> [u32; 128] {
    let f = |i: usize| f32::from_bits(key.inputs[i]);
    let (a, b, c, d) = (f(0), f(1), f(2), f(3));
    let (near, far) = (f(4), f(5));
    let (mode, density, start, end) = (key.inputs[6], f(7), f(8), f(9));
    let range = far - near;
    let mut data = [0f32; 256];
    for i in 0..=128 {
        let z_ndc = if range != 0. {
            2. * (i as f32 / 128. - near) / range - 1.
        } else {
            0.
        };
        let den = c * z_ndc - a;
        let z_eye = if den.abs() > 1e-12 {
            (b - d * z_ndc) / den
        } else {
            1e30
        };
        let v = factor(mode, density, start, end, z_eye.abs());
        if i < 128 {
            data[i] = v;
        }
        if i > 0 {
            data[127 + i] = v - data[i - 1];
        }
    }
    core::array::from_fn(|i| {
        let (mut input, mut diff) = (data[i], data[i + 128]);
        let mut val = 0u32;
        if input > 0. {
            input *= 2048.;
            val = if input < 2048. { input as u32 } else { 0x7ff };
        }
        let mut val2 = 0u32;
        if diff != 0. {
            diff *= 2048.;
            if diff < -4096. {
                diff = -4096.;
            } else if diff > 4095. {
                diff = 4095.;
            }
            val2 = (diff as i32 as u32) & 0x1fff;
        }
        val2 | val << 13
    })
}

pub(crate) fn set_fog(pname: u32, p: &[f32; 4]) {
    unsafe {
        if crate::lists::record(crate::lists::Cmd::Fog(pname, *p)) {
            return;
        }
        with_frame(|s, m| {
            let f = &mut s.fog;
            match pname {
                0x0b65 => {
                    let mode = p[0] as u32;
                    if !matches!(mode, LINEAR | EXP | EXP2) {
                        m.fail(0x0500);
                        return;
                    }
                    f.mode = mode;
                }
                0x0b62 => {
                    if p[0] < 0. {
                        m.fail(0x0501);
                        return;
                    }
                    f.density = p[0];
                }
                0x0b63 => f.start = p[0],
                0x0b64 => f.end = p[0],
                0x0b61 => f.index = p[0],
                // Clamped, like all GLclampf colors.
                0x0b66 => {
                    f.color = p.map(|c| {
                        if c < 0. {
                            0.
                        } else if c > 1. {
                            1.
                        } else {
                            c
                        }
                    })
                }
                _ => m.fail(0x0500),
            }
        })
    }
}

#[no_mangle]
pub unsafe extern "C" fn glFogfv(pname: u32, params: *const f32) {
    if params.is_null() {
        with_frame(|_, m| m.fail(0x0501));
        return;
    }
    let n = if pname == 0x0b66 { 4 } else { 1 };
    let mut p = [0.; 4];
    for (i, v) in p.iter_mut().enumerate().take(n) {
        *v = params.add(i).read();
    }
    set_fog(pname, &p);
}

#[no_mangle]
pub unsafe extern "C" fn glFogf(pname: u32, param: f32) {
    if pname == 0x0b66 {
        with_frame(|_, m| m.fail(0x0500));
        return;
    }
    set_fog(pname, &[param, 0., 0., 0.]);
}

#[no_mangle]
pub unsafe extern "C" fn glFogiv(pname: u32, params: *const i32) {
    if params.is_null() {
        with_frame(|_, m| m.fail(0x0501));
        return;
    }
    let mut p = [0.; 4];
    if pname == 0x0b66 {
        for (i, v) in p.iter_mut().enumerate() {
            *v = ((2. * params.add(i).read() as f64 + 1.) / 4294967295.) as f32;
        }
    } else {
        p[0] = params.read() as f32;
    }
    set_fog(pname, &p);
}

#[no_mangle]
pub unsafe extern "C" fn glFogi(pname: u32, param: i32) {
    if pname == 0x0b66 {
        with_frame(|_, m| m.fail(0x0500));
        return;
    }
    glFogiv(pname, &param);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn table_for_linear_fog_over_an_orthographic_range() {
        let mut projection = crate::matrix::IDENTITY;
        // glOrtho(near 0, far 10) style z row: z_ndc = a z + b, w = 1.
        projection[10] = -0.2;
        projection[14] = -1.;
        let mut fog = Fog::new();
        fog.enabled = true;
        fog.mode = LINEAR;
        fog.start = 0.;
        fog.end = 10.;
        let key = fog.key(&projection, [0., 1.]).unwrap();
        let t = table(&key);
        // Entry 0: window depth 0 = z_ndc -1: eye distance 0 -> factor 1 -> 0x7ff.
        assert_eq!(t[0] >> 13, 0x7ff);
        // Factors fall with depth and their differences are negative (13-bit two's complement).
        assert!(t[64] >> 13 < t[0] >> 13);
        assert!(t[10] & 0x1000 != 0);
        assert!(fog.key(&projection, [0., 1.]) == Some(key));
        fog.enabled = false;
        assert!(fog.key(&projection, [0., 1.]).is_none());
    }

    #[test]
    fn factors_are_clamped() {
        assert_eq!(factor(LINEAR, 1., 0., 10., 20.), 0.);
        assert_eq!(factor(LINEAR, 1., 0., 10., -5.), 1.);
        assert_eq!(factor(LINEAR, 1., 3., 3., 4.), 1.);
        assert!(factor(EXP, 1., 0., 1., 0.) == 1.);
    }
}
