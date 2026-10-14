//! Client arrays and buffer-backed geometry. Cache storage survives submitted work.
use crate::platform::gpu as p;
use crate::renderer::{with_renderer, Buffer, GpuVertex};
use alloc::vec::Vec;
use core::{ffi::c_void, ptr};

#[derive(Clone, Copy, PartialEq, Eq, Default)]
struct Array {
    size: usize,
    kind: u32,
    stride: usize,
    pointer: usize,
    buffer: u32,
}

struct Object {
    name: u32,
    data: Vec<u8>,
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

pub(crate) struct Arrays {
    vertex: Array,
    color: Array,
    texcoord: Array,
    extra_texcoords: [Array; 2],
    pub client_active: usize,
    enabled: [bool; 5],
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
            client_active: 0,
            enabled: [false; 5],
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
        0x1406 | 0x140c => Some(4),
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

unsafe fn pointer_set(color: bool, size: i32, kind: u32, stride: i32, pointer: *const c_void) {
    api!(|a: &mut Arrays| {
        if width(kind).is_none() || (!color && !matches!(kind, 0x1402 | 0x1406)) {
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
            0x8078 => 2 + a.client_active,
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
            });

            out.add(i).write(name);
        }

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

#[no_mangle]
pub unsafe extern "C" fn glDrawArrays(mode: u32, first: i32, count: i32) {
    crate::renderer::draw_arrays(mode, first, count, 0, 0);
}

#[no_mangle]
pub unsafe extern "C" fn glDrawElements(mode: u32, count: i32, kind: u32, indices: *const c_void) {
    if !matches!(kind, 0x1401 | 0x1403 | 0x1405) {
        crate::renderer::with_matrices(|m| m.fail(0x0500));

        return;
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
