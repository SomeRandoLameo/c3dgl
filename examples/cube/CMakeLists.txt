add_executable(c3dgl_cube main.cpp)
set_target_properties(c3dgl_cube PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(c3dgl_cube PRIVATE -Wall -Wextra -O2)
# libpng from the devkitPro portlibs (3ds-libpng) loads the image from romfs
find_package(PNG REQUIRED)
target_link_libraries(c3dgl_cube PRIVATE c3dgl::c3dgl PNG::PNG citro3d)

ctr_generate_smdh(c3dgl_cube.smdh
    NAME        "c3dgl cube"
    DESCRIPTION "OpenGL 1.1 on citro3d: cubes, PNG texture, scissor, lines"
    AUTHOR      "c3dgl"
)
# romfs/ is packed into the .3dsx (romfs:/tonk.png)
dkp_add_asset_target(c3dgl_cube_romfs romfs)
dkp_track_assets(c3dgl_cube_romfs FILES tonk.png)

ctr_create_3dsx(c3dgl_cube SMDH c3dgl_cube.smdh ROMFS c3dgl_cube_romfs)
