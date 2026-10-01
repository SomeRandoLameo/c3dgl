# Plain C on purpose: also checks that the headers work without C++
add_executable(c3dgl_api main.c)
set_target_properties(c3dgl_api PROPERTIES C_STANDARD 99 C_EXTENSIONS ON)
target_compile_options(c3dgl_api PRIVATE -Wall -Wextra -O2)
target_link_libraries(c3dgl_api PRIVATE c3dgl::c3dgl m)

ctr_generate_smdh(c3dgl_api.smdh
    NAME        "c3dgl api"
    DESCRIPTION "OpenGL 1.1 on citro3d: queries, errors, entry points"
    AUTHOR      "c3dgl"
)
ctr_create_3dsx(c3dgl_api SMDH c3dgl_api.smdh)
