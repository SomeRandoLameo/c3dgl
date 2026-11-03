//! ARMv6 SIMD/DSP instructions on pairs of signed 16-bit values packed in a word (low half first),
//! with portable equivalents for host builds. Each function computes exactly what its instruction
//! does.

/// Saturating add of both halves (QADD16).
#[inline(always)]
pub(crate) fn qadd16(a: u32, b: u32) -> u32 {
    #[cfg(target_arch = "arm")]
    unsafe {
        let r: u32;
        core::arch::asm!("qadd16 {r}, {a}, {b}", r = lateout(reg) r, a = in(reg) a, b = in(reg) b,
            options(pure, nomem, nostack, preserves_flags));
        r
    }
    #[cfg(not(target_arch = "arm"))]
    {
        let (al, ah) = (a as u16 as i16 as i32, (a >> 16) as u16 as i16 as i32);
        let (bl, bh) = (b as u16 as i16 as i32, (b >> 16) as u16 as i16 as i32);
        (al + bl).clamp(-32768, 32767) as u16 as u32 | ((ah + bh).clamp(-32768, 32767) as u32) << 16
    }
}

/// (a * low half of b) >> 16, the top 32 bits of the 48-bit product (SMULWB).
#[inline(always)]
pub(crate) fn smulwb(a: i32, b: u32) -> i32 {
    #[cfg(target_arch = "arm")]
    unsafe {
        let r: i32;
        core::arch::asm!("smulwb {r}, {a}, {b}", r = lateout(reg) r, a = in(reg) a, b = in(reg) b,
            options(pure, nomem, nostack, preserves_flags));
        r
    }
    #[cfg(not(target_arch = "arm"))]
    {
        ((a as i64 * b as u16 as i16 as i64) >> 16) as i32
    }
}

/// (a * high half of b) >> 16 (SMULWT).
#[inline(always)]
pub(crate) fn smulwt(a: i32, b: u32) -> i32 {
    #[cfg(target_arch = "arm")]
    unsafe {
        let r: i32;
        core::arch::asm!("smulwt {r}, {a}, {b}", r = lateout(reg) r, a = in(reg) a, b = in(reg) b,
            options(pure, nomem, nostack, preserves_flags));
        r
    }
    #[cfg(not(target_arch = "arm"))]
    {
        ((a as i64 * (b >> 16) as u16 as i16 as i64) >> 16) as i32
    }
}

/// ((a * low half of b) >> 16) + acc (SMLAWB).
#[inline(always)]
pub(crate) fn smlawb(a: i32, b: u32, acc: i32) -> i32 {
    #[cfg(target_arch = "arm")]
    unsafe {
        let r: i32;
        core::arch::asm!("smlawb {r}, {a}, {b}, {c}", r = lateout(reg) r, a = in(reg) a, b = in(reg) b,
            c = in(reg) acc, options(pure, nomem, nostack, preserves_flags));
        r
    }
    #[cfg(not(target_arch = "arm"))]
    {
        ((a as i64 * b as u16 as i16 as i64) >> 16) as i32 + acc
    }
}

/// ((a * high half of b) >> 16) + acc (SMLAWT).
#[inline(always)]
pub(crate) fn smlawt(a: i32, b: u32, acc: i32) -> i32 {
    #[cfg(target_arch = "arm")]
    unsafe {
        let r: i32;
        core::arch::asm!("smlawt {r}, {a}, {b}, {c}", r = lateout(reg) r, a = in(reg) a, b = in(reg) b,
            c = in(reg) acc, options(pure, nomem, nostack, preserves_flags));
        r
    }
    #[cfg(not(target_arch = "arm"))]
    {
        ((a as i64 * (b >> 16) as u16 as i16 as i64) >> 16) as i32 + acc
    }
}

/// Clamp to a signed 16-bit value (SSAT #16).
#[inline(always)]
pub(crate) fn ssat16(v: i32) -> i32 {
    #[cfg(target_arch = "arm")]
    unsafe {
        let r: i32;
        core::arch::asm!("ssat {r}, #16, {v}", r = lateout(reg) r, v = in(reg) v,
            options(pure, nomem, nostack));
        r
    }
    #[cfg(not(target_arch = "arm"))]
    {
        v.clamp(-32768, 32767)
    }
}

/// (v >> 8) clamped to 0..=255 (USAT #8, ASR #8).
#[inline(always)]
pub(crate) fn usat8_asr8(v: i32) -> u32 {
    #[cfg(target_arch = "arm")]
    unsafe {
        let r: u32;
        core::arch::asm!("usat {r}, #8, {v}, asr #8", r = lateout(reg) r, v = in(reg) v,
            options(pure, nomem, nostack));
        r
    }
    #[cfg(not(target_arch = "arm"))]
    {
        (v >> 8).clamp(0, 255) as u32
    }
}

/// The correctly rounded square root (VSQRT on the 3DS: the same value as libm's sqrtf without
/// the call).
#[inline(always)]
pub(crate) fn sqrt(x: f32) -> f32 {
    #[cfg(target_arch = "arm")]
    unsafe {
        let r: f32;
        core::arch::asm!("vsqrt.f32 {r}, {x}", r = lateout(sreg) r, x = in(sreg) x,
            options(pure, nomem, nostack));
        r
    }
    #[cfg(not(target_arch = "arm"))]
    {
        extern "C" {
            fn sqrtf(x: f32) -> f32;
        }
        unsafe { sqrtf(x) }
    }
}

/// Two 16-bit values in one word, `low` in the low half.
#[inline(always)]
pub(crate) fn pack(low: i32, high: i32) -> u32 {
    low as u16 as u32 | (high as u32) << 16
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn halves_saturate_and_multiply_like_the_instructions() {
        assert_eq!(
            qadd16(pack(32000, -32000), pack(1000, -1000)),
            pack(32767, -32768)
        );
        assert_eq!(qadd16(pack(-5, 7), pack(3, -9)), pack(-2, -2));
        let w = pack(-32768, 12345);
        assert_eq!(smulwb(65536, w), -32768);
        assert_eq!(smulwt(-131072, w), -24690);
        assert_eq!(
            smulwb(i32::MAX, pack(32767, 0)),
            ((i32::MAX as i64 * 32767) >> 16) as i32
        );
        assert_eq!(ssat16(40000), 32767);
        assert_eq!(ssat16(-40000), -32768);
        assert_eq!(usat8_asr8(-5), 0);
        assert_eq!(usat8_asr8(255 * 256 + 255), 255);
        assert_eq!(usat8_asr8(70000), 255);
        assert_eq!(usat8_asr8(1000), 3);
    }
}
