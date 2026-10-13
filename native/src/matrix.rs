//! Column-major GL matrices. Fixed stacks, no frame-time allocation.
use crate::renderer::with_matrices;

pub const IDENTITY: [f32; 16] = [
    1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.,
];

const INVALID_ENUM: u32 = 0x0500;
const INVALID_VALUE: u32 = 0x0501;

pub fn multiply(a: &[f32; 16], b: &[f32; 16]) -> [f32; 16] {
    let mut out = [0.; 16];
    for col in 0..4 {
        for row in 0..4 {
            for k in 0..4 {
                out[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
            }
        }
    }

    out
}

pub struct Matrices {
    stacks: [[[f32; 16]; 32]; 2],
    depths: [usize; 2],
    mode: usize,
    pub dirty: bool,
    error: u32,
}

impl Matrices {
    pub fn new() -> Self {
        Self {
            stacks: [[IDENTITY; 32]; 2],
            depths: [0; 2],
            mode: 0,
            dirty: true,
            error: 0,
        }
    }

    pub(crate) fn fail(&mut self, error: u32) {
        if self.error == 0 {
            self.error = error;
        }
    }

    fn load(&mut self, value: [f32; 16]) {
        let old = &mut self.stacks[self.mode][self.depths[self.mode]];
        if *old != value {
            *old = value;
            self.dirty = true;
        }
    }

    fn mult(&mut self, value: [f32; 16]) {
        self.load(multiply(
            &self.stacks[self.mode][self.depths[self.mode]],
            &value,
        ));
    }

    pub(crate) fn query(&self, pname: u32) -> Option<([f32; 16], usize)> {
        let mut out = [0.; 16];
        match pname {
            0x0ba6 | 0x0ba7 => {
                let mode = (pname - 0x0ba6) as usize;
                return Some((self.stacks[mode][self.depths[mode]], 16));
            }
            0x0ba0 => out[0] = (0x1700 + self.mode) as f32,
            0x0ba3 | 0x0ba4 => out[0] = (self.depths[(pname - 0x0ba3) as usize] + 1) as f32,
            0x0d36 => out[0] = 32.,
            0x0d38 => out[0] = 2.,
            _ => return None,
        }
        Some((out, 1))
    }

    pub fn combined(&self) -> [f32; 16] {
        multiply(
            &self.stacks[1][self.depths[1]],
            &self.stacks[0][self.depths[0]],
        )
    }
}

#[no_mangle]
pub unsafe extern "C" fn glGetError() -> u32 {
    let mut result = 0;
    crate::renderer::with_immediate(|r| {
        let m = &mut r.matrices;
        result = m.error;
        m.error = 0;
    });
    result
}

#[no_mangle]
pub unsafe extern "C" fn glMatrixMode(mode: u32) {
    with_matrices(|m| match mode {
        0x1700 => m.mode = 0,
        0x1701 => m.mode = 1,
        _ => m.fail(INVALID_ENUM),
    });
}

#[no_mangle]
pub unsafe extern "C" fn glLoadIdentity() {
    with_matrices(|m| m.load(IDENTITY));
}

#[no_mangle]
pub unsafe extern "C" fn glLoadMatrixf(value: *const f32) {
    with_matrices(|m| {
        if value.is_null() {
            m.fail(INVALID_VALUE);
        } else {
            m.load(value.cast::<[f32; 16]>().read());
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glMultMatrixf(value: *const f32) {
    with_matrices(|m| {
        if value.is_null() {
            m.fail(INVALID_VALUE);
        } else {
            m.mult(value.cast::<[f32; 16]>().read());
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glPushMatrix() {
    with_matrices(|m| {
        let d = m.depths[m.mode];
        let limit = if m.mode == 0 { 32 } else { 2 };
        if d + 1 == limit {
            m.fail(0x0503);
        } else {
            m.stacks[m.mode][d + 1] = m.stacks[m.mode][d];
            m.depths[m.mode] += 1;
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glPopMatrix() {
    with_matrices(|m| {
        if m.depths[m.mode] == 0 {
            m.fail(0x0504);
        } else {
            m.depths[m.mode] -= 1;
            m.dirty = true;
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glTranslatef(x: f32, y: f32, z: f32) {
    let mut a = IDENTITY;
    a[12] = x;
    a[13] = y;
    a[14] = z;
    with_matrices(|m| m.mult(a));
}

#[no_mangle]
pub unsafe extern "C" fn glScalef(x: f32, y: f32, z: f32) {
    let mut a = IDENTITY;
    a[0] = x;
    a[5] = y;
    a[10] = z;
    with_matrices(|m| m.mult(a));
}

extern "C" {
    fn sinf(x: f32) -> f32;
    fn cosf(x: f32) -> f32;
    fn sqrtf(x: f32) -> f32;
}

#[no_mangle]
pub unsafe extern "C" fn glRotatef(angle: f32, x: f32, y: f32, z: f32) {
    let length = sqrtf(x * x + y * y + z * z);
    if length == 0. {
        return;
    }

    let (x, y, z) = (x / length, y / length, z / length);
    let radians = angle * (core::f32::consts::PI / 180.);
    let (s, c) = (sinf(radians), cosf(radians));
    let t = 1. - c;
    let a = [
        t * x * x + c,
        t * x * y + s * z,
        t * x * z - s * y,
        0.,
        t * x * y - s * z,
        t * y * y + c,
        t * y * z + s * x,
        0.,
        t * x * z + s * y,
        t * y * z - s * x,
        t * z * z + c,
        0.,
        0.,
        0.,
        0.,
        1.,
    ];
    with_matrices(|m| m.mult(a));
}

#[no_mangle]
pub unsafe extern "C" fn glOrtho(l: f64, r: f64, b: f64, t: f64, n: f64, f: f64) {
    with_matrices(|m| {
        if l == r || b == t || n == f {
            m.fail(INVALID_VALUE);
            return;
        }
        let a = [
            2. / (r - l),
            0.,
            0.,
            0.,
            0.,
            2. / (t - b),
            0.,
            0.,
            0.,
            0.,
            -2. / (f - n),
            0.,
            -(r + l) / (r - l),
            -(t + b) / (t - b),
            -(f + n) / (f - n),
            1.,
        ];
        m.mult(a.map(|x| x as f32));
    });
}

#[no_mangle]
pub unsafe extern "C" fn glFrustum(l: f64, r: f64, b: f64, t: f64, n: f64, f: f64) {
    with_matrices(|m| {
        if l == r || b == t || n == f || n <= 0. || f <= 0. {
            m.fail(INVALID_VALUE);
            return;
        }
        let a = [
            2. * n / (r - l),
            0.,
            0.,
            0.,
            0.,
            2. * n / (t - b),
            0.,
            0.,
            (r + l) / (r - l),
            (t + b) / (t - b),
            -(f + n) / (f - n),
            -1.,
            0.,
            0.,
            -2. * f * n / (f - n),
            0.,
        ];
        m.mult(a.map(|x| x as f32));
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn multiplication_order_and_first_error() {
        let mut m = Matrices::new();
        let mut translate = IDENTITY;
        translate[12] = 3.;
        m.mult(translate);
        let mut scale = IDENTITY;
        scale[0] = 2.;
        m.mult(scale);
        let a = m.combined();
        assert_eq!(a[0], 2.);
        assert_eq!(a[12], 3.);
        m.mode = 1;
        m.load(scale);
        assert_eq!(m.combined()[12], 6.);
        m.fail(INVALID_ENUM);
        m.fail(INVALID_VALUE);
        assert_eq!(m.error, INVALID_ENUM);
    }
}
