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
    /// Two-sided lighting: back-facing polygons take the back colors.
    pub two_sided: bool,
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
            || self.two_sided
            || self.style.clip.any()
            || self.modes_active()
            || ((self.factor != 0. || self.units != 0.) && self.offset.iter().any(|o| *o))
    }
}

/// One draw: object-space triangles with a depth bias folded into the MVP, or expanded NDC quads.
pub(crate) enum Item {
    /// Each polygon's vertices once and its triangles as a fan of indices.
    Fill {
        vertices: Vec<GpuVertex>,
        indices: Vec<u16>,
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
    /// The polygon being assembled (reused: no allocation per polygon).
    vs: Vec<GpuVertex>,
    bs: Vec<[f32; 4]>,
    edges: Vec<bool>,
    scratch: crate::clip::Scratch,
    /// Where the source vertices start in the first fill item, once polygons index them.
    direct: Option<u16>,
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
    /// The first index of the source vertices in the first fill item (appended at the first
    /// use); None when they do not fit.
    fn direct_base(&mut self, src: &Src) -> Result<Option<u16>, u32> {
        if self.direct.is_none() {
            let Some(Item::Fill { vertices, bias, .. }) = self.items.first_mut() else {
                return Ok(None);
            };
            if *bias != 0. || vertices.len() + src.v.len() > LIMIT {
                return Ok(None);
            }
            let base = vertices.len() as u16;
            vertices.try_reserve(src.v.len()).map_err(|_| 0x0505u32)?;
            vertices.extend_from_slice(src.v);
            self.direct = Some(base);
        }
        Ok(self.direct)
    }

    /// The fill item for `n` more vertices with `bias`.
    fn fill(&mut self, bias: f32, n: usize) -> (&mut Vec<GpuVertex>, &mut Vec<u16>) {
        let new = !matches!(
            self.items.last(),
            Some(Item::Fill { vertices, bias: b, .. }) if *b == bias && vertices.len() + n <= LIMIT
        );
        if new {
            self.items.push(Item::Fill {
                vertices: Vec::new(),
                indices: Vec::new(),
                bias,
            });
        }
        match self.items.last_mut() {
            Some(Item::Fill {
                vertices, indices, ..
            }) => (vertices, indices),
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

    /// A polygon (triangle, quad, polygon) of the vertices `idx` of `src` with edge flags:
    /// filled, outlined or as vertices depending on the polygon mode of the side facing the
    /// viewer. `pv`: flat shading vertex.
    fn polygon(
        &mut self,
        src: &Src,
        idx: &[usize],
        edges: &[bool],
        pv: Option<usize>,
    ) -> Result<(), u32> {
        let two_sided = !src.backs.is_empty();
        // Filled, smooth, one-sided and not clipped: the polygon's triangles index the source
        // vertices (taken over once), as the copied path would draw them.
        let s = self.s;
        if !s.modes_active()
            && !s.flat
            && !two_sided
            && !s.offset[0]
            // Only while everything goes to the first item (the drawing order stays).
            && self.items.len() == 1
            && idx.iter().all(|&i| s.style.clip.inside(&src.v[i]))
        {
            if let Some(base) = self.direct_base(src)? {
                if let Some(Item::Fill { indices, .. }) = self.items.first_mut() {
                    let n = idx.len();
                    indices.try_reserve(3 * (n - 2)).map_err(|_| 0x0505u32)?;
                    for i in 1..n - 1 {
                        indices.extend([idx[0], idx[i], idx[i + 1]].map(|k| base + k as u16));
                    }
                    return Ok(());
                }
            }
        }
        let mut vs = core::mem::take(&mut self.vs);
        let mut bs = core::mem::take(&mut self.bs);
        let mut flags = core::mem::take(&mut self.edges);
        vs.clear();
        bs.clear();
        flags.clear();
        vs.extend(idx.iter().map(|i| src.v[*i]));
        if two_sided {
            bs.extend(idx.iter().map(|i| src.backs[*i]));
        }
        flags.extend_from_slice(edges);
        let result = self.polygon_with(src, &mut vs, &mut bs, &mut flags, pv);
        (self.vs, self.bs, self.edges) = (vs, bs, flags);
        result
    }

    fn polygon_with(
        &mut self,
        src: &Src,
        vs: &mut Vec<GpuVertex>,
        bs: &mut Vec<[f32; 4]>,
        edges: &mut Vec<bool>,
        pv: Option<usize>,
    ) -> Result<(), u32> {
        let s = self.s;
        let two_sided = !src.backs.is_empty();
        // The flat shading colors, before clipping creates new vertices.
        let flat = pv.map(|i| {
            (
                src.v[i].color,
                if two_sided {
                    src.backs[i].map(|c| c as u8)
                } else {
                    [0; 4]
                },
            )
        });
        if s.style.clip.any() {
            s.style
                .clip
                .polygon_in_place(vs, bs, edges, &mut self.scratch);
            if vs.len() < 3 {
                return Ok(());
            }
        }
        let n = vs.len();
        let active = s.modes_active();
        let front = !(active || two_sided) || (area(s, &vs) > 0.) == s.front_ccw;
        let back = two_sided && !front;
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
            polygon_offset(s, &vs) / range
        } else {
            0.
        };
        // The vertex with the color of the provoking vertex (flat shading) or of the side seen.
        let shade = |k: usize| {
            let color = match flat {
                Some((front_color, back_color)) => {
                    if back {
                        back_color
                    } else {
                        front_color
                    }
                }
                None if back => bs[k].map(|c| c as u8),
                None => vs[k].color,
            };
            GpuVertex { color, ..vs[k] }
        };
        match mode {
            FILL => {
                // The polygon's vertices share its shading: drawn as a fan of indices.
                let (out, indices) = self.fill(window, n);
                out.try_reserve(n).map_err(|_| 0x0505u32)?;
                indices.try_reserve(3 * (n - 2)).map_err(|_| 0x0505u32)?;
                let base = out.len() as u16;
                out.extend((0..n).map(shade));
                for i in 1..n - 1 {
                    indices.extend([base, base + i as u16, base + i as u16 + 1]);
                }
            }
            _ => {
                // Edge i runs from vertex i to i + 1; its flag also decides whether vertex i is
                // drawn as a point. The outline restarts the line stipple.
                s.style.counter.set(0);
                for i in 0..n {
                    if !edges[i] {
                        continue;
                    }
                    let a = shade(i);
                    let (out, indices) = self.clip();
                    if mode == LINE {
                        let b = shade((i + 1) % n);
                        lines::segment(&s.style, &a, &b, 2. * window, out, indices)?;
                    } else {
                        lines::point(&s.style, &a, 2. * window, None, out, indices)?;
                    }
                }
            }
        }
        Ok(())
    }
}

/// The vertices of a draw: drawing order, back colors (empty without two-sided lighting) and
/// GL edge flags.
struct Src<'a> {
    v: &'a [GpuVertex],
    backs: &'a [[f32; 4]],
    edges: &'a [bool],
}

/// Visit the polygons of the primitives of `mode` (4..=9) over `n` vertices in drawing order:
/// `f(vertex indices, edge flags, flat shading vertex)`. `whole`: GL_POLYGON is one polygon
/// (outlines, feedback) rather than a triangle fan. `flat`: provoking vertices apply.
pub(crate) fn visit(
    mode: u32,
    n: usize,
    edges: &[bool],
    flat: bool,
    whole: bool,
    f: &mut dyn FnMut(&[usize], &[bool], Option<usize>) -> Result<(), u32>,
) -> Result<(), u32> {
    let flat = |i: usize| flat.then_some(i);
    let flags3 = |idx: &[usize; 3]| idx.map(|i| edges[i]);
    let flags4 = |idx: &[usize; 4]| idx.map(|i| edges[i]);
    match mode {
        4 => {
            for k in (0..n / 3 * 3).step_by(3) {
                let idx = [k, k + 1, k + 2];
                f(&idx, &flags3(&idx), flat(k + 2))?;
            }
        }
        5 => {
            for i in 0..n.saturating_sub(2) {
                // Every other triangle is flipped to keep the winding of the first one.
                let idx = if i % 2 == 1 {
                    [i + 1, i, i + 2]
                } else {
                    [i, i + 1, i + 2]
                };
                f(&idx, &[true; 3], flat(i + 2))?;
            }
        }
        6 => {
            for i in 1..n.saturating_sub(1) {
                f(&[0, i, i + 1], &[true; 3], flat(i + 1))?;
            }
        }
        7 => {
            for k in (0..n / 4 * 4).step_by(4) {
                let idx = [k, k + 1, k + 2, k + 3];
                f(&idx, &flags4(&idx), flat(k + 3))?;
            }
        }
        8 => {
            let mut i = 0;
            while i + 3 < n {
                // Quad i is v[2i], v[2i+1], v[2i+3], v[2i+2].
                f(&[i, i + 1, i + 3, i + 2], &[true; 4], flat(i + 3))?;
                i += 2;
            }
        }
        9 => {
            if n >= 3 {
                if whole {
                    let idx: Vec<usize> = (0..n).collect();
                    f(&idx, edges, flat(0))?;
                } else {
                    for i in 1..n - 1 {
                        f(&[0, i, i + 1], &[true; 3], flat(0))?;
                    }
                }
            }
        }
        _ => return Err(0x0500),
    }
    Ok(())
}

/// Assemble the primitives of `mode` (4..=9) from `v` (drawing order; `backs`: back colors of
/// two-sided lighting or empty; `edges`: GL edge flags of each vertex) into ordered draws.
/// Buffers of `assemble` kept between draws (no allocation per draw).
#[derive(Default)]
pub(crate) struct Scratch {
    items: Vec<Item>,
    vs: Vec<GpuVertex>,
    bs: Vec<[f32; 4]>,
    edges: Vec<bool>,
    clip: crate::clip::Scratch,
    /// The vectors of the first fill item of the last draw.
    fill: (Vec<GpuVertex>, Vec<u16>),
}

impl Scratch {
    /// Take back the items of a drawn assembly.
    pub fn recycle(&mut self, mut items: Vec<Item>) {
        if let Some(Item::Fill {
            mut vertices,
            mut indices,
            ..
        }) = items.drain(..).next()
        {
            vertices.clear();
            indices.clear();
            self.fill = (vertices, indices);
        }
        self.items = items;
    }
}

pub(crate) fn assemble(
    mode: u32,
    v: &[GpuVertex],
    backs: &[[f32; 4]],
    edges: &[bool],
    s: &Settings,
) -> Result<Vec<Item>, u32> {
    assemble_with(mode, v, backs, edges, s, &mut Scratch::default())
}

/// `assemble` with reused buffers.
pub(crate) fn assemble_with(
    mode: u32,
    v: &[GpuVertex],
    backs: &[[f32; 4]],
    edges: &[bool],
    s: &Settings,
    scratch: &mut Scratch,
) -> Result<Vec<Item>, u32> {
    let mut b = Builder {
        s,
        items: core::mem::take(&mut scratch.items),
        vs: core::mem::take(&mut scratch.vs),
        bs: core::mem::take(&mut scratch.bs),
        edges: core::mem::take(&mut scratch.edges),
        scratch: core::mem::take(&mut scratch.clip),
        direct: None,
    };
    b.items.clear();
    let src = Src { v, backs, edges };
    // Filled output: at most three vertices per input vertex (fans, strips, clipped polygons).
    if !s.modes_active() && !s.offset[0] {
        let (mut first, mut indices) = core::mem::take(&mut scratch.fill);
        first
            .try_reserve((2 * v.len()).min(LIMIT))
            .map_err(|_| 0x0505u32)?;
        indices
            .try_reserve((3 * v.len()).min(3 * LIMIT))
            .map_err(|_| 0x0505u32)?;
        b.items.push(Item::Fill {
            vertices: first,
            indices,
            bias: 0.,
        });
    }
    // GL_POLYGON is one polygon when its outline is drawn.
    let result = visit(
        mode,
        v.len(),
        edges,
        s.flat,
        s.modes_active(),
        &mut |idx, flags, pv| b.polygon(&src, idx, flags, pv),
    );
    (scratch.vs, scratch.bs, scratch.edges, scratch.clip) = (b.vs, b.bs, b.edges, b.scratch);
    result?;
    Ok(b.items)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn vertex(x: f32, y: f32, color: u8) -> GpuVertex {
        GpuVertex {
            position: [x, y, 0., 1.],
            color: [color, 0, 0, 255],
            texcoord: [0., 0., 0., 1.],
            extra_texcoords: [[0., 0., 0., 1.]; 2],
        }
    }

    fn settings() -> Settings {
        Settings {
            flat: false,
            two_sided: false,
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
                clip: Default::default(),
                stipple: None,
                counter: Default::default(),
                last_clip: lines::no_clip_cache(),
                point: lines::PointStyle::plain(),
            },
        }
    }

    #[test]
    fn flat_shading_uses_the_provoking_vertex() {
        let mut s = settings();
        s.flat = true;
        let v = [
            vertex(0., 0., 10),
            vertex(1., 0., 20),
            vertex(1., 1., 30),
            vertex(0., 1., 40),
        ];
        // Fan: triangle (0, 1, 2) takes vertex 2, (0, 2, 3) vertex 3.
        let items = assemble(6, &v, &[], &[true; 4], &s).unwrap();
        let Item::Fill {
            vertices,
            indices,
            bias,
        } = &items[0]
        else {
            panic!()
        };
        assert_eq!(*bias, 0.);
        assert_eq!(
            indices
                .iter()
                .map(|&i| vertices[i as usize].color[0])
                .collect::<Vec<_>>(),
            [30, 30, 30, 40, 40, 40]
        );
        // GL_POLYGON: the first vertex; quads: the last.
        let items = assemble(9, &v, &[], &[true; 4], &s).unwrap();
        let Item::Fill { vertices, .. } = &items[0] else {
            panic!()
        };
        assert!(vertices.iter().all(|v| v.color[0] == 10));
        let items = assemble(7, &v, &[], &[true; 4], &s).unwrap();
        let Item::Fill { vertices, .. } = &items[0] else {
            panic!()
        };
        assert!(vertices.iter().all(|v| v.color[0] == 40));
    }

    #[test]
    fn line_mode_honors_edge_flags_faces_and_culling() {
        let mut s = settings();
        s.modes = [LINE, LINE];
        let v = [
            vertex(-0.5, -0.5, 1),
            vertex(0.5, -0.5, 1),
            vertex(0.5, 0.5, 1),
        ];
        let edges = [true, true, false];
        let items = assemble(4, &v, &[], &edges, &s).unwrap();
        let Item::Clip { vertices, .. } = &items[0] else {
            panic!()
        };
        assert_eq!(vertices.len(), 8, "two edges of three");
        // Back face culled on the CPU (clockwise triangle, CCW front).
        s.cull = true;
        let cw = [v[0], v[2], v[1]];
        assert!(assemble(4, &cw, &[], &[true; 3], &s).unwrap().is_empty());
        assert_eq!(assemble(4, &v, &[], &[true; 3], &s).unwrap().len(), 1);
        // Front fill, back line: the clockwise triangle becomes an outline, the other fills.
        s.cull = false;
        s.modes = [FILL, LINE];
        assert!(matches!(
            assemble(4, &v, &[], &[true; 3], &s).unwrap()[0],
            Item::Fill { .. }
        ));
        assert!(matches!(
            assemble(4, &cw, &[], &[true; 3], &s).unwrap()[0],
            Item::Clip { .. }
        ));
        // Point mode: one quad per flagged vertex.
        s.modes = [POINT, POINT];
        let Item::Clip { vertices, .. } =
            &assemble(4, &v, &[], &[true, false, true], &s).unwrap()[0]
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
        let v = [vertex(0., 0., 1), vertex(1., 0., 1), vertex(0., 1., 1)];
        assert!(s.needed());
        let items = assemble(4, &v, &[], &[true; 3], &s).unwrap();
        let Item::Fill { bias, .. } = &items[0] else {
            panic!()
        };
        assert_eq!(*bias, DEPTH_RESOLUTION);
        s.offset = [false; 3];
        assert!(!s.needed());
    }
}
