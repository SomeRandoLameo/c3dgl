add_executable(c3dgl_fog main.cpp)
set_target_properties(c3dgl_fog PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(c3dgl_fog PRIVATE -Wall -Wextra -O2)
target_link_libraries(c3dgl_fog PRIVATE c3dgl::c3dgl)

ctr_generate_smdh(c3dgl_fog.smdh
    NAME        "c3dgl fog"
    DESCRIPTION "OpenGL 1.1 on citro3d: fog (linear, exp, exp2)"
    AUTHOR      "c3dgl"
)
ctr_create_3dsx(c3dgl_fog SMDH c3dgl_fog.smdh)
