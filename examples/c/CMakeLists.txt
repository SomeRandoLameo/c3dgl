# C examples for the C ABI of the Rust renderer. Build:
#   cmake -S examples/c -B build-example-c -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake
#   cmake --build build-example-c
cmake_minimum_required(VERSION 3.18)
if(NOT CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{DEVKITPRO})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/3DS.cmake" CACHE FILEPATH "3DS toolchain")
endif()
project(c3dgl_examples_c LANGUAGES C)

add_subdirectory(../.. c3dgl)

foreach(example api)
    set(target c3dgl_example_${example})
    add_executable(${target} ${example}/main.c)
    set_target_properties(${target} PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)
    target_compile_options(${target} PRIVATE -O2 -Wall -Wextra)
    target_link_libraries(${target} PRIVATE c3dgl::native m)
    ctr_create_3dsx(${target})
endforeach()
