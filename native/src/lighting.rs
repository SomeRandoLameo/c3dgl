//! GL lighting (GL 1.1 section 2.13), evaluated per vertex on the CPU like the C renderer:
//! lights, materials, color material and the light model, with the lit front color and, for
//! two-sided lighting, the back color of each vertex.
use crate::renderer::{with_frame, with_immediate};
use core::ptr;

extern "C" {
    fn powf(x: f32, y: f32) -> f32;
    fn cosf(x: f32) -> f32;
    fn sqrtf(x: f32) -> f32;
}

pub(crate) const MAX_LIGHTS: usize = 8;

#[derive(Clone, Copy)]
pub(crate) struct Light {
    ambient: [f32; 4],
    diffuse: [f32; 4],
    specular: [f32; 4],
    /// Eye coordinates (transformed by the modelview when set).
    position: [f32; 4],
    spot_direction: [f32; 3],
    spot_exponent: f32,
    spot_cutoff: f32,
    attenuation: [f32; 3],
    unit_position: [f32; 3],
    half_vector: [f32; 3],
    spot_unit: [f32; 3],
    spot_cos: f32,
}

#[derive(Clone, Copy)]
pub(crate) struct Material {
    ambient: [f32; 4],
    diffuse: [f32; 4],
    specular: [f32; 4],
    emission: [f32; 4],
    shininess: f32,
    color_indexes: [f32; 3],
}

/// Eye-space normal transform of a modelview matrix.
#[derive(Clone, Copy)]
pub(crate) struct NormalMatrix {
    modelview: [f32; 16],
    matrix: [f32; 9],
    rescale: f32,
}

#[derive(Clone)]
pub(crate) struct Lighting {
    pub enabled: bool,
    lights: [Light; MAX_LIGHTS],
    light_enabled: u32,
    material: [Material; 2],
    model_ambient: [f32; 4],
    local_viewer: bool,
    pub two_side: bool,
    color_material: bool,
    color_material_face: u32,
    color_material_mode: u32,
    normalize: bool,
    rescale_normal: bool,
    normals: Option<NormalMatrix>,
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

fn color_byte(c: f32) -> u8 {
    if c <= 0. {
        0
    } else if c >= 1. {
        255
    } else {
        (c * 255. + 0.5) as u8
    }
}

impl Light {
    fn new(index: usize) -> Self {
        let c = if index == 0 { 1. } else { 0. };
        let mut l = Self {
            ambient: [0., 0., 0., 1.],
            diffuse: [c, c, c, 1.],
            specular: [c, c, c, 1.],
            position: [0., 0., 1., 0.],
            spot_direction: [0., 0., -1.],
            spot_exponent: 0.,
            spot_cutoff: 180.,
            attenuation: [1., 0., 0.],
            unit_position: [0.; 3],
            half_vector: [0.; 3],
            spot_unit: [0.; 3],
            spot_cos: 0.,
        };
        l.update();
        l
    }

    /// Recompute the derived values after a change.
    fn update(&mut self) {
        self.unit_position = [self.position[0], self.position[1], self.position[2]];
        normalize3(&mut self.unit_position);
        self.half_vector =
            core::array::from_fn(|k| self.unit_position[k] + if k == 2 { 1. } else { 0. });
        normalize3(&mut self.half_vector);
        self.spot_unit = self.spot_direction;
        normalize3(&mut self.spot_unit);
        self.spot_cos = unsafe { cosf(self.spot_cutoff * core::f32::consts::PI / 180.) };
    }
}

impl Material {
    fn new() -> Self {
        Self {
            ambient: [0.2, 0.2, 0.2, 1.],
            diffuse: [0.8, 0.8, 0.8, 1.],
            specular: [0., 0., 0., 1.],
            emission: [0., 0., 0., 1.],
            shininess: 0.,
            color_indexes: [0., 1., 1.],
        }
    }
}

impl NormalMatrix {
    /// n' = n M^-1: the cofactor matrix divided by the determinant, and the GL_RESCALE_NORMAL
    /// factor 1/length of M^-1's third row.
    fn new(m: &[f32; 16]) -> Self {
        let e = |r: usize, c: usize| m[c * 4 + r];
        let cof = [
            e(1, 1) * e(2, 2) - e(1, 2) * e(2, 1),
            e(1, 2) * e(2, 0) - e(1, 0) * e(2, 2),
            e(1, 0) * e(2, 1) - e(1, 1) * e(2, 0),
            e(0, 2) * e(2, 1) - e(0, 1) * e(2, 2),
            e(0, 0) * e(2, 2) - e(0, 2) * e(2, 0),
            e(0, 1) * e(2, 0) - e(0, 0) * e(2, 1),
            e(0, 1) * e(1, 2) - e(0, 2) * e(1, 1),
            e(0, 2) * e(1, 0) - e(0, 0) * e(1, 2),
            e(0, 0) * e(1, 1) - e(0, 1) * e(1, 0),
        ];
        let det = e(0, 0) * cof[0] + e(0, 1) * cof[1] + e(0, 2) * cof[2];
        // Singular: normals collapse to 0 (ambient and emission only).
        let inv = if det != 0. { 1. / det } else { 0. };
        let matrix: [f32; 9] = core::array::from_fn(|i| cof[i] * inv);
        let len =
            unsafe { sqrtf(matrix[2] * matrix[2] + matrix[5] * matrix[5] + matrix[8] * matrix[8]) };
        Self {
            modelview: *m,
            matrix,
            rescale: if len > 0. { 1. / len } else { 1. },
        }
    }
}

impl Lighting {
    /// The capability flags (GL_ENABLE_BIT): lighting, lights, color material, normalization.
    pub fn restore_enables(&mut self, from: &Lighting) {
        self.enabled = from.enabled;
        self.light_enabled = from.light_enabled;
        self.color_material = from.color_material;
        self.normalize = from.normalize;
        self.rescale_normal = from.rescale_normal;
    }

    /// GL_TRANSFORM_BIT: normalization flags.
    pub fn restore_normalization(&mut self, from: &Lighting) {
        self.normalize = from.normalize;
        self.rescale_normal = from.rescale_normal;
    }

    pub fn new() -> Self {
        Self {
            enabled: false,
            lights: core::array::from_fn(Light::new),
            light_enabled: 0,
            material: [Material::new(); 2],
            model_ambient: [0.2, 0.2, 0.2, 1.],
            local_viewer: false,
            two_side: false,
            color_material: false,
            color_material_face: 0x0408,
            color_material_mode: 0x1602,
            normalize: false,
            rescale_normal: false,
            normals: None,
        }
    }

    /// Capabilities of this module; false for others.
    pub fn set_enabled(&mut self, cap: u32, on: bool, current_color: [u8; 4]) -> bool {
        match cap {
            0x0b50 => self.enabled = on,
            0x4000..=0x4007 => {
                let bit = 1 << (cap - 0x4000);
                self.light_enabled = if on {
                    self.light_enabled | bit
                } else {
                    self.light_enabled & !bit
                };
            }
            0x0b57 => {
                self.color_material = on;
                if on {
                    self.apply_color_material(current_color);
                }
            }
            0x0ba1 => self.normalize = on,
            0x803a => self.rescale_normal = on,
            _ => return false,
        }
        true
    }

    pub fn is_enabled(&self, cap: u32) -> Option<bool> {
        Some(match cap {
            0x0b50 => self.enabled,
            0x4000..=0x4007 => self.light_enabled >> (cap - 0x4000) & 1 != 0,
            0x0b57 => self.color_material,
            0x0ba1 => self.normalize,
            0x803a => self.rescale_normal,
            _ => return None,
        })
    }

    /// GL_COLOR_MATERIAL: the tracked material properties take the color (and keep it).
    fn apply_color_material(&mut self, color: [u8; 4]) {
        let c = color.map(|b| b as f32 * (1. / 255.));
        let (face, mode) = (self.color_material_face, self.color_material_mode);
        for (f, m) in self.material.iter_mut().enumerate() {
            if face != 0x0408 && face != if f == 1 { 0x0405 } else { 0x0404 } {
                continue;
            }
            if matches!(mode, 0x1200 | 0x1602) {
                m.ambient = c;
            }
            if matches!(mode, 0x1201 | 0x1602) {
                m.diffuse = c;
            }
            if mode == 0x1202 {
                m.specular = c;
            }
            if mode == 0x1600 {
                m.emission = c;
            }
        }
    }

    fn normal_matrix(&mut self, modelview: &[f32; 16]) -> NormalMatrix {
        if !self.normals.is_some_and(|n| n.modelview == *modelview) {
            self.normals = Some(NormalMatrix::new(modelview));
        }
        self.normals.unwrap()
    }

    /// Object-space normal to eye space. Not normalized unless asked for, like GL.
    fn eye_normal(&mut self, modelview: &[f32; 16], normal: [f32; 3]) -> [f32; 3] {
        let nm = self.normal_matrix(modelview);
        let mut n: [f32; 3] = core::array::from_fn(|k| {
            nm.matrix[k * 3] * normal[0]
                + nm.matrix[k * 3 + 1] * normal[1]
                + nm.matrix[k * 3 + 2] * normal[2]
        });
        if self.normalize {
            normalize3(&mut n);
        } else if self.rescale_normal {
            for c in n.iter_mut() {
                *c *= nm.rescale;
            }
        }
        n
    }

    /// Lit color of one side: `eye` = vertex in eye space, `n` = eye-space normal of that side.
    fn shade_face(&self, m: &Material, eye: [f32; 3], n: [f32; 3]) -> [u8; 4] {
        let mut c: [f32; 3] =
            core::array::from_fn(|k| m.emission[k] + m.ambient[k] * self.model_ambient[k]);
        for (i, li) in self.lights.iter().enumerate() {
            if self.light_enabled & (1 << i) == 0 {
                continue;
            }
            // vp: unit vector from the vertex to the light; attenuation for positional lights only.
            let mut vp = [0.; 3];
            let mut att = 1.;
            if li.position[3] != 0. {
                for k in 0..3 {
                    vp[k] = li.position[k] / li.position[3] - eye[k];
                }
                let d2 = dot3(&vp, &vp);
                let d = unsafe { sqrtf(d2) };
                if d > 0. {
                    for v in vp.iter_mut() {
                        *v /= d;
                    }
                }
                let denom = li.attenuation[0] + li.attenuation[1] * d + li.attenuation[2] * d2;
                if denom > 0. {
                    att = 1. / denom;
                }
            } else {
                vp = li.unit_position;
            }

            if li.spot_cutoff != 180. {
                // Outside the cone the light contributes nothing, not even ambient.
                let cos_angle = -dot3(&vp, &li.spot_unit);
                if cos_angle < li.spot_cos {
                    continue;
                }
                att *= unsafe { powf(cos_angle, li.spot_exponent) };
            }

            let mut r: [f32; 3] = core::array::from_fn(|k| m.ambient[k] * li.ambient[k]);
            let ndotl = dot3(&n, &vp);
            if ndotl > 0. {
                for k in 0..3 {
                    r[k] += ndotl * m.diffuse[k] * li.diffuse[k];
                }
                let spec: [f32; 3] = core::array::from_fn(|k| m.specular[k] * li.specular[k]);
                if spec[0] != 0. || spec[1] != 0. || spec[2] != 0. {
                    // Half vector between vp and the direction to the eye ((0, 0, 1) without
                    // local viewer).
                    let mut h;
                    if !self.local_viewer && li.position[3] == 0. {
                        h = li.half_vector;
                    } else {
                        let mut to_eye = [0., 0., 1.];
                        if self.local_viewer {
                            to_eye = [-eye[0], -eye[1], -eye[2]];
                            normalize3(&mut to_eye);
                        }
                        h = core::array::from_fn(|k| vp[k] + to_eye[k]);
                        normalize3(&mut h);
                    }
                    let ndoth = dot3(&n, &h);
                    if ndoth > 0. {
                        let f = unsafe { powf(ndoth, m.shininess) };
                        for k in 0..3 {
                            r[k] += f * spec[k];
                        }
                    }
                }
            }
            for k in 0..3 {
                c[k] += att * r[k];
            }
        }
        [
            color_byte(c[0]),
            color_byte(c[1]),
            color_byte(c[2]),
            color_byte(m.diffuse[3]),
        ]
    }

    /// Lit front color of a vertex and, with two-sided lighting, its back color. `modelview`:
    /// the modelview matrix; `position`: object space; `color`: the vertex color bytes (for
    /// GL_COLOR_MATERIAL).
    pub fn light_vertex(
        &mut self,
        modelview: &[f32; 16],
        position: [f32; 4],
        normal: [f32; 3],
        color: [u8; 4],
    ) -> ([u8; 4], Option<[u8; 4]>) {
        if self.color_material {
            self.apply_color_material(color);
        }
        // The eye-space position is only needed for positional lights and the local viewer.
        let need_eye = self.local_viewer
            || (0..MAX_LIGHTS)
                .any(|i| self.light_enabled & (1 << i) != 0 && self.lights[i].position[3] != 0.);
        let mut eye = [0., 0., 0., 1.];
        if need_eye {
            eye =
                core::array::from_fn(|r| (0..4).map(|c| modelview[c * 4 + r] * position[c]).sum());
            if eye[3] != 1. && eye[3] != 0. {
                for k in 0..3 {
                    eye[k] /= eye[3];
                }
            }
        }
        let n = self.eye_normal(modelview, normal);
        let eye3 = [eye[0], eye[1], eye[2]];
        let front = self.shade_face(&self.material[0], eye3, n);
        let back = self
            .two_side
            .then(|| self.shade_face(&self.material[1], eye3, [-n[0], -n[1], -n[2]]));
        (front, back)
    }

    /// The eye-space normal transform for a modelview (sphere-map texture generation).
    pub fn eye_normal_of(&mut self, modelview: &[f32; 16], normal: [f32; 3]) -> [f32; 3] {
        self.eye_normal(modelview, normal)
    }
}

fn is_color_param(pname: u32) -> bool {
    matches!(pname, 0x1200..=0x1202 | 0x1600 | 0x1602 | 0x0b53)
}

/// Integer parameters: colors map like glColor*i (most positive integer = 1.0).
fn int_params(pname: u32, input: &[i32], out: &mut [f32; 4]) {
    let color = is_color_param(pname);
    for (o, v) in out.iter_mut().zip(input) {
        *o = if color {
            ((2. * *v as f64 + 1.) / 4294967295.) as f32
        } else {
            *v as f32
        };
    }
}

fn light_param_count(pname: u32) -> usize {
    match pname {
        0x1200..=0x1203 => 4,
        0x1204 => 3,
        0x1205..=0x1209 => 1,
        _ => 0,
    }
}

unsafe fn set_light(light: u32, pname: u32, p: &[f32; 4]) {
    with_frame(|s, m| {
        if !(0x4000..0x4000 + MAX_LIGHTS as u32).contains(&light) {
            m.fail(0x0500);
            return;
        }
        let mv = m.modelview();
        let li = &mut s.light.lights[(light - 0x4000) as usize];
        match pname {
            0x1200 => li.ambient = *p,
            0x1201 => li.diffuse = *p,
            0x1202 => li.specular = *p,
            0x1203 => {
                li.position = core::array::from_fn(|r| (0..4).map(|c| mv[c * 4 + r] * p[c]).sum());
            }
            0x1204 => {
                li.spot_direction =
                    core::array::from_fn(|r| (0..3).map(|c| mv[c * 4 + r] * p[c]).sum());
            }
            0x1205 => {
                if !(0. ..=128.).contains(&p[0]) {
                    m.fail(0x0501);
                    return;
                }
                li.spot_exponent = p[0];
            }
            0x1206 => {
                if !(0. ..=90.).contains(&p[0]) && p[0] != 180. {
                    m.fail(0x0501);
                    return;
                }
                li.spot_cutoff = p[0];
            }
            0x1207..=0x1209 => {
                if p[0] < 0. {
                    m.fail(0x0501);
                    return;
                }
                li.attenuation[(pname - 0x1207) as usize] = p[0];
            }
            _ => {
                m.fail(0x0500);
                return;
            }
        }
        li.update();
    });
}

#[no_mangle]
pub unsafe extern "C" fn glLightfv(light: u32, pname: u32, params: *const f32) {
    let n = light_param_count(pname);
    if n == 0 || params.is_null() {
        with_frame(|_, m| m.fail(if n == 0 { 0x0500 } else { 0x0501 }));
        return;
    }
    let mut p = [0.; 4];
    for (i, v) in p.iter_mut().enumerate().take(n) {
        *v = params.add(i).read();
    }
    set_light(light, pname, &p);
}

#[no_mangle]
pub unsafe extern "C" fn glLightf(light: u32, pname: u32, param: f32) {
    if light_param_count(pname) != 1 {
        with_frame(|_, m| m.fail(0x0500));
        return;
    }
    set_light(light, pname, &[param, 0., 0., 0.]);
}

#[no_mangle]
pub unsafe extern "C" fn glLightiv(light: u32, pname: u32, params: *const i32) {
    let n = light_param_count(pname);
    if n == 0 || params.is_null() {
        with_frame(|_, m| m.fail(if n == 0 { 0x0500 } else { 0x0501 }));
        return;
    }
    let mut p = [0.; 4];
    int_params(pname, slice(params, n), &mut p);
    set_light(light, pname, &p);
}

#[no_mangle]
pub unsafe extern "C" fn glLighti(light: u32, pname: u32, param: i32) {
    if light_param_count(pname) != 1 {
        with_frame(|_, m| m.fail(0x0500));
        return;
    }
    glLightiv(light, pname, &param);
}

unsafe fn slice<'a>(p: *const i32, n: usize) -> &'a [i32] {
    core::slice::from_raw_parts(p, n)
}

fn light_model_count(pname: u32) -> usize {
    match pname {
        0x0b53 => 4,
        0x0b51 | 0x0b52 => 1,
        _ => 0,
    }
}

unsafe fn set_light_model(pname: u32, p: &[f32; 4]) {
    with_frame(|s, m| match pname {
        0x0b53 => s.light.model_ambient = *p,
        0x0b51 => s.light.local_viewer = p[0] != 0.,
        0x0b52 => s.light.two_side = p[0] != 0.,
        _ => m.fail(0x0500),
    });
}

#[no_mangle]
pub unsafe extern "C" fn glLightModelfv(pname: u32, params: *const f32) {
    let n = light_model_count(pname);
    if n == 0 || params.is_null() {
        with_frame(|_, m| m.fail(if n == 0 { 0x0500 } else { 0x0501 }));
        return;
    }
    let mut p = [0.; 4];
    for (i, v) in p.iter_mut().enumerate().take(n) {
        *v = params.add(i).read();
    }
    set_light_model(pname, &p);
}

#[no_mangle]
pub unsafe extern "C" fn glLightModelf(pname: u32, param: f32) {
    if light_model_count(pname) != 1 {
        with_frame(|_, m| m.fail(0x0500));
        return;
    }
    set_light_model(pname, &[param, 0., 0., 0.]);
}

#[no_mangle]
pub unsafe extern "C" fn glLightModeliv(pname: u32, params: *const i32) {
    let n = light_model_count(pname);
    if n == 0 || params.is_null() {
        with_frame(|_, m| m.fail(if n == 0 { 0x0500 } else { 0x0501 }));
        return;
    }
    let mut p = [0.; 4];
    int_params(pname, slice(params, n), &mut p);
    set_light_model(pname, &p);
}

#[no_mangle]
pub unsafe extern "C" fn glLightModeli(pname: u32, param: i32) {
    if light_model_count(pname) != 1 {
        with_frame(|_, m| m.fail(0x0500));
        return;
    }
    glLightModeliv(pname, &param);
}

fn material_count(pname: u32) -> usize {
    match pname {
        0x1200..=0x1202 | 0x1600 | 0x1602 => 4,
        0x1603 => 3,
        0x1601 => 1,
        _ => 0,
    }
}

fn face_valid(face: u32) -> bool {
    matches!(face, 0x0404 | 0x0405 | 0x0408)
}

/// Also allowed between glBegin and glEnd: the next vertices are lit with the new material.
unsafe fn set_material(face: u32, pname: u32, p: &[f32; 4]) {
    with_immediate(|r| {
        if !face_valid(face) || material_count(pname) == 0 {
            r.matrices.fail(0x0500);
            return;
        }
        if pname == 0x1601 && !(0. ..=128.).contains(&p[0]) {
            r.matrices.fail(0x0501);
            return;
        }
        for (f, mat) in r.frame.light.material.iter_mut().enumerate() {
            if face != 0x0408 && face != if f == 1 { 0x0405 } else { 0x0404 } {
                continue;
            }
            match pname {
                0x1200 => mat.ambient = *p,
                0x1201 => mat.diffuse = *p,
                0x1602 => {
                    mat.ambient = *p;
                    mat.diffuse = *p;
                }
                0x1202 => mat.specular = *p,
                0x1600 => mat.emission = *p,
                0x1601 => mat.shininess = p[0],
                _ => mat.color_indexes = [p[0], p[1], p[2]],
            }
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glMaterialfv(face: u32, pname: u32, params: *const f32) {
    let n = material_count(pname);
    if n == 0 || params.is_null() {
        with_frame(|_, m| m.fail(if n == 0 { 0x0500 } else { 0x0501 }));
        return;
    }
    let mut p = [0.; 4];
    for (i, v) in p.iter_mut().enumerate().take(n) {
        *v = params.add(i).read();
    }
    set_material(face, pname, &p);
}

#[no_mangle]
pub unsafe extern "C" fn glMaterialf(face: u32, pname: u32, param: f32) {
    if pname != 0x1601 {
        with_frame(|_, m| m.fail(0x0500));
        return;
    }
    set_material(face, pname, &[param, 0., 0., 0.]);
}

#[no_mangle]
pub unsafe extern "C" fn glMaterialiv(face: u32, pname: u32, params: *const i32) {
    let n = material_count(pname);
    if n == 0 || params.is_null() {
        with_frame(|_, m| m.fail(if n == 0 { 0x0500 } else { 0x0501 }));
        return;
    }
    let mut p = [0.; 4];
    int_params(pname, slice(params, n), &mut p);
    set_material(face, pname, &p);
}

#[no_mangle]
pub unsafe extern "C" fn glMateriali(face: u32, pname: u32, param: i32) {
    if pname != 0x1601 {
        with_frame(|_, m| m.fail(0x0500));
        return;
    }
    glMaterialiv(face, pname, &param);
}

#[no_mangle]
pub unsafe extern "C" fn glColorMaterial(face: u32, mode: u32) {
    with_immediate(|r| {
        let mode_valid = matches!(mode, 0x1600 | 0x1200 | 0x1201 | 0x1202 | 0x1602);
        if !face_valid(face) || !mode_valid {
            r.matrices.fail(0x0500);
            return;
        }
        let current = r
            .immediate
            .color
            .map(crate::immediate::gpu_color_component)
            .map(|c| c as u8);
        r.frame.light.color_material_face = face;
        r.frame.light.color_material_mode = mode;
        if r.frame.light.color_material {
            r.frame.light.apply_color_material(current);
        }
    });
}

/// glGetLight: values and their count (0: error); `color`: the values are colors.
unsafe fn get_light(light: u32, pname: u32) -> Result<([f32; 4], usize, bool), u32> {
    let mut result = Err(0x0500);
    with_frame(|s, _| {
        if !(0x4000..0x4000 + MAX_LIGHTS as u32).contains(&light) {
            return;
        }
        let li = &s.light.lights[(light - 0x4000) as usize];
        let (v, n) = match pname {
            0x1200 => (li.ambient, 4),
            0x1201 => (li.diffuse, 4),
            0x1202 => (li.specular, 4),
            0x1203 => (li.position, 4),
            0x1204 => (
                [
                    li.spot_direction[0],
                    li.spot_direction[1],
                    li.spot_direction[2],
                    0.,
                ],
                3,
            ),
            0x1205 => ([li.spot_exponent, 0., 0., 0.], 1),
            0x1206 => ([li.spot_cutoff, 0., 0., 0.], 1),
            0x1207..=0x1209 => ([li.attenuation[(pname - 0x1207) as usize], 0., 0., 0.], 1),
            _ => return,
        };
        result = Ok((v, n, is_color_param(pname)));
    });
    result
}

unsafe fn get_material(face: u32, pname: u32) -> Result<([f32; 4], usize, bool), u32> {
    let mut result = Err(0x0500);
    with_frame(|s, _| {
        if !matches!(face, 0x0404 | 0x0405) {
            return;
        }
        let m = &s.light.material[(face == 0x0405) as usize];
        let (v, n) = match pname {
            0x1200 => (m.ambient, 4),
            0x1201 => (m.diffuse, 4),
            0x1202 => (m.specular, 4),
            0x1600 => (m.emission, 4),
            0x1601 => ([m.shininess, 0., 0., 0.], 1),
            0x1603 => (
                [
                    m.color_indexes[0],
                    m.color_indexes[1],
                    m.color_indexes[2],
                    0.,
                ],
                3,
            ),
            _ => return,
        };
        result = Ok((v, n, is_color_param(pname)));
    });
    result
}

fn normalized_to_int(v: f32) -> i32 {
    let v = v as f64;
    if v >= 1. {
        2147483647
    } else if v <= -1. {
        -2147483647
    } else {
        (v * 2147483647.) as i32
    }
}

unsafe fn write_f(result: Result<([f32; 4], usize, bool), u32>, out: *mut f32) {
    match result {
        Ok((v, n, _)) if !out.is_null() => ptr::copy_nonoverlapping(v.as_ptr(), out, n),
        Ok(_) => with_frame(|_, m| m.fail(0x0501)),
        Err(e) => with_frame(|_, m| m.fail(e)),
    }
}

unsafe fn write_i(result: Result<([f32; 4], usize, bool), u32>, out: *mut i32) {
    match result {
        Ok((v, n, color)) if !out.is_null() => {
            for (i, x) in v[..n].iter().enumerate() {
                out.add(i).write(if color {
                    normalized_to_int(*x)
                } else {
                    (*x as f64 + if *x >= 0. { 0.5 } else { -0.5 }) as i32
                });
            }
        }
        Ok(_) => with_frame(|_, m| m.fail(0x0501)),
        Err(e) => with_frame(|_, m| m.fail(e)),
    }
}

#[no_mangle]
pub unsafe extern "C" fn glGetLightfv(light: u32, pname: u32, params: *mut f32) {
    write_f(get_light(light, pname), params);
}

#[no_mangle]
pub unsafe extern "C" fn glGetLightiv(light: u32, pname: u32, params: *mut i32) {
    write_i(get_light(light, pname), params);
}

#[no_mangle]
pub unsafe extern "C" fn glGetMaterialfv(face: u32, pname: u32, params: *mut f32) {
    write_f(get_material(face, pname), params);
}

#[no_mangle]
pub unsafe extern "C" fn glGetMaterialiv(face: u32, pname: u32, params: *mut i32) {
    write_i(get_material(face, pname), params);
}

impl Lighting {
    /// glGet queries of the light model and material tracking.
    pub fn query(&self, pname: u32) -> Option<([f64; 16], usize)> {
        let mut v = [0.; 16];
        let n = match pname {
            0x0b53 => {
                for (o, c) in v.iter_mut().zip(self.model_ambient) {
                    *o = c as f64;
                }
                4
            }
            0x0b51 => {
                v[0] = self.local_viewer as u8 as f64;
                1
            }
            0x0b52 => {
                v[0] = self.two_side as u8 as f64;
                1
            }
            0x0b55 => {
                v[0] = self.color_material_face as f64;
                1
            }
            0x0b56 => {
                v[0] = self.color_material_mode as f64;
                1
            }
            _ => return None,
        };
        Some((v, n))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::matrix::IDENTITY;

    #[test]
    fn defaults_and_directional_diffuse() {
        let mut l = Lighting::new();
        l.enabled = true;
        l.light_enabled = 1;
        // Light 0 is white and directional along +z: a normal facing +z gets ambient + diffuse.
        let (front, back) = l.light_vertex(&IDENTITY, [0., 0., 0., 1.], [0., 0., 1.], [255; 4]);
        // 0.2 * 0.2 ambient model + 0.2 * 0 light ambient + 1 * 0.8 diffuse = 0.84.
        assert_eq!(
            front,
            [color_byte(0.84), color_byte(0.84), color_byte(0.84), 255]
        );
        assert!(back.is_none());
        // A normal facing away only receives the model ambient.
        let (front, _) = l.light_vertex(&IDENTITY, [0., 0., 0., 1.], [0., 0., -1.], [255; 4]);
        assert_eq!(
            front,
            [color_byte(0.04), color_byte(0.04), color_byte(0.04), 255]
        );
        // Two-sided lighting lights the flipped normal with the back material.
        l.two_side = true;
        let (_, back) = l.light_vertex(&IDENTITY, [0., 0., 0., 1.], [0., 0., -1.], [255; 4]);
        assert_eq!(back.unwrap()[0], color_byte(0.84));
    }

    #[test]
    fn color_material_tracks_the_vertex_color() {
        let mut l = Lighting::new();
        l.enabled = true;
        l.light_enabled = 1;
        l.set_enabled(0x0b57, true, [0; 4]);
        let (front, _) =
            l.light_vertex(&IDENTITY, [0., 0., 0., 1.], [0., 0., 1.], [255, 0, 0, 255]);
        // Ambient and diffuse follow the color: red 0.2*0.2 + 1.0, green and blue 0.
        assert_eq!(front[0], 255);
        assert_eq!(front[1], 0);
        assert_eq!(l.material[0].diffuse, [1., 0., 0., 1.]);
    }

    #[test]
    fn spot_cone_and_normal_matrix() {
        let mut l = Lighting::new();
        l.enabled = true;
        l.light_enabled = 1;
        l.lights[0].position = [0., 0., 5., 1.];
        l.lights[0].spot_direction = [0., 0., -1.];
        l.lights[0].spot_cutoff = 10.;
        l.lights[0].update();
        // On the axis the spot lights the vertex; far off the axis it contributes nothing.
        let on = l
            .light_vertex(&IDENTITY, [0., 0., 0., 1.], [0., 0., 1.], [255; 4])
            .0[0];
        let off = l
            .light_vertex(&IDENTITY, [50., 0., 0., 1.], [0., 0., 1.], [255; 4])
            .0[0];
        assert!(on > off);
        assert_eq!(off, color_byte(0.04));
        // A scaling modelview scales eye normals by 1/scale unless normalized or rescaled.
        let mut scale = IDENTITY;
        scale[0] = 2.;
        scale[5] = 2.;
        scale[10] = 2.;
        let n = l.eye_normal(&scale, [0., 0., 1.]);
        assert!((n[2] - 0.5).abs() < 1e-6);
        l.normalize = true;
        assert!((l.eye_normal(&scale, [0., 0., 1.])[2] - 1.).abs() < 1e-6);
        assert_eq!(normalized_to_int(1.), i32::MAX);
        assert_eq!(normalized_to_int(-1.), -2147483647);
        assert_eq!(normalized_to_int(0.), 0);
    }
}
