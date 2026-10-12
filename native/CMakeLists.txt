# Include with add_subdirectory(native) in a devkitPro 3DS project.
cmake_minimum_required(VERSION 3.18)
project(c3dgl_native LANGUAGES C)
find_program(C3DGL_CARGO cargo REQUIRED)
set(C3DGL_RUST_TARGET armv6k-nintendo-3ds)
set(C3DGL_RUST_OUT "${CMAKE_CURRENT_BINARY_DIR}/cargo")
set(C3DGL_ARCHIVE "${C3DGL_RUST_OUT}/${C3DGL_RUST_TARGET}/release/libc3dgl.a")
add_custom_command(OUTPUT "${C3DGL_ARCHIVE}"
    COMMAND ${CMAKE_COMMAND} -E env "CARGO_TARGET_DIR=${C3DGL_RUST_OUT}"
        ${C3DGL_CARGO} build --release --target ${C3DGL_RUST_TARGET}
        -Zbuild-std=core,alloc --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS Cargo.toml rust-toolchain.toml src/lib.rs src/platform.rs src/gpu.rs
        src/renderer.rs src/registers.rs src/shader.rs
    VERBATIM)
add_custom_target(c3dgl_native_build ALL DEPENDS "${C3DGL_ARCHIVE}")
add_library(c3dgl_native_rust STATIC IMPORTED GLOBAL)
set_target_properties(c3dgl_native_rust PROPERTIES IMPORTED_LOCATION "${C3DGL_ARCHIVE}")
add_dependencies(c3dgl_native_rust c3dgl_native_build)
add_library(c3dgl_native_platform STATIC platform/libctru.c)
add_library(c3dgl_native INTERFACE)
add_library(c3dgl::native ALIAS c3dgl_native)
target_include_directories(c3dgl_native INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(c3dgl_native INTERFACE c3dgl_native_rust c3dgl_native_platform ctru m)

option(C3DGL_NATIVE_BUILD_ABI_TEST "Build a C link test (not a triangle demo)" ON)
if(C3DGL_NATIVE_BUILD_ABI_TEST)
    add_executable(c3dgl_native_abi tests/abi.c)
    target_link_libraries(c3dgl_native_abi PRIVATE c3dgl::native)
    target_compile_options(c3dgl_native_abi PRIVATE -Wall -Wextra -Werror)
endif()

option(C3DGL_NATIVE_BUILD_TRIANGLE "Build GPU triangle and benchmark" ON)
if(C3DGL_NATIVE_BUILD_TRIANGLE)
    add_executable(c3dgl_native_triangle examples/triangle.c)
    target_link_libraries(c3dgl_native_triangle PRIVATE c3dgl::native)
    target_compile_options(c3dgl_native_triangle PRIVATE -O2 -Wall -Wextra -Werror)
    ctr_create_3dsx(c3dgl_native_triangle)
endif()
