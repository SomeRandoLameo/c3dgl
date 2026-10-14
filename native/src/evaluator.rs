//! GL evaluators: Bezier maps evaluated on the CPU, feeding immediate-mode vertices. Evaluated
//! values only feed the generated vertex; the current color and texcoords stay unchanged.
//! Evaluated normals are not produced yet (there is no lighting).
use crate::renderer::with_immediate;
use alloc::vec::Vec;
use core::slice;

const MAX_ORDER: usize = 30;
/// Components of COLOR_4, INDEX, NORMAL, TEXTURE_COORD_1..4, VERTEX_3, VERTEX_4.
const COMPONENTS: [usize; 9] = [4, 1, 3, 1, 2, 3, 4, 3, 4];
const TEX1: usize = 3;
const TEX4: usize = 6;
const VERTEX3: usize = 7;
const VERTEX4: usize = 8;

#[derive(Default)]
pub(crate) struct Map {
    enabled: bool,
    points: Vec<f32>,
    uorder: usize,
    vorder: usize,
    u: [f32; 2],
    v: [f32; 2],
}

impl Map {
    fn active(&self) -> bool {
        self.enabled && !self.points.is_empty()
    }
}

pub(crate) struct Evaluators {
    map1: [Map; 9],
    map2: [Map; 9],
    pub auto_normal: bool,
    grid1: (i32, f32, f32),
    grid2: (i32, f32, f32, i32, f32, f32),
}

/// An evaluated vertex: position, and the values that replace the current color and texcoord.
pub(crate) struct Evaluated {
    pub position: [f32; 4],
    pub color: Option<[f32; 4]>,
    pub texcoord: Option<[f32; 4]>,
}

/// What glPushAttrib keeps: map enables, auto-normal and the grids.
#[derive(Clone, Copy)]
pub(crate) struct EvalSave {
    map1: [bool; 9],
    map2: [bool; 9],
    auto_normal: bool,
    grid1: (i32, f32, f32),
    grid2: (i32, f32, f32, i32, f32, f32),
}

impl Evaluators {
    pub fn save(&self) -> EvalSave {
        EvalSave {
            map1: core::array::from_fn(|i| self.map1[i].enabled),
            map2: core::array::from_fn(|i| self.map2[i].enabled),
            auto_normal: self.auto_normal,
            grid1: self.grid1,
            grid2: self.grid2,
        }
    }

    pub fn restore_enables(&mut self, s: &EvalSave) {
        for i in 0..9 {
            self.map1[i].enabled = s.map1[i];
            self.map2[i].enabled = s.map2[i];
        }
        self.auto_normal = s.auto_normal;
    }

    pub fn restore_grids(&mut self, s: &EvalSave) {
        self.grid1 = s.grid1;
        self.grid2 = s.grid2;
    }

    pub fn new() -> Self {
        Self {
            map1: core::array::from_fn(|_| Map::default()),
            map2: core::array::from_fn(|_| Map::default()),
            auto_normal: false,
            grid1: (1, 0., 1.),
            grid2: (1, 0., 1., 1, 0., 1.),
        }
    }

    /// GL_MAP1_* / GL_MAP2_* / GL_AUTO_NORMAL; false for other capabilities.
    pub fn set_enabled(&mut self, cap: u32, on: bool) -> bool {
        match cap {
            0x0d80 => self.auto_normal = on,
            0x0d90..=0x0d98 => self.map1[(cap - 0x0d90) as usize].enabled = on,
            0x0db0..=0x0db8 => self.map2[(cap - 0x0db0) as usize].enabled = on,
            _ => return false,
        }
        true
    }

    pub fn is_enabled(&self, cap: u32) -> Option<bool> {
        match cap {
            0x0d80 => Some(self.auto_normal),
            0x0d90..=0x0d98 => Some(self.map1[(cap - 0x0d90) as usize].enabled),
            0x0db0..=0x0db8 => Some(self.map2[(cap - 0x0db0) as usize].enabled),
            _ => None,
        }
    }

    fn texcoord_map(maps: &[Map; 9]) -> Option<usize> {
        (TEX1..=TEX4).rev().find(|i| maps[*i].active())
    }

    fn vertex_map(maps: &[Map; 9]) -> Option<usize> {
        [VERTEX4, VERTEX3].into_iter().find(|i| maps[*i].active())
    }

    fn build(
        vertex: usize,
        pos: [f32; 4],
        color: Option<[f32; 4]>,
        tex: Option<(usize, [f32; 4])>,
    ) -> Option<Evaluated> {
        let w = if COMPONENTS[vertex] == 4 { pos[3] } else { 1. };
        if w == 0. {
            // Points at infinity are not supported.
            return None;
        }
        let texcoord = tex.map(|(i, t)| {
            let size = COMPONENTS[i];
            [
                t[0],
                if size > 1 { t[1] } else { 0. },
                if size > 2 { t[2] } else { 0. },
                if size > 3 { t[3] } else { 1. },
            ]
        });
        Some(Evaluated {
            position: [pos[0] / w, pos[1] / w, pos[2] / w, 1.],
            color,
            texcoord,
        })
    }

    pub fn eval1(&self, u: f32) -> Option<Evaluated> {
        let maps = &self.map1;
        let vertex = Self::vertex_map(maps)?;
        let value = |i: usize| eval_map1(&maps[i], COMPONENTS[i], u);
        let color = maps[0].active().then(|| value(0));
        let tex = Self::texcoord_map(maps).map(|i| (i, value(i)));
        Self::build(vertex, value(vertex), color, tex)
    }

    pub fn eval2(&self, u: f32, v: f32) -> Option<Evaluated> {
        let maps = &self.map2;
        let vertex = Self::vertex_map(maps)?;
        let value = |i: usize| eval_map2(&maps[i], COMPONENTS[i], u, v);
        let color = maps[0].active().then(|| value(0));
        let tex = Self::texcoord_map(maps).map(|i| (i, value(i)));
        Self::build(vertex, value(vertex), color, tex)
    }
}

/// Bernstein basis of degree n - 1 at t, built up degree by degree.
fn bernstein(n: usize, t: f32, b: &mut [f32; MAX_ORDER]) {
    b[0] = 1.;
    for d in 1..n {
        b[d] = t * b[d - 1];
        for i in (1..d).rev() {
            b[i] = (1. - t) * b[i] + t * b[i - 1];
        }
        b[0] *= 1. - t;
    }
}

fn eval_map1(m: &Map, k: usize, u: f32) -> [f32; 4] {
    let mut b = [0.; MAX_ORDER];
    bernstein(m.uorder, (u - m.u[0]) / (m.u[1] - m.u[0]), &mut b);
    let mut out = [0.; 4];
    for i in 0..m.uorder {
        for c in 0..k {
            out[c] += b[i] * m.points[i * k + c];
        }
    }
    out
}

fn eval_map2(m: &Map, k: usize, u: f32, v: f32) -> [f32; 4] {
    let (mut bu, mut bv) = ([0.; MAX_ORDER], [0.; MAX_ORDER]);
    bernstein(m.uorder, (u - m.u[0]) / (m.u[1] - m.u[0]), &mut bu);
    bernstein(m.vorder, (v - m.v[0]) / (m.v[1] - m.v[0]), &mut bv);
    // Sum over v first, then over u.
    let mut out = [0.; 4];
    let mut p = 0;
    for i in 0..m.uorder {
        let mut q = [0.; 4];
        for j in 0..m.vorder {
            for c in 0..k {
                q[c] += bv[j] * m.points[p + c];
            }
            p += k;
        }
        for c in 0..k {
            out[c] += bu[i] * q[c];
        }
    }
    out
}

/// Grid coordinate i of n segments over [a, b]; exactly b at i = n.
fn grid(i: i32, n: i32, a: f32, b: f32) -> f32 {
    if i == n {
        b
    } else {
        a + (b - a) * i as f32 / n as f32
    }
}

unsafe fn submit(r: &mut crate::renderer::Renderer, e: Evaluated) {
    if e.texcoord.is_some_and(|t| t[3] != 1.) {
        r.frame.textures.mark_q();
    }
    crate::renderer::emit_vertex(r, e.position, e.color, e.texcoord);
}

unsafe fn eval_coord1(u: f32) {
    with_immediate(|r| {
        if !r.immediate.active {
            return;
        }
        if let Some(e) = r.frame.eval.eval1(u) {
            submit(r, e);
        }
    });
}

unsafe fn eval_coord2(u: f32, v: f32) {
    with_immediate(|r| {
        if !r.immediate.active {
            return;
        }
        if let Some(e) = r.frame.eval.eval2(u, v) {
            submit(r, e);
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glEvalCoord1f(u: f32) {
    eval_coord1(u);
}

#[no_mangle]
pub unsafe extern "C" fn glEvalCoord1d(u: f64) {
    eval_coord1(u as f32);
}

#[no_mangle]
pub unsafe extern "C" fn glEvalCoord1fv(u: *const f32) {
    if u.is_null() {
        with_immediate(|r| r.matrices.fail(0x0501));
    } else {
        eval_coord1(u.read());
    }
}

#[no_mangle]
pub unsafe extern "C" fn glEvalCoord2f(u: f32, v: f32) {
    eval_coord2(u, v);
}

#[no_mangle]
pub unsafe extern "C" fn glEvalCoord2d(u: f64, v: f64) {
    eval_coord2(u as f32, v as f32);
}

#[no_mangle]
pub unsafe extern "C" fn glEvalCoord2fv(uv: *const f32) {
    if uv.is_null() {
        with_immediate(|r| r.matrices.fail(0x0501));
    } else {
        eval_coord2(uv.read(), uv.add(1).read());
    }
}

#[derive(Clone, Copy)]
enum Source {
    F32(*const f32),
    F64(*const f64),
}

impl Source {
    unsafe fn at(self, i: usize) -> f32 {
        match self {
            Source::F32(p) => p.add(i).read(),
            Source::F64(p) => p.add(i).read() as f32,
        }
    }
}

#[allow(clippy::too_many_arguments)]
unsafe fn define_map(
    target: u32,
    two_d: bool,
    u: [f64; 2],
    ustride: i32,
    uorder: i32,
    v: [f64; 2],
    vstride: i32,
    vorder: i32,
    points: Source,
) {
    with_immediate(|r| {
        let base = if two_d { 0x0db0 } else { 0x0d90 };
        if !(base..=base + 8).contains(&target) {
            r.matrices.fail(0x0500);
            return;
        }
        let index = (target - base) as usize;
        let k = COMPONENTS[index];
        let bad_order = |n: i32| n < 1 || n as usize > MAX_ORDER;
        if bad_order(uorder)
            || bad_order(vorder)
            || u[0] == u[1]
            || (two_d && v[0] == v[1])
            || (ustride as usize) < k
            || (two_d && (vstride as usize) < k)
            || ustride < 0
            || vstride < 0
        {
            r.matrices.fail(0x0501);
            return;
        }
        if r.immediate.active {
            r.matrices.fail(0x0502);
            return;
        }
        let (uorder, vorder) = (uorder as usize, vorder as usize);
        let mut data = Vec::new();
        if data.try_reserve_exact(uorder * vorder * k).is_err() {
            r.matrices.fail(0x0505);
            return;
        }
        for i in 0..uorder {
            for j in 0..vorder {
                for c in 0..k {
                    data.push(points.at(i * ustride as usize + j * vstride as usize + c));
                }
            }
        }
        let maps = if two_d {
            &mut r.frame.eval.map2
        } else {
            &mut r.frame.eval.map1
        };
        let m = &mut maps[index];
        m.points = data;
        m.uorder = uorder;
        m.vorder = vorder;
        m.u = [u[0] as f32, u[1] as f32];
        m.v = [v[0] as f32, v[1] as f32];
    });
}

#[no_mangle]
pub unsafe extern "C" fn glMap1f(
    target: u32,
    u1: f32,
    u2: f32,
    stride: i32,
    order: i32,
    points: *const f32,
) {
    define_map(
        target,
        false,
        [u1 as f64, u2 as f64],
        stride,
        order,
        [0., 1.],
        0,
        1,
        Source::F32(points),
    );
}

#[no_mangle]
pub unsafe extern "C" fn glMap1d(
    target: u32,
    u1: f64,
    u2: f64,
    stride: i32,
    order: i32,
    points: *const f64,
) {
    define_map(
        target,
        false,
        [u1, u2],
        stride,
        order,
        [0., 1.],
        0,
        1,
        Source::F64(points),
    );
}

#[no_mangle]
pub unsafe extern "C" fn glMap2f(
    target: u32,
    u1: f32,
    u2: f32,
    ustride: i32,
    uorder: i32,
    v1: f32,
    v2: f32,
    vstride: i32,
    vorder: i32,
    points: *const f32,
) {
    define_map(
        target,
        true,
        [u1 as f64, u2 as f64],
        ustride,
        uorder,
        [v1 as f64, v2 as f64],
        vstride,
        vorder,
        Source::F32(points),
    );
}

#[no_mangle]
pub unsafe extern "C" fn glMap2d(
    target: u32,
    u1: f64,
    u2: f64,
    ustride: i32,
    uorder: i32,
    v1: f64,
    v2: f64,
    vstride: i32,
    vorder: i32,
    points: *const f64,
) {
    define_map(
        target,
        true,
        [u1, u2],
        ustride,
        uorder,
        [v1, v2],
        vstride,
        vorder,
        Source::F64(points),
    );
}

#[no_mangle]
pub unsafe extern "C" fn glMapGrid1f(un: i32, u1: f32, u2: f32) {
    with_immediate(|r| {
        if un <= 0 {
            r.matrices.fail(0x0501);
        } else {
            r.frame.eval.grid1 = (un, u1, u2);
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glMapGrid1d(un: i32, u1: f64, u2: f64) {
    glMapGrid1f(un, u1 as f32, u2 as f32);
}

#[no_mangle]
pub unsafe extern "C" fn glMapGrid2f(un: i32, u1: f32, u2: f32, vn: i32, v1: f32, v2: f32) {
    with_immediate(|r| {
        if un <= 0 || vn <= 0 {
            r.matrices.fail(0x0501);
        } else {
            r.frame.eval.grid2 = (un, u1, u2, vn, v1, v2);
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glMapGrid2d(un: i32, u1: f64, u2: f64, vn: i32, v1: f64, v2: f64) {
    glMapGrid2f(un, u1 as f32, u2 as f32, vn, v1 as f32, v2 as f32);
}

fn grid_point1(e: &Evaluators, i: i32) -> f32 {
    let (n, a, b) = e.grid1;
    grid(i, n, a, b)
}

fn grid_point2(e: &Evaluators, i: i32, j: i32) -> (f32, f32) {
    let (un, u1, u2, vn, v1, v2) = e.grid2;
    (grid(i, un, u1, u2), grid(j, vn, v1, v2))
}

#[no_mangle]
pub unsafe extern "C" fn glEvalPoint1(i: i32) {
    let mut u = 0.;
    with_immediate(|r| u = grid_point1(&r.frame.eval, i));
    eval_coord1(u);
}

#[no_mangle]
pub unsafe extern "C" fn glEvalPoint2(i: i32, j: i32) {
    let mut uv = (0., 0.);
    with_immediate(|r| uv = grid_point2(&r.frame.eval, i, j));
    eval_coord2(uv.0, uv.1);
}

unsafe fn in_begin() -> bool {
    let mut active = false;
    with_immediate(|r| active = r.immediate.active);
    active
}

#[no_mangle]
pub unsafe extern "C" fn glEvalMesh1(mode: u32, i1: i32, i2: i32) {
    if !matches!(mode, 0x1b00 | 0x1b01) {
        with_immediate(|r| r.matrices.fail(0x0500));
        return;
    }
    if in_begin() {
        with_immediate(|r| r.matrices.fail(0x0502));
        return;
    }
    crate::immediate::glBegin(if mode == 0x1b00 { 0 } else { 3 });
    for i in i1..=i2 {
        glEvalPoint1(i);
    }
    crate::immediate::glEnd();
}

#[no_mangle]
pub unsafe extern "C" fn glEvalMesh2(mode: u32, i1: i32, i2: i32, j1: i32, j2: i32) {
    use crate::immediate::{glBegin, glEnd};
    if !matches!(mode, 0x1b00..=0x1b02) {
        with_immediate(|r| r.matrices.fail(0x0500));
        return;
    }
    if in_begin() {
        with_immediate(|r| r.matrices.fail(0x0502));
        return;
    }
    match mode {
        0x1b02 => {
            for j in j1..j2 {
                glBegin(8);
                for i in i1..=i2 {
                    glEvalPoint2(i, j);
                    glEvalPoint2(i, j + 1);
                }
                glEnd();
            }
        }
        0x1b01 => {
            for j in j1..=j2 {
                glBegin(3);
                for i in i1..=i2 {
                    glEvalPoint2(i, j);
                }
                glEnd();
            }
            for i in i1..=i2 {
                glBegin(3);
                for j in j1..=j2 {
                    glEvalPoint2(i, j);
                }
                glEnd();
            }
        }
        _ => {
            glBegin(0);
            for j in j1..=j2 {
                for i in i1..=i2 {
                    glEvalPoint2(i, j);
                }
            }
            glEnd();
        }
    }
}

/// glGetMap: GL_COEFF (control points), GL_ORDER or GL_DOMAIN.
unsafe fn get_map(target: u32, query: u32, out: &mut [f64; 30 * 30 * 4]) -> Result<usize, u32> {
    let mut result = Err(0x0500);
    with_immediate(|r| {
        let (two_d, index) = match target {
            0x0d90..=0x0d98 => (false, (target - 0x0d90) as usize),
            0x0db0..=0x0db8 => (true, (target - 0x0db0) as usize),
            _ => return,
        };
        let m = if two_d {
            &r.frame.eval.map2[index]
        } else {
            &r.frame.eval.map1[index]
        };
        let loaded = !m.points.is_empty();
        result = match query {
            0x0a00 => {
                for (o, p) in out.iter_mut().zip(&m.points) {
                    *o = *p as f64;
                }
                Ok(m.points.len())
            }
            0x0a01 => {
                out[0] = if loaded { m.uorder as f64 } else { 1. };
                out[1] = if loaded { m.vorder as f64 } else { 1. };
                Ok(if two_d { 2 } else { 1 })
            }
            0x0a02 => {
                out[0] = if loaded { m.u[0] as f64 } else { 0. };
                out[1] = if loaded { m.u[1] as f64 } else { 1. };
                out[2] = if loaded { m.v[0] as f64 } else { 0. };
                out[3] = if loaded { m.v[1] as f64 } else { 1. };
                Ok(if two_d { 4 } else { 2 })
            }
            _ => Err(0x0500),
        };
    });
    result
}

#[no_mangle]
pub unsafe extern "C" fn glGetMapdv(target: u32, query: u32, v: *mut f64) {
    let mut values = [0.; 30 * 30 * 4];
    match get_map(target, query, &mut values) {
        Ok(n) if !v.is_null() => slice::from_raw_parts_mut(v, n).copy_from_slice(&values[..n]),
        Ok(_) => with_immediate(|r| r.matrices.fail(0x0501)),
        Err(e) => with_immediate(|r| r.matrices.fail(e)),
    }
}

#[no_mangle]
pub unsafe extern "C" fn glGetMapfv(target: u32, query: u32, v: *mut f32) {
    let mut values = [0.; 30 * 30 * 4];
    match get_map(target, query, &mut values) {
        Ok(n) if !v.is_null() => {
            for (i, x) in values[..n].iter().enumerate() {
                v.add(i).write(*x as f32);
            }
        }
        Ok(_) => with_immediate(|r| r.matrices.fail(0x0501)),
        Err(e) => with_immediate(|r| r.matrices.fail(e)),
    }
}

#[no_mangle]
pub unsafe extern "C" fn glGetMapiv(target: u32, query: u32, v: *mut i32) {
    let mut values = [0.; 30 * 30 * 4];
    match get_map(target, query, &mut values) {
        Ok(n) if !v.is_null() => {
            for (i, x) in values[..n].iter().enumerate() {
                let r = if *x >= 0. { *x + 0.5 } else { *x - 0.5 };
                v.add(i).write(r as i32);
            }
        }
        Ok(_) => with_immediate(|r| r.matrices.fail(0x0501)),
        Err(e) => with_immediate(|r| r.matrices.fail(e)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn map(points: &[f32], order: usize) -> Map {
        Map {
            enabled: true,
            points: points.to_vec(),
            uorder: order,
            vorder: 1,
            u: [0., 1.],
            v: [0., 1.],
        }
    }

    #[test]
    fn bezier_endpoints_midpoint_and_rational_circle() {
        let mut e = Evaluators::new();
        // Cubic through (0,0,0) .. (3,0,0) with control points on a line: x(t) = 3t.
        e.map1[VERTEX3] = map(&[0., 0., 0., 1., 0., 0., 2., 0., 0., 3., 0., 0.], 4);
        let p = e.eval1(0.5).unwrap();
        assert!((p.position[0] - 1.5).abs() < 1e-6 && p.position[3] == 1.);
        assert_eq!(e.eval1(0.).unwrap().position[0], 0.);
        assert!((e.eval1(1.).unwrap().position[0] - 3.).abs() < 1e-6);
        // Quarter circle: rational quadratic with weights (1, w, 1).
        let w = core::f32::consts::FRAC_1_SQRT_2;
        e.map1[VERTEX4] = map(&[1., 0., 0., 1., w, w, 0., w, 0., 1., 0., 1.], 3);
        let p = e.eval1(0.37).unwrap().position;
        assert!(((p[0] * p[0] + p[1] * p[1]).sqrt() - 1.).abs() < 1e-5);
        // A w = 0 vertex is dropped; a disabled or missing map gives no vertex.
        e.map1[VERTEX4] = map(&[1., 0., 0., 0., 1., 0., 0., 0.], 2);
        assert!(e.eval1(0.5).is_none());
        assert!(Evaluators::new().eval1(0.5).is_none());
        assert_eq!(grid(5, 5, 0., 2.), 2.);
        assert_eq!(grid(1, 4, 0., 2.), 0.5);
    }

    #[test]
    fn color_texcoord_maps_and_capabilities() {
        let mut e = Evaluators::new();
        e.map1[VERTEX3] = map(&[0., 0., 0., 1., 1., 0.], 2);
        e.map1[0] = map(&[1., 0., 0., 1., 0., 0., 1., 1.], 2);
        e.map1[TEX1 + 1] = map(&[0., 0., 2., 4.], 2);
        let v = e.eval1(0.5).unwrap();
        assert_eq!(v.color.unwrap(), [0.5, 0., 0.5, 1.]);
        assert_eq!(v.texcoord.unwrap(), [1., 2., 0., 1.]);
        assert!(e.set_enabled(0x0d98, true) && e.is_enabled(0x0d98) == Some(true));
        assert!(!e.set_enabled(0x0de1, true) && e.is_enabled(0x0de1).is_none());
    }
}
