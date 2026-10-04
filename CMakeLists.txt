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
target_link_libraries(c3dgl PRIVATE c3dgl_shaders citro3d PUBLIC m)
target_compile_options(c3dgl PRIVATE -O2 -Wall -Wextra)
set_target_properties(c3dgl PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)

# GLU: Mesa GLU 9.0.3, unmodified (external/glu/README.md). Link c3dgl::glu for <GL/glu.h>
option(C3DGL_BUILD_GLU "Build GLU (c3dgl::glu)" ON)
if(C3DGL_BUILD_GLU)
    set(C3DGL_GLU_SOURCES
    libutil/error.c
    libutil/glue.c
    libutil/mipmap.c
    libutil/project.c
    libutil/quad.c
    libutil/registry.c
    libtess/dict.c
    libtess/geom.c
    libtess/memalloc.c
    libtess/mesh.c
    libtess/normal.c
    libtess/priorityq.c
    libtess/render.c
    libtess/sweep.c
    libtess/tess.c
    libtess/tessmono.c
    libnurbs/interface/bezierEval.cc
    libnurbs/interface/bezierPatch.cc
    libnurbs/interface/bezierPatchMesh.cc
    libnurbs/interface/glcurveval.cc
    libnurbs/interface/glinterface.cc
    libnurbs/interface/glrenderer.cc
    libnurbs/interface/glsurfeval.cc
    libnurbs/interface/incurveeval.cc
    libnurbs/interface/insurfeval.cc
    libnurbs/internals/arc.cc
    libnurbs/internals/arcsorter.cc
    libnurbs/internals/arctess.cc
    libnurbs/internals/backend.cc
    libnurbs/internals/basiccrveval.cc
    libnurbs/internals/basicsurfeval.cc
    libnurbs/internals/bin.cc
    libnurbs/internals/bufpool.cc
    libnurbs/internals/cachingeval.cc
    libnurbs/internals/ccw.cc
    libnurbs/internals/coveandtiler.cc
    libnurbs/internals/curve.cc
    libnurbs/internals/curvelist.cc
    libnurbs/internals/curvesub.cc
    libnurbs/internals/dataTransform.cc
    libnurbs/internals/displaylist.cc
    libnurbs/internals/flist.cc
    libnurbs/internals/flistsorter.cc
    libnurbs/internals/hull.cc
    libnurbs/internals/intersect.cc
    libnurbs/internals/knotvector.cc
    libnurbs/internals/mapdesc.cc
    libnurbs/internals/mapdescv.cc
    libnurbs/internals/maplist.cc
    libnurbs/internals/mesher.cc
    libnurbs/internals/monoTriangulationBackend.cc
    libnurbs/internals/monotonizer.cc
    libnurbs/internals/mycode.cc
    libnurbs/internals/nurbsinterfac.cc
    libnurbs/internals/nurbstess.cc
    libnurbs/internals/patch.cc
    libnurbs/internals/patchlist.cc
    libnurbs/internals/quilt.cc
    libnurbs/internals/reader.cc
    libnurbs/internals/renderhints.cc
    libnurbs/internals/slicer.cc
    libnurbs/internals/sorter.cc
    libnurbs/internals/splitarcs.cc
    libnurbs/internals/subdivider.cc
    libnurbs/internals/tobezier.cc
    libnurbs/internals/trimline.cc
    libnurbs/internals/trimregion.cc
    libnurbs/internals/trimvertpool.cc
    libnurbs/internals/uarray.cc
    libnurbs/internals/varray.cc
    libnurbs/nurbtess/directedLine.cc
    libnurbs/nurbtess/gridWrap.cc
    libnurbs/nurbtess/monoChain.cc
    libnurbs/nurbtess/monoPolyPart.cc
    libnurbs/nurbtess/monoTriangulation.cc
    libnurbs/nurbtess/partitionX.cc
    libnurbs/nurbtess/partitionY.cc
    libnurbs/nurbtess/polyDBG.cc
    libnurbs/nurbtess/polyUtil.cc
    libnurbs/nurbtess/primitiveStream.cc
    libnurbs/nurbtess/quicksort.cc
    libnurbs/nurbtess/rectBlock.cc
    libnurbs/nurbtess/sampleComp.cc
    libnurbs/nurbtess/sampleCompBot.cc
    libnurbs/nurbtess/sampleCompRight.cc
    libnurbs/nurbtess/sampleCompTop.cc
    libnurbs/nurbtess/sampleMonoPoly.cc
    libnurbs/nurbtess/sampledLine.cc
    libnurbs/nurbtess/searchTree.cc
    libnurbs/internals
    libnurbs/interface
    libnurbs/nurbtess
    )
    list(TRANSFORM C3DGL_GLU_SOURCES PREPEND external/glu/src/)
    add_library(c3dgl_glu STATIC ${C3DGL_GLU_SOURCES})
    add_library(c3dgl::glu ALIAS c3dgl_glu)
    target_include_directories(c3dgl_glu PUBLIC external/glu/include
        PRIVATE external/glu/src/include external/glu/src/libnurbs/internals external/glu/src/libnurbs/interface
                external/glu/src/libnurbs/nurbtess)
    target_compile_definitions(c3dgl_glu PRIVATE LIBRARYBUILD)
    # Third-party code: build it as upstream does, without c3dgl's warning flags
    target_compile_options(c3dgl_glu PRIVATE -O2 -w)
    target_link_libraries(c3dgl_glu PUBLIC c3dgl)
endif()

if(C3DGL_BUILD_EXAMPLES)
    add_subdirectory(examples/cube)
    add_subdirectory(examples/primitives)
    add_subdirectory(examples/fragment)
    add_subdirectory(examples/api)
    add_subdirectory(examples/glu)
    add_subdirectory(examples/texture)
    add_subdirectory(examples/lighting)
    add_subdirectory(examples/fog)
    add_subdirectory(examples/clipplane)
    add_subdirectory(examples/points)
    add_subdirectory(examples/lists)
    add_subdirectory(examples/texgen)
    add_subdirectory(examples/pixels)
endif()
