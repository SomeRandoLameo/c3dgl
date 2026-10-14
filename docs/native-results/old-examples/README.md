# Direct old C / native Rust example comparison

The existing example sources are unchanged. The same capture adapter calls original
functions and links separately against each renderer. Exactly one run per backend
and case was made, with no repeats. GPU dependency audits for native consumers pass.
Source, adapter, ROM and image hashes are recorded in `summary.json` and the case
folders. `build-check.txt` records the successful build; PPMs are actual displayed
GPU readbacks and PNGs are lossless conversions.

| Original example scene | Direct result |
|---|---|
| primitives: existing strip, fan, quad-strip and polygon cells | GL=0 on both; 4,591 of 96,000 pixels differ, maximum channel delta 1; no pixel delta above 1 |
| texture: original 16x16 F image and first square cell | GL=0 on both; full 400x240 images are byte-identical |
| cube: original 12x12 checker and textured first cube | C GL=0; Rust GL_INVALID_VALUE (1281), because level-zero NPOT uploads are not supported; 3,732 pixels differ, maximum delta 255 |

The primitive difference remains a failed exact comparison. C `glColor4f` converts
to byte colors immediately, while Rust currently captures float colors, making
color quantization a likely explanation, not a verified fix. No thresholds or
inputs were changed to produce a passing result. The cube's 12x12 image was not
replaced by a power-of-two texture.

These are selected original scene functions, not the complete interactive programs.
Unsupported original pages are omitted identically on both sides and explicitly
listed in `native/compare/README.md`; no missing GL API is stubbed. The complete
native build and symbol availability do not establish full legacy GL compatibility.
Next concrete gaps exposed by this comparison: NPOT textures and exact float-color
parity. Repeat/performance and independent hardware checks are not part of this run.
