//! Point parameters (GL 1.4, ES 1.1: size limits, fade threshold, distance attenuation) and the
//! state of point sprites (GL_OES_point_sprite). The points themselves are expanded in `lines.rs`.
use crate::renderer::with_frame;

pub(crate) const MAX_POINT_SIZE: f32 = crate::lines::MAX_POINT_SIZE;

#[derive(Clone, Copy)]
pub(crate) struct Points {
    pub min: f32,
    pub max: f32,
    pub fade: f32,
    pub attenuation: [f32; 3],
    pub sprite: bool,
}

impl Points {
    pub fn new() -> Self {
        Self {
            min: 0.,
            max: MAX_POINT_SIZE,
            fade: 1.,
            attenuation: [1., 0., 0.],
            sprite: false,
        }
    }

    pub fn query(&self, pname: u32) -> Option<([f64; 16], usize)> {
        let mut v = [0.; 16];
        let n = match pname {
            0x0b12 | 0x846d => {
                v[0] = 1.;
                v[1] = MAX_POINT_SIZE as f64;
                2
            }
            // Any size: points are quads.
            0x0b13 => 1,
            0x8126 => {
                v[0] = self.min as f64;
                1
            }
            0x8127 => {
                v[0] = self.max as f64;
                1
            }
            0x8128 => {
                v[0] = self.fade as f64;
                1
            }
            0x8129 => {
                for i in 0..3 {
                    v[i] = self.attenuation[i] as f64;
                }
                3
            }
            _ => return None,
        };
        Some((v, n))
    }
}

/// glPointParameter with up to three values.
pub(crate) unsafe fn set_point_parameter(pname: u32, v: [f32; 3]) {
    with_frame(|s, m| {
        let p = &mut s.points;
        match pname {
            0x8126 | 0x8127 | 0x8128 => {
                if v[0] < 0. {
                    m.fail(0x0501);
                    return;
                }
                match pname {
                    0x8126 => p.min = v[0],
                    0x8127 => p.max = v[0],
                    _ => p.fade = v[0],
                }
            }
            0x8129 => p.attenuation = v,
            _ => m.fail(0x0500),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glPointParameterfv(pname: u32, params: *const f32) {
    if params.is_null() {
        with_frame(|_, m| m.fail(0x0501));
        return;
    }
    let n = if pname == 0x8129 { 3 } else { 1 };
    let v: [f32; 3] = core::array::from_fn(|i| {
        if i < n {
            params.add(i).read_unaligned()
        } else {
            0.
        }
    });
    if crate::lists::record(crate::lists::Cmd::PointParameter(pname, v)) {
        return;
    }
    set_point_parameter(pname, v);
}

#[no_mangle]
pub unsafe extern "C" fn glPointParameterf(pname: u32, param: f32) {
    // The attenuation needs three values.
    if pname == 0x8129 {
        if !crate::lists::record(crate::lists::Cmd::Raise(0x0500)) {
            with_frame(|_, m| m.fail(0x0500));
        }
        return;
    }
    glPointParameterfv(pname, &param);
}

#[no_mangle]
pub unsafe extern "C" fn glPointParameteri(pname: u32, param: i32) {
    glPointParameterf(pname, param as f32);
}

#[no_mangle]
pub unsafe extern "C" fn glPointParameteriv(pname: u32, params: *const i32) {
    if params.is_null() {
        with_frame(|_, m| m.fail(0x0501));
        return;
    }
    let n = if pname == 0x8129 { 3 } else { 1 };
    let v: [f32; 3] = core::array::from_fn(|i| {
        if i < n {
            params.add(i).read_unaligned() as f32
        } else {
            0.
        }
    });
    glPointParameterfv(pname, v.as_ptr());
}
