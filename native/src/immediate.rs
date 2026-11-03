//! Immediate triangle assembly; GPU copies live until the submitted frame completes.
use crate::renderer::{with_immediate, GpuVertex};
use alloc::vec::Vec;

pub(crate) struct Batch {
    pub vertices: Vec<GpuVertex>,
    pub indices: Vec<u16>,
    pub edges: Vec<bool>,
    /// Back colors (two-sided lighting), or empty.
    pub backs: Vec<[f32; 4]>,
    pub count: u32,
    /// GL mode of the batch; modes below 4 (points, lines) carry raw vertices for expansion.
    pub primitive: u32,
    /// Flat shading is already in the vertex colors (separate triangles and quads: every vertex
    /// has the color of its primitive's provoking vertex).
    pub flat_applied: bool,
}

pub(crate) struct Immediate {
    pub active: bool,
    pub color: [f32; 4],
    pub texcoord: [f32; 4],
    pub extra_texcoords: [[f32; 4]; 2],
    pub edge: bool,
    pub normal: [f32; 3],
    mode: u32,
    /// Flat shading of separate triangles or quads is applied to the colors as they arrive.
    flat_colors: bool,
    vertices: Vec<GpuVertex>,
    edges: Vec<bool>,
    /// Back colors of two-sided lighting, parallel to `vertices` (empty without it).
    backs: Vec<[f32; 4]>,
    /// The GPU bytes of the last `color` converted (most vertices share the current color).
    gpu_cache: core::cell::Cell<([f32; 4], [u8; 4])>,
}

impl Immediate {
    /// The current color as GPU color bytes.
    #[inline]
    pub fn gpu_color(&self) -> [u8; 4] {
        let (from, gpu) = self.gpu_cache.get();
        if from == self.color {
            return gpu;
        }
        let gpu = self.color.map(gpu_color_byte);
        self.gpu_cache.set((self.color, gpu));
        gpu
    }

    pub fn new() -> Self {
        Self {
            active: false,
            color: [1.; 4],
            texcoord: [0., 0., 0., 1.],
            extra_texcoords: [[0., 0., 0., 1.]; 2],
            edge: true,
            normal: [0., 0., 1.],
            mode: 4,
            flat_colors: false,
            vertices: Vec::new(),
            edges: Vec::new(),
            backs: Vec::new(),
            gpu_cache: core::cell::Cell::new(([1.; 4], [255; 4])),
        }
    }

    fn begin(&mut self, mode: u32, flat: bool) -> Result<(), u32> {
        if self.active {
            return Err(0x0502);
        }

        crate::primitive::Plan::new(mode, 0)?;

        self.vertices.clear();
        self.edges.clear();
        // Room for the most vertices a batch takes: `push` writes without checks.
        self.vertices.try_reserve(4096).map_err(|_| 0x0505u32)?;
        self.edges.try_reserve(4096).map_err(|_| 0x0505u32)?;
        self.backs.clear();
        self.active = true;
        self.mode = mode;
        self.flat_colors = flat && matches!(mode, 4 | 7);

        Ok(())
    }

    fn vertex(&mut self, position: [f32; 4]) -> Result<(), u32> {
        self.vertex_with(position, None, None)
    }

    /// A vertex whose color and texcoord replace the current ones (evaluators).
    pub fn vertex_with(
        &mut self,
        position: [f32; 4],
        color: Option<[f32; 4]>,
        texcoord: Option<[f32; 4]>,
    ) -> Result<(), u32> {
        let color = color.unwrap_or(self.color).map(gpu_color_byte);
        self.push(position, color, None, texcoord, None)
    }

    /// A vertex with its final (possibly lit) GPU color bytes and optional back color.
    #[inline]
    pub fn push(
        &mut self,
        position: [f32; 4],
        color: [u8; 4],
        back: Option<[f32; 4]>,
        texcoord: Option<[f32; 4]>,
        extra: Option<[[f32; 4]; 2]>,
    ) -> Result<(), u32> {
        if !self.active {
            return Err(0x0502);
        }

        let n = self.vertices.len();
        if n == 4096 {
            return Err(0x0505);
        }

        if let Some(back) = back {
            self.backs.try_reserve(1).map_err(|_| 0x0505u32)?;
            self.backs.push(back);
        }
        // Capacity for 4096 reserved by `begin`.
        unsafe {
            self.edges.as_mut_ptr().add(n).write(self.edge);
            self.edges.set_len(n + 1);
            self.vertices.as_mut_ptr().add(n).write(GpuVertex {
                position,
                color,
                texcoord: texcoord.unwrap_or(self.texcoord),
                extra_texcoords: extra.unwrap_or(self.extra_texcoords),
            });
            self.vertices.set_len(n + 1);
        }
        // The provoking (last) vertex of a separate triangle or quad: its colors for the others.
        let size = if self.mode == 4 { 3 } else { 4 };
        if self.flat_colors && (n + 1) % size == 0 {
            for k in n + 1 - size..n {
                self.vertices[k].color = color;
            }
            if let Some(back) = back {
                let first = self.backs.len() - size;
                for b in &mut self.backs[first..first + size - 1] {
                    *b = back;
                }
            }
        }

        Ok(())
    }

    /// Whether the next vertex provokes the color of a primitive under flat shading (GL 1.1 table
    /// 2.8, the vertices `polygon::visit` and `lines::expand` take): the others' colors are
    /// never seen.
    pub fn next_provokes(&self) -> bool {
        let i = self.vertices.len();
        match self.mode {
            1 => i % 2 == 1,
            3 => i >= 1,
            4 => i % 3 == 2,
            5 | 6 => i >= 2,
            7 => i % 4 == 3,
            8 => i >= 3 && i % 2 == 1,
            9 => i == 0,
            // Points; line loops (closed by the first vertex).
            _ => true,
        }
    }

    /// Take back the storage of a drawn batch (no allocation for the next glBegin).
    pub fn recycle(&mut self, batch: Batch) {
        if self.vertices.capacity() < batch.vertices.capacity() {
            self.vertices = batch.vertices;
            self.vertices.clear();
        }
        if self.edges.capacity() < batch.edges.capacity() {
            self.edges = batch.edges;
            self.edges.clear();
        }
        if self.backs.capacity() < batch.backs.capacity() {
            self.backs = batch.backs;
            self.backs.clear();
        }
    }

    pub fn end(&mut self) -> Result<Batch, u32> {
        if !self.active {
            return Err(0x0502);
        }

        self.active = false;
        let plan = crate::primitive::Plan::new(self.mode, self.vertices.len())?;
        self.vertices.truncate(plan.consumed);
        self.edges.truncate(plan.consumed);
        self.backs.truncate(plan.consumed);
        let vertices = core::mem::take(&mut self.vertices);
        let edges = core::mem::take(&mut self.edges);
        let backs = core::mem::take(&mut self.backs);
        let indices = if self.mode <= 4 {
            Vec::new()
        } else {
            plan.indices(|i| i as u16)?
        };

        Ok(Batch {
            vertices,
            indices,
            edges,
            backs,
            count: if self.mode < 4 {
                plan.consumed as u32
            } else {
                plan.output as u32
            },
            primitive: self.mode,
            flat_applied: self.flat_colors,
        })
    }
}

#[no_mangle]
pub unsafe extern "C" fn glBegin(mode: u32) {
    if crate::lists::record(crate::lists::Cmd::Begin(mode)) {
        return;
    }
    with_immediate(|r| {
        if let Err(e) = r.immediate.begin(mode, r.frame.poly.flat) {
            r.matrices.fail(e);
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glEnd() {
    if crate::lists::record(crate::lists::Cmd::End) {
        return;
    }
    crate::renderer::end_immediate();
}

#[no_mangle]
pub unsafe extern "C" fn glVertex4f(x: f32, y: f32, z: f32, w: f32) {
    if crate::lists::record(crate::lists::Cmd::Vertex([x, y, z, w])) {
        return;
    }
    with_immediate(|r| crate::renderer::emit_vertex(r, [x, y, z, w], None, None));
}

#[no_mangle]
pub unsafe extern "C" fn glVertex3f(x: f32, y: f32, z: f32) {
    glVertex4f(x, y, z, 1.);
}

#[no_mangle]
pub unsafe extern "C" fn glVertex2f(x: f32, y: f32) {
    glVertex4f(x, y, 0., 1.);
}

/// A color component as the byte the GPU gets.
pub(crate) fn gpu_color_byte(value: f32) -> u8 {
    (value.clamp(0., 1.) * 255. + 0.5) as u8
}

pub(crate) fn gpu_color_component(value: f32) -> f32 {
    (value.clamp(0., 1.) * 255. + 0.5) as u8 as f32
}

// Match the reference's byte storage followed by float GPU expansion.
pub(crate) fn color_component(value: f32) -> f32 {
    let byte = if value <= 0. {
        0
    } else if value >= 1. {
        255
    } else {
        (value * 255. + 0.5) as u8
    };
    byte as f32 * (1. / 255.)
}

#[no_mangle]
pub unsafe extern "C" fn glColor4f(r: f32, g: f32, b: f32, a: f32) {
    if crate::lists::record(crate::lists::Cmd::Color([r, g, b, a])) {
        return;
    }
    with_immediate(|s| {
        s.immediate.color = [r, g, b, a].map(color_component);
    });
}

#[no_mangle]
pub unsafe extern "C" fn glColor3f(r: f32, g: f32, b: f32) {
    glColor4f(r, g, b, 1.);
}

#[no_mangle]
pub unsafe extern "C" fn glColor4ub(r: u8, g: u8, b: u8, a: u8) {
    if crate::lists::record(crate::lists::Cmd::Color4ub([r, g, b, a])) {
        return;
    }
    with_immediate(|s| {
        s.immediate.color = [r, g, b, a].map(|v| v as f32 * (1. / 255.));
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn byte_colors_round_trip_through_the_float_path() {
        // The unsigned byte fast path of color arrays relies on this.
        for c in 0..=255u8 {
            assert_eq!(gpu_color_byte(c as f32 / 255.), c);
        }
    }

    #[test]
    fn reference_byte_color_rounding_and_expansion() {
        assert_eq!(color_component(-1.), 0.);
        assert_eq!(color_component(2.), 1.);
        assert_eq!(color_component(0.3), 77. * (1. / 255.));
        assert_eq!(color_component(0.5), 128. * (1. / 255.));
        for byte in 0..=255 {
            let input = byte as f32 / 255.;
            assert_eq!(color_component(input), byte as f32 * (1. / 255.));
        }
        for byte in 0..255 {
            let threshold = (byte as f32 + 0.5) / 255.;
            assert_eq!(
                color_component(threshold - 0.00001),
                byte as f32 * (1. / 255.)
            );
            assert_eq!(
                color_component(threshold + 0.00001),
                (byte + 1) as f32 * (1. / 255.)
            );
        }
    }

    #[test]
    fn assembled_indices_preserve_vertex_attributes_and_current_state() {
        for mode in 5..=9 {
            let mut s = Immediate::new();
            s.begin(mode, false).unwrap();
            for i in 0..5 {
                s.color = [i as f32, 1., 0., 1.];
                s.texcoord = [i as f32, 0.5, 0., 1.];
                s.vertex([i as f32, 0., 0., 1.]).unwrap();
            }
            let batch = s.end().unwrap();
            let plan = crate::primitive::Plan::new(mode, 5).unwrap();
            assert_eq!(batch.vertices.len(), plan.consumed);
            assert_eq!(batch.count as usize, plan.output);
            for i in batch.indices {
                let v = batch.vertices[i as usize];
                assert_eq!(v.position[0], i as f32);
                assert_eq!(v.color[0], if i == 0 { 0 } else { 255 });
                assert_eq!(v.texcoord[0], i as f32);
            }
            assert_eq!(s.color[0], 4.);
            assert_eq!(s.texcoord[0], 4.);
        }
    }

    #[test]
    fn vector_reads_allow_unaligned_inputs() {
        let values = [1f32, 2., 3., 4.];
        let mut bytes = [0u8; 17];
        unsafe {
            core::ptr::copy_nonoverlapping(
                values.as_ptr().cast::<u8>(),
                bytes.as_mut_ptr().add(1),
                16,
            );
            assert_eq!(vector::<f32, 4>(bytes.as_ptr().add(1).cast()), Some(values));
        }
    }

    #[test]
    fn assembly_and_errors() {
        let mut s = Immediate::new();
        assert_eq!(s.vertex([0.; 4]), Err(0x0502));
        assert_eq!(s.begin(10, false), Err(0x0500));
        s.begin(4, false).unwrap();
        assert_eq!(s.begin(4, false), Err(0x0502));
        s.color = [0., 1., 0., 1.];
        for _ in 0..4 {
            s.vertex([1., 2., 3., 1.]).unwrap();
        }
        let v = s.end().unwrap();
        assert_eq!(v.vertices.len(), 3);
        assert_eq!(v.count, 3);
        assert!(v.indices.is_empty());
        assert_eq!(v.vertices[0].color, [0, 255, 0, 255]);
        assert!(s.end().is_err());
        s.begin(4, false).unwrap();
        assert_eq!(s.color, [0., 1., 0., 1.]);
        for _ in 0..4096 {
            s.vertex([0.; 4]).unwrap();
        }
        assert_eq!(s.vertex([0.; 4]), Err(0x0505));
    }
}

#[no_mangle]
pub unsafe extern "C" fn glNormal3f(x: f32, y: f32, z: f32) {
    if crate::lists::record(crate::lists::Cmd::Normal([x, y, z])) {
        return;
    }
    with_immediate(|r| r.immediate.normal = [x, y, z]);
}

#[no_mangle]
pub unsafe extern "C" fn glNormal3fv(v: *const f32) {
    if let Some(n) = vector::<f32, 3>(v) {
        glNormal3f(n[0], n[1], n[2]);
    }
}

#[no_mangle]
pub unsafe extern "C" fn glNormal3d(x: f64, y: f64, z: f64) {
    glNormal3f(x as f32, y as f32, z as f32);
}

/// Signed integers map to [-1, 1] like glColor: (2c + 1) / (2^b - 1).
#[no_mangle]
pub unsafe extern "C" fn glNormal3b(x: i8, y: i8, z: i8) {
    let f = |c: i8| (2. * c as f32 + 1.) / 255.;
    glNormal3f(f(x), f(y), f(z));
}

#[no_mangle]
pub unsafe extern "C" fn glNormal3s(x: i16, y: i16, z: i16) {
    let f = |c: i16| (2. * c as f32 + 1.) / 65535.;
    glNormal3f(f(x), f(y), f(z));
}

#[no_mangle]
pub unsafe extern "C" fn glNormal3i(x: i32, y: i32, z: i32) {
    let f = |c: i32| ((2. * c as f64 + 1.) / 4294967295.) as f32;
    glNormal3f(f(x), f(y), f(z));
}

#[no_mangle]
pub unsafe extern "C" fn glEdgeFlag(flag: u8) {
    if crate::lists::record(crate::lists::Cmd::EdgeFlag(flag)) {
        return;
    }
    with_immediate(|r| r.immediate.edge = flag != 0);
}

#[no_mangle]
pub unsafe extern "C" fn glEdgeFlagv(flag: *const u8) {
    if flag.is_null() {
        with_immediate(|r| r.matrices.fail(0x0501));
    } else {
        glEdgeFlag(flag.read());
    }
}

/// A counter-clockwise quad from (x1, y1) to (x2, y2) at z = 0.
#[no_mangle]
pub unsafe extern "C" fn glRectf(x1: f32, y1: f32, x2: f32, y2: f32) {
    glBegin(7);
    glVertex2f(x1, y1);
    glVertex2f(x2, y1);
    glVertex2f(x2, y2);
    glVertex2f(x1, y2);
    glEnd();
}

#[no_mangle]
pub unsafe extern "C" fn glRectd(x1: f64, y1: f64, x2: f64, y2: f64) {
    glRectf(x1 as f32, y1 as f32, x2 as f32, y2 as f32);
}

#[no_mangle]
pub unsafe extern "C" fn glRecti(x1: i32, y1: i32, x2: i32, y2: i32) {
    glRectf(x1 as f32, y1 as f32, x2 as f32, y2 as f32);
}

#[no_mangle]
pub unsafe extern "C" fn glRects(x1: i16, y1: i16, x2: i16, y2: i16) {
    glRectf(x1 as f32, y1 as f32, x2 as f32, y2 as f32);
}

#[no_mangle]
pub unsafe extern "C" fn glRectfv(v1: *const f32, v2: *const f32) {
    if v1.is_null() || v2.is_null() {
        with_immediate(|r| r.matrices.fail(0x0501));
    } else {
        glRectf(v1.read(), v1.add(1).read(), v2.read(), v2.add(1).read());
    }
}

#[no_mangle]
pub unsafe extern "C" fn glTexCoord2f(s: f32, t: f32) {
    glTexCoord4f(s, t, 0., 1.);
}

#[no_mangle]
pub unsafe extern "C" fn glTexCoord3f(s: f32, t: f32, r: f32) {
    glTexCoord4f(s, t, r, 1.);
}
#[no_mangle]
pub unsafe extern "C" fn glTexCoord4f(s: f32, t: f32, r: f32, q: f32) {
    if crate::lists::record(crate::lists::Cmd::TexCoord([s, t, r, q])) {
        return;
    }
    with_immediate(|state| {
        state.immediate.texcoord = [s, t, r, q];
        if q != 1. {
            state.frame.textures.mark_q();
        }
    });
}
#[no_mangle]
pub unsafe extern "C" fn glTexCoord3fv(v: *const f32) {
    if let Some([s, t, r]) = vector(v) {
        glTexCoord3f(s, t, r);
    }
}
#[no_mangle]
pub unsafe extern "C" fn glTexCoord4fv(v: *const f32) {
    if let Some([s, t, r, q]) = vector(v) {
        glTexCoord4f(s, t, r, q);
    }
}

#[no_mangle]
pub unsafe extern "C" fn glMultiTexCoord4f(unit: u32, s: f32, t: f32, r: f32, q: f32) {
    if crate::lists::record(crate::lists::Cmd::MultiTexCoord(unit, [s, t, r, q])) {
        return;
    }
    with_immediate(|state| match unit {
        0x84c0 => {
            state.immediate.texcoord = [s, t, r, q];
            if q != 1. {
                state.frame.textures.mark_q();
            }
        }
        0x84c1..=0x84c2 => state.immediate.extra_texcoords[(unit - 0x84c1) as usize] = [s, t, r, q],
        _ => state.matrices.fail(0x0500),
    });
}

#[no_mangle]
pub unsafe extern "C" fn glMultiTexCoord2f(unit: u32, s: f32, t: f32) {
    glMultiTexCoord4f(unit, s, t, 0., 1.);
}
#[no_mangle]
pub unsafe extern "C" fn glMultiTexCoord3f(unit: u32, s: f32, t: f32, r: f32) {
    glMultiTexCoord4f(unit, s, t, r, 1.);
}

unsafe fn vector<T: Copy, const N: usize>(values: *const T) -> Option<[T; N]> {
    if values.is_null() {
        with_immediate(|r| r.matrices.fail(0x0501));
        return None;
    }
    Some(core::array::from_fn(|i| {
        core::ptr::read_unaligned(values.add(i))
    }))
}
#[no_mangle]
pub unsafe extern "C" fn glVertex2fv(v: *const f32) {
    if let Some([x, y]) = vector(v) {
        glVertex2f(x, y);
    }
}
#[no_mangle]
pub unsafe extern "C" fn glVertex3fv(v: *const f32) {
    if let Some([x, y, z]) = vector(v) {
        glVertex3f(x, y, z);
    }
}
#[no_mangle]
pub unsafe extern "C" fn glVertex4fv(v: *const f32) {
    if let Some([x, y, z, w]) = vector(v) {
        glVertex4f(x, y, z, w);
    }
}
#[no_mangle]
pub unsafe extern "C" fn glColor3fv(v: *const f32) {
    if let Some([r, g, b]) = vector(v) {
        glColor3f(r, g, b);
    }
}
#[no_mangle]
pub unsafe extern "C" fn glColor4fv(v: *const f32) {
    if let Some([r, g, b, a]) = vector(v) {
        glColor4f(r, g, b, a);
    }
}
#[no_mangle]
pub unsafe extern "C" fn glColor3ub(r: u8, g: u8, b: u8) {
    glColor4ub(r, g, b, 255);
}
#[no_mangle]
pub unsafe extern "C" fn glColor3ubv(v: *const u8) {
    if let Some([r, g, b]) = vector(v) {
        glColor3ub(r, g, b);
    }
}
#[no_mangle]
pub unsafe extern "C" fn glColor4ubv(v: *const u8) {
    if let Some([r, g, b, a]) = vector(v) {
        glColor4ub(r, g, b, a);
    }
}
#[no_mangle]
pub unsafe extern "C" fn glTexCoord2fv(v: *const f32) {
    if let Some([s, t]) = vector(v) {
        glTexCoord2f(s, t);
    }
}
