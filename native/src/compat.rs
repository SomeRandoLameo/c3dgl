//! OpenGL ES 1.1 float and fixed-point (16.16) API on top of the GL functions, glMultiTexCoord
//! variants, glTexParameter/glTexEnv variants, glGetPointerv and glGetFixedv. Enum-valued
//! parameters are passed unscaled in the x functions, like in ES.
use crate::renderer::with_frame;
use core::ffi::c_void;

extern "C" {
    fn lroundf(x: f32) -> i32;
}

fn to_float(x: i32) -> f32 {
    x as f32 / 65536.
}

fn to_fixed(f: f64) -> i32 {
    let v = f * 65536.;
    if v >= 2147483647. {
        i32::MAX
    } else if v <= -2147483648. {
        i32::MIN
    } else {
        // Round half away from zero.
        if v >= 0. {
            (v + 0.5) as i32
        } else {
            -((-v + 0.5) as i32)
        }
    }
}

fn param_is_enum(pname: u32) -> bool {
    matches!(
        pname,
        0x2200 | 0x2801 | 0x2800 | 0x2802 | 0x2803 | 0x8191 | 0x8862 | 0x8571..=0x8572 | 0x8580..=0x858a
            | 0x8590..=0x859a
    ) && !matches!(pname, 0x8583..=0x858f)
}

fn fail(code: u32) {
    unsafe { with_frame(|_, m| m.fail(code)) };
}

// ---- float variants of the double functions -------------------------------------------------
#[no_mangle]
pub unsafe extern "C" fn glOrthof(l: f32, r: f32, b: f32, t: f32, n: f32, f: f32) {
    crate::matrix::glOrtho(l as f64, r as f64, b as f64, t as f64, n as f64, f as f64);
}
#[no_mangle]
pub unsafe extern "C" fn glFrustumf(l: f32, r: f32, b: f32, t: f32, n: f32, f: f32) {
    crate::matrix::glFrustum(l as f64, r as f64, b as f64, t as f64, n as f64, f as f64);
}
#[no_mangle]
pub unsafe extern "C" fn glAlphaFuncx(func: u32, reference: i32) {
    crate::raster::glAlphaFunc(func, to_float(reference));
}
#[no_mangle]
pub unsafe extern "C" fn glClearColorx(r: i32, g: i32, b: i32, a: i32) {
    crate::frame::glClearColor(to_float(r), to_float(g), to_float(b), to_float(a));
}
#[no_mangle]
pub unsafe extern "C" fn glClearDepthx(d: i32) {
    crate::frame::glClearDepth(to_float(d) as f64);
}
#[no_mangle]
pub unsafe extern "C" fn glColor4x(r: i32, g: i32, b: i32, a: i32) {
    crate::immediate::glColor4f(to_float(r), to_float(g), to_float(b), to_float(a));
}
#[no_mangle]
pub unsafe extern "C" fn glDepthRangex(n: i32, f: i32) {
    crate::raster::glDepthRange(to_float(n) as f64, to_float(f) as f64);
}
#[no_mangle]
pub unsafe extern "C" fn glLineWidthx(w: i32) {
    crate::frame::glLineWidth(to_float(w));
}
#[no_mangle]
pub unsafe extern "C" fn glNormal3x(x: i32, y: i32, z: i32) {
    crate::immediate::glNormal3f(to_float(x), to_float(y), to_float(z));
}
#[no_mangle]
pub unsafe extern "C" fn glPointSizex(s: i32) {
    crate::frame::glPointSize(to_float(s));
}
#[no_mangle]
pub unsafe extern "C" fn glPointParameterx(pname: u32, param: i32) {
    crate::points::glPointParameterf(pname, to_float(param));
}
#[no_mangle]
pub unsafe extern "C" fn glPointParameterxv(pname: u32, params: *const i32) {
    if params.is_null() {
        return fail(0x0501);
    }
    let n = if pname == 0x8129 { 3 } else { 1 };
    let f: [f32; 3] = core::array::from_fn(|i| {
        if i < n {
            to_float(params.add(i).read_unaligned())
        } else {
            0.
        }
    });
    crate::points::glPointParameterfv(pname, f.as_ptr());
}
#[no_mangle]
pub unsafe extern "C" fn glPolygonOffsetx(factor: i32, units: i32) {
    crate::frame::glPolygonOffset(to_float(factor), to_float(units));
}
#[no_mangle]
pub unsafe extern "C" fn glRotatex(a: i32, x: i32, y: i32, z: i32) {
    crate::matrix::glRotatef(to_float(a), to_float(x), to_float(y), to_float(z));
}
#[no_mangle]
pub unsafe extern "C" fn glScalex(x: i32, y: i32, z: i32) {
    crate::matrix::glScalef(to_float(x), to_float(y), to_float(z));
}
#[no_mangle]
pub unsafe extern "C" fn glTranslatex(x: i32, y: i32, z: i32) {
    crate::matrix::glTranslatef(to_float(x), to_float(y), to_float(z));
}
#[no_mangle]
pub unsafe extern "C" fn glOrthox(l: i32, r: i32, b: i32, t: i32, n: i32, f: i32) {
    crate::matrix::glOrtho(
        to_float(l) as f64,
        to_float(r) as f64,
        to_float(b) as f64,
        to_float(t) as f64,
        to_float(n) as f64,
        to_float(f) as f64,
    );
}
#[no_mangle]
pub unsafe extern "C" fn glFrustumx(l: i32, r: i32, b: i32, t: i32, n: i32, f: i32) {
    crate::matrix::glFrustum(
        to_float(l) as f64,
        to_float(r) as f64,
        to_float(b) as f64,
        to_float(t) as f64,
        to_float(n) as f64,
        to_float(f) as f64,
    );
}
#[no_mangle]
pub unsafe extern "C" fn glLoadMatrixx(m: *const i32) {
    if m.is_null() {
        return fail(0x0501);
    }
    let f: [f32; 16] = core::array::from_fn(|i| to_float(m.add(i).read_unaligned()));
    crate::matrix::glLoadMatrixf(f.as_ptr());
}
#[no_mangle]
pub unsafe extern "C" fn glMultMatrixx(m: *const i32) {
    if m.is_null() {
        return fail(0x0501);
    }
    let f: [f32; 16] = core::array::from_fn(|i| to_float(m.add(i).read_unaligned()));
    crate::matrix::glMultMatrixf(f.as_ptr());
}
#[no_mangle]
pub unsafe extern "C" fn glMultiTexCoord4x(target: u32, s: i32, t: i32, r: i32, q: i32) {
    crate::immediate::glMultiTexCoord4f(target, to_float(s), to_float(t), to_float(r), to_float(q));
}
#[no_mangle]
pub unsafe extern "C" fn glSampleCoveragex(value: i32, invert: u8) {
    glSampleCoverage(to_float(value), invert);
}
#[no_mangle]
pub unsafe extern "C" fn glFogx(pname: u32, param: i32) {
    if pname == 0x0b66 {
        return fail(0x0500);
    }
    // The mode is an enum, passed unscaled.
    glFogf_(
        pname,
        if pname == 0x0b65 {
            param as f32
        } else {
            to_float(param)
        },
    );
}
unsafe fn glFogf_(pname: u32, v: f32) {
    crate::fog::glFogf(pname, v);
}
#[no_mangle]
pub unsafe extern "C" fn glFogxv(pname: u32, params: *const i32) {
    if params.is_null() {
        return fail(0x0501);
    }
    let f: [f32; 4] = if pname == 0x0b66 {
        core::array::from_fn(|i| to_float(params.add(i).read_unaligned()))
    } else {
        let p = params.read_unaligned();
        [
            if pname == 0x0b65 {
                p as f32
            } else {
                to_float(p)
            },
            0.,
            0.,
            0.,
        ]
    };
    crate::fog::glFogfv(pname, f.as_ptr());
}
#[no_mangle]
pub unsafe extern "C" fn glClipPlanex(plane: u32, equation: *const i32) {
    if equation.is_null() {
        return fail(0x0501);
    }
    let e: [f64; 4] = core::array::from_fn(|i| equation.add(i).read_unaligned() as f64 / 65536.);
    crate::clip::glClipPlane(plane, e.as_ptr());
}
#[no_mangle]
pub unsafe extern "C" fn glGetClipPlanex(plane: u32, equation: *mut i32) {
    if equation.is_null() {
        return fail(0x0501);
    }
    let mut e = [0f64; 4];
    crate::clip::glGetClipPlane(plane, e.as_mut_ptr());
    for i in 0..4 {
        equation.add(i).write_unaligned(to_fixed(e[i]));
    }
}

// ---- lighting -------------------------------------------------------------------------------
fn light_count(pname: u32) -> usize {
    match pname {
        0x1200..=0x1203 => 4,
        0x1204 => 3,
        0x1205..=0x1209 => 1,
        _ => 0,
    }
}
fn model_count(pname: u32) -> usize {
    match pname {
        0x0b53 => 4,
        0x0b51 | 0x0b52 => 1,
        _ => 0,
    }
}
fn material_count(pname: u32) -> usize {
    match pname {
        0x1200 | 0x1201 | 0x1202 | 0x1600 | 0x1602 => 4,
        0x1601 => 1,
        _ => 0,
    }
}

unsafe fn fixed_values(params: *const i32, n: usize) -> Option<[f32; 4]> {
    if params.is_null() {
        fail(0x0501);
        return None;
    }
    Some(core::array::from_fn(|i| {
        if i < n {
            to_float(params.add(i).read_unaligned())
        } else {
            0.
        }
    }))
}

#[no_mangle]
pub unsafe extern "C" fn glLightx(light: u32, pname: u32, param: i32) {
    crate::lighting::glLightf(light, pname, to_float(param));
}
#[no_mangle]
pub unsafe extern "C" fn glLightModelx(pname: u32, param: i32) {
    crate::lighting::glLightModelf(pname, to_float(param));
}
#[no_mangle]
pub unsafe extern "C" fn glMaterialx(face: u32, pname: u32, param: i32) {
    crate::lighting::glMaterialf(face, pname, to_float(param));
}
#[no_mangle]
pub unsafe extern "C" fn glLightxv(light: u32, pname: u32, params: *const i32) {
    let n = light_count(pname);
    if n == 0 {
        return fail(0x0500);
    }
    if let Some(f) = fixed_values(params, n) {
        crate::lighting::glLightfv(light, pname, f.as_ptr());
    }
}
#[no_mangle]
pub unsafe extern "C" fn glLightModelxv(pname: u32, params: *const i32) {
    let n = model_count(pname);
    if n == 0 {
        return fail(0x0500);
    }
    if let Some(f) = fixed_values(params, n) {
        crate::lighting::glLightModelfv(pname, f.as_ptr());
    }
}
#[no_mangle]
pub unsafe extern "C" fn glMaterialxv(face: u32, pname: u32, params: *const i32) {
    let n = material_count(pname);
    if n == 0 {
        return fail(0x0500);
    }
    if let Some(f) = fixed_values(params, n) {
        crate::lighting::glMaterialfv(face, pname, f.as_ptr());
    }
}
#[no_mangle]
pub unsafe extern "C" fn glGetLightxv(light: u32, pname: u32, params: *mut i32) {
    let n = light_count(pname);
    if n == 0 || params.is_null() {
        return fail(if n == 0 { 0x0500 } else { 0x0501 });
    }
    let mut f = [0f32; 4];
    crate::lighting::glGetLightfv(light, pname, f.as_mut_ptr());
    for i in 0..n {
        params.add(i).write_unaligned(to_fixed(f[i] as f64));
    }
}
#[no_mangle]
pub unsafe extern "C" fn glGetMaterialxv(face: u32, pname: u32, params: *mut i32) {
    let n = material_count(pname);
    if n == 0 || params.is_null() {
        return fail(if n == 0 { 0x0500 } else { 0x0501 });
    }
    let mut f = [0f32; 4];
    crate::lighting::glGetMaterialfv(face, pname, f.as_mut_ptr());
    for i in 0..n {
        params.add(i).write_unaligned(to_fixed(f[i] as f64));
    }
}

// ---- texture parameters and environments ------------------------------------------------------
#[no_mangle]
pub unsafe extern "C" fn glTexParameterx(target: u32, pname: u32, param: i32) {
    crate::texture::glTexParameteri(
        target,
        pname,
        if param_is_enum(pname) {
            param
        } else {
            to_float(param) as i32
        },
    );
}
#[no_mangle]
pub unsafe extern "C" fn glTexParameterxv(target: u32, pname: u32, params: *const i32) {
    if params.is_null() {
        return fail(0x0501);
    }
    glTexParameterx(target, pname, params.read_unaligned());
}
#[no_mangle]
pub unsafe extern "C" fn glGetTexParameterxv(target: u32, pname: u32, params: *mut i32) {
    if params.is_null() {
        return fail(0x0501);
    }
    let mut v = [0f32; 4];
    crate::texture::glGetTexParameterfv(target, pname, v.as_mut_ptr());
    // Enums and booleans, the priority truncated.
    let n = if pname == 0x1004 { 4 } else { 1 };
    for i in 0..n {
        params.add(i).write_unaligned(v[i] as i32);
    }
}

#[no_mangle]
pub unsafe extern "C" fn glTexEnviv(target: u32, pname: u32, params: *const i32) {
    if params.is_null() {
        return fail(0x0501);
    }
    if pname == 0x2201 {
        let c: [f32; 4] =
            core::array::from_fn(|i| params.add(i).read_unaligned() as f32 / 2147483647.);
        crate::texture::glTexEnvfv(target, pname, c.as_ptr());
    } else {
        crate::texture::glTexEnvi(target, pname, params.read_unaligned());
    }
}
#[no_mangle]
pub unsafe extern "C" fn glTexEnvx(target: u32, pname: u32, param: i32) {
    crate::texture::glTexEnvi(
        target,
        pname,
        if param_is_enum(pname) {
            param
        } else {
            to_float(param) as i32
        },
    );
}
#[no_mangle]
pub unsafe extern "C" fn glTexEnvxv(target: u32, pname: u32, params: *const i32) {
    if params.is_null() {
        return fail(0x0501);
    }
    if pname == 0x2201 {
        let c: [f32; 4] = core::array::from_fn(|i| to_float(params.add(i).read_unaligned()));
        crate::texture::glTexEnvfv(target, pname, c.as_ptr());
    } else {
        glTexEnvx(target, pname, params.read_unaligned());
    }
}
#[no_mangle]
pub unsafe extern "C" fn glGetTexEnvxv(target: u32, pname: u32, params: *mut i32) {
    if params.is_null() {
        return fail(0x0501);
    }
    if pname == 0x2201 || pname == 0x8573 || pname == 0x0d1c {
        let mut f = [0f32; 4];
        crate::texture::glGetTexEnvfv(target, pname, f.as_mut_ptr());
        let n = if pname == 0x2201 { 4 } else { 1 };
        for i in 0..n {
            params.add(i).write_unaligned(to_fixed(f[i] as f64));
        }
    } else {
        crate::texture::glGetTexEnviv(target, pname, params);
    }
}

// ---- glMultiTexCoord ------------------------------------------------------------------------
macro_rules! multi_tex {
    ($($name:ident, $t:ty, [$($a:ident),*], |$v:ident| $e:expr;)*) => {$(
        #[no_mangle]
        pub unsafe extern "C" fn $name(unit: u32, $($a: $t),*) {
            let $v = [$($a as f32),*];
            let c: [f32; 4] = $e;
            crate::immediate::glMultiTexCoord4f(unit, c[0], c[1], c[2], c[3]);
        }
    )*};
}
macro_rules! multi_tex_v {
    ($($name:ident, $t:ty, $n:expr;)*) => {$(
        #[no_mangle]
        pub unsafe extern "C" fn $name(unit: u32, v: *const $t) {
            if v.is_null() {
                return fail(0x0501);
            }
            let mut c = [0f32, 0., 0., 1.];
            for i in 0..$n {
                c[i] = v.add(i).read_unaligned() as f32;
            }
            crate::immediate::glMultiTexCoord4f(unit, c[0], c[1], c[2], c[3]);
        }
    )*};
}

multi_tex! {
    glMultiTexCoord1d, f64, [s], |v| [v[0], 0., 0., 1.];
    glMultiTexCoord1f, f32, [s], |v| [v[0], 0., 0., 1.];
    glMultiTexCoord1i, i32, [s], |v| [v[0], 0., 0., 1.];
    glMultiTexCoord1s, i16, [s], |v| [v[0], 0., 0., 1.];
    glMultiTexCoord2d, f64, [s, t], |v| [v[0], v[1], 0., 1.];
    glMultiTexCoord2i, i32, [s, t], |v| [v[0], v[1], 0., 1.];
    glMultiTexCoord2s, i16, [s, t], |v| [v[0], v[1], 0., 1.];
    glMultiTexCoord3d, f64, [s, t, r], |v| [v[0], v[1], v[2], 1.];
    glMultiTexCoord3i, i32, [s, t, r], |v| [v[0], v[1], v[2], 1.];
    glMultiTexCoord3s, i16, [s, t, r], |v| [v[0], v[1], v[2], 1.];
    glMultiTexCoord4d, f64, [s, t, r, q], |v| [v[0], v[1], v[2], v[3]];
    glMultiTexCoord4i, i32, [s, t, r, q], |v| [v[0], v[1], v[2], v[3]];
    glMultiTexCoord4s, i16, [s, t, r, q], |v| [v[0], v[1], v[2], v[3]];
}
multi_tex_v! {
    glMultiTexCoord1dv, f64, 1;
    glMultiTexCoord1fv, f32, 1;
    glMultiTexCoord1iv, i32, 1;
    glMultiTexCoord1sv, i16, 1;
    glMultiTexCoord2dv, f64, 2;
    glMultiTexCoord2fv, f32, 2;
    glMultiTexCoord2iv, i32, 2;
    glMultiTexCoord2sv, i16, 2;
    glMultiTexCoord3dv, f64, 3;
    glMultiTexCoord3fv, f32, 3;
    glMultiTexCoord3iv, i32, 3;
    glMultiTexCoord3sv, i16, 3;
    glMultiTexCoord4dv, f64, 4;
    glMultiTexCoord4fv, f32, 4;
    glMultiTexCoord4iv, i32, 4;
    glMultiTexCoord4sv, i16, 4;
}

// ---- queries --------------------------------------------------------------------------------
#[no_mangle]
pub unsafe extern "C" fn glGetFixedv(pname: u32, params: *mut i32) {
    if params.is_null() {
        return fail(0x0501);
    }
    match crate::frame::state_values(pname) {
        Some((v, n)) => {
            for i in 0..n {
                params.add(i).write_unaligned(to_fixed(v[i]));
            }
        }
        None => fail(0x0500),
    }
}

#[no_mangle]
pub unsafe extern "C" fn glGetPointerv(pname: u32, params: *mut *mut c_void) {
    if params.is_null() {
        return fail(0x0501);
    }
    let mut result = None;
    crate::renderer::with_immediate(|r| {
        result = r
            .arrays
            .pointer(pname)
            .or_else(|| r.frame.select.pointer(pname));
    });
    match result {
        Some(p) => params.write(p as *mut c_void),
        None => fail(0x0500),
    }
}

#[no_mangle]
pub unsafe extern "C" fn glPixelStoref(pname: u32, param: f32) {
    crate::texture::glPixelStorei(pname, lroundf(param));
}

/// Multisampling does not exist: the value is kept and has no effect (GL 1.3).
#[no_mangle]
pub unsafe extern "C" fn glSampleCoverage(value: f32, invert: u8) {
    if crate::lists::record(crate::lists::Cmd::SampleCoverage(value, invert != 0)) {
        return;
    }
    with_frame(|s, _| {
        s.sample_coverage = [value.clamp(0., 1.), (invert != 0) as u8 as f32];
    });
}

#[no_mangle]
pub unsafe extern "C" fn glFlush() {
    crate::renderer::finish_frame();
}

#[no_mangle]
pub unsafe extern "C" fn glFinish() {
    crate::renderer::finish_frame();
}

/// Hints are advisory: validated and otherwise ignored.
#[no_mangle]
pub unsafe extern "C" fn glHint(target: u32, mode: u32) {
    if !matches!(target, 0x0c50..=0x0c54 | 0x8192) || !matches!(mode, 0x1100..=0x1102) {
        return fail(0x0500);
    }
}

/// GL 1.2 3D textures do not exist.
#[no_mangle]
pub unsafe extern "C" fn glTexImage3D(
    _target: u32,
    _level: i32,
    _internal: i32,
    _w: i32,
    _h: i32,
    _d: i32,
    _border: i32,
    _format: u32,
    _ty: u32,
    _pixels: *const c_void,
) {
    fail(0x0500);
}
