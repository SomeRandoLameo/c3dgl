//! User clip planes (glClipPlane): clipped on the CPU in object space before lines and points
//! are expanded and before polygons are filled, outlined or drawn as vertices. Attributes are
//! interpolated linearly like GL does in clip space.
use crate::{
    lines::lerp,
    renderer::{with_immediate, GpuVertex},
};
use alloc::vec::Vec;

pub(crate) const MAX_PLANES: usize = 6;

/// Planes in eye coordinates and the enable mask.
#[derive(Clone, Copy)]
pub(crate) struct ClipState {
    pub planes: [[f32; 4]; MAX_PLANES],
    pub enabled: u8,
}

impl ClipState {
    pub fn new() -> Self {
        Self {
            planes: [[0.; 4]; MAX_PLANES],
            enabled: 0,
        }
    }

    /// The enabled planes in object coordinates (p_obj = p_eye * M).
    pub fn object_planes(&self, modelview: &[f32; 16]) -> Planes {
        let mut out = Planes::default();
        for i in 0..MAX_PLANES {
            if self.enabled >> i & 1 == 0 {
                continue;
            }
            let p = &self.planes[i];
            out.p[out.n] = core::array::from_fn(|c| {
                p[0] * modelview[c * 4]
                    + p[1] * modelview[c * 4 + 1]
                    + p[2] * modelview[c * 4 + 2]
                    + p[3] * modelview[c * 4 + 3]
            });
            out.n += 1;
        }
        out
    }
}

#[derive(Clone, Copy, Default)]
pub(crate) struct Planes {
    n: usize,
    p: [[f32; 4]; MAX_PLANES],
}

fn distance(plane: &[f32; 4], v: &GpuVertex) -> f32 {
    plane[0] * v.position[0] + plane[1] * v.position[1] + plane[2] * v.position[2] + plane[3]
}

impl Planes {
    pub fn any(&self) -> bool {
        self.n > 0
    }

    /// Points are kept or dropped whole.
    pub fn point_clipped(&self, v: &GpuVertex) -> bool {
        self.p[..self.n].iter().any(|p| distance(p, v) < 0.)
    }

    /// Clip the segment a -> b; None when nothing is left.
    pub fn segment(&self, a: &GpuVertex, b: &GpuVertex) -> Option<(GpuVertex, GpuVertex)> {
        let (mut t0, mut t1) = (0f32, 1f32);
        for p in &self.p[..self.n] {
            let (da, db) = (distance(p, a), distance(p, b));
            if da < 0. && db < 0. {
                return None;
            }
            if da < 0. {
                t0 = t0.max(da / (da - db));
            } else if db < 0. {
                t1 = t1.min(da / (da - db));
            }
        }
        if t0 > t1 {
            return None;
        }
        let mut va = *a;
        let mut vb = *b;
        if t0 > 0. {
            va = lerp(a, b, t0);
        }
        if t1 < 1. {
            vb = lerp(a, b, t1);
        }
        Some((va, vb))
    }

    /// Sutherland-Hodgman against the enabled planes; edges along a clip plane are not outlined.
    /// `backs` may be empty. Fewer than 3 vertices: nothing is left.
    pub fn polygon(
        &self,
        vs: &[GpuVertex],
        backs: &[[f32; 4]],
        edges: &[bool],
    ) -> (Vec<GpuVertex>, Vec<[f32; 4]>, Vec<bool>) {
        let two = !backs.is_empty();
        let mut verts = vs.to_vec();
        let mut back = backs.to_vec();
        let mut flags = edges.to_vec();
        for plane in &self.p[..self.n] {
            if verts.len() < 3 {
                break;
            }
            let n = verts.len();
            let (mut ov, mut ob, mut oe) = (Vec::new(), Vec::new(), Vec::new());
            let mut da = distance(plane, &verts[0]);
            for i in 0..n {
                let (a, b) = (&verts[i], &verts[(i + 1) % n]);
                let db = distance(plane, b);
                if da >= 0. {
                    ov.push(*a);
                    if two {
                        ob.push(back[i]);
                    }
                    oe.push(flags[i]);
                }
                if (da >= 0.) != (db >= 0.) {
                    let t = da / (da - db);
                    ov.push(lerp(a, b, t));
                    if two {
                        let j = (i + 1) % n;
                        ob.push(core::array::from_fn(|k| {
                            (back[i][k] + (back[j][k] - back[i][k]) * t) as u8 as f32
                        }));
                    }
                    // Leaving: the next edge runs along the plane.
                    oe.push(da < 0. && flags[i]);
                }
                da = db;
            }
            verts = ov;
            back = ob;
            flags = oe;
        }
        if verts.len() < 3 {
            return (Vec::new(), Vec::new(), Vec::new());
        }
        (verts, back, flags)
    }
}

fn plane_valid(plane: u32) -> Option<usize> {
    (0x3000..0x3000 + MAX_PLANES as u32)
        .contains(&plane)
        .then(|| (plane - 0x3000) as usize)
}

unsafe fn set_plane(plane: u32, e: [f64; 4]) {
    with_immediate(|r| {
        if r.immediate.active {
            r.matrices.fail(0x0502);
            return;
        }
        let Some(i) = plane_valid(plane) else {
            r.matrices.fail(0x0500);
            return;
        };
        // Stored in eye coordinates: p_eye = p * M^-1 with the current modelview.
        let inv = crate::texgen::invert(&r.matrices.modelview());
        r.frame.clip.planes[i] = core::array::from_fn(|c| {
            (e[0] * inv[c * 4] as f64
                + e[1] * inv[c * 4 + 1] as f64
                + e[2] * inv[c * 4 + 2] as f64
                + e[3] * inv[c * 4 + 3] as f64) as f32
        });
    });
}

#[no_mangle]
pub unsafe extern "C" fn glClipPlane(plane: u32, equation: *const f64) {
    if equation.is_null() {
        with_immediate(|r| r.matrices.fail(0x0501));
        return;
    }
    set_plane(plane, core::array::from_fn(|i| equation.add(i).read()));
}

#[no_mangle]
pub unsafe extern "C" fn glClipPlanef(plane: u32, equation: *const f32) {
    if equation.is_null() {
        with_immediate(|r| r.matrices.fail(0x0501));
        return;
    }
    set_plane(
        plane,
        core::array::from_fn(|i| equation.add(i).read() as f64),
    );
}

unsafe fn get_plane(plane: u32) -> Option<[f32; 4]> {
    let mut out = None;
    with_immediate(|r| {
        if r.immediate.active {
            r.matrices.fail(0x0502);
            return;
        }
        match plane_valid(plane) {
            Some(i) => out = Some(r.frame.clip.planes[i]),
            None => r.matrices.fail(0x0500),
        }
    });
    out
}

#[no_mangle]
pub unsafe extern "C" fn glGetClipPlane(plane: u32, equation: *mut f64) {
    if let Some(p) = get_plane(plane) {
        for (i, v) in p.iter().enumerate() {
            equation.add(i).write(*v as f64);
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn glGetClipPlanef(plane: u32, equation: *mut f32) {
    if let Some(p) = get_plane(plane) {
        for (i, v) in p.iter().enumerate() {
            equation.add(i).write(*v);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn v(x: f32, y: f32) -> GpuVertex {
        GpuVertex {
            position: [x, y, 0., 1.],
            color: [0., 100., 200., 255.],
            texcoord: [x, y, 0., 1.],
            extra_texcoords: [[0., 0., 0., 1.]; 2],
        }
    }

    fn planes(p: [f32; 4]) -> Planes {
        let mut s = ClipState::new();
        s.planes[0] = p;
        s.enabled = 1;
        s.object_planes(&crate::matrix::IDENTITY)
    }

    #[test]
    fn points_segments_and_polygons_are_cut_at_the_plane() {
        // Keep x >= 0.
        let c = planes([1., 0., 0., 0.]);
        assert!(c.any() && c.point_clipped(&v(-1., 0.)) && !c.point_clipped(&v(0., 5.)));
        assert!(c.segment(&v(-2., 0.), &v(-1., 0.)).is_none());
        let (a, b) = c.segment(&v(-1., 0.), &v(1., 0.)).unwrap();
        assert_eq!((a.position[0], b.position[0]), (0., 1.));
        assert_eq!(a.texcoord[0], 0.);
        // A square straddling the plane becomes a rectangle; the edge along the plane has no flag.
        let sq = [v(-1., -1.), v(1., -1.), v(1., 1.), v(-1., 1.)];
        let (out, _, edges) = c.polygon(&sq, &[], &[true; 4]);
        assert_eq!(out.len(), 4);
        assert!(out.iter().all(|p| p.position[0] >= 0.));
        assert_eq!(edges.iter().filter(|e| !**e).count(), 1);
        // Completely outside: nothing; completely inside: unchanged.
        assert!(c
            .polygon(&[v(-3., 0.), v(-2., 0.), v(-2., 1.)], &[], &[true; 3])
            .0
            .is_empty());
        assert_eq!(
            c.polygon(&[v(1., 0.), v(2., 0.), v(2., 1.)], &[], &[true; 3])
                .0
                .len(),
            3
        );
        assert!(!Planes::default().any());
    }
}
