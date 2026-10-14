//! GL frame state. Top-screen landscape viewport and whole-buffer clears.
use crate::{
    matrix::Matrices,
    renderer::{clear_gl, with_frame},
};

pub const COLOR: u32 = 0x4000;
pub const DEPTH: u32 = 0x0100;

pub struct FrameState {
    pub raster: crate::raster::Raster,
    pub textures: crate::texture::Textures,
    pub viewport: [i32; 4],
    pub viewport_dirty: bool,
    pub drawable: bool,
    pub clear_color: u32,
    pub clear_depth: f32,
}

impl FrameState {
    pub fn new() -> Self {
        Self {
            raster: crate::raster::Raster::new(),
            textures: crate::texture::Textures::new(),
            viewport: [0, 0, 400, 240],
            viewport_dirty: false,
            drawable: true,
            clear_color: 0,
            clear_depth: 1.,
        }
    }

    pub fn visible(&self) -> bool {
        let [x, y, w, h] = self.viewport;
        w > 0 && h > 0 && x < 400 && y < 240 && x as i64 + w as i64 > 0 && y as i64 + h as i64 > 0
    }
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
    with_frame(|s, _| s.clear_color = (byte(r) << 24) | (byte(g) << 16) | (byte(b) << 8) | byte(a));
}

#[no_mangle]
pub unsafe extern "C" fn glClearDepth(depth: f64) {
    with_frame(|s, _| s.clear_depth = depth.clamp(0., 1.) as f32);
}

#[no_mangle]
pub unsafe extern "C" fn glClearDepthf(depth: f32) {
    glClearDepth(depth as f64);
}

#[no_mangle]
pub unsafe extern "C" fn glViewport(x: i32, y: i32, w: i32, h: i32) {
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
pub unsafe extern "C" fn glClear(mask: u32) {
    clear_gl(mask);
}

fn query(s: &FrameState, m: &Matrices, pname: u32) -> Option<([f64; 16], usize)> {
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
    with_frame(|s, m| {
        if out.is_null() {
            m.fail(0x0501);
            return;
        }

        match query(s, m, pname) {
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
    with_frame(|s, m| {
        if out.is_null() {
            m.fail(0x0501);
            return;
        }

        match query(s, m, pname) {
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
    with_frame(|s, m| {
        if out.is_null() {
            m.fail(0x0501);
            return;
        }

        match query(s, m, pname) {
            Some((v, n)) => {
                for (i, value) in v[..n].iter().enumerate() {
                    out.add(i).write((*value != 0.) as u8);
                }
            }
            None => m.fail(0x0500),
        }
    });
}
