//! Texture coordinate generation (GL 1.1 section 2.10.4), per vertex on the CPU like the C
//! renderer. The generated coordinates already include the unit's texture matrix (applied
//! with all of s, t, r, q so that a generated r takes part), so these units get an identity
//! matrix in the vertex shader.
use crate::renderer::with_immediate;
use core::ptr;

extern "C" {
    fn sqrtf(x: f32) -> f32;
}

pub(crate) const OBJECT_LINEAR: u32 = 0x2401;
pub(crate) const EYE_LINEAR: u32 = 0x2400;
pub(crate) const SPHERE_MAP: u32 = 0x2402;

#[derive(Clone, Copy)]
pub(crate) struct TexGenUnit {
    /// Bit per coordinate s, t, r, q.
    pub enabled: u8,
    mode: [u32; 4],
    object_plane: [[f32; 4]; 4],
    /// Eye coordinates (transformed by the inverse modelview of the glTexGen call).
    eye_plane: [[f32; 4]; 4],
}

impl TexGenUnit {
    pub fn new() -> Self {
        let mut u = Self {
            enabled: 0,
            mode: [EYE_LINEAR; 4],
            object_plane: [[0.; 4]; 4],
            eye_plane: [[0.; 4]; 4],
        };
        u.object_plane[0][0] = 1.;
        u.eye_plane[0][0] = 1.;
        u.object_plane[1][1] = 1.;
        u.eye_plane[1][1] = 1.;
        u
    }
}

/// Everything linear folded into one transform per unit: eye planes times the modelview are
/// object planes, and the texture matrix rows s, t, q times the per-coordinate inputs give the
/// final rows.
#[derive(Clone, Copy, Default)]
pub(crate) struct Transform {
    gen: [[f32; 4]; 3],
    pass: [[f32; 3]; 3],
    sphere: [[f32; 2]; 3],
    uses_pass: bool,
    uses_sphere: bool,
}

#[derive(Clone)]
pub(crate) struct TexGen {
    pub units: [TexGenUnit; 3],
    /// Bumped by every glTexGen change; with the matrices it keys the cached transforms.
    serial: u32,
    cache: Option<(u32, [f32; 16], [[f32; 16]; 3], [Transform; 3])>,
}

impl TexGen {
    pub fn new() -> Self {
        Self {
            units: [TexGenUnit::new(); 3],
            serial: 0,
            cache: None,
        }
    }

    /// GL_ENABLE_BIT: the enable flags only.
    pub fn restore_enables(&mut self, from: &TexGen) {
        for u in 0..3 {
            self.units[u].enabled = from.units[u].enabled;
        }
        self.serial += 1;
    }

    /// Any coordinate of any unit is generated.
    pub fn active(&self) -> bool {
        self.units.iter().any(|u| u.enabled != 0)
    }

    /// Units with generation (a mask), whose texture matrix is folded into the coordinates.
    pub fn unit_mask(&self) -> u8 {
        (0..3).fold(0, |m, u| m | ((self.units[u].enabled != 0) as u8) << u)
    }

    pub fn set_enabled(&mut self, unit: usize, coord: usize, on: bool) {
        let bit = 1 << coord;
        self.serial += 1;
        if on {
            self.units[unit].enabled |= bit;
        } else {
            self.units[unit].enabled &= !bit;
        }
    }

    fn transforms(&mut self, mv: &[f32; 16], tm: &[[f32; 16]; 3]) -> [Transform; 3] {
        if let Some((serial, cmv, ctm, t)) = &self.cache {
            if *serial == self.serial && cmv == mv && ctm == tm {
                return *t;
            }
        }
        let t = core::array::from_fn(|u| unit_transform(&self.units[u], mv, &tm[u]));
        self.cache = Some((self.serial, *mv, *tm, t));
        t
    }
}

fn unit_transform(g: &TexGenUnit, mv: &[f32; 16], tm: &[f32; 16]) -> Transform {
    let mut x = Transform::default();
    if g.enabled == 0 {
        return x;
    }
    // Input coordinate c (s, t, r, q) = gen[c] . (x, y, z, 1) + pass[c] . (s, t, q)
    //                                   + sphere[c] . (s, t)
    let mut gen = [[0f32; 4]; 4];
    let mut pass = [[0f32; 3]; 4];
    let mut sphere = [[0f32; 2]; 4];
    for c in 0..4 {
        if g.enabled >> c & 1 == 0 {
            if c != 2 {
                // r stays 0.
                pass[c][if c == 3 { 2 } else { c }] = 1.;
                x.uses_pass = true;
            }
            continue;
        }
        match g.mode[c] {
            OBJECT_LINEAR => gen[c] = g.object_plane[c],
            EYE_LINEAR => {
                // plane . (MV p) = (plane^T MV) . p
                for j in 0..4 {
                    let p = &g.eye_plane[c];
                    gen[c][j] = p[0] * mv[j * 4]
                        + p[1] * mv[j * 4 + 1]
                        + p[2] * mv[j * 4 + 2]
                        + p[3] * mv[j * 4 + 3];
                }
            }
            _ => {
                // GL_SPHERE_MAP: s, t only.
                sphere[c][c] = 1.;
                x.uses_sphere = true;
            }
        }
    }
    for (i, row) in [0usize, 1, 3].into_iter().enumerate() {
        let r = [tm[row], tm[4 + row], tm[8 + row], tm[12 + row]];
        for c in 0..4 {
            for j in 0..4 {
                x.gen[i][j] += r[c] * gen[c][j];
            }
            for j in 0..3 {
                x.pass[i][j] += r[c] * pass[c][j];
            }
            for j in 0..2 {
                x.sphere[i][j] += r[c] * sphere[c][j];
            }
        }
    }
    x
}

fn dot3(a: &[f32; 3], b: &[f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

fn normalize3(v: &mut [f32; 3]) {
    let len = unsafe { sqrtf(dot3(v, v)) };
    if len > 0. {
        for c in v.iter_mut() {
            *c /= len;
        }
    }
}

/// GL_SPHERE_MAP s, t: r = u - 2 n (n.u) with u the unit vector from the eye to the vertex and
/// n the eye-space normal, then s, t = r_x/m + 1/2, r_y/m + 1/2 with
/// m = 2 sqrt(r_x^2 + r_y^2 + (r_z + 1)^2).
fn sphere_map(mv: &[f32; 16], position: [f32; 4], n: [f32; 3]) -> [f32; 2] {
    let mut u: [f32; 4] =
        core::array::from_fn(|r| (0..4).map(|c| mv[c * 4 + r] * position[c]).sum());
    if u[3] != 1. && u[3] != 0. {
        let w = u[3];
        for c in u.iter_mut().take(3) {
            *c /= w;
        }
    }
    let mut u3 = [u[0], u[1], u[2]];
    normalize3(&mut u3);
    let nu = 2. * dot3(&n, &u3);
    let r: [f32; 3] = core::array::from_fn(|k| u3[k] - nu * n[k]);
    let m = 2. * unsafe { sqrtf(r[0] * r[0] + r[1] * r[1] + (r[2] + 1.) * (r[2] + 1.)) };
    let inv = if m > 0. { 1. / m } else { 0. };
    [r[0] * inv + 0.5, r[1] * inv + 0.5]
}

impl TexGen {
    /// Generate the coordinates of the units with generation into `tex` (unit 0) and `extra`
    /// (units 1 and 2). `position`: object space with w = 1; `eye_normal`: the eye-space
    /// normal (sphere map only).
    pub fn generate(
        &mut self,
        mv: &[f32; 16],
        textures: &[[f32; 16]; 3],
        position: [f32; 4],
        eye_normal: impl FnMut() -> [f32; 3],
        tex: &mut [f32; 4],
        extra: &mut [[f32; 4]; 2],
    ) {
        let transforms = self.transforms(mv, textures);
        let enabled = self.enabled_units();
        apply(&transforms, enabled, mv, position, eye_normal, tex, extra);
    }

    /// The units with generation, bit per unit.
    pub fn enabled_units(&self) -> u8 {
        (0..3).fold(0, |m, u| m | ((self.units[u].enabled != 0) as u8) << u)
    }

    /// The per-unit transforms of the current state, once per draw (see `apply`).
    pub fn prepare(&mut self, mv: &[f32; 16], textures: &[[f32; 16]; 3]) -> [Transform; 3] {
        self.transforms(mv, textures)
    }
}

/// Generate the coordinates of the `enabled` units with transforms from `TexGen::prepare`.
#[inline]
pub(crate) fn apply(
    transforms: &[Transform; 3],
    enabled: u8,
    mv: &[f32; 16],
    position: [f32; 4],
    mut eye_normal: impl FnMut() -> [f32; 3],
    tex: &mut [f32; 4],
    extra: &mut [[f32; 4]; 2],
) {
    let mut sphere = None;
    let p = position;
    for unit in 0..3 {
        if enabled >> unit & 1 == 0 {
            continue;
        }
        let x = &transforms[unit];
        let tc: &mut [f32; 4] = if unit == 0 { tex } else { &mut extra[unit - 1] };
        let mut out = [0f32; 3];
        for i in 0..3 {
            out[i] = x.gen[i][0] * p[0] + x.gen[i][1] * p[1] + x.gen[i][2] * p[2] + x.gen[i][3];
        }
        if x.uses_pass {
            let input = [tc[0], tc[1], tc[3]];
            for i in 0..3 {
                out[i] +=
                    x.pass[i][0] * input[0] + x.pass[i][1] * input[1] + x.pass[i][2] * input[2];
            }
        }
        if x.uses_sphere {
            let s = *sphere.get_or_insert_with(|| sphere_map(mv, position, eye_normal()));
            for i in 0..3 {
                out[i] += x.sphere[i][0] * s[0] + x.sphere[i][1] * s[1];
            }
        }
        tc[0] = out[0];
        tc[1] = out[1];
        tc[3] = out[2];
        if unit == 0 {
            // Not generated.
            tc[2] = 0.;
        }
    }
}

/// General 4x4 inverse by cofactors; identity if singular.
pub(crate) fn invert(a: &[f32; 16]) -> [f32; 16] {
    let mut inv = [0f32; 16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15]
        + a[9] * a[7] * a[14]
        + a[13] * a[6] * a[11]
        - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15]
        - a[8] * a[7] * a[14]
        - a[12] * a[6] * a[11]
        + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15]
        + a[8] * a[7] * a[13]
        + a[12] * a[5] * a[11]
        - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14]
        - a[8] * a[6] * a[13]
        - a[12] * a[5] * a[10]
        + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15]
        - a[9] * a[3] * a[14]
        - a[13] * a[2] * a[11]
        + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15]
        + a[8] * a[3] * a[14]
        + a[12] * a[2] * a[11]
        - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15]
        - a[8] * a[3] * a[13]
        - a[12] * a[1] * a[11]
        + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14]
        + a[8] * a[2] * a[13]
        + a[12] * a[1] * a[10]
        - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15]
        + a[5] * a[3] * a[14]
        + a[13] * a[2] * a[7]
        - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15]
        - a[4] * a[3] * a[14]
        - a[12] * a[2] * a[7]
        + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15]
        + a[4] * a[3] * a[13]
        + a[12] * a[1] * a[7]
        - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14]
        - a[4] * a[2] * a[13]
        - a[12] * a[1] * a[6]
        + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11]
        - a[5] * a[3] * a[10]
        - a[9] * a[2] * a[7]
        + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11]
        + a[4] * a[3] * a[10]
        + a[8] * a[2] * a[7]
        - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11]
        - a[4] * a[3] * a[9]
        - a[8] * a[1] * a[7]
        + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10]
        + a[4] * a[2] * a[9]
        + a[8] * a[1] * a[6]
        - a[8] * a[2] * a[5];
    let det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if det == 0. {
        return crate::matrix::IDENTITY;
    }
    inv.map(|v| v / det)
}

fn param_count(pname: u32) -> usize {
    if matches!(pname, 0x2501 | 0x2502) {
        4
    } else {
        1
    }
}

pub(crate) unsafe fn set_tex_gen(coord: u32, pname: u32, p: &[f32; 4], scalar: bool) {
    if crate::lists::record(crate::lists::Cmd::TexGen(coord, pname, *p, scalar)) {
        return;
    }
    with_immediate(|r| {
        if r.immediate.active {
            r.matrices.fail(0x0502);
            return;
        }
        if !(0x2000..=0x2003).contains(&coord) {
            r.matrices.fail(0x0500);
            return;
        }
        let c = (coord - 0x2000) as usize;
        let unit = r.frame.textures.active;
        let mv = r.matrices.modelview();
        let g = &mut r.frame.texgen;
        g.serial += 1;
        match pname {
            0x2500 => {
                let mode = p[0] as u32;
                if mode != OBJECT_LINEAR && mode != EYE_LINEAR && (mode != SPHERE_MAP || c >= 2) {
                    r.matrices.fail(0x0500);
                    return;
                }
                g.units[unit].mode[c] = mode;
            }
            0x2501 if !scalar => g.units[unit].object_plane[c] = *p,
            0x2502 if !scalar => {
                // Stored in eye coordinates: p_eye = p * M^-1 with the current modelview.
                let inv = invert(&mv);
                g.units[unit].eye_plane[c] = core::array::from_fn(|i| {
                    p[0] * inv[i * 4]
                        + p[1] * inv[i * 4 + 1]
                        + p[2] * inv[i * 4 + 2]
                        + p[3] * inv[i * 4 + 3]
                });
            }
            _ => r.matrices.fail(0x0500),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glTexGenf(coord: u32, pname: u32, param: f32) {
    set_tex_gen(coord, pname, &[param, 0., 0., 0.], true);
}

#[no_mangle]
pub unsafe extern "C" fn glTexGeni(coord: u32, pname: u32, param: i32) {
    glTexGenf(coord, pname, param as f32);
}

#[no_mangle]
pub unsafe extern "C" fn glTexGend(coord: u32, pname: u32, param: f64) {
    glTexGenf(coord, pname, param as f32);
}

#[no_mangle]
pub unsafe extern "C" fn glTexGenfv(coord: u32, pname: u32, params: *const f32) {
    if params.is_null() {
        with_immediate(|r| r.matrices.fail(0x0501));
        return;
    }
    let mut p = [0.; 4];
    for (i, v) in p.iter_mut().enumerate().take(param_count(pname)) {
        *v = params.add(i).read();
    }
    set_tex_gen(coord, pname, &p, false);
}

#[no_mangle]
pub unsafe extern "C" fn glTexGeniv(coord: u32, pname: u32, params: *const i32) {
    if params.is_null() {
        with_immediate(|r| r.matrices.fail(0x0501));
        return;
    }
    let mut p = [0.; 4];
    for (i, v) in p.iter_mut().enumerate().take(param_count(pname)) {
        *v = params.add(i).read() as f32;
    }
    set_tex_gen(coord, pname, &p, false);
}

#[no_mangle]
pub unsafe extern "C" fn glTexGendv(coord: u32, pname: u32, params: *const f64) {
    if params.is_null() {
        with_immediate(|r| r.matrices.fail(0x0501));
        return;
    }
    let mut p = [0.; 4];
    for (i, v) in p.iter_mut().enumerate().take(param_count(pname)) {
        *v = params.add(i).read() as f32;
    }
    set_tex_gen(coord, pname, &p, false);
}

/// Values and their count, or an error code.
unsafe fn get_tex_gen(coord: u32, pname: u32) -> Result<([f32; 4], usize), u32> {
    let mut result = Err(0x0500);
    with_immediate(|r| {
        if r.immediate.active {
            result = Err(0x0502);
            return;
        }
        if !(0x2000..=0x2003).contains(&coord) {
            return;
        }
        let c = (coord - 0x2000) as usize;
        let g = &r.frame.texgen.units[r.frame.textures.active];
        result = match pname {
            0x2500 => Ok(([g.mode[c] as f32, 0., 0., 0.], 1)),
            0x2501 => Ok((g.object_plane[c], 4)),
            0x2502 => Ok((g.eye_plane[c], 4)),
            _ => Err(0x0500),
        };
    });
    result
}

#[no_mangle]
pub unsafe extern "C" fn glGetTexGenfv(coord: u32, pname: u32, params: *mut f32) {
    match get_tex_gen(coord, pname) {
        Ok((v, n)) if !params.is_null() => ptr::copy_nonoverlapping(v.as_ptr(), params, n),
        Ok(_) => with_immediate(|r| r.matrices.fail(0x0501)),
        Err(e) => with_immediate(|r| r.matrices.fail(e)),
    }
}

#[no_mangle]
pub unsafe extern "C" fn glGetTexGeniv(coord: u32, pname: u32, params: *mut i32) {
    match get_tex_gen(coord, pname) {
        Ok((v, n)) if !params.is_null() => {
            for (i, x) in v[..n].iter().enumerate() {
                params
                    .add(i)
                    .write((*x + if *x >= 0. { 0.5 } else { -0.5 }) as i32);
            }
        }
        Ok(_) => with_immediate(|r| r.matrices.fail(0x0501)),
        Err(e) => with_immediate(|r| r.matrices.fail(e)),
    }
}

#[no_mangle]
pub unsafe extern "C" fn glGetTexGendv(coord: u32, pname: u32, params: *mut f64) {
    match get_tex_gen(coord, pname) {
        Ok((v, n)) if !params.is_null() => {
            for (i, x) in v[..n].iter().enumerate() {
                params.add(i).write(*x as f64);
            }
        }
        Ok(_) => with_immediate(|r| r.matrices.fail(0x0501)),
        Err(e) => with_immediate(|r| r.matrices.fail(e)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::matrix::IDENTITY;

    #[test]
    fn object_linear_with_identity_matrix_and_pass_through() {
        let mut g = TexGen::new();
        g.units[0].mode = [OBJECT_LINEAR; 4];
        g.units[0].object_plane[0] = [2., 0., 0., 1.];
        g.units[0].object_plane[1] = [0., 3., 0., 0.];
        g.set_enabled(0, 0, true);
        g.set_enabled(0, 1, true);
        assert!(g.active() && g.unit_mask() == 1);
        let mut tex = [7., 8., 9., 1.];
        let mut extra = [[0.; 4]; 2];
        g.generate(
            &IDENTITY,
            &[IDENTITY; 3],
            [1., 2., 3., 1.],
            || [0., 0., 1.],
            &mut tex,
            &mut extra,
        );
        assert_eq!(tex, [3., 6., 0., 1.]);
        // q is not generated: it passes through the texture matrix.
        let mut tex = [0., 0., 0., 2.];
        g.generate(
            &IDENTITY,
            &[IDENTITY; 3],
            [0., 0., 0., 1.],
            || [0., 0., 1.],
            &mut tex,
            &mut extra,
        );
        assert_eq!(tex[3], 2.);
    }

    #[test]
    fn sphere_map_faces_the_viewer_at_the_center() {
        let mut g = TexGen::new();
        g.units[0].mode[0] = SPHERE_MAP;
        g.units[0].mode[1] = SPHERE_MAP;
        g.set_enabled(0, 0, true);
        g.set_enabled(0, 1, true);
        let mut tex = [0.; 4];
        let mut extra = [[0.; 4]; 2];
        // A vertex straight ahead (eye -z) with the normal toward the eye maps to (0.5, 0.5).
        g.generate(
            &IDENTITY,
            &[IDENTITY; 3],
            [0., 0., -2., 1.],
            || [0., 0., 1.],
            &mut tex,
            &mut extra,
        );
        assert!((tex[0] - 0.5).abs() < 1e-6 && (tex[1] - 0.5).abs() < 1e-6);
    }

    #[test]
    fn inverse_and_eye_plane_transform() {
        let mut m = IDENTITY;
        m[12] = 5.;
        let inv = invert(&m);
        assert_eq!(inv[12], -5.);
        assert_eq!(invert(&[0.; 16]), IDENTITY);
    }
}
