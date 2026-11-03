add_executable(c3dgl_lighting main.cpp)
set_target_properties(c3dgl_lighting PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(c3dgl_lighting PRIVATE -Wall -Wextra -O2)
target_link_libraries(c3dgl_lighting PRIVATE c3dgl::c3dgl citro3d)

ctr_generate_smdh(c3dgl_lighting.smdh
    NAME        "c3dgl lighting"
    DESCRIPTION "OpenGL 1.1 on citro3d: lighting (lights, materials, color material, two-sided)"
    AUTHOR      "c3dgl"
)
ctr_create_3dsx(c3dgl_lighting SMDH c3dgl_lighting.smdh)
