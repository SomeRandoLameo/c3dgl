//! Attribute stacks (glPushAttrib, glPushClientAttrib). A push snapshots everything; a pop
//! restores only the groups of the pushed mask (GL 1.1 tables 6.x). A feature that adds state
//! must add it here.
use crate::{
    arrays::ArraysSave,
    evaluator::EvalSave,
    frame::PolyState,
    lighting::Lighting,
    raster::Raster,
    renderer::{with_immediate, Renderer},
    texture::TexSave,
};
use alloc::vec::Vec;

const DEPTH: usize = 16;

const CURRENT: u32 = 0x1;
const POINT: u32 = 0x2;
const LINE: u32 = 0x4;
const POLYGON: u32 = 0x8;
const LIGHTING: u32 = 0x40;
const FOG: u32 = 0x80;
const DEPTH_BUFFER: u32 = 0x100;
const STENCIL_BUFFER: u32 = 0x400;
const VIEWPORT: u32 = 0x800;
const TRANSFORM: u32 = 0x1000;
const ENABLE: u32 = 0x2000;
const COLOR_BUFFER: u32 = 0x4000;
const EVAL: u32 = 0x10000;
const TEXTURE: u32 = 0x40000;
const SCISSOR: u32 = 0x80000;

const CLIENT_PIXEL_STORE: u32 = 0x1;
const CLIENT_VERTEX_ARRAY: u32 = 0x2;

pub(crate) struct Saved {
    mask: u32,
    color: [f32; 4],
    normal: [f32; 3],
    texcoord: [f32; 4],
    extra_texcoords: [[f32; 4]; 2],
    edge: bool,
    raster: Raster,
    poly: PolyState,
    line_width: f32,
    point_size: f32,
    clear_color: u32,
    clear_depth: f32,
    clear_stencil: u8,
    viewport: [i32; 4],
    light: Lighting,
    eval: EvalSave,
    textures: TexSave,
    texgen: crate::texgen::TexGen,
    fog: crate::fog::Fog,
    clip: crate::clip::ClipState,
    mode: usize,
}

pub(crate) struct ClientSaved {
    mask: u32,
    arrays: ArraysSave,
    alignment: usize,
}

#[derive(Default)]
pub(crate) struct Stacks {
    attribs: Vec<Saved>,
    clients: Vec<ClientSaved>,
}

#[no_mangle]
pub unsafe extern "C" fn glPushAttrib(mask: u32) {
    with_immediate(|r| {
        if r.immediate.active {
            r.matrices.fail(0x0502);
            return;
        }
        if r.stacks.attribs.len() == DEPTH || r.stacks.attribs.try_reserve(1).is_err() {
            r.matrices.fail(if r.stacks.attribs.len() == DEPTH {
                0x0503
            } else {
                0x0505
            });
            return;
        }
        let f = &r.frame;
        let saved = Saved {
            mask,
            color: r.immediate.color,
            normal: r.immediate.normal,
            texcoord: r.immediate.texcoord,
            extra_texcoords: r.immediate.extra_texcoords,
            edge: r.immediate.edge,
            raster: f.raster,
            poly: f.poly,
            line_width: f.line_width,
            point_size: f.point_size,
            clear_color: f.clear_color,
            clear_depth: f.clear_depth,
            clear_stencil: f.clear_stencil,
            viewport: f.viewport,
            light: f.light.clone(),
            eval: f.eval.save(),
            textures: f.textures.save(),
            texgen: f.texgen.clone(),
            fog: f.fog,
            clip: f.clip,
            mode: r.matrices.mode(),
        };
        r.stacks.attribs.push(saved);
    });
}

/// Restore the groups of a saved state selected by its mask.
unsafe fn restore(r: &mut Renderer, a: &Saved) {
    let mask = a.mask;
    let f = &mut r.frame;
    if mask & CURRENT != 0 {
        r.immediate.color = a.color;
        r.immediate.normal = a.normal;
        r.immediate.texcoord = a.texcoord;
        r.immediate.extra_texcoords = a.extra_texcoords;
        r.immediate.edge = a.edge;
    }
    if mask & POINT != 0 {
        f.point_size = a.point_size;
    }
    if mask & LINE != 0 {
        f.line_width = a.line_width;
    }
    if mask & POLYGON != 0 {
        f.raster.cull = a.raster.cull;
        f.raster.face = a.raster.face;
        f.raster.front = a.raster.front;
        f.poly.modes = a.poly.modes;
        f.poly.offset = a.poly.offset;
        f.poly.factor = a.poly.factor;
        f.poly.units = a.poly.units;
    }
    if mask & LIGHTING != 0 {
        f.poly.flat = a.poly.flat;
        f.light = a.light.clone();
    }
    if mask & FOG != 0 {
        f.fog.restore_all(&a.fog);
    }
    if mask & DEPTH_BUFFER != 0 {
        f.raster.depth = a.raster.depth;
        f.raster.function = a.raster.function;
        f.raster.depth_mask = a.raster.depth_mask;
        f.clear_depth = a.clear_depth;
    }
    if mask & STENCIL_BUFFER != 0 {
        f.raster.stencil = a.raster.stencil;
        f.raster.stencil_func = a.raster.stencil_func;
        f.raster.stencil_ref = a.raster.stencil_ref;
        f.raster.stencil_func_mask = a.raster.stencil_func_mask;
        f.raster.stencil_write_mask = a.raster.stencil_write_mask;
        f.raster.stencil_ops = a.raster.stencil_ops;
        f.clear_stencil = a.clear_stencil;
    }
    if mask & VIEWPORT != 0 {
        f.viewport = a.viewport;
        f.raster.range = a.raster.range;
        f.drawable = f.visible();
        f.viewport_dirty = true;
    }
    if mask & TRANSFORM != 0 {
        r.matrices.set_mode(a.mode);
        f.light.restore_normalization(&a.light);
        f.clip = a.clip;
    }
    if mask & ENABLE != 0 {
        f.raster.alpha = a.raster.alpha;
        f.raster.blend.enabled = a.raster.blend.enabled;
        f.raster.cull = a.raster.cull;
        f.raster.depth = a.raster.depth;
        f.raster.scissor = a.raster.scissor;
        f.raster.stencil = a.raster.stencil;
        f.poly.offset = a.poly.offset;
        f.light.restore_enables(&a.light);
        f.fog.enabled = a.fog.enabled;
        f.clip.enabled = a.clip.enabled;
        f.eval.restore_enables(&a.eval);
        f.texgen.restore_enables(&a.texgen);
    }
    if mask & COLOR_BUFFER != 0 {
        f.raster.alpha = a.raster.alpha;
        f.raster.alpha_func = a.raster.alpha_func;
        f.raster.alpha_ref = a.raster.alpha_ref;
        f.raster.blend = a.raster.blend;
        f.raster.color_mask = a.raster.color_mask;
        f.clear_color = a.clear_color;
    }
    if mask & TEXTURE != 0 {
        f.textures.restore(&a.textures, true, true);
        f.texgen = a.texgen.clone();
        r.matrices.active_texture(f.textures.active);
    } else if mask & ENABLE != 0 {
        f.textures.restore(&a.textures, true, false);
    }
    if mask & EVAL != 0 {
        f.eval.restore_enables(&a.eval);
        f.eval.restore_grids(&a.eval);
    }
    if mask & SCISSOR != 0 {
        f.raster.scissor = a.raster.scissor;
        f.raster.scissor_box = a.raster.scissor_box;
    }
    f.raster.dirty = true;
}

#[no_mangle]
pub unsafe extern "C" fn glPopAttrib() {
    with_immediate(|r| {
        if r.immediate.active {
            r.matrices.fail(0x0502);
            return;
        }
        let Some(saved) = r.stacks.attribs.pop() else {
            r.matrices.fail(0x0504);
            return;
        };
        restore(r, &saved);
    });
}

#[no_mangle]
pub unsafe extern "C" fn glPushClientAttrib(mask: u32) {
    with_immediate(|r| {
        if r.immediate.active {
            r.matrices.fail(0x0502);
            return;
        }
        if r.stacks.clients.len() == DEPTH || r.stacks.clients.try_reserve(1).is_err() {
            r.matrices.fail(if r.stacks.clients.len() == DEPTH {
                0x0503
            } else {
                0x0505
            });
            return;
        }
        let saved = ClientSaved {
            mask,
            arrays: r.arrays.save(),
            alignment: r.frame.textures.alignment(),
        };
        r.stacks.clients.push(saved);
    });
}

#[no_mangle]
pub unsafe extern "C" fn glPopClientAttrib() {
    with_immediate(|r| {
        if r.immediate.active {
            r.matrices.fail(0x0502);
            return;
        }
        let Some(saved) = r.stacks.clients.pop() else {
            r.matrices.fail(0x0504);
            return;
        };
        if saved.mask & CLIENT_PIXEL_STORE != 0 {
            r.frame.textures.set_alignment(saved.alignment);
        }
        if saved.mask & CLIENT_VERTEX_ARRAY != 0 {
            r.arrays.restore(&saved.arrays);
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stacks_start_empty() {
        let s = Stacks::default();
        assert!(s.attribs.is_empty() && s.clients.is_empty());
        assert_eq!(DEPTH, 16);
    }
}
