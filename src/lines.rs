//! CPU expansion of points and lines to screen-aligned NDC triangles, drawn with an identity
//! projection and without culling (the C renderer's "clip space" draws).
use crate::renderer::GpuVertex;
use alloc::vec::Vec;

/// Lines and points are clipped against w > CLIP_W_MIN before the perspective divide.
const CLIP_W_MIN: f32 = 1e-5;
pub(crate) const MAX_POINT_SIZE: f32 = 256.;

pub(crate) struct Style {
    /// Object to clip space, GL column-major.
    pub pmv: [f32; 16],
    pub viewport: [i32; 4],
    pub line_width: f32,
    pub point_size: f32,
    /// User clip planes in object space.
    pub clip: crate::clip::Planes,
    /// Line stipple (factor, pattern) when enabled.
    pub stipple: Option<(u32, u16)>,
    /// Fragments of the current strip so far (the stipple position carried between segments).
    pub counter: core::cell::Cell<u32>,
    /// The last position transformed to clip space and the result (strips share endpoints).
    pub last_clip: core::cell::Cell<Option<([f32; 4], [f32; 4])>>,
    /// Point parameters: size limits, distance attenuation (with the modelview matrix for the eye
    /// distance), the texture units whose coordinates a point sprite replaces, and the per-vertex
    /// sizes of a point size array (empty without one).
    pub point: PointStyle,
}

#[derive(Clone, Default)]
pub(crate) struct PointStyle {
    pub min: f32,
    pub max: f32,
    pub attenuation: [f32; 3],
    pub modelview: [f32; 16],
    pub sprite_units: u8,
    pub sizes: Vec<f32>,
}

impl PointStyle {
    pub fn plain() -> Self {
        Self {
            min: 0.,
            max: MAX_POINT_SIZE,
            attenuation: [1., 0., 0.],
            modelview: crate::matrix::IDENTITY,
            sprite_units: 0,
            sizes: Vec::new(),
        }
    }
}

extern "C" {
    fn ceilf(x: f32) -> f32;
}

/// Width of the window in which stippled lines emit quads.
const SCREEN_WIDTH: f32 = 400.;

/// Vertices a primitive mode of `count` vertices consumes (0 points, 1 lines, 2 loop, 3 strip).
pub(crate) fn consumed(mode: u32, count: usize) -> usize {
    match mode {
        1 => count / 2 * 2,
        _ => count,
    }
}

/// Correctly rounded square root on the 3DS FPU; elsewhere Newton iterations (no_std has no sqrt).
pub(crate) fn sqrt(x: f32) -> f32 {
    #[cfg(target_arch = "arm")]
    {
        let r: f32;
        unsafe { core::arch::asm!("vsqrt.f32 {0}, {1}", out(sreg) r, in(sreg) x) };
        r
    }
    #[cfg(not(target_arch = "arm"))]
    {
        if x <= 0. || !x.is_finite() {
            return if x == f32::INFINITY { x } else { 0. };
        }
        let mut y = f32::from_bits((x.to_bits() >> 1) + 0x1fbd_1df5);
        for _ in 0..4 {
            y = 0.5 * (y + x / y);
        }
        y
    }
}

fn transform(m: &[f32; 16], p: [f32; 4]) -> [f32; 4] {
    core::array::from_fn(|r| (0..4).map(|c| m[c * 4 + r] * p[c]).sum())
}

pub(crate) fn lerp(a: &GpuVertex, b: &GpuVertex, t: f32) -> GpuVertex {
    let mix = |x: f32, y: f32| x + (y - x) * t;
    // Colors are bytes: truncated like the C renderer.
    let mix_color = |x: [u8; 4], y: [u8; 4]| -> [u8; 4] {
        core::array::from_fn(|i| mix(x[i] as f32, y[i] as f32) as u8)
    };
    let mix4 = |x: [f32; 4], y: [f32; 4]| core::array::from_fn(|i| mix(x[i], y[i]));
    GpuVertex {
        position: mix4(a.position, b.position),
        color: mix_color(a.color, b.color),
        texcoord: mix4(a.texcoord, b.texcoord),
        extra_texcoords: core::array::from_fn(|i| mix4(a.extra_texcoords[i], b.extra_texcoords[i])),
    }
}

fn at(v: &GpuVertex, x: f32, y: f32, z: f32) -> GpuVertex {
    GpuVertex {
        position: [x, y, z, 1.],
        ..*v
    }
}

#[inline(always)]
fn quad(out: &mut Vec<GpuVertex>, indices: &mut Vec<u16>, q: [GpuVertex; 4]) -> Result<(), u32> {
    quad_with(out, indices, |i| q[i])
}

/// A quad of the vertices `corner(0..4)`, built straight in the output (no copies of them).
#[inline(always)]
fn quad_with(
    out: &mut Vec<GpuVertex>,
    indices: &mut Vec<u16>,
    mut corner: impl FnMut(usize) -> GpuVertex,
) -> Result<(), u32> {
    if out.len() + 4 > 4096 {
        return Err(0x0505);
    }
    let base = out.len() as u16;
    out.try_reserve(4).map_err(|_| 0x0505u32)?;
    indices.try_reserve(6).map_err(|_| 0x0505u32)?;
    unsafe {
        let dst = out.as_mut_ptr().add(out.len());
        for i in 0..4 {
            dst.add(i).write(corner(i));
        }
        out.set_len(out.len() + 4);
    }
    indices.extend([base, base + 1, base + 2, base, base + 2, base + 3]);
    Ok(())
}

/// The size of a point: the vertex's (point size array) or glPointSize, scaled by the distance
/// attenuation 1 / sqrt(a + b d + c d^2) of the eye distance d, clamped to GL_POINT_SIZE_MIN/MAX.
/// The fade threshold only applies with multisampling, which PICA does not have.
fn point_size(s: &Style, v: &GpuVertex, own: Option<f32>) -> f32 {
    let mut size = own.unwrap_or(s.point_size);
    let att = s.point.attenuation;
    if att[0] != 1. || att[1] != 0. || att[2] != 0. {
        let m = &s.point.modelview;
        let p = v.position;
        let e: [f32; 3] =
            core::array::from_fn(|i| m[i] * p[0] + m[4 + i] * p[1] + m[8 + i] * p[2] + m[12 + i]);
        let d = sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
        let k = att[0] + att[1] * d + att[2] * d * d;
        size = if k > 0. {
            size / sqrt(k)
        } else {
            MAX_POINT_SIZE
        };
    }
    size = size.max(s.point.min).min(s.point.max);
    size.max(1.).min(MAX_POINT_SIZE)
}

pub(crate) fn point(
    s: &Style,
    v: &GpuVertex,
    z_bias: f32,
    own_size: Option<f32>,
    out: &mut Vec<GpuVertex>,
    indices: &mut Vec<u16>,
) -> Result<(), u32> {
    if s.clip.point_clipped(v) {
        return Ok(());
    }
    let c = transform(&s.pmv, v.position);
    if c[3] < CLIP_W_MIN {
        return Ok(());
    }
    let p = [c[0] / c[3], c[1] / c[3], c[2] / c[3] + z_bias];
    let r = 0.5 * point_size(s, v, own_size);
    let (rx, ry) = (2. * r / s.viewport[2] as f32, 2. * r / s.viewport[3] as f32);
    // Bottom left, top left, top right, bottom right (NDC y points up).
    let corner = [(-1., -1.), (-1., 1.), (1., 1.), (1., -1.)];
    quad_with(out, indices, |i| {
        {
            let mut q = at(v, p[0] + corner[i].0 * rx, p[1] + corner[i].1 * ry, p[2]);
            // Point sprites: the coordinates of the sprite units run from (0, 0) at the top left
            // to (1, 1) at the bottom right.
            let tc = [0.5 + 0.5 * corner[i].0, 0.5 - 0.5 * corner[i].1, 0., 1.];
            if s.point.sprite_units & 1 != 0 {
                q.texcoord = tc;
            }
            for unit in 1..3 {
                if s.point.sprite_units >> unit & 1 != 0 {
                    q.extra_texcoords[unit - 1] = tc;
                }
            }
            q
        }
    })
}

impl Style {
    /// `pmv` times a position, reusing the last result (identical for an equal position).
    fn clip_coords(&self, p: &[f32; 4]) -> [f32; 4] {
        // Bitwise: -0 and 0 may transform differently.
        if let Some((last, clip)) = self.last_clip.get() {
            if last.map(f32::to_bits) == p.map(f32::to_bits) {
                return clip;
            }
        }
        let clip = transform(&self.pmv, *p);
        self.last_clip.set(Some((*p, clip)));
        clip
    }
}

/// An empty `Style::last_clip`.
pub(crate) fn no_clip_cache() -> core::cell::Cell<Option<([f32; 4], [f32; 4])>> {
    core::cell::Cell::new(None)
}

pub(crate) fn segment(
    s: &Style,
    a: &GpuVertex,
    b: &GpuVertex,
    z_bias: f32,
    out: &mut Vec<GpuVertex>,
    indices: &mut Vec<u16>,
) -> Result<(), u32> {
    // Copies only when user clip planes or the w clip change an endpoint.
    let clipped;
    let (a, b) = if s.clip.any() {
        let Some(c) = s.clip.segment(a, b) else {
            return Ok(());
        };
        clipped = c;
        (&clipped.0, &clipped.1)
    } else {
        (a, b)
    };
    let (mut ca, mut cb) = (s.clip_coords(&a.position), s.clip_coords(&b.position));
    if ca[3] < CLIP_W_MIN && cb[3] < CLIP_W_MIN {
        return Ok(());
    }
    let w_clipped;
    let (va, vb) = if ca[3] < CLIP_W_MIN {
        let t = (CLIP_W_MIN - ca[3]) / (cb[3] - ca[3]);
        for i in 0..4 {
            ca[i] += (cb[i] - ca[i]) * t;
        }
        w_clipped = lerp(a, b, t);
        (&w_clipped, b)
    } else if cb[3] < CLIP_W_MIN {
        let t = (CLIP_W_MIN - cb[3]) / (ca[3] - cb[3]);
        for i in 0..4 {
            cb[i] += (ca[i] - cb[i]) * t;
        }
        w_clipped = lerp(b, a, t);
        (a, &w_clipped)
    } else {
        (a, b)
    };
    let pa = [ca[0] / ca[3], ca[1] / ca[3], ca[2] / ca[3] + z_bias];
    let pb = [cb[0] / cb[3], cb[1] / cb[3], cb[2] / cb[3] + z_bias];

    // Direction in pixels, then half the line width back to NDC (perpendicular, and along the
    // line for square caps).
    let (half_w, half_h) = (0.5 * s.viewport[2] as f32, 0.5 * s.viewport[3] as f32);
    let (mut dx, mut dy) = ((pb[0] - pa[0]) * half_w, (pb[1] - pa[1]) * half_h);
    let len = sqrt(dx * dx + dy * dy);
    if len < 1e-6 {
        dx = 1.;
        dy = 0.;
    } else {
        dx /= len;
        dy /= len;
    }
    let r = 0.5 * s.line_width;
    if let (Some((factor, pattern)), true) = (s.stipple, len >= 1e-6) {
        // The caps reach r further along the line, r * |major component of the direction| along
        // the major axis.
        let cap = r * dx.abs().max(dy.abs());
        return stippled(
            s,
            (factor, pattern),
            (va, vb),
            (pa, pb),
            (-dy * r / half_w, dx * r / half_h),
            cap,
            out,
            indices,
        );
    }
    let (nx, ny) = (-dy * r / half_w, dx * r / half_h);
    let (ex, ey) = (dx * r / half_w, dy * r / half_h);
    quad(
        out,
        indices,
        [
            at(va, pa[0] - ex - nx, pa[1] - ey - ny, pa[2]),
            at(va, pa[0] - ex + nx, pa[1] - ey + ny, pa[2]),
            at(vb, pb[0] + ex + nx, pb[1] + ey + ny, pb[2]),
            at(vb, pb[0] + ex - nx, pb[1] + ey - ny, pb[2]),
        ],
    )
}

/// Line stipple (GL 1.1 section 3.4.2): fragment s of a strip (counted along the major axis,
/// carried over from segment to segment) is drawn when bit (s / factor) % 16 of the pattern is
/// set. Fragment k of the segment is the pixel column (row) k whose center lies in [start, end)
/// along the major axis; each run of drawn fragments becomes a quad that ends on the pixel
/// edges between them, clipped to the line with its square caps (`cap_u` along the major axis).
#[allow(clippy::too_many_arguments)]
fn stippled(
    s: &Style,
    (factor, pattern): (u32, u16),
    (a, b): (&GpuVertex, &GpuVertex),
    (pa, pb): ([f32; 3], [f32; 3]),
    (nx, ny): (f32, f32),
    cap_u: f32,
    out: &mut Vec<GpuVertex>,
    indices: &mut Vec<u16>,
) -> Result<(), u32> {
    let vp = s.viewport;
    let (half_w, half_h) = (0.5 * vp[2] as f32, 0.5 * vp[3] as f32);
    let wa = [
        vp[0] as f32 + (pa[0] + 1.) * half_w,
        vp[1] as f32 + (pa[1] + 1.) * half_h,
    ];
    let wb = [
        vp[0] as f32 + (pb[0] + 1.) * half_w,
        vp[1] as f32 + (pb[1] + 1.) * half_h,
    ];
    let axis = if (wb[0] - wa[0]).abs() >= (wb[1] - wa[1]).abs() {
        0
    } else {
        1
    };

    // u: window coordinate along the major axis, mirrored to increase from a to b.
    let sign = if wb[axis] >= wa[axis] { 1. } else { -1. };
    let (ua, ub) = (sign * wa[axis], sign * wb[axis]);
    // First fragment, one past the last; endpoints within 1/256 pixel of a center (rounding of
    // vertices given at pixel centers) count as on it.
    let first = unsafe { ceilf(ua - 0.5 - 1. / 256.) };
    let end = unsafe { ceilf(ub - 0.5 - 1. / 256.) };
    if !(end > first) {
        return Ok(());
    }

    let period = 16 * factor;
    let advance = |from: f32, to: f32| ((to - from) as u64 % period as u64) as u32;
    let mut at_start = s.counter.get();
    s.counter.set((at_start + advance(first, end)) % period);

    // Quads only for the fragments that can be on the screen (window 0..400 along either axis,
    // plus the line width).
    let margin = unsafe { ceilf(2. * cap_u) } + 1.;
    let screen0 = (if sign > 0. { 0. } else { -SCREEN_WIDTH }) - margin;
    let k0 = first.max(screen0);
    let k1 = end.min(screen0 + SCREEN_WIDTH + 2. * margin);
    if !(k1 > k0) {
        return Ok(());
    }
    at_start = (at_start + advance(first, k0)) % period;

    let n = (k1 - k0) as u32;
    let mut runs = alloc::vec::Vec::new();
    stipple_runs(pattern, factor, at_start, n, |k, run| runs.push((k, run)));
    for (k, run) in runs {
        {
            let u0 = (k0 + k as f32).max(ua - cap_u);
            let u1 = (k0 + run as f32).min(ub + cap_u);
            let (t0, t1) = ((u0 - ua) / (ub - ua), (u1 - ua) / (ub - ua));
            // The attributes interpolate within the line, the position at the run's own ends
            // (which reach into the square caps).
            let (c0, c1) = (t0.clamp(0., 1.), t1.clamp(0., 1.));
            let (va, vb) = (lerp(a, b, c0), lerp(a, b, c1));
            let p0: [f32; 3] = core::array::from_fn(|i| pa[i] + (pb[i] - pa[i]) * t0);
            let p1: [f32; 3] = core::array::from_fn(|i| pa[i] + (pb[i] - pa[i]) * t1);
            quad(
                out,
                indices,
                [
                    at(&va, p0[0] - nx, p0[1] - ny, p0[2]),
                    at(&va, p0[0] + nx, p0[1] + ny, p0[2]),
                    at(&vb, p1[0] + nx, p1[1] + ny, p1[2]),
                    at(&vb, p1[0] - nx, p1[1] - ny, p1[2]),
                ],
            )?;
        }
    }
    Ok(())
}

/// The runs [k, run) of drawn fragments among `n`: fragment k is drawn when pattern bit
/// ((at_start + k) % (16 factor)) / factor is set. Walked bit by bit: no division per fragment
/// (the ARM11 divides in software).
fn stipple_runs(pattern: u16, factor: u32, at_start: u32, n: u32, mut f: impl FnMut(u32, u32)) {
    let mut bit = at_start / factor % 16;
    let mut sub = at_start % factor;
    let mut k = 0;
    while k < n {
        let lit = (pattern >> bit) & 1 != 0;
        let mut run = k;
        loop {
            run += factor - sub;
            sub = 0;
            bit = (bit + 1) & 15;
            if run >= n {
                run = n;
                break;
            }
            if ((pattern >> bit) & 1 != 0) != lit {
                break;
            }
        }
        if lit {
            f(k, run);
        }
        k = run;
    }
}

/// The color of `pv` on `v` (flat shading).
pub(crate) fn shaded(v: &GpuVertex, pv: &GpuVertex) -> GpuVertex {
    GpuVertex {
        color: pv.color,
        ..*v
    }
}

/// Expand `v` (consumed vertices of a point or line mode) into NDC vertices and indices.
/// `flat`: segments take the color of their last vertex (the first one closing a loop).
pub(crate) fn expand(
    mode: u32,
    v: &[GpuVertex],
    s: &Style,
    flat: bool,
) -> Result<(Vec<GpuVertex>, Vec<u16>), u32> {
    let (mut out, mut indices) = (Vec::new(), Vec::new());
    expand_into(mode, v, s, flat, &mut out, &mut indices)?;
    Ok((out, indices))
}

/// `expand` into reused buffers (cleared first).
pub(crate) fn expand_into(
    mode: u32,
    v: &[GpuVertex],
    s: &Style,
    flat: bool,
    out: &mut Vec<GpuVertex>,
    indices: &mut Vec<u16>,
) -> Result<(), u32> {
    out.clear();
    indices.clear();
    // One quad per point or segment (stippled lines may need more).
    let quads = v.len() + 1;
    out.try_reserve((4 * quads).min(4096))
        .map_err(|_| 0x0505u32)?;
    indices
        .try_reserve((6 * quads).min(6144))
        .map_err(|_| 0x0505u32)?;
    // A primitive starts a strip.
    s.counter.set(0);
    let mut line = |a: &GpuVertex, b: &GpuVertex, pv: &GpuVertex| {
        if flat {
            segment(s, &shaded(a, pv), &shaded(b, pv), 0., out, indices)
        } else {
            segment(s, a, b, 0., out, indices)
        }
    };
    match mode {
        0 => {
            for (i, p) in v.iter().enumerate() {
                point(s, p, 0., s.point.sizes.get(i).copied(), out, indices)?;
            }
            return Ok(());
        }
        1 => {
            for pair in v.chunks_exact(2) {
                // Every line of GL_LINES restarts the pattern.
                s.counter.set(0);
                line(&pair[0], &pair[1], &pair[1])?;
            }
        }
        2 | 3 => {
            for pair in v.windows(2) {
                line(&pair[0], &pair[1], &pair[1])?;
            }
            // A loop closes with the segment from the last vertex to the first.
            if mode == 2 && v.len() >= 2 {
                line(&v[v.len() - 1], &v[0], &v[0])?;
            }
        }
        _ => return Err(0x0500),
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn vertex(x: f32, y: f32, z: f32, w: f32) -> GpuVertex {
        GpuVertex {
            position: [x, y, z, w],
            color: [1, 2, 3, 4],
            texcoord: [0., 0., 0., 1.],
            extra_texcoords: [[0., 0., 0., 1.]; 2],
        }
    }

    fn style(width: f32, size: f32) -> Style {
        Style {
            pmv: crate::matrix::IDENTITY,
            viewport: [0, 0, 400, 240],
            line_width: width,
            point_size: size,
            clip: Default::default(),
            stipple: None,
            counter: Default::default(),
            last_clip: no_clip_cache(),
            point: PointStyle::plain(),
        }
    }

    #[test]
    fn horizontal_line_widens_perpendicular_and_with_square_caps() {
        let v = [vertex(-0.5, 0., 0., 1.), vertex(0.5, 0., 0., 1.)];
        let (out, indices) = expand(1, &v, &style(4., 1.), false).unwrap();
        assert_eq!(indices, [0, 1, 2, 0, 2, 3]);
        // Half width 2 px: 2/200 in x, 2/120 in y.
        let (ex, ny) = (2. / 200., 2. / 120.);
        assert!((out[0].position[0] - (-0.5 - ex)).abs() < 1e-6);
        assert!((out[0].position[1] - (0. - ny)).abs() < 1e-6);
        assert!((out[2].position[0] - (0.5 + ex)).abs() < 1e-6);
        assert!((out[2].position[1] - (0. + ny)).abs() < 1e-6);
        assert_eq!(out[0].color, [1, 2, 3, 4]);
    }

    #[test]
    fn stipple_runs_match_the_fragment_formula() {
        for pattern in [0xffffu16, 0x0f0f, 0xaaaa, 0x8001, 0x0000, 0x1234, 0xf00f] {
            for factor in [1u32, 2, 3, 7, 256] {
                for at_start in [0u32, 1, 5, 16 * factor - 1, 7 * factor + 2] {
                    let at_start = at_start % (16 * factor);
                    for n in [0u32, 1, 2, 17, 100, 333] {
                        let mut want = alloc::vec::Vec::new();
                        let on = |k: u32| {
                            (pattern >> (((at_start + k) % (16 * factor)) / factor)) & 1 != 0
                        };
                        let mut k = 0;
                        while k < n {
                            let lit = on(k);
                            let mut run = k + 1;
                            while run < n && on(run) == lit {
                                run += 1;
                            }
                            if lit {
                                want.push((k, run));
                            }
                            k = run;
                        }
                        let mut got = alloc::vec::Vec::new();
                        stipple_runs(pattern, factor, at_start, n, |a, b| got.push((a, b)));
                        assert_eq!(got, want, "{pattern:#x} {factor} {at_start} {n}");
                    }
                }
            }
        }
    }

    #[test]
    fn software_sqrt_is_accurate() {
        for x in [0.25f32, 1., 2., 3., 100., 12345.678, 1e-6] {
            assert!((sqrt(x) - x.sqrt()).abs() <= x.sqrt() * 2e-7, "{x}");
        }
        assert_eq!(sqrt(0.), 0.);
    }

    #[test]
    fn loop_strip_point_counts_and_clipping() {
        let v = [
            vertex(-0.5, -0.5, 0., 1.),
            vertex(0.5, -0.5, 0., 1.),
            vertex(0., 0.5, 0., 1.),
        ];
        assert_eq!(expand(2, &v, &style(1., 1.), false).unwrap().0.len(), 12);
        assert_eq!(expand(3, &v, &style(1., 1.), false).unwrap().0.len(), 8);
        assert_eq!(
            expand(1, &v[..2], &style(1., 1.), false).unwrap().0.len(),
            4
        );
        assert_eq!(consumed(1, 5), 4);
        let (points, idx) = expand(0, &v, &style(1., 8.), false).unwrap();
        assert_eq!((points.len(), idx.len()), (12, 18));
        // A size-8 point is 8 px wide: 8/400*... half 4 px = 4/200 in NDC.
        assert!((points[2].position[0] - points[0].position[0] - 8. / 200.).abs() < 1e-6);
        // Behind the viewer: dropped; one end behind: clipped at w = CLIP_W_MIN.
        let behind = [vertex(0., 0., 0., -1.)];
        assert!(expand(0, &behind, &style(1., 1.), false)
            .unwrap()
            .0
            .is_empty());
        let half = [vertex(0., 0., 0., 1.), vertex(0., 0., 0., -1.)];
        let (out, _) = expand(1, &half, &style(1., 1.), false).unwrap();
        assert_eq!(out.len(), 4);
        assert!(out.iter().all(|v| v.position.iter().all(|c| c.is_finite())));
        assert!(expand(4, &v, &style(1., 1.), false).is_err());
    }
}
