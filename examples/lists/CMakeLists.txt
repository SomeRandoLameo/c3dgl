add_executable(c3dgl_lists main.cpp)
set_target_properties(c3dgl_lists PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(c3dgl_lists PRIVATE -Wall -Wextra -O2)
target_link_libraries(c3dgl_lists PRIVATE c3dgl::c3dgl citro3d)

ctr_generate_smdh(c3dgl_lists.smdh
    NAME        "c3dgl lists"
    DESCRIPTION "OpenGL 1.1 on citro3d: display lists (gears)"
    AUTHOR      "c3dgl"
)
ctr_create_3dsx(c3dgl_lists SMDH c3dgl_lists.smdh)
