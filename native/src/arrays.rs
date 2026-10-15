//! Client arrays and buffer-backed geometry. Cache storage survives submitted work.
use crate::platform::gpu as p;
use crate::renderer::{with_renderer, Buffer, GpuVertex};
use alloc::vec::Vec;
use core::{ffi::c_void, ptr};

#[derive(Clone, Copy, PartialEq, Eq)]
struct Array {
    size: usize,
    kind: u32,
    stride: usize,
    pointer: usize,
    buffer: u32,
}

impl Default for Array {
    /// Four GL_FLOAT components, tightly packed, no pointer.
    fn default() -> Self {
        Self {
            size: 4,
            kind: 0x1406,
            stride: 0,
            pointer: 0,
            buffer: 0,
        }
    }
}

impl Array {
    /// No edge flag array: GL_UNSIGNED_BYTE, tightly packed.
    fn edge_flag_default() -> Self {
        Self {
            size: 1,
            kind: 0x1401,
            ..Self::default()
        }
    }

    /// No point size array: GL_FLOAT, tightly packed, no pointer.
    fn point_size_default() -> Self {
        Self {
            size: 1,
            kind: 0x1406,
            ..Self::default()
        }
    }
}

struct Object {
    name: u32,
    data: Vec<u8>,
    /// GL_BUFFER_USAGE (0 until glBufferData).
    usage: u32,
}

#[derive(PartialEq, Eq)]
struct Key {
    mode: u32,
    vertex: Array,
    color: Option<Array>,
    current_color: [u32; 4],
    texcoord: Option<Array>,
    current_texcoord: [u32; 4],
    extra_texcoords: [Option<Array>; 2],
    extra_current: [[u32; 4]; 2],
    first: usize,
    count: usize,
    kind: u32,
    indices: usize,
    element: u32,
}

struct Mesh {
    key: Key,
    storage: Buffer,
    index_offset: Option<u32>,
}

/// The values of one array element.
#[derive(Default)]
pub(crate) struct Element {
    pub texcoords: [Option<[f32; 4]>; 3],
    pub color: Option<[f32; 4]>,
    pub normal: Option<[f32; 3]>,
    pub edge: Option<bool>,
    pub position: Option<[f32; 4]>,
}

/// What glPushClientAttrib keeps of the vertex arrays.
#[derive(Clone, Copy)]
pub(crate) struct ArraysSave {
    vertex: Array,
    color: Array,
    texcoord: Array,
    extra_texcoords: [Array; 2],
    normal: Array,
    point_size: Array,
    edge_flag: Array,
    enabled: [bool; 8],
    bound: [u32; 2],
    client_active: usize,
}

pub(crate) struct Arrays {
    vertex: Array,
    color: Array,
    texcoord: Array,
    extra_texcoords: [Array; 2],
    normal: Array,
    point_size: Array,
    edge_flag: Array,
    pub client_active: usize,
    /// Vertex, color, texcoord 0..2, normal, point size, edge flag.
    enabled: [bool; 8],
    bound: [u32; 2],
    next: u32,
    objects: Vec<Object>,
    cache: Vec<Mesh>,
    retired: Vec<Mesh>,
}

impl Arrays {
    pub fn new() -> Self {
        Self {
            vertex: Array::default(),
            color: Array::default(),
            texcoord: Array::default(),
            extra_texcoords: [Array::default(); 2],
            normal: Array::default(),
            point_size: Array::point_size_default(),
            edge_flag: Array::edge_flag_default(),
            client_active: 0,
            enabled: [false; 8],
            bound: [0; 2],
            next: 1,
            objects: Vec::new(),
            cache: Vec::new(),
            retired: Vec::new(),
        }
    }

    pub fn collected(&mut self) {
        self.retired.clear();
    }

    fn invalidate(&mut self, name: u32) {
        let mut i = 0;
        while i < self.cache.len() {
            let k = &self.cache[i].key;
            if k.vertex.buffer == name
                || k.color.is_some_and(|a| a.buffer == name)
                || k.texcoord.is_some_and(|a| a.buffer == name)
                || k.extra_texcoords.iter().flatten().any(|a| a.buffer == name)
                || k.element == name
            {
                self.retired.push(self.cache.swap_remove(i));
            } else {
                i += 1;
            }
        }
    }

    pub fn save(&self) -> ArraysSave {
        ArraysSave {
            vertex: self.vertex,
            color: self.color,
            texcoord: self.texcoord,
            extra_texcoords: self.extra_texcoords,
            normal: self.normal,
            point_size: self.point_size,
            edge_flag: self.edge_flag,
            enabled: self.enabled,
            bound: self.bound,
            client_active: self.client_active,
        }
    }

    /// Buffers deleted since the save are not bound again (their arrays are cleared).
    pub fn restore(&mut self, s: &ArraysSave) {
        let exists = |name: u32| name == 0 || self.objects.iter().any(|o| o.name == name);
        let fix = |a: Array, exists: &dyn Fn(u32) -> bool| {
            if exists(a.buffer) {
                a
            } else {
                Array::default()
            }
        };
        self.vertex = fix(s.vertex, &exists);
        self.color = fix(s.color, &exists);
        self.texcoord = fix(s.texcoord, &exists);
        self.extra_texcoords = [
            fix(s.extra_texcoords[0], &exists),
            fix(s.extra_texcoords[1], &exists),
        ];
        self.normal = fix(s.normal, &exists);
        self.edge_flag = if exists(s.edge_flag.buffer) {
            s.edge_flag
        } else {
            Array::edge_flag_default()
        };
        self.point_size = if exists(s.point_size.buffer) {
            s.point_size
        } else {
            Array::point_size_default()
        };
        self.enabled = s.enabled;
        self.bound = [
            if exists(s.bound[0]) { s.bound[0] } else { 0 },
            if exists(s.bound[1]) { s.bound[1] } else { 0 },
        ];
        self.client_active = s.client_active;
    }

    /// The array indices of the vertices of a draw in drawing order (index lists resolved).
    unsafe fn indices_of(
        &self,
        first: usize,
        count: usize,
        kind: u32,
        indices: usize,
    ) -> Result<Vec<usize>, u32> {
        let w = match kind {
            0 => 0,
            0x1401 => 1,
            0x1403 => 2,
            0x1405 => 4,
            _ => return Err(0x0500),
        };
        let source = if kind == 0 {
            ptr::null()
        } else {
            self.source(
                self.bound[1],
                indices,
                count.checked_mul(w).ok_or(0x0501u32)?,
            )?
        };
        let mut out = Vec::new();
        out.try_reserve_exact(count).map_err(|_| 0x0505u32)?;
        for i in 0..count {
            out.push(match w {
                0 => first + i,
                1 => ptr::read(source.add(i)) as usize,
                2 => ptr::read_unaligned(source.add(i * 2).cast::<u16>()) as usize,
                _ => ptr::read_unaligned(source.add(i * 4).cast::<u32>()) as usize,
            });
        }
        Ok(out)
    }

    /// The attribute values of array element `index`: texcoords per unit, color (as bytes in
    /// [0, 1]), normal, edge flag, position; None for an attribute without an enabled array.
    pub unsafe fn element(&self, index: usize) -> Result<Element, u32> {
        let mut e = Element::default();
        for unit in 0..3 {
            if self.enabled[2 + unit] {
                let a = if unit == 0 {
                    self.texcoord
                } else {
                    self.extra_texcoords[unit - 1]
                };
                e.texcoords[unit] = Some(self.value(a, index, false)?);
            }
        }
        if self.enabled[1] {
            e.color = Some(self.value(self.color, index, true)?);
        }
        if self.enabled[5] {
            e.normal = Some(self.normal_value(index)?);
        }
        if self.enabled[7] {
            e.edge = Some(self.edge_value(index)?);
        }
        if self.enabled[0] {
            e.position = Some(self.value(self.vertex, index, false)?);
        }
        Ok(e)
    }

    pub fn vertex_enabled(&self) -> bool {
        self.enabled[0]
    }

    /// The vertex indices of a draw (display lists compile a draw into glArrayElement calls).
    pub unsafe fn draw_indices(
        &self,
        first: usize,
        count: usize,
        kind: u32,
        indices: usize,
    ) -> Result<Vec<usize>, u32> {
        self.indices_of(first, count, kind, indices)
    }

    /// The edge flags of the vertices of a draw when an edge flag array is enabled.
    pub unsafe fn edge_flags(
        &self,
        first: usize,
        count: usize,
        kind: u32,
        indices: usize,
    ) -> Result<Option<Vec<bool>>, u32> {
        if !self.enabled[7] {
            return Ok(None);
        }
        let mut out = Vec::new();
        for n in self.indices_of(first, count, kind, indices)? {
            out.push(self.edge_value(n)?);
        }
        Ok(Some(out))
    }

    unsafe fn edge_value(&self, index: usize) -> Result<bool, u32> {
        let a = self.edge_flag;
        let step = if a.stride == 0 { 1 } else { a.stride };
        let offset = index
            .checked_mul(step)
            .and_then(|n| n.checked_add(a.pointer))
            .ok_or(0x0502u32)?;
        Ok(self.source(a.buffer, offset, 1)?.read() != 0)
    }

    /// The point sizes of the vertices of a draw (drawing order), when a point size array is
    /// enabled: negative values count as 0.
    pub unsafe fn point_sizes(
        &self,
        first: usize,
        count: usize,
        kind: u32,
        indices: usize,
    ) -> Result<Option<Vec<f32>>, u32> {
        if !self.enabled[6] {
            return Ok(None);
        }
        let w = match kind {
            0 => 0,
            0x1401 => 1,
            0x1403 => 2,
            0x1405 => 4,
            _ => return Err(0x0500),
        };
        let source = if kind == 0 {
            ptr::null()
        } else {
            self.source(
                self.bound[1],
                indices,
                count.checked_mul(w).ok_or(0x0501u32)?,
            )?
        };
        let mut out = Vec::new();
        out.try_reserve_exact(count).map_err(|_| 0x0505u32)?;
        for i in 0..count {
            let n = match w {
                0 => first + i,
                1 => ptr::read(source.add(i)) as usize,
                2 => ptr::read_unaligned(source.add(i * 2).cast::<u16>()) as usize,
                _ => ptr::read_unaligned(source.add(i * 4).cast::<u32>()) as usize,
            };
            out.push(self.value(self.point_size, n, false)?[0].max(0.));
        }
        Ok(Some(out))
    }

    /// Client array state queries (size, type, stride, buffer binding, enable flags).
    pub fn query(&self, pname: u32) -> Option<f64> {
        let array = |a: &Array| a.size as f64;
        let tex = self.client_active.min(2);
        let unit = |u: usize| match u {
            0 => &self.texcoord,
            n => &self.extra_texcoords[n - 1],
        };
        Some(match pname {
            0x807a => array(&self.vertex),
            0x807b => self.vertex.kind as f64,
            0x807c => self.vertex.stride as f64,
            0x807e => self.normal.kind as f64,
            0x807f => self.normal.stride as f64,
            0x8081 => array(&self.color),
            0x8082 => self.color.kind as f64,
            0x8083 => self.color.stride as f64,
            0x8088 => array(unit(tex)),
            0x8089 => unit(tex).kind as f64,
            0x808a => unit(tex).stride as f64,
            0x84e1 => (0x84c0 + self.client_active) as f64,
            0x808c => self.edge_flag.stride as f64,
            0x889b => self.edge_flag.buffer as f64,
            0x898a => self.point_size.kind as f64,
            0x898b => self.point_size.stride as f64,
            0x8894 => self.bound[0] as f64,
            0x8895 => self.bound[1] as f64,
            0x8896 => self.vertex.buffer as f64,
            0x8897 => self.normal.buffer as f64,
            0x8898 => self.color.buffer as f64,
            0x889a => unit(tex).buffer as f64,
            0x8b9f => self.point_size.buffer as f64,
            _ => return None,
        })
    }

    /// Whether a client array is enabled (glIsEnabled, glGetBooleanv).
    pub fn is_enabled(&self, cap: u32) -> Option<bool> {
        Some(match cap {
            0x8074 => self.enabled[0],
            0x8076 => self.enabled[1],
            0x8078 => self.enabled[2 + self.client_active.min(2)],
            0x8075 => self.enabled[5],
            0x8b9c => self.enabled[6],
            0x8079 => self.enabled[7],
            _ => return None,
        })
    }

    /// The pointer of a client array (glGetPointerv).
    pub fn pointer(&self, pname: u32) -> Option<usize> {
        Some(match pname {
            0x808e => self.vertex.pointer,
            0x808f => self.normal.pointer,
            0x8090 => self.color.pointer,
            0x8092 => match self.client_active {
                0 => self.texcoord.pointer,
                n => self.extra_texcoords[n - 1].pointer,
            },
            0x8093 => self.edge_flag.pointer,
            0x898c => self.point_size.pointer,
            _ => return None,
        })
    }

    /// Unit 0 reads four texcoord components per vertex: q is in use.
    pub fn texcoord0_has_q(&self) -> bool {
        self.enabled[2] && self.texcoord.size == 4
    }

    unsafe fn source(&self, name: u32, address: usize, bytes: usize) -> Result<*const u8, u32> {
        if name == 0 {
            if address == 0 || address.checked_add(bytes).is_none() || bytes > isize::MAX as usize {
                return Err(0x0502);
            }

            Ok(address as *const u8)
        } else {
            let o = self
                .objects
                .iter()
                .find(|o| o.name == name)
                .ok_or(0x0502u32)?;

            if address > o.data.len() || bytes > o.data.len() - address {
                return Err(0x0502);
            }

            Ok(o.data.as_ptr().add(address))
        }
    }

    unsafe fn value(&self, a: Array, index: usize, color: bool) -> Result<[f32; 4], u32> {
        let width = width(a.kind).ok_or(0x0500u32)?;
        let step = if a.stride == 0 {
            a.size * width
        } else {
            a.stride
        };

        let offset = index
            .checked_mul(step)
            .and_then(|n| n.checked_add(a.pointer))
            .ok_or(0x0502u32)?;

        let source = self.source(a.buffer, offset, a.size * width)?;
        let mut out = if color { [1.; 4] } else { [0., 0., 0., 1.] };
        for (i, v) in out.iter_mut().enumerate().take(a.size) {
            let q = source.add(i * width);
            *v = match a.kind {
                0x1406 => ptr::read_unaligned(q.cast::<f32>()),
                0x140a => ptr::read_unaligned(q.cast::<f64>()) as f32,
                0x140c => ptr::read_unaligned(q.cast::<i32>()) as f32 * (1. / 65536.),
                0x1400 => ptr::read(q.cast::<i8>()) as f32,
                0x1401 => ptr::read(q) as f32,
                0x1402 => ptr::read_unaligned(q.cast::<i16>()) as f32,
                0x1403 => ptr::read_unaligned(q.cast::<u16>()) as f32,
                0x1404 => ptr::read_unaligned(q.cast::<i32>()) as f32,
                0x1405 => ptr::read_unaligned(q.cast::<u32>()) as f32,
                _ => return Err(0x0500),
            };

            if color {
                *v = match a.kind {
                    0x1400 => (*v / 127.).max(-1.),
                    0x1401 => *v / 255.,
                    0x1402 => (*v / 32767.).max(-1.),
                    0x1403 => *v / 65535.,
                    _ => *v,
                };
            }

            if !v.is_finite() {
                return Err(0x0501);
            }
        }

        Ok(out)
    }

    /// The vertices of a draw in drawing order (indices resolved), for CPU-side expansion.
    /// Normals are read in the same order (the current normal without a normal array).
    pub unsafe fn gather(
        &self,
        first: usize,
        count: usize,
        kind: u32,
        indices: usize,
        current_color: [f32; 4],
        current_texcoord: [f32; 4],
        extra_current: [[f32; 4]; 2],
        current_normal: [f32; 3],
    ) -> Result<(Vec<GpuVertex>, Vec<[f32; 3]>), u32> {
        if !self.enabled[0] {
            return Err(0x0502);
        }
        let w = match kind {
            0 => 0,
            0x1401 => 1,
            0x1403 => 2,
            0x1405 => 4,
            _ => return Err(0x0500),
        };
        if count > 4096 {
            return Err(0x0501);
        }
        let source = if kind == 0 {
            ptr::null()
        } else {
            self.source(
                self.bound[1],
                indices,
                count.checked_mul(w).ok_or(0x0501u32)?,
            )?
        };
        let mut out = Vec::new();
        let mut normals = Vec::new();
        out.try_reserve_exact(count).map_err(|_| 0x0505u32)?;
        normals.try_reserve_exact(count).map_err(|_| 0x0505u32)?;
        for i in 0..count {
            let n = match w {
                0 => first + i,
                1 => ptr::read(source.add(i)) as usize,
                2 => ptr::read_unaligned(source.add(i * 2).cast::<u16>()) as usize,
                _ => ptr::read_unaligned(source.add(i * 4).cast::<u32>()) as usize,
            };
            if kind != 0 && n >= 4096 {
                return Err(0x0501);
            }
            let mut extra = extra_current;
            for unit in 0..2 {
                if self.enabled[3 + unit] {
                    extra[unit] = self.value(self.extra_texcoords[unit], n, false)?;
                }
            }
            normals.push(if self.enabled[5] {
                self.normal_value(n)?
            } else {
                current_normal
            });
            out.push(GpuVertex {
                position: self.value(self.vertex, n, false)?,
                color: if self.enabled[1] {
                    self.value(self.color, n, true)?
                        .map(crate::immediate::gpu_color_component)
                } else {
                    current_color.map(crate::immediate::gpu_color_component)
                },
                texcoord: if self.enabled[2] {
                    self.value(self.texcoord, n, false)?
                } else {
                    current_texcoord
                },
                extra_texcoords: extra,
            });
        }
        Ok((out, normals))
    }

    /// Element `index` of the normal array: signed integers map to [-1, 1] like glColor.
    unsafe fn normal_value(&self, index: usize) -> Result<[f32; 3], u32> {
        let a = self.normal;
        let width = width(a.kind).ok_or(0x0500u32)?;
        let step = if a.stride == 0 { 3 * width } else { a.stride };
        let offset = index
            .checked_mul(step)
            .and_then(|n| n.checked_add(a.pointer))
            .ok_or(0x0502u32)?;
        let source = self.source(a.buffer, offset, 3 * width)?;
        let mut out = [0.; 3];
        for (i, v) in out.iter_mut().enumerate() {
            let q = source.add(i * width);
            *v = match a.kind {
                0x1406 => ptr::read_unaligned(q.cast::<f32>()),
                0x140a => ptr::read_unaligned(q.cast::<f64>()) as f32,
                0x140c => ptr::read_unaligned(q.cast::<i32>()) as f32 * (1. / 65536.),
                0x1400 => (2. * ptr::read(q.cast::<i8>()) as f32 + 1.) / 255.,
                0x1402 => (2. * ptr::read_unaligned(q.cast::<i16>()) as f32 + 1.) / 65535.,
                0x1404 => {
                    ((2. * ptr::read_unaligned(q.cast::<i32>()) as f64 + 1.) / 4294967295.) as f32
                }
                _ => return Err(0x0500),
            };
            if !v.is_finite() {
                return Err(0x0501);
            }
        }
        Ok(out)
    }

    pub unsafe fn prepare(
        &mut self,
        mode: u32,
        first: usize,
        count: usize,
        kind: u32,
        indices: usize,
        current_color: [f32; 4],
        current_texcoord: [f32; 4],
        extra_current: [[f32; 4]; 2],
    ) -> Result<(u32, Option<u32>, u32), u32> {
        if !self.enabled[0] {
            return Err(0x0502);
        }

        let plan = crate::primitive::Plan::new(mode, count)?;
        let count = plan.consumed;
        let key = Key {
            mode,
            vertex: self.vertex,
            color: self.enabled[1].then_some(self.color),
            current_color: if self.enabled[1] {
                [0; 4]
            } else {
                current_color.map(f32::to_bits)
            },

            texcoord: self.enabled[2].then_some(self.texcoord),
            current_texcoord: if self.enabled[2] {
                [0; 4]
            } else {
                current_texcoord.map(f32::to_bits)
            },
            extra_texcoords: core::array::from_fn(|i| {
                self.enabled[3 + i].then_some(self.extra_texcoords[i])
            }),
            extra_current: core::array::from_fn(|i| {
                if self.enabled[3 + i] {
                    [0; 4]
                } else {
                    extra_current[i].map(f32::to_bits)
                }
            }),
            first,
            count,
            kind,
            indices,
            element: if kind == 0 { 0 } else { self.bound[1] },
        };

        let persistent = key.vertex.buffer != 0
            && key.color.is_none_or(|a| a.buffer != 0)
            && key.texcoord.is_none_or(|a| a.buffer != 0)
            && key.extra_texcoords.iter().flatten().all(|a| a.buffer != 0)
            && (kind == 0 || key.element != 0);
        if persistent {
            if let Some(m) = self.cache.iter().find(|m| m.key == key) {
                return Ok((m.storage.physical(), m.index_offset, plan.output as u32));
            }
        }

        self.retired
            .try_reserve(self.cache.len() + 1)
            .map_err(|_| 0x0505u32)?;
        self.cache.try_reserve(1).map_err(|_| 0x0505u32)?;

        let mut index_data = Vec::<u16>::new();
        let vertex_count = if kind == 0 {
            count
        } else {
            let w = match kind {
                0x1401 => 1,
                0x1403 => 2,
                0x1405 => 4,
                _ => return Err(0x0500),
            };

            let source =
                self.source(key.element, indices, count.checked_mul(w).ok_or(0x0501u32)?)?;
            index_data.try_reserve_exact(count).map_err(|_| 0x0505u32)?;

            let mut max = 0;
            for i in 0..count {
                let n = match w {
                    1 => ptr::read(source.add(i)) as u32,
                    2 => ptr::read_unaligned(source.add(i * 2).cast::<u16>()) as u32,
                    _ => ptr::read_unaligned(source.add(i * 4).cast::<u32>()),
                };
                if n >= 4096 {
                    return Err(0x0501);
                }
                let n = n as u16;

                max = max.max(n as usize);
                index_data.push(n);
            }

            max + 1
        };

        if vertex_count > 4096 {
            return Err(0x0501);
        }

        if mode != 4 {
            index_data = plan.indices(|i| if kind == 0 { i as u16 } else { index_data[i] })?;
        }
        let offset = vertex_count * core::mem::size_of::<GpuVertex>();
        let storage = Buffer::new(offset + index_data.len() * 2, false).ok_or(0x0505u32)?;
        for i in 0..vertex_count {
            let mut extra = extra_current;
            for unit in 0..2 {
                if let Some(a) = key.extra_texcoords[unit] {
                    extra[unit] = self.value(a, if kind == 0 { first + i } else { i }, false)?;
                }
            }
            storage.pointer.cast::<GpuVertex>().add(i).write(GpuVertex {
                position: self.value(key.vertex, if kind == 0 { first + i } else { i }, false)?,
                color: if let Some(a) = key.color {
                    self.value(a, if kind == 0 { first + i } else { i }, true)?
                        .map(crate::immediate::gpu_color_component)
                } else {
                    current_color.map(crate::immediate::gpu_color_component)
                },
                extra_texcoords: extra,
                texcoord: if let Some(a) = key.texcoord {
                    self.value(a, if kind == 0 { first + i } else { i }, false)?
                } else {
                    current_texcoord
                },
            });
        }

        ptr::copy_nonoverlapping(
            index_data.as_ptr().cast::<u8>(),
            storage.pointer.add(offset),
            index_data.len() * 2,
        );

        if p::c3dglPlatformFlush(storage.pointer, (offset + index_data.len() * 2) as u32) < 0 {
            return Err(0x0502);
        }

        let physical = storage.physical();
        let index_offset = (kind != 0 || mode != 4).then_some(offset as u32 | 0x80000000);
        let mesh = Mesh {
            key,
            storage,
            index_offset,
        };

        if persistent {
            if self.cache.len() >= 64 {
                self.retired.push(self.cache.remove(0));
            }

            self.cache.push(mesh);
        } else {
            self.retired.push(mesh);
        }

        Ok((physical, index_offset, plan.output as u32))
    }
}

fn width(kind: u32) -> Option<usize> {
    match kind {
        0x1400 | 0x1401 => Some(1),
        0x1402 | 0x1403 => Some(2),
        0x1404 | 0x1405 | 0x1406 | 0x140c => Some(4),
        0x140a => Some(8),
        _ => None,
    }
}

fn target(t: u32) -> Result<usize, u32> {
    match t {
        0x8892 => Ok(0),
        0x8893 => Ok(1),
        _ => Err(0x0500),
    }
}

macro_rules! api {
    ($body:expr) => {
        with_renderer(|r| {
            if let Err(e) = ($body)(&mut r.arrays) {
                r.matrices.fail(e);
            }
        });
    };
}

#[no_mangle]
pub unsafe extern "C" fn glVertexPointer(
    size: i32,
    kind: u32,
    stride: i32,
    pointer: *const c_void,
) {
    pointer_set(false, size, kind, stride, pointer);
}

#[no_mangle]
pub unsafe extern "C" fn glColorPointer(size: i32, kind: u32, stride: i32, pointer: *const c_void) {
    pointer_set(true, size, kind, stride, pointer);
}

#[no_mangle]
pub unsafe extern "C" fn glNormalPointer(kind: u32, stride: i32, pointer: *const c_void) {
    api!(|a: &mut Arrays| {
        if !matches!(kind, 0x1400 | 0x1402 | 0x1404 | 0x1406 | 0x140a | 0x140c) {
            return Err(0x0500);
        }
        if stride < 0 {
            return Err(0x0501);
        }
        a.normal = Array {
            size: 3,
            kind,
            stride: stride as usize,
            pointer: pointer as usize,
            buffer: a.bound[0],
        };
        Ok(())
    });
}

#[no_mangle]
pub unsafe extern "C" fn glEdgeFlagPointer(stride: i32, pointer: *const c_void) {
    api!(|a: &mut Arrays| {
        if stride < 0 {
            return Err(0x0501);
        }
        a.edge_flag = Array {
            size: 1,
            kind: 0x1401,
            stride: stride as usize,
            pointer: pointer as usize,
            buffer: a.bound[0],
        };
        Ok(())
    });
}

/// GL_OES_point_size_array: one GL_FLOAT or GL_FIXED size per vertex.
#[no_mangle]
pub unsafe extern "C" fn glPointSizePointerOES(kind: u32, stride: i32, pointer: *const c_void) {
    api!(|a: &mut Arrays| {
        if !matches!(kind, 0x1406 | 0x140c) {
            return Err(0x0500);
        }
        if stride < 0 {
            return Err(0x0501);
        }
        a.point_size = Array {
            size: 1,
            kind,
            stride: stride as usize,
            pointer: pointer as usize,
            buffer: a.bound[0],
        };
        Ok(())
    });
}

unsafe fn pointer_set(color: bool, size: i32, kind: u32, stride: i32, pointer: *const c_void) {
    api!(|a: &mut Arrays| {
        if width(kind).is_none()
            || (!color && !matches!(kind, 0x1400 | 0x1402 | 0x1404 | 0x1406 | 0x140a | 0x140c))
        {
            return Err(0x0500);
        }

        if stride < 0 || !(if color { 3..=4 } else { 2..=4 }).contains(&size) {
            return Err(0x0501);
        }

        let v = Array {
            size: size as usize,
            kind,
            stride: stride as usize,
            pointer: pointer as usize,
            buffer: a.bound[0],
        };

        if color {
            a.color = v;
        } else {
            a.vertex = v;
        }

        Ok(())
    });
}

unsafe fn enable(state: u32, on: bool) {
    api!(|a: &mut Arrays| {
        let i = match state {
            0x8074 => 0,
            0x8076 => 1,
            0x8075 => 5,
            0x8078 => 2 + a.client_active,
            0x8b9c => 6,
            0x8079 => 7,
            _ => return Err(0x0500),
        };

        a.enabled[i] = on;

        Ok(())
    });
}

#[no_mangle]
pub unsafe extern "C" fn glEnableClientState(state: u32) {
    enable(state, true);
}

#[no_mangle]
pub unsafe extern "C" fn glDisableClientState(state: u32) {
    enable(state, false);
}

#[no_mangle]
pub unsafe extern "C" fn glGenBuffers(n: i32, out: *mut u32) {
    api!(|a: &mut Arrays| {
        if n < 0 {
            return Err(0x0501);
        }

        if n > 0 && out.is_null() {
            return Err(0x0501);
        }

        a.objects.try_reserve(n as usize).map_err(|_| 0x0505u32)?;
        for i in 0..n as usize {
            let name = a.next;
            a.next = a.next.checked_add(1).ok_or(0x0505u32)?;
            a.objects.push(Object {
                name,
                data: Vec::new(),
                usage: 0,
            });

            out.add(i).write(name);
        }

        Ok(())
    });
}

#[no_mangle]
pub unsafe extern "C" fn glIsBuffer(name: u32) -> u8 {
    let mut value = 0;
    with_renderer(|r| value = (name != 0 && r.arrays.objects.iter().any(|o| o.name == name)) as u8);
    value
}

#[no_mangle]
pub unsafe extern "C" fn glGetBufferParameteriv(t: u32, pname: u32, out: *mut i32) {
    api!(|a: &mut Arrays| {
        let name = a.bound[target(t)?];
        if name == 0 {
            return Err(0x0502);
        }
        let o = a.objects.iter().find(|o| o.name == name).ok_or(0x0502u32)?;
        if out.is_null() {
            return Err(0x0501);
        }
        out.write(match pname {
            0x8764 => o.data.len() as i32,
            0x8765 => o.usage as i32,
            _ => return Err(0x0500),
        });
        Ok(())
    });
}

#[no_mangle]
pub unsafe extern "C" fn glBindBuffer(t: u32, name: u32) {
    api!(|a: &mut Arrays| {
        let i = target(t)?;
        if name != 0 && !a.objects.iter().any(|o| o.name == name) {
            a.objects.try_reserve(1).map_err(|_| 0x0505u32)?;
            a.objects.push(Object {
                name,
                data: Vec::new(),
                usage: 0,
            });

            a.next = a.next.max(name.checked_add(1).ok_or(0x0505u32)?);
        }

        a.bound[i] = name;

        Ok(())
    });
}

#[no_mangle]
pub unsafe extern "C" fn glBufferData(t: u32, size: isize, data: *const c_void, usage: u32) {
    api!(|a: &mut Arrays| {
        let name = a.bound[target(t)?];
        if !matches!(usage, 0x88e0 | 0x88e4 | 0x88e8) {
            return Err(0x0500);
        }

        if size < 0 || size as usize > u32::MAX as usize {
            return Err(0x0501);
        }

        let o = a
            .objects
            .iter_mut()
            .find(|o| o.name == name)
            .ok_or(0x0502u32)?;

        let mut bytes = Vec::new();
        bytes
            .try_reserve_exact(size as usize)
            .map_err(|_| 0x0505u32)?;
        bytes.resize(size as usize, 0);

        if !data.is_null() {
            ptr::copy_nonoverlapping(data.cast::<u8>(), bytes.as_mut_ptr(), size as usize);
        }

        o.data = bytes;
        o.usage = usage;
        a.invalidate(name);

        Ok(())
    });
}

#[no_mangle]
pub unsafe extern "C" fn glBufferSubData(t: u32, offset: isize, size: isize, data: *const c_void) {
    api!(|a: &mut Arrays| {
        let name = a.bound[target(t)?];
        if offset < 0 || size < 0 || (size > 0 && data.is_null()) {
            return Err(0x0501);
        }

        let o = a
            .objects
            .iter_mut()
            .find(|o| o.name == name)
            .ok_or(0x0502u32)?;

        let off = offset as usize;
        let len = size as usize;

        if off > o.data.len() || len > o.data.len() - off {
            return Err(0x0501);
        }

        if len > 0 {
            ptr::copy_nonoverlapping(data.cast::<u8>(), o.data.as_mut_ptr().add(off), len);
            a.invalidate(name);
        }

        Ok(())
    });
}

#[no_mangle]
pub unsafe extern "C" fn glDeleteBuffers(n: i32, names: *const u32) {
    api!(|a: &mut Arrays| {
        if n < 0 || (n > 0 && names.is_null()) {
            return Err(0x0501);
        }

        for i in 0..n as usize {
            let name = names.add(i).read();
            if name == 0 {
                continue;
            }

            a.invalidate(name);
            a.objects.retain(|o| o.name != name);

            for b in &mut a.bound {
                if *b == name {
                    *b = 0;
                }
            }

            if a.vertex.buffer == name {
                a.vertex = Array::default();
            }

            if a.color.buffer == name {
                a.color = Array::default();
            }

            for coord in &mut a.extra_texcoords {
                if coord.buffer == name {
                    *coord = Array::default();
                }
            }
            if a.texcoord.buffer == name {
                a.texcoord = Array::default();
            }
        }

        Ok(())
    });
}

/// glArrayElement while a display list is compiled: the element is dereferenced now and recorded
/// as the glMultiTexCoord, glColor, glNormal, glEdgeFlag and glVertex calls it stands for.
pub(crate) unsafe fn array_element_list(index: usize) {
    use crate::immediate::*;
    let mut element = None;
    with_renderer(|r| match r.arrays.element(index) {
        Ok(e) => element = Some(e),
        Err(e) => r.matrices.fail(e),
    });
    let Some(e) = element else {
        return;
    };
    for unit in 0..3 {
        if let Some(t) = e.texcoords[unit] {
            glMultiTexCoord4f(0x84c0 + unit as u32, t[0], t[1], t[2], t[3]);
        }
    }
    if let Some(c) = e.color {
        glColor4f(c[0], c[1], c[2], c[3]);
    }
    if let Some(n) = e.normal {
        glNormal3f(n[0], n[1], n[2]);
    }
    if let Some(f) = e.edge {
        glEdgeFlag(f as u8);
    }
    if let Some(p) = e.position {
        glVertex4f(p[0], p[1], p[2], p[3]);
    }
}

/// glDrawArrays / glDrawElements while compiling: glBegin, glArrayElement per vertex, glEnd.
pub(crate) unsafe fn draw_list(mode: u32, first: i32, count: i32, kind: u32, indices: usize) {
    let mut result = None;
    with_renderer(|r| {
        if first < 0 || count < 0 {
            r.matrices.fail(0x0501);
            return;
        }
        if !r.arrays.vertex_enabled() {
            return;
        }
        match r
            .arrays
            .draw_indices(first as usize, count as usize, kind, indices)
        {
            Ok(v) => result = Some(v),
            Err(e) => r.matrices.fail(e),
        }
    });
    let Some(list) = result else {
        return;
    };
    crate::immediate::glBegin(mode);
    for n in list {
        array_element_list(n);
    }
    crate::immediate::glEnd();
}

/// glArrayElement: the vertex of the enabled arrays inside glBegin/glEnd. Attributes without an
/// array keep the current values; only the normal of the array becomes the current one, and
/// without a vertex array only the current values change.
#[no_mangle]
pub unsafe extern "C" fn glArrayElement(index: i32) {
    if crate::lists::compiling() {
        if index >= 0 {
            array_element_list(index as usize);
        } else {
            with_renderer(|r| r.matrices.fail(0x0501));
        }
        return;
    }
    crate::renderer::array_element(index);
}

/// glInterleavedArrays formats: texcoord size, color size, normal, vertex size, color type and
/// the byte offsets of color, normal and vertex, and the default stride.
struct Interleaved {
    format: u32,
    tex: i32,
    color: i32,
    normal: bool,
    vertex: i32,
    color_type: u32,
    offsets: [usize; 3],
    stride: usize,
}

const F: usize = 4;
const C: usize = 4;
const UB: u32 = 0x1401;
const FL: u32 = 0x1406;
#[rustfmt::skip]
const INTERLEAVED: [Interleaved; 14] = [
    Interleaved { format: 0x2a20, tex: 0, color: 0, normal: false, vertex: 2, color_type: 0, offsets: [0, 0, 0], stride: 2 * F },
    Interleaved { format: 0x2a21, tex: 0, color: 0, normal: false, vertex: 3, color_type: 0, offsets: [0, 0, 0], stride: 3 * F },
    Interleaved { format: 0x2a22, tex: 0, color: 4, normal: false, vertex: 2, color_type: UB, offsets: [0, 0, C], stride: C + 2 * F },
    Interleaved { format: 0x2a23, tex: 0, color: 4, normal: false, vertex: 3, color_type: UB, offsets: [0, 0, C], stride: C + 3 * F },
    Interleaved { format: 0x2a24, tex: 0, color: 3, normal: false, vertex: 3, color_type: FL, offsets: [0, 0, 3 * F], stride: 6 * F },
    Interleaved { format: 0x2a25, tex: 0, color: 0, normal: true, vertex: 3, color_type: 0, offsets: [0, 0, 3 * F], stride: 6 * F },
    Interleaved { format: 0x2a26, tex: 0, color: 4, normal: true, vertex: 3, color_type: FL, offsets: [0, 4 * F, 7 * F], stride: 10 * F },
    Interleaved { format: 0x2a27, tex: 2, color: 0, normal: false, vertex: 3, color_type: 0, offsets: [0, 0, 2 * F], stride: 5 * F },
    Interleaved { format: 0x2a28, tex: 4, color: 0, normal: false, vertex: 4, color_type: 0, offsets: [0, 0, 4 * F], stride: 8 * F },
    Interleaved { format: 0x2a29, tex: 2, color: 4, normal: false, vertex: 3, color_type: UB, offsets: [2 * F, 0, C + 2 * F], stride: C + 5 * F },
    Interleaved { format: 0x2a2a, tex: 2, color: 3, normal: false, vertex: 3, color_type: FL, offsets: [2 * F, 0, 5 * F], stride: 8 * F },
    Interleaved { format: 0x2a2b, tex: 2, color: 0, normal: true, vertex: 3, color_type: 0, offsets: [0, 2 * F, 5 * F], stride: 8 * F },
    Interleaved { format: 0x2a2c, tex: 2, color: 4, normal: true, vertex: 3, color_type: FL, offsets: [2 * F, 6 * F, 9 * F], stride: 12 * F },
    Interleaved { format: 0x2a2d, tex: 4, color: 4, normal: true, vertex: 4, color_type: FL, offsets: [4 * F, 8 * F, 11 * F], stride: 15 * F },
];

#[no_mangle]
pub unsafe extern "C" fn glInterleavedArrays(format: u32, stride: i32, pointer: *const c_void) {
    let Some(f) = INTERLEAVED.iter().find(|f| f.format == format) else {
        return with_renderer(|r| r.matrices.fail(0x0500));
    };
    if stride < 0 {
        return with_renderer(|r| r.matrices.fail(0x0501));
    }
    let stride = if stride == 0 { f.stride as i32 } else { stride };
    let base = pointer as *const u8;
    with_renderer(|r| {
        let a = &mut r.arrays;
        a.enabled[7] = false;
        a.enabled[2 + a.client_active.min(2)] = f.tex > 0;
        a.enabled[1] = f.color > 0;
        a.enabled[5] = f.normal;
        a.enabled[0] = true;
    });
    if f.tex > 0 {
        glTexCoordPointer(f.tex, 0x1406, stride, base.cast());
    }
    if f.color > 0 {
        glColorPointer(f.color, f.color_type, stride, base.add(f.offsets[0]).cast());
    }
    if f.normal {
        glNormalPointer(0x1406, stride, base.add(f.offsets[1]).cast());
    }
    glVertexPointer(f.vertex, 0x1406, stride, base.add(f.offsets[2]).cast());
}

#[no_mangle]
pub unsafe extern "C" fn glDrawArrays(mode: u32, first: i32, count: i32) {
    if crate::lists::compiling() {
        return draw_list(mode, first, count, 0, 0);
    }
    crate::renderer::draw_arrays(mode, first, count, 0, 0);
}

#[no_mangle]
pub unsafe extern "C" fn glDrawElements(mode: u32, count: i32, kind: u32, indices: *const c_void) {
    if !matches!(kind, 0x1401 | 0x1403 | 0x1405) {
        crate::renderer::with_matrices(|m| m.fail(0x0500));

        return;
    }

    if crate::lists::compiling() {
        return draw_list(mode, 0, count, kind, indices as usize);
    }
    crate::renderer::draw_arrays(mode, 0, count, kind, indices as usize);
}

#[no_mangle]
pub unsafe extern "C" fn glClientActiveTexture(unit: u32) {
    api!(|a: &mut Arrays| {
        if !(0x84c0..=0x84c2).contains(&unit) {
            return Err(0x0500);
        }
        a.client_active = (unit - 0x84c0) as usize;
        Ok(())
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn texcoord_dimensions_fixed_double_and_unaligned_stride() {
        let a = Arrays::new();
        unsafe {
            let bytes = [-2i8, 3];
            let mut array = Array {
                size: 1,
                kind: 0x1400,
                stride: 0,
                pointer: bytes.as_ptr() as usize,
                buffer: 0,
            };
            assert_eq!(a.value(array, 1, false).unwrap(), [3., 0., 0., 1.]);
            let fixed = [65536i32, -32768, 0, 131072];
            array = Array {
                size: 4,
                kind: 0x140c,
                stride: 0,
                pointer: fixed.as_ptr() as usize,
                buffer: 0,
            };
            assert_eq!(a.value(array, 0, false).unwrap(), [1., -0.5, 0., 2.]);
            let values = [0.25f64, 0.75, -1., 2.];
            let mut storage = [0u8; 33];
            ptr::copy_nonoverlapping(
                values.as_ptr().cast::<u8>(),
                storage.as_mut_ptr().add(1),
                32,
            );
            array = Array {
                size: 2,
                kind: 0x140a,
                stride: 16,
                pointer: storage.as_ptr().add(1) as usize,
                buffer: 0,
            };
            assert_eq!(a.value(array, 1, false).unwrap(), [-1., 2., 0., 1.]);
        }
    }

    #[test]
    fn formats_stride_and_buffer_bounds() {
        let mut a = Arrays::new();
        a.objects.push(Object {
            name: 7,
            data: alloc::vec![99, 99, 255, 128, 0, 64, 255, 255],
            usage: 0,
        });
        let color = Array {
            size: 3,
            kind: 0x1401,
            stride: 4,
            pointer: 2,
            buffer: 7,
        };
        unsafe {
            assert_eq!(a.value(color, 0, true).unwrap(), [1., 128. / 255., 0., 1.]);
            assert!(a.value(color, 1, true).is_err());
            assert!(a.source(7, usize::MAX, 4).is_err());
            let positions = [10i16, 20, 30, 99, 40, 50, 60, 99];
            let vertex = Array {
                size: 3,
                kind: 0x1402,
                stride: 8,
                pointer: positions.as_ptr() as usize,
                buffer: 0,
            };
            assert_eq!(a.value(vertex, 1, false).unwrap(), [40., 50., 60., 1.]);
            let signed = [-128i8, 0, 127];
            let color = Array {
                size: 3,
                kind: 0x1400,
                stride: 0,
                pointer: signed.as_ptr() as usize,
                buffer: 0,
            };
            assert_eq!(a.value(color, 0, true).unwrap(), [-1., 0., 1., 1.]);
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn glTexCoordPointer(
    size: i32,
    kind: u32,
    stride: i32,
    pointer: *const c_void,
) {
    api!(|a: &mut Arrays| {
        if !matches!(kind, 0x1400 | 0x1402 | 0x1406 | 0x140a | 0x140c) {
            return Err(0x0500);
        }

        if !(1..=4).contains(&size) || stride < 0 {
            return Err(0x0501);
        }

        let coordinate = Array {
            size: size as usize,
            kind,
            stride: stride as usize,
            pointer: pointer as usize,
            buffer: a.bound[0],
        };

        if a.client_active == 0 {
            a.texcoord = coordinate;
        } else {
            a.extra_texcoords[a.client_active - 1] = coordinate;
        }
        Ok(())
    });
}

#[cfg(test)]
pub(crate) unsafe fn uv_lifecycle() {
    use crate::matrix::glGetError;
    let mut name = 0;
    glGenBuffers(1, &mut name);
    glBindBuffer(0x8892, name);
    let coords: [[f32; 2]; 3] = [[0., 0.], [1., 0.], [0.5, 1.]];
    glBufferData(0x8892, 24, coords.as_ptr().cast(), 0x88e4);
    glTexCoordPointer(2, 0x1406, 8, ptr::null());
    glEnableClientState(0x8078);
    glDrawElements(4, 3, 0x1401, ptr::null());
    let mut old = ptr::null_mut();
    with_renderer(|r| {
        let m = r
            .arrays
            .cache
            .iter()
            .find(|m| m.key.texcoord.is_some_and(|a| a.buffer == name))
            .unwrap();
        old = m.storage.pointer;
        let v = core::slice::from_raw_parts(old.cast::<GpuVertex>(), 3);
        assert_eq!(v[1].texcoord, [1., 0., 0., 1.]);
        assert_eq!(v[2].texcoord, [0.5, 1., 0., 1.]);
    });
    glBindBuffer(0x8892, 0);
    glDrawElements(4, 3, 0x1401, ptr::null());
    with_renderer(|r| {
        assert_eq!(
            r.arrays
                .cache
                .iter()
                .find(|m| m.key.texcoord.is_some_and(|a| a.buffer == name))
                .unwrap()
                .storage
                .pointer,
            old
        )
    });
    let patch = [0.25f32, 0.75];
    glBindBuffer(0x8892, name);
    glBufferSubData(0x8892, 0, 8, patch.as_ptr().cast());
    glDrawElements(4, 3, 0x1401, ptr::null());
    with_renderer(|r| {
        let m = r
            .arrays
            .cache
            .iter()
            .find(|m| m.key.texcoord.is_some_and(|a| a.buffer == name))
            .unwrap();
        assert_ne!(m.storage.pointer, old);
        assert!(r.arrays.retired.iter().any(|m| m.storage.pointer == old));
        assert_eq!(
            (*m.storage.pointer.cast::<GpuVertex>()).texcoord,
            [0.25, 0.75, 0., 1.]
        );
    });
    glDeleteBuffers(1, &name);
    glDisableClientState(0x8078);
    assert_eq!(glGetError(), 0);
}

#[cfg(test)]
pub(crate) unsafe fn topology_lifecycle() {
    use crate::matrix::glGetError;
    let positions: [[f32; 3]; 5] = [
        [99.; 3],
        [-1., -1., 0.],
        [1., -1., 0.],
        [1., 1., 0.],
        [-1., 1., 0.],
    ];
    let mut names = [0; 2];
    glGenBuffers(2, names.as_mut_ptr());
    glBindBuffer(0x8892, names[0]);
    glBufferData(0x8892, 60, positions.as_ptr().cast(), 0x88e4);
    glVertexPointer(3, 0x1406, 12, ptr::null());
    glEnableClientState(0x8074);
    glDisableClientState(0x8076);
    glDisableClientState(0x8078);
    glDrawArrays(7, 1, 4);
    let mut old = ptr::null_mut();
    with_renderer(|r| {
        let m = r
            .arrays
            .cache
            .iter()
            .find(|m| m.key.vertex.buffer == names[0] && m.key.mode == 7 && m.key.kind == 0)
            .unwrap();
        old = m.storage.pointer;
        assert_eq!((*old.cast::<GpuVertex>()).position, [-1., -1., 0., 1.]);
        assert_eq!(
            core::slice::from_raw_parts(
                old.add(4 * core::mem::size_of::<GpuVertex>()).cast::<u16>(),
                6
            ),
            &[0, 1, 2, 0, 2, 3]
        );
    });
    // The fifth input is incomplete; no access beyond the VBO and no new upload.
    glDrawArrays(7, 1, 5);
    with_renderer(|r| {
        assert_eq!(
            r.arrays
                .cache
                .iter()
                .find(|m| m.key.vertex.buffer == names[0] && m.key.mode == 7 && m.key.kind == 0)
                .unwrap()
                .storage
                .pointer,
            old
        )
    });
    glDrawArrays(5, 1, 4);
    with_renderer(|r| {
        let m = r
            .arrays
            .cache
            .iter()
            .find(|m| m.key.vertex.buffer == names[0] && m.key.mode == 5)
            .unwrap();
        assert_ne!(m.storage.pointer, old);
        assert_eq!(
            core::slice::from_raw_parts(
                m.storage
                    .pointer
                    .add(4 * core::mem::size_of::<GpuVertex>())
                    .cast::<u16>(),
                6
            ),
            &[0, 1, 2, 2, 1, 3]
        );
    });
    let indices = [1u32, 2, 3, 4];
    glBindBuffer(0x8893, names[1]);
    glBufferData(0x8893, 16, indices.as_ptr().cast(), 0x88e4);
    glDrawElements(7, 4, 0x1405, ptr::null());
    let mut indexed = ptr::null_mut();
    with_renderer(|r| {
        let m = r
            .arrays
            .cache
            .iter()
            .find(|m| m.key.vertex.buffer == names[0] && m.key.kind == 0x1405)
            .unwrap();
        indexed = m.storage.pointer;
        assert_eq!(
            m.index_offset,
            Some((5 * core::mem::size_of::<GpuVertex>()) as u32 | 0x80000000)
        );
        assert_eq!(
            core::slice::from_raw_parts(
                indexed
                    .add(5 * core::mem::size_of::<GpuVertex>())
                    .cast::<u16>(),
                6
            ),
            &[1, 2, 3, 1, 3, 4]
        );
    });
    let patch = [4u32, 3, 2, 1];
    glBufferSubData(0x8893, 0, 16, patch.as_ptr().cast());
    glDrawElements(7, 4, 0x1405, ptr::null());
    with_renderer(|r| {
        let m = r
            .arrays
            .cache
            .iter()
            .find(|m| m.key.vertex.buffer == names[0] && m.key.kind == 0x1405)
            .unwrap();
        assert_ne!(m.storage.pointer, indexed);
        assert_eq!(
            core::slice::from_raw_parts(
                m.storage
                    .pointer
                    .add(5 * core::mem::size_of::<GpuVertex>())
                    .cast::<u16>(),
                6
            ),
            &[4, 3, 2, 4, 2, 1]
        );
        assert!(r
            .arrays
            .retired
            .iter()
            .any(|m| m.storage.pointer == indexed));
    });
    let invalid = u32::MAX;
    glBufferSubData(0x8893, 0, 4, (&invalid as *const u32).cast());
    glDrawElements(7, 4, 0x1405, ptr::null());
    assert_eq!(glGetError(), 0x0501);
    glDeleteBuffers(2, names.as_ptr());
    glDisableClientState(0x8074);
    assert_eq!(glGetError(), 0);
}
