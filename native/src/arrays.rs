//! Client arrays and buffer-backed geometry. Cache storage survives submitted work.
use crate::platform::gpu as p;
use crate::renderer::{with_renderer, Buffer, Vertex};
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
    vertex: Array,
    color: Option<Array>,
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
    enabled: [bool; 2],
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
            enabled: [false; 2],
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
        first: usize,
        count: usize,
        kind: u32,
        indices: usize,
    ) -> Result<(u32, Option<u32>), u32> {
        if !self.enabled[0] {
            return Err(0x0502);
        }

        let key = Key {
            vertex: self.vertex,
            color: self.enabled[1].then_some(self.color),
            first,
            count,
            kind,
            indices,
            element: if kind == 0 { 0 } else { self.bound[1] },
        };

        let persistent = key.vertex.buffer != 0
            && key.color.is_none_or(|a| a.buffer != 0)
            && (kind == 0 || key.element != 0);
        if persistent {
            if let Some(m) = self.cache.iter().find(|m| m.key == key) {
                return Ok((m.storage.physical(), m.index_offset));
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
                _ => return Err(0x0500),
            };

            let source =
                self.source(key.element, indices, count.checked_mul(w).ok_or(0x0501u32)?)?;
            index_data.try_reserve_exact(count).map_err(|_| 0x0505u32)?;

            let mut max = 0;
            for i in 0..count {
                let n = if w == 1 {
                    ptr::read(source.add(i)) as u16
                } else {
                    ptr::read_unaligned(source.add(i * 2).cast::<u16>())
                };

                max = max.max(n as usize);
                index_data.push(n);
            }

            max + 1
        };

        if vertex_count > 4096 {
            return Err(0x0501);
        }

        let offset = vertex_count * 32;
        let storage = Buffer::new(offset + index_data.len() * 2, false).ok_or(0x0505u32)?;
        for i in 0..vertex_count {
            storage.pointer.cast::<Vertex>().add(i).write(Vertex {
                position: self.value(key.vertex, if kind == 0 { first + i } else { i }, false)?,
                color: if let Some(a) = key.color {
                    self.value(a, if kind == 0 { first + i } else { i }, true)?
                } else {
                    [1.; 4]
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
        let index_offset = (kind != 0).then_some(offset as u32 | 0x80000000);
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

        Ok((physical, index_offset))
    }
}

fn width(kind: u32) -> Option<usize> {
    match kind {
        0x1400 | 0x1401 => Some(1),
        0x1402 | 0x1403 => Some(2),
        0x1406 => Some(4),
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
    if !matches!(kind, 0x1401 | 0x1403) {
        crate::renderer::with_matrices(|m| m.fail(0x0500));

        return;
    }

    crate::renderer::draw_arrays(mode, 0, count, kind, indices as usize);
}

#[cfg(test)]
mod tests {
    use super::*;
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
