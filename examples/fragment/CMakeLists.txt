add_executable(c3dgl_fragment main.cpp)
set_target_properties(c3dgl_fragment PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(c3dgl_fragment PRIVATE -Wall -Wextra -O2)
target_link_libraries(c3dgl_fragment PRIVATE c3dgl::c3dgl citro3d)

ctr_generate_smdh(c3dgl_fragment.smdh
    NAME        "c3dgl fragment"
    DESCRIPTION "OpenGL 1.1 on citro3d: fragment operations (alpha test, ...)"
    AUTHOR      "c3dgl"
)
ctr_create_3dsx(c3dgl_fragment SMDH c3dgl_fragment.smdh)
