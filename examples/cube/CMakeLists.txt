add_executable(c3dgl_cube main.cpp)
set_target_properties(c3dgl_cube PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(c3dgl_cube PRIVATE -Wall -Wextra -O2)
target_link_libraries(c3dgl_cube PRIVATE c3dgl::c3dgl)

ctr_generate_smdh(c3dgl_cube.smdh
    NAME        "c3dgl cube"
    DESCRIPTION "OpenGL 1.1 on citro3d: cube, scissor, lines"
    AUTHOR      "c3dgl"
)
ctr_create_3dsx(c3dgl_cube SMDH c3dgl_cube.smdh)
