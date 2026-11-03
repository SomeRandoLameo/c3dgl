# C examples for the C ABI of the Rust renderer. Build:
#   cmake -S examples/c -B build-examples-c -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake
#   cmake --build build-examples-c
cmake_minimum_required(VERSION 3.18)
if(NOT CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{DEVKITPRO})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/3DS.cmake" CACHE FILEPATH "3DS toolchain")
endif()
# GLU (examples select and glu) is C++ code, so the project needs the C++ compiler too.
project(c3dgl_examples_c LANGUAGES C CXX ASM)

add_subdirectory(../.. c3dgl)
add_subdirectory(../../external/glu glu)
find_package(PNG REQUIRED)

foreach(example api cube primitives fragment glu texture lighting fog clipplane points lists texgen pixels select stipple accum)
    set(target c3dgl_example_${example})
    add_executable(${target} ${example}/main.c)
    set_target_properties(${target} PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)
    target_compile_options(${target} PRIVATE -O2 -Wall -Wextra -ffunction-sections -fdata-sections)
    target_link_options(${target} PRIVATE -Wl,--gc-sections)
    target_link_libraries(${target} PRIVATE c3dgl::native m)
    if(example STREQUAL cube)
        target_link_libraries(${target} PRIVATE PNG::PNG)
        # romfs/ is packed into the .3dsx (romfs:/tonk.png)
        dkp_add_asset_target(${target}_romfs ${CMAKE_CURRENT_SOURCE_DIR}/cube/romfs)
        dkp_track_assets(${target}_romfs FILES tonk.png)
        ctr_create_3dsx(${target} ROMFS ${target}_romfs)
    else()
        if(example STREQUAL select OR example STREQUAL glu)
            target_link_libraries(${target} PRIVATE c3dgl::glu)
        endif()
        ctr_create_3dsx(${target})
    endif()
endforeach()
