# c3dgl: OpenGL 1.1 subset for the Nintendo 3DS on citro3d.
# Standalone: depends only on libctru and citro3d. Link `c3dgl::c3dgl`, then
# #include <GL/gl.h> for the GL API and <c3dgl.h> for setup/buffer swap.
cmake_minimum_required(VERSION 3.13)

# Built on its own: use the devkitPro 3DS toolchain (must happen before project())
if((CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) AND NOT CMAKE_TOOLCHAIN_FILE)
    if(NOT DEFINED ENV{DEVKITPRO})
        message(FATAL_ERROR "DEVKITPRO is not set. Install devkitPro or pass -DCMAKE_TOOLCHAIN_FILE=.../cmake/3DS.cmake")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/3DS.cmake" CACHE PATH "devkitPro 3DS toolchain")
endif()

project(c3dgl LANGUAGES C CXX ASM)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(C3DGL_TOP_LEVEL ON)
else()
    set(C3DGL_TOP_LEVEL OFF)
endif()
option(C3DGL_BUILD_EXAMPLES "Build the c3dgl examples" ${C3DGL_TOP_LEVEL})

# Vertex shader, embedded as c3dgl_vsh_shbin.h
ctr_add_shader_library(c3dgl_vsh shaders/c3dgl.v.pica)
dkp_add_embedded_binary_library(c3dgl_shaders c3dgl_vsh)

add_library(c3dgl STATIC src/c3dgl.c)
add_library(c3dgl::c3dgl ALIAS c3dgl)

target_include_directories(c3dgl PUBLIC include)
target_link_libraries(c3dgl PRIVATE c3dgl_shaders citro3d)
target_compile_options(c3dgl PRIVATE -O2 -Wall -Wextra)
set_target_properties(c3dgl PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)

if(C3DGL_BUILD_EXAMPLES)
    add_subdirectory(examples/cube)
    add_subdirectory(examples/primitives)
    add_subdirectory(examples/fragment)
    add_subdirectory(examples/api)
endif()
