# Standalone downstream project: cmake -S examples/cpp -B build-example-cpp \
#     -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake && cmake --build build-example-cpp
cmake_minimum_required(VERSION 3.18)
if(NOT CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{DEVKITPRO})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/3DS.cmake" CACHE FILEPATH "3DS toolchain")
endif()
project(c3dgl_example_cpp LANGUAGES C CXX)

add_subdirectory(../.. c3dgl)

add_executable(c3dgl_example_cpp main.cpp)
target_link_libraries(c3dgl_example_cpp PRIVATE c3dgl::native)
target_compile_options(c3dgl_example_cpp PRIVATE -O2 -Wall -Wextra -Werror)
set_target_properties(c3dgl_example_cpp PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
ctr_create_3dsx(c3dgl_example_cpp)
