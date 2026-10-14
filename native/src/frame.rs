//! GL frame state. Top-screen landscape viewport and whole-buffer clears.
use crate::{
    matrix::Matrices,
    renderer::{clear_gl, with_frame, with_renderer},
};

pub const COLOR: u32 = 0x4000;
pub const DEPTH: u32 = 0x0100;

/// Polygon state handled on the CPU (see polygon.rs).
#[derive(Clone, Copy)]
pub struct PolyState {
    pub flat: bool,
    pub modes: [u32; 2],
    pub offset: [bool; 3],
    pub factor: f32,
    pub units: f32,
}

pub struct FrameState {
    pub select: crate::feedback::Select,
    pub lists: crate::lists::Lists,
    pub clip: crate::clip::ClipState,
    pub fog: crate::fog::Fog,
    pub texgen: crate::texgen::TexGen,
    pub light: crate::lighting::Lighting,
    pub eval: crate::evaluator::Evaluators,
    pub poly: PolyState,
    pub raster: crate::raster::Raster,
    pub textures: crate::texture::Textures,
    pub viewport: [i32; 4],
    pub viewport_dirty: bool,
    pub drawable: bool,
    pub clear_color: u32,
    pub clear_depth: f32,
    pub clear_stencil: u8,
    /// Stencil was enabled once: depth-only memory fills would destroy its contents.
    pub stencil_used: bool,
    pub line_width: f32,
    pub point_size: f32,
}

impl FrameState {
    pub fn new() -> Self {
        Self {
            select: crate::feedback::Select::new(),
            lists: crate::lists::Lists::new(),
            clip: crate::clip::ClipState::new(),
            fog: crate::fog::Fog::new(),
            texgen: crate::texgen::TexGen::new(),
            light: crate::lighting::Lighting::new(),
            eval: crate::evaluator::Evaluators::new(),
            poly: PolyState {
                flat: false,
                modes: [crate::polygon::FILL; 2],
                offset: [false; 3],
                factor: 0.,
                units: 0.,
            },
            raster: crate::raster::Raster::new(),
            textures: crate::texture::Textures::new(),
            viewport: [0, 0, 400, 240],
            viewport_dirty: false,
            drawable: true,
            clear_color: 0,
            clear_depth: 1.,
            clear_stencil: 0,
            stencil_used: false,
            line_width: 1.,
            point_size: 1.,
        }
    }

    pub fn visible(&self) -> bool {
        let [x, y, w, h] = self.viewport;
        w > 0 && h > 0 && x < 400 && y < 240 && x as i64 + w as i64 > 0 && y as i64 + h as i64 > 0
    }
}

pub(crate) fn offset_index(cap: u32) -> usize {
    match cap {
        0x8037 => 0,
        0x2a02 => 1,
        _ => 2,
    }
}

#[no_mangle]
pub unsafe extern "C" fn glShadeModel(mode: u32) {
    if crate::lists::record(crate::lists::Cmd::ShadeModel(mode)) {
        return;
    }
    with_frame(|s, m| match mode {
        0x1d00 | 0x1d01 => s.poly.flat = mode == 0x1d00,
        _ => m.fail(0x0500),
    });
}

#[no_mangle]
pub unsafe extern "C" fn glPolygonMode(face: u32, mode: u32) {
    if crate::lists::record(crate::lists::Cmd::PolygonMode(face, mode)) {
        return;
    }
    with_frame(|s, m| {
        if !matches!(face, 0x0404 | 0x0405 | 0x0408) || !matches!(mode, 0x1b00..=0x1b02) {
            m.fail(0x0500);
            return;
        }
        if face != 0x0405 {
            s.poly.modes[0] = mode;
        }
        if face != 0x0404 {
            s.poly.modes[1] = mode;
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glPolygonOffset(factor: f32, units: f32) {
    if crate::lists::record(crate::lists::Cmd::PolygonOffset(factor, units)) {
        return;
    }
    with_frame(|s, _| {
        s.poly.factor = factor;
        s.poly.units = units;
    });
}

/// The current vertex attributes: color, normal, texture coordinates and edge flag.
fn current_query(i: &crate::immediate::Immediate, pname: u32) -> Option<([f64; 16], usize)> {
    let mut v = [0.; 16];
    let n = match pname {
        0x0b00 => {
            for (o, c) in v.iter_mut().zip(i.color) {
                *o = c as f64;
            }
            4
        }
        0x0b02 => {
            for (o, c) in v.iter_mut().zip(i.normal) {
                *o = c as f64;
            }
            3
        }
        0x0b03 => {
            for (o, c) in v.iter_mut().zip(i.texcoord) {
                *o = c as f64;
            }
            4
        }
        0x0b43 => {
            v[0] = i.edge as u8 as f64;
            1
        }
        _ => return None,
    };
    Some((v, n))
}

fn byte(x: f32) -> u32 {
    if x <= 0. {
        0
    } else if x >= 1. {
        255
    } else {
        (x * 255. + 0.5) as u32
    }
}

#[no_mangle]
pub unsafe extern "C" fn glClearColor(r: f32, g: f32, b: f32, a: f32) {
    if crate::lists::record(crate::lists::Cmd::ClearColor([r, g, b, a])) {
        return;
    }
    with_frame(|s, _| s.clear_color = (byte(r) << 24) | (byte(g) << 16) | (byte(b) << 8) | byte(a));
}

#[no_mangle]
pub unsafe extern "C" fn glClearDepth(depth: f64) {
    if crate::lists::record(crate::lists::Cmd::ClearDepth(depth)) {
        return;
    }
    with_frame(|s, _| s.clear_depth = depth.clamp(0., 1.) as f32);
}

#[no_mangle]
pub unsafe extern "C" fn glClearStencil(s: i32) {
    if crate::lists::record(crate::lists::Cmd::ClearStencil(s)) {
        return;
    }
    with_frame(|f, _| f.clear_stencil = s as u8);
}

#[no_mangle]
pub unsafe extern "C" fn glClearDepthf(depth: f32) {
    glClearDepth(depth as f64);
}

#[no_mangle]
pub unsafe extern "C" fn glViewport(x: i32, y: i32, w: i32, h: i32) {
    if crate::lists::record(crate::lists::Cmd::Viewport([x, y, w, h])) {
        return;
    }
    with_frame(|s, m| {
        if w < 0 || h < 0 {
            m.fail(0x0501);
            return;
        }

        let viewport = [x, y, w.min(400), h.min(240)];
        if s.viewport != viewport {
            s.viewport = viewport;
            s.drawable = s.visible();
            s.viewport_dirty = true;
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glLineWidth(width: f32) {
    if crate::lists::record(crate::lists::Cmd::LineWidth(width)) {
        return;
    }
    with_frame(|s, m| {
        if !(width > 0.) {
            m.fail(0x0501);
            return;
        }
        s.line_width = width;
    });
}

#[no_mangle]
pub unsafe extern "C" fn glPointSize(size: f32) {
    if crate::lists::record(crate::lists::Cmd::PointSize(size)) {
        return;
    }
    with_frame(|s, m| {
        if !(size > 0.) {
            m.fail(0x0501);
            return;
        }
        s.point_size = size;
    });
}

#[no_mangle]
pub unsafe extern "C" fn glClear(mask: u32) {
    if crate::lists::record(crate::lists::Cmd::Clear(mask)) {
        return;
    }
    clear_gl(mask);
}

fn query(
    s: &FrameState,
    m: &Matrices,
    imm: &crate::immediate::Immediate,
    pname: u32,
) -> Option<([f64; 16], usize)> {
    if let Some(v) = current_query(imm, pname) {
        return Some(v);
    }
    if let Some(v) = s.light.query(pname) {
        return Some(v);
    }
    if let Some(x) = s.select.query(pname).or_else(|| s.lists.query(pname)) {
        let mut v = [0.; 16];
        v[0] = x;
        return Some((v, 1));
    }
    if let Some(v) = s.fog.query(pname) {
        return Some(v);
    }
    if let Some(v) = s.textures.query(pname) {
        return Some(v);
    }
    if let Some(v) = s.raster.query(pname) {
        return Some(v);
    }
    let mut v = [0.; 16];
    let len = match pname {
        0x0ba2 => {
            for (out, input) in v.iter_mut().zip(s.viewport) {
                *out = input as f64;
            }

            4
        }

        0x0c22 => {
            for (i, out) in v[..4].iter_mut().enumerate() {
                *out = ((s.clear_color >> (24 - 8 * i)) & 255) as f64 / 255.;
            }

            4
        }

        0x0b73 => {
            v[0] = s.clear_depth as f64;

            1
        }

        0x0b54 => {
            v[0] = if s.poly.flat { 0x1d00 } else { 0x1d01 } as f64;
            1
        }

        0x0b40 => {
            v[0] = s.poly.modes[0] as f64;
            v[1] = s.poly.modes[1] as f64;
            2
        }

        0x8038 => {
            v[0] = s.poly.factor as f64;
            1
        }

        0x2a00 => {
            v[0] = s.poly.units as f64;
            1
        }

        0x8037 | 0x2a02 | 0x2a01 => {
            v[0] = s.poly.offset[offset_index(pname)] as u8 as f64;
            1
        }

        0x0d32 => {
            v[0] = crate::clip::MAX_PLANES as f64;
            1
        }

        0x0b91 => {
            v[0] = s.clear_stencil as f64;
            1
        }

        0x0b21 => {
            v[0] = s.line_width as f64;
            1
        }

        0x0b11 => {
            v[0] = s.point_size as f64;
            1
        }

        // Points are quads, so any size works; lines any width.
        0x0b12 | 0x846d => {
            v[0] = 1.;
            v[1] = crate::lines::MAX_POINT_SIZE as f64;
            2
        }

        0x0d3a => {
            v[0] = 400.;
            v[1] = 240.;
            2
        }

        _ => return m.query(pname).map(|(v, n)| (v.map(|x| x as f64), n)),
    };

    Some((v, len))
}

#[no_mangle]
pub unsafe extern "C" fn glGetFloatv(pname: u32, out: *mut f32) {
    with_renderer(|r| {
        let (s, m, imm) = (&r.frame, &mut r.matrices, &r.immediate);
        if out.is_null() {
            m.fail(0x0501);
            return;
        }

        match query(s, m, imm, pname) {
            Some((v, n)) => {
                for (i, value) in v[..n].iter().enumerate() {
                    out.add(i).write(*value as f32);
                }
            }

            None => m.fail(0x0500),
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glGetIntegerv(pname: u32, out: *mut i32) {
    with_renderer(|r| {
        let (s, m, imm) = (&r.frame, &mut r.matrices, &r.immediate);
        if out.is_null() {
            m.fail(0x0501);
            return;
        }

        match query(s, m, imm, pname) {
            Some((v, n)) => {
                for (i, value) in v[..n].iter().enumerate() {
                    let integer = if pname == 0x0c22
                        || pname == 0x0b73
                        || pname == 0x0b70
                        || pname == 0x0bc2
                        || pname == 0x8005
                    {
                        (*value * 2147483647.) as i32
                    } else {
                        (*value + if *value >= 0. { 0.5 } else { -0.5 }) as i32
                    };

                    out.add(i).write(integer);
                }
            }

            None => m.fail(0x0500),
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_and_offscreen_viewports() {
        let mut s = FrameState::new();
        assert!(s.visible());
        for v in [
            [0, 0, 0, 120],
            [400, 0, 200, 120],
            [i32::MIN, 0, 400, 240],
            [-400, 0, 400, 240],
        ] {
            s.viewport = v;
            assert!(!s.visible());
        }

        s.viewport = [-10, -10, 400, 240];
        assert!(s.visible());
        assert_eq!(byte(0.5), 128);
        assert_eq!(byte(-1.), 0);
        assert_eq!(byte(2.), 255);
    }
}

#[no_mangle]
pub unsafe extern "C" fn glGetBooleanv(pname: u32, out: *mut u8) {
    with_renderer(|r| {
        let (s, m, imm) = (&r.frame, &mut r.matrices, &r.immediate);
        if out.is_null() {
            m.fail(0x0501);
            return;
        }

        match query(s, m, imm, pname) {
            Some((v, n)) => {
                for (i, value) in v[..n].iter().enumerate() {
                    out.add(i).write((*value != 0.) as u8);
                }
            }
            None => m.fail(0x0500),
        }
    });
}
