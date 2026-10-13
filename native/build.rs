use std::{env, path::PathBuf, process::Command};
fn main() {
    println!("cargo:rerun-if-changed=platform/libctru.c");
    println!("cargo:rerun-if-env-changed=DEVKITPRO");
    println!("cargo:rerun-if-env-changed=DEVKITARM");
    if env::var_os("CARGO_FEATURE_RUST3DS").is_none() {
        return;
    }
    assert_eq!(
        env::var("CARGO_CFG_TARGET_OS").unwrap(),
        "horizon",
        "rust3ds requires the armv6k-nintendo-3ds target"
    );
    let dkp = PathBuf::from(env::var("DEVKITPRO").expect("Set DEVKITPRO"));
    let arm = env::var_os("DEVKITARM")
        .map(PathBuf::from)
        .unwrap_or_else(|| dkp.join("devkitARM"));
    let out = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let status = Command::new(arm.join("bin/arm-none-eabi-gcc"))
        .args([
            "-c",
            "platform/libctru.c",
            "-O2",
            "-std=gnu11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-march=armv6k",
            "-mtune=mpcore",
            "-mfloat-abi=hard",
            "-mfpu=vfp",
            "-mtp=soft",
            "-DARM11",
            "-D__3DS__",
        ])
        .arg("-I")
        .arg(dkp.join("libctru/include"))
        .arg("-o")
        .arg(out.join("platform.o"))
        .status()
        .expect("devkitARM gcc");
    assert!(status.success(), "platform C compilation failed");
    let status = Command::new(arm.join("bin/arm-none-eabi-ar"))
        .arg("crs")
        .arg(out.join("libc3dgl_native_platform.a"))
        .arg(out.join("platform.o"))
        .status()
        .expect("devkitARM ar");
    assert!(status.success(), "platform archive failed");
    println!("cargo:rustc-link-search=native={}", out.display());
    println!(
        "cargo:rustc-link-search=native={}",
        dkp.join("libctru/lib").display()
    );
    println!("cargo:rustc-link-lib=static=c3dgl_native_platform");
    println!("cargo:rustc-link-lib=static=ctru");
    println!("cargo:rustc-link-lib=m");
}
