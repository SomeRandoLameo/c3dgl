//! glRenderMode, feedback and selection (GL 1.1 sections 5.2 and 5.3). In GL_FEEDBACK and
//! GL_SELECT render mode nothing is drawn: points, lines and polygons (after user clip planes,
//! culling and polygon mode) are clipped against the view volume and either written to the
//! feedback buffer as tokens with their vertices in window coordinates, or recorded as a hit
//! with their window depth range.
use crate::{
    polygon,
    renderer::{with_immediate, GpuVertex, Renderer},
};

pub(crate) const RENDER: u32 = 0x1c00;
pub(crate) const FEEDBACK: u32 = 0x1c01;
pub(crate) const SELECT: u32 = 0x1c02;
const MAX_NAMES: usize = 64;

pub(crate) struct Select {
    pub mode: u32,
    feedback_buffer: *mut f32,
    feedback_size: i32,
    feedback_count: i32,
    feedback_type: u32,
    feedback_set: bool,
    select_buffer: *mut u32,
    select_size: i32,
    select_count: i32,
    select_set: bool,
    hits: i32,
    hit: bool,
    hit_min: f32,
    hit_max: f32,
    names: [u32; MAX_NAMES],
    depth: usize,
    /// The next line segment starts a line (GL_LINE_RESET_TOKEN).
    line_reset: bool,
}

impl Select {
    pub fn new() -> Self {
        Self {
            mode: RENDER,
            feedback_buffer: core::ptr::null_mut(),
            feedback_size: 0,
            feedback_count: 0,
            feedback_type: 0x0600,
            feedback_set: false,
            select_buffer: core::ptr::null_mut(),
            select_size: 0,
            select_count: 0,
            select_set: false,
            hits: 0,
            hit: false,
            hit_min: 1.,
            hit_max: 0.,
            names: [0; MAX_NAMES],
            depth: 0,
            line_reset: true,
        }
    }

    pub fn active(&self) -> bool {
        self.mode != RENDER
    }

    pub fn begin_primitive(&mut self) {
        self.line_reset = true;
    }

    pub fn query(&self, pname: u32) -> Option<f64> {
        match pname {
            0x0c40 => Some(self.mode as f64),
            0x0d70 => Some(self.depth as f64),
            0x0d37 => Some(MAX_NAMES as f64),
            _ => None,
        }
    }

    unsafe fn select_value(&mut self, v: u32) {
        if self.select_count < self.select_size {
            self.select_buffer.add(self.select_count as usize).write(v);
        }
        if self.select_count <= self.select_size {
            self.select_count += 1;
        }
    }

    unsafe fn write_hit_record(&mut self) {
        self.select_value(self.depth as u32);
        self.select_value((self.hit_min as f64 * 4294967295.) as u32);
        self.select_value((self.hit_max as f64 * 4294967295.) as u32);
        for i in 0..self.depth {
            self.select_value(self.names[i]);
        }
        self.hits += 1;
        self.hit = false;
        self.hit_min = 1.;
        self.hit_max = 0.;
    }

    pub unsafe fn feedback_value(&mut self, v: f32) {
        if self.feedback_count < self.feedback_size {
            self.feedback_buffer
                .add(self.feedback_count as usize)
                .write(v);
        }
        if self.feedback_count <= self.feedback_size {
            self.feedback_count += 1;
        }
    }

    fn select_hit(&mut self, z: f32) {
        self.hit = true;
        self.hit_min = self.hit_min.min(z);
        self.hit_max = self.hit_max.max(z);
    }
}

/// A vertex as feedback sees it: clip coordinates, color, texcoords of unit 0 with the texture
/// matrix applied.
#[derive(Clone, Copy)]
struct FVertex {
    clip: [f32; 4],
    color: [f32; 4],
    tex: [f32; 4],
}

struct Ctx {
    pmv: [f32; 16],
    tex_matrix: [f32; 16],
    texgen0: bool,
    viewport: [i32; 4],
    range: [f32; 2],
    clip: crate::clip::Planes,
    cull: bool,
    cull_face: u32,
    front_ccw: bool,
    modes: [u32; 2],
    flat: bool,
    offset_unused: (),
}

fn transform(m: &[f32; 16], p: [f32; 4]) -> [f32; 4] {
    core::array::from_fn(|r| (0..4).map(|c| m[c * 4 + r] * p[c]).sum())
}

fn fvertex(c: &Ctx, v: &GpuVertex) -> FVertex {
    let tc = v.texcoord;
    let tex = if c.texgen0 {
        tc
    } else {
        let tm = &c.tex_matrix;
        core::array::from_fn(|r| {
            tm[r] * tc[0] + tm[4 + r] * tc[1] + tm[8 + r] * tc[2] + tm[12 + r] * tc[3]
        })
    };
    FVertex {
        clip: transform(&c.pmv, v.position),
        color: v.color.map(|b| b * (1. / 255.)),
        tex,
    }
}

fn lerp(a: &FVertex, b: &FVertex, t: f32) -> FVertex {
    let mix = |x: [f32; 4], y: [f32; 4]| -> [f32; 4] {
        core::array::from_fn(|i| x[i] + (y[i] - x[i]) * t)
    };
    FVertex {
        clip: mix(a.clip, b.clip),
        color: mix(a.color, b.color),
        tex: mix(a.tex, b.tex),
    }
}

/// Distance to view volume plane 0..5 (-w <= x, x <= w, y, z), >= 0 inside.
fn plane_distance(c: &[f32; 4], plane: usize) -> f32 {
    if plane & 1 != 0 {
        c[3] - c[plane >> 1]
    } else {
        c[3] + c[plane >> 1]
    }
}

fn window(c: &Ctx, clip: &[f32; 4]) -> [f32; 3] {
    let vp = c.viewport;
    let (n, f) = (c.range[0], c.range[1]);
    [
        vp[0] as f32 + 0.5 * (clip[0] / clip[3] + 1.) * vp[2] as f32,
        vp[1] as f32 + 0.5 * (clip[1] / clip[3] + 1.) * vp[3] as f32,
        n + 0.5 * (clip[2] / clip[3] + 1.) * (f - n),
    ]
}

/// One vertex in the layout of the feedback type: window x, y, z, clip w, color, texcoords.
unsafe fn write_vertex(s: &mut Select, win: [f32; 3], w: f32, color: [f32; 4], tex: [f32; 4]) {
    let ty = s.feedback_type;
    s.feedback_value(win[0]);
    s.feedback_value(win[1]);
    if ty != 0x0600 {
        s.feedback_value(win[2]);
    }
    if ty == 0x0604 {
        s.feedback_value(w);
    }
    if ty == 0x0600 || ty == 0x0601 {
        return;
    }
    for c in color {
        s.feedback_value(c);
    }
    if ty != 0x0602 {
        for t in tex {
            s.feedback_value(t);
        }
    }
}

/// A clipped primitive: token (and vertex count for polygons) with its vertices, or a hit.
unsafe fn primitive(s: &mut Select, c: &Ctx, token: u32, vs: &[FVertex]) {
    if s.mode == SELECT {
        for v in vs {
            let w = window(c, &v.clip);
            s.select_hit(w[2]);
        }
        return;
    }
    s.feedback_value(token as f32);
    if token == 0x0703 {
        s.feedback_value(vs.len() as f32);
    }
    for v in vs {
        let w = window(c, &v.clip);
        write_vertex(s, w, v.clip[3], v.color, v.tex);
    }
}

unsafe fn point(s: &mut Select, c: &Ctx, v: &GpuVertex) {
    if c.clip.point_clipped(v) {
        return;
    }
    let f = fvertex(c, v);
    if (0..6).any(|p| plane_distance(&f.clip, p) < 0.) {
        return;
    }
    // Only the origin is left, which has no window position.
    if f.clip[3] <= 0. {
        return;
    }
    primitive(s, c, 0x0701, &[f]);
}

unsafe fn line(s: &mut Select, c: &Ctx, a: &GpuVertex, b: &GpuVertex) {
    let Some((a, b)) = c.clip.segment(a, b) else {
        return;
    };
    let f = [fvertex(c, &a), fvertex(c, &b)];
    let (mut t0, mut t1) = (0f32, 1f32);
    for p in 0..6 {
        let (da, db) = (plane_distance(&f[0].clip, p), plane_distance(&f[1].clip, p));
        if da < 0. && db < 0. {
            return;
        }
        if da < 0. {
            t0 = t0.max(da / (da - db));
        } else if db < 0. {
            t1 = t1.min(da / (da - db));
        }
    }
    if t0 > t1 {
        return;
    }
    let clipped = [lerp(&f[0], &f[1], t0), lerp(&f[0], &f[1], t1)];
    if clipped[0].clip[3] <= 0. || clipped[1].clip[3] <= 0. {
        return;
    }
    let token = if s.line_reset { 0x0707 } else { 0x0702 };
    primitive(s, c, token, &clipped);
    s.line_reset = false;
}

/// Filled polygon: Sutherland-Hodgman against the view volume.
unsafe fn filled(s: &mut Select, c: &Ctx, vs: &[FVertex]) {
    let mut poly = vs.to_vec();
    for p in 0..6 {
        if poly.len() < 3 {
            break;
        }
        let n = poly.len();
        let mut out = alloc::vec::Vec::new();
        let mut da = plane_distance(&poly[0].clip, p);
        for i in 0..n {
            let (a, b) = (&poly[i], &poly[(i + 1) % n]);
            let db = plane_distance(&b.clip, p);
            if da >= 0. {
                out.push(*a);
            }
            if (da >= 0.) != (db >= 0.) {
                out.push(lerp(a, b, da / (da - db)));
            }
            da = db;
        }
        poly = out;
    }
    // Degenerate: collapsed onto the eye.
    if poly.iter().any(|v| v.clip[3] <= 0.) {
        poly.clear();
    }
    if poly.len() >= 3 {
        primitive(s, c, 0x0703, &poly);
    }
}

fn area(c: &Ctx, vs: &[GpuVertex]) -> f32 {
    let c0 = transform(&c.pmv, vs[0].position);
    let mut c1 = transform(&c.pmv, vs[1].position);
    let mut a = 0.;
    for v in &vs[2..] {
        let c2 = transform(&c.pmv, v.position);
        a += c0[0] * (c1[1] * c2[3] - c2[1] * c1[3]) - c1[0] * (c0[1] * c2[3] - c2[1] * c0[3])
            + c2[0] * (c0[1] * c1[3] - c1[1] * c0[3]);
        c1 = c2;
    }
    a
}

/// Feed the primitives of a draw (points, lines, polygons) to the selection or feedback sink.
pub(crate) unsafe fn draw(
    r: &mut Renderer,
    mode: u32,
    v: &[GpuVertex],
    backs: &[[f32; 4]],
    edges: &[bool],
) {
    let mv = r.matrices.modelview();
    let c = Ctx {
        pmv: r.matrices.combined(),
        tex_matrix: r.matrices.texture_for(0),
        texgen0: r.frame.texgen.units[0].enabled != 0,
        viewport: r.frame.viewport,
        range: r.frame.raster.range,
        clip: r.frame.clip.object_planes(&mv),
        cull: r.frame.raster.cull,
        cull_face: r.frame.raster.face,
        front_ccw: r.frame.raster.front == 0x0901,
        modes: r.frame.poly.modes,
        flat: r.frame.poly.flat,
        offset_unused: (),
    };
    let two_sided = !backs.is_empty();
    let s = &mut r.frame.select;
    s.begin_primitive();
    let shaded = |x: &GpuVertex, pv: &GpuVertex| GpuVertex {
        color: pv.color,
        ..*x
    };
    match mode {
        0 => {
            for p in v {
                point(s, &c, p);
            }
        }
        1 => {
            for pair in v.chunks_exact(2) {
                s.line_reset = true;
                if c.flat {
                    line(s, &c, &shaded(&pair[0], &pair[1]), &pair[1]);
                } else {
                    line(s, &c, &pair[0], &pair[1]);
                }
            }
        }
        2 | 3 => {
            for pair in v.windows(2) {
                if c.flat {
                    line(
                        s,
                        &c,
                        &shaded(&pair[0], &pair[1]),
                        &shaded(&pair[1], &pair[1]),
                    );
                } else {
                    line(s, &c, &pair[0], &pair[1]);
                }
            }
            if mode == 2 && v.len() >= 2 {
                let (last, first) = (&v[v.len() - 1], &v[0]);
                if c.flat {
                    line(s, &c, &shaded(last, first), &shaded(first, first));
                } else {
                    line(s, &c, last, first);
                }
            }
        }
        _ => {
            let _ = polygon::visit(mode, v.len(), edges, c.flat, true, &mut |idx, flags, pv| {
                let mut vs: alloc::vec::Vec<GpuVertex> = idx.iter().map(|i| v[*i]).collect();
                let mut bs: alloc::vec::Vec<[f32; 4]> = if two_sided {
                    idx.iter().map(|i| backs[*i]).collect()
                } else {
                    alloc::vec::Vec::new()
                };
                let mut flags = flags.to_vec();
                let flat = pv.map(|i| (v[i].color, if two_sided { backs[i] } else { [0.; 4] }));
                if c.clip.any() {
                    let (a, b, e) = c.clip.polygon(&vs, &bs, &flags);
                    if a.len() < 3 {
                        return Ok(());
                    }
                    (vs, bs, flags) = (a, b, e);
                }
                let front = (area(&c, &vs) > 0.) == c.front_ccw;
                if c.cull && (c.cull_face == 0x0408 || (c.cull_face == 0x0404) == front) {
                    return Ok(());
                }
                let back = two_sided && !front;
                let shade = |k: usize| {
                    let color = match flat {
                        Some((f, b)) => {
                            if back {
                                b
                            } else {
                                f
                            }
                        }
                        None if back => bs[k],
                        None => vs[k].color,
                    };
                    GpuVertex { color, ..vs[k] }
                };
                let n = vs.len();
                let shaded_vs: alloc::vec::Vec<GpuVertex> = (0..n).map(shade).collect();
                match c.modes[if front { 0 } else { 1 }] {
                    polygon::FILL => {
                        let f: alloc::vec::Vec<FVertex> =
                            shaded_vs.iter().map(|x| fvertex(&c, x)).collect();
                        filled(s, &c, &f);
                    }
                    mode => {
                        s.line_reset = true;
                        for i in 0..n {
                            if !flags[i] {
                                continue;
                            }
                            if mode == polygon::LINE {
                                line(s, &c, &shaded_vs[i], &shaded_vs[(i + 1) % n]);
                            } else {
                                point(s, &c, &shaded_vs[i]);
                            }
                        }
                    }
                }
                Ok(())
            });
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn glRenderMode(mode: u32) -> i32 {
    let mut result = 0;
    with_immediate(|r| {
        if r.immediate.active {
            r.matrices.fail(0x0502);
            return;
        }
        if !matches!(mode, RENDER | FEEDBACK | SELECT) {
            r.matrices.fail(0x0500);
            return;
        }
        let s = &mut r.frame.select;
        if (mode == FEEDBACK && !s.feedback_set) || (mode == SELECT && !s.select_set) {
            r.matrices.fail(0x0502);
            return;
        }
        // Leaving a mode returns its result: the number of hit records or feedback values, -1 on
        // overflow.
        if s.mode == SELECT {
            if s.hit {
                s.write_hit_record();
            }
            result = if s.select_count > s.select_size {
                -1
            } else {
                s.hits
            };
        } else if s.mode == FEEDBACK {
            result = if s.feedback_count > s.feedback_size {
                -1
            } else {
                s.feedback_count
            };
        }
        s.mode = mode;
        s.feedback_count = 0;
        s.select_count = 0;
        s.hits = 0;
        s.hit = false;
        s.hit_min = 1.;
        s.hit_max = 0.;
        s.depth = 0;
    });
    result
}

#[no_mangle]
pub unsafe extern "C" fn glFeedbackBuffer(size: i32, ty: u32, buffer: *mut f32) {
    with_immediate(|r| {
        let s = &mut r.frame.select;
        if r.immediate.active || s.mode == FEEDBACK {
            r.matrices.fail(0x0502);
            return;
        }
        if !(0x0600..=0x0604).contains(&ty) {
            r.matrices.fail(0x0500);
            return;
        }
        if size < 0 {
            r.matrices.fail(0x0501);
            return;
        }
        s.feedback_buffer = buffer;
        s.feedback_size = if buffer.is_null() { 0 } else { size };
        s.feedback_type = ty;
        s.feedback_set = true;
    });
}

#[no_mangle]
pub unsafe extern "C" fn glSelectBuffer(size: i32, buffer: *mut u32) {
    with_immediate(|r| {
        let s = &mut r.frame.select;
        if r.immediate.active || s.mode == SELECT {
            r.matrices.fail(0x0502);
            return;
        }
        if size < 0 {
            r.matrices.fail(0x0501);
            return;
        }
        s.select_buffer = buffer;
        s.select_size = if buffer.is_null() { 0 } else { size };
        s.select_set = true;
    });
}

#[no_mangle]
pub unsafe extern "C" fn glPassThrough(token: f32) {
    if crate::lists::record(crate::lists::Cmd::PassThrough(token)) {
        return;
    }
    with_immediate(|r| {
        if r.immediate.active {
            r.matrices.fail(0x0502);
            return;
        }
        let s = &mut r.frame.select;
        if s.mode != FEEDBACK {
            return;
        }
        s.feedback_value(0x0700 as f32);
        s.feedback_value(token);
    });
}

/// The name stack commands are ignored outside selection mode.
unsafe fn names(cmd: crate::lists::Cmd, f: impl FnOnce(&mut Select) -> Result<(), u32>) {
    if crate::lists::record(cmd) {
        return;
    }
    with_immediate(|r| {
        if r.immediate.active {
            r.matrices.fail(0x0502);
            return;
        }
        let s = &mut r.frame.select;
        if s.mode != SELECT {
            return;
        }
        if let Err(e) = f(s) {
            r.matrices.fail(e);
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glInitNames() {
    names(crate::lists::Cmd::InitNames, |s| {
        if s.hit {
            s.write_hit_record();
        }
        s.depth = 0;
        Ok(())
    });
}

#[no_mangle]
pub unsafe extern "C" fn glLoadName(name: u32) {
    names(crate::lists::Cmd::LoadName(name), |s| {
        if s.depth == 0 {
            return Err(0x0502);
        }
        if s.hit {
            s.write_hit_record();
        }
        s.names[s.depth - 1] = name;
        Ok(())
    });
}

#[no_mangle]
pub unsafe extern "C" fn glPushName(name: u32) {
    names(crate::lists::Cmd::PushName(name), |s| {
        if s.hit {
            s.write_hit_record();
        }
        if s.depth == MAX_NAMES {
            return Err(0x0503);
        }
        s.names[s.depth] = name;
        s.depth += 1;
        Ok(())
    });
}

#[no_mangle]
pub unsafe extern "C" fn glPopName() {
    names(crate::lists::Cmd::PopName, |s| {
        if s.hit {
            s.write_hit_record();
        }
        if s.depth == 0 {
            return Err(0x0504);
        }
        s.depth -= 1;
        Ok(())
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hit_records_and_overflow() {
        let mut buffer = [0u32; 8];
        let mut s = Select::new();
        s.mode = SELECT;
        s.select_buffer = buffer.as_mut_ptr();
        s.select_size = 8;
        s.names[0] = 7;
        s.depth = 1;
        s.select_hit(0.25);
        s.select_hit(0.5);
        unsafe { s.write_hit_record() };
        assert_eq!(s.hits, 1);
        assert_eq!(
            &buffer[..4],
            &[
                1,
                (0.25f64 * 4294967295.) as u32,
                (0.5f64 * 4294967295.) as u32,
                7
            ]
        );
        // The buffer holds 8 values: a second record of 4 fits, a third overflows.
        s.select_hit(0.1);
        unsafe { s.write_hit_record() };
        s.select_hit(0.1);
        unsafe { s.write_hit_record() };
        assert!(s.select_count > s.select_size);
    }

    #[test]
    fn plane_distances_and_window_mapping() {
        let inside = [0., 0., 0., 1.];
        assert!((0..6).all(|p| plane_distance(&inside, p) >= 0.));
        assert!(plane_distance(&[2., 0., 0., 1.], 1) < 0.);
        assert!(plane_distance(&[-2., 0., 0., 1.], 0) < 0.);
        let mut s = Select::new();
        assert!(!s.active());
        s.mode = FEEDBACK;
        assert!(s.active());
    }
}
