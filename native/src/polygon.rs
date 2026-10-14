//! CPU polygon assembly for flat shading, polygon modes, edge flags and polygon offset.
//! Filled polygons stay object-space triangles; outlines and vertices are expanded to NDC
//! (clip-space draws), culled on the CPU like the C renderer does.
use crate::{lines, renderer::GpuVertex};
use alloc::vec::Vec;

pub(crate) const POINT: u32 = 0x1b00;
pub(crate) const LINE: u32 = 0x1b01;
pub(crate) const FILL: u32 = 0x1b02;

/// r of glPolygonOffset: one step of the 24-bit depth buffer.
const DEPTH_RESOLUTION: f32 = 1. / 16777216.;
const CLIP_W_MIN: f32 = 1e-5;

pub(crate) struct Settings {
    pub flat: bool,
    /// Front and back polygon mode.
    pub modes: [u32; 2],
    /// glPolygonOffset enable for fill, line and point.
    pub offset: [bool; 3],
    pub factor: f32,
    pub units: f32,
    pub front_ccw: bool,
    pub cull: bool,
    pub cull_face: u32,
    pub range: [f32; 2],
    pub style: lines::Style,
}

impl Settings {
    fn modes_active(&self) -> bool {
        self.modes != [FILL, FILL]
    }

    /// Does anything require polygons to be assembled on the CPU?
    pub fn needed(&self) -> bool {
        self.flat
            || self.modes_active()
            || ((self.factor != 0. || self.units != 0.) && self.offset.iter().any(|o| *o))
    }
}

/// One draw: object-space triangles with a depth bias folded into the MVP, or expanded NDC quads.
pub(crate) enum Item {
    Fill {
        vertices: Vec<GpuVertex>,
        bias: f32,
    },
    Clip {
        vertices: Vec<GpuVertex>,
        indices: Vec<u16>,
    },
}

const LIMIT: usize = 4000;

struct Builder<'a> {
    s: &'a Settings,
    items: Vec<Item>,
}

fn transform(m: &[f32; 16], p: [f32; 4]) -> [f32; 4] {
    core::array::from_fn(|r| (0..4).map(|c| m[c * 4 + r] * p[c]).sum())
}

/// Twice the signed area in clip space, positive when counter-clockwise on screen.
fn area(s: &Settings, vs: &[GpuVertex]) -> f32 {
    let c0 = transform(&s.style.pmv, vs[0].position);
    let mut c1 = transform(&s.style.pmv, vs[1].position);
    let mut area = 0.;
    for v in &vs[2..] {
        let c2 = transform(&s.style.pmv, v.position);
        area += c0[0] * (c1[1] * c2[3] - c2[1] * c1[3]) - c1[0] * (c0[1] * c2[3] - c2[1] * c0[3])
            + c2[0] * (c0[1] * c1[3] - c1[1] * c0[3]);
        c1 = c2;
    }
    area
}

/// glPolygonOffset in window depth units: factor * m + r * units, m = max depth slope.
fn polygon_offset(s: &Settings, vs: &[GpuVertex]) -> f32 {
    let units = s.units * DEPTH_RESOLUTION;
    if s.factor == 0. {
        return units;
    }
    let n = vs.len();
    let half_w = 0.5 * s.style.viewport[2] as f32;
    let half_h = 0.5 * s.style.viewport[3] as f32;
    let half_d = 0.5 * (s.range[1] - s.range[0]);
    let mut w = [[0f32; 3]; 3];
    for (i, out) in w.iter_mut().enumerate() {
        let c = transform(
            &s.style.pmv,
            vs[if i == 0 { 0 } else { n - 3 + i }].position,
        );
        if c[3] < CLIP_W_MIN {
            return units;
        }
        *out = [
            c[0] / c[3] * half_w,
            c[1] / c[3] * half_h,
            c[2] / c[3] * half_d,
        ];
    }
    let e1 = [w[1][0] - w[0][0], w[1][1] - w[0][1], w[1][2] - w[0][2]];
    let e2 = [w[2][0] - w[0][0], w[2][1] - w[0][1], w[2][2] - w[0][2]];
    let nx = e1[1] * e2[2] - e1[2] * e2[1];
    let ny = e1[2] * e2[0] - e1[0] * e2[2];
    let nz = e1[0] * e2[1] - e1[1] * e2[0];
    let m = if nz.abs() > 1e-12 {
        (nx / nz).abs().max((ny / nz).abs())
    } else {
        0.
    };
    s.factor * m + units
}

impl Builder<'_> {
    fn fill(&mut self, bias: f32) -> &mut Vec<GpuVertex> {
        let new = !matches!(
            self.items.last(),
            Some(Item::Fill { vertices, bias: b }) if *b == bias && vertices.len() + 3 <= LIMIT
        );
        if new {
            self.items.push(Item::Fill {
                vertices: Vec::new(),
                bias,
            });
        }
        match self.items.last_mut() {
            Some(Item::Fill { vertices, .. }) => vertices,
            _ => unreachable!(),
        }
    }

    fn clip(&mut self) -> (&mut Vec<GpuVertex>, &mut Vec<u16>) {
        let new = !matches!(
            self.items.last(),
            Some(Item::Clip { vertices, .. }) if vertices.len() + 4 <= LIMIT
        );
        if new {
            self.items.push(Item::Clip {
                vertices: Vec::new(),
                indices: Vec::new(),
            });
        }
        match self.items.last_mut() {
            Some(Item::Clip { vertices, indices }) => (vertices, indices),
            _ => unreachable!(),
        }
    }

    /// A polygon (triangle, quad, polygon) with edge flags: filled, outlined or as vertices
    /// depending on the polygon mode of the side facing the viewer. `pv`: flat shading vertex.
    fn polygon(
        &mut self,
        vs: &[GpuVertex],
        edges: &[bool],
        pv: Option<&GpuVertex>,
    ) -> Result<(), u32> {
        let s = self.s;
        let n = vs.len();
        let active = s.modes_active();
        let front = !active || (area(s, vs) > 0.) == s.front_ccw;
        let mode = if active {
            // Outlines and vertices are drawn in NDC: the GPU cannot cull them.
            if s.cull && (s.cull_face == 0x0408 || (s.cull_face == 0x0404) == front) {
                return Ok(());
            }
            s.modes[if front { 0 } else { 1 }]
        } else {
            FILL
        };
        let offset = match mode {
            FILL => s.offset[0],
            LINE => s.offset[1],
            _ => s.offset[2],
        };
        let range = s.range[1] - s.range[0];
        let window = if offset && range != 0. {
            polygon_offset(s, vs) / range
        } else {
            0.
        };
        let shade = |v: &GpuVertex| match pv {
            Some(pv) => lines::shaded(v, pv),
            None => *v,
        };
        match mode {
            FILL => {
                let a = shade(&vs[0]);
                let out = self.fill(window);
                out.try_reserve(3 * (n - 2)).map_err(|_| 0x0505u32)?;
                for i in 1..n - 1 {
                    out.extend([a, shade(&vs[i]), shade(&vs[i + 1])]);
                }
            }
            _ => {
                // Edge i runs from vertex i to i + 1; its flag also decides whether vertex i is
                // drawn as a point.
                for i in 0..n {
                    if !edges[i] {
                        continue;
                    }
                    let a = shade(&vs[i]);
                    let (out, indices) = self.clip();
                    if mode == LINE {
                        let b = shade(&vs[(i + 1) % n]);
                        lines::segment(&s.style, &a, &b, 2. * window, out, indices)?;
                    } else {
                        lines::point(&s.style, &a, 2. * window, out, indices)?;
                    }
                }
            }
        }
        Ok(())
    }
}

/// Assemble the primitives of `mode` (4..=9) from `v` (drawing order; `edges`: GL edge flags of
/// each vertex) into ordered draws.
pub(crate) fn assemble(
    mode: u32,
    v: &[GpuVertex],
    edges: &[bool],
    s: &Settings,
) -> Result<Vec<Item>, u32> {
    let mut b = Builder {
        s,
        items: Vec::new(),
    };
    let flat = |pv: &GpuVertex| s.flat.then_some(*pv);
    match mode {
        4 => {
            for (t, e) in v.chunks_exact(3).zip(edges.chunks_exact(3)) {
                b.polygon(t, e, flat(&t[2]).as_ref())?;
            }
        }
        5 => {
            for i in 0..v.len().saturating_sub(2) {
                // Every other triangle is flipped to keep the winding of the first one.
                let t = if i % 2 == 1 {
                    [v[i + 1], v[i], v[i + 2]]
                } else {
                    [v[i], v[i + 1], v[i + 2]]
                };
                b.polygon(&t, &[true; 3], flat(&v[i + 2]).as_ref())?;
            }
        }
        6 => {
            for i in 1..v.len().saturating_sub(1) {
                b.polygon(
                    &[v[0], v[i], v[i + 1]],
                    &[true; 3],
                    flat(&v[i + 1]).as_ref(),
                )?;
            }
        }
        7 => {
            for (q, e) in v.chunks_exact(4).zip(edges.chunks_exact(4)) {
                b.polygon(q, e, flat(&q[3]).as_ref())?;
            }
        }
        8 => {
            let mut i = 0;
            while i + 3 < v.len() {
                // Quad i is v[2i], v[2i+1], v[2i+3], v[2i+2].
                let q = [v[i], v[i + 1], v[i + 3], v[i + 2]];
                b.polygon(&q, &[true; 4], flat(&v[i + 3]).as_ref())?;
                i += 2;
            }
        }
        9 => {
            if v.len() >= 3 {
                if s.modes_active() {
                    // One polygon: its outline and flags are not a fan's.
                    b.polygon(v, edges, flat(&v[0]).as_ref())?;
                } else {
                    for i in 1..v.len() - 1 {
                        b.polygon(&[v[0], v[i], v[i + 1]], &[true; 3], flat(&v[0]).as_ref())?;
                    }
                }
            }
        }
        _ => return Err(0x0500),
    }
    Ok(b.items)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn vertex(x: f32, y: f32, color: f32) -> GpuVertex {
        GpuVertex {
            position: [x, y, 0., 1.],
            color: [color, 0., 0., 255.],
            texcoord: [0., 0., 0., 1.],
            extra_texcoords: [[0., 0., 0., 1.]; 2],
        }
    }

    fn settings() -> Settings {
        Settings {
            flat: false,
            modes: [FILL, FILL],
            offset: [false; 3],
            factor: 0.,
            units: 0.,
            front_ccw: true,
            cull: false,
            cull_face: 0x0405,
            range: [0., 1.],
            style: lines::Style {
                pmv: crate::matrix::IDENTITY,
                viewport: [0, 0, 400, 240],
                line_width: 1.,
                point_size: 1.,
            },
        }
    }

    #[test]
    fn flat_shading_uses_the_provoking_vertex() {
        let mut s = settings();
        s.flat = true;
        let v = [
            vertex(0., 0., 10.),
            vertex(1., 0., 20.),
            vertex(1., 1., 30.),
            vertex(0., 1., 40.),
        ];
        // Fan: triangle (0, 1, 2) takes vertex 2, (0, 2, 3) vertex 3.
        let items = assemble(6, &v, &[true; 4], &s).unwrap();
        let Item::Fill { vertices, bias } = &items[0] else {
            panic!()
        };
        assert_eq!(*bias, 0.);
        assert_eq!(
            vertices.iter().map(|v| v.color[0]).collect::<Vec<_>>(),
            [30., 30., 30., 40., 40., 40.]
        );
        // GL_POLYGON: the first vertex; quads: the last.
        let items = assemble(9, &v, &[true; 4], &s).unwrap();
        let Item::Fill { vertices, .. } = &items[0] else {
            panic!()
        };
        assert!(vertices.iter().all(|v| v.color[0] == 10.));
        let items = assemble(7, &v, &[true; 4], &s).unwrap();
        let Item::Fill { vertices, .. } = &items[0] else {
            panic!()
        };
        assert!(vertices.iter().all(|v| v.color[0] == 40.));
    }

    #[test]
    fn line_mode_honors_edge_flags_faces_and_culling() {
        let mut s = settings();
        s.modes = [LINE, LINE];
        let v = [
            vertex(-0.5, -0.5, 1.),
            vertex(0.5, -0.5, 1.),
            vertex(0.5, 0.5, 1.),
        ];
        let edges = [true, true, false];
        let items = assemble(4, &v, &edges, &s).unwrap();
        let Item::Clip { vertices, .. } = &items[0] else {
            panic!()
        };
        assert_eq!(vertices.len(), 8, "two edges of three");
        // Back face culled on the CPU (clockwise triangle, CCW front).
        s.cull = true;
        let cw = [v[0], v[2], v[1]];
        assert!(assemble(4, &cw, &[true; 3], &s).unwrap().is_empty());
        assert_eq!(assemble(4, &v, &[true; 3], &s).unwrap().len(), 1);
        // Front fill, back line: the clockwise triangle becomes an outline, the other fills.
        s.cull = false;
        s.modes = [FILL, LINE];
        assert!(matches!(
            assemble(4, &v, &[true; 3], &s).unwrap()[0],
            Item::Fill { .. }
        ));
        assert!(matches!(
            assemble(4, &cw, &[true; 3], &s).unwrap()[0],
            Item::Clip { .. }
        ));
        // Point mode: one quad per flagged vertex.
        s.modes = [POINT, POINT];
        let Item::Clip { vertices, .. } = &assemble(4, &v, &[true, false, true], &s).unwrap()[0]
        else {
            panic!()
        };
        assert_eq!(vertices.len(), 8);
    }

    #[test]
    fn polygon_offset_splits_draws_by_bias() {
        let mut s = settings();
        s.offset = [true; 3];
        s.units = 1.;
        let v = [vertex(0., 0., 1.), vertex(1., 0., 1.), vertex(0., 1., 1.)];
        assert!(s.needed());
        let items = assemble(4, &v, &[true; 3], &s).unwrap();
        let Item::Fill { bias, .. } = &items[0] else {
            panic!()
        };
        assert_eq!(*bias, DEPTH_RESOLUTION);
        s.offset = [false; 3];
        assert!(!s.needed());
    }
}
