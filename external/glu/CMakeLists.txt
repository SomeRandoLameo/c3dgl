# Mesa GLU 9.0.3, unmodified (see README.md), on top of the Rust renderer: the <GL/glu.h> API for 3DS projects.
# Link c3dgl::glu. Add with add_subdirectory(external/glu) after c3dgl::native exists.
set(C3DGL_GLU_SOURCES
    src/libutil/error.c
    src/libutil/glue.c
    src/libutil/mipmap.c
    src/libutil/project.c
    src/libutil/quad.c
    src/libutil/registry.c
    src/libtess/dict.c
    src/libtess/geom.c
    src/libtess/memalloc.c
    src/libtess/mesh.c
    src/libtess/normal.c
    src/libtess/priorityq.c
    src/libtess/render.c
    src/libtess/sweep.c
    src/libtess/tess.c
    src/libtess/tessmono.c
    src/libnurbs/interface/bezierEval.cc
    src/libnurbs/interface/bezierPatch.cc
    src/libnurbs/interface/bezierPatchMesh.cc
    src/libnurbs/interface/glcurveval.cc
    src/libnurbs/interface/glinterface.cc
    src/libnurbs/interface/glrenderer.cc
    src/libnurbs/interface/glsurfeval.cc
    src/libnurbs/interface/incurveeval.cc
    src/libnurbs/interface/insurfeval.cc
    src/libnurbs/internals/arc.cc
    src/libnurbs/internals/arcsorter.cc
    src/libnurbs/internals/arctess.cc
    src/libnurbs/internals/backend.cc
    src/libnurbs/internals/basiccrveval.cc
    src/libnurbs/internals/basicsurfeval.cc
    src/libnurbs/internals/bin.cc
    src/libnurbs/internals/bufpool.cc
    src/libnurbs/internals/cachingeval.cc
    src/libnurbs/internals/ccw.cc
    src/libnurbs/internals/coveandtiler.cc
    src/libnurbs/internals/curve.cc
    src/libnurbs/internals/curvelist.cc
    src/libnurbs/internals/curvesub.cc
    src/libnurbs/internals/dataTransform.cc
    src/libnurbs/internals/displaylist.cc
    src/libnurbs/internals/flist.cc
    src/libnurbs/internals/flistsorter.cc
    src/libnurbs/internals/hull.cc
    src/libnurbs/internals/intersect.cc
    src/libnurbs/internals/knotvector.cc
    src/libnurbs/internals/mapdesc.cc
    src/libnurbs/internals/mapdescv.cc
    src/libnurbs/internals/maplist.cc
    src/libnurbs/internals/mesher.cc
    src/libnurbs/internals/monoTriangulationBackend.cc
    src/libnurbs/internals/monotonizer.cc
    src/libnurbs/internals/mycode.cc
    src/libnurbs/internals/nurbsinterfac.cc
    src/libnurbs/internals/nurbstess.cc
    src/libnurbs/internals/patch.cc
    src/libnurbs/internals/patchlist.cc
    src/libnurbs/internals/quilt.cc
    src/libnurbs/internals/reader.cc
    src/libnurbs/internals/renderhints.cc
    src/libnurbs/internals/slicer.cc
    src/libnurbs/internals/sorter.cc
    src/libnurbs/internals/splitarcs.cc
    src/libnurbs/internals/subdivider.cc
    src/libnurbs/internals/tobezier.cc
    src/libnurbs/internals/trimline.cc
    src/libnurbs/internals/trimregion.cc
    src/libnurbs/internals/trimvertpool.cc
    src/libnurbs/internals/uarray.cc
    src/libnurbs/internals/varray.cc
    src/libnurbs/nurbtess/directedLine.cc
    src/libnurbs/nurbtess/gridWrap.cc
    src/libnurbs/nurbtess/monoChain.cc
    src/libnurbs/nurbtess/monoPolyPart.cc
    src/libnurbs/nurbtess/monoTriangulation.cc
    src/libnurbs/nurbtess/partitionX.cc
    src/libnurbs/nurbtess/partitionY.cc
    src/libnurbs/nurbtess/polyDBG.cc
    src/libnurbs/nurbtess/polyUtil.cc
    src/libnurbs/nurbtess/primitiveStream.cc
    src/libnurbs/nurbtess/quicksort.cc
    src/libnurbs/nurbtess/rectBlock.cc
    src/libnurbs/nurbtess/sampleComp.cc
    src/libnurbs/nurbtess/sampleCompBot.cc
    src/libnurbs/nurbtess/sampleCompRight.cc
    src/libnurbs/nurbtess/sampleCompTop.cc
    src/libnurbs/nurbtess/sampleMonoPoly.cc
    src/libnurbs/nurbtess/sampledLine.cc
    src/libnurbs/nurbtess/searchTree.cc
)
add_library(c3dgl_glu STATIC ${C3DGL_GLU_SOURCES})
add_library(c3dgl::glu ALIAS c3dgl_glu)
target_include_directories(c3dgl_glu PUBLIC include
    PRIVATE src/include src/libnurbs/internals src/libnurbs/interface src/libnurbs/nurbtess)
target_compile_definitions(c3dgl_glu PRIVATE LIBRARYBUILD)
# Third-party code: build it as upstream does, without c3dgl's warning flags
target_compile_options(c3dgl_glu PRIVATE -O2 -w -ffunction-sections -fdata-sections)
target_link_libraries(c3dgl_glu PUBLIC c3dgl::native)
