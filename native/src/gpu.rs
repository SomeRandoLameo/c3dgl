//! PICA200 packets, with no libctru or service calls.
use crate::{registers as r, shader, Commands};

pub const FINISH_WORDS: usize = 10;
pub const DRAW_WORDS: usize = 22;

fn write(c: &mut Commands<'_>, reg: u16, mask: u8, values: &[u32], inc: bool) -> Result<(), ()> {
    if c.write(reg, mask, inc, values) { Ok(()) } else { Err(()) }
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
    if exponent < 0 { return sign << (mantissa_bits + 7); }
    let mantissa = if exponent > 127 { 0 } else { (bits & 0x7fffff) >> (23 - mantissa_bits) };
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
    seq(c, r::VIEWPORT_WIDTH, &[pica_float(120.0, 16), pica_float(2.0 / 240.0, 23) << 1,
        pica_float(200.0, 16), pica_float(2.0 / 400.0, 23) << 1])?;
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
    for (stage, reg) in [r::TEXENV0_SOURCE, r::TEXENV1_SOURCE, r::TEXENV2_SOURCE,
        r::TEXENV3_SOURCE, r::TEXENV4_SOURCE, r::TEXENV5_SOURCE].iter().enumerate() {
        seq(c, *reg, &[if stage == 0 { 0 } else { 0x000f000f }, 0, 0, u32::MAX, 0])?;
    }
    one(c, r::LIGHTING_ENABLE0, 0)?;
    one(c, r::LIGHTING_ENABLE1, 1)?;
    // Disable geometry shader and upload the three-instruction vertex program.
    masked(c, r::GEOSTAGE_CONFIG, 3, 0)?;
    masked(c, r::GEOSTAGE_CONFIG2, 3, 0)?;
    masked(c, r::VSH_COM_MODE, 1, 0)?;
    one(c, r::VSH_CODETRANSFER_CONFIG, 0)?;
    write(c, r::VSH_CODETRANSFER_DATA, 15, shader::CODE, false)?;
    one(c, r::VSH_CODETRANSFER_END, 1)?;
    one(c, r::VSH_OPDESCS_CONFIG, 0)?;
    write(c, r::VSH_OPDESCS_DATA, 15, shader::DESCRIPTORS, false)?;
    one(c, r::VSH_ENTRYPOINT, 0x7fff0000 | shader::ENTRY)?;
    one(c, r::VSH_OUTMAP_MASK, 3)?;
    one(c, r::VSH_OUTMAP_TOTAL1, 1)?;
    one(c, r::VSH_OUTMAP_TOTAL2, 1)?;
    masked(c, r::PRIMITIVE_CONFIG, 1, 1)?;
    seq(c, r::SH_OUTMAP_TOTAL, &[2, 0x03020100, 0x0b0a0908,
        0x1f1f1f1f, 0x1f1f1f1f, 0x1f1f1f1f, 0x1f1f1f1f, 0x1f1f1f1f])?;
    one(c, r::SH_OUTATTR_MODE, 0)?;
    one(c, r::SH_OUTATTR_CLOCK, 3)?;
    masked(c, r::GEOSTAGE_CONFIG, 10, 0)?;
    one(c, r::GSH_MISC0, 0)?;
    one(c, r::GSH_MISC1, 0)?;
    one(c, r::GSH_INPUTBUFFER_CONFIG, 0xa0000000)?;
    // v0/v1: float4 position, float4 color, 32-byte stride.
    seq(c, r::ATTRIBBUFFERS_FORMAT_LOW, &[0xff, 0x1ffc0000])?;
    masked(c, r::VSH_INPUTBUFFER_CONFIG, 11, 0xa0000001)?;
    one(c, r::VSH_NUM_ATTR, 1)?;
    seq(c, r::VSH_ATTRIBUTES_PERMUTATION_LOW, &[0x10, 0])?;
    one(c, r::ATTRIBBUFFERS_LOC, vertices >> 3)?;
    let mut buffers = [0; 36];
    buffers[1] = 0x10;
    buffers[2] = 0x20200000;
    seq(c, r::ATTRIBBUFFER0_OFFSET, &buffers)?;
    finish(c)
}

pub fn begin(c: &mut Commands<'_>) -> Result<(), ()> {
    one(c, r::FRAMEBUFFER_INVALIDATE, 1)
}

pub fn draw(c: &mut Commands<'_>, first: u32, count: u32) -> Result<(), ()> {
    // Reserve the whole draw and the eventual finish before touching the buffer.
    if c.words.len() - c.used < DRAW_WORDS + FINISH_WORDS { return Err(()); }
    masked(c, r::PRIMITIVE_CONFIG, 2, 0)?;
    one(c, r::RESTART_PRIMITIVE, 1)?;
    one(c, r::INDEXBUFFER_CONFIG, 0x80000000)?;
    one(c, r::NUMVERTICES, count)?;
    one(c, r::VERTEX_OFFSET, first)?;
    masked(c, r::GEOSTAGE_CONFIG2, 1, 1)?;
    masked(c, r::START_DRAW_FUNC0, 1, 0)?;
    one(c, r::DRAWARRAYS, 1)?;
    masked(c, r::START_DRAW_FUNC0, 1, 1)?;
    masked(c, r::GEOSTAGE_CONFIG2, 1, 0)?;
    one(c, r::VTX_FUNC, 1)
}

pub fn finish(c: &mut Commands<'_>) -> Result<(), ()> {
    if c.words.len() - c.used < FINISH_WORDS { return Err(()); }
    one(c, r::FRAMEBUFFER_FLUSH, 1)?;
    one(c, r::FRAMEBUFFER_INVALIDATE, 1)?;
    one(c, r::EARLYDEPTH_CLEAR, 1)?;
    one(c, r::FINALIZE, 0x12345678)?;
    if c.used % 4 != 0 { one(c, r::FINALIZE, 0x12345678)?; }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
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
