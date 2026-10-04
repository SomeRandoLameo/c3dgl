add_executable(c3dgl_accum main.cpp)
set_target_properties(c3dgl_accum PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(c3dgl_accum PRIVATE -Wall -Wextra -O2)
target_link_libraries(c3dgl_accum PRIVATE c3dgl::c3dgl c3dgl::glu citro3d)

ctr_generate_smdh(c3dgl_accum.smdh
    NAME        "c3dgl accum"
    DESCRIPTION "OpenGL 1.1 on citro3d: the accumulation buffer"
    AUTHOR      "c3dgl"
)
ctr_create_3dsx(c3dgl_accum SMDH c3dgl_accum.smdh)
