# Standalone downstream project: cmake -S examples/c -B build-example-c \
#     -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake && cmake --build build-example-c
cmake_minimum_required(VERSION 3.18)
if(NOT CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{DEVKITPRO})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/3DS.cmake" CACHE FILEPATH "3DS toolchain")
endif()
project(c3dgl_example_c LANGUAGES C)

# c3dgl as a subdirectory; its own tests and demos stay off when not top level.
add_subdirectory(../.. c3dgl)

add_executable(c3dgl_example_c main.c)
target_link_libraries(c3dgl_example_c PRIVATE c3dgl::native)
target_compile_options(c3dgl_example_c PRIVATE -O2 -Wall -Wextra -Werror)
ctr_create_3dsx(c3dgl_example_c)
