> Correction, 2026-10-13: the functional GPU/image evidence below remains valid, but the C timing baseline used FLOAT arrays and did not hit its persistent GPU cache. These numbers therefore do not establish a matched-residency performance advantage. The corrected benchmark uses the C compact SHORT layout, explicitly asserts cache residency and reuse, and is recorded in the depth/culling checkpoint at `docs/native-results/depth-step/`.

# Arrays, indices and VBO results

The complete emulator checkpoint is **PASS**: three runs each of C, C++, rust3ds and the C reference, with full images, resource checks and repeated CPU gates. See [the complete report](full/README.md) and [machine-readable results](full/azahar-summary.json).

The files directly in this directory are the earlier C/C++ functional-only check and are superseded by `full/`. They did not establish the complete checkpoint. The discarded rust3ds f64 test payload is documented separately and excluded from the accepted results.
