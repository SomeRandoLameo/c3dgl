//! Immediate triangle assembly; GPU copies live until the submitted frame completes.
use crate::renderer::{with_immediate, Vertex};
use alloc::vec::Vec;

pub(crate) struct Immediate {
    pub active: bool,
    pub color: [f32; 4],
    vertices: Vec<Vertex>,
}

impl Immediate {
    pub fn new() -> Self {
        Self {
            active: false,
            color: [1.; 4],
            vertices: Vec::new(),
        }
    }

    fn begin(&mut self, mode: u32) -> Result<(), u32> {
        if self.active {
            return Err(0x0502);
        }

        if mode != 4 {
            return Err(0x0500);
        }

        self.vertices
            .try_reserve(4096usize.saturating_sub(self.vertices.len()))
            .map_err(|_| 0x0505u32)?;
        self.vertices.clear();
        self.active = true;

        Ok(())
    }

    fn vertex(&mut self, position: [f32; 4]) -> Result<(), u32> {
        if !self.active {
            return Err(0x0502);
        }

        if self.vertices.len() == 4096 {
            return Err(0x0505);
        }

        self.vertices.push(Vertex {
            position,
            color: self.color,
        });

        Ok(())
    }

    pub fn end(&mut self) -> Result<Vec<Vertex>, u32> {
        if !self.active {
            return Err(0x0502);
        }

        self.active = false;
        self.vertices.truncate(self.vertices.len() / 3 * 3);

        Ok(core::mem::take(&mut self.vertices))
    }
}

#[no_mangle]
pub unsafe extern "C" fn glBegin(mode: u32) {
    with_immediate(|r| {
        if let Err(e) = r.immediate.begin(mode) {
            r.matrices.fail(e);
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glEnd() {
    crate::renderer::end_immediate();
}

#[no_mangle]
pub unsafe extern "C" fn glVertex4f(x: f32, y: f32, z: f32, w: f32) {
    with_immediate(|r| {
        if let Err(e) = r.immediate.vertex([x, y, z, w]) {
            r.matrices.fail(e);
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glVertex3f(x: f32, y: f32, z: f32) {
    glVertex4f(x, y, z, 1.);
}

#[no_mangle]
pub unsafe extern "C" fn glVertex2f(x: f32, y: f32) {
    glVertex4f(x, y, 0., 1.);
}

#[no_mangle]
pub unsafe extern "C" fn glColor4f(r: f32, g: f32, b: f32, a: f32) {
    with_immediate(|s| {
        s.immediate.color = [r, g, b, a].map(|v| v.clamp(0., 1.));
    });
}

#[no_mangle]
pub unsafe extern "C" fn glColor3f(r: f32, g: f32, b: f32) {
    glColor4f(r, g, b, 1.);
}

#[no_mangle]
pub unsafe extern "C" fn glColor4ub(r: u8, g: u8, b: u8, a: u8) {
    glColor4f(
        r as f32 / 255.,
        g as f32 / 255.,
        b as f32 / 255.,
        a as f32 / 255.,
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn assembly_and_errors() {
        let mut s = Immediate::new();
        assert_eq!(s.vertex([0.; 4]), Err(0x0502));
        assert_eq!(s.begin(1), Err(0x0500));
        s.begin(4).unwrap();
        assert_eq!(s.begin(4), Err(0x0502));
        s.color = [0., 1., 0., 1.];
        for _ in 0..4 {
            s.vertex([1., 2., 3., 1.]).unwrap();
        }
        let v = s.end().unwrap();
        assert_eq!(v.len(), 3);
        assert_eq!(v[0].color, [0., 1., 0., 1.]);
        assert!(s.end().is_err());
        s.begin(4).unwrap();
        assert_eq!(s.color, [0., 1., 0., 1.]);
        for _ in 0..4096 {
            s.vertex([0.; 4]).unwrap();
        }
        assert_eq!(s.vertex([0.; 4]), Err(0x0505));
    }
}
