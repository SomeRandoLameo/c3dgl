add_executable(c3dgl_glu_example main.cpp)
set_target_properties(c3dgl_glu_example PROPERTIES OUTPUT_NAME c3dgl_glu)
set_target_properties(c3dgl_glu_example PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(c3dgl_glu_example PRIVATE -Wall -Wextra -O2)
target_link_libraries(c3dgl_glu_example PRIVATE c3dgl::c3dgl c3dgl::glu)

ctr_generate_smdh(c3dgl_glu.smdh
    NAME        "c3dgl glu"
    DESCRIPTION "OpenGL 1.1 on citro3d: GLU matrices, images, quadrics"
    AUTHOR      "c3dgl"
)
ctr_create_3dsx(c3dgl_glu_example SMDH c3dgl_glu.smdh)
