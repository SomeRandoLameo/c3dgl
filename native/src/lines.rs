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
}

/// Vertices a primitive mode of `count` vertices consumes (0 points, 1 lines, 2 loop, 3 strip).
pub(crate) fn consumed(mode: u32, count: usize) -> usize {
    match mode {
        1 => count / 2 * 2,
        _ => count,
    }
}

/// Correctly rounded square root on the 3DS FPU; elsewhere Newton iterations (no_std has no sqrt).
fn sqrt(x: f32) -> f32 {
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
    let mix_color = |x: [f32; 4], y: [f32; 4]| -> [f32; 4] {
        core::array::from_fn(|i| mix(x[i], y[i]) as u8 as f32)
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

fn quad(out: &mut Vec<GpuVertex>, indices: &mut Vec<u16>, q: [GpuVertex; 4]) -> Result<(), u32> {
    if out.len() + 4 > 4096 {
        return Err(0x0505);
    }
    let base = out.len() as u16;
    out.try_reserve(4).map_err(|_| 0x0505u32)?;
    indices.try_reserve(6).map_err(|_| 0x0505u32)?;
    out.extend(q);
    indices.extend([base, base + 1, base + 2, base, base + 2, base + 3]);
    Ok(())
}

pub(crate) fn point(
    s: &Style,
    v: &GpuVertex,
    z_bias: f32,
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
    let r = 0.5 * s.point_size.clamp(1., MAX_POINT_SIZE);
    let (rx, ry) = (2. * r / s.viewport[2] as f32, 2. * r / s.viewport[3] as f32);
    // Bottom left, top left, top right, bottom right (NDC y points up).
    let corner = [(-1., -1.), (-1., 1.), (1., 1.), (1., -1.)];
    quad(
        out,
        indices,
        core::array::from_fn(|i| at(v, p[0] + corner[i].0 * rx, p[1] + corner[i].1 * ry, p[2])),
    )
}

pub(crate) fn segment(
    s: &Style,
    a: &GpuVertex,
    b: &GpuVertex,
    z_bias: f32,
    out: &mut Vec<GpuVertex>,
    indices: &mut Vec<u16>,
) -> Result<(), u32> {
    let Some((a, b)) = s.clip.segment(a, b) else {
        return Ok(());
    };
    let (a, b) = (&a, &b);
    let (mut ca, mut cb) = (transform(&s.pmv, a.position), transform(&s.pmv, b.position));
    if ca[3] < CLIP_W_MIN && cb[3] < CLIP_W_MIN {
        return Ok(());
    }
    let (mut va, mut vb) = (*a, *b);
    if ca[3] < CLIP_W_MIN {
        let t = (CLIP_W_MIN - ca[3]) / (cb[3] - ca[3]);
        for i in 0..4 {
            ca[i] += (cb[i] - ca[i]) * t;
        }
        va = lerp(a, b, t);
    } else if cb[3] < CLIP_W_MIN {
        let t = (CLIP_W_MIN - cb[3]) / (ca[3] - cb[3]);
        for i in 0..4 {
            cb[i] += (ca[i] - cb[i]) * t;
        }
        vb = lerp(b, a, t);
    }
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
    let (nx, ny) = (-dy * r / half_w, dx * r / half_h);
    let (ex, ey) = (dx * r / half_w, dy * r / half_h);
    quad(
        out,
        indices,
        [
            at(&va, pa[0] - ex - nx, pa[1] - ey - ny, pa[2]),
            at(&va, pa[0] - ex + nx, pa[1] - ey + ny, pa[2]),
            at(&vb, pb[0] + ex + nx, pb[1] + ey + ny, pb[2]),
            at(&vb, pb[0] + ex - nx, pb[1] + ey - ny, pb[2]),
        ],
    )
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
    let mut line = |a: &GpuVertex, b: &GpuVertex, pv: &GpuVertex| {
        if flat {
            segment(
                s,
                &shaded(a, pv),
                &shaded(b, pv),
                0.,
                &mut out,
                &mut indices,
            )
        } else {
            segment(s, a, b, 0., &mut out, &mut indices)
        }
    };
    match mode {
        0 => {
            for p in v {
                point(s, p, 0., &mut out, &mut indices)?;
            }
            return Ok((out, indices));
        }
        1 => {
            for pair in v.chunks_exact(2) {
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
    Ok((out, indices))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn vertex(x: f32, y: f32, z: f32, w: f32) -> GpuVertex {
        GpuVertex {
            position: [x, y, z, w],
            color: [1., 2., 3., 4.],
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
        assert_eq!(out[0].color, [1., 2., 3., 4.]);
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
