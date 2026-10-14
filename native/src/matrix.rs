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
    stacks: [[[f32; 16]; 32]; 5],
    depths: [usize; 5],
    mode: usize,
    active: usize,
    pub dirty: bool,
    pub texture_dirty: bool,
    error: u32,
}

impl Matrices {
    pub fn new() -> Self {
        Self {
            stacks: [[IDENTITY; 32]; 5],
            depths: [0; 5],
            mode: 0,
            active: 0,
            dirty: true,
            texture_dirty: true,
            error: 0,
        }
    }

    /// Initialize in place; building the 10 KiB value on the 32 KiB 3DS stack overflows it.
    pub unsafe fn init(dst: *mut Self) {
        let stacks = core::ptr::addr_of_mut!((*dst).stacks).cast::<[f32; 16]>();
        for i in 0..5 * 32 {
            stacks.add(i).write(IDENTITY);
        }
        core::ptr::addr_of_mut!((*dst).depths).write([0; 5]);
        core::ptr::addr_of_mut!((*dst).mode).write(0);
        core::ptr::addr_of_mut!((*dst).active).write(0);
        core::ptr::addr_of_mut!((*dst).dirty).write(true);
        core::ptr::addr_of_mut!((*dst).texture_dirty).write(true);
        core::ptr::addr_of_mut!((*dst).error).write(0);
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
            self.changed();
        }
    }

    fn changed(&mut self) {
        if self.mode >= 2 {
            self.texture_dirty = true;
        } else {
            self.dirty = true;
        }
    }

    pub fn active_texture(&mut self, unit: usize) {
        self.active = unit;
        if self.mode >= 2 {
            self.mode = 2 + unit;
        }
    }
    pub fn texture(&self) -> [f32; 16] {
        self.texture_for(self.active)
    }
    pub fn texture_for(&self, unit: usize) -> [f32; 16] {
        self.stacks[2 + unit][self.depths[2 + unit]]
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
            0x0ba6..=0x0ba8 => {
                let mode = if pname == 0x0ba8 {
                    2 + self.active
                } else {
                    (pname - 0x0ba6) as usize
                };
                return Some((self.stacks[mode][self.depths[mode]], 16));
            }
            0x0ba0 => out[0] = (0x1700 + self.mode.min(2)) as f32,
            0x0ba3..=0x0ba5 => {
                out[0] = (self.depths[if pname == 0x0ba5 {
                    2 + self.active
                } else {
                    (pname - 0x0ba3) as usize
                }] + 1) as f32
            }
            0x0d36 => out[0] = 32.,
            0x0d38 | 0x0d39 => out[0] = 2.,
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
        0x1702 => m.mode = 2 + m.active,
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
            m.changed();
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
    fn texture_transform_queries_and_independent_dirty_state() {
        let mut m = Matrices::new();
        m.dirty = false;
        m.texture_dirty = false;
        m.mode = 2;
        let mut translate = IDENTITY;
        translate[12] = 0.25;
        m.mult(translate);
        let mut scale = IDENTITY;
        scale[0] = 2.;
        scale[5] = -1.;
        m.mult(scale);
        assert_eq!(m.texture()[0], 2.);
        assert_eq!(m.texture()[12], 0.25);
        assert_eq!(m.combined(), IDENTITY);
        assert!(!m.dirty);
        assert!(m.texture_dirty);
        assert_eq!(m.query(0x0ba8), Some((m.texture(), 16)));
        assert_eq!(m.query(0x0ba5).unwrap().0[0], 1.);
        assert_eq!(m.query(0x0d39).unwrap().0[0], 2.);
        assert_eq!(m.query(0x0ba0).unwrap().0[0], 0x1702 as f32);
        let mut projective = IDENTITY;
        projective[3] = 1.;
        m.load(projective);
        assert_eq!(m.texture()[3], 1.);
    }

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

#[cfg(test)]
pub(crate) unsafe fn texture_stack_lifecycle() {
    use crate::frame::glGetFloatv;
    glMatrixMode(0x1702);
    glLoadIdentity();
    glTranslatef(0.25, 0.5, 0.);
    glPushMatrix();
    glScalef(2., 3., 1.);
    let mut matrix = [0.; 16];
    glGetFloatv(0x0ba8, matrix.as_mut_ptr());
    assert_eq!(matrix[0], 2.);
    assert_eq!(matrix[5], 3.);
    assert_eq!(matrix[12], 0.25);
    let mut depth = 0.;
    glGetFloatv(0x0ba5, &mut depth);
    assert_eq!(depth, 2.);
    glPushMatrix();
    assert_eq!(glGetError(), 0x0503);
    glPopMatrix();
    glGetFloatv(0x0ba8, matrix.as_mut_ptr());
    assert_eq!(matrix[0], 1.);
    assert_eq!(matrix[12], 0.25);
    glPopMatrix();
    assert_eq!(glGetError(), 0x0504);
    glLoadIdentity();
    glMatrixMode(0x1700);
    assert_eq!(glGetError(), 0);
}
