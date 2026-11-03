# GPU protocol references

The Rust encoder and shader are independent implementations. Register names,
float layouts, shader configuration, command-list finalization and required draw
ordering were checked against these official zlib-licensed sources:

- [Citro3D](https://github.com/devkitPro/citro3d/tree/9f21cf7b380ce6f9e01a0420f19f0763e5443ca7),
  especially `source/base.c`, `framebuffer.c`, `drawArrays.c`, `effect.c`,
  `attribs.c`, `buffers.c`, `texenv.c` and `renderqueue.c`.
- [libctru](https://github.com/devkitPro/libctru/tree/9b55eda44cf80b971503991e0c78f9bc8fe50425),
  especially `gpu/gpu.c`, `gpu/shaderProgram.c`, `gpu/gxqueue.c` and
  `include/3ds/gpu/registers.h`.

This is not an upstream implementation or a bundled copy of either renderer.
Applicable license notices are retained alongside this file. The libctru runtime
adapter links the application's installed libctru. Native executables contain no
Citro3D, GPUCMD, DVLB or shaderProgram helper symbols.
