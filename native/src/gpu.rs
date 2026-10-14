//! PICA200 packets, with no libctru or service calls.
use crate::{registers as r, shader, Commands};

pub const FINISH_WORDS: usize = 10;
// Includes the geometry base packet used by renderer draws.
pub const DRAW_WORDS: usize = 24;

fn write(c: &mut Commands<'_>, reg: u16, mask: u8, values: &[u32], inc: bool) -> Result<(), ()> {
    if c.write(reg, mask, inc, values) {
        Ok(())
    } else {
        Err(())
    }
}

fn one(c: &mut Commands<'_>, reg: u16, value: u32) -> Result<(), ()> {
    write(c, reg, 15, &[value], false)
}

fn seq(c: &mut Commands<'_>, reg: u16, values: &[u32]) -> Result<(), ()> {
    write(c, reg, 15, values, true)
}

fn masked(c: &mut Commands<'_>, reg: u16, mask: u8, value: u32) -> Result<(), ()> {
    write(c, reg, mask, &[value], false)
}

pub fn pica_float(value: f32, mantissa_bits: u32) -> u32 {
    let bits = value.to_bits();
    let sign = bits >> 31;
    let exponent = ((bits >> 23) & 255) as i32 - 127 + 63;
    if exponent < 0 {
        return sign << (mantissa_bits + 7);
    }

    let mantissa = if exponent > 127 {
        0
    } else {
        (bits & 0x7fffff) >> (23 - mantissa_bits)
    };

    (sign << (mantissa_bits + 7)) | ((exponent.min(127) as u32) << mantissa_bits) | mantissa
}

pub fn initialize(c: &mut Commands<'_>, color: u32, depth: u32, vertices: u32) -> Result<(), ()> {
    // Framebuffer is sideways: 240 columns, 400 rows; RGBA8 and D24S8 in VRAM.
    let dimension = 0x01000000 | (399 << 12) | 240;
    one(c, r::FRAMEBUFFER_INVALIDATE, 1)?;
    seq(c, r::DEPTHBUFFER_LOC, &[depth >> 3, color >> 3, dimension])?;
    one(c, r::RENDERBUF_DIM, dimension)?;
    one(c, r::DEPTHBUFFER_FORMAT, 3)?;
    one(c, r::COLORBUFFER_FORMAT, 2)?;
    one(c, r::FRAMEBUFFER_BLOCK32, 0)?;
    seq(c, r::COLORBUFFER_READ, &[15, 15, 3, 3])?;
    seq(
        c,
        r::VIEWPORT_WIDTH,
        &[
            pica_float(120.0, 16),
            pica_float(2.0 / 240.0, 23) << 1,
            pica_float(200.0, 16),
            pica_float(2.0 / 400.0, 23) << 1,
        ],
    )?;
    one(c, r::VIEWPORT_XY, 0)?;
    seq(c, r::SCISSORTEST_MODE, &[0, 0, 0])?;
    one(c, r::DEPTHMAP_ENABLE, 1)?;
    one(c, r::FACECULLING_CONFIG, 0)?;
    seq(c, r::DEPTHMAP_SCALE, &[pica_float(-1.0, 16), 0])?;
    // Alpha and stencil disabled; depth always passes, only RGBA is written.
    seq(c, r::FRAGOP_ALPHA_TEST, &[0x10, 0xff000010, 0, 0x0f10])?;
    masked(c, r::GAS_DELTAZ_DEPTH, 8, 0)?;
    one(c, r::BLEND_COLOR, 0)?;
    one(c, r::BLEND_FUNC, 0x01010000)?; // ONE,ZERO for RGB and alpha
    one(c, r::LOGIC_OP, 3)?;
    masked(c, r::COLOR_OPERATION, 7, 0x00e40100)?;
    one(c, r::FRAGOP_SHADOW, 0)?;
    masked(c, r::EARLYDEPTH_TEST1, 1, 0)?;
    one(c, r::EARLYDEPTH_TEST2, 0)?;
    masked(c, r::EARLYDEPTH_FUNC, 1, 0)?;
    masked(c, r::EARLYDEPTH_DATA, 7, 0)?;
    masked(c, r::TEXUNIT_CONFIG, 11, 1 << 12)?;
    masked(c, r::TEXUNIT_CONFIG, 4, 1 << 16)?;
    one(c, r::TEXUNIT0_SHADOW, 1)?;
    masked(c, r::TEXENV_UPDATE_BUFFER, 7, 0)?;
    one(c, r::TEXENV_BUFFER_COLOR, u32::MAX)?;
    one(c, r::FOG_COLOR, 0)?;
    for (stage, reg) in [
        r::TEXENV0_SOURCE,
        r::TEXENV1_SOURCE,
        r::TEXENV2_SOURCE,
        r::TEXENV3_SOURCE,
        r::TEXENV4_SOURCE,
        r::TEXENV5_SOURCE,
    ]
    .iter()
    .enumerate()
    {
        seq(
            c,
            *reg,
            &[if stage == 0 { 0 } else { 0x000f000f }, 0, 0, u32::MAX, 0],
        )?;
    }
    one(c, r::LIGHTING_ENABLE0, 0)?;
    one(c, r::LIGHTING_ENABLE1, 1)?;
    // Disable geometry shader and upload the built-in matrix vertex program.
    masked(c, r::GEOSTAGE_CONFIG, 3, 0)?;
    masked(c, r::GEOSTAGE_CONFIG2, 3, 0)?;
    masked(c, r::VSH_COM_MODE, 1, 0)?;
    one(c, r::VSH_CODETRANSFER_CONFIG, 0)?;
    write(c, r::VSH_CODETRANSFER_DATA, 15, shader::CODE, false)?;
    one(c, r::VSH_CODETRANSFER_END, 1)?;
    one(c, r::VSH_OPDESCS_CONFIG, 0)?;
    write(c, r::VSH_OPDESCS_DATA, 15, shader::DESCRIPTORS, false)?;
    for (register, values) in shader::CONSTANTS {
        one(c, r::VSH_FLOATUNIFORM_CONFIG, 0x80000000 | register)?;
        write(c, r::VSH_FLOATUNIFORM_DATA, 15, values, false)?;
    }
    one(c, r::VSH_ENTRYPOINT, 0x7fff0000 | shader::ENTRY)?;
    one(c, r::VSH_OUTMAP_MASK, 7)?;
    one(c, r::VSH_OUTMAP_TOTAL1, 2)?;
    one(c, r::VSH_OUTMAP_TOTAL2, 2)?;
    masked(c, r::PRIMITIVE_CONFIG, 1, 1)?;
    seq(
        c,
        r::SH_OUTMAP_TOTAL,
        &[
            3, 0x03020100, 0x0b0a0908, 0x1f100d0c, 0x1f1f1f1f, 0x1f1f1f1f, 0x1f1f1f1f, 0x1f1f1f1f,
        ],
    )?;
    one(c, r::SH_OUTATTR_MODE, 1)?;
    one(c, r::SH_OUTATTR_CLOCK, 3 | (1 << 8))?;
    masked(c, r::GEOSTAGE_CONFIG, 10, 0)?;
    one(c, r::GSH_MISC0, 0)?;
    one(c, r::GSH_MISC1, 0)?;
    one(c, r::GSH_INPUTBUFFER_CONFIG, 0xa0000000)?;
    // v0/v1/v2: float4 position/color/UV; the stride also covers the extra texture units.
    seq(c, r::ATTRIBBUFFERS_FORMAT_LOW, &[0xfff, 0x2ff80000])?;
    masked(c, r::VSH_INPUTBUFFER_CONFIG, 11, 0xa0000002)?;
    one(c, r::VSH_NUM_ATTR, 2)?;
    seq(c, r::VSH_ATTRIBUTES_PERMUTATION_LOW, &[0x210, 0])?;
    one(c, r::ATTRIBBUFFERS_LOC, vertices >> 3)?;
    let mut buffers = [0; 36];
    buffers[1] = 0x210;
    buffers[2] = ((core::mem::size_of::<crate::renderer::GpuVertex>() as u32) << 16) | 0x30000000;
    seq(c, r::ATTRIBBUFFER0_OFFSET, &buffers)?;
    finish(c)
}

/// Float32 uniform vectors are sent w,z,y,x; rows implement PICA screen/depth mapping.
pub fn matrix(c: &mut Commands<'_>, m: &[f32; 16]) -> Result<(), ()> {
    if c.words.len() - c.used < 20 + DRAW_WORDS + FINISH_WORDS {
        return Err(());
    }

    let mut data = [0; 16];
    for row in 0..4 {
        for col in 0..4 {
            let value = match row {
                0 => m[col * 4 + 1],
                1 => -m[col * 4],
                2 => (m[col * 4 + 2] - m[col * 4 + 3]) * 0.5,
                _ => m[col * 4 + 3],
            };

            data[row * 4 + 3 - col] = value.to_bits();
        }
    }

    one(c, r::VSH_FLOATUNIFORM_CONFIG, 0x80000000)?;
    write(c, r::VSH_FLOATUNIFORM_DATA, 15, &data, false)
}

pub fn viewport(c: &mut Commands<'_>, v: [i32; 4]) -> Result<(), ()> {
    if c.words.len() - c.used < 8 + DRAW_WORDS + FINISH_WORDS {
        return Err(());
    }

    let [x, y, w, h] = v;
    seq(
        c,
        r::VIEWPORT_WIDTH,
        &[
            pica_float(h as f32 * 0.5, 16),
            pica_float(2. / h as f32, 23) << 1,
            pica_float(w as f32 * 0.5, 16),
            pica_float(2. / w as f32, 23) << 1,
        ],
    )?;

    one(
        c,
        r::VIEWPORT_XY,
        ((400 - x - w) as u32 & 0xffff) << 16 | (y as u32 & 0xffff),
    )
}

pub fn begin(c: &mut Commands<'_>) -> Result<(), ()> {
    one(c, r::FRAMEBUFFER_INVALIDATE, 1)
}

#[cfg(test)]
pub fn draw(c: &mut Commands<'_>, first: u32, count: u32) -> Result<(), ()> {
    // Reserve the whole draw and the eventual finish before touching the buffer.
    if c.words.len() - c.used < DRAW_WORDS + FINISH_WORDS {
        return Err(());
    }

    draw_index(c, first, count, None)
}

fn draw_index(c: &mut Commands<'_>, first: u32, count: u32, index: Option<u32>) -> Result<(), ()> {
    masked(
        c,
        r::PRIMITIVE_CONFIG,
        2,
        if index.is_some() { 0x300 } else { 0 },
    )?;
    one(c, r::RESTART_PRIMITIVE, 1)?;
    one(c, r::INDEXBUFFER_CONFIG, index.unwrap_or(0x80000000))?;
    one(c, r::NUMVERTICES, count)?;
    one(c, r::VERTEX_OFFSET, first)?;

    if index.is_some() {
        masked(c, r::GEOSTAGE_CONFIG, 2, 0x100)?;
        masked(c, r::GEOSTAGE_CONFIG2, 2, 0x100)?;
    } else {
        masked(c, r::GEOSTAGE_CONFIG2, 1, 1)?;
    }

    masked(c, r::START_DRAW_FUNC0, 1, 0)?;
    one(
        c,
        if index.is_some() {
            0x022f
        } else {
            r::DRAWARRAYS
        },
        1,
    )?;

    masked(c, r::START_DRAW_FUNC0, 1, 1)?;

    if index.is_some() {
        masked(c, r::GEOSTAGE_CONFIG, 2, 0)?;
        masked(c, r::GEOSTAGE_CONFIG2, 2, 0)?;
    } else {
        masked(c, r::GEOSTAGE_CONFIG2, 1, 0)?;
    }

    one(c, r::VTX_FUNC, 1)?;

    if index.is_some() {
        masked(c, r::PRIMITIVE_CONFIG, 8, 0)?;
        masked(c, r::PRIMITIVE_CONFIG, 8, 0)?;
    }

    Ok(())
}

pub fn finish(c: &mut Commands<'_>) -> Result<(), ()> {
    if c.words.len() - c.used < FINISH_WORDS {
        return Err(());
    }

    one(c, r::FRAMEBUFFER_FLUSH, 1)?;
    one(c, r::FRAMEBUFFER_INVALIDATE, 1)?;
    one(c, r::EARLYDEPTH_CLEAR, 1)?;
    one(c, r::FINALIZE, 0x12345678)?;

    if c.used % 4 != 0 {
        one(c, r::FINALIZE, 0x12345678)?;
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn texture_matrix_scales_rows_without_scaling_projective_q() {
        let mut words = [0; 128];
        let mut commands = Commands::new(&mut words);
        let mut matrix = crate::matrix::IDENTITY;
        matrix[12] = 0.25;
        matrix[3] = 1.;
        texture_matrix(&mut commands, &matrix, [0.75, 0.5]).unwrap();
        let packet = commands.as_slice();
        assert_eq!(packet[0], 0x80000005);
        let data: alloc::vec::Vec<_> = core::iter::once(packet[2])
            .chain(packet[4..15].iter().copied())
            .collect();
        assert_eq!(data[0..4], [0.1875f32.to_bits(), 0, 0, 0.75f32.to_bits()]);
        assert_eq!(data[4..8], [0, 0, 0.5f32.to_bits(), 0]);
        assert_eq!(data[8..12], [1f32.to_bits(), 0, 0, 1f32.to_bits()]);
        let mut small = [7; 16 + DRAW_WORDS + 8 + FINISH_WORDS - 1];
        let mut commands = Commands::new(&mut small);
        assert!(texture_matrix(&mut commands, &matrix, [1.; 2]).is_err());
        assert_eq!(commands.used, 0);
        assert!(small.iter().all(|word| *word == 7));
    }

    #[test]
    fn texture_binding_modulation_incompleteness_and_overflow() {
        let mut words = [0; 128];
        let mut c = Commands::new(&mut words);
        let mut t = crate::texture::Packet {
            enabled: true,
            complete: true,
            address: 0x20001000,
            dimensions: 0x00100008,
            parameters: 0x2206,
            replace: false,
            scale: [1.; 2],
            tev: [0; 5],
        };
        texture(&mut c, t).unwrap();
        assert_eq!(c.used, TEXTURE_WORDS);
        assert_eq!(c.as_slice()[0], 0x1001);
        assert_eq!(c.as_slice()[2], 0x10000);
        assert_eq!(c.as_slice()[5], 0x000f0081 | (4 << 20) | (1 << 31));
        assert_eq!(c.as_slice()[6], t.dimensions);
        assert_eq!(c.as_slice()[9], t.address >> 3);
        assert_eq!(c.as_slice()[12], 0x00300030);
        assert_eq!(c.as_slice()[15], 0x00010001);
        t.complete = false;
        let start = c.used;
        texture(&mut c, t).unwrap();
        assert_eq!(c.as_slice()[start], 0x1000);
        assert_eq!(c.as_slice()[start + 12], 0x00e000e0);
        t.enabled = false;
        let start = c.used;
        texture(&mut c, t).unwrap();
        assert_eq!(c.as_slice()[start + 12], 0);
        assert_eq!(c.as_slice()[start + 15], 0);
        let mut too_small = [7; TEXTURE_WORDS + DRAW_WORDS + 8 + FINISH_WORDS - 1];
        let mut c = Commands::new(&mut too_small);
        assert!(texture(&mut c, t).is_err());
        assert_eq!(c.used, 0);
        assert!(too_small.iter().all(|v| *v == 7));
    }

    #[test]
    fn depth_cull_packet_and_overflow() {
        let mut state = crate::raster::Raster::new();
        state.depth = true;
        state.cull = true;
        state.range = [0.2, 0.8];
        let mut words = [0; 80];
        let mut c = Commands::new(&mut words);
        raster(&mut c, state).unwrap();
        assert_eq!(c.used, RASTER_WORDS);
        assert_eq!(c.as_slice()[4], 2);
        assert_eq!(c.as_slice()[6], 0x1f61);
        assert_eq!(c.as_slice()[8], 0x02000000);
        assert_eq!(c.as_slice()[10], pica_float(-(0.8f32 - 0.2), 16));
        assert_eq!(c.as_slice()[12], pica_float(1. - 0.8, 16));
        let mut too_small = [7; 51];
        let mut c = Commands::new(&mut too_small);
        assert!(raster(&mut c, state).is_err());
        assert!(c.as_slice().is_empty());
        assert_eq!(too_small, [7; 51]);
    }

    #[test]
    fn indexed_geometry_packet_and_transactional_overflow() {
        let mut words = [0; 44];
        let mut c = Commands::new(&mut words);
        geometry(&mut c, 0x20000000, 0, 6, Some(0x80000080)).unwrap();
        assert_eq!(c.used, DRAW_WORDS + 8);
        assert_eq!(c.as_slice()[0], 0x04000000);
        assert_eq!(c.as_slice()[6], 0x80000080);
        assert_eq!(c.as_slice()[19] & 0x3ff, 0x022f);
        let before = c.as_slice().to_vec();
        assert!(geometry(&mut c, 0x20001000, 0, 3, None).is_err());
        assert_eq!(c.as_slice(), before);
    }

    #[test]
    fn landscape_subviewport_layout() {
        let mut words = [0; 64];
        let mut c = Commands::new(&mut words);
        viewport(&mut c, [200, 120, 200, 120]).unwrap();
        assert_eq!(c.used, 8);
        assert_eq!(c.as_slice()[0], pica_float(60., 16));
        assert_eq!(c.as_slice()[4], pica_float(2. / 200., 23) << 1);
        assert_eq!(c.as_slice()[6], 120);
    }

    #[test]
    fn matrix_uniform_layout_and_overflow() {
        let mut words = [0; 64];
        let mut c = Commands::new(&mut words);
        matrix(&mut c, &crate::matrix::IDENTITY).unwrap();
        let encoded = c.as_slice();
        assert_eq!(encoded.len(), 20);
        assert_eq!(encoded[0], 0x80000000);
        assert_eq!(encoded[1] & 0x3ff, r::VSH_FLOATUNIFORM_CONFIG as u32);
        assert_eq!(
            encoded[2..8],
            [0, 0x00ff02c1, 0, 1f32.to_bits(), 0, (-0f32).to_bits()]
        );
        assert_eq!(encoded[11], (-0.5f32).to_bits());
        let before = encoded.to_vec();
        assert!(matrix(&mut c, &crate::matrix::IDENTITY).is_err());
        assert_eq!(c.as_slice(), before);
    }

    #[test]
    fn draw_overflow_leaves_commands_unchanged() {
        let mut words = [7; 36];
        let mut c = Commands::new(&mut words);
        begin(&mut c).unwrap();
        draw(&mut c, 0, 3).unwrap();
        let old = c.as_slice().to_vec();
        assert!(draw(&mut c, 0, 3).is_err());
        assert_eq!(c.as_slice(), old);
        finish(&mut c).unwrap();
        assert_eq!(c.used % 4, 0);
    }

    #[test]
    fn init_and_frame_have_aligned_finalizers() {
        let mut words = [0; 1024];
        let mut c = Commands::new(&mut words);
        initialize(&mut c, 0x18000000, 0x18300000, 0x20000000).unwrap();
        assert_eq!(c.used % 4, 0);
        assert!(c.used < 1024);
        assert_eq!(c.as_slice()[c.used - 1] & 0x3ff, r::FINALIZE as u32);
    }

    #[test]
    fn float_formats_match_ctru_layout() {
        assert_eq!(pica_float(1.0, 16), 0x3f0000);
        assert_eq!(pica_float(-1.0, 16), 0xbf0000);
        assert_eq!(pica_float(0.0, 16), 0);
        assert_eq!(pica_float(120.0, 16), 0x45e000);
        assert_eq!(pica_float(1.0, 23), 0x1f800000);
    }
}

pub fn geometry(
    c: &mut Commands<'_>,
    address: u32,
    first: u32,
    count: u32,
    index: Option<u32>,
) -> Result<(), ()> {
    let words = DRAW_WORDS + if index.is_some() { 8 } else { 0 };
    if c.words.len() - c.used < words + FINISH_WORDS {
        return Err(());
    }
    one(c, r::ATTRIBBUFFERS_LOC, address >> 3)?;
    draw_index(c, first, count, index)
}

pub const RASTER_WORDS: usize = 14;
pub(crate) fn raster(c: &mut Commands<'_>, s: crate::raster::Raster) -> Result<(), ()> {
    if c.words.len() - c.used < RASTER_WORDS + DRAW_WORDS + 8 + FINISH_WORDS {
        return Err(());
    }
    one(c, r::BLEND_FUNC, s.blend.word())?;
    one(c, r::BLEND_COLOR, s.blend.color_word())?;
    one(c, r::FACECULLING_CONFIG, s.cull_word())?;
    one(c, r::DEPTH_COLOR_MASK, s.depth_word())?;
    masked(
        c,
        r::GAS_DELTAZ_DEPTH,
        8,
        ((0xaf02 >> (s.comparison() * 2)) & 3) << 24,
    )?;
    seq(
        c,
        r::DEPTHMAP_SCALE,
        &[
            pica_float(-(s.range[1] - s.range[0]), 16),
            pica_float(1. - s.range[1], 16),
        ],
    )
}

pub const TEXTURE_WORDS: usize = 26;
pub(crate) fn texture(c: &mut Commands<'_>, t: crate::texture::Packet) -> Result<(), ()> {
    if c.words.len() - c.used < TEXTURE_WORDS + DRAW_WORDS + 8 + FINISH_WORDS {
        return Err(());
    }
    let active = t.enabled && t.complete;
    masked(c, r::TEXUNIT_CONFIG, 11, (1 << 12) | active as u32)?;
    // Every changed binding/image invalidates texture cache, without evicting VBOs.
    masked(c, r::TEXUNIT_CONFIG, 4, 1 << 16)?;
    seq(
        c,
        0x0081,
        &[
            0,
            t.dimensions,
            t.parameters | 0x30000000,
            0,
            t.address >> 3,
        ],
    )?;
    one(c, 0x008e, 0)?; // Resident uploads are converted to RGBA8.
    let source = if !t.enabled {
        0
    } else if t.complete {
        if t.replace {
            0x00030003
        } else {
            0x00300030
        }
    } else {
        if t.replace {
            0x000e000e
        } else {
            0x00e000e0
        }
    };
    let combine = if t.enabled && !t.replace {
        0x00010001
    } else {
        0
    };
    // Incomplete level-zero textures (including default mip filters) sample black, alpha one.
    seq(c, r::TEXENV0_SOURCE, &[source, 0, combine, 0xff000000, 0])?;
    one(c, r::VSH_FLOATUNIFORM_CONFIG, 0x80000004)?;
    write(
        c,
        r::VSH_FLOATUNIFORM_DATA,
        15,
        &[
            1f32.to_bits(),
            0,
            t.scale[1].to_bits(),
            t.scale[0].to_bits(),
        ],
        false,
    )
}

pub(crate) fn texture_matrix(
    c: &mut Commands<'_>,
    matrix: &[f32; 16],
    scale: [f32; 2],
) -> Result<(), ()> {
    if c.words.len() - c.used < 16 + DRAW_WORDS + 8 + FINISH_WORDS {
        return Err(());
    }
    let mut data = [0; 12];
    for row in 0..3 {
        let component = if row == 2 { 3 } else { row };
        for col in 0..4 {
            let value = matrix[col * 4 + component] * if row == 2 { 1. } else { scale[row] };
            data[row * 4 + 3 - col] = value.to_bits();
        }
    }
    one(c, r::VSH_FLOATUNIFORM_CONFIG, 0x80000005)?;
    write(c, r::VSH_FLOATUNIFORM_DATA, 15, &data, false)
}
