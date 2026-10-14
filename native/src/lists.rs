//! Display lists. Between glNewList and glEndList the GL functions of every compiled command
//! record it and return (`record`); glCallList executes the commands through the same functions,
//! so a list does exactly what the calls would do. Client memory is copied while compiling.
use crate::renderer::with_immediate;
use alloc::{rc::Rc, vec::Vec};
use core::ffi::c_void;

const MAX_NESTING: usize = 64;

/// One compiled command, with the arguments of the GL function that recorded it.
#[derive(Clone)]
pub(crate) enum Cmd {
    Enable(u32, bool),
    ShadeModel(u32),
    Viewport([i32; 4]),
    Scissor([i32; 4]),
    ClearColor([f32; 4]),
    ClearDepth(f64),
    ClearStencil(i32),
    Clear(u32),
    ColorMask([u8; 4]),
    DepthMask(u8),
    DepthFunc(u32),
    StencilFunc(u32, i32, u32),
    StencilOp(u32, u32, u32),
    StencilMask(u32),
    AlphaFunc(u32, f32),
    BlendFuncSeparate([u32; 4]),
    BlendEquationSeparate(u32, u32),
    BlendColor([f32; 4]),
    CullFace(u32),
    FrontFace(u32),
    PolygonMode(u32, u32),
    PolygonOffset(f32, f32),
    DepthRange(f64, f64),
    LineWidth(f32),
    PointSize(f32),
    MatrixMode(u32),
    PushMatrix,
    PopMatrix,
    LoadIdentity,
    LoadMatrix([f32; 16]),
    MultMatrix([f32; 16]),
    Translate([f32; 3]),
    Rotate([f32; 4]),
    Scale([f32; 3]),
    Ortho([f64; 6]),
    Frustum([f64; 6]),
    Begin(u32),
    End,
    Vertex([f32; 4]),
    TexCoord([f32; 4]),
    MultiTexCoord(u32, [f32; 4]),
    EdgeFlag(u8),
    Normal([f32; 3]),
    Color([f32; 4]),
    Color4ub([u8; 4]),
    Light(u32, u32, [f32; 4]),
    LightModel(u32, [f32; 4]),
    Material(u32, u32, [f32; 4]),
    ColorMaterial(u32, u32),
    Fog(u32, [f32; 4]),
    TexGen(u32, u32, [f32; 4], bool),
    ClipPlane(u32, [f64; 4]),
    BindTexture(u32, u32),
    TexEnvi(u32, u32, i32),
    TexEnvColor([f32; 4]),
    ActiveTexture(u32),
    TexParameter(u32, u32, i32),
    TexImage {
        sub: bool,
        target: u32,
        level: i32,
        internal: i32,
        x: i32,
        y: i32,
        w: i32,
        h: i32,
        border: i32,
        f: u32,
        ty: u32,
        /// Tightly packed.
        pixels: Option<Vec<u8>>,
    },
    CompressedTexImage {
        target: u32,
        level: i32,
        internal: u32,
        w: i32,
        h: i32,
        border: i32,
        data: Option<Vec<u8>>,
    },
    Map {
        target: u32,
        two_d: bool,
        u: [f64; 2],
        v: [f64; 2],
        uorder: i32,
        vorder: i32,
        points: Vec<f32>,
    },
    MapGrid1(i32, f32, f32),
    MapGrid2(i32, f32, f32, i32, f32, f32),
    EvalCoord1(f32),
    EvalCoord2(f32, f32),
    EvalPoint1(i32),
    EvalPoint2(i32, i32),
    EvalMesh1(u32, i32, i32),
    EvalMesh2(u32, i32, i32, i32, i32),
    PushAttrib(u32),
    PopAttrib,
    CallList(u32),
    CallLists(Vec<u32>),
    ListBase(u32),
    PassThrough(f32),
    InitNames,
    LoadName(u32),
    PushName(u32),
    PopName,
    /// A call that failed while compiling raises its error when executed.
    Raise(u32),
}

struct Compiling {
    name: u32,
    mode: u32,
    cmds: Vec<Cmd>,
}

pub(crate) struct Lists {
    /// Sorted by name.
    lists: Vec<(u32, Rc<Vec<Cmd>>)>,
    compiling: Option<Compiling>,
    pub base: u32,
    depth: usize,
}

impl Lists {
    pub fn new() -> Self {
        Self {
            lists: Vec::new(),
            compiling: None,
            base: 0,
            depth: 0,
        }
    }

    fn find(&self, name: u32) -> Result<usize, usize> {
        self.lists.binary_search_by_key(&name, |l| l.0)
    }

    pub fn is_compiling(&self) -> bool {
        self.compiling.is_some()
    }

    /// GL_LIST_INDEX, GL_LIST_MODE, GL_LIST_BASE.
    pub fn query(&self, pname: u32) -> Option<f64> {
        match pname {
            0x0b32 => Some(self.base as f64),
            0x0b33 => Some(self.compiling.as_ref().map_or(0, |c| c.name) as f64),
            0x0b30 => Some(self.compiling.as_ref().map_or(0, |c| c.mode) as f64),
            _ => None,
        }
    }
}

/// Record `cmd` if a list is being compiled. True: the call is done (GL_COMPILE); false: the
/// caller executes it (not compiling, or GL_COMPILE_AND_EXECUTE).
pub(crate) unsafe fn record(cmd: Cmd) -> bool {
    let mut done = false;
    with_immediate(|r| {
        if let Some(c) = &mut r.frame.lists.compiling {
            if c.cmds.try_reserve(1).is_err() {
                r.matrices.fail(0x0505);
                done = true;
                return;
            }
            c.cmds.push(cmd);
            done = c.mode == 0x1300;
        }
    });
    done
}

/// Compiling: the pending error of a call that cannot be recorded as given (true: do not run it).
pub(crate) unsafe fn record_error(code: u32) -> bool {
    record(Cmd::Raise(code))
}

pub(crate) unsafe fn compiling() -> bool {
    let mut c = false;
    with_immediate(|r| c = r.frame.lists.is_compiling());
    c
}

unsafe fn execute(cmd: &Cmd) {
    use crate::{
        blend::*, clip::*, evaluator::*, fog::*, frame::*, immediate::*, lighting::*, matrix::*,
        raster::*, texgen::*, texture::*,
    };
    match cmd {
        Cmd::Enable(cap, on) => {
            if *on {
                glEnable(*cap)
            } else {
                glDisable(*cap)
            }
        }
        Cmd::ShadeModel(m) => glShadeModel(*m),
        Cmd::Viewport(v) => glViewport(v[0], v[1], v[2], v[3]),
        Cmd::Scissor(v) => glScissor(v[0], v[1], v[2], v[3]),
        Cmd::ClearColor(c) => glClearColor(c[0], c[1], c[2], c[3]),
        Cmd::ClearDepth(d) => glClearDepth(*d),
        Cmd::ClearStencil(s) => glClearStencil(*s),
        Cmd::Clear(m) => glClear(*m),
        Cmd::ColorMask(c) => glColorMask(c[0], c[1], c[2], c[3]),
        Cmd::DepthMask(f) => glDepthMask(*f),
        Cmd::DepthFunc(f) => glDepthFunc(*f),
        Cmd::StencilFunc(f, r, m) => glStencilFunc(*f, *r, *m),
        Cmd::StencilOp(a, b, c) => glStencilOp(*a, *b, *c),
        Cmd::StencilMask(m) => glStencilMask(*m),
        Cmd::AlphaFunc(f, r) => glAlphaFunc(*f, *r),
        Cmd::BlendFuncSeparate(b) => glBlendFuncSeparate(b[0], b[1], b[2], b[3]),
        Cmd::BlendEquationSeparate(a, b) => glBlendEquationSeparate(*a, *b),
        Cmd::BlendColor(c) => glBlendColor(c[0], c[1], c[2], c[3]),
        Cmd::CullFace(f) => glCullFace(*f),
        Cmd::FrontFace(f) => glFrontFace(*f),
        Cmd::PolygonMode(f, m) => glPolygonMode(*f, *m),
        Cmd::PolygonOffset(a, b) => glPolygonOffset(*a, *b),
        Cmd::DepthRange(a, b) => glDepthRange(*a, *b),
        Cmd::LineWidth(w) => glLineWidth(*w),
        Cmd::PointSize(s) => glPointSize(*s),
        Cmd::MatrixMode(m) => glMatrixMode(*m),
        Cmd::PushMatrix => glPushMatrix(),
        Cmd::PopMatrix => glPopMatrix(),
        Cmd::LoadIdentity => glLoadIdentity(),
        Cmd::LoadMatrix(m) => glLoadMatrixf(m.as_ptr()),
        Cmd::MultMatrix(m) => glMultMatrixf(m.as_ptr()),
        Cmd::Translate(v) => glTranslatef(v[0], v[1], v[2]),
        Cmd::Rotate(v) => glRotatef(v[0], v[1], v[2], v[3]),
        Cmd::Scale(v) => glScalef(v[0], v[1], v[2]),
        Cmd::Ortho(v) => glOrtho(v[0], v[1], v[2], v[3], v[4], v[5]),
        Cmd::Frustum(v) => glFrustum(v[0], v[1], v[2], v[3], v[4], v[5]),
        Cmd::Begin(m) => glBegin(*m),
        Cmd::End => glEnd(),
        Cmd::Vertex(v) => glVertex4f(v[0], v[1], v[2], v[3]),
        Cmd::TexCoord(v) => glTexCoord4f(v[0], v[1], v[2], v[3]),
        Cmd::MultiTexCoord(u, v) => glMultiTexCoord4f(*u, v[0], v[1], v[2], v[3]),
        Cmd::EdgeFlag(f) => glEdgeFlag(*f),
        Cmd::Normal(n) => glNormal3f(n[0], n[1], n[2]),
        Cmd::Color(c) => glColor4f(c[0], c[1], c[2], c[3]),
        Cmd::Color4ub(c) => glColor4ub(c[0], c[1], c[2], c[3]),
        Cmd::Light(l, p, v) => set_light(*l, *p, v),
        Cmd::LightModel(p, v) => set_light_model(*p, v),
        Cmd::Material(f, p, v) => set_material(*f, *p, v),
        Cmd::ColorMaterial(f, m) => glColorMaterial(*f, *m),
        Cmd::Fog(p, v) => set_fog(*p, v),
        Cmd::TexGen(c, p, v, scalar) => set_tex_gen(*c, *p, v, *scalar),
        Cmd::ClipPlane(p, e) => glClipPlane(*p, e.as_ptr()),
        Cmd::BindTexture(t, n) => glBindTexture(*t, *n),
        Cmd::TexEnvi(t, p, v) => glTexEnvi(*t, *p, *v),
        Cmd::TexEnvColor(c) => glTexEnvfv(0x2300, 0x2201, c.as_ptr()),
        Cmd::ActiveTexture(u) => glActiveTexture(*u),
        Cmd::TexParameter(k, p, v) => glTexParameteri(*k, *p, *v),
        Cmd::TexImage {
            sub,
            target,
            level,
            internal,
            x,
            y,
            w,
            h,
            border,
            f,
            ty,
            pixels,
        } => {
            let pointer = pixels
                .as_ref()
                .map_or(core::ptr::null(), |p| p.as_ptr().cast::<c_void>());
            // The pixels were stored tightly packed.
            let alignment = set_alignment(1);
            if *sub && *target == 0x0de0 {
                glTexSubImage1D(*target, *level, *x, *w, *f, *ty, pointer);
            } else if *sub {
                glTexSubImage2D(*target, *level, *x, *y, *w, *h, *f, *ty, pointer);
            } else if *target == 0x0de0 {
                glTexImage1D(*target, *level, *internal, *w, *border, *f, *ty, pointer);
            } else {
                glTexImage2D(
                    *target, *level, *internal, *w, *h, *border, *f, *ty, pointer,
                );
            }
            set_alignment(alignment);
        }
        Cmd::CompressedTexImage {
            target,
            level,
            internal,
            w,
            h,
            border,
            data,
        } => {
            let pointer = data
                .as_ref()
                .map_or(core::ptr::null(), |d| d.as_ptr().cast::<c_void>());
            let size = data.as_ref().map_or(0, |d| d.len() as i32);
            glCompressedTexImage2D(*target, *level, *internal, *w, *h, *border, size, pointer);
        }
        Cmd::Map {
            target,
            two_d,
            u,
            v,
            uorder,
            vorder,
            points,
        } => {
            let k = points.len() as i32 / (*uorder * *vorder).max(1);
            if *two_d {
                glMap2f(
                    *target,
                    u[0] as f32,
                    u[1] as f32,
                    *vorder * k,
                    *uorder,
                    v[0] as f32,
                    v[1] as f32,
                    k,
                    *vorder,
                    points.as_ptr(),
                );
            } else {
                glMap1f(
                    *target,
                    u[0] as f32,
                    u[1] as f32,
                    k,
                    *uorder,
                    points.as_ptr(),
                );
            }
        }
        Cmd::MapGrid1(n, a, b) => glMapGrid1f(*n, *a, *b),
        Cmd::MapGrid2(un, u1, u2, vn, v1, v2) => glMapGrid2f(*un, *u1, *u2, *vn, *v1, *v2),
        Cmd::EvalCoord1(u) => glEvalCoord1f(*u),
        Cmd::EvalCoord2(u, v) => glEvalCoord2f(*u, *v),
        Cmd::EvalPoint1(i) => glEvalPoint1(*i),
        Cmd::EvalPoint2(i, j) => glEvalPoint2(*i, *j),
        Cmd::EvalMesh1(m, a, b) => glEvalMesh1(*m, *a, *b),
        Cmd::EvalMesh2(m, a, b, c, d) => glEvalMesh2(*m, *a, *b, *c, *d),
        Cmd::PushAttrib(m) => crate::attrib::glPushAttrib(*m),
        Cmd::PopAttrib => crate::attrib::glPopAttrib(),
        Cmd::CallList(l) => glCallList(*l),
        Cmd::CallLists(names) => glCallLists(names.len() as i32, 0x1405, names.as_ptr().cast()),
        Cmd::ListBase(b) => glListBase(*b),
        Cmd::PassThrough(t) => crate::feedback::glPassThrough(*t),
        Cmd::InitNames => crate::feedback::glInitNames(),
        Cmd::LoadName(n) => crate::feedback::glLoadName(*n),
        Cmd::PushName(n) => crate::feedback::glPushName(*n),
        Cmd::PopName => crate::feedback::glPopName(),
        Cmd::Raise(code) => with_immediate(|r| r.matrices.fail(*code)),
    }
}

unsafe fn execute_list(name: u32) {
    let mut commands = None;
    with_immediate(|r| {
        let l = &mut r.frame.lists;
        if l.depth >= MAX_NESTING {
            return;
        }
        if let Ok(i) = l.find(name) {
            l.depth += 1;
            commands = Some(l.lists[i].1.clone());
        }
    });
    let Some(commands) = commands else {
        return;
    };
    for cmd in commands.iter() {
        execute(cmd);
    }
    with_immediate(|r| r.frame.lists.depth -= 1);
}

#[no_mangle]
pub unsafe extern "C" fn glNewList(list: u32, mode: u32) {
    with_immediate(|r| {
        if r.immediate.active || r.frame.lists.compiling.is_some() {
            r.matrices.fail(0x0502);
            return;
        }
        if list == 0 {
            r.matrices.fail(0x0501);
            return;
        }
        if mode != 0x1300 && mode != 0x1301 {
            r.matrices.fail(0x0500);
            return;
        }
        r.frame.lists.compiling = Some(Compiling {
            name: list,
            mode,
            cmds: Vec::new(),
        });
    });
}

#[no_mangle]
pub unsafe extern "C" fn glEndList() {
    with_immediate(|r| {
        let l = &mut r.frame.lists;
        let Some(c) = l.compiling.take() else {
            r.matrices.fail(0x0502);
            return;
        };
        let cmds = Rc::new(c.cmds);
        match l.find(c.name) {
            Ok(i) => l.lists[i].1 = cmds,
            Err(i) => {
                if l.lists.try_reserve(1).is_err() {
                    r.matrices.fail(0x0505);
                    return;
                }
                l.lists.insert(i, (c.name, cmds));
            }
        }
    });
}

#[no_mangle]
pub unsafe extern "C" fn glCallList(list: u32) {
    if record(Cmd::CallList(list)) {
        return;
    }
    execute_list(list);
}

/// Bytes per name of a glCallLists type, 0 if the type is invalid.
fn names_size(ty: u32) -> usize {
    match ty {
        0x1400 | 0x1401 => 1,
        0x1402 | 0x1403 | 0x1407 => 2,
        0x1408 => 3,
        0x1404 | 0x1405 | 0x1406 | 0x1409 => 4,
        _ => 0,
    }
}

/// Name i of glCallLists, before the list base is added (GL_n_BYTES: big endian).
unsafe fn name_at(ty: u32, lists: *const c_void, i: usize) -> u32 {
    let p = lists.cast::<u8>().add(i * names_size(ty));
    let b = |k: usize| p.add(k).read();
    match ty {
        0x1400 => b(0) as i8 as i32 as u32,
        0x1401 => b(0) as u32,
        0x1402 => i16::from_ne_bytes([b(0), b(1)]) as i32 as u32,
        0x1403 => u16::from_ne_bytes([b(0), b(1)]) as u32,
        0x1404 | 0x1405 => u32::from_ne_bytes([b(0), b(1), b(2), b(3)]),
        0x1406 => f32::from_ne_bytes([b(0), b(1), b(2), b(3)]) as i32 as u32,
        0x1407 => (b(0) as u32) << 8 | b(1) as u32,
        0x1408 => (b(0) as u32) << 16 | (b(1) as u32) << 8 | b(2) as u32,
        _ => (b(0) as u32) << 24 | (b(1) as u32) << 16 | (b(2) as u32) << 8 | b(3) as u32,
    }
}

#[no_mangle]
pub unsafe extern "C" fn glCallLists(n: i32, ty: u32, lists: *const c_void) {
    let size = names_size(ty);
    if compiling() {
        // The names are read now; an invalid call is recorded as its error.
        let cmd = if n < 0 {
            Cmd::Raise(0x0501)
        } else if size == 0 {
            Cmd::Raise(0x0500)
        } else if lists.is_null() && n > 0 {
            Cmd::Raise(0x0501)
        } else {
            Cmd::CallLists((0..n as usize).map(|i| name_at(ty, lists, i)).collect())
        };
        if record(cmd) {
            return;
        }
    }
    if n < 0 {
        with_immediate(|r| r.matrices.fail(0x0501));
        return;
    }
    if size == 0 {
        with_immediate(|r| r.matrices.fail(0x0500));
        return;
    }
    for i in 0..n as usize {
        // The base is read per name: a called list may change it.
        let mut base = 0;
        with_immediate(|r| base = r.frame.lists.base);
        execute_list(base.wrapping_add(name_at(ty, lists, i)));
    }
}

#[no_mangle]
pub unsafe extern "C" fn glListBase(base: u32) {
    if record(Cmd::ListBase(base)) {
        return;
    }
    with_immediate(|r| r.frame.lists.base = base);
}

/// The first `range` consecutive unused names; they become empty lists. 0 if none.
#[no_mangle]
pub unsafe extern "C" fn glGenLists(range: i32) -> u32 {
    let mut result = 0;
    with_immediate(|r| {
        if range < 0 {
            r.matrices.fail(0x0501);
            return;
        }
        if range == 0 {
            return;
        }
        let l = &mut r.frame.lists;
        let (mut base, mut index) = (1u32, 0usize);
        while index < l.lists.len() {
            if l.lists[index].0 - base >= range as u32 {
                break;
            }
            base = l.lists[index].0 + 1;
            index += 1;
        }
        if base == 0 || range as u32 - 1 > u32::MAX - base {
            return;
        }
        if l.lists.try_reserve(range as usize).is_err() {
            r.matrices.fail(0x0505);
            return;
        }
        for k in 0..range as u32 {
            l.lists
                .insert(index + k as usize, (base + k, Rc::new(Vec::new())));
        }
        result = base;
    });
    result
}

#[no_mangle]
pub unsafe extern "C" fn glDeleteLists(list: u32, range: i32) {
    with_immediate(|r| {
        if range < 0 {
            r.matrices.fail(0x0501);
            return;
        }
        let end = list as u64 + range as u64;
        r.frame
            .lists
            .lists
            .retain(|l| !(l.0 >= list && (l.0 as u64) < end));
    });
}

#[no_mangle]
pub unsafe extern "C" fn glIsList(list: u32) -> u8 {
    let mut found = 0;
    with_immediate(|r| found = r.frame.lists.find(list).is_ok() as u8);
    found
}

/// Called from the renderer's single global-context test.
#[cfg(test)]
pub(crate) unsafe fn lifecycle() {
    use crate::{frame::*, matrix::glGetError};
    let line_width = || {
        let mut w = 0.;
        glGetFloatv(0x0b21, &mut w);
        w
    };
    assert_eq!(glGenLists(2), 1);
    assert_eq!(glGenLists(1), 3);
    assert_eq!(glIsList(2), 1);
    assert_eq!(glIsList(9), 0);
    // GL_COMPILE records without executing; calling the list executes it.
    glNewList(1, 0x1300);
    glLineWidth(5.);
    glPointSize(7.);
    assert_eq!(
        line_width(),
        1.,
        "queries run immediately, nothing was applied"
    );
    glEndList();
    assert_eq!(line_width(), 1.);
    glCallList(1);
    assert_eq!(line_width(), 5.);
    // GL_COMPILE_AND_EXECUTE applies at once and records too; lists nest and calls take a base.
    glNewList(2, 0x1301);
    glLineWidth(2.);
    assert_eq!(line_width(), 2.);
    glCallList(1);
    assert_eq!(line_width(), 5.);
    glEndList();
    glLineWidth(1.);
    glListBase(1);
    let names = [1u8];
    glCallLists(1, 0x1401, names.as_ptr().cast());
    assert_eq!(line_width(), 5., "list 2 sets 2 then calls list 1");
    glListBase(0);
    // Errors: no nesting of glNewList, glEndList without glNewList, list 0, bad enum.
    glNewList(0, 0x1300);
    assert_eq!(glGetError(), 0x0501);
    glNewList(5, 0x1111);
    assert_eq!(glGetError(), 0x0500);
    glEndList();
    assert_eq!(glGetError(), 0x0502);
    glNewList(5, 0x1300);
    glNewList(6, 0x1300);
    assert_eq!(glGetError(), 0x0502);
    glEndList();
    glDeleteLists(1, 3);
    assert_eq!(glIsList(1), 0);
    assert_eq!(glIsList(5), 1);
    glDeleteLists(5, 1);
    glLineWidth(1.);
    glPointSize(1.);
    assert_eq!(glGetError(), 0);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn call_list_names_follow_gl_encodings() {
        let b = [0xffu8, 0x01, 0x02, 0x03];
        unsafe {
            assert_eq!(name_at(0x1400, b.as_ptr().cast(), 0), 0xffff_ffff);
            assert_eq!(name_at(0x1401, b.as_ptr().cast(), 0), 255);
            assert_eq!(name_at(0x1407, b.as_ptr().cast(), 1), 0x0203);
            assert_eq!(name_at(0x1408, b.as_ptr().cast(), 0), 0xff0102);
            let f = 3.7f32.to_ne_bytes();
            assert_eq!(name_at(0x1406, f.as_ptr().cast(), 0), 3);
        }
        assert_eq!(names_size(0x1408), 3);
        assert_eq!(names_size(0x1234), 0);
    }

    #[test]
    fn lists_stay_sorted_and_report_modes() {
        let mut l = Lists::new();
        l.lists.insert(0, (5, Rc::new(Vec::new())));
        assert_eq!(l.find(5), Ok(0));
        assert_eq!(l.find(2), Err(0));
        assert_eq!(l.find(9), Err(1));
        assert!(!l.is_compiling());
        l.compiling = Some(Compiling {
            name: 7,
            mode: 0x1301,
            cmds: Vec::new(),
        });
        assert_eq!((l.query(0x0b33), l.query(0x0b30)), (Some(7.), Some(4865.)));
        assert!(l.query(0x1111).is_none());
    }
}
