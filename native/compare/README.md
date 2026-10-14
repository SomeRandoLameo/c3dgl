# Existing examples as the compatibility oracle

Build the same capture entry point twice, linking the old C renderer or the native
Rust renderer. `old_examples.cpp` includes the existing `examples/*/main.cpp`
directly and invokes its existing scene/texture functions. No geometry, texture
patterns or drawing algorithms are copied or replaced. Original example files
remain unchanged. Both consumers use the original GL declarations; native linkage
contains no Citro3D/GPU encoder symbols. Unsupported code is discarded by section
GC, with no GL stubs or fallback renderer.

The interactive original main and unsupported pages are not run yet. This is a
1:1 comparison of selected original scenes, not full example compatibility:

| Example | Exact original functions currently selected | Remaining full-example gaps |
|---|---|---|
| primitives, primitives2, primitives3 | all three complete pages of the original main loop, call for call | line stipple/smooth lines, point sprites, edge-flag arrays, printed pages and input |
| texture | `createF(16)`, `drawCell(0,0,texture)` | multitexturing/combine, mipmaps, compression |
| multitexture | complete original `drawMultitexturePage`, eight cells | mipmap and compression pages remain separate |
| mipmap | complete original `drawMipmapPage`, eight floor cells (levels, trilinear, GL_GENERATE_MIPMAP, RGB565) | compressed page remains separate |
| compressed | complete original `drawCompressedPage` (paletted, ETC1, alpha test) | none for this example file |
| overlay | complete original `drawOverlay` (scissor, GL_LINES) | rest of the cube program (PNG, stats, input) |
| lighting | the eight cells of the original main loop (diffuse, specular, point and spot light, three lights, color material, two-sided, flat+scaled) | the parameter, error and attribute self-tests (need GLES fixed-point calls) |
| texgen | the first frame of the original loop (`setupScene()` and `drawFrame()`, extracted from main unchanged) | scrolling, input and statistics |
| fog | the eight cells of the original main loop | API self-test (needs GLES fixed-point calls) |
| fragment, fragment2 | both pages of the original main loop | none for this example |
| texcoords | complete original `drawTexcoordPage(createF(16))`, all eight cells | subsequent multitexture, mipmap and compression pages remain separate |
| texmatrix | complete original first-page body in `drawTextureMatrixPage`, eight cells at time=0 | subsequent pages remain separate |
| cube | `createCheckerTexture()`, `drawCube(texture,0,0,0,200,240,true)` | full main also uses PNG/ROMFS, scissor, lines and Citro3D statistics |

The C renderer uses `C3DGL_W_DEPTH=0` to select the same Z-depth convention as Rust.
The common entry point supplies initialization, deterministic inputs/capture,
GL-error checks and cleanup. It does not implement drawing features. Original
source, adapter and both ROM hashes are recorded. Build-time source manifests are
checked before running so modified sources require a rebuild.

```sh
cmake -S native/compare -B build-native-old-examples \
  -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/3DS.cmake"
cmake --build build-native-old-examples
python3 native/scripts/compare_old_examples.py \
  --emulator /path/to/azahar --sdmc /path/to/sdmc
```

Default: five pairs, one run per backend/case (ten launches, no repetitions).
`--case texture` limits it to one pair. The exact-match gate returns nonzero on
any GL error or pixel difference; one-channel-level differences are reported,
not silently accepted. Original displayed GPU images and guest reports are kept.
`compatibility.json` lists missing symbols for all old examples; a symbol being
present does not establish support for every argument/enum/page.

Use the original examples as the primary acceptance scenes for further GL groups.
Expand this shared selection when features become supported, then run full original
pages. Small isolated host/packet regressions and prior native demos remain useful
for diagnosis, but do not replace the original-source comparisons.
