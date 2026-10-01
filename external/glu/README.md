# Mesa GLU 9.0.3 (unmodified)

The OpenGL Utility Library as released by Mesa, used by c3dgl as its GLU (`c3dgl::glu`, `#include <GL/glu.h>`).

- Source: https://archive.mesa3d.org/glu/glu-9.0.3.tar.xz
  (sha256 `bd43fe12f374b1192eb15fe20e45ff456b9bc26ab57f0eee919f96ca0f8a330f`),
  upstream https://gitlab.freedesktop.org/mesa/glu
- Copied: `include/` and `src/` without changes, except that `src/meson.build` is replaced by c3dgl's CMakeLists.txt.
- License: SGI Free Software License B, Version 2.0 (MIT style), see the header of each source file.

To update, replace `include/` and `src/` with a newer release and adjust the source list in c3dgl's CMakeLists.txt.
