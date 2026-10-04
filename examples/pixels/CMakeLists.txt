add_executable(c3dgl_pixels main.cpp)
set_target_properties(c3dgl_pixels PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(c3dgl_pixels PRIVATE -Wall -Wextra -O2)
target_link_libraries(c3dgl_pixels PRIVATE c3dgl::c3dgl citro3d)

ctr_generate_smdh(c3dgl_pixels.smdh
    NAME        "c3dgl pixels"
    DESCRIPTION "OpenGL 1.1 on citro3d: raster position, bitmaps, glDrawPixels, glCopyPixels"
    AUTHOR      "c3dgl"
)
ctr_create_3dsx(c3dgl_pixels SMDH c3dgl_pixels.smdh)
