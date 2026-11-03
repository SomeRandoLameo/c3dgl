add_executable(c3dgl_points main.cpp)
set_target_properties(c3dgl_points PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(c3dgl_points PRIVATE -Wall -Wextra -O2)
target_link_libraries(c3dgl_points PRIVATE c3dgl::c3dgl citro3d)

ctr_generate_smdh(c3dgl_points.smdh
    NAME        "c3dgl points"
    DESCRIPTION "OpenGL ES 1.1 on citro3d: point parameters and point sprites"
    AUTHOR      "c3dgl"
)
ctr_create_3dsx(c3dgl_points SMDH c3dgl_points.smdh)
